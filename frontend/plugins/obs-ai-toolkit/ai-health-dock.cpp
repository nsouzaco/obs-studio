/*
 * AI Health Dashboard Dock Implementation
 */

#include "ai-health-dock.hpp"
#include "quality-advisor.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <qt-wrappers.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QDockWidget>
#include <QMainWindow>

#define UPDATE_INTERVAL 2000 /* 2 seconds */

/* ========================================================================== */
/* Health Score Widget Implementation                                          */
/* ========================================================================== */

HealthScoreWidget::HealthScoreWidget(QWidget *parent)
	: QFrame(parent),
	  m_score(100)
{
	setMinimumSize(120, 120);
	setMaximumSize(150, 150);
}

void HealthScoreWidget::setScore(int score)
{
	m_score = qBound(0, score, 100);
	update();
}

QColor HealthScoreWidget::getColorForScore(int score)
{
	if (score >= HEALTH_EXCELLENT)
		return QColor(63, 185, 80);  /* Green */
	else if (score >= HEALTH_GOOD)
		return QColor(136, 193, 82); /* Light green */
	else if (score >= HEALTH_WARNING)
		return QColor(210, 153, 34); /* Yellow/Orange */
	else if (score >= HEALTH_CRITICAL)
		return QColor(248, 81, 73);  /* Red */
	else
		return QColor(200, 50, 50);  /* Dark red */
}

void HealthScoreWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	int side = qMin(width(), height());
	int x = (width() - side) / 2;
	int y = (height() - side) / 2;
	int margin = 10;
	int penWidth = 8;

	QRectF rect(x + margin, y + margin, side - 2 * margin, side - 2 * margin);

	/* Draw background circle */
	QPen bgPen(QColor(48, 54, 61), penWidth);
	bgPen.setCapStyle(Qt::RoundCap);
	painter.setPen(bgPen);
	painter.drawArc(rect, 0, 360 * 16);

	/* Draw score arc */
	QColor scoreColor = getColorForScore(m_score);
	QPen scorePen(scoreColor, penWidth);
	scorePen.setCapStyle(Qt::RoundCap);
	painter.setPen(scorePen);

	int spanAngle = (m_score * 360 * 16) / 100;
	painter.drawArc(rect, 90 * 16, -spanAngle);

	/* Draw score text */
	painter.setPen(Qt::white);
	QFont font = painter.font();
	font.setPointSize(24);
	font.setBold(true);
	painter.setFont(font);
	painter.drawText(rect, Qt::AlignCenter, QString::number(m_score));

	/* Draw label */
	font.setPointSize(10);
	font.setBold(false);
	painter.setFont(font);
	painter.setPen(QColor(139, 148, 158));
	QRectF labelRect = rect;
	labelRect.moveTop(labelRect.top() + 30);
	painter.drawText(labelRect, Qt::AlignCenter, obs_module_text("AIHealthDock.HealthScore"));
}

/* ========================================================================== */
/* AI Health Dock Implementation                                               */
/* ========================================================================== */

AIHealthDock::AIHealthDock(QWidget *parent)
	: QFrame(parent)
{
	cpuInfo = os_cpu_usage_info_start();
	setupUI();

	/* Create quality advisor */
	advisor = new QualityAdvisor(this);
	connect(advisor, &QualityAdvisor::issueDetected, this,
		[this](const QString &msg, int severity) {
			addIssue(msg.toStdString(), severity);
		});
	connect(advisor, &QualityAdvisor::recommendationGenerated, this,
		[this](const QString &rec) {
			addRecommendation(rec.toStdString());
		});

	/* Setup update timer */
	connect(&updateTimer, &QTimer::timeout, this, &AIHealthDock::updateMetrics);
	updateTimer.start(UPDATE_INTERVAL);
}

AIHealthDock::~AIHealthDock()
{
	updateTimer.stop();
	if (cpuInfo)
		os_cpu_usage_info_destroy(cpuInfo);
	delete advisor;
}

void AIHealthDock::setupUI()
{
	setObjectName("AIHealthDock");
	setStyleSheet(
		"QFrame#AIHealthDock { background-color: #0d1117; }"
		"QLabel { color: #f0f6fc; }"
		"QGroupBox { color: #8b949e; border: 1px solid #30363d; border-radius: 6px; margin-top: 8px; padding-top: 8px; }"
		"QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
		"QProgressBar { background-color: #21262d; border: none; border-radius: 3px; height: 6px; }"
		"QProgressBar::chunk { background-color: #3fb950; border-radius: 3px; }"
		"QListWidget { background-color: #161b22; border: 1px solid #30363d; border-radius: 6px; color: #f0f6fc; }"
		"QListWidget::item { padding: 4px; }"
	);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(12, 12, 12, 12);
	mainLayout->setSpacing(12);

	/* Health Score */
	QHBoxLayout *scoreLayout = new QHBoxLayout();
	healthScoreWidget = new HealthScoreWidget(this);
	scoreLayout->addStretch();
	scoreLayout->addWidget(healthScoreWidget);
	scoreLayout->addStretch();
	mainLayout->addLayout(scoreLayout);

	/* Metrics Grid */
	QGridLayout *metricsLayout = new QGridLayout();
	metricsLayout->setSpacing(8);

	/* CPU */
	QLabel *cpuTitle = new QLabel(obs_module_text("AIHealthDock.CPU"), this);
	cpuTitle->setStyleSheet("color: #8b949e; font-size: 11px;");
	cpuLabel = new QLabel("0%", this);
	cpuLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
	cpuBar = new QProgressBar(this);
	cpuBar->setRange(0, 100);
	cpuBar->setValue(0);
	cpuBar->setTextVisible(false);
	metricsLayout->addWidget(cpuTitle, 0, 0);
	metricsLayout->addWidget(cpuLabel, 1, 0);
	metricsLayout->addWidget(cpuBar, 2, 0);

	/* Bitrate */
	QLabel *bitrateTitle = new QLabel(obs_module_text("AIHealthDock.Bitrate"), this);
	bitrateTitle->setStyleSheet("color: #8b949e; font-size: 11px;");
	bitrateLabel = new QLabel("0 kbps", this);
	bitrateLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
	metricsLayout->addWidget(bitrateTitle, 0, 1);
	metricsLayout->addWidget(bitrateLabel, 1, 1);

	/* Dropped Frames */
	QLabel *droppedTitle = new QLabel(obs_module_text("AIHealthDock.DroppedFrames"), this);
	droppedTitle->setStyleSheet("color: #8b949e; font-size: 11px;");
	droppedLabel = new QLabel("0%", this);
	droppedLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
	metricsLayout->addWidget(droppedTitle, 0, 2);
	metricsLayout->addWidget(droppedLabel, 1, 2);

	/* Network */
	QLabel *networkTitle = new QLabel(obs_module_text("AIHealthDock.NetworkCongestion"), this);
	networkTitle->setStyleSheet("color: #8b949e; font-size: 11px;");
	networkLabel = new QLabel("0%", this);
	networkLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
	networkBar = new QProgressBar(this);
	networkBar->setRange(0, 100);
	networkBar->setValue(0);
	networkBar->setTextVisible(false);
	metricsLayout->addWidget(networkTitle, 0, 3);
	metricsLayout->addWidget(networkLabel, 1, 3);
	metricsLayout->addWidget(networkBar, 2, 3);

	mainLayout->addLayout(metricsLayout);

	/* Issues */
	QGroupBox *issuesGroup = new QGroupBox(obs_module_text("AIHealthDock.Issues"), this);
	QVBoxLayout *issuesLayout = new QVBoxLayout(issuesGroup);
	issuesList = new QListWidget(this);
	issuesList->setMaximumHeight(80);
	issuesList->addItem(obs_module_text("AIHealthDock.NoIssues"));
	issuesLayout->addWidget(issuesList);
	mainLayout->addWidget(issuesGroup);

	/* Recommendations */
	QGroupBox *recsGroup = new QGroupBox(obs_module_text("AIHealthDock.Recommendations"), this);
	QVBoxLayout *recsLayout = new QVBoxLayout(recsGroup);
	recommendationsList = new QListWidget(this);
	recommendationsList->setMaximumHeight(80);
	recommendationsList->addItem(obs_module_text("AIHealthDock.Healthy"));
	recsLayout->addWidget(recommendationsList);
	mainLayout->addWidget(recsGroup);

	mainLayout->addStretch();
}

void AIHealthDock::updateMetrics()
{
	StreamMetrics metrics;

	/* Get CPU usage */
	if (cpuInfo) {
		metrics.cpuUsage = os_cpu_usage_info_query(cpuInfo);
	}

	/* Get output stats */
	obs_output_t *streamOutput = obs_frontend_get_streaming_output();
	obs_output_t *recordOutput = obs_frontend_get_recording_output();
	obs_output_t *output = streamOutput ? streamOutput : recordOutput;

	if (output) {
		metrics.droppedFrames = obs_output_get_frames_dropped(output);
		metrics.totalFrames = obs_output_get_total_frames(output);
		
		if (metrics.totalFrames > 0) {
			metrics.droppedFramesPercent = 
				(double)metrics.droppedFrames / metrics.totalFrames * 100.0;
		}

		metrics.congestion = obs_output_get_congestion(output);

		/* Calculate bitrate */
		uint64_t totalBytes = obs_output_get_total_bytes(output);
		/* This is simplified - proper implementation would track over time */
		metrics.bitrate = totalBytes * 8.0 / 1000.0; /* Convert to kbps */
	}

	if (streamOutput)
		obs_output_release(streamOutput);
	if (recordOutput)
		obs_output_release(recordOutput);

	setMetrics(metrics);

	/* Update quality advisor */
	if (advisor) {
		advisor->analyzeMetrics(metrics);
	}
}

void AIHealthDock::setMetrics(const StreamMetrics &metrics)
{
	currentMetrics = metrics;

	/* Update health score */
	int score = calculateHealthScore(metrics);
	healthScoreWidget->setScore(score);

	/* Update CPU */
	cpuLabel->setText(formatPercent(metrics.cpuUsage));
	cpuBar->setValue((int)metrics.cpuUsage);
	if (metrics.cpuUsage >= CPU_CRITICAL_THRESHOLD) {
		cpuBar->setStyleSheet("QProgressBar::chunk { background-color: #f85149; }");
	} else if (metrics.cpuUsage >= CPU_WARN_THRESHOLD) {
		cpuBar->setStyleSheet("QProgressBar::chunk { background-color: #d29922; }");
	} else {
		cpuBar->setStyleSheet("QProgressBar::chunk { background-color: #3fb950; }");
	}

	/* Update Bitrate */
	bitrateLabel->setText(formatBitrate(metrics.bitrate));

	/* Update Dropped Frames */
	droppedLabel->setText(formatPercent(metrics.droppedFramesPercent));
	if (metrics.droppedFramesPercent >= DROPPED_CRITICAL_THRESHOLD) {
		droppedLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #f85149;");
	} else if (metrics.droppedFramesPercent >= DROPPED_WARN_THRESHOLD) {
		droppedLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #d29922;");
	} else {
		droppedLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #3fb950;");
	}

	/* Update Network */
	int networkPercent = (int)(metrics.congestion * 100);
	networkLabel->setText(QString("%1%").arg(networkPercent));
	networkBar->setValue(networkPercent);
	if (metrics.congestion >= CONGESTION_CRITICAL_THRESHOLD) {
		networkBar->setStyleSheet("QProgressBar::chunk { background-color: #f85149; }");
	} else if (metrics.congestion >= CONGESTION_WARN_THRESHOLD) {
		networkBar->setStyleSheet("QProgressBar::chunk { background-color: #d29922; }");
	} else {
		networkBar->setStyleSheet("QProgressBar::chunk { background-color: #3fb950; }");
	}
}

int AIHealthDock::calculateHealthScore(const StreamMetrics &metrics)
{
	int score = 100;

	/* CPU penalty */
	if (metrics.cpuUsage >= CPU_CRITICAL_THRESHOLD)
		score -= 30;
	else if (metrics.cpuUsage >= CPU_WARN_THRESHOLD)
		score -= 15;

	/* Dropped frames penalty */
	if (metrics.droppedFramesPercent >= DROPPED_CRITICAL_THRESHOLD)
		score -= 30;
	else if (metrics.droppedFramesPercent >= DROPPED_WARN_THRESHOLD)
		score -= 15;

	/* Network congestion penalty */
	if (metrics.congestion >= CONGESTION_CRITICAL_THRESHOLD)
		score -= 25;
	else if (metrics.congestion >= CONGESTION_WARN_THRESHOLD)
		score -= 10;

	/* Encoder lag penalty */
	if (metrics.encoderLagging)
		score -= 20;

	return qMax(0, score);
}

QString AIHealthDock::formatBitrate(double kbps)
{
	if (kbps >= 1000)
		return QString("%1 Mbps").arg(kbps / 1000.0, 0, 'f', 1);
	return QString("%1 kbps").arg((int)kbps);
}

QString AIHealthDock::formatPercent(double percent)
{
	return QString("%1%").arg(percent, 0, 'f', 1);
}

void AIHealthDock::addIssue(const std::string &message, int severity)
{
	/* Clear "no issues" placeholder */
	if (issuesList->count() == 1 && 
	    issuesList->item(0)->text() == obs_module_text("AIHealthDock.NoIssues")) {
		issuesList->clear();
	}

	QString icon = severity >= 2 ? "⚠️" : "ℹ️";
	QString text = QString("%1 %2").arg(icon, QString::fromStdString(message));
	
	QListWidgetItem *item = new QListWidgetItem(text, issuesList);
	if (severity >= 2)
		item->setForeground(QColor("#f85149"));
	else if (severity == 1)
		item->setForeground(QColor("#d29922"));

	/* Keep list manageable */
	while (issuesList->count() > 5)
		delete issuesList->takeItem(0);
}

void AIHealthDock::addRecommendation(const std::string &recommendation)
{
	/* Clear "healthy" placeholder */
	if (recommendationsList->count() == 1 && 
	    recommendationsList->item(0)->text() == obs_module_text("AIHealthDock.Healthy")) {
		recommendationsList->clear();
	}

	QString text = QString("💡 %1").arg(QString::fromStdString(recommendation));
	new QListWidgetItem(text, recommendationsList);

	/* Keep list manageable */
	while (recommendationsList->count() > 5)
		delete recommendationsList->takeItem(0);
}

void AIHealthDock::clearIssues()
{
	issuesList->clear();
	issuesList->addItem(obs_module_text("AIHealthDock.NoIssues"));
}

void AIHealthDock::clearRecommendations()
{
	recommendationsList->clear();
	recommendationsList->addItem(obs_module_text("AIHealthDock.Healthy"));
}

/* ========================================================================== */
/* Module Integration                                                          */
/* ========================================================================== */

static AIHealthDock *healthDock = nullptr;

extern "C" void InitAIHealthDock(void)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	if (!mainWindow)
		return;

	obs_frontend_push_ui_translation(obs_module_get_string);

	/* Create the dock content widget */
	healthDock = new AIHealthDock();

	/* Use obs_frontend_add_dock_by_id which creates the dock wrapper */
	bool added = obs_frontend_add_dock_by_id(
		"obs-ai-toolkit-health",
		obs_module_text("AIHealthDock"),
		healthDock);

	if (!added) {
		delete healthDock;
		healthDock = nullptr;
	}

	obs_frontend_pop_ui_translation();
}

extern "C" void FreeAIHealthDock(void)
{
	/* The dock is managed by OBS frontend, just remove it */
	obs_frontend_remove_dock("obs-ai-toolkit-health");
	healthDock = nullptr;
}

#include "moc_ai-health-dock.cpp"

