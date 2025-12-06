/*
 * Quality Advisor
 * Monitors stream metrics and provides AI recommendations
 */

#pragma once

#include "ai-health-dock.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <map>
#include <string>
#include <chrono>

class QualityAdvisor : public QObject {
	Q_OBJECT

public:
	explicit QualityAdvisor(QObject *parent = nullptr);
	~QualityAdvisor();

	void analyzeMetrics(const StreamMetrics &metrics);
	void reset();

signals:
	void issueDetected(const QString &message, int severity);
	void recommendationGenerated(const QString &recommendation);

private:
	struct IssueState {
		std::chrono::steady_clock::time_point lastTriggered;
		int triggerCount = 0;
	};

	bool shouldTriggerIssue(const std::string &issueType);
	void generateRecommendations(const StreamMetrics &metrics);

	/* Cooldown tracking per issue type */
	std::map<std::string, IssueState> issueStates;

	/* Cooldown duration (60 seconds) */
	static constexpr int COOLDOWN_SECONDS = 60;

	/* Previous metrics for trend detection */
	StreamMetrics previousMetrics;
	bool hasPreviousMetrics = false;
};


