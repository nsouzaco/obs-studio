/*
 * LlamaRunner - Subprocess-based LLM inference
 * Runs llama-cli as a separate process to avoid symbol conflicts
 */

#include "llama-runner.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextStream>

#include <sstream>

LlamaRunner::LlamaRunner(QObject *parent)
	: QObject(parent)
{
}

LlamaRunner::~LlamaRunner()
{
	cancel();
}

std::vector<LlamaRunnerModelInfo> LlamaRunner::getAvailableModels()
{
	return {
		{
			"qwen2.5-0.5b",
			"qwen2.5-0.5b-instruct-q4_k_m.gguf",
			"https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf",
			397000000
		},
		{
			"qwen2.5-1.5b",
			"qwen2.5-1.5b-instruct-q4_k_m.gguf",
			"https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf",
			986000000
		},
		{
			"phi-3-mini",
			"Phi-3-mini-4k-instruct-q4.gguf",
			"https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-gguf/resolve/main/Phi-3-mini-4k-instruct-q4.gguf",
			2200000000
		},
		{
			"qwen2.5-3b",
			"qwen2.5-3b-instruct-q4_k_m.gguf",
			"https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf",
			2105000000
		}
	};
}

std::string LlamaRunner::getModelsDir()
{
	QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	QDir dir(dataPath + "/obs-ai-toolkit/llama-models");
	if (!dir.exists()) {
		dir.mkpath(".");
	}
	return dir.absolutePath().toStdString();
}

bool LlamaRunner::isModelDownloaded(const std::string &modelName)
{
	std::string path = getModelPath(modelName);
	return QFile::exists(QString::fromStdString(path));
}

std::string LlamaRunner::getModelPath(const std::string &modelName)
{
	auto models = getAvailableModels();
	for (const auto &m : models) {
		if (m.name == modelName) {
			return getModelsDir() + "/" + m.filename;
		}
	}
	return "";
}

std::string LlamaRunner::getLlamaCliPath()
{
	/* Look for llama-cli in the app bundle */
	QString appPath = QCoreApplication::applicationDirPath();
	
	/* macOS: OBS.app/Contents/MacOS -> OBS.app/Contents/Helpers/llama-cli */
	QString bundlePath = appPath + "/../Helpers/llama-cli";
	if (QFile::exists(bundlePath)) {
		return bundlePath.toStdString();
	}
	
	/* Also try alongside the executable */
	QString localPath = appPath + "/llama-cli";
	if (QFile::exists(localPath)) {
		return localPath.toStdString();
	}
	
	/* Fallback: check if llama-cli is in PATH */
	QString systemPath = "/usr/local/bin/llama-cli";
	if (QFile::exists(systemPath)) {
		return systemPath.toStdString();
	}
	
	/* Try homebrew path */
	QString brewPath = "/opt/homebrew/bin/llama-cli";
	if (QFile::exists(brewPath)) {
		return brewPath.toStdString();
	}
	
	return "";
}

bool LlamaRunner::isLlamaCliAvailable()
{
	return !getLlamaCliPath().empty();
}

bool LlamaRunner::downloadModel(
	const std::string &modelName,
	std::function<void(int, const std::string&)> progressCallback)
{
	auto models = getAvailableModels();
	LlamaRunnerModelInfo targetModel;
	bool found = false;
	
	for (const auto &m : models) {
		if (m.name == modelName) {
			targetModel = m;
			found = true;
			break;
		}
	}
	
	if (!found) {
		if (progressCallback) {
			progressCallback(0, "Model not found: " + modelName);
		}
		return false;
	}
	
	std::string destPath = getModelsDir() + "/" + targetModel.filename;
	
	if (QFile::exists(QString::fromStdString(destPath))) {
		if (progressCallback) {
			progressCallback(100, "Model already downloaded");
		}
		return true;
	}
	
	if (progressCallback) {
		progressCallback(0, "Downloading " + targetModel.name + "...");
	}
	
	QDir().mkpath(QString::fromStdString(getModelsDir()));
	
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("PATH", "/usr/bin:/bin:/usr/local/bin:" + env.value("PATH"));
	process.setProcessEnvironment(env);
	
	QString curlPath = "/usr/bin/curl";
	QStringList curlArgs = {
		"-L",
		"-f",
		"--connect-timeout", "30",
		"--max-time", "1800",
		"-o", QString::fromStdString(destPath),
		"--progress-bar",
		QString::fromStdString(targetModel.url)
	};
	
	process.start(curlPath, curlArgs);
	
	if (!process.waitForStarted(10000)) {
		if (progressCallback) {
			progressCallback(0, "Could not start curl");
		}
		return false;
	}
	
	int lastProgress = 0;
	while (!process.waitForFinished(1000)) {
		lastProgress = std::min(99, lastProgress + 1);
		if (progressCallback) {
			progressCallback(lastProgress,
				"Downloading " + modelName + " model...");
		}
	}
	
	if (process.exitCode() != 0) {
		if (progressCallback) {
			progressCallback(0, "Download failed");
		}
		QFile::remove(QString::fromStdString(destPath));
		return false;
	}
	
	QFile downloadedFile(QString::fromStdString(destPath));
	if (!downloadedFile.exists() || downloadedFile.size() < 100000000) {
		if (progressCallback) {
			progressCallback(0, "Download incomplete");
		}
		QFile::remove(QString::fromStdString(destPath));
		return false;
	}
	
	if (progressCallback) {
		progressCallback(100, "Model downloaded successfully");
	}
	
	return true;
}

bool LlamaRunner::setModel(const std::string &modelName)
{
	std::string path = getModelPath(modelName);
	if (path.empty() || !QFile::exists(QString::fromStdString(path))) {
		return false;
	}
	currentModelPath = path;
	currentModelName = modelName;
	return true;
}

QStringList LlamaRunner::buildArgs(const std::string &prompt, int maxTokens)
{
	QStringList args;
	
	args << "-m" << QString::fromStdString(currentModelPath);
	args << "-n" << QString::number(maxTokens);
	args << "-c" << "4096";           /* Context size */
	args << "--temp" << "0.2";        /* Lower temperature for more deterministic output */
	args << "--top-p" << "0.85";      /* Tighter sampling */
	args << "--top-k" << "40";        /* Add top-k for additional focus */
	args << "--repeat-penalty" << "1.3";  /* Stronger repetition penalty */
	args << "--no-display-prompt";    /* Don't echo prompt */
	args << "-e";                     /* Process escape sequences in prompt */
	args << "-p" << QString::fromStdString(prompt);
	
	return args;
}

LlamaRunnerResult LlamaRunner::runInference(
	const std::string &prompt,
	int maxTokens,
	std::function<void(int, const std::string&)> progressCallback)
{
	LlamaRunnerResult result;
	result.success = false;
	
	if (currentModelPath.empty()) {
		result.error = "No model set";
		return result;
	}
	
	std::string cliPath = getLlamaCliPath();
	if (cliPath.empty()) {
		result.error = "llama-cli not found";
		return result;
	}
	
	if (progressCallback) {
		progressCallback(10, "Starting inference...");
	}
	
	cancelled = false;
	process = new QProcess(this);
	process->setProcessChannelMode(QProcess::SeparateChannels);
	
	QStringList args = buildArgs(prompt, maxTokens);
	
	blog(LOG_INFO, "[LlamaRunner] Starting: %s", cliPath.c_str());
	blog(LOG_INFO, "[LlamaRunner] Model: %s", currentModelPath.c_str());
	
	process->start(QString::fromStdString(cliPath), args);
	
	if (!process->waitForStarted(10000)) {
		result.error = "Failed to start llama-cli: " + process->errorString().toStdString();
		delete process;
		process = nullptr;
		return result;
	}
	
	/* Close stdin so llama-cli doesn't wait for interactive input */
	process->closeWriteChannel();
	
	if (progressCallback) {
		progressCallback(30, "Generating...");
	}
	
	/* Wait for completion with timeout (5 minutes) */
	int elapsed = 0;
	const int timeout = 300000; /* 5 minutes */
	const int pollInterval = 500;
	
	while (!process->waitForFinished(pollInterval)) {
		if (cancelled) {
			process->kill();
			process->waitForFinished(1000);
			result.error = "Cancelled";
			delete process;
			process = nullptr;
			return result;
		}
		
		elapsed += pollInterval;
		if (elapsed > timeout) {
			process->kill();
			process->waitForFinished(1000);
			result.error = "Timeout";
			delete process;
			process = nullptr;
			return result;
		}
		
		/* Update progress */
		int progress = 30 + (elapsed * 60 / timeout);
		if (progressCallback) {
			progressCallback(std::min(90, progress), "Generating...");
		}
	}
	
	if (process->exitCode() != 0) {
		QString output = QString::fromUtf8(process->readAllStandardOutput());
		result.error = "llama-cli failed: " + output.toStdString();
		blog(LOG_ERROR, "[LlamaRunner] Exit code: %d, output: %s", 
			process->exitCode(), output.toStdString().c_str());
		delete process;
		process = nullptr;
		return result;
	}
	
	QString output = QString::fromUtf8(process->readAllStandardOutput());
	result.text = output.trimmed().toStdString();
	result.success = true;
	
	blog(LOG_INFO, "[LlamaRunner] Generated %zu characters", result.text.length());
	
	delete process;
	process = nullptr;
	
	if (progressCallback) {
		progressCallback(100, "Done");
	}
	
	return result;
}

std::vector<LlamaRunnerChapter> LlamaRunner::generateChapters(
	const std::vector<std::pair<double, std::string>> &segments,
	std::function<void(int, const std::string&)> progressCallback)
{
	std::vector<LlamaRunnerChapter> chapters;
	
	if (segments.empty()) {
		return chapters;
	}
	
	if (progressCallback) {
		progressCallback(10, "Preparing prompt...");
	}
	
	/* Get video duration from last segment */
	double videoDuration = segments.back().first;
	int durationMins = (int)(videoDuration / 60);
	int durationSecs = (int)videoDuration % 60;
	std::string durationStr = std::to_string(durationMins) + ":" + 
		(durationSecs < 10 ? "0" : "") + std::to_string(durationSecs);
	
	/* Build transcript with timestamps - keep it short to avoid overwhelming the LLM */
	const size_t maxTranscriptChars = 3000;
	std::stringstream transcriptWithTimes;
	size_t totalChars = 0;
	
	for (const auto &seg : segments) {
		int mins = (int)(seg.first / 60);
		int secs = (int)seg.first % 60;
		
		std::stringstream segLine;
		segLine << "[" << mins << ":"
			<< (secs < 10 ? "0" : "") << secs << "] "
			<< seg.second << "\n";
		
		std::string line = segLine.str();
		
		if (totalChars + line.length() > maxTranscriptChars) {
			transcriptWithTimes << "\n[... transcript truncated ...]\n";
			break;
		}
		
		transcriptWithTimes << line;
		totalChars += line.length();
	}
	
	/* Create prompt using Qwen chat template */
	std::string prompt = R"(<|im_start|>system
You create YouTube chapter markers from video transcripts. Output ONLY timestamp-title pairs, one per line.<|im_end|}
<|im_start|>user
Create 5-8 YouTube chapters for this )" + durationStr + R"( video.

Rules:
- First chapter MUST be "0:00 Introduction" or similar
- Format each line as: M:SS Title (2-4 words)
- Identify major topic changes in the content

Transcript:
)" + transcriptWithTimes.str() + R"(<|im_end|>
<|im_start|>assistant
0:00 Introduction
)";
	
	blog(LOG_INFO, "[LlamaRunner] === CHAPTER GENERATION DEBUG ===");
	blog(LOG_INFO, "[LlamaRunner] Prompt length: %zu chars", prompt.length());
	blog(LOG_INFO, "[LlamaRunner] Transcript segments: %zu", segments.size());
	blog(LOG_INFO, "[LlamaRunner] Video duration: %s", durationStr.c_str());
	blog(LOG_INFO, "[LlamaRunner] Prompt start: %.200s...", prompt.c_str());
	blog(LOG_INFO, "[LlamaRunner] Prompt end: ...%s", prompt.substr(prompt.length() > 200 ? prompt.length() - 200 : 0).c_str());
	
	if (progressCallback) {
		progressCallback(20, "Generating chapters...");
	}
	
	/* Run inference */
	LlamaRunnerResult result = runInference(prompt, 512, [&](int p, const std::string &s) {
		if (progressCallback) {
			progressCallback(20 + (p * 60 / 100), s);
		}
	});
	
	blog(LOG_INFO, "[LlamaRunner] Inference success: %s", result.success ? "YES" : "NO");
	blog(LOG_INFO, "[LlamaRunner] Raw result text (%zu chars): [%s]", result.text.length(), result.text.c_str());
	if (!result.error.empty()) {
		blog(LOG_INFO, "[LlamaRunner] Error: %s", result.error.c_str());
	}
	
	if (!result.success) {
		blog(LOG_ERROR, "[LlamaRunner] Chapter generation failed: %s", result.error.c_str());
		return chapters;
	}
	
	if (progressCallback) {
		progressCallback(85, "Parsing chapters...");
	}
	
	/* Parse chapters from output - prepend the primed intro since we primed the model */
	std::string fullOutput = "0:00 Introduction\n" + result.text;
	blog(LOG_INFO, "[LlamaRunner] Full output with prepended 0:00: [%s]", fullOutput.substr(0, 500).c_str());
	
	/* Match chapter format: starts with MM:SS (not [MM:SS] which is transcript format) */
	QRegularExpression chapterRegex(R"(^(\d{1,2}):(\d{2})\s+(.+)$)");
	/* Detect transcript-style lines to filter out */
	QRegularExpression transcriptRegex(R"(\[\d{1,2}:\d{2}\])");
	
	QStringList lines = QString::fromStdString(fullOutput).split('\n', Qt::SkipEmptyParts);
	blog(LOG_INFO, "[LlamaRunner] Parsing %lld lines", (long long)lines.size());
	
	for (const QString &line : lines) {
		QString trimmedLine = line.trimmed();
		blog(LOG_INFO, "[LlamaRunner] Checking line: [%s]", trimmedLine.toStdString().c_str());
		
		/* Skip lines that look like transcript entries */
		if (transcriptRegex.match(trimmedLine).hasMatch()) {
			continue;
		}
		
		/* Skip lines that are too long (likely transcript text, not chapter titles) */
		if (trimmedLine.length() > 80) {
			continue;
		}
		
		QRegularExpressionMatch match = chapterRegex.match(trimmedLine);
		if (match.hasMatch()) {
			LlamaRunnerChapter chapter;
			int mins = match.captured(1).toInt();
			int secs = match.captured(2).toInt();
			chapter.timestamp = mins * 60.0 + secs;
			chapter.title = match.captured(3).trimmed().toStdString();
			
			/* Clean up title - remove quotes, trailing punctuation */
			while (!chapter.title.empty() &&
				(chapter.title.back() == '"' || chapter.title.back() == '\'' ||
				 chapter.title.back() == '.' || chapter.title.back() == ',')) {
				chapter.title.pop_back();
			}
			while (!chapter.title.empty() &&
				(chapter.title.front() == '"' || chapter.title.front() == '\'')) {
				chapter.title.erase(0, 1);
			}
			
			/* Skip if title is too short or too long */
			if (chapter.title.length() >= 3 && chapter.title.length() <= 60) {
				chapters.push_back(chapter);
			}
		}
	}
	
	/* If we got no valid chapters, create fallback chapters from transcript */
	if (chapters.empty()) {
		blog(LOG_WARNING, "[LlamaRunner] No chapters parsed from output. Raw text was: %s",
			result.text.substr(0, 1000).c_str());
		blog(LOG_INFO, "[LlamaRunner] Creating fallback chapters from transcript timestamps");
		
		/* Create chapters at regular intervals from the transcript */
		double lastChapterTime = -60.0; /* Start before 0 so first segment triggers */
		int chapterNum = 1;
		for (const auto &seg : segments) {
			/* Create a chapter every ~60 seconds */
			if (seg.first - lastChapterTime >= 60.0 && chapters.size() < 15) {
				LlamaRunnerChapter chapter;
				chapter.timestamp = seg.first;
				/* Use first few words of transcript as title */
				std::string text = seg.second;
				/* Find first 4-5 words */
				size_t wordCount = 0;
				size_t pos = 0;
				for (size_t i = 0; i < text.length() && wordCount < 5; i++) {
					if (text[i] == ' ') {
						wordCount++;
						pos = i;
					}
				}
				if (pos > 0 && pos < text.length()) {
					chapter.title = text.substr(0, pos);
				} else {
					chapter.title = text.substr(0, std::min(text.length(), (size_t)40));
				}
				/* Clean up */
				while (!chapter.title.empty() && 
					(chapter.title.back() == '.' || chapter.title.back() == ',' ||
					 chapter.title.back() == ' ')) {
					chapter.title.pop_back();
				}
				if (chapter.title.length() >= 3) {
					chapters.push_back(chapter);
					lastChapterTime = seg.first;
					chapterNum++;
				}
			}
		}
		
		/* Ensure we have at least one chapter at 0:00 */
		if (!chapters.empty() && chapters[0].timestamp > 0.5) {
			LlamaRunnerChapter intro;
			intro.timestamp = 0.0;
			intro.title = "Introduction";
			chapters.insert(chapters.begin(), intro);
		}
	}
	
	if (progressCallback) {
		progressCallback(100, "Done");
	}
	
	blog(LOG_INFO, "[LlamaRunner] Generated %zu chapters", chapters.size());
	
	return chapters;
}

void LlamaRunner::cancel()
{
	cancelled = true;
	if (process && process->state() != QProcess::NotRunning) {
		process->kill();
		process->waitForFinished(1000);
	}
}

bool LlamaRunner::isRunning() const
{
	return process && process->state() != QProcess::NotRunning;
}

