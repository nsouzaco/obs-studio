/*
 * FFmpeg Utilities for OBS AI Toolkit
 * Native audio extraction without external dependencies
 */

#pragma once

#include <string>
#include <vector>
#include <functional>

/* Extract audio waveform from video file using native FFmpeg APIs */
class FFmpegAudioExtractor {
public:
	struct WaveformResult {
		std::vector<float> samples;
		double duration;
		bool success;
		std::string error;
	};

	/*
	 * Extract waveform data from a video/audio file
	 * @param filePath Path to the media file
	 * @param numSamples Target number of samples (will be adjusted based on duration)
	 * @param progressCallback Optional callback for progress updates (0-100)
	 * @return WaveformResult with normalized amplitude values (0.0-1.0)
	 */
	static WaveformResult extractWaveform(
		const std::string &filePath,
		int targetSamples = 2000,
		std::function<void(int)> progressCallback = nullptr
	);

	/*
	 * Get duration of a media file in seconds
	 */
	static double getDuration(const std::string &filePath);
};


