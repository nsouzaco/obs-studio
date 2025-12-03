/*
 * Video Analyzer Dialog
 * Provides video transcription with editing capabilities
 */

#pragma once

#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QProgressBar>
#include <QThread>
#include <QCheckBox>
#include <QJsonArray>
#include <QScrollArea>
#include <QKeyEvent>
#include <vector>
#include <string>
#include <utility>

/* Forward declaration */
class FFmpegVideoPlayer;

/* Timeline widget with waveform and selection */
class VideoTimeline : public QWidget {
	Q_OBJECT

public:
	VideoTimeline(QWidget *parent = nullptr);
	
	void setDuration(qint64 duration);
	void setPosition(qint64 position);
	void setWaveform(const std::vector<float> &waveformData);
	void clearWaveform();
	void clearSelection();
	int getPlayheadX() const;
	
	bool hasSelection() const { return selectionStart >= 0 && selectionEnd > selectionStart; }
	qint64 getSelectionStart() const { return selectionStart; }
	qint64 getSelectionEnd() const { return selectionEnd; }
	
	/* Cut regions (non-destructive editing) */
	void addCutRegion(qint64 start, qint64 end);
	void removeCutRegion(int index);
	void clearCutRegions();
	const std::vector<std::pair<qint64, qint64>>& getCutRegions() const { return cutRegions; }
	bool isPositionCut(qint64 position) const;
	
	/* Pixels per second for zoom level */
	static constexpr int PIXELS_PER_SECOND = 8;

signals:
	void positionClicked(qint64 position);
	void selectionChanged(qint64 start, qint64 end);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

private:
	enum DragMode { None, DragLeftHandle, DragRightHandle, DragSelection, Creating };
	
	void drawTimeMarkers(QPainter &painter);
	void drawWaveform(QPainter &painter);
	void drawSelection(QPainter &painter);
	void drawCutRegions(QPainter &painter);
	void updateSize();
	int positionToX(qint64 pos) const;
	qint64 xToPosition(int x) const;
	
	qint64 totalDuration = 0;
	qint64 currentPosition = 0;
	std::vector<float> waveform;
	int calculatedWidth = 400;
	
	/* Selection */
	qint64 selectionStart = -1;
	qint64 selectionEnd = -1;
	DragMode dragMode = None;
	int dragStartX = 0;
	qint64 dragStartPos = 0;
	
	/* Cut regions for non-destructive editing */
	std::vector<std::pair<qint64, qint64>> cutRegions;
};

/* Transcription segment for display */
struct TranscriptSegment {
	double start;
	double end;
	QString text;
};

/* Worker thread for analysis */
class VideoAnalysisWorker : public QObject {
	Q_OBJECT

public:
	VideoAnalysisWorker(QObject *parent = nullptr);
	~VideoAnalysisWorker();

	void setVideoPath(const QString &path);
	void setTranscriptionModel(const QString &model);
	void setLanguage(const QString &lang);

public slots:
	void process();

signals:
	void progressUpdated(int percent, const QString &status);
	void transcriptionComplete(const QJsonArray &segments);
	void analysisError(const QString &error);
	void finished();

private:
	QString videoPath;
	QString modelName = "base";
	QString languageCode = "en";

	void runTranscription();
};

/* Main dialog */
class VideoAnalyzerDialog : public QDialog {
	Q_OBJECT

public:
	VideoAnalyzerDialog(QWidget *parent = nullptr);
	~VideoAnalyzerDialog();

protected:
	void keyPressEvent(QKeyEvent *event) override;

private slots:
	void onBrowseClicked();
	void onAnalyzeClicked();
	void onPlayPauseClicked();
	void onStopClicked();
	void onPositionChanged(qint64 position);
	void onDurationChanged(qint64 duration);
	void onTimelineClicked(qint64 position);
	void onTranscriptClicked();
	void onAnalysisProgress(int percent, const QString &status);
	void onTranscriptionComplete(const QJsonArray &segments);
	void onAnalysisError(const QString &error);
	void onExportClicked();
	void onSelectionChanged(qint64 start, qint64 end);
	void onExportClipClicked();
	void onCutSectionClicked();
	void onClearSelectionClicked();
	void onUndoCutClicked();
	void onSaveEditedVideoClicked();

private:
	void setupUI();
	void updatePlayPauseButton();
	QString formatTime(qint64 ms);
	void populateTranscript();
	void onExportJSON();
	void onExportCSV();
	void onExportYouTubeAI();
	void loadWaveform(const QString &videoPath);
	void cutSelectedTranscriptSegment();
	int getTranscriptSegmentAtCursor();
	std::pair<int, int> getSelectedTranscriptRange();
	
	/* Search functionality */
	void onSearchTextChanged(const QString &text);
	void onSearchNext();
	void onSearchPrev();
	void highlightSearchResults();
	void jumpToSearchResult(int index);

	FFmpegVideoPlayer *videoPlayer = nullptr;
	QScrollArea *timelineScroll = nullptr;
	VideoTimeline *timeline = nullptr;
	QPushButton *playPauseButton = nullptr;
	QPushButton *stopButton = nullptr;
	QSlider *volumeSlider = nullptr;
	QLabel *timeLabel = nullptr;
	
	/* Clip selection controls */
	QWidget *clipActionsWidget = nullptr;
	QLabel *selectionLabel = nullptr;
	QPushButton *exportClipButton = nullptr;
	QPushButton *cutSectionButton = nullptr;
	QPushButton *clearSelectionButton = nullptr;
	QPushButton *undoCutButton = nullptr;
	QPushButton *saveEditedButton = nullptr;
	
	bool updatingTranscript = false;
	qint64 skipTargetPosition = 0;

	/* File selection */
	QLineEdit *videoPathEdit = nullptr;
	QPushButton *browseButton = nullptr;

	/* Analysis options */
	QComboBox *modelCombo = nullptr;
	QComboBox *languageCombo = nullptr;
	QPushButton *analyzeButton = nullptr;
	QProgressBar *progressBar = nullptr;
	QLabel *statusLabel = nullptr;

	/* Results */
	QTextEdit *transcriptView = nullptr;
	
	/* Search */
	QLineEdit *searchEdit = nullptr;
	QPushButton *searchPrevButton = nullptr;
	QPushButton *searchNextButton = nullptr;
	QLabel *searchResultsLabel = nullptr;
	std::vector<int> searchMatchIndices; /* Indices of matching transcript segments */
	int currentSearchIndex = -1;
	QString currentSearchQuery;
	
	/* Export */
	QComboBox *exportFormatCombo = nullptr;
	QPushButton *exportButton = nullptr;

	/* Data */
	std::vector<TranscriptSegment> transcriptSegments;
	QThread *analysisThread = nullptr;
	VideoAnalysisWorker *analysisWorker = nullptr;
};

/* Module functions */
extern "C" void InitVideoAnalyzer(void);
extern "C" void FreeVideoAnalyzer(void);

