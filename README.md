# OBS AI Toolkit

A video analysis plugin for OBS Studio featuring automatic transcription, AI-powered chapter generation, and non-linear video editing capabilities.

## Original Repository

This project is built on top of [OBS Studio](https://github.com/obsproject/obs-studio), the free and open-source software for video recording and live streaming.

## What I Built

**OBS AI Toolkit** is a native C++/Qt plugin that adds intelligent video analysis capabilities directly into OBS Studio:

- **Automatic Transcription** — Uses whisper.cpp to transcribe video audio locally, with word-level timestamps
- **AI Chapter Generation** — Leverages llama.cpp to analyze transcripts and generate semantic YouTube chapters
- **Transcript Search** — Find and navigate to specific moments in your video via text search
- **Non-Linear Editing** — Mark and cut sections directly from the transcript, export edited videos
- **Native Video Player** — Custom FFmpeg-based player with waveform visualization and timeline scrubbing

### Key Features

| Feature | Description |
|---------|-------------|
| Transcription | Local whisper.cpp inference, multiple model sizes (tiny→large), 99 languages |
| Chapter Generation | LLM-powered semantic chapter titles via llama-cli subprocess |
| Search | Real-time transcript search with match highlighting and navigation |
| Timeline | Waveform display, click-to-seek, cut region visualization |
| Export | JSON, CSV, YouTube chapters format, edited video with cuts applied |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                     VideoAnalyzerDialog                         │
│                      (Main UI / Qt Dialog)                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────┐ │
│  │ FFmpegVideoPlayer│  │  VideoTimeline   │  │ TranscriptView│ │
│  │   (video-player) │  │   (waveform +    │  │  (searchable  │ │
│  │                  │  │    cut regions)  │  │   text view)  │ │
│  └────────┬─────────┘  └────────┬─────────┘  └───────┬───────┘ │
│           │                     │                     │         │
├───────────┴─────────────────────┴─────────────────────┴─────────┤
│                        Core Components                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────┐ │
│  │   ffmpeg-utils   │  │  whisper-utils   │  │  llama-runner │ │
│  │  (decode/encode) │  │  (transcription) │  │ (LLM chapters)│ │
│  └──────────────────┘  └──────────────────┘  └───────────────┘ │
│           │                     │                     │         │
│           ▼                     ▼                     ▼         │
│      libavcodec            whisper.cpp           llama-cli      │
│      libavformat           (static lib)          (bundled via   │
│      libswscale                                   ExternalProject│
│      libswresample                                + subprocess) │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### File Structure

```
frontend/plugins/obs-ai-toolkit/
├── video-analyzer-dialog.cpp/hpp  # Main dialog, UI, coordination
├── video-player.cpp/hpp           # FFmpeg-based video playback widget
├── ffmpeg-utils.cpp/hpp           # Audio extraction, video encoding
├── whisper-utils.cpp/hpp          # Whisper model loading, transcription
├── llama-runner.cpp/hpp           # LLM subprocess management
├── CMakeLists.txt                 # Build configuration
└── test/
    ├── test-utils.cpp             # Unit tests (26 tests)
    └── test-integration.cpp       # Integration tests (25 tests)

cmake/
└── BuildLlamaCpp.cmake            # ExternalProject config for llama.cpp
                                   # Builds llama-cli (b5270) with Metal on macOS
                                   # Universal binary (arm64 + x86_64)
```

## Setup + Run Steps

### Prerequisites

- **macOS** (Apple Silicon or Intel)
- **Xcode** with command line tools
- **CMake** 3.28+

### Build

```bash
# Clone the repository
git clone --recursive https://github.com/user/obs-studio.git
cd obs-studio

# Configure with CMake
cmake --preset macos

# Build
cd build_macos
xcodebuild -project obs-studio.xcodeproj -configuration RelWithDebInfo -jobs 8

# Launch
open frontend/RelWithDebInfo/OBS.app
```

### AI Chapter Generation

The llama.cpp inference engine is built automatically during compilation and bundled with OBS.app — no additional installation required. The plugin downloads LLM models on-demand when you first use chapter generation.

### Using the Plugin

1. Open OBS Studio
2. Go to **Tools → Video Analyzer**
3. Browse and select a video file
4. Choose a Whisper model (base recommended for speed/quality balance)
5. Click **Analyze** to transcribe
6. Use the transcript to navigate, search, or mark cuts
7. Export as JSON, CSV, or YouTube Chapters

## Testing

The plugin includes 51 tests covering core functionality.

### Run Tests

```bash
# Configure with tests enabled
cd build_macos
cmake -DENABLE_AI_TOOLKIT_TESTS=ON ..

# Build tests
cmake --build . --target test-ai-toolkit-utils test-ai-toolkit-integration

# Run unit tests (26 tests)
DYLD_FRAMEWORK_PATH="../.deps/obs-deps-qt6-2025-08-23-universal/lib" \
  ./frontend/plugins/obs-ai-toolkit/test/Debug/test-ai-toolkit-utils

# Run integration tests (25 tests)
DYLD_FRAMEWORK_PATH="../.deps/obs-deps-qt6-2025-08-23-universal/lib" \
DYLD_LIBRARY_PATH="../.deps/obs-deps-2025-08-23-universal/lib" \
  ./frontend/plugins/obs-ai-toolkit/test/Debug/test-ai-toolkit-integration
```

### Test Coverage

| Category | Tests |
|----------|-------|
| Model management (Whisper/LLM) | 8 |
| Timeline position/pixel math | 4 |
| Cut region logic | 4 |
| Time formatting | 4 |
| YouTube chapter format/parse | 4 |
| Transcript segments | 2 |
| FFmpeg error handling | 10 |
| Filesystem operations | 5 |
| Process execution | 3 |
| Model URL validation | 3 |
| Error handling | 4 |

## Technical Decisions

### Why Native FFmpeg Instead of Qt Multimedia?

Qt Multimedia requires platform-specific plugins (e.g., `libqavfmediaplayer.dylib` on macOS) that aren't bundled with OBS. Rather than adding complex plugin dependencies, I built a custom video player using FFmpeg directly:

- **Pros**: No plugin dependencies, consistent behavior, full control over decode/display pipeline
- **Cons**: More code to maintain, manual audio/video sync

### Why whisper.cpp as Static Library?

The original implementation used Python + openai-whisper, requiring users to have Python and pip packages installed. Embedding whisper.cpp directly:

- **Eliminates Python dependency** — Works out of the box
- **Better integration** — Progress callbacks, cancellation, memory management
- **Smaller footprint** — No Python runtime overhead

### Why llama-cli Subprocess Instead of Linked Library?

whisper.cpp includes ggml (the tensor library). Linking llama.cpp would cause symbol conflicts since both include different ggml versions. Using CMake ExternalProject to build llama.cpp in complete isolation and invoking it as a subprocess:

- **Avoids symbol conflicts** — Built in separate build tree, no shared symbols
- **Zero user setup** — llama-cli is bundled with OBS.app automatically
- **GPU acceleration** — Built with Metal support on macOS for fast inference
- **Universal binary** — arm64 + x86_64 for all Mac hardware

### UI/UX Decisions

- **Transcript-centric workflow** — Click any line to seek to that moment
- **Non-destructive editing** — Cut regions are visual markers; original file untouched until export
- **Search as navigation** — Treat transcript like a document, find moments by content

---

## License

This project inherits OBS Studio's GPLv2 license. See [COPYING](COPYING) for details.

