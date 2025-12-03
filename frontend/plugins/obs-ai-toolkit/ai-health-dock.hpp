/*
 * AI Health Dashboard Dock
 * Real-time stream health monitoring with AI recommendations
 */

#pragma once

#include <obs.hpp>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QFrame>
#include <QTimer>
#include <QPointer>
#include <QLabel>
#include <QProgressBar>
#include <QListWidget>
#include <QVBoxLayout>

#include <vector>
#include <string>
#include <chrono>

class QualityAdvisor;

/* Health score thresholds */
constexpr int HEALTH_EXCELLENT = 90;
constexpr int HEALTH_GOOD = 75;
constexpr int HEALTH_WARNING = 50;
constexpr int HEALTH_CRITICAL = 25;

/* Metric thresholds */
constexpr double CPU_WARN_THRESHOLD = 80.0;
constexpr double CPU_CRITICAL_THRESHOLD = 90.0;
constexpr double DROPPED_WARN_THRESHOLD = 1.0;
constexpr double DROPPED_CRITICAL_THRESHOLD = 5.0;
constexpr double CONGESTION_WARN_THRESHOLD = 0.3;
constexpr double CONGESTION_CRITICAL_THRESHOLD = 0.5;

struct StreamMetrics {
	double cpuUsage = 0.0;
	double droppedFramesPercent = 0.0;
	double bitrate = 0.0;
	double congestion = 0.0;
	bool encoderLagging = false;
	int totalFrames = 0;
	int droppedFrames = 0;
};

struct Issue {
	std::string message;
	int severity; // 0=info, 1=warning, 2=critical
	std::chrono::steady_clock::time_point timestamp;
};

class HealthScoreWidget : public QFrame {
	Q_OBJECT

public:
	explicit HealthScoreWidget(QWidget *parent = nullptr);
	void setScore(int score);
	int score() const { return m_score; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	int m_score = 100;
	QColor getColorForScore(int score);
};

class AIHealthDock : public QFrame {
	Q_OBJECT

public:
	explicit AIHealthDock(QWidget *parent = nullptr);
	~AIHealthDock();

	void setMetrics(const StreamMetrics &metrics);
	void addIssue(const std::string &message, int severity);
	void addRecommendation(const std::string &recommendation);
	void clearIssues();
	void clearRecommendations();

private slots:
	void updateMetrics();

private:
	void setupUI();
	int calculateHealthScore(const StreamMetrics &metrics);
	QString formatBitrate(double bps);
	QString formatPercent(double percent);

	/* UI Components */
	HealthScoreWidget *healthScoreWidget = nullptr;
	QLabel *cpuLabel = nullptr;
	QLabel *bitrateLabel = nullptr;
	QLabel *droppedLabel = nullptr;
	QLabel *networkLabel = nullptr;
	QProgressBar *cpuBar = nullptr;
	QProgressBar *networkBar = nullptr;
	QListWidget *issuesList = nullptr;
	QListWidget *recommendationsList = nullptr;

	/* Metrics */
	StreamMetrics currentMetrics;
	std::vector<Issue> activeIssues;
	std::vector<std::string> recommendations;

	/* Timer for updates */
	QTimer updateTimer;

	/* CPU tracking */
	os_cpu_usage_info_t *cpuInfo = nullptr;

	/* Quality Advisor */
	QualityAdvisor *advisor = nullptr;
};

/* Global functions for module integration */
#ifdef __cplusplus
extern "C" {
#endif

void InitAIHealthDock(void);
void FreeAIHealthDock(void);

#ifdef __cplusplus
}
#endif

