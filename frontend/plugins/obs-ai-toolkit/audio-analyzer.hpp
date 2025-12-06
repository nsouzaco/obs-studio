/*
 * Audio Analyzer
 * FFmpeg-based audio analysis for highlight detection
 */

#pragma once

#include <string>
#include <vector>
#include <functional>

struct AudioPeak {
	double timestamp;    /* Seconds */
	double level;        /* dB */
	double duration;     /* Seconds */
};

class FFmpegAudioAnalyzer {
public:
	FFmpegAudioAnalyzer();
	~FFmpegAudioAnalyzer();

	bool open(const std::string &filePath);
	void close();

	/* Analyze audio and find peaks above threshold */
	std::vector<AudioPeak> findPeaks(
		double thresholdDb,
		double minDuration = 0.5,
		std::function<void(int)> progressCallback = nullptr);

	/* Get audio duration in seconds */
	double getDuration() const;

private:
	struct Impl;
	Impl *impl = nullptr;
};


