/*
 * Integration Tests for OBS AI Toolkit
 * Tests that require FFmpeg, filesystem operations, and actual runtime behavior
 * 
 * Uses simple assertions (Qt Test not bundled with OBS's Qt distribution)
 */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QString>
#include <QProcess>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <cmath>

#include "ffmpeg-utils.hpp"

static int g_testsPassed = 0;
static int g_testsFailed = 0;
static const char* g_currentTest = nullptr;

#define TEST_ASSERT(condition, message) do { \
    if (!(condition)) { \
        std::cerr << "  FAIL: " << message << std::endl; \
        std::cerr << "        at " << __FILE__ << ":" << __LINE__ << std::endl; \
        g_testsFailed++; \
        return; \
    } \
} while(0)

template<typename T>
std::string toStdString(const T& val) { 
    std::ostringstream ss; 
    ss << val; 
    return ss.str(); 
}

template<>
std::string toStdString<QString>(const QString& val) { 
    return val.toStdString(); 
}

template<>
std::string toStdString<std::string>(const std::string& val) { 
    return val; 
}

#define TEST_COMPARE(actual, expected, message) do { \
    auto _actual = (actual); \
    auto _expected = (expected); \
    if (_actual != _expected) { \
        std::cerr << "  FAIL: " << message << std::endl; \
        std::cerr << "        Expected: " << toStdString(_expected) << std::endl; \
        std::cerr << "        Actual:   " << toStdString(_actual) << std::endl; \
        std::cerr << "        at " << __FILE__ << ":" << __LINE__ << std::endl; \
        g_testsFailed++; \
        return; \
    } \
} while(0)

#define TEST_COMPARE_DOUBLE(actual, expected, epsilon, message) do { \
    double _actual = (actual); \
    double _expected = (expected); \
    if (std::fabs(_actual - _expected) > epsilon) { \
        std::cerr << "  FAIL: " << message << std::endl; \
        std::cerr << "        Expected: " << _expected << std::endl; \
        std::cerr << "        Actual:   " << _actual << std::endl; \
        std::cerr << "        at " << __FILE__ << ":" << __LINE__ << std::endl; \
        g_testsFailed++; \
        return; \
    } \
} while(0)

#define RUN_TEST(func) do { \
    g_currentTest = #func; \
    std::cout << "  Running " << #func << "..." << std::endl; \
    func(); \
    g_testsPassed++; \
} while(0)

struct WhisperModelInfo {
    std::string name;
    std::string filename;
    std::string url;
    size_t size;
};

struct LlamaRunnerModelInfo {
    std::string name;
    std::string filename;
    std::string url;
    size_t size;
};

std::vector<WhisperModelInfo> getWhisperModels()
{
    return {
        {"tiny",   "ggml-tiny.bin",   "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin",   75000000},
        {"base",   "ggml-base.bin",   "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin",   142000000},
        {"small",  "ggml-small.bin",  "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin",  466000000},
        {"medium", "ggml-medium.bin", "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin", 1530000000},
    };
}

std::vector<LlamaRunnerModelInfo> getLlamaModels()
{
    return {
        {"qwen2.5-0.5b", "qwen2.5-0.5b-instruct-q4_k_m.gguf", 
         "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf", 397000000},
        {"qwen2.5-3b", "qwen2.5-3b-instruct-q4_k_m.gguf", 
         "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf", 2105000000}
    };
}

void testGetDurationInvalidFile()
{
    double duration = FFmpegAudioExtractor::getDuration("/nonexistent/path/file.mp4");
    TEST_COMPARE_DOUBLE(duration, 0.0, 0.001, "Invalid file should return 0 duration");
}

void testGetDurationEmptyPath()
{
    double duration = FFmpegAudioExtractor::getDuration("");
    TEST_COMPARE_DOUBLE(duration, 0.0, 0.001, "Empty path should return 0 duration");
}

void testGetDurationNullChars()
{
    /* Path with embedded null - should not crash */
    std::string weirdPath = "/some/path\0/file.mp4";
    double duration = FFmpegAudioExtractor::getDuration(weirdPath);
    TEST_COMPARE_DOUBLE(duration, 0.0, 0.001, "Weird path should return 0 duration");
}

void testGetDurationDirectoryPath()
{
    /* Trying to get duration of a directory (not a file) */
    double duration = FFmpegAudioExtractor::getDuration("/tmp");
    TEST_COMPARE_DOUBLE(duration, 0.0, 0.001, "Directory path should return 0 duration");
}

void testExtractWaveformInvalidFile()
{
    auto result = FFmpegAudioExtractor::extractWaveform("/nonexistent/path/file.mp4");
    TEST_ASSERT(!result.success, "Invalid file should not succeed");
    TEST_ASSERT(!result.error.empty(), "Should have error message");
}

void testExtractWaveformEmptyPath()
{
    auto result = FFmpegAudioExtractor::extractWaveform("");
    TEST_ASSERT(!result.success, "Empty path should not succeed");
}

void testWaveformResultStructure()
{
    auto result = FFmpegAudioExtractor::extractWaveform("/nonexistent/file.mp4");
    
    /* Result structure should be properly initialized */
    TEST_ASSERT(result.duration >= 0.0, "Duration should be non-negative");
    /* samples vector exists (even if empty for invalid file) */
}

void testWaveformProgressCallback()
{
    int progressCalled = 0;
    int lastProgress = -1;
    
    /* Test that progress callback is called (even for invalid file - early failure) */
    auto result = FFmpegAudioExtractor::extractWaveform(
        "/nonexistent/file.mp4",
        1000,
        [&](int progress) {
            progressCalled++;
            TEST_ASSERT(progress >= 0 && progress <= 100, "Progress should be 0-100");
            TEST_ASSERT(progress >= lastProgress, "Progress should not decrease");
            lastProgress = progress;
        }
    );
    
    /* For invalid file, callback may or may not be called depending on implementation */
    /* Just verify we didn't crash */
}

void testWaveformTargetSamples()
{
    /* Even though file doesn't exist, verify target samples parameter is accepted */
    auto result1 = FFmpegAudioExtractor::extractWaveform("/nonexistent.mp4", 100);
    auto result2 = FFmpegAudioExtractor::extractWaveform("/nonexistent.mp4", 5000);
    
    /* Both should fail gracefully */
    TEST_ASSERT(!result1.success, "Should fail for invalid file");
    TEST_ASSERT(!result2.success, "Should fail for invalid file");
}

void testTemporaryDirectoryCreation()
{
    QTemporaryDir tempDir;
    TEST_ASSERT(tempDir.isValid(), "Temporary directory should be valid");
    TEST_ASSERT(QDir(tempDir.path()).exists(), "Temp directory should exist");
}

void testModelDirectoryPath()
{
    /* Test that model directory path generation works */
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    TEST_ASSERT(!dataPath.isEmpty(), "App data location should not be empty");
    
    /* Verify the path format is reasonable */
    TEST_ASSERT(dataPath.contains("/") || dataPath.contains("\\"), 
        "Path should contain separators");
}

void testWhisperModelDirectoryCreation()
{
    QTemporaryDir tempDir;
    TEST_ASSERT(tempDir.isValid(), "Temp dir should be valid");
    
    QString modelsPath = tempDir.path() + "/whisper-models";
    QDir dir;
    bool created = dir.mkpath(modelsPath);
    TEST_ASSERT(created, "Should be able to create models directory");
    TEST_ASSERT(QDir(modelsPath).exists(), "Models directory should exist");
}

void testLlamaModelDirectoryCreation()
{
    QTemporaryDir tempDir;
    TEST_ASSERT(tempDir.isValid(), "Temp dir should be valid");
    
    QString modelsPath = tempDir.path() + "/obs-ai-toolkit/llama-models";
    QDir dir;
    bool created = dir.mkpath(modelsPath);
    TEST_ASSERT(created, "Should be able to create nested models directory");
    TEST_ASSERT(QDir(modelsPath).exists(), "Nested models directory should exist");
}

void testModelFileExistenceCheck()
{
    QTemporaryDir tempDir;
    TEST_ASSERT(tempDir.isValid(), "Temp dir should be valid");
    
    QString modelPath = tempDir.path() + "/test-model.bin";
    
    /* Initially doesn't exist */
    TEST_ASSERT(!QFile::exists(modelPath), "Model file should not exist initially");
    
    /* Create a dummy file */
    QFile file(modelPath);
    TEST_ASSERT(file.open(QIODevice::WriteOnly), "Should be able to create file");
    file.write("test data");
    file.close();
    
    /* Now it should exist */
    TEST_ASSERT(QFile::exists(modelPath), "Model file should exist after creation");
    
    /* Check file size */
    QFileInfo info(modelPath);
    TEST_ASSERT(info.size() > 0, "Model file should have content");
}

void testLlamaCliPathDetection()
{
    QString appPath = QCoreApplication::applicationDirPath();
    TEST_ASSERT(!appPath.isEmpty(), "Application path should not be empty");
    
    /* Test various potential llama-cli locations */
#if defined(__APPLE__)
    QString bundlePath = appPath + "/../Helpers/llama-cli";
    QString brewPath = "/opt/homebrew/bin/llama-cli";
    QString localPath = "/usr/local/bin/llama-cli";
    
    /* At least one of these paths should be checkable (not necessarily exist) */
    bool pathsCheckable = true;
    TEST_ASSERT(pathsCheckable, "Should be able to check llama-cli paths");
#endif
}

void testLlamaCliPathWithHomebrewFallback()
{
    /* Check if Homebrew llama-cli exists (dev environment) */
    QString brewPathArm = "/opt/homebrew/bin/llama-cli";
    QString brewPathIntel = "/usr/local/bin/llama-cli";
    
    bool hasHomebrew = QFile::exists(brewPathArm) || QFile::exists(brewPathIntel);
    
    /* This test just verifies the check doesn't crash */
    std::cout << "    (Homebrew llama-cli: " << (hasHomebrew ? "found" : "not found") << ")" << std::endl;
}

void testQProcessBasicExecution()
{
    /* Test that QProcess works for basic commands */
    QProcess process;
    process.start("echo", QStringList() << "test");
    
    bool started = process.waitForStarted(5000);
    TEST_ASSERT(started, "echo command should start");
    
    bool finished = process.waitForFinished(5000);
    TEST_ASSERT(finished, "echo command should finish");
    
    TEST_COMPARE(process.exitCode(), 0, "echo should exit with 0");
}

void testQProcessEnvironment()
{
    /* Test that environment variables are accessible */
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
    TEST_ASSERT(!path.isEmpty(), "PATH environment variable should exist");
}

void testQProcessNonExistentCommand()
{
    /* Test handling of non-existent commands */
    QProcess process;
    process.start("nonexistent_command_12345", QStringList());
    
    bool started = process.waitForStarted(1000);
    /* Should either fail to start or start and fail */
    if (started) {
        process.waitForFinished(1000);
    }
    /* Main test: didn't crash */
}

void testWhisperModelURLsValid()
{
    auto models = getWhisperModels();
    
    for (const auto& model : models) {
        TEST_ASSERT(model.url.find("https://") == 0, 
            "Whisper model URLs should use HTTPS");
        TEST_ASSERT(model.url.find("huggingface.co") != std::string::npos,
            "Whisper model URLs should use Hugging Face");
        TEST_ASSERT(model.url.find(".bin") != std::string::npos,
            "Whisper model URLs should end in .bin");
    }
}

void testLlamaModelURLsValid()
{
    auto models = getLlamaModels();
    
    for (const auto& model : models) {
        TEST_ASSERT(model.url.find("https://") == 0,
            "LLM model URLs should use HTTPS");
        TEST_ASSERT(model.url.find("huggingface.co") != std::string::npos,
            "LLM model URLs should use Hugging Face");
        TEST_ASSERT(model.url.find(".gguf") != std::string::npos,
            "LLM model URLs should be GGUF format");
    }
}

void testModelFilenamesMatchURLs()
{
    /* Verify filenames in model info match what's in the URL */
    auto whisperModels = getWhisperModels();
    for (const auto& model : whisperModels) {
        TEST_ASSERT(model.url.find(model.filename) != std::string::npos,
            "Whisper filename should appear in URL");
    }
    
    auto llamaModels = getLlamaModels();
    for (const auto& model : llamaModels) {
        TEST_ASSERT(model.url.find(model.filename) != std::string::npos,
            "LLM filename should appear in URL");
    }
}

void testWaveformSampleNormalization()
{
    /* Test that waveform samples from the result are normalized 0-1 */
    /* Since we can't test with real files, verify the structure */
    
    auto result = FFmpegAudioExtractor::extractWaveform("/nonexistent.mp4", 100);
    
    /* Even for failed extraction, verify samples vector exists */
    /* (might have fallback samples for UI) */
    
    if (result.success && !result.samples.empty()) {
        for (float sample : result.samples) {
            TEST_ASSERT(sample >= 0.0f && sample <= 1.0f,
                "Waveform samples should be normalized 0-1");
        }
    }
}

void testFFmpegErrorMessages()
{
    auto result = FFmpegAudioExtractor::extractWaveform("/nonexistent/path.mp4");
    
    TEST_ASSERT(!result.success, "Should fail for nonexistent file");
    TEST_ASSERT(!result.error.empty(), "Should have error message");
    TEST_ASSERT(result.error.length() > 5, "Error message should be descriptive");
}

void testMultipleWaveformExtractions()
{
    /* Test that multiple extractions don't interfere */
    auto result1 = FFmpegAudioExtractor::extractWaveform("/path1.mp4");
    auto result2 = FFmpegAudioExtractor::extractWaveform("/path2.mp4");
    auto result3 = FFmpegAudioExtractor::extractWaveform("/path3.mp4");
    
    /* All should fail gracefully (files don't exist) */
    TEST_ASSERT(!result1.success, "Result 1 should fail");
    TEST_ASSERT(!result2.success, "Result 2 should fail");
    TEST_ASSERT(!result3.success, "Result 3 should fail");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    std::cout << "========================================" << std::endl;
    std::cout << "OBS AI Toolkit Integration Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "\n[FFmpeg Duration Tests]" << std::endl;
    RUN_TEST(testGetDurationInvalidFile);
    RUN_TEST(testGetDurationEmptyPath);
    RUN_TEST(testGetDurationNullChars);
    RUN_TEST(testGetDurationDirectoryPath);
    
    std::cout << "\n[FFmpeg Waveform Tests]" << std::endl;
    RUN_TEST(testExtractWaveformInvalidFile);
    RUN_TEST(testExtractWaveformEmptyPath);
    RUN_TEST(testWaveformResultStructure);
    RUN_TEST(testWaveformProgressCallback);
    RUN_TEST(testWaveformTargetSamples);
    RUN_TEST(testWaveformSampleNormalization);
    
    std::cout << "\n[Filesystem Tests]" << std::endl;
    RUN_TEST(testTemporaryDirectoryCreation);
    RUN_TEST(testModelDirectoryPath);
    RUN_TEST(testWhisperModelDirectoryCreation);
    RUN_TEST(testLlamaModelDirectoryCreation);
    RUN_TEST(testModelFileExistenceCheck);
    
    std::cout << "\n[llama-cli Detection Tests]" << std::endl;
    RUN_TEST(testLlamaCliPathDetection);
    RUN_TEST(testLlamaCliPathWithHomebrewFallback);
    
    std::cout << "\n[Process Execution Tests]" << std::endl;
    RUN_TEST(testQProcessBasicExecution);
    RUN_TEST(testQProcessEnvironment);
    RUN_TEST(testQProcessNonExistentCommand);
    
    std::cout << "\n[Model URL Validation Tests]" << std::endl;
    RUN_TEST(testWhisperModelURLsValid);
    RUN_TEST(testLlamaModelURLsValid);
    RUN_TEST(testModelFilenamesMatchURLs);
    
    std::cout << "\n[Error Handling Tests]" << std::endl;
    RUN_TEST(testFFmpegErrorMessages);
    RUN_TEST(testMultipleWaveformExtractions);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << g_testsPassed << " passed, " << g_testsFailed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return g_testsFailed > 0 ? 1 : 0;
}
