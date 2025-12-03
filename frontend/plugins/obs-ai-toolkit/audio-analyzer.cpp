/*
 * Audio Analyzer Implementation using FFmpeg
 */

#include "audio-analyzer.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include <cmath>
#include <algorithm>

struct FFmpegAudioAnalyzer::Impl {
	AVFormatContext *formatCtx = nullptr;
	AVCodecContext *codecCtx = nullptr;
	SwrContext *swrCtx = nullptr;
	int audioStreamIndex = -1;
	double duration = 0.0;
};

FFmpegAudioAnalyzer::FFmpegAudioAnalyzer()
	: impl(new Impl())
{
}

FFmpegAudioAnalyzer::~FFmpegAudioAnalyzer()
{
	close();
	delete impl;
}

bool FFmpegAudioAnalyzer::open(const std::string &filePath)
{
	close();

	/* Open input file */
	if (avformat_open_input(&impl->formatCtx, filePath.c_str(), nullptr, nullptr) < 0) {
		return false;
	}

	/* Find stream info */
	if (avformat_find_stream_info(impl->formatCtx, nullptr) < 0) {
		close();
		return false;
	}

	/* Find audio stream */
	for (unsigned int i = 0; i < impl->formatCtx->nb_streams; i++) {
		if (impl->formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			impl->audioStreamIndex = i;
			break;
		}
	}

	if (impl->audioStreamIndex < 0) {
		close();
		return false;
	}

	/* Get codec */
	AVCodecParameters *codecpar = impl->formatCtx->streams[impl->audioStreamIndex]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
	if (!codec) {
		close();
		return false;
	}

	/* Create codec context */
	impl->codecCtx = avcodec_alloc_context3(codec);
	if (!impl->codecCtx) {
		close();
		return false;
	}

	if (avcodec_parameters_to_context(impl->codecCtx, codecpar) < 0) {
		close();
		return false;
	}

	if (avcodec_open2(impl->codecCtx, codec, nullptr) < 0) {
		close();
		return false;
	}

	/* Calculate duration */
	impl->duration = (double)impl->formatCtx->duration / AV_TIME_BASE;

	/* Setup resampler for float output */
	AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_MONO;
	
	if (swr_alloc_set_opts2(&impl->swrCtx,
				&outLayout,
				AV_SAMPLE_FMT_FLT,
				48000,
				&impl->codecCtx->ch_layout,
				impl->codecCtx->sample_fmt,
				impl->codecCtx->sample_rate,
				0, nullptr) < 0) {
		close();
		return false;
	}

	if (swr_init(impl->swrCtx) < 0) {
		close();
		return false;
	}

	return true;
}

void FFmpegAudioAnalyzer::close()
{
	if (impl->swrCtx) {
		swr_free(&impl->swrCtx);
		impl->swrCtx = nullptr;
	}
	if (impl->codecCtx) {
		avcodec_free_context(&impl->codecCtx);
		impl->codecCtx = nullptr;
	}
	if (impl->formatCtx) {
		avformat_close_input(&impl->formatCtx);
		impl->formatCtx = nullptr;
	}
	impl->audioStreamIndex = -1;
	impl->duration = 0.0;
}

double FFmpegAudioAnalyzer::getDuration() const
{
	return impl->duration;
}

std::vector<AudioPeak> FFmpegAudioAnalyzer::findPeaks(
	double thresholdDb,
	double minDuration,
	std::function<void(int)> progressCallback)
{
	std::vector<AudioPeak> peaks;

	if (!impl->formatCtx || impl->audioStreamIndex < 0)
		return peaks;

	/* Convert dB threshold to linear */
	double thresholdLinear = pow(10.0, thresholdDb / 20.0);

	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	/* State for peak detection */
	bool inPeak = false;
	double peakStart = 0.0;
	double peakMax = 0.0;
	double currentTime = 0.0;
	int64_t totalSamples = 0;

	/* Time base for the audio stream */
	AVRational timeBase = impl->formatCtx->streams[impl->audioStreamIndex]->time_base;

	while (av_read_frame(impl->formatCtx, packet) >= 0) {
		if (packet->stream_index == impl->audioStreamIndex) {
			if (avcodec_send_packet(impl->codecCtx, packet) >= 0) {
				while (avcodec_receive_frame(impl->codecCtx, frame) >= 0) {
					/* Get timestamp */
					if (frame->pts != AV_NOPTS_VALUE) {
						currentTime = (double)frame->pts * av_q2d(timeBase);
					}

					/* Resample to float */
					float *outBuf = nullptr;
					int outSamples = swr_get_out_samples(impl->swrCtx, frame->nb_samples);
					av_samples_alloc((uint8_t **)&outBuf, nullptr, 1,
							 outSamples, AV_SAMPLE_FMT_FLT, 0);

					int converted = swr_convert(impl->swrCtx,
								    (uint8_t **)&outBuf, outSamples,
								    (const uint8_t **)frame->data,
								    frame->nb_samples);

					/* Analyze samples */
					for (int i = 0; i < converted; i++) {
						float sample = fabs(outBuf[i]);
						double sampleTime = currentTime + (double)i / 48000.0;

						if (sample >= thresholdLinear) {
							if (!inPeak) {
								inPeak = true;
								peakStart = sampleTime;
								peakMax = sample;
							} else {
								peakMax = std::max(peakMax, (double)sample);
							}
						} else if (inPeak) {
							double peakDuration = sampleTime - peakStart;
							if (peakDuration >= minDuration) {
								AudioPeak peak;
								peak.timestamp = peakStart;
								peak.level = 20.0 * log10(peakMax);
								peak.duration = peakDuration;
								peaks.push_back(peak);
							}
							inPeak = false;
							peakMax = 0.0;
						}
					}

					totalSamples += converted;
					av_freep(&outBuf);
				}
			}

			/* Report progress */
			if (progressCallback && impl->duration > 0) {
				int progress = (int)(currentTime / impl->duration * 100);
				progressCallback(progress);
			}
		}
		av_packet_unref(packet);
	}

	/* Handle final peak if still in one */
	if (inPeak) {
		double peakDuration = currentTime - peakStart;
		if (peakDuration >= minDuration) {
			AudioPeak peak;
			peak.timestamp = peakStart;
			peak.level = 20.0 * log10(peakMax);
			peak.duration = peakDuration;
			peaks.push_back(peak);
		}
	}

	av_frame_free(&frame);
	av_packet_free(&packet);

	/* Seek back to beginning for potential re-analysis */
	av_seek_frame(impl->formatCtx, impl->audioStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(impl->codecCtx);

	return peaks;
}

