/*
 * Whisper.cpp Utilities Implementation
 * Native speech-to-text using whisper.cpp
 */

#include "whisper-utils.hpp"
#include "ffmpeg-utils.hpp"

#include <whisper.h>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QUrl>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <cmath>
#include <algorithm>

/* Model download URLs from Hugging Face */
#define WHISPER_MODEL_URL(model) "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" model

std::vector<WhisperModelInfo> WhisperTranscriber::getAvailableModels()
{
	return {
		{"tiny",   "ggml-tiny.bin",   WHISPER_MODEL_URL("ggml-tiny.bin"),   75000000},
		{"base",   "ggml-base.bin",   WHISPER_MODEL_URL("ggml-base.bin"),   142000000},
		{"small",  "ggml-small.bin",  WHISPER_MODEL_URL("ggml-small.bin"),  466000000},
		{"medium", "ggml-medium.bin", WHISPER_MODEL_URL("ggml-medium.bin"), 1530000000},
	};
}

std::string WhisperTranscriber::getModelsDir()
{
	QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	QDir dir(dataPath);
	if (!dir.exists("whisper-models")) {
		dir.mkpath("whisper-models");
	}
	return (dataPath + "/whisper-models").toStdString();
}

bool WhisperTranscriber::isModelDownloaded(const std::string &modelName)
{
	std::string path = getModelPath(modelName);
	return QFile::exists(QString::fromStdString(path));
}

std::string WhisperTranscriber::getModelPath(const std::string &modelName)
{
	auto models = getAvailableModels();
	for (const auto &model : models) {
		if (model.name == modelName) {
			return getModelsDir() + "/" + model.filename;
		}
	}
	return "";
}

bool WhisperTranscriber::downloadModel(
	const std::string &modelName,
	std::function<void(int, const std::string&)> progressCallback)
{
	auto models = getAvailableModels();
	WhisperModelInfo targetModel;
	bool found = false;
	
	for (const auto &model : models) {
		if (model.name == modelName) {
			targetModel = model;
			found = true;
			break;
		}
	}
	
	if (!found) {
		if (progressCallback) {
			progressCallback(0, "Unknown model: " + modelName);
		}
		return false;
	}
	
	std::string destPath = getModelsDir() + "/" + targetModel.filename;
	
	/* Check if already exists */
	if (QFile::exists(QString::fromStdString(destPath))) {
		if (progressCallback) {
			progressCallback(100, "Model already downloaded");
		}
		return true;
	}
	
	if (progressCallback) {
		progressCallback(0, "Downloading " + modelName + " model...");
	}
	
	/* Download using curl command line for reliability */
	QString curlCmd = QString("curl -L -o \"%1\" \"%2\"")
		.arg(QString::fromStdString(destPath))
		.arg(QString::fromStdString(targetModel.url));
	
	/* Create the models directory if it doesn't exist */
	QDir().mkpath(QString::fromStdString(getModelsDir()));
	
	/* Use QProcess for download with progress tracking */
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	
	/* Try curl first, fall back to wget */
	QStringList curlArgs = {
		"-L",  /* Follow redirects */
		"-k",  /* Allow insecure connections (skip cert verification) */
		"-o", QString::fromStdString(destPath),
		"--progress-bar",
		QString::fromStdString(targetModel.url)
	};
	
	process.start("curl", curlArgs);
	
	if (!process.waitForStarted(5000)) {
		/* Try wget as fallback */
		QStringList wgetArgs = {
			"-O", QString::fromStdString(destPath),
			QString::fromStdString(targetModel.url)
		};
		process.start("wget", wgetArgs);
		
		if (!process.waitForStarted(5000)) {
			if (progressCallback) {
				progressCallback(0, "Neither curl nor wget available for download");
			}
			return false;
		}
	}
	
	/* Wait for download with progress updates */
	int lastProgress = 0;
	while (!process.waitForFinished(1000)) {
		lastProgress = std::min(99, lastProgress + 2);
		if (progressCallback) {
			progressCallback(lastProgress, 
				QString("Downloading %1 model...")
					.arg(QString::fromStdString(modelName)).toStdString());
		}
	}
	
	if (process.exitCode() != 0) {
		if (progressCallback) {
			progressCallback(0, "Download failed: " + process.readAllStandardOutput().toStdString());
		}
		/* Clean up partial download */
		QFile::remove(QString::fromStdString(destPath));
		return false;
	}
	
	/* Verify the file exists and has content */
	QFile downloadedFile(QString::fromStdString(destPath));
	if (!downloadedFile.exists() || downloadedFile.size() < 1000000) {
		if (progressCallback) {
			progressCallback(0, "Download incomplete or file too small");
		}
		QFile::remove(QString::fromStdString(destPath));
		return false;
	}
	
	if (progressCallback) {
		progressCallback(100, "Model downloaded successfully");
	}
	
	return true;
}

WhisperTranscriber::WhisperTranscriber()
{
}

WhisperTranscriber::~WhisperTranscriber()
{
	unloadModel();
}

bool WhisperTranscriber::loadModel(const std::string &modelName)
{
	if (ctx && loadedModelName == modelName) {
		return true; /* Already loaded */
	}
	
	unloadModel();
	
	std::string modelPath = getModelPath(modelName);
	if (modelPath.empty() || !QFile::exists(QString::fromStdString(modelPath))) {
		return false;
	}
	
	struct whisper_context_params cparams = whisper_context_default_params();
	ctx = whisper_init_from_file_with_params(modelPath.c_str(), cparams);
	
	if (ctx) {
		loadedModelName = modelName;
		return true;
	}
	
	return false;
}

bool WhisperTranscriber::isModelLoaded() const
{
	return ctx != nullptr;
}

void WhisperTranscriber::unloadModel()
{
	if (ctx) {
		whisper_free((whisper_context *)ctx);
		ctx = nullptr;
		loadedModelName.clear();
	}
}

/* Helper: Extract audio as float samples at 16kHz mono (whisper format) */
static std::vector<float> extractAudioForWhisper(
	const std::string &filePath,
	std::function<void(int, const std::string&)> progressCallback)
{
	std::vector<float> audioData;
	
	AVFormatContext *formatCtx = nullptr;
	AVCodecContext *codecCtx = nullptr;
	SwrContext *swrCtx = nullptr;
	AVFrame *frame = nullptr;
	AVPacket *packet = nullptr;
	
	if (progressCallback) {
		progressCallback(5, "Opening audio file...");
	}
	
	/* Open input file */
	if (avformat_open_input(&formatCtx, filePath.c_str(), nullptr, nullptr) != 0) {
		return audioData;
	}
	
	if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
		avformat_close_input(&formatCtx);
		return audioData;
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
		avformat_close_input(&formatCtx);
		return audioData;
	}
	
	/* Allocate codec context */
	codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx) {
		avformat_close_input(&formatCtx);
		return audioData;
	}
	
	avcodec_parameters_to_context(codecCtx, formatCtx->streams[audioStreamIndex]->codecpar);
	
	if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
		avcodec_free_context(&codecCtx);
		avformat_close_input(&formatCtx);
		return audioData;
	}
	
	/* Setup resampler: output is 16kHz mono float (whisper format) */
	AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_MONO;
	AVChannelLayout inLayout;
	
	if (codecCtx->ch_layout.nb_channels > 0) {
		av_channel_layout_copy(&inLayout, &codecCtx->ch_layout);
	} else {
		av_channel_layout_default(&inLayout, 2);
	}
	
	int ret = swr_alloc_set_opts2(&swrCtx,
		&outLayout, AV_SAMPLE_FMT_FLT, WHISPER_SAMPLE_RATE,
		&inLayout, codecCtx->sample_fmt, codecCtx->sample_rate,
		0, nullptr);
	
	av_channel_layout_uninit(&inLayout);
	
	if (ret < 0 || swr_init(swrCtx) < 0) {
		if (swrCtx) swr_free(&swrCtx);
		avcodec_free_context(&codecCtx);
		avformat_close_input(&formatCtx);
		return audioData;
	}
	
	frame = av_frame_alloc();
	packet = av_packet_alloc();
	
	if (!frame || !packet) {
		if (frame) av_frame_free(&frame);
		if (packet) av_packet_free(&packet);
		swr_free(&swrCtx);
		avcodec_free_context(&codecCtx);
		avformat_close_input(&formatCtx);
		return audioData;
	}
	
	/* Get total duration for progress */
	double totalDuration = 0;
	if (formatCtx->duration != AV_NOPTS_VALUE) {
		totalDuration = (double)formatCtx->duration / AV_TIME_BASE;
	}
	
	/* Process audio */
	double processedTime = 0;
	
	while (av_read_frame(formatCtx, packet) >= 0) {
		if (packet->stream_index == audioStreamIndex) {
			if (avcodec_send_packet(codecCtx, packet) >= 0) {
				while (avcodec_receive_frame(codecCtx, frame) >= 0) {
					int outSamples = av_rescale_rnd(
						swr_get_delay(swrCtx, codecCtx->sample_rate) + frame->nb_samples,
						WHISPER_SAMPLE_RATE, codecCtx->sample_rate, AV_ROUND_UP);
					
					std::vector<float> buffer(outSamples);
					uint8_t *outPtr = (uint8_t *)buffer.data();
					
					int converted = swr_convert(swrCtx,
						&outPtr, outSamples,
						(const uint8_t **)frame->extended_data, frame->nb_samples);
					
					if (converted > 0) {
						audioData.insert(audioData.end(), buffer.begin(), buffer.begin() + converted);
					}
					
					/* Update progress */
					processedTime += (double)frame->nb_samples / codecCtx->sample_rate;
					if (progressCallback && totalDuration > 0) {
						int progress = 5 + (int)(processedTime / totalDuration * 20);
						progressCallback(std::min(progress, 25), "Extracting audio...");
					}
				}
			}
		}
		av_packet_unref(packet);
	}
	
	/* Flush */
	avcodec_send_packet(codecCtx, nullptr);
	while (avcodec_receive_frame(codecCtx, frame) >= 0) {
		int outSamples = av_rescale_rnd(
			swr_get_delay(swrCtx, codecCtx->sample_rate) + frame->nb_samples,
			WHISPER_SAMPLE_RATE, codecCtx->sample_rate, AV_ROUND_UP);
		
		std::vector<float> buffer(outSamples);
		uint8_t *outPtr = (uint8_t *)buffer.data();
		
		int converted = swr_convert(swrCtx,
			&outPtr, outSamples,
			(const uint8_t **)frame->extended_data, frame->nb_samples);
		
		if (converted > 0) {
			audioData.insert(audioData.end(), buffer.begin(), buffer.begin() + converted);
		}
	}
	
	/* Cleanup */
	av_frame_free(&frame);
	av_packet_free(&packet);
	swr_free(&swrCtx);
	avcodec_free_context(&codecCtx);
	avformat_close_input(&formatCtx);
	
	return audioData;
}

WhisperResult WhisperTranscriber::transcribe(
	const std::string &audioPath,
	const std::string &language,
	std::function<void(int, const std::string&)> progressCallback)
{
	WhisperResult result;
	result.success = false;
	
	if (!ctx) {
		result.error = "Model not loaded";
		return result;
	}
	
	/* Extract audio */
	std::vector<float> audioData = extractAudioForWhisper(audioPath, progressCallback);
	
	if (audioData.empty()) {
		result.error = "Could not extract audio from file";
		return result;
	}
	
	if (progressCallback) {
		progressCallback(30, "Transcribing...");
	}
	
	/* Setup whisper parameters */
	struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
	
	wparams.print_realtime = false;
	wparams.print_progress = false;
	wparams.print_timestamps = false;
	wparams.print_special = false;
	wparams.translate = false;
	wparams.no_context = true;
	wparams.single_segment = false;
	wparams.token_timestamps = true;
	
	/* Set language if specified */
	if (!language.empty() && language != "auto") {
		wparams.language = language.c_str();
	} else {
		wparams.language = nullptr; /* Auto-detect */
	}
	
	/* Progress callback wrapper */
	struct ProgressData {
		std::function<void(int, const std::string&)> callback;
	};
	ProgressData progressData = {progressCallback};
	
	wparams.progress_callback = [](struct whisper_context *, struct whisper_state *, int progress, void *user_data) {
		ProgressData *pd = (ProgressData *)user_data;
		if (pd && pd->callback) {
			int adjustedProgress = 30 + (progress * 65 / 100);
			pd->callback(adjustedProgress, "Transcribing...");
		}
	};
	wparams.progress_callback_user_data = &progressData;
	
	/* Run transcription */
	whisper_context *wctx = (whisper_context *)ctx;
	
	int ret = whisper_full(wctx, wparams, audioData.data(), (int)audioData.size());
	
	if (ret != 0) {
		result.error = "Transcription failed";
		return result;
	}
	
	/* Get detected language */
	int langId = whisper_full_lang_id(wctx);
	if (langId >= 0) {
		result.detectedLanguage = whisper_lang_str(langId);
	}
	
	/* Extract segments */
	int numSegments = whisper_full_n_segments(wctx);
	
	for (int i = 0; i < numSegments; i++) {
		WhisperSegment seg;
		seg.start = (double)whisper_full_get_segment_t0(wctx, i) / 100.0;
		seg.end = (double)whisper_full_get_segment_t1(wctx, i) / 100.0;
		seg.text = whisper_full_get_segment_text(wctx, i);
		
		/* Trim whitespace */
		size_t start = seg.text.find_first_not_of(" \t\n\r");
		size_t end = seg.text.find_last_not_of(" \t\n\r");
		if (start != std::string::npos && end != std::string::npos) {
			seg.text = seg.text.substr(start, end - start + 1);
		}
		
		if (!seg.text.empty()) {
			result.segments.push_back(seg);
		}
	}
	
	if (progressCallback) {
		progressCallback(100, "Transcription complete");
	}
	
	result.success = true;
	return result;
}

