/*
 * Whisper.cpp Utilities for OBS AI Toolkit
 * Native speech-to-text without external dependencies
 */

#pragma once

#include <string>
#include <vector>
#include <functional>

/* Transcription segment */
struct WhisperSegment {
	double start;
	double end;
	std::string text;
};

/* Transcription result */
struct WhisperResult {
	std::vector<WhisperSegment> segments;
	bool success;
	std::string error;
	std::string detectedLanguage;
};

/* Whisper model info */
struct WhisperModelInfo {
	std::string name;
	std::string filename;
	std::string url;
	size_t size; /* in bytes */
};

/* Whisper transcription wrapper */
class WhisperTranscriber {
public:
	WhisperTranscriber();
	~WhisperTranscriber();

	/*
	 * Get available models
	 */
	static std::vector<WhisperModelInfo> getAvailableModels();

	/*
	 * Get the models directory path
	 */
	static std::string getModelsDir();

	/*
	 * Check if a model is downloaded
	 */
	static bool isModelDownloaded(const std::string &modelName);

	/*
	 * Get the path to a model file
	 */
	static std::string getModelPath(const std::string &modelName);

	/*
	 * Download a model (blocking)
	 * @param modelName Model name (tiny, base, small, medium)
	 * @param progressCallback Progress callback (0-100)
	 * @return true if successful
	 */
	static bool downloadModel(
		const std::string &modelName,
		std::function<void(int, const std::string&)> progressCallback = nullptr
	);

	/*
	 * Load a model
	 * @param modelName Model name (tiny, base, small, medium)
	 * @return true if successful
	 */
	bool loadModel(const std::string &modelName);

	/*
	 * Check if model is loaded
	 */
	bool isModelLoaded() const;

	/*
	 * Unload the current model
	 */
	void unloadModel();

	/*
	 * Transcribe audio from a file
	 * @param audioPath Path to audio/video file
	 * @param language Language code (en, es, etc.) or empty for auto-detect
	 * @param progressCallback Progress callback (0-100, status message)
	 * @return Transcription result
	 */
	WhisperResult transcribe(
		const std::string &audioPath,
		const std::string &language = "",
		std::function<void(int, const std::string&)> progressCallback = nullptr
	);

private:
	void *ctx = nullptr; /* whisper_context* */
	std::string loadedModelName;
};


