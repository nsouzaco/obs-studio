/*
 * FFmpeg-based Video Player Implementation
 * Uses only FFmpeg (bundled with OBS) - no Qt Multimedia plugins required
 */

#include "video-player.hpp"

#include <QPainter>
#include <QResizeEvent>
#include <QAudioFormat>
#include <cmath>

/* ========================================================================== */
/* Video Decoder Thread                                                        */
/* ========================================================================== */

VideoDecoderThread::VideoDecoderThread(QObject *parent)
	: QThread(parent)
{
}

VideoDecoderThread::~VideoDecoderThread()
{
	stop();
	wait();
	closeFile();
}

bool VideoDecoderThread::openFile(const QString &path)
{
	closeFile();
	
	/* Open input file */
	if (avformat_open_input(&formatCtx, path.toUtf8().constData(), nullptr, nullptr) < 0) {
		emit errorOccurred("Could not open file: " + path);
		return false;
	}
	
	/* Get stream info */
	if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
		emit errorOccurred("Could not find stream info");
		closeFile();
		return false;
	}
	
	/* Find video and audio streams */
	for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
		AVCodecParameters *codecpar = formatCtx->streams[i]->codecpar;
		
		if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex < 0) {
			const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
			if (codec) {
				videoCodecCtx = avcodec_alloc_context3(codec);
				avcodec_parameters_to_context(videoCodecCtx, codecpar);
				if (avcodec_open2(videoCodecCtx, codec, nullptr) >= 0) {
					videoStreamIndex = i;
					videoWidth = codecpar->width;
					videoHeight = codecpar->height;
					videoTimeBase = av_q2d(formatCtx->streams[i]->time_base);
				} else {
					avcodec_free_context(&videoCodecCtx);
				}
			}
		}
		else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex < 0) {
			const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
			if (codec) {
				audioCodecCtx = avcodec_alloc_context3(codec);
				avcodec_parameters_to_context(audioCodecCtx, codecpar);
				if (avcodec_open2(audioCodecCtx, codec, nullptr) >= 0) {
					audioStreamIndex = i;
					audioTimeBase = av_q2d(formatCtx->streams[i]->time_base);
				} else {
					avcodec_free_context(&audioCodecCtx);
				}
			}
		}
	}
	
	if (videoStreamIndex < 0) {
		emit errorOccurred("No video stream found");
		closeFile();
		return false;
	}
	
	/* Setup video scaler (to RGB32) */
	swsCtx = sws_getContext(
		videoWidth, videoHeight, videoCodecCtx->pix_fmt,
		videoWidth, videoHeight, AV_PIX_FMT_RGB32,
		SWS_BILINEAR, nullptr, nullptr, nullptr
	);
	
	/* Setup audio resampler if audio exists */
	if (audioStreamIndex >= 0 && audioCodecCtx) {
		AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
		AVChannelLayout inLayout;
		
		if (audioCodecCtx->ch_layout.nb_channels > 0) {
			av_channel_layout_copy(&inLayout, &audioCodecCtx->ch_layout);
		} else {
			av_channel_layout_default(&inLayout, 2);
		}
		
		swr_alloc_set_opts2(&swrCtx,
			&outLayout, AV_SAMPLE_FMT_S16, 44100,
			&inLayout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate,
			0, nullptr);
		
		av_channel_layout_uninit(&inLayout);
		
		if (swrCtx) {
			swr_init(swrCtx);
		}
	}
	
	/* Get duration */
	if (formatCtx->duration != AV_NOPTS_VALUE) {
		duration = (double)formatCtx->duration / AV_TIME_BASE;
	}
	
	emit durationChanged(duration);
	return true;
}

void VideoDecoderThread::closeFile()
{
	stop();
	wait();
	
	if (swsCtx) {
		sws_freeContext(swsCtx);
		swsCtx = nullptr;
	}
	if (swrCtx) {
		swr_free(&swrCtx);
	}
	if (videoCodecCtx) {
		avcodec_free_context(&videoCodecCtx);
	}
	if (audioCodecCtx) {
		avcodec_free_context(&audioCodecCtx);
	}
	if (formatCtx) {
		avformat_close_input(&formatCtx);
	}
	
	videoStreamIndex = -1;
	audioStreamIndex = -1;
	videoWidth = 0;
	videoHeight = 0;
	duration = 0.0;
	currentPosition = 0.0;
}

void VideoDecoderThread::play()
{
	if (!formatCtx) return;
	
	playing = true;
	emit stateChanged(true);
	
	if (!isRunning()) {
		stopRequested = false;
		start();
	}
}

void VideoDecoderThread::pause()
{
	playing = false;
	emit stateChanged(false);
}

void VideoDecoderThread::stop()
{
	playing = false;
	stopRequested = true;
	emit stateChanged(false);
}

void VideoDecoderThread::seek(double seconds)
{
	QMutexLocker locker(&codecMutex);
	seekSequence++;  /* Increment sequence to invalidate stale updates */
	seekTarget = seconds;
	seekMode = SeekMode::Simple;
	seekInProgress = true;
	seekRequested = true;
}

void VideoDecoderThread::resetSeekState()
{
	QMutexLocker locker(&startupMutex);
	startupComplete = false;
	stopRequested = false;
}

bool VideoDecoderThread::waitForStartup(int timeoutMs)
{
	QMutexLocker locker(&startupMutex);
	if (startupComplete) return true;
	return startupCondition.wait(&startupMutex, timeoutMs);
}

void VideoDecoderThread::emitPositionThrottled(double pts)
{
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - lastPositionEmit).count();
	
	if (elapsed >= POSITION_UPDATE_INTERVAL_MS) {
		lastPositionEmit = now;
		emit positionChanged(pts, seekSequence.load());
	}
}

void VideoDecoderThread::recreateAudioResampler()
{
	/* Recreate swresample context for clean state after seek */
	if (swrCtx) {
		swr_free(&swrCtx);
	}
	
	if (audioCodecCtx) {
		AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
		AVChannelLayout inLayout;
		
		if (audioCodecCtx->ch_layout.nb_channels > 0) {
			av_channel_layout_copy(&inLayout, &audioCodecCtx->ch_layout);
		} else {
			av_channel_layout_default(&inLayout, 2);
		}
		
		swr_alloc_set_opts2(&swrCtx,
			&outLayout, AV_SAMPLE_FMT_S16, 44100,
			&inLayout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate,
			0, nullptr);
		
		av_channel_layout_uninit(&inLayout);
		
		if (swrCtx) {
			swr_init(swrCtx);
		}
	}
}

QImage VideoDecoderThread::convertFrameToImage(AVFrame *frame)
{
	QImage image(videoWidth, videoHeight, QImage::Format_RGB32);
	uint8_t *dstData[1] = { image.bits() };
	int dstLinesize[1] = { (int)image.bytesPerLine() };
	
	sws_scale(swsCtx, frame->data, frame->linesize,
		0, videoHeight, dstData, dstLinesize);
	
	return image;
}

void VideoDecoderThread::seekPastPosition(double targetSeconds)
{
	QMutexLocker locker(&codecMutex);
	seekSequence++;
	seekInProgress = true;
	
	/* Store target for the decode loop to handle */
	seekTarget = targetSeconds;
	seekMode = SeekMode::PastPosition;  /* THIS IS THE KEY FIX */
	seekRequested = true;
	
	/* performSeekPastPosition will handle sync - don't set sync flags here */
}

void VideoDecoderThread::performSeekPastPosition(double targetSeconds)
{
	/* This is called from decodeLoop when seekRequested is true.
	 * It performs a frame-accurate seek by:
	 * 1. Seeking to keyframe before target
	 * 2. Decoding and discarding frames until we reach/pass target
	 * 3. Emitting the first frame at/past target
	 */
	
	int64_t targetTs = (int64_t)(targetSeconds / videoTimeBase);
	
	/* 1. Seek to keyframe at or before target */
	int ret = av_seek_frame(formatCtx, videoStreamIndex, targetTs, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		seekInProgress = false;
		emit seekCompleted(currentPosition, false);
		return;
	}
	
	/* 2. Flush decoder state */
	avcodec_flush_buffers(videoCodecCtx);
	if (audioCodecCtx) {
		avcodec_flush_buffers(audioCodecCtx);
	}
	
	/* Recreate resampler to avoid residual audio artifacts */
	recreateAudioResampler();
	
	/* Notify main thread to flush audio output buffer */
	emit seekOccurred();
	
	/* 3. Decode and discard until we reach or pass target */
	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	double landedPts = targetSeconds;
	bool videoPastTarget = false;
	bool audioPastTarget = (audioStreamIndex < 0); /* No audio = already past */
	int discardedFrames = 0;  /* For debugging */
	
	while (!(videoPastTarget && audioPastTarget) && !stopRequested) {
		ret = av_read_frame(formatCtx, pkt);
		if (ret < 0) {
			/* EOF or error - accept where we are */
			break;
		}
		
		if (pkt->stream_index == videoStreamIndex && !videoPastTarget) {
			ret = avcodec_send_packet(videoCodecCtx, pkt);
			if (ret >= 0) {
				while (avcodec_receive_frame(videoCodecCtx, frame) == 0) {
					double pts = 0;
					if (frame->pts != AV_NOPTS_VALUE) {
						pts = frame->pts * videoTimeBase;
					}
					
					/* CRITICAL: Use >= targetSeconds with NO tolerance!
					 * This ensures we NEVER emit a frame inside the cut region.
					 * Even a 1ms tolerance can cause the playhead to land inside
					 * the cut region and trigger another skip attempt. */
					if (pts >= targetSeconds) {
						/* This frame is at or past target - we're done with video */
						landedPts = pts;
						videoPastTarget = true;
						
						/* Emit this frame as our first frame after seek */
						QImage img = convertFrameToImage(frame);
						currentPosition = pts;
						emit frameReady(img, pts);
						break;
					}
					/* Otherwise silently discard this frame */
					discardedFrames++;
				}
			}
		}
		else if (pkt->stream_index == audioStreamIndex && audioCodecCtx && !audioPastTarget) {
			/* Decode and discard audio until we're past target */
			ret = avcodec_send_packet(audioCodecCtx, pkt);
			if (ret >= 0) {
				AVFrame *audioFrame = av_frame_alloc();
				while (avcodec_receive_frame(audioCodecCtx, audioFrame) == 0) {
					double audioPts = 0;
					if (audioFrame->pts != AV_NOPTS_VALUE) {
						audioPts = audioFrame->pts * audioTimeBase;
					}
					
					/* Same rule: no tolerance */
					if (audioPts >= targetSeconds) {
						/* Audio is past target - done discarding */
						audioPastTarget = true;
						break;
					}
					/* Otherwise discard this audio frame */
				}
				av_frame_free(&audioFrame);
			}
		}
		
		av_packet_unref(pkt);
	}
	
	av_frame_free(&frame);
	av_packet_free(&pkt);
	
	/* 4. Update state */
	currentPosition = landedPts;
	
	/* Clear both sync flags since we handled seeking completely */
	videoSyncPending = false;
	audioSyncPending = false;
	seekInProgress = false;
	
	/* 5. Emit position and signal completion with actual landed position */
	emit positionChanged(landedPts, seekSequence.load());
	emit seekCompleted(landedPts, videoPastTarget);
}

void VideoDecoderThread::run()
{
	/* Signal that thread has started */
	{
		QMutexLocker locker(&startupMutex);
		startupComplete = true;
		startupCondition.wakeAll();
	}
	
	decodeLoop();
}

void VideoDecoderThread::decodeLoop()
{
	if (!formatCtx || videoStreamIndex < 0) return;
	
	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	
	double startTime = 0;
	double pausedTime = 0;
	bool wasPaused = false;
	
	/* Initialize position emit timestamp */
	lastPositionEmit = std::chrono::steady_clock::now();
	
	while (!stopRequested) {
		/* Handle seek - dispatch based on seek mode */
		if (seekRequested) {
			double target = seekTarget.load();
			SeekMode mode = seekMode.load();
			seekRequested = false;
			
			if (mode == SeekMode::PastPosition) {
				/* Frame-accurate seek: decode forward until past target.
				 * This is used for cut region skipping to guarantee we
				 * land PAST the target, not on a keyframe before it. */
				performSeekPastPosition(target);
				
				/* Reset timing after performSeekPastPosition sets currentPosition */
				startTime = av_gettime_relative() / 1000000.0 - currentPosition;
				seekMode = SeekMode::Simple;  /* Reset mode for next seek */
				continue;  /* Skip rest of this iteration */
			}
			
			/* Simple seek (original behavior) - seek to keyframe and use
			 * sync flags to discard frames before target */
			int64_t timestamp = (int64_t)(target / videoTimeBase);
			av_seek_frame(formatCtx, videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(videoCodecCtx);
			if (audioCodecCtx) {
				avcodec_flush_buffers(audioCodecCtx);
			}
			
			/* Recreate audio resampler for clean state */
			recreateAudioResampler();
			
			/* Set sync targets - discard frames before this point */
			videoSyncTarget = target;
			videoSyncPending = true;
			audioSyncTarget = target;
			audioSyncPending = true;
			
			/* Notify main thread to flush audio output buffer */
			emit seekOccurred();
			
			/* Reset timing baseline - but DON'T emit position yet!
			 * The actual position will come from the next decoded frame.
			 * This prevents the cut-region guard from being cleared prematurely. */
			startTime = av_gettime_relative() / 1000000.0 - target;
			seekInProgress = false;
		}
		
		/* Handle pause */
		if (!playing) {
			if (!wasPaused) {
				pausedTime = av_gettime_relative() / 1000000.0;
				wasPaused = true;
			}
			msleep(10);
			continue;
		}
		
		if (wasPaused) {
			startTime += av_gettime_relative() / 1000000.0 - pausedTime;
			wasPaused = false;
		}
		
		/* Read packet */
		int ret = av_read_frame(formatCtx, packet);
		if (ret < 0) {
			if (ret == AVERROR_EOF) {
				emit endOfFile();
				playing = false;
				emit stateChanged(false);
			}
			break;
		}
		
		if (packet->stream_index == videoStreamIndex) {
			/* Decode video */
			if (avcodec_send_packet(videoCodecCtx, packet) >= 0) {
				while (avcodec_receive_frame(videoCodecCtx, frame) >= 0) {
					/* Calculate PTS */
					double pts = 0;
					if (frame->pts != AV_NOPTS_VALUE) {
						pts = frame->pts * videoTimeBase;
					}
					
					/* After a seek, discard video frames that are before the target.
					 * This is critical for cut region skipping - we must not emit
					 * positions before the seek target, otherwise the skip guard
					 * won't work correctly. */
					if (videoSyncPending) {
						double syncTarget = videoSyncTarget.load();
						if (pts < syncTarget - 0.05) {
							/* Frame is before target - discard silently */
							continue;
						}
						videoSyncPending = false;
					}
					
					/* Sync to real time */
					double now = av_gettime_relative() / 1000000.0 - startTime;
					double delay = pts - now;
					if (delay > 0 && delay < 1.0) {
						msleep((unsigned long)(delay * 1000));
					}
					
					/* Convert to RGB */
					QImage image(videoWidth, videoHeight, QImage::Format_RGB32);
					uint8_t *dstData[1] = { image.bits() };
					int dstLinesize[1] = { (int)image.bytesPerLine() };
					
					sws_scale(swsCtx, frame->data, frame->linesize,
						0, videoHeight, dstData, dstLinesize);
					
					currentPosition = pts;
					emit frameReady(image, pts);
					/* Use throttled position updates to prevent signal flooding */
					emitPositionThrottled(pts);
				}
			}
		}
		else if (packet->stream_index == audioStreamIndex && audioCodecCtx && swrCtx) {
			/* Decode audio */
			if (avcodec_send_packet(audioCodecCtx, packet) >= 0) {
				AVFrame *audioFrame = av_frame_alloc();
				while (avcodec_receive_frame(audioCodecCtx, audioFrame) >= 0) {
					/* Calculate audio PTS */
					double audioPts = 0;
					if (audioFrame->pts != AV_NOPTS_VALUE) {
						audioPts = audioFrame->pts * audioTimeBase;
					}
					
					/* After a seek, discard audio frames that are before the target */
					if (audioSyncPending) {
						double syncTarget = audioSyncTarget.load();
						/* Allow small tolerance (50ms) for sync */
						if (audioPts < syncTarget - 0.05) {
							continue; /* Discard this frame */
						}
						audioSyncPending = false;
					}
					
					/* Calculate output sample count */
					int outSamples = swr_get_out_samples(swrCtx, audioFrame->nb_samples);
					if (outSamples <= 0) {
						continue;
					}
					
					/* Allocate output buffer for resampled audio */
					/* 2 channels * 2 bytes (16-bit) */
					int bufferSize = outSamples * 2 * 2;
					QByteArray audioData(bufferSize, 0);
					uint8_t *outBuffer = reinterpret_cast<uint8_t*>(audioData.data());
					
					/* Resample */
					int samplesConverted = swr_convert(swrCtx,
						&outBuffer, outSamples,
						(const uint8_t**)audioFrame->data, audioFrame->nb_samples);
					
					if (samplesConverted > 0) {
						/* Resize to actual converted size */
						int actualSize = samplesConverted * 2 * 2;
						audioData.resize(actualSize);
						emit audioReady(audioData);
					}
				}
				av_frame_free(&audioFrame);
			}
		}
		
		av_packet_unref(packet);
	}
	
	av_frame_free(&frame);
	av_packet_free(&packet);
}

/* ========================================================================== */
/* FFmpeg Video Player Widget                                                  */
/* ========================================================================== */

FFmpegVideoPlayer::FFmpegVideoPlayer(QWidget *parent)
	: QWidget(parent)
{
	setMinimumSize(320, 180);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setStyleSheet("background-color: black;");
	
	decoder = new VideoDecoderThread(this);
	
	connect(decoder, &VideoDecoderThread::frameReady, 
		this, &FFmpegVideoPlayer::onFrameReady);
	connect(decoder, &VideoDecoderThread::audioReady,
		this, &FFmpegVideoPlayer::onAudioReady);
	connect(decoder, &VideoDecoderThread::positionChanged,
		this, &FFmpegVideoPlayer::onPositionChanged);
	connect(decoder, &VideoDecoderThread::durationChanged,
		this, &FFmpegVideoPlayer::onDurationChanged);
	connect(decoder, &VideoDecoderThread::stateChanged,
		this, &FFmpegVideoPlayer::onStateChanged);
	connect(decoder, &VideoDecoderThread::seekOccurred,
		this, &FFmpegVideoPlayer::onSeekOccurred);
	connect(decoder, &VideoDecoderThread::seekCompleted,
		this, &FFmpegVideoPlayer::onSeekCompleted);
	connect(decoder, &VideoDecoderThread::endOfFile,
		this, &FFmpegVideoPlayer::onEndOfFile);
	connect(decoder, &VideoDecoderThread::errorOccurred,
		this, &FFmpegVideoPlayer::onError);
	
	setupAudio();
}

FFmpegVideoPlayer::~FFmpegVideoPlayer()
{
	closeFile();
	if (audioSink) {
		audioSink->stop();
		delete audioSink;
	}
}

void FFmpegVideoPlayer::setupAudio()
{
	QAudioFormat format;
	format.setSampleRate(44100);
	format.setChannelCount(2);
	format.setSampleFormat(QAudioFormat::Int16);
	
	audioSink = new QAudioSink(format, this);
	/* Use larger buffer (500ms) to handle seek transitions smoothly */
	audioSink->setBufferSize(44100 * 2 * 2 / 2);
	audioSink->setVolume(volume);
}

void FFmpegVideoPlayer::applyFadeIn(QByteArray &audioData)
{
	if (fadeInSamplesRemaining <= 0) return;
	
	int16_t *samples = reinterpret_cast<int16_t*>(audioData.data());
	int numSamples = audioData.size() / 2; /* 2 bytes per sample */
	int samplesToFade = qMin(numSamples, fadeInSamplesRemaining * 2); /* stereo */
	
	for (int i = 0; i < samplesToFade; i += 2) {
		/* Calculate fade factor (0.0 to 1.0) */
		float progress = 1.0f - (float)(fadeInSamplesRemaining * 2 - i) / (FADE_IN_SAMPLES * 2);
		/* Use smooth ease-in curve for natural fade */
		float factor = progress * progress; /* quadratic ease-in */
		
		samples[i] = (int16_t)(samples[i] * factor);     /* left */
		if (i + 1 < numSamples) {
			samples[i + 1] = (int16_t)(samples[i + 1] * factor); /* right */
		}
		
		if (i % 2 == 0) {
			fadeInSamplesRemaining--;
		}
	}
}

bool FFmpegVideoPlayer::openFile(const QString &path)
{
	closeFile();
	
	if (!decoder->openFile(path)) {
		return false;
	}
	
	videoWidth = decoder->getWidth();
	videoHeight = decoder->getHeight();
	
	/* Start audio output */
	if (audioSink && decoder->hasAudio()) {
		audioDevice = audioSink->start();
	}
	
	/* Seek to start to get first frame */
	decoder->seek(0);
	decoder->play();
	decoder->pause();
	
	return true;
}

void FFmpegVideoPlayer::closeFile()
{
	if (decoder) {
		decoder->stop();
		decoder->wait();
		decoder->closeFile();
	}
	if (audioSink) {
		audioSink->stop();
		audioDevice = nullptr;
	}
	currentFrame = QImage();
	scaledFrame = QImage();
	update();
}

void FFmpegVideoPlayer::play()
{
	if (decoder) {
		if (audioSink && decoder->hasAudio() && !audioDevice) {
			audioDevice = audioSink->start();
		}
		decoder->play();
	}
}

void FFmpegVideoPlayer::pause()
{
	if (decoder) {
		decoder->pause();
	}
}

void FFmpegVideoPlayer::stop()
{
	if (decoder) {
		decoder->stop();
		decoder->seek(0);
	}
	if (audioSink) {
		audioSink->stop();
		audioDevice = nullptr;
	}
}

void FFmpegVideoPlayer::seek(double seconds)
{
	if (!decoder) return;
	
	/* Update seek sequence before requesting seek */
	currentSeekSequence = decoder->getSeekSequence() + 1;
	
	/* If thread has exited (e.g., after EOF), restart it properly */
	if (!decoder->isRunning()) {
		/* Reset state before restarting */
		decoder->resetSeekState();
		decoder->seek(seconds);
		decoder->start();  /* Start the thread */
		
		/* Wait briefly for thread to begin processing (100ms timeout) */
		if (decoder->waitForStartup(100)) {
			/* Thread started, pause it if we weren't playing */
			if (!decoder->isPlaying()) {
				decoder->pause();
			}
		}
	} else {
		decoder->seek(seconds);
	}
}

void FFmpegVideoPlayer::seekPastPosition(double targetSeconds)
{
	if (!decoder) return;
	
	/* Update seek sequence before requesting seek */
	currentSeekSequence = decoder->getSeekSequence() + 1;
	
	/* If thread has exited, restart it */
	if (!decoder->isRunning()) {
		decoder->resetSeekState();
		decoder->seekPastPosition(targetSeconds);
		decoder->start();
		
		if (decoder->waitForStartup(100)) {
			if (!decoder->isPlaying()) {
				decoder->pause();
			}
		}
	} else {
		decoder->seekPastPosition(targetSeconds);
	}
}

bool FFmpegVideoPlayer::isSeekInProgress() const
{
	return decoder ? decoder->isSeekInProgress() : false;
}

void FFmpegVideoPlayer::setVolume(float vol)
{
	volume = qBound(0.0f, vol, 1.0f);
	if (audioSink) {
		audioSink->setVolume(volume);
	}
}

double FFmpegVideoPlayer::getDuration() const
{
	return decoder ? decoder->getDuration() : 0.0;
}

double FFmpegVideoPlayer::getPosition() const
{
	return decoder ? decoder->getPosition() : 0.0;
}

bool FFmpegVideoPlayer::isPlaying() const
{
	return decoder ? decoder->isPlaying() : false;
}

bool FFmpegVideoPlayer::hasVideo() const
{
	return decoder ? decoder->hasVideo() : false;
}

QSize FFmpegVideoPlayer::sizeHint() const
{
	if (videoWidth > 0 && videoHeight > 0) {
		return QSize(videoWidth, videoHeight);
	}
	return QSize(640, 360);
}

void FFmpegVideoPlayer::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.fillRect(rect(), Qt::black);
	
	if (scaledFrame.isNull()) {
		/* Draw placeholder text */
		painter.setPen(Qt::gray);
		painter.drawText(rect(), Qt::AlignCenter, "No video loaded");
		return;
	}
	
	/* Center the frame */
	int x = (width() - scaledFrame.width()) / 2;
	int y = (height() - scaledFrame.height()) / 2;
	painter.drawImage(x, y, scaledFrame);
}

void FFmpegVideoPlayer::resizeEvent(QResizeEvent *)
{
	if (!currentFrame.isNull()) {
		/* Scale frame to fit widget while maintaining aspect ratio */
		scaledFrame = currentFrame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
}

void FFmpegVideoPlayer::onFrameReady(const QImage &frame, double pts)
{
	Q_UNUSED(pts);
	currentFrame = frame;
	scaledFrame = currentFrame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
	update();
}

void FFmpegVideoPlayer::onAudioReady(const QByteArray &data)
{
	if (audioDevice && audioSink && audioSink->state() != QAudio::StoppedState) {
		QByteArray audioData = data;
		
		/* Apply fade-in if we just seeked to prevent audio pops */
		if (fadeInSamplesRemaining > 0) {
			applyFadeIn(audioData);
		}
		
		/* Write all data, handling partial writes */
		qint64 written = 0;
		const char *ptr = audioData.constData();
		qint64 remaining = audioData.size();
		
		while (remaining > 0) {
			qint64 w = audioDevice->write(ptr + written, remaining);
			if (w <= 0) break;
			written += w;
			remaining -= w;
		}
	}
}

void FFmpegVideoPlayer::onPositionChanged(double seconds, uint64_t sequence)
{
	/* Ignore stale position updates from before our last seek */
	if (sequence < currentSeekSequence) {
		return;
	}
	currentSeekSequence = sequence;
	emit positionChanged((qint64)(seconds * 1000));
}

void FFmpegVideoPlayer::onDurationChanged(double seconds)
{
	emit durationChanged((qint64)(seconds * 1000));
}

void FFmpegVideoPlayer::onStateChanged(bool isPlaying)
{
	emit playbackStateChanged(isPlaying);
}

void FFmpegVideoPlayer::onSeekOccurred()
{
	/* Flush audio output buffer to prevent crackling from old audio data */
	if (audioSink) {
		/* 1. Stop audio output completely */
		audioSink->stop();
		
		/* 2. Restart audio output fresh */
		audioDevice = audioSink->start();
		
		if (audioDevice) {
			/* 3. Insert ~20ms of silence for smooth ramping */
			/* 44100 Hz * 2 channels * 2 bytes * 0.02 sec = ~3528 bytes */
			QByteArray silence(3528, 0);
			audioDevice->write(silence);
		}
	}
	
	/* 4. Enable fade-in for the next real audio samples after seek */
	fadeInSamplesRemaining = FADE_IN_SAMPLES;
}

void FFmpegVideoPlayer::onSeekCompleted(double actualPosition, bool success)
{
	/* Forward seek completion to listeners for coordination */
	emit seekCompleted(actualPosition, success);
}

void FFmpegVideoPlayer::onEndOfFile()
{
	/* Stop at end */
	if (audioSink) {
		audioSink->stop();
		audioDevice = nullptr;
	}
}

void FFmpegVideoPlayer::onError(const QString &error)
{
	emit errorOccurred(error);
}

