/*
 * FFmpeg Utilities Implementation
 * Native audio extraction using FFmpeg C APIs
 */

#include "ffmpeg-utils.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>

double FFmpegAudioExtractor::getDuration(const std::string &filePath)
{
	AVFormatContext *formatCtx = nullptr;
	
	if (avformat_open_input(&formatCtx, filePath.c_str(), nullptr, nullptr) != 0) {
		return 0.0;
	}
	
	if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
		avformat_close_input(&formatCtx);
		return 0.0;
	}
	
	double duration = 0.0;
	if (formatCtx->duration != AV_NOPTS_VALUE) {
		duration = (double)formatCtx->duration / AV_TIME_BASE;
	}
	
	avformat_close_input(&formatCtx);
	return duration;
}

FFmpegAudioExtractor::WaveformResult FFmpegAudioExtractor::extractWaveform(
	const std::string &filePath,
	int targetSamples,
	std::function<void(int)> progressCallback)
{
	WaveformResult result;
	result.success = false;
	result.duration = 0.0;
	
	AVFormatContext *formatCtx = nullptr;
	AVCodecContext *codecCtx = nullptr;
	SwrContext *swrCtx = nullptr;
	AVFrame *frame = nullptr;
	AVPacket *packet = nullptr;
	
	/* Open input file */
	if (avformat_open_input(&formatCtx, filePath.c_str(), nullptr, nullptr) != 0) {
		result.error = "Could not open file";
		return result;
	}
	
	/* Get stream info */
	if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
		result.error = "Could not find stream info";
		avformat_close_input(&formatCtx);
		return result;
	}
	
	/* Find audio stream */
	int audioStreamIndex = -1;
	const AVCodec *codec = nullptr;
	
	for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
		if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStreamIndex = i;
			codec = avcodec_find_decoder(formatCtx->streams[i]->codecpar->codec_id);
			break;
		}
	}
	
	if (audioStreamIndex < 0 || !codec) {
		result.error = "No audio stream found";
		avformat_close_input(&formatCtx);
		return result;
	}
	
	/* Get duration */
	if (formatCtx->duration != AV_NOPTS_VALUE) {
		result.duration = (double)formatCtx->duration / AV_TIME_BASE;
	}
	
	/* Allocate codec context */
	codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx) {
		result.error = "Could not allocate codec context";
		avformat_close_input(&formatCtx);
		return result;
	}
	
	/* Copy codec parameters */
	if (avcodec_parameters_to_context(codecCtx, formatCtx->streams[audioStreamIndex]->codecpar) < 0) {
		result.error = "Could not copy codec parameters";
		avcodec_free_context(&codecCtx);
		avformat_close_input(&formatCtx);
		return result;
	}
	
	/* Open codec */
	if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
		result.error = "Could not open codec";
		avcodec_free_context(&codecCtx);
		avformat_close_input(&formatCtx);
		return result;
	}
	
	/* Setup resampler to convert to mono s16 at 8kHz */
	AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_MONO;
	AVChannelLayout inLayout;
	
	if (codecCtx->ch_layout.nb_channels > 0) {
		av_channel_layout_copy(&inLayout, &codecCtx->ch_layout);
	} else {
		av_channel_layout_default(&inLayout, 2);
	}
	
	int ret = swr_alloc_set_opts2(&swrCtx,
		&outLayout, AV_SAMPLE_FMT_S16, 8000,
		&inLayout, codecCtx->sample_fmt, codecCtx->sample_rate,
		0, nullptr);
	
	av_channel_layout_uninit(&inLayout);
	
	if (ret < 0 || swr_init(swrCtx) < 0) {
		result.error = "Could not initialize resampler";
		if (swrCtx) swr_free(&swrCtx);
		avcodec_free_context(&codecCtx);
		avformat_close_input(&formatCtx);
		return result;
	}
	
	/* Allocate frame and packet */
	frame = av_frame_alloc();
	packet = av_packet_alloc();
	
	if (!frame || !packet) {
		result.error = "Could not allocate frame/packet";
		if (frame) av_frame_free(&frame);
		if (packet) av_packet_free(&packet);
		swr_free(&swrCtx);
		avcodec_free_context(&codecCtx);
		avformat_close_input(&formatCtx);
		return result;
	}
	
	/* Collect all audio samples */
	std::vector<int16_t> allSamples;
	allSamples.reserve(8000 * (int)result.duration); /* Estimate */
	
	int64_t totalPackets = formatCtx->streams[audioStreamIndex]->nb_frames;
	if (totalPackets <= 0) totalPackets = 1000; /* Estimate */
	int64_t processedPackets = 0;
	
	while (av_read_frame(formatCtx, packet) >= 0) {
		if (packet->stream_index == audioStreamIndex) {
			/* Send packet to decoder */
			if (avcodec_send_packet(codecCtx, packet) >= 0) {
				/* Receive decoded frames */
				while (avcodec_receive_frame(codecCtx, frame) >= 0) {
					/* Resample to mono s16 8kHz */
					int outSamples = av_rescale_rnd(
						swr_get_delay(swrCtx, codecCtx->sample_rate) + frame->nb_samples,
						8000, codecCtx->sample_rate, AV_ROUND_UP);
					
					std::vector<int16_t> buffer(outSamples);
					uint8_t *outPtr = (uint8_t *)buffer.data();
					
					int converted = swr_convert(swrCtx,
						&outPtr, outSamples,
						(const uint8_t **)frame->extended_data, frame->nb_samples);
					
					if (converted > 0) {
						allSamples.insert(allSamples.end(), buffer.begin(), buffer.begin() + converted);
					}
				}
			}
			processedPackets++;
			
			/* Report progress */
			if (progressCallback && totalPackets > 0) {
				int progress = (int)(processedPackets * 100 / totalPackets);
				progressCallback(std::min(progress, 99));
			}
		}
		av_packet_unref(packet);
	}
	
	/* Flush decoder */
	avcodec_send_packet(codecCtx, nullptr);
	while (avcodec_receive_frame(codecCtx, frame) >= 0) {
		int outSamples = av_rescale_rnd(
			swr_get_delay(swrCtx, codecCtx->sample_rate) + frame->nb_samples,
			8000, codecCtx->sample_rate, AV_ROUND_UP);
		
		std::vector<int16_t> buffer(outSamples);
		uint8_t *outPtr = (uint8_t *)buffer.data();
		
		int converted = swr_convert(swrCtx,
			&outPtr, outSamples,
			(const uint8_t **)frame->extended_data, frame->nb_samples);
		
		if (converted > 0) {
			allSamples.insert(allSamples.end(), buffer.begin(), buffer.begin() + converted);
		}
	}
	
	/* Cleanup FFmpeg resources */
	av_frame_free(&frame);
	av_packet_free(&packet);
	swr_free(&swrCtx);
	avcodec_free_context(&codecCtx);
	avformat_close_input(&formatCtx);
	
	/* Calculate waveform from samples */
	if (allSamples.empty()) {
		result.samples.resize(100, 0.1f);
		result.success = true;
		return result;
	}
	
	/* Calculate actual number of samples based on duration */
	int numSamples = std::max(100, std::min(targetSamples, (int)(result.duration * 4)));
	int samplesPerSegment = std::max(1, (int)allSamples.size() / numSamples);
	
	result.samples.reserve(numSamples);
	
	for (int i = 0; i < numSamples; i++) {
		size_t startIdx = i * samplesPerSegment;
		size_t endIdx = std::min(startIdx + samplesPerSegment, allSamples.size());
		
		if (startIdx >= allSamples.size()) {
			result.samples.push_back(0.0f);
			continue;
		}
		
		/* Get peak amplitude in segment */
		int16_t maxVal = 0;
		for (size_t j = startIdx; j < endIdx; j++) {
			int16_t absVal = std::abs(allSamples[j]);
			if (absVal > maxVal) maxVal = absVal;
		}
		
		/* Normalize to 0-1 */
		float amplitude = (float)maxVal / 32767.0f;
		
		/* Compress dynamic range for better visualization */
		amplitude = std::pow(amplitude, 0.6f);
		
		result.samples.push_back(amplitude);
	}
	
	/* Normalize to use full range */
	float maxAmp = *std::max_element(result.samples.begin(), result.samples.end());
	if (maxAmp > 0) {
		for (float &amp : result.samples) {
			amp = std::min(1.0f, amp / maxAmp);
		}
	}
	
	if (progressCallback) {
		progressCallback(100);
	}
	
	result.success = true;
	return result;
}


