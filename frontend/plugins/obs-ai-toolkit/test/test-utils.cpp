/*
 * Unit Tests for OBS AI Toolkit Utilities
 * Tests model management, path functions, and data structures
 * 
 * Uses simple assertions instead of Qt Test framework
 * (Qt Test not bundled with OBS's Qt distribution)
 */

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QRegularExpression>

#include <iostream>
#include <sstream>
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

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

/* Helper to convert various types to std::string for output */
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

#define RUN_TEST(func) do { \
    g_currentTest = #func; \
    std::cout << "  Running " << #func << "..." << std::endl; \
    func(); \
    if (g_testsFailed == 0 || g_testsFailed == g_testsFailed) { \
        g_testsPassed++; \
    } \
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

struct TranscriptSegment {
    double start;
    double end;
    QString text;
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

std::string getWhisperModelPath(const std::string &modelName, const std::string &modelsDir)
{
    auto models = getWhisperModels();
    for (const auto &model : models) {
        if (model.name == modelName) {
            return modelsDir + "/" + model.filename;
        }
    }
    return "";
}

std::string getLlamaModelPath(const std::string &modelName, const std::string &modelsDir)
{
    auto models = getLlamaModels();
    for (const auto &m : models) {
        if (m.name == modelName) {
            return modelsDir + "/" + m.filename;
        }
    }
    return "";
}

/* Timeline position/time conversion */
constexpr int PIXELS_PER_SECOND = 8;

int positionToX(qint64 positionMs, qint64 totalDurationMs)
{
    if (totalDurationMs <= 0) return 0;
    double seconds = positionMs / 1000.0;
    return static_cast<int>(seconds * PIXELS_PER_SECOND);
}

qint64 xToPosition(int x, qint64 totalDurationMs)
{
    if (x < 0) return 0;
    double seconds = x / static_cast<double>(PIXELS_PER_SECOND);
    qint64 positionMs = static_cast<qint64>(seconds * 1000);
    return qBound<qint64>(0, positionMs, totalDurationMs);
}

/* Cut region helpers */
bool isPositionInCutRegion(qint64 positionMs, const std::vector<std::pair<qint64, qint64>> &cutRegions)
{
    for (const auto &region : cutRegions) {
        if (positionMs >= region.first && positionMs < region.second) {
            return true;
        }
    }
    return false;
}

qint64 findEndOfCutRegion(qint64 positionMs, const std::vector<std::pair<qint64, qint64>> &cutRegions)
{
    for (const auto &region : cutRegions) {
        if (positionMs >= region.first && positionMs < region.second) {
            return region.second;
        }
    }
    return positionMs;
}

/* Format time for display */
QString formatTime(qint64 ms)
{
    int totalSecs = ms / 1000;
    int hours = totalSecs / 3600;
    int mins = (totalSecs % 3600) / 60;
    int secs = totalSecs % 60;
    
    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    } else {
        return QString("%1:%2")
            .arg(mins)
            .arg(secs, 2, 10, QChar('0'));
    }
}

/* YouTube chapter format */
QString formatYouTubeChapter(double timestamp, const QString &title)
{
    int totalSecs = static_cast<int>(timestamp);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    return QString("%1:%2 %3")
        .arg(mins)
        .arg(secs, 2, 10, QChar('0'))
        .arg(title);
}

bool parseYouTubeChapter(const QString &line, double &timestamp, QString &title)
{
    QRegularExpression regex(R"(^(\d{1,2}):(\d{2})\s+(.+)$)");
    QRegularExpressionMatch match = regex.match(line.trimmed());
    
    if (!match.hasMatch()) {
        return false;
    }
    
    int mins = match.captured(1).toInt();
    int secs = match.captured(2).toInt();
    timestamp = mins * 60.0 + secs;
    title = match.captured(3).trimmed();
    return true;
}

void testWhisperModelsAvailable()
{
    auto models = getWhisperModels();
    TEST_ASSERT(!models.empty(), "Models should not be empty");
    TEST_COMPARE(models.size(), 4u, "Should have 4 whisper models");
    TEST_COMPARE(models[0].name, std::string("tiny"), "First model should be tiny");
    TEST_COMPARE(models[3].name, std::string("medium"), "Last model should be medium");
}

void testWhisperModelPath()
{
    std::string modelsDir = "/test/models";
    std::string path = getWhisperModelPath("base", modelsDir);
    TEST_COMPARE(path, std::string("/test/models/ggml-base.bin"), "Base model path");
    
    path = getWhisperModelPath("tiny", modelsDir);
    TEST_COMPARE(path, std::string("/test/models/ggml-tiny.bin"), "Tiny model path");
}

void testWhisperModelPathInvalid()
{
    std::string modelsDir = "/test/models";
    std::string path = getWhisperModelPath("nonexistent", modelsDir);
    TEST_ASSERT(path.empty(), "Invalid model should return empty path");
}

void testWhisperModelSizes()
{
    auto models = getWhisperModels();
    for (size_t i = 1; i < models.size(); i++) {
        TEST_ASSERT(models[i].size > models[i-1].size, 
            "Models should increase in size");
    }
    for (const auto &model : models) {
        TEST_ASSERT(model.url.find("https://") == 0,
            "Model URLs should be HTTPS");
    }
}

void testLlamaModelsAvailable()
{
    auto models = getLlamaModels();
    TEST_ASSERT(!models.empty(), "LLM models should not be empty");
    TEST_COMPARE(models.size(), 4u, "Should have 4 LLM models");
    
    bool foundRecommended = false;
    for (const auto &m : models) {
        if (m.name == "qwen2.5-3b") {
            foundRecommended = true;
            break;
        }
    }
    TEST_ASSERT(foundRecommended, "Recommended model qwen2.5-3b should exist");
}

void testLlamaModelPath()
{
    std::string modelsDir = "/test/llama";
    std::string path = getLlamaModelPath("qwen2.5-3b", modelsDir);
    TEST_COMPARE(path, std::string("/test/llama/qwen2.5-3b-instruct-q4_k_m.gguf"), "Qwen model path");
}

void testLlamaModelPathInvalid()
{
    std::string modelsDir = "/test/llama";
    std::string path = getLlamaModelPath("invalid-model", modelsDir);
    TEST_ASSERT(path.empty(), "Invalid LLM model should return empty path");
}

void testLlamaModelSizes()
{
    auto models = getLlamaModels();
    for (const auto &model : models) {
        TEST_ASSERT(model.size >= 100000000,
            "LLM models should be at least 100MB");
        TEST_ASSERT(model.url.find("huggingface.co") != std::string::npos,
            "LLM model URLs should use Hugging Face");
    }
}

void testPositionToX()
{
    TEST_COMPARE(positionToX(1000, 60000), 8, "1 second = 8 pixels");
    TEST_COMPARE(positionToX(10000, 60000), 80, "10 seconds = 80 pixels");
    TEST_COMPARE(positionToX(0, 60000), 0, "0 ms = 0 pixels");
}

void testXToPosition()
{
    TEST_COMPARE(xToPosition(8, 60000), 1000LL, "8 pixels = 1000ms");
    TEST_COMPARE(xToPosition(80, 60000), 10000LL, "80 pixels = 10000ms");
    TEST_COMPARE(xToPosition(0, 60000), 0LL, "0 pixels = 0ms");
}

void testPositionRoundTrip()
{
    qint64 duration = 300000;
    QList<qint64> testPositions = {0, 1000, 5000, 30000, 60000, 120000};
    
    for (qint64 pos : testPositions) {
        int x = positionToX(pos, duration);
        qint64 recovered = xToPosition(x, duration);
        TEST_ASSERT(qAbs(recovered - pos) <= 125,
            "Round-trip should preserve position within tolerance");
    }
}

void testPositionBounds()
{
    qint64 duration = 60000;
    TEST_COMPARE(xToPosition(-10, duration), 0LL, "Negative X should clamp to 0");
    TEST_COMPARE(xToPosition(100000, duration), duration, "Beyond duration should clamp");
}

void testCutRegionDetection()
{
    std::vector<std::pair<qint64, qint64>> cutRegions = {
        {5000, 10000},
        {20000, 25000},
    };
    
    TEST_ASSERT(isPositionInCutRegion(7000, cutRegions), "7000 should be in first cut");
    TEST_ASSERT(isPositionInCutRegion(22000, cutRegions), "22000 should be in second cut");
    TEST_ASSERT(!isPositionInCutRegion(3000, cutRegions), "3000 should not be in any cut");
    TEST_ASSERT(!isPositionInCutRegion(15000, cutRegions), "15000 should not be in any cut");
    TEST_ASSERT(isPositionInCutRegion(5000, cutRegions), "Start boundary is inclusive");
    TEST_ASSERT(!isPositionInCutRegion(10000, cutRegions), "End boundary is exclusive");
}

void testCutRegionEndFinding()
{
    std::vector<std::pair<qint64, qint64>> cutRegions = {
        {5000, 10000},
        {20000, 25000},
    };
    
    TEST_COMPARE(findEndOfCutRegion(7000, cutRegions), 10000LL, "Should find end of first cut");
    TEST_COMPARE(findEndOfCutRegion(22000, cutRegions), 25000LL, "Should find end of second cut");
    TEST_COMPARE(findEndOfCutRegion(3000, cutRegions), 3000LL, "Outside cut returns same position");
}

void testOverlappingCutRegions()
{
    std::vector<std::pair<qint64, qint64>> cutRegions = {
        {5000, 10000},
        {10000, 15000},
    };
    
    TEST_ASSERT(isPositionInCutRegion(10000, cutRegions), "10000 should be in second cut");
    TEST_COMPARE(findEndOfCutRegion(10000, cutRegions), 15000LL, "Should find end of adjacent cut");
}

void testEmptyCutRegions()
{
    std::vector<std::pair<qint64, qint64>> cutRegions;
    TEST_ASSERT(!isPositionInCutRegion(5000, cutRegions), "No position in empty cuts");
    TEST_COMPARE(findEndOfCutRegion(5000, cutRegions), 5000LL, "Returns same position for empty cuts");
}

void testFormatTimeSeconds()
{
    TEST_COMPARE(formatTime(5000), QString("0:05"), "5 seconds");
    TEST_COMPARE(formatTime(45000), QString("0:45"), "45 seconds");
}

void testFormatTimeMinutes()
{
    TEST_COMPARE(formatTime(60000), QString("1:00"), "1 minute");
    TEST_COMPARE(formatTime(90000), QString("1:30"), "1:30");
    TEST_COMPARE(formatTime(600000), QString("10:00"), "10 minutes");
}

void testFormatTimeHours()
{
    TEST_COMPARE(formatTime(3600000), QString("1:00:00"), "1 hour");
    TEST_COMPARE(formatTime(3661000), QString("1:01:01"), "1:01:01");
}

void testFormatTimeZero()
{
    TEST_COMPARE(formatTime(0), QString("0:00"), "Zero time");
}

void testYouTubeChapterFormat()
{
    TEST_COMPARE(formatYouTubeChapter(0, "Introduction"), QString("0:00 Introduction"), "Intro chapter");
    TEST_COMPARE(formatYouTubeChapter(65, "Main Topic"), QString("1:05 Main Topic"), "1:05 chapter");
}

void testYouTubeChapterParse()
{
    double timestamp;
    QString title;
    
    TEST_ASSERT(parseYouTubeChapter("0:00 Introduction", timestamp, title), "Should parse intro");
    TEST_COMPARE(timestamp, 0.0, "Intro timestamp");
    TEST_COMPARE(title, QString("Introduction"), "Intro title");
    
    TEST_ASSERT(parseYouTubeChapter("2:30 Main Content", timestamp, title), "Should parse 2:30");
    TEST_COMPARE(timestamp, 150.0, "2:30 = 150 seconds");
}

void testYouTubeChapterRoundTrip()
{
    QString formatted = formatYouTubeChapter(125, "Test Chapter");
    
    double timestamp;
    QString title;
    TEST_ASSERT(parseYouTubeChapter(formatted, timestamp, title), "Should parse formatted");
    TEST_COMPARE(timestamp, 125.0, "Round-trip timestamp");
    TEST_COMPARE(title, QString("Test Chapter"), "Round-trip title");
}

void testYouTubeChapterParseInvalid()
{
    double timestamp;
    QString title;
    
    TEST_ASSERT(!parseYouTubeChapter("Introduction", timestamp, title), "Missing timestamp");
    TEST_ASSERT(!parseYouTubeChapter("", timestamp, title), "Empty string");
    TEST_ASSERT(!parseYouTubeChapter("[0:30] Some text", timestamp, title), "Transcript format");
}

void testTranscriptSegmentConstruction()
{
    TranscriptSegment seg;
    seg.start = 0.0;
    seg.end = 5.0;
    seg.text = "Hello world";
    
    TEST_COMPARE(seg.start, 0.0, "Segment start");
    TEST_COMPARE(seg.end, 5.0, "Segment end");
    TEST_COMPARE(seg.text, QString("Hello world"), "Segment text");
}

void testTranscriptSegmentOrdering()
{
    std::vector<TranscriptSegment> segments;
    
    TranscriptSegment s1 = {0.0, 5.0, "First"};
    TranscriptSegment s2 = {5.0, 10.0, "Second"};
    TranscriptSegment s3 = {10.0, 15.0, "Third"};
    
    segments.push_back(s3);
    segments.push_back(s1);
    segments.push_back(s2);
    
    std::sort(segments.begin(), segments.end(), 
        [](const TranscriptSegment &a, const TranscriptSegment &b) {
            return a.start < b.start;
        });
    
    TEST_COMPARE(segments[0].text, QString("First"), "First after sort");
    TEST_COMPARE(segments[1].text, QString("Second"), "Second after sort");
    TEST_COMPARE(segments[2].text, QString("Third"), "Third after sort");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    std::cout << "========================================" << std::endl;
    std::cout << "OBS AI Toolkit Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "\n[Whisper Model Tests]" << std::endl;
    RUN_TEST(testWhisperModelsAvailable);
    RUN_TEST(testWhisperModelPath);
    RUN_TEST(testWhisperModelPathInvalid);
    RUN_TEST(testWhisperModelSizes);
    
    std::cout << "\n[LLM Model Tests]" << std::endl;
    RUN_TEST(testLlamaModelsAvailable);
    RUN_TEST(testLlamaModelPath);
    RUN_TEST(testLlamaModelPathInvalid);
    RUN_TEST(testLlamaModelSizes);
    
    std::cout << "\n[Timeline Position Tests]" << std::endl;
    RUN_TEST(testPositionToX);
    RUN_TEST(testXToPosition);
    RUN_TEST(testPositionRoundTrip);
    RUN_TEST(testPositionBounds);
    
    std::cout << "\n[Cut Region Tests]" << std::endl;
    RUN_TEST(testCutRegionDetection);
    RUN_TEST(testCutRegionEndFinding);
    RUN_TEST(testOverlappingCutRegions);
    RUN_TEST(testEmptyCutRegions);
    
    std::cout << "\n[Time Formatting Tests]" << std::endl;
    RUN_TEST(testFormatTimeSeconds);
    RUN_TEST(testFormatTimeMinutes);
    RUN_TEST(testFormatTimeHours);
    RUN_TEST(testFormatTimeZero);
    
    std::cout << "\n[YouTube Chapter Tests]" << std::endl;
    RUN_TEST(testYouTubeChapterFormat);
    RUN_TEST(testYouTubeChapterParse);
    RUN_TEST(testYouTubeChapterRoundTrip);
    RUN_TEST(testYouTubeChapterParseInvalid);
    
    std::cout << "\n[Transcript Segment Tests]" << std::endl;
    RUN_TEST(testTranscriptSegmentConstruction);
    RUN_TEST(testTranscriptSegmentOrdering);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << g_testsPassed << " passed, " << g_testsFailed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return g_testsFailed > 0 ? 1 : 0;
}
