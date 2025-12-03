/*
 * FFmpeg-based Video Player Widget
 * Uses only FFmpeg (bundled with OBS) - no Qt Multimedia plugins required
 */

#pragma once

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QLabel>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QAudioOutput>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <atomic>
#include <queue>
#include <functional>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

/* Video frame with timestamp */
struct VideoFrame {
	QImage image;
	double pts; /* Presentation timestamp in seconds */
};

/* Decoder thread that reads and decodes video/audio */
class VideoDecoderThread : public QThread {
	Q_OBJECT

public:
	/* Seek mode determines how the seek is processed */
	enum class SeekMode {
		Simple,       /* Seek to keyframe, use sync flags to discard old frames */
		PastPosition  /* Decode forward until we're past target (frame-accurate) */
	};

	VideoDecoderThread(QObject *parent = nullptr);
	~VideoDecoderThread();

	bool openFile(const QString &path);
	void closeFile();
	
	void play();
	void pause();
	void stop();
	void seek(double seconds);
	
	/* Frame-accurate seek that decodes forward past target position.
	 * This guarantees landing at or past targetSeconds, avoiding keyframe issues. */
	void seekPastPosition(double targetSeconds);
	
	/* Reset state for proper restart after EOF */
	void resetSeekState();
	
	/* Wait for thread startup with timeout (returns true if started) */
	bool waitForStartup(int timeoutMs);
	
	/* Check if a seek is currently being processed */
	bool isSeekInProgress() const { return seekInProgress.load(); }
	
	/* Get current seek sequence number for stale update detection */
	uint64_t getSeekSequence() const { return seekSequence.load(); }
	
	double getDuration() const { return duration; }
	double getPosition() const { return currentPosition; }
	bool isPlaying() const { return playing; }
	bool hasVideo() const { return videoStreamIndex >= 0; }
	bool hasAudio() const { return audioStreamIndex >= 0; }
	int getWidth() const { return videoWidth; }
	int getHeight() const { return videoHeight; }

signals:
	void frameReady(const QImage &frame, double pts);
	void audioReady(const QByteArray &data);
	/* Position changed with sequence number for stale update detection */
	void positionChanged(double seconds, uint64_t sequence);
	void durationChanged(double seconds);
	void stateChanged(bool playing);
	void seekOccurred(); /* Notify to flush audio buffers */
	/* Seek completed with actual position reached and success flag */
	void seekCompleted(double actualPosition, bool success);
	void endOfFile();
	void errorOccurred(const QString &error);

protected:
	void run() override;

private:
	void decodeLoop();
	bool decodeVideoFrame(AVPacket *packet);
	bool decodeAudioFrame(AVPacket *packet);
	void emitPositionThrottled(double pts);
	void recreateAudioResampler();
	QImage convertFrameToImage(AVFrame *frame);
	
	/* Performs the frame-accurate seek (called from decodeLoop) */
	void performSeekPastPosition(double targetSeconds);
	
	/* FFmpeg contexts */
	AVFormatContext *formatCtx = nullptr;
	AVCodecContext *videoCodecCtx = nullptr;
	AVCodecContext *audioCodecCtx = nullptr;
	SwsContext *swsCtx = nullptr;
	SwrContext *swrCtx = nullptr;
	
	int videoStreamIndex = -1;
	int audioStreamIndex = -1;
	
	int videoWidth = 0;
	int videoHeight = 0;
	double duration = 0.0;
	double currentPosition = 0.0;
	double videoTimeBase = 0.0;
	double audioTimeBase = 0.0;
	
	std::atomic<bool> playing{false};
	std::atomic<bool> stopRequested{false};
	std::atomic<bool> seekRequested{false};
	std::atomic<double> seekTarget{0.0};
	
	/* Seek mode: Simple vs PastPosition (frame-accurate) */
	std::atomic<SeekMode> seekMode{SeekMode::Simple};
	
	/* Track sync after seek - discard frames before this PTS */
	std::atomic<double> audioSyncTarget{0.0};
	std::atomic<bool> audioSyncPending{false};
	std::atomic<double> videoSyncTarget{0.0};
	std::atomic<bool> videoSyncPending{false};
	
	/* Thread-safe seek tracking */
	std::atomic<bool> seekInProgress{false};
	std::atomic<uint64_t> seekSequence{0};
	
	/* Rate limiting for position updates (~20Hz) */
	std::chrono::steady_clock::time_point lastPositionEmit;
	static constexpr int POSITION_UPDATE_INTERVAL_MS = 50;
	
	/* Mutex for codec operations (flush must be synchronized) */
	QMutex codecMutex;
	
	/* Thread startup synchronization */
	QMutex startupMutex;
	QWaitCondition startupCondition;
	bool startupComplete = false;
};

/* Custom video display widget */
class FFmpegVideoPlayer : public QWidget {
	Q_OBJECT

public:
	FFmpegVideoPlayer(QWidget *parent = nullptr);
	~FFmpegVideoPlayer();

	bool openFile(const QString &path);
	void closeFile();
	
	void play();
	void pause();
	void stop();
	void seek(double seconds);
	
	/* Frame-accurate seek that guarantees landing at or past target.
	 * Use this for cut region skipping to avoid keyframe loops. */
	void seekPastPosition(double targetSeconds);
	
	void setVolume(float volume); /* 0.0 to 1.0 */
	
	/* Check if a seek is currently being processed */
	bool isSeekInProgress() const;
	
	double getDuration() const;
	double getPosition() const;
	bool isPlaying() const;
	bool hasVideo() const;
	
	QSize sizeHint() const override;

signals:
	void positionChanged(qint64 positionMs);
	void durationChanged(qint64 durationMs);
	void playbackStateChanged(bool playing);
	/* Emitted when seek operation completes with success flag */
	void seekCompleted(double actualPositionSeconds, bool success);
	void errorOccurred(const QString &error);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void onFrameReady(const QImage &frame, double pts);
	void onAudioReady(const QByteArray &data);
	void onPositionChanged(double seconds, uint64_t sequence);
	void onDurationChanged(double seconds);
	void onStateChanged(bool playing);
	void onSeekOccurred();
	void onSeekCompleted(double actualPosition, bool success);
	void onEndOfFile();
	void onError(const QString &error);

private:
	void setupAudio();
	void applyFadeIn(QByteArray &audioData);
	
	VideoDecoderThread *decoder = nullptr;
	QImage currentFrame;
	QImage scaledFrame;
	
	/* Audio output */
	QAudioSink *audioSink = nullptr;
	QIODevice *audioDevice = nullptr;
	float volume = 0.7f;
	
	/* Audio fade-in after seek to prevent pops - increased for smoother transition */
	int fadeInSamplesRemaining = 0;
	static constexpr int FADE_IN_SAMPLES = 2048; /* ~46ms at 44100Hz for smoother fade */
	
	/* Track current seek sequence for stale update detection */
	uint64_t currentSeekSequence = 0;
	
	int videoWidth = 0;
	int videoHeight = 0;
};

