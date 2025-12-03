# OBS AI Toolkit - Development Journal

A detailed chronicle of the problems, bugs, technical decisions, and solutions encountered while building the OBS AI Toolkit plugin.

---

## Phase 1: Qt Multimedia Video Player (Failed Approach)

### The Problem

The initial implementation used Qt's `QMediaPlayer` and `QVideoWidget` for video playback:

```cpp
mediaPlayer = new QMediaPlayer(this);
QAudioOutput *audioOutput = new QAudioOutput(this);
mediaPlayer->setAudioOutput(audioOutput);
mediaPlayer->setVideoOutput(videoWidget);
```

**What went wrong:** On macOS, Qt Multimedia requires platform-specific plugins (`libqavfmediaplayer.dylib`) that aren't bundled with OBS Studio. Users would see "No video" or get cryptic errors about missing backends.

### User Prompt That Surfaced This
> "The video won't play, it just shows a black screen"

### Technical Decision
**Build a custom FFmpeg-based video player** instead of relying on Qt Multimedia. OBS already bundles FFmpeg, so we can use it directly without additional dependencies.

---

## Phase 2: Building the FFmpeg Video Player

### Architecture Chosen

```
┌─────────────────────────────┐
│   FFmpegVideoPlayer         │  (Main thread - Qt Widget)
│   - Receives frames via     │
│     Qt signals              │
│   - Renders to QLabel       │
│   - Manages QAudioSink      │
└─────────────┬───────────────┘
              │ Qt signals (queued)
┌─────────────▼───────────────┐
│   VideoDecoderThread        │  (Background thread)
│   - av_read_frame loop      │
│   - Video/audio decoding    │
│   - Emits frames + audio    │
└─────────────────────────────┘
```

### Key Implementation Details

1. **Decode loop runs in QThread** - Prevents UI blocking during decode
2. **Frames sent via signals** - `frameReady(QImage)` and `audioReady(QByteArray, double pts)`
3. **A/V sync via wall clock** - Video frames sleep until their PTS matches elapsed time

---

## Phase 3: The Seeking Nightmare

### Problem: Erratic Playback After Seek

When clicking the timeline to seek, the video would:
- Jump to the wrong position
- Stutter and skip frames
- Sometimes freeze entirely

### Root Cause Analysis

FFmpeg's `av_seek_frame` seeks to the nearest **keyframe**, not the exact timestamp. If you seek to 5.0s but the nearest keyframe is at 3.0s, you get frames starting from 3.0s.

```cpp
// This seeks to a keyframe BEFORE the target
av_seek_frame(formatCtx, videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
```

The decode loop was emitting those intermediate frames, causing visual jumps.

### Solution: Frame Discarding

After seek, discard decoded frames until we reach the target:

```cpp
if (seekJustCompleted) {
    if (videoPts < seekTarget - 0.1) {
        continue;  // Discard this frame
    }
    seekJustCompleted = false;
}
```

---

## Phase 4: Audio Crackling

### Problem Description

After any seek operation, audio would crackle and pop for 0.5-1 second.

### Why It Happens

1. **Stale audio in decode buffers** - FFmpeg's audio decoder has internal buffers with old samples
2. **Stale audio in QAudioSink** - The Qt audio output has ~500ms of buffered audio
3. **Discontinuous PTS** - Audio PTS jumps after seek, causing sync issues
4. **swresample state** - The resampler context retains history from pre-seek audio

### Solution: Multi-Level Flush

```cpp
void FFmpegVideoPlayer::onSeekOccurred()
{
    // 1. Flush Qt audio buffer
    if (audioSink && audioDevice) {
        audioSink->stop();
        audioDevice = audioSink->start();
    }
    
    // 2. Enable fade-in to mask any remaining artifacts
    fadeInSamplesRemaining = FADE_IN_SAMPLES;  // ~23ms
}
```

Plus in the decoder:
```cpp
// Flush FFmpeg decoders
avcodec_flush_buffers(videoCodecCtx);
avcodec_flush_buffers(audioCodecCtx);

// Flush resampler
swr_convert(swrContext, nullptr, 0, nullptr, 0);
```

---

## Phase 5: Cut Region Skip Loop

### The Bug

When playback reached a "cut" region (a segment the user marked for removal), the player would:
- Skip to the end of the cut region
- Immediately trigger another skip
- Get stuck in an infinite loop
- Sometimes crash

### User Prompt
> "When I cut a section and play through it, the video goes crazy and jumps everywhere"

### Root Cause

```cpp
void VideoAnalyzerDialog::onPositionChanged(qint64 position)
{
    if (videoPlayer->isPlaying()) {
        for (const auto &region : cutRegions) {
            if (position >= region.first && position < region.second) {
                videoPlayer->seek(region.second / 1000.0);  // PROBLEM!
                return;
            }
        }
    }
}
```

The issue: **keyframe granularity**. When we seek to `region.second`, FFmpeg might land at a keyframe *before* that position. The next `positionChanged` event reports a position still inside the cut region, triggering another seek. Infinite loop.

### Solution: Skip Target Tracking

```cpp
if (videoPlayer->isPlaying()) {
    bool checkCutRegions = true;
    
    // If we just triggered a skip, wait until we pass the target
    if (skipTargetPosition > 0) {
        if (position >= skipTargetPosition)
            skipTargetPosition = 0;  // We made it past, clear target
        else
            checkCutRegions = false;  // Still waiting, don't re-trigger
    }
    
    if (checkCutRegions) {
        for (const auto &region : cutRegions) {
            if (position >= region.first && position < region.second) {
                skipTargetPosition = region.second;  // Remember target
                videoPlayer->seek((double)region.second / 1000.0);
                return;
            }
        }
    }
}
```

### What I Initially Tried (and removed in deslop)

My first fix used a `goto` statement which worked but was ugly:

```cpp
if (skipTargetPosition > 0) {
    if (position >= skipTargetPosition)
        skipTargetPosition = 0;
    goto auto_scroll;  // Skip cut region check
}
// ... cut region check ...
auto_scroll:
// ... scroll code ...
```

Refactored to use a boolean flag instead.

---

## Phase 6: Search Button Icons Not Displaying

### The Bug

The search navigation buttons (◀ and ▶) appeared as empty/blank buttons. The same Unicode characters worked fine on the play/pause button.

### User's Detailed Bug Report
> "Problem Summary: Search Bar Navigation Buttons Not Displaying Icons... The buttons are created and added to the layout (they appear as clickable squares). The text/icon inside is simply not visible. Identical Unicode characters (▶) work on the play button but not on these search buttons."

### What Was Tried (all failed)
- Plain text `<` and `>`
- Unicode arrows `←` and `→`
- Unicode triangles `◀` and `▶`
- Emojis `⬅️` and `➡️`
- Various font-size stylesheets
- Explicit `color: #fff` styling

### Root Cause

The working button:
```cpp
playPauseButton = new QPushButton("▶", this);
playPauseButton->setFixedWidth(40);
// No stylesheet!
```

The broken button:
```cpp
searchNextButton = new QPushButton("▶", this);
searchNextButton->setFixedWidth(40);
searchNextButton->setStyleSheet("QPushButton { color: #fff; } QPushButton:disabled { color: #666; }");
```

**When you apply ANY stylesheet to a QPushButton in Qt, it stops inheriting the default theme styling entirely** - including font rendering properties that make Unicode text visible.

### Solution

Just remove the stylesheet:
```cpp
searchNextButton = new QPushButton("▶", this);
searchNextButton->setFixedWidth(40);
// No stylesheet - let it inherit from theme
```

---

## Phase 7: Python Dependency Hell

### Original Architecture

The first transcription implementation shelled out to Python:

```cpp
QString pythonScript = R"PYTHON(
import whisper
model = whisper.load_model("base")
result = model.transcribe(video_path)
# ... output JSON ...
)PYTHON";

QProcess process;
process.start(pythonPath, {scriptPath, videoPath, outputPath});
```

### Problems

1. **Users need Python installed** - Many don't have it
2. **Need pip packages** - `pip install openai-whisper` fails in various ways
3. **PATH issues** - Finding the right Python on macOS is a nightmare
4. **Performance** - Python startup overhead, no progress callbacks

### Solution: Native whisper.cpp

Integrated whisper.cpp as a static library:

```cmake
add_subdirectory("${CMAKE_SOURCE_DIR}/deps/whisper.cpp" EXCLUDE_FROM_ALL)
target_link_libraries(obs-ai-toolkit whisper)
```

```cpp
WhisperTranscriber transcriber;
transcriber.loadModel("base");
WhisperResult result = transcriber.transcribe(
    videoPath,
    languageCode,
    [this](int progress, const std::string &status) {
        emit progressUpdated(progress, QString::fromStdString(status));
    }
);
```

---

## Phase 8: LLM Symbol Conflicts

### The Problem

Wanted to add llama.cpp for AI chapter generation, but:

```
duplicate symbol 'ggml_backend_buffer_init' in:
    libwhisper.a(ggml-backend.c.o)
    libllama.a(ggml-backend.c.o)
```

Both whisper.cpp and llama.cpp include their own copy of ggml (the tensor library), with slightly different versions.

### Solution: Subprocess Architecture

Instead of linking llama.cpp, use `llama-cli` as a subprocess:

```cpp
class LlamaRunner {
    QProcess process;
    
    std::vector<Chapter> generateChapters(const std::vector<Segment> &transcript) {
        QString prompt = buildPrompt(transcript);
        
        process.start("llama-cli", {
            "-m", modelPath,
            "-p", prompt,
            "--temp", "0.3"
        });
        
        // Parse JSON output
    }
};
```

**Benefits:**
- No symbol conflicts
- Users can upgrade llama.cpp independently
- Plugin works without LLM (just disables chapter generation)
- `brew install llama.cpp` for easy setup

---

## Phase 9: Transcript Click Seeking Bug

### The Bug

Clicking a transcript line to seek would sometimes jump to a random position, or trigger multiple seeks.

### Root Cause

When we update the transcript HTML (e.g., during search highlighting), Qt's `QTextEdit` fires cursor position change events. Our click handler was treating these as user clicks:

```cpp
void VideoAnalyzerDialog::onTranscriptClicked()
{
    QTextCursor cursor = transcriptView->textCursor();
    int block = cursor.blockNumber();
    // Seek to this block's timestamp...
}
```

### Solution: Guard Flag

```cpp
void VideoAnalyzerDialog::populateTranscript()
{
    updatingTranscript = true;  // Set guard
    transcriptView->setHtml(html);
    updatingTranscript = false;
}

void VideoAnalyzerDialog::onTranscriptClicked()
{
    if (updatingTranscript)  // Check guard
        return;
    // ... actual seek logic ...
}
```

---

## Summary of Technical Decisions

| Decision | Rationale |
|----------|-----------|
| Custom FFmpeg player over Qt Multimedia | Qt Multimedia needs plugins not bundled with OBS |
| whisper.cpp static lib over Python | Eliminates runtime dependencies, better integration |
| llama-cli subprocess over linked library | Avoids ggml symbol conflicts |
| Wall-clock A/V sync over audio master clock | Simpler implementation, good enough for our use case |
| Boolean guard over debounce timer for cut skips | More predictable behavior with keyframe seeking |
| Remove stylesheets from Unicode buttons | Qt stylesheet inheritance breaks font rendering |

---

## Files Modified/Created

```
frontend/plugins/obs-ai-toolkit/
├── video-analyzer-dialog.cpp  # Main dialog (heavily modified)
├── video-analyzer-dialog.hpp  # (modified)
├── video-player.cpp           # NEW - Custom FFmpeg player
├── video-player.hpp           # NEW
├── ffmpeg-utils.cpp           # NEW - Audio extraction, video export
├── ffmpeg-utils.hpp           # NEW
├── whisper-utils.cpp          # NEW - Whisper model management
├── whisper-utils.hpp          # NEW
├── llama-runner.cpp           # NEW - LLM subprocess wrapper
├── llama-runner.hpp           # NEW
└── CMakeLists.txt             # (modified to add deps)
```

---

## Lessons Learned

1. **Qt stylesheets are all-or-nothing** - Setting any property breaks theme inheritance for *all* properties
2. **FFmpeg seek is keyframe-based** - Always expect to land before your target
3. **Threading + Qt signals = eventual consistency** - Position updates arrive asynchronously; state machines help
4. **Symbol conflicts in C libraries are nasty** - Subprocesses are a valid architectural escape hatch
5. **Python as a runtime dependency is painful** - Native code eliminates entire categories of user issues

