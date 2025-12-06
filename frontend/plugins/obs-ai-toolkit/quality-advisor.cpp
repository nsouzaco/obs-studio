/*
 * Quality Advisor Implementation
 */

#include "quality-advisor.hpp"

#include <obs-module.h>

QualityAdvisor::QualityAdvisor(QObject *parent)
	: QObject(parent)
{
}

QualityAdvisor::~QualityAdvisor()
{
}

bool QualityAdvisor::shouldTriggerIssue(const std::string &issueType)
{
	auto now = std::chrono::steady_clock::now();
	auto &state = issueStates[issueType];

	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		now - state.lastTriggered).count();

	if (elapsed >= COOLDOWN_SECONDS || state.triggerCount == 0) {
		state.lastTriggered = now;
		state.triggerCount++;
		return true;
	}

	return false;
}

void QualityAdvisor::analyzeMetrics(const StreamMetrics &metrics)
{
	/* Check CPU usage */
	if (metrics.cpuUsage >= CPU_CRITICAL_THRESHOLD) {
		if (shouldTriggerIssue("cpu_critical")) {
			QString msg = QString(obs_module_text("QualityAdvisor.CriticalCPU"))
				.arg(metrics.cpuUsage, 0, 'f', 1);
			emit issueDetected(msg, 2);
		}
	} else if (metrics.cpuUsage >= CPU_WARN_THRESHOLD) {
		if (shouldTriggerIssue("cpu_warn")) {
			QString msg = QString(obs_module_text("QualityAdvisor.HighCPU"))
				.arg(metrics.cpuUsage, 0, 'f', 1);
			emit issueDetected(msg, 1);
		}
	}

	/* Check dropped frames */
	if (metrics.droppedFramesPercent >= DROPPED_CRITICAL_THRESHOLD) {
		if (shouldTriggerIssue("dropped_critical")) {
			QString msg = QString(obs_module_text("QualityAdvisor.DroppedFrames"))
				.arg(metrics.droppedFramesPercent, 0, 'f', 1);
			emit issueDetected(msg, 2);
		}
	} else if (metrics.droppedFramesPercent >= DROPPED_WARN_THRESHOLD) {
		if (shouldTriggerIssue("dropped_warn")) {
			QString msg = QString(obs_module_text("QualityAdvisor.DroppedFrames"))
				.arg(metrics.droppedFramesPercent, 0, 'f', 1);
			emit issueDetected(msg, 1);
		}
	}

	/* Check network congestion */
	if (metrics.congestion >= CONGESTION_CRITICAL_THRESHOLD) {
		if (shouldTriggerIssue("network_critical")) {
			int percent = (int)(metrics.congestion * 100);
			QString msg = QString(obs_module_text("QualityAdvisor.NetworkCongestion"))
				.arg(percent);
			emit issueDetected(msg, 2);
		}
	} else if (metrics.congestion >= CONGESTION_WARN_THRESHOLD) {
		if (shouldTriggerIssue("network_warn")) {
			int percent = (int)(metrics.congestion * 100);
			QString msg = QString(obs_module_text("QualityAdvisor.NetworkCongestion"))
				.arg(percent);
			emit issueDetected(msg, 1);
		}
	}

	/* Check encoder lag */
	if (metrics.encoderLagging) {
		if (shouldTriggerIssue("encoder_lag")) {
			emit issueDetected(obs_module_text("QualityAdvisor.EncoderLag"), 2);
		}
	}

	/* Generate recommendations based on current state */
	generateRecommendations(metrics);

	/* Store for trend analysis */
	previousMetrics = metrics;
	hasPreviousMetrics = true;
}

void QualityAdvisor::generateRecommendations(const StreamMetrics &metrics)
{
	/* Only generate recommendations on significant issues */
	
	/* High CPU recommendation */
	if (metrics.cpuUsage >= CPU_CRITICAL_THRESHOLD) {
		if (shouldTriggerIssue("rec_cpu")) {
			emit recommendationGenerated(
				"Consider changing encoder preset from 'medium' to 'veryfast' "
				"or lowering output resolution");
		}
	}

	/* High dropped frames recommendation */
	if (metrics.droppedFramesPercent >= DROPPED_CRITICAL_THRESHOLD) {
		if (shouldTriggerIssue("rec_dropped")) {
			emit recommendationGenerated(
				"Reduce streaming bitrate by 20-30% or check network stability");
		}
	}

	/* Network congestion recommendation */
	if (metrics.congestion >= CONGESTION_CRITICAL_THRESHOLD) {
		if (shouldTriggerIssue("rec_network")) {
			emit recommendationGenerated(
				"Consider using a wired ethernet connection instead of WiFi");
		}
	}

	/* Encoder lag recommendation */
	if (metrics.encoderLagging) {
		if (shouldTriggerIssue("rec_encoder")) {
			emit recommendationGenerated(
				"Lower output resolution from 1080p to 720p or use GPU encoding");
		}
	}
}

void QualityAdvisor::reset()
{
	issueStates.clear();
	hasPreviousMetrics = false;
}

#include "moc_quality-advisor.cpp"


