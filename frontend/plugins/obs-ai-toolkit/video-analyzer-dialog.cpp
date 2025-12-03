/*
 * Combined Video Analyzer Dialog Implementation
 */

#include "video-analyzer-dialog.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTextStream>
#include <QMainWindow>
#include <QProcess>
#include <QDir>
#include <QPainter>
#include <QMouseEvent>
#include <QStyle>
#include <QAudioOutput>
#include <QScrollBar>
#include <QScrollArea>

/* ========================================================================== */
/* Video Timeline Widget                                                       */
/* ========================================================================== */

VideoTimeline::VideoTimeline(QWidget *parent)
	: QWidget(parent)
{
	setMinimumHeight(70);
	setMaximumHeight(70);
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void VideoTimeline::setDuration(qint64 duration)
{
	totalDuration = duration;
	updateSize();
	update();
}

void VideoTimeline::setPosition(qint64 position)
{
	currentPosition = position;
	update();
}

void VideoTimeline::updateSize()
{
	if (totalDuration > 0) {
		/* Calculate width: PIXELS_PER_SECOND pixels per second of video */
		int durationSec = totalDuration / 1000;
		calculatedWidth = qMax(400, durationSec * PIXELS_PER_SECOND);
		setMinimumWidth(calculatedWidth);
		setMaximumWidth(calculatedWidth);
	} else {
		calculatedWidth = 400;
		setMinimumWidth(400);
		setMaximumWidth(16777215); /* QWIDGETSIZE_MAX */
	}
	updateGeometry();
}

QSize VideoTimeline::sizeHint() const
{
	return QSize(calculatedWidth, 70);
}

QSize VideoTimeline::minimumSizeHint() const
{
	return QSize(400, 70);
}

int VideoTimeline::getPlayheadX() const
{
	if (totalDuration <= 0) return 0;
	double progress = (double)currentPosition / totalDuration;
	return (int)(calculatedWidth * progress);
}

void VideoTimeline::setWaveform(const std::vector<float> &waveformData)
{
	waveform = waveformData;
	update();
}

void VideoTimeline::clearWaveform()
{
	waveform.clear();
	update();
}

void VideoTimeline::clearSelection()
{
	selectionStart = -1;
	selectionEnd = -1;
	emit selectionChanged(-1, -1);
	update();
}

void VideoTimeline::addCutRegion(qint64 start, qint64 end)
{
	cutRegions.push_back({start, end});
	/* Sort by start time */
	std::sort(cutRegions.begin(), cutRegions.end(),
		[](const auto &a, const auto &b) { return a.first < b.first; });
	update();
}

void VideoTimeline::removeCutRegion(int index)
{
	if (index >= 0 && index < (int)cutRegions.size()) {
		cutRegions.erase(cutRegions.begin() + index);
		update();
	}
}

void VideoTimeline::clearCutRegions()
{
	cutRegions.clear();
	update();
}

bool VideoTimeline::isPositionCut(qint64 position) const
{
	for (const auto &region : cutRegions) {
		if (position >= region.first && position < region.second) {
			return true;
		}
	}
	return false;
}

int VideoTimeline::positionToX(qint64 pos) const
{
	if (totalDuration <= 0) return 0;
	return (int)((double)pos / totalDuration * calculatedWidth);
}

qint64 VideoTimeline::xToPosition(int x) const
{
	if (calculatedWidth <= 0) return 0;
	return (qint64)((double)x / calculatedWidth * totalDuration);
}

void VideoTimeline::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	int w = calculatedWidth;
	int h = height();

	/* Background */
	painter.fillRect(0, 0, w, h, QColor(30, 30, 30));

	/* Layout: time markers at top (15px), waveform in middle (35px), track at bottom (20px) */
	int timeMarkerH = 15;
	int waveformY = timeMarkerH;
	int waveformH = 35;
	int trackY = waveformY + waveformH;
	int trackH = 20;

	/* Draw time markers */
	drawTimeMarkers(painter);

	/* Draw waveform */
	if (!waveform.empty()) {
		drawWaveform(painter);
	} else {
		/* Empty waveform area */
		painter.fillRect(0, waveformY, w, waveformH, QColor(40, 40, 40));
	}

	/* Track background */
	painter.fillRect(0, trackY, w, trackH, QColor(50, 50, 50));

	if (totalDuration <= 0)
		return;

	/* Draw cut regions (faded out sections) */
	drawCutRegions(painter);

	/* Draw selection overlay */
	drawSelection(painter);

	/* Progress bar */
	double progress = (double)currentPosition / totalDuration;
	int progressW = (int)(w * progress);
	painter.fillRect(0, trackY, progressW, trackH, QColor(0, 122, 204, 150));

	/* Position indicator (playhead) */
	int posX = (int)(w * progress);
	painter.setPen(QPen(QColor(255, 0, 0), 2));
	painter.drawLine(posX, 0, posX, h);
	
	/* Playhead handle */
	painter.setBrush(QColor(255, 50, 50));
	painter.setPen(Qt::NoPen);
	painter.drawEllipse(QPoint(posX, trackY + trackH / 2), 6, 6);
}

void VideoTimeline::drawSelection(QPainter &painter)
{
	if (selectionStart < 0 || selectionEnd <= selectionStart)
		return;

	int h = height();
	int startX = positionToX(selectionStart);
	int endX = positionToX(selectionEnd);
	int selWidth = endX - startX;

	/* Selection overlay */
	painter.fillRect(startX, 0, selWidth, h, QColor(100, 150, 255, 60));
	
	/* Selection border */
	painter.setPen(QPen(QColor(100, 150, 255), 2));
	painter.drawRect(startX, 0, selWidth, h - 1);

	/* Left handle */
	painter.fillRect(startX - 4, 0, 8, h, QColor(100, 150, 255, 200));
	painter.setPen(QPen(Qt::white, 1));
	painter.drawLine(startX, 10, startX, h - 10);
	painter.drawLine(startX - 2, h / 2 - 5, startX - 2, h / 2 + 5);
	painter.drawLine(startX + 2, h / 2 - 5, startX + 2, h / 2 + 5);

	/* Right handle */
	painter.fillRect(endX - 4, 0, 8, h, QColor(100, 150, 255, 200));
	painter.drawLine(endX, 10, endX, h - 10);
	painter.drawLine(endX - 2, h / 2 - 5, endX - 2, h / 2 + 5);
	painter.drawLine(endX + 2, h / 2 - 5, endX + 2, h / 2 + 5);
}

void VideoTimeline::drawCutRegions(QPainter &painter)
{
	int h = height();
	
	for (const auto &region : cutRegions) {
		int startX = positionToX(region.first);
		int endX = positionToX(region.second);
		int regionWidth = endX - startX;
		
		/* Faded/grayed out overlay - simple dark fill */
		painter.fillRect(startX, 0, regionWidth, h, QColor(0, 0, 0, 180));
		
		/* Red border to indicate cut */
		painter.setPen(QPen(QColor(200, 50, 50), 2));
		painter.drawLine(startX, 0, startX, h);
		painter.drawLine(endX, 0, endX, h);
		
		/* "CUT" label */
		painter.setPen(QColor(200, 80, 80));
		painter.setFont(QFont("Arial", 9, QFont::Bold));
		if (regionWidth > 40) {
			painter.drawText(startX + 5, h / 2 + 4, "CUT");
		}
	}
}

void VideoTimeline::drawTimeMarkers(QPainter &painter)
{
	int w = calculatedWidth;
	int h = 15;
	
	painter.fillRect(0, 0, w, h, QColor(35, 35, 35));
	
	if (totalDuration <= 0)
		return;

	painter.setFont(QFont("Arial", 9));

	double durationSec = totalDuration / 1000.0;
	
	/* Fixed tick interval: every 10 seconds with labels, minor ticks every 5 seconds */
	double majorInterval = 10.0;
	double minorInterval = 5.0;

	/* Draw minor ticks */
	painter.setPen(QColor(60, 60, 60));
	for (double t = 0; t <= durationSec; t += minorInterval) {
		int x = (int)(t * PIXELS_PER_SECOND);
		painter.drawLine(x, h - 2, x, h);
	}

	/* Draw major ticks and labels */
	for (double t = 0; t <= durationSec; t += majorInterval) {
		int x = (int)(t * PIXELS_PER_SECOND);
		
		/* Major tick mark */
		painter.setPen(QColor(100, 100, 100));
		painter.drawLine(x, h - 5, x, h);
		
		/* Time label */
		int mins = (int)(t / 60);
		int secs = (int)t % 60;
		QString label = QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
		
		painter.setPen(QColor(150, 150, 150));
		painter.drawText(x + 2, h - 4, label);
	}
}

void VideoTimeline::drawWaveform(QPainter &painter)
{
	int w = calculatedWidth;
	int waveformY = 15; /* After time markers */
	int waveformH = 35;
	
	/* Background */
	painter.fillRect(0, waveformY, w, waveformH, QColor(20, 20, 25));
	
	if (waveform.empty())
		return;

	/* Draw waveform as mirrored bars from center */
	int numSamples = waveform.size();
	float barWidth = (float)w / numSamples;
	int centerY = waveformY + waveformH / 2;
	int maxBarHeight = waveformH / 2 - 2;

	painter.setPen(Qt::NoPen);
	
	for (int i = 0; i < numSamples; i++) {
		float amplitude = waveform[i];
		int barHeight = (int)(amplitude * maxBarHeight);
		if (barHeight < 1) barHeight = 1;
		
		int x = (int)(i * barWidth);
		
		/* Gradient color based on amplitude: blue -> cyan -> green for louder */
		int r = (int)(30 + amplitude * 50);
		int g = (int)(150 + amplitude * 105);
		int b = (int)(200 - amplitude * 50);
		
		QColor barColor(r, g, b, 220);
		painter.setBrush(barColor);
		
		/* Draw mirrored bars (above and below center line) */
		int barW = qMax(2, (int)barWidth);
		painter.drawRect(x, centerY - barHeight, barW, barHeight);
		painter.drawRect(x, centerY, barW, barHeight);
	}
	
	/* Draw center line */
	painter.setPen(QPen(QColor(60, 60, 70), 1));
	painter.drawLine(0, centerY, w, centerY);
}

void VideoTimeline::mousePressEvent(QMouseEvent *event)
{
	if (totalDuration <= 0)
		return;

	int x = event->pos().x();
	int startX = positionToX(selectionStart);
	int endX = positionToX(selectionEnd);

	/* Check if clicking on handles */
	if (hasSelection()) {
		if (abs(x - startX) < 10) {
			dragMode = DragLeftHandle;
			setCursor(Qt::SizeHorCursor);
			return;
		}
		if (abs(x - endX) < 10) {
			dragMode = DragRightHandle;
			setCursor(Qt::SizeHorCursor);
			return;
		}
		/* Check if clicking inside selection to drag it */
		if (x > startX + 10 && x < endX - 10) {
			dragMode = DragSelection;
			dragStartX = x;
			dragStartPos = selectionStart;
			setCursor(Qt::ClosedHandCursor);
			return;
		}
	}

	/* Start new selection with Shift+Click, otherwise seek */
	if (event->modifiers() & Qt::ShiftModifier) {
		dragMode = Creating;
		selectionStart = xToPosition(x);
		selectionEnd = selectionStart;
		dragStartX = x;
		update();
	} else {
		/* Normal click - seek */
		qint64 position = xToPosition(x);
		emit positionClicked(position);
	}
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *event)
{
	if (totalDuration <= 0)
		return;

	int x = event->pos().x();
	x = qMax(0, qMin(x, calculatedWidth));
	qint64 pos = xToPosition(x);

	switch (dragMode) {
	case DragLeftHandle:
		selectionStart = qMin(pos, selectionEnd - 100);
		emit selectionChanged(selectionStart, selectionEnd);
		update();
		break;
	case DragRightHandle:
		selectionEnd = qMax(pos, selectionStart + 100);
		emit selectionChanged(selectionStart, selectionEnd);
		update();
		break;
	case DragSelection: {
		qint64 delta = xToPosition(x) - xToPosition(dragStartX);
		qint64 duration = selectionEnd - selectionStart;
		qint64 newStart = dragStartPos + delta;
		newStart = qMax((qint64)0, qMin(newStart, totalDuration - duration));
		selectionStart = newStart;
		selectionEnd = newStart + duration;
		emit selectionChanged(selectionStart, selectionEnd);
		update();
		break;
	}
	case Creating:
		selectionEnd = qMax(pos, selectionStart + 100);
		emit selectionChanged(selectionStart, selectionEnd);
		update();
		break;
	default:
		/* Update cursor based on position */
		if (hasSelection()) {
			int startX = positionToX(selectionStart);
			int endX = positionToX(selectionEnd);
			if (abs(x - startX) < 10 || abs(x - endX) < 10) {
				setCursor(Qt::SizeHorCursor);
			} else if (x > startX + 10 && x < endX - 10) {
				setCursor(Qt::OpenHandCursor);
			} else {
				setCursor(Qt::PointingHandCursor);
			}
		}
		break;
	}
}

void VideoTimeline::mouseReleaseEvent(QMouseEvent *event)
{
	Q_UNUSED(event);
	dragMode = None;
	setCursor(Qt::PointingHandCursor);
	
	if (hasSelection()) {
		emit selectionChanged(selectionStart, selectionEnd);
	}
}

/* ========================================================================== */
/* Video Analysis Worker                                                       */
/* ========================================================================== */

VideoAnalysisWorker::VideoAnalysisWorker(QObject *parent)
	: QObject(parent)
{
}

VideoAnalysisWorker::~VideoAnalysisWorker()
{
}

void VideoAnalysisWorker::setVideoPath(const QString &path) { videoPath = path; }
void VideoAnalysisWorker::setTranscriptionModel(const QString &model) { modelName = model; }
void VideoAnalysisWorker::setLanguage(const QString &lang) { languageCode = lang; }

void VideoAnalysisWorker::process()
{
	emit progressUpdated(5, "Transcribing audio...");
	runTranscription();
	emit finished();
}

void VideoAnalysisWorker::runTranscription()
{
	QString tempDir = QDir::tempPath();
	QString scriptPath = tempDir + "/obs_video_transcribe.py";
	QString outputPath = tempDir + "/obs_video_transcript.json";

	QFile scriptFile(scriptPath);
	if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		emit analysisError("Could not create transcription script");
		return;
	}

	QString pythonScript = R"PYTHON(
import sys
import os
import json

os.environ["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + os.environ.get("PATH", "")

try:
    import whisper
except ImportError:
    print("ERROR: whisper not installed", file=sys.stderr)
    sys.exit(1)

video_path = sys.argv[1]
output_path = sys.argv[2]
model_name = sys.argv[3] if len(sys.argv) > 3 else "base"
language = sys.argv[4] if len(sys.argv) > 4 else "en"

print(f"Loading model: {model_name}", file=sys.stderr)
model = whisper.load_model(model_name)

print(f"Transcribing...", file=sys.stderr)
lang_param = None if language == "auto" else language

# Use word_timestamps for accurate timing
# condition_on_previous_text=False prevents hallucination and improves timing accuracy
result = model.transcribe(
    video_path, 
    language=lang_param, 
    verbose=False,
    word_timestamps=True,
    condition_on_previous_text=False
)

segments = []
for seg in result.get("segments", []):
    # Use the actual segment timing from Whisper
    # These should reflect real audio timestamps, not normalized ones
    start_time = seg["start"]
    end_time = seg["end"]
    
    # If word-level timestamps are available and more accurate, use them
    words = seg.get("words", [])
    if words and len(words) > 0:
        # Word timestamps are more precise
        first_word = words[0]
        last_word = words[-1]
        if "start" in first_word and first_word["start"] > 0:
            start_time = first_word["start"]
        if "end" in last_word:
            end_time = last_word["end"]
    
    # Only add segment if it has actual content
    text = seg["text"].strip()
    if text:
        segments.append({
            "start": start_time,
            "end": end_time,
            "text": text
        })

with open(output_path, "w") as f:
    json.dump(segments, f)

print(f"Done: {len(segments)} segments", file=sys.stderr)
)PYTHON";

	QTextStream out(&scriptFile);
	out << pythonScript;
	scriptFile.close();

	/* Find Python */
	QStringList pythonPaths = {
		QDir::homePath() + "/.venv/main/bin/python",
		"/opt/homebrew/bin/python3",
		"/usr/local/bin/python3",
		"python3"
	};

	QString pythonPath;
	for (const QString &path : pythonPaths) {
		if (QFile::exists(path)) {
			pythonPath = path;
			break;
		}
	}

	if (pythonPath.isEmpty()) {
		QFile::remove(scriptPath);
		emit analysisError("Python not found");
		return;
	}

	QProcess process;
	process.start(pythonPath, {scriptPath, videoPath, outputPath, modelName, languageCode});

	int progress = 10;
	while (!process.waitForFinished(2000)) {
		if (progress < 95) {
			progress += 5;
			emit progressUpdated(progress, "Transcribing...");
		}
	}

	QFile::remove(scriptPath);

	if (process.exitCode() != 0) {
		emit analysisError("Transcription failed: " + process.readAllStandardError());
		return;
	}

	QFile outputFile(outputPath);
	if (!outputFile.open(QIODevice::ReadOnly)) {
		emit analysisError("Could not read transcription results");
		return;
	}

	QJsonDocument doc = QJsonDocument::fromJson(outputFile.readAll());
	outputFile.close();
	QFile::remove(outputPath);

	if (doc.isArray()) {
		emit transcriptionComplete(doc.array());
	}
}

/* ========================================================================== */
/* Video Analyzer Dialog                                                       */
/* ========================================================================== */

VideoAnalyzerDialog::VideoAnalyzerDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(obs_module_text("VideoAnalyzer.Title"));
	setMinimumSize(1100, 700);
	resize(1200, 800); /* Default to larger size */
	setupUI();
}

VideoAnalyzerDialog::~VideoAnalyzerDialog()
{
	if (mediaPlayer) {
		mediaPlayer->stop();
	}
	if (analysisThread) {
		analysisThread->quit();
		analysisThread->wait();
		delete analysisThread;
	}
}

void VideoAnalyzerDialog::setupUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	/* File Selection Row */
	QHBoxLayout *fileLayout = new QHBoxLayout();
	videoPathEdit = new QLineEdit(this);
	videoPathEdit->setPlaceholderText(obs_module_text("VideoAnalyzer.SelectVideo"));
	browseButton = new QPushButton("Browse...", this);
	connect(browseButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onBrowseClicked);
	fileLayout->addWidget(videoPathEdit);
	fileLayout->addWidget(browseButton);
	mainLayout->addLayout(fileLayout);

	/* Main Content Splitter */
	QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

	/* Left Side: Video Player */
	QWidget *videoContainer = new QWidget(this);
	QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
	videoLayout->setContentsMargins(0, 0, 0, 0);

	/* Video Widget */
	videoWidget = new QVideoWidget(this);
	videoWidget->setMinimumSize(480, 270);
	videoLayout->addWidget(videoWidget, 1);

	/* Timeline in scroll area */
	timelineScroll = new QScrollArea(this);
	timelineScroll->setWidgetResizable(false);
	timelineScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	timelineScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	timelineScroll->setFixedHeight(90);
	timelineScroll->setStyleSheet("QScrollArea { border: none; background: #1e1e1e; }");
	
	timeline = new VideoTimeline(this);
	connect(timeline, &VideoTimeline::positionClicked, this, &VideoAnalyzerDialog::onTimelineClicked);
	connect(timeline, &VideoTimeline::selectionChanged, this, &VideoAnalyzerDialog::onSelectionChanged);
	timelineScroll->setWidget(timeline);
	videoLayout->addWidget(timelineScroll);

	/* Clip Selection Actions */
	clipActionsWidget = new QWidget(this);
	QHBoxLayout *clipLayout = new QHBoxLayout(clipActionsWidget);
	clipLayout->setContentsMargins(0, 0, 0, 0);
	
	selectionLabel = new QLabel("Hold Shift + Click & drag to select a region", this);
	selectionLabel->setStyleSheet("color: #888; font-style: italic;");
	
	exportClipButton = new QPushButton("Export Clip", this);
	exportClipButton->setEnabled(false);
	exportClipButton->setToolTip("Export selected region as a new video file");
	exportClipButton->setStyleSheet("QPushButton { background: #2a5; color: white; padding: 4px 12px; } QPushButton:disabled { background: #444; color: #888; }");
	connect(exportClipButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onExportClipClicked);
	
	cutSectionButton = new QPushButton("✂ Cut", this);
	cutSectionButton->setEnabled(false);
	cutSectionButton->setToolTip("Mark selected region to be cut from final video");
	cutSectionButton->setStyleSheet("QPushButton { background: #a52; color: white; padding: 4px 12px; } QPushButton:disabled { background: #444; color: #888; }");
	connect(cutSectionButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onCutSectionClicked);
	
	clearSelectionButton = new QPushButton("Clear Selection", this);
	clearSelectionButton->setEnabled(false);
	connect(clearSelectionButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onClearSelectionClicked);
	
	undoCutButton = new QPushButton("Undo Last Cut", this);
	undoCutButton->setEnabled(false);
	undoCutButton->setToolTip("Remove the last cut region");
	connect(undoCutButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onUndoCutClicked);
	
	saveEditedButton = new QPushButton("💾 Save Edited Video", this);
	saveEditedButton->setEnabled(false);
	saveEditedButton->setToolTip("Export final video with all cuts applied");
	saveEditedButton->setStyleSheet("QPushButton { background: #26a; color: white; padding: 4px 12px; font-weight: bold; } QPushButton:disabled { background: #444; color: #888; }");
	connect(saveEditedButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onSaveEditedVideoClicked);
	
	clipLayout->addWidget(selectionLabel);
	clipLayout->addStretch();
	clipLayout->addWidget(exportClipButton);
	clipLayout->addWidget(cutSectionButton);
	clipLayout->addWidget(clearSelectionButton);
	clipLayout->addWidget(undoCutButton);
	clipLayout->addWidget(saveEditedButton);
	videoLayout->addWidget(clipActionsWidget);

	/* Time Label */
	timeLabel = new QLabel("0:00 / 0:00", this);
	timeLabel->setAlignment(Qt::AlignCenter);
	videoLayout->addWidget(timeLabel);

	/* Video Controls */
	QHBoxLayout *controlsLayout = new QHBoxLayout();
	playPauseButton = new QPushButton("▶", this);
	playPauseButton->setFixedWidth(40);
	stopButton = new QPushButton("■", this);
	stopButton->setFixedWidth(40);
	
	QLabel *volLabel = new QLabel("🔊", this);
	volumeSlider = new QSlider(Qt::Horizontal, this);
	volumeSlider->setRange(0, 100);
	volumeSlider->setValue(70);
	volumeSlider->setFixedWidth(100);

	connect(playPauseButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onPlayPauseClicked);
	connect(stopButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onStopClicked);

	controlsLayout->addWidget(playPauseButton);
	controlsLayout->addWidget(stopButton);
	controlsLayout->addStretch();
	controlsLayout->addWidget(volLabel);
	controlsLayout->addWidget(volumeSlider);
	videoLayout->addLayout(controlsLayout);

	splitter->addWidget(videoContainer);

	/* Right Side: Transcript & Controls */
	QWidget *rightPanel = new QWidget(this);
	QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);

	/* Transcription Options */
	QGroupBox *optionsGroup = new QGroupBox("Transcription", this);
	QHBoxLayout *optionsLayout = new QHBoxLayout(optionsGroup);

	optionsLayout->addWidget(new QLabel("Model:", this));
	modelCombo = new QComboBox(this);
	modelCombo->addItem("Tiny (fast)", "tiny");
	modelCombo->addItem("Base", "base");
	modelCombo->addItem("Small", "small");
	modelCombo->addItem("Medium (accurate)", "medium");
	modelCombo->setCurrentIndex(1);
	modelCombo->setFixedWidth(120);
	optionsLayout->addWidget(modelCombo);

	optionsLayout->addWidget(new QLabel("Language:", this));
	languageCombo = new QComboBox(this);
	languageCombo->addItem("Auto-detect", "auto");
	languageCombo->addItem("English", "en");
	languageCombo->addItem("Spanish", "es");
	languageCombo->addItem("French", "fr");
	languageCombo->addItem("German", "de");
	languageCombo->addItem("Japanese", "ja");
	languageCombo->addItem("Korean", "ko");
	languageCombo->addItem("Chinese", "zh");
	languageCombo->addItem("Portuguese", "pt");
	languageCombo->addItem("Italian", "it");
	languageCombo->setCurrentIndex(1);
	languageCombo->setFixedWidth(100);
	optionsLayout->addWidget(languageCombo);
	optionsLayout->addStretch();

	rightLayout->addWidget(optionsGroup);

	/* Progress */
	progressBar = new QProgressBar(this);
	progressBar->setVisible(false);
	rightLayout->addWidget(progressBar);

	statusLabel = new QLabel("", this);
	statusLabel->setStyleSheet("color: #888;");
	rightLayout->addWidget(statusLabel);

	/* Transcript View with Export in header */
	QWidget *transcriptContainer = new QWidget(this);
	QVBoxLayout *transcriptContainerLayout = new QVBoxLayout(transcriptContainer);
	transcriptContainerLayout->setContentsMargins(0, 0, 0, 0);
	transcriptContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	/* Header row with label and export */
	QHBoxLayout *transcriptHeader = new QHBoxLayout();
	QLabel *transcriptLabel = new QLabel(obs_module_text("VideoAnalyzer.Transcript"), this);
	transcriptLabel->setStyleSheet("font-weight: bold;");
	transcriptHeader->addWidget(transcriptLabel);
	transcriptHeader->addStretch();
	
	exportFormatCombo = new QComboBox(this);
	exportFormatCombo->addItem("JSON", "json");
	exportFormatCombo->addItem("CSV", "csv");
	exportFormatCombo->addItem("YouTube Chapters", "youtube");
	exportFormatCombo->setFixedWidth(130);
	
	exportButton = new QPushButton("Export", this);
	exportButton->setEnabled(false);
	exportButton->setFixedWidth(70);
	connect(exportButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onExportClicked);
	
	transcriptHeader->addWidget(exportFormatCombo);
	transcriptHeader->addWidget(exportButton);
	transcriptContainerLayout->addLayout(transcriptHeader);

	transcriptView = new QTextEdit(this);
	transcriptView->setReadOnly(true);
	transcriptView->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	transcriptView->setPlaceholderText("Click Analyze to transcribe.\nClick any line to jump to that point.\nPress Backspace on a line to cut that section.\nCmd+Z to undo cuts.");
	transcriptView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	transcriptView->setMinimumHeight(200);
	transcriptView->setFocusPolicy(Qt::StrongFocus);
	transcriptView->setStyleSheet(R"(
		QTextEdit {
			font-size: 14px;
			line-height: 1.6;
			padding: 8px;
		}
	)");
	connect(transcriptView, &QTextEdit::cursorPositionChanged, this, &VideoAnalyzerDialog::onTranscriptClicked);
	transcriptContainerLayout->addWidget(transcriptView, 1);
	
	rightLayout->addWidget(transcriptContainer, 1);

	/* Analyze Button at bottom */
	analyzeButton = new QPushButton(obs_module_text("VideoAnalyzer.Analyze"), this);
	analyzeButton->setStyleSheet("QPushButton { padding: 10px 20px; font-weight: bold; font-size: 14px; }");
	connect(analyzeButton, &QPushButton::clicked, this, &VideoAnalyzerDialog::onAnalyzeClicked);
	rightLayout->addWidget(analyzeButton);

	splitter->addWidget(rightPanel);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({500, 500}); /* Equal initial sizes */

	mainLayout->addWidget(splitter, 1);

	/* Setup Media Player */
	mediaPlayer = new QMediaPlayer(this);
	QAudioOutput *audioOutput = new QAudioOutput(this);
	mediaPlayer->setAudioOutput(audioOutput);
	mediaPlayer->setVideoOutput(videoWidget);

	connect(mediaPlayer, &QMediaPlayer::positionChanged, this, &VideoAnalyzerDialog::onPositionChanged);
	connect(mediaPlayer, &QMediaPlayer::durationChanged, this, &VideoAnalyzerDialog::onDurationChanged);
	connect(mediaPlayer, &QMediaPlayer::playbackStateChanged, [this]() {
		updatePlayPauseButton();
	});
	connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged, [this](QMediaPlayer::MediaStatus status) {
		if (status == QMediaPlayer::LoadedMedia) {
			/* Show first frame when video is loaded */
			mediaPlayer->setPosition(1);
			mediaPlayer->pause();
			statusLabel->setText("Video loaded successfully!");
		} else if (status == QMediaPlayer::InvalidMedia) {
			statusLabel->setText("Error: Invalid or unsupported video format");
		} else if (status == QMediaPlayer::NoMedia) {
			statusLabel->setText("No video loaded");
		}
	});
	connect(mediaPlayer, &QMediaPlayer::errorOccurred, [this](QMediaPlayer::Error error, const QString &errorString) {
		Q_UNUSED(error);
		statusLabel->setText("Error: " + errorString);
		QMessageBox::warning(this, "Video Error", errorString);
	});

	connect(volumeSlider, &QSlider::valueChanged, [audioOutput](int value) {
		audioOutput->setVolume(value / 100.0f);
	});
	audioOutput->setVolume(0.7f);
}

void VideoAnalyzerDialog::keyPressEvent(QKeyEvent *event)
{
	/* Cmd+Z (Mac) or Ctrl+Z (Windows/Linux) to undo last cut */
	if (event->matches(QKeySequence::Undo)) {
		if (!timeline->getCutRegions().empty()) {
			onUndoCutClicked();
		}
		return;
	}
	
	/* Backspace or Delete to cut selected transcript segment */
	if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
		if (transcriptView->hasFocus()) {
			cutSelectedTranscriptSegment();
			return;
		}
	}
	
	QDialog::keyPressEvent(event);
}

int VideoAnalyzerDialog::getTranscriptSegmentAtCursor()
{
	QTextCursor cursor = transcriptView->textCursor();
	int block = cursor.blockNumber();
	
	if (block >= 0 && block < (int)transcriptSegments.size()) {
		return block;
	}
	return -1;
}

std::pair<int, int> VideoAnalyzerDialog::getSelectedTranscriptRange()
{
	QTextCursor cursor = transcriptView->textCursor();
	
	if (!cursor.hasSelection()) {
		/* No selection, use current block */
		int block = cursor.blockNumber();
		if (block >= 0 && block < (int)transcriptSegments.size()) {
			return {block, block};
		}
		return {-1, -1};
	}
	
	/* Get the block numbers for selection start and end */
	int selStart = cursor.selectionStart();
	int selEnd = cursor.selectionEnd();
	
	/* Find block numbers by position */
	QTextCursor startCursor = transcriptView->textCursor();
	startCursor.setPosition(selStart);
	int startBlock = startCursor.blockNumber();
	
	QTextCursor endCursor = transcriptView->textCursor();
	endCursor.setPosition(selEnd);
	int endBlock = endCursor.blockNumber();
	
	/* Ensure valid range */
	if (startBlock < 0) startBlock = 0;
	if (endBlock >= (int)transcriptSegments.size()) endBlock = transcriptSegments.size() - 1;
	if (startBlock > endBlock) std::swap(startBlock, endBlock);
	
	return {startBlock, endBlock};
}

void VideoAnalyzerDialog::cutSelectedTranscriptSegment()
{
	auto [firstIdx, lastIdx] = getSelectedTranscriptRange();
	if (firstIdx < 0 || lastIdx < 0 || firstIdx >= (int)transcriptSegments.size())
		return;
	
	/* Get time range from FIRST segment's START to LAST segment's END */
	const TranscriptSegment &firstSeg = transcriptSegments[firstIdx];
	const TranscriptSegment &lastSeg = transcriptSegments[lastIdx];
	
	qint64 startMs = (qint64)(firstSeg.start * 1000);
	qint64 endMs = (qint64)(lastSeg.end * 1000);
	
	/* Add cut region spanning all selected segments */
	timeline->addCutRegion(startMs, endMs);
	
	/* Update UI */
	bool hasCuts = !timeline->getCutRegions().empty();
	undoCutButton->setEnabled(hasCuts);
	saveEditedButton->setEnabled(hasCuts);
	
	selectionLabel->setText(QString("%1 cut region(s) - Hold Shift + Click & drag to select more").arg(timeline->getCutRegions().size()));
	selectionLabel->setStyleSheet("color: #f88; font-style: italic;");
	
	/* Refresh transcript to show strikethrough */
	populateTranscript();
	
	int numSegments = lastIdx - firstIdx + 1;
	statusLabel->setText(QString("Cut %1 transcript segment(s)").arg(numSegments));
}

void VideoAnalyzerDialog::onBrowseClicked()
{
	QString filter = "Video Files (*.mp4 *.mkv *.flv *.mov *.ts *.avi *.webm);;All Files (*.*)";
	QString path = QFileDialog::getOpenFileName(this, "Select Video", QString(), filter);
	
	if (!path.isEmpty()) {
		videoPathEdit->setText(path);
		
		/* Clear previous data */
		timeline->clearWaveform();
		transcriptSegments.clear();
		transcriptView->clear();
		exportButton->setEnabled(false);
		
		/* Load video into player */
		mediaPlayer->setSource(QUrl::fromLocalFile(path));
		
		statusLabel->setText("Loading video...");
		
		/* Load waveform in background */
		loadWaveform(path);
	}
}

void VideoAnalyzerDialog::onPlayPauseClicked()
{
	if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
		mediaPlayer->pause();
	} else {
		mediaPlayer->play();
	}
}

void VideoAnalyzerDialog::onStopClicked()
{
	mediaPlayer->stop();
}

void VideoAnalyzerDialog::updatePlayPauseButton()
{
	if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
		playPauseButton->setText("⏸");
	} else {
		playPauseButton->setText("▶");
	}
}

void VideoAnalyzerDialog::onPositionChanged(qint64 position)
{
	timeline->setPosition(position);
	timeLabel->setText(formatTime(position) + " / " + formatTime(mediaPlayer->duration()));
	
	/* Skip cut regions during playback */
	if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
		const auto &cutRegions = timeline->getCutRegions();
		for (const auto &region : cutRegions) {
			if (position >= region.first && position < region.second) {
				/* We're in a cut region, skip to end */
				mediaPlayer->setPosition(region.second);
				return;
			}
		}
	}
	
	/* Auto-scroll to keep playhead visible */
	int playheadX = timeline->getPlayheadX();
	int scrollPos = timelineScroll->horizontalScrollBar()->value();
	int viewWidth = timelineScroll->viewport()->width();
	
	/* If playhead is outside visible area, scroll to center it */
	if (playheadX < scrollPos + 50 || playheadX > scrollPos + viewWidth - 50) {
		int newScrollPos = playheadX - viewWidth / 2;
		timelineScroll->horizontalScrollBar()->setValue(newScrollPos);
	}
}

void VideoAnalyzerDialog::onDurationChanged(qint64 duration)
{
	timeline->setDuration(duration);
}

void VideoAnalyzerDialog::onTimelineClicked(qint64 position)
{
	mediaPlayer->setPosition(position);
}

QString VideoAnalyzerDialog::formatTime(qint64 ms)
{
	int secs = ms / 1000;
	int mins = secs / 60;
	secs = secs % 60;
	int hours = mins / 60;
	mins = mins % 60;

	if (hours > 0) {
		return QString("%1:%2:%3")
			.arg(hours)
			.arg(mins, 2, 10, QChar('0'))
			.arg(secs, 2, 10, QChar('0'));
	}
	return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

void VideoAnalyzerDialog::onTranscriptClicked()
{
	/* Get clicked position and find corresponding timestamp */
	QTextCursor cursor = transcriptView->textCursor();
	int block = cursor.blockNumber();
	
	if (block >= 0 && block < (int)transcriptSegments.size()) {
		double timestamp = transcriptSegments[block].start;
		mediaPlayer->setPosition((qint64)(timestamp * 1000));
	}
}

void VideoAnalyzerDialog::onAnalyzeClicked()
{
	QString videoPath = videoPathEdit->text();
	if (videoPath.isEmpty()) {
		QMessageBox::warning(this, "Warning", "Please select a video file first.");
		return;
	}

	/* Clean up previous thread */
	if (analysisThread) {
		analysisThread->quit();
		analysisThread->wait();
		delete analysisThread;
		analysisThread = nullptr;
	}

	/* Setup worker */
	analysisThread = new QThread();
	analysisWorker = new VideoAnalysisWorker();
	analysisWorker->setVideoPath(videoPath);
	analysisWorker->setTranscriptionModel(modelCombo->currentData().toString());
	analysisWorker->setLanguage(languageCombo->currentData().toString());
	analysisWorker->moveToThread(analysisThread);

	connect(analysisThread, &QThread::started, analysisWorker, &VideoAnalysisWorker::process);
	connect(analysisWorker, &VideoAnalysisWorker::progressUpdated, this, &VideoAnalyzerDialog::onAnalysisProgress);
	connect(analysisWorker, &VideoAnalysisWorker::transcriptionComplete, this, &VideoAnalyzerDialog::onTranscriptionComplete);
	connect(analysisWorker, &VideoAnalysisWorker::analysisError, this, &VideoAnalyzerDialog::onAnalysisError);
	connect(analysisWorker, &VideoAnalysisWorker::finished, [this]() {
		analyzeButton->setEnabled(true);
		analyzeButton->setText(obs_module_text("VideoAnalyzer.Analyze"));
		progressBar->setVisible(false);
		statusLabel->setText("Analysis complete!");
		
		bool hasData = !transcriptSegments.empty();
		exportButton->setEnabled(hasData);
	});

	/* Update UI */
	analyzeButton->setEnabled(false);
	analyzeButton->setText("Analyzing...");
	progressBar->setVisible(true);
	progressBar->setValue(0);

	analysisThread->start();
}

void VideoAnalyzerDialog::onAnalysisProgress(int percent, const QString &status)
{
	progressBar->setValue(percent);
	statusLabel->setText(status);
}

void VideoAnalyzerDialog::onTranscriptionComplete(const QJsonArray &segments)
{
	transcriptSegments.clear();
	
	for (int i = 0; i < segments.size(); i++) {
		QJsonObject obj = segments[i].toObject();
		TranscriptSegment seg;
		seg.start = obj["start"].toDouble();
		seg.end = obj["end"].toDouble();
		seg.text = obj["text"].toString();
		transcriptSegments.push_back(seg);
	}

	populateTranscript();
}

void VideoAnalyzerDialog::populateTranscript()
{
	QString html;
	const auto &cutRegions = timeline->getCutRegions();
	
	for (const auto &seg : transcriptSegments) {
		int mins = (int)(seg.start / 60);
		int secs = (int)seg.start % 60;
		QString timestamp = QString("[%1:%2]").arg(mins).arg(secs, 2, 10, QChar('0'));
		
		/* Check if this segment overlaps with any cut region */
		qint64 segStartMs = (qint64)(seg.start * 1000);
		qint64 segEndMs = (qint64)(seg.end * 1000);
		bool isCut = false;
		
		for (const auto &cut : cutRegions) {
			/* Check for overlap: segment overlaps cut if segStart < cutEnd AND segEnd > cutStart */
			if (segStartMs < cut.second && segEndMs > cut.first) {
				isCut = true;
				break;
			}
		}
		
		if (isCut) {
			/* Strikethrough and italic for cut segments */
			html += QString("<p style='margin: 4px 0; cursor: pointer; text-decoration: line-through; font-style: italic; opacity: 0.5;'>"
			                "<span style='color: #888; font-weight: bold;'>%1</span> %2</p>")
			        .arg(timestamp, seg.text.toHtmlEscaped());
		} else {
			html += QString("<p style='margin: 4px 0; cursor: pointer;'>"
			                "<span style='color: #0af; font-weight: bold;'>%1</span> %2</p>")
			        .arg(timestamp, seg.text.toHtmlEscaped());
		}
	}
	transcriptView->setHtml(html);
}

void VideoAnalyzerDialog::onAnalysisError(const QString &error)
{
	QMessageBox::critical(this, "Error", error);
	analyzeButton->setEnabled(true);
	analyzeButton->setText(obs_module_text("VideoAnalyzer.Analyze"));
	progressBar->setVisible(false);
}

void VideoAnalyzerDialog::onExportClicked()
{
	QString format = exportFormatCombo->currentData().toString();
	if (format == "json") {
		onExportJSON();
	} else if (format == "csv") {
		onExportCSV();
	} else if (format == "youtube") {
		onExportYouTube();
	}
}

void VideoAnalyzerDialog::onSelectionChanged(qint64 start, qint64 end)
{
	bool hasSelection = start >= 0 && end > start;
	bool hasCuts = !timeline->getCutRegions().empty();
	
	exportClipButton->setEnabled(hasSelection);
	cutSectionButton->setEnabled(hasSelection);
	clearSelectionButton->setEnabled(hasSelection);
	undoCutButton->setEnabled(hasCuts);
	saveEditedButton->setEnabled(hasCuts);
	
	if (hasSelection) {
		QString startStr = formatTime(start);
		QString endStr = formatTime(end);
		qint64 duration = end - start;
		QString durStr = formatTime(duration);
		selectionLabel->setText(QString("Selected: %1 - %2 (Duration: %3)").arg(startStr, endStr, durStr));
		selectionLabel->setStyleSheet("color: #6af; font-weight: bold;");
	} else if (hasCuts) {
		selectionLabel->setText(QString("%1 cut region(s) - Hold Shift + Click & drag to select more").arg(timeline->getCutRegions().size()));
		selectionLabel->setStyleSheet("color: #f88; font-style: italic;");
	} else {
		selectionLabel->setText("Hold Shift + Click & drag to select a region");
		selectionLabel->setStyleSheet("color: #888; font-style: italic;");
	}
}

void VideoAnalyzerDialog::onExportClipClicked()
{
	if (!timeline->hasSelection())
		return;

	QString videoPath = videoPathEdit->text();
	if (videoPath.isEmpty())
		return;

	qint64 startMs = timeline->getSelectionStart();
	qint64 endMs = timeline->getSelectionEnd();
	
	/* Get output filename */
	QFileInfo fi(videoPath);
	QString defaultName = fi.baseName() + "_clip." + fi.suffix();
	QString savePath = QFileDialog::getSaveFileName(this, "Export Clip", 
		fi.absolutePath() + "/" + defaultName, 
		"Video Files (*.mp4 *.mkv *.mov);;All Files (*.*)");
	
	if (savePath.isEmpty())
		return;

	/* Convert milliseconds to seconds with decimals */
	double startSec = startMs / 1000.0;
	double durationSec = (endMs - startMs) / 1000.0;

	statusLabel->setText("Exporting clip...");
	
	/* Use FFmpeg to extract the clip */
	QProcess *process = new QProcess(this);
	QStringList args;
	args << "-y"  /* Overwrite output */
	     << "-ss" << QString::number(startSec, 'f', 3)
	     << "-i" << videoPath
	     << "-t" << QString::number(durationSec, 'f', 3)
	     << "-c" << "copy"  /* Copy without re-encoding for speed */
	     << savePath;

	connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		[this, process, savePath](int exitCode, QProcess::ExitStatus) {
			if (exitCode == 0) {
				statusLabel->setText("Clip exported successfully!");
				QMessageBox::information(this, "Export Complete", 
					"Clip exported to:\n" + savePath);
			} else {
				statusLabel->setText("Export failed");
				QMessageBox::warning(this, "Export Failed", 
					"Failed to export clip:\n" + process->readAllStandardError());
			}
			process->deleteLater();
		});

	process->start("/opt/homebrew/bin/ffmpeg", args);
	if (!process->waitForStarted()) {
		/* Try without path */
		process->start("ffmpeg", args);
	}
}

void VideoAnalyzerDialog::onCutSectionClicked()
{
	if (!timeline->hasSelection())
		return;

	qint64 startMs = timeline->getSelectionStart();
	qint64 endMs = timeline->getSelectionEnd();
	
	/* Add to cut regions */
	timeline->addCutRegion(startMs, endMs);
	timeline->clearSelection();
	
	/* Update UI */
	bool hasCuts = !timeline->getCutRegions().empty();
	undoCutButton->setEnabled(hasCuts);
	saveEditedButton->setEnabled(hasCuts);
	
	selectionLabel->setText(QString("%1 cut region(s) - Shift+Click to select more").arg(timeline->getCutRegions().size()));
	selectionLabel->setStyleSheet("color: #f88; font-style: italic;");
	
	statusLabel->setText(QString("Marked %1 - %2 for cutting. Video will skip this section during playback.")
		.arg(formatTime(startMs), formatTime(endMs)));
}

void VideoAnalyzerDialog::onUndoCutClicked()
{
	const auto &regions = timeline->getCutRegions();
	if (regions.empty())
		return;
	
	/* Remove last cut region */
	timeline->removeCutRegion(regions.size() - 1);
	
	bool hasCuts = !timeline->getCutRegions().empty();
	undoCutButton->setEnabled(hasCuts);
	saveEditedButton->setEnabled(hasCuts);
	
	if (hasCuts) {
		selectionLabel->setText(QString("%1 cut region(s) - Hold Shift + Click & drag to select more").arg(timeline->getCutRegions().size()));
		selectionLabel->setStyleSheet("color: #f88; font-style: italic;");
	} else {
		selectionLabel->setText("Hold Shift + Click & drag to select a region");
		selectionLabel->setStyleSheet("color: #888; font-style: italic;");
	}
	
	/* Refresh transcript to remove strikethrough from restored segments */
	populateTranscript();
	
	statusLabel->setText("Last cut region removed.");
}

void VideoAnalyzerDialog::onSaveEditedVideoClicked()
{
	const auto &cutRegions = timeline->getCutRegions();
	if (cutRegions.empty())
		return;

	QString videoPath = videoPathEdit->text();
	if (videoPath.isEmpty())
		return;

	/* Get output filename */
	QFileInfo fi(videoPath);
	QString defaultName = fi.baseName() + "_edited." + fi.suffix();
	QString savePath = QFileDialog::getSaveFileName(this, "Save Edited Video",
		fi.absolutePath() + "/" + defaultName,
		"Video Files (*.mp4 *.mkv *.mov);;All Files (*.*)");
	
	if (savePath.isEmpty())
		return;

	double totalDuration = mediaPlayer->duration() / 1000.0;

	statusLabel->setText("Processing video (this may take a while)...");

	/* Build segments to keep (inverse of cut regions) */
	QString segmentsJson = "[";
	double lastEnd = 0;
	for (const auto &region : cutRegions) {
		double cutStart = region.first / 1000.0;
		double cutEnd = region.second / 1000.0;
		if (cutStart > lastEnd) {
			if (segmentsJson.length() > 1) segmentsJson += ",";
			segmentsJson += QString("[%1,%2]").arg(lastEnd, 0, 'f', 3).arg(cutStart, 0, 'f', 3);
		}
		lastEnd = cutEnd;
	}
	if (lastEnd < totalDuration) {
		if (segmentsJson.length() > 1) segmentsJson += ",";
		segmentsJson += QString("[%1,%2]").arg(lastEnd, 0, 'f', 3).arg(totalDuration, 0, 'f', 3);
	}
	segmentsJson += "]";

	/* Create Python script for multi-segment export */
	QString tempDir = QDir::tempPath();
	QString scriptPath = tempDir + "/obs_video_multicuts.py";
	
	QFile scriptFile(scriptPath);
	if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QMessageBox::warning(this, "Error", "Could not create processing script");
		return;
	}

	QString script = R"PYTHON(
import subprocess
import sys
import os
import json
import tempfile

os.environ["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + os.environ.get("PATH", "")

input_file = sys.argv[1]
output_file = sys.argv[2]
segments = json.loads(sys.argv[3])

print(f"Processing {len(segments)} segments...", file=sys.stderr)

if len(segments) == 0:
    print("No segments to keep!", file=sys.stderr)
    sys.exit(1)

if len(segments) == 1:
    # Simple case: just trim
    start, end = segments[0]
    cmd = ["ffmpeg", "-y", "-ss", str(start), "-i", input_file, "-t", str(end - start), "-c", "copy", output_file]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(1)
else:
    # Multiple segments - need to use filter_complex
    filter_parts = []
    concat_inputs = ""
    
    for i, (start, end) in enumerate(segments):
        filter_parts.append(f"[0:v]trim={start}:{end},setpts=PTS-STARTPTS[v{i}]")
        filter_parts.append(f"[0:a]atrim={start}:{end},asetpts=PTS-STARTPTS[a{i}]")
        concat_inputs += f"[v{i}][a{i}]"
    
    filter_complex = ";".join(filter_parts) + f";{concat_inputs}concat=n={len(segments)}:v=1:a=1[v][a]"
    
    cmd = [
        "ffmpeg", "-y", "-i", input_file,
        "-filter_complex", filter_complex,
        "-map", "[v]", "-map", "[a]",
        output_file
    ]
    
    print(f"Running FFmpeg with {len(segments)} segments...", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(1)

print("Done!", file=sys.stderr)
)PYTHON";

	QTextStream out(&scriptFile);
	out << script;
	scriptFile.close();

	QProcess *process = new QProcess(this);
	QStringList args;
	args << scriptPath << videoPath << savePath << segmentsJson;

	connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		[this, process, savePath, scriptPath](int exitCode, QProcess::ExitStatus) {
			QFile::remove(scriptPath);
			if (exitCode == 0) {
				statusLabel->setText("Video saved successfully!");
				QMessageBox::information(this, "Save Complete",
					"Edited video saved to:\n" + savePath);
			} else {
				statusLabel->setText("Save failed");
				QMessageBox::warning(this, "Save Failed",
					"Failed to save edited video:\n" + process->readAllStandardError());
			}
			process->deleteLater();
		});

	QString pythonPath = QDir::homePath() + "/.venv/main/bin/python";
	if (!QFile::exists(pythonPath))
		pythonPath = "/opt/homebrew/bin/python3";
	
	process->start(pythonPath, args);
}

void VideoAnalyzerDialog::onClearSelectionClicked()
{
	timeline->clearSelection();
}

void VideoAnalyzerDialog::onExportJSON()
{
	QString path = QFileDialog::getSaveFileName(this, "Export JSON", QString(), "JSON Files (*.json)");
	if (path.isEmpty()) return;

	QJsonObject root;
	
	QJsonArray transcriptArr;
	for (const auto &seg : transcriptSegments) {
		QJsonObject obj;
		obj["start"] = seg.start;
		obj["end"] = seg.end;
		obj["text"] = seg.text;
		transcriptArr.append(obj);
	}
	root["transcript"] = transcriptArr;

	QJsonDocument doc(root);
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(doc.toJson(QJsonDocument::Indented));
		file.close();
		QMessageBox::information(this, "Export", "Exported to " + path);
	}
}

void VideoAnalyzerDialog::onExportCSV()
{
	QString path = QFileDialog::getSaveFileName(this, "Export CSV", QString(), "CSV Files (*.csv)");
	if (path.isEmpty()) return;

	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QTextStream out(&file);
		out << "Start,End,Text\n";
		
		for (const auto &seg : transcriptSegments) {
			QString escapedText = seg.text;
			escapedText.replace("\"", "\"\"");
			out << seg.start << "," << seg.end << ","
			    << "\"" << escapedText << "\"\n";
		}
		
		file.close();
		QMessageBox::information(this, "Export", "Exported to " + path);
	}
}

void VideoAnalyzerDialog::onExportYouTube()
{
	QString path = QFileDialog::getSaveFileName(this, "Export YouTube Chapters", QString(), "Text Files (*.txt)");
	if (path.isEmpty()) return;

	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QTextStream out(&file);
		out << "YouTube Chapters\n";
		out << "================\n\n";
		
		/* Create chapters from transcript segments */
		for (const auto &seg : transcriptSegments) {
			int mins = (int)(seg.start / 60);
			int secs = (int)seg.start % 60;
			/* Truncate text for chapter title */
			QString title = seg.text.left(50);
			if (seg.text.length() > 50) title += "...";
			out << QString("%1:%2 %3\n").arg(mins).arg(secs, 2, 10, QChar('0')).arg(title);
		}
		
		file.close();
		QMessageBox::information(this, "Export", "Exported to " + path);
	}
}

void VideoAnalyzerDialog::loadWaveform(const QString &videoPath)
{
	/* Extract waveform data using FFmpeg in a separate thread */
	QString tempDir = QDir::tempPath();
	QString scriptPath = tempDir + "/obs_waveform.py";
	QString outputPath = tempDir + "/obs_waveform.json";

	QFile scriptFile(scriptPath);
	if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		statusLabel->setText("Could not create waveform script");
		return;
	}

	QString pythonScript = R"PYTHON(
import sys
import os
import json
import subprocess
import struct
import tempfile

os.environ["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + os.environ.get("PATH", "")

video_path = sys.argv[1]
output_path = sys.argv[2]

# Calculate number of samples based on duration
# Target: ~4 samples per second for good detail at 8 pixels/sec zoom
# This will be calculated after getting duration

# Get duration
try:
    cmd = ["ffprobe", "-v", "error", "-show_entries", "format=duration",
           "-of", "default=noprint_wrappers=1:nokey=1", video_path]
    duration = float(subprocess.check_output(cmd, stderr=subprocess.DEVNULL).decode().strip())
except:
    duration = 0

# Calculate samples: ~4 samples per second, minimum 100, maximum 4000
num_samples = max(100, min(4000, int(duration * 4)))

if duration == 0:
    with open(output_path, "w") as f:
        json.dump([0.1] * 100, f)
    sys.exit(0)

# Extract raw audio samples using FFmpeg
# Output as signed 16-bit PCM, mono, 8kHz
temp_pcm = tempfile.NamedTemporaryFile(suffix='.raw', delete=False)
temp_pcm.close()

cmd = [
    "ffmpeg", "-y", "-i", video_path,
    "-ac", "1",           # Mono
    "-ar", "8000",        # 8kHz sample rate
    "-f", "s16le",        # Signed 16-bit little-endian
    "-acodec", "pcm_s16le",
    temp_pcm.name
]

try:
    subprocess.run(cmd, capture_output=True, timeout=120)
except Exception as e:
    with open(output_path, "w") as f:
        json.dump([0.1] * num_samples, f)
    sys.exit(0)

# Read PCM data and calculate peak levels per segment
try:
    with open(temp_pcm.name, 'rb') as f:
        pcm_data = f.read()
    os.unlink(temp_pcm.name)
except:
    with open(output_path, "w") as f:
        json.dump([0.1] * num_samples, f)
    sys.exit(0)

# Parse samples (16-bit signed integers)
num_raw_samples = len(pcm_data) // 2
if num_raw_samples == 0:
    with open(output_path, "w") as f:
        json.dump([0.1] * num_samples, f)
    sys.exit(0)

samples = struct.unpack(f'<{num_raw_samples}h', pcm_data)

# Calculate peak amplitude for each segment
samples_per_segment = max(1, num_raw_samples // num_samples)
waveform = []

for i in range(num_samples):
    start_idx = i * samples_per_segment
    end_idx = min(start_idx + samples_per_segment, num_raw_samples)
    
    if start_idx >= num_raw_samples:
        waveform.append(0.0)
        continue
    
    segment = samples[start_idx:end_idx]
    
    # Get peak (maximum absolute value)
    peak = max(abs(min(segment)), abs(max(segment))) if segment else 0
    
    # Normalize to 0-1 range (16-bit max is 32767)
    amplitude = peak / 32767.0
    
    # Apply some curve to make quieter parts more visible
    amplitude = amplitude ** 0.6  # Compress dynamic range slightly
    
    waveform.append(amplitude)

# Normalize to use full range
if waveform:
    max_amp = max(waveform) if max(waveform) > 0 else 1
    waveform = [min(1.0, a / max_amp) for a in waveform]

with open(output_path, "w") as f:
    json.dump(waveform, f)
)PYTHON";

	QTextStream out(&scriptFile);
	out << pythonScript;
	scriptFile.close();

	/* Run in background thread */
	QThread *waveformThread = new QThread();
	QProcess *process = new QProcess();
	
	connect(waveformThread, &QThread::started, [=]() {
		QStringList pythonPaths = {
			QDir::homePath() + "/.venv/main/bin/python",
			"/opt/homebrew/bin/python3",
			"/usr/local/bin/python3",
			"python3"
		};

		QString pythonPath;
		for (const QString &path : pythonPaths) {
			if (QFile::exists(path)) {
				pythonPath = path;
				break;
			}
		}

		if (!pythonPath.isEmpty()) {
			process->start(pythonPath, {scriptPath, videoPath, outputPath});
			process->waitForFinished(60000);
		}
	});

	connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		[=](int exitCode, QProcess::ExitStatus) {
			QFile::remove(scriptPath);
			
			if (exitCode == 0) {
				QFile outputFile(outputPath);
				if (outputFile.open(QIODevice::ReadOnly)) {
					QJsonDocument doc = QJsonDocument::fromJson(outputFile.readAll());
					outputFile.close();
					QFile::remove(outputPath);
					
					if (doc.isArray()) {
						std::vector<float> waveformData;
						QJsonArray arr = doc.array();
						for (int i = 0; i < arr.size(); i++) {
							waveformData.push_back((float)arr[i].toDouble());
						}
						
						/* Update UI on main thread */
						QMetaObject::invokeMethod(timeline, [this, waveformData]() {
							timeline->setWaveform(waveformData);
							statusLabel->setText("Video loaded. Click Analyze to process.");
						}, Qt::QueuedConnection);
					}
				}
			}
			
			waveformThread->quit();
		});

	connect(waveformThread, &QThread::finished, [=]() {
		process->deleteLater();
		waveformThread->deleteLater();
	});

	waveformThread->start();
}

/* ========================================================================== */
/* Module Integration                                                          */
/* ========================================================================== */

static VideoAnalyzerDialog *videoAnalyzerDialog = nullptr;

static void ShowVideoAnalyzer(void)
{
	obs_frontend_push_ui_translation(obs_module_get_string);

	if (!videoAnalyzerDialog) {
		QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
		videoAnalyzerDialog = new VideoAnalyzerDialog(mainWindow);
	}

	videoAnalyzerDialog->show();
	videoAnalyzerDialog->raise();
	videoAnalyzerDialog->activateWindow();

	obs_frontend_pop_ui_translation();
}

extern "C" void InitVideoAnalyzer(void)
{
	QAction *action = (QAction *)obs_frontend_add_tools_menu_qaction(
		obs_module_text("VideoAnalyzer"));

	QObject::connect(action, &QAction::triggered, ShowVideoAnalyzer);
}

extern "C" void FreeVideoAnalyzer(void)
{
	if (videoAnalyzerDialog) {
		delete videoAnalyzerDialog;
		videoAnalyzerDialog = nullptr;
	}
}

#include "moc_video-analyzer-dialog.cpp"

