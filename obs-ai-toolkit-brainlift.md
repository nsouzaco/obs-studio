# OBS AI Toolkit - Development Summary

## Project Overview

A native OBS Studio plugin for AI-powered video analysis: automatic transcription via Whisper and YouTube chapter generation via local LLMs. Started as a Python prototype, evolved into fully native C++/Qt through extensive debugging and architectural pivots.

---

## Phase 1: Python Prototype (Abandoned)

**User:** *"This approach feels too hacky for proper OBS integration."*

Shelling out to Python for transcription required users to have Python installed, manage pip dependencies, and deal with PATH issues. Decision: pivot to native C++/Qt plugin.

---

## Phase 2: Qt Multimedia Video Player (Failed)

Qt Multimedia requires platform-specific backend plugins not bundled with OBS. We tried copying plugins from Homebrew, fixing directory structures, rewriting library paths with install_name_tool. Nothing worked reliably.

**User:** *"This needs to work on any user's machine. Why are we relying on Homebrew paths when FFmpeg is already bundled in the repo?"*

**Decision:** Build a custom video player using FFmpeg (already bundled with OBS) instead of fighting Qt Multimedia's plugin system.

---

## Phase 3: Custom FFmpeg Player

Built a two-thread architecture: background VideoDecoderThread running av_read_frame loop, main thread FFmpegVideoPlayer rendering frames via QPainter. Audio through QAudioSink. A/V sync via wall-clock timing.

---

## Phase 4: Seeking Problems

**User:** *"Seeking is broken—clicking the timeline jumps to incorrect positions and causes stuttering."*

**Root cause:** FFmpeg's av_seek_frame seeks to the nearest keyframe, not the exact timestamp. Seeking to 5.0s might land at a keyframe at 3.0s, and the decoder was emitting all those intermediate frames.

**Fix:** Discard decoded frames until their timestamp reaches the actual seek target.

---

## Phase 5: Audio Crackling

**User:** *"Audio works, but there's crackling for several seconds after each seek. I've tried some fixes and it's improved but still not clean—can you do deeper research on this?"*

**Root causes:** Stale audio in FFmpeg decoder buffers, stale audio in QAudioSink (~500ms buffer), swresample retaining pre-seek state, discontinuous timestamps confusing sync.

Web research revealed professional players use audio fading to mask discontinuities. An abrupt start creates a "step" in the waveform that sounds like a click.

**Fix:** Multi-level flush (FFmpeg decoders + swresample + QAudioSink) plus ~23ms quadratic fade-in after seek.

---

## Phase 6: The Cut Region Loop Disaster

Users can mark video segments as "cut regions" that skip during playback. When the playhead enters a cut region, it should jump to the end.

**User:** *"Playing through cut regions is completely broken—the video jumps erratically, audio crackles, and sometimes the app freezes. We've tried multiple fixes but keep hitting the same issue. Research how other FFmpeg-based video editors handle this."*

### The Feedback Loop

The cut-skip logic ran in onPositionChanged. It would detect we're in a cut region and trigger a seek to the end. But FFmpeg's seek lands on a keyframe *before* the target, so the next position update still shows us inside the cut region, triggering another seek. Infinite loop.

### Going in Circles

We tried multiple fixes that all failed:
1. **State enum (Idle/Seeking/WaitingForTarget)** - Position updates from before the seek kept arriving, clearing the state prematurely
2. **Blocking decode-forward seek** - Interfered with the normal decode loop, caused deadlocks
3. **500ms time-based debounce** - Video would pause awkwardly and never advance

### Breaking Out: The Expert Prompt

After going in circles, the breakthrough came from reframing the problem. Instead of asking for another quick fix, I prompted Claude as a domain expert:

*"Act as a senior software engineer specialized in FFmpeg and media player development. Here's my two-thread architecture, here's the cut-skip logic, here's what happens when I seek. Multiple seeks are cascading. What am I fundamentally misunderstanding about FFmpeg seeking?"*

This produced the key insight: **don't emit position updates immediately after a seek**. The decoder should discard frames until it actually reaches the target timestamp, then start emitting positions. Combined with a skip-target guard that suppresses cut-region checks until we've actually passed the target, this finally broke the loop.

---

## Phase 7: Transcript Click Bug

**User:** *"Clicking transcript lines to seek sometimes jumps to random positions or triggers multiple seeks."*

**Root cause:** The click handler was connected to Qt's cursorPositionChanged signal. When we updated transcript HTML (for search highlighting or after cuts), setHtml triggered cursor changes that our handler treated as clicks.

**Fix:** Guard flag set during programmatic updates, checked in click handler.

---

## Phase 8: Search Button Icons Invisible

**User:** *"The search navigation buttons render as empty squares, but the same Unicode characters display correctly on the play button. We've tried plain text, Unicode arrows, emojis, various font stylesheets, and explicit color styling—nothing works. Do a thorough investigation on why these specific buttons behave differently."*

**Root cause:** The play button had no stylesheet. The search buttons had a stylesheet for disabled state colors. In Qt, applying ANY stylesheet breaks theme inheritance for ALL properties—including font rendering.

**Fix:** Remove the stylesheet entirely.

---

## Phase 9: Native Whisper Integration

Shelling to Python for transcription caused endless user issues: missing Python, pip failures, PATH problems, no progress callbacks.

**Fix:** Integrated whisper.cpp as a static library with native progress callbacks.

---

## Phase 10: LLM Symbol Conflicts

Adding llama.cpp for chapter generation caused duplicate symbol errors—both whisper.cpp and llama.cpp bundle their own copy of ggml.

**Fix:** Use llama-cli as a subprocess instead of linking the library. No symbol conflicts, users can upgrade independently, plugin works without LLM installed.

---

## Phase 11: LLM Chapter Generation

**User:** *"The YouTube chapters are complete nonsense—not related to the video content at all."*

### Issues Found

1. **Model too small** - Started with 0.5B parameters, couldn't follow complex instructions
2. **Wrong chat template** - Phi-3 produced gibberish because we used generic formatting instead of its specific template
3. **Transcript too long** - 8,921 characters overwhelmed instruction-following
4. **Negative examples backfire** - "BAD (don't do this)" examples confused small models into following them
5. **Malformed first chapter** - Primed with "0:00 " but model started at different timestamp, causing concatenation
6. **Model download 404** - Large models split into multiple files on HuggingFace

### Final Configuration
- Qwen2.5-3B (3 billion parameters)
- 3,000 character transcript limit
- Positive examples only
- Proper Qwen chat template
- Complete first chapter priming

---

## Summary: All Problems

| Problem | Root Cause | Fix |
|---------|------------|-----|
| Python feels hacky | Not proper integration | Native C++ plugin |
| Black video screen | Qt Multimedia plugins missing | Custom FFmpeg player |
| Seek jumps wrong | Keyframe-based seeking | Frame discarding |
| Audio crackling | Stale buffers everywhere | Multi-flush + fade-in |
| Cut region loop | Seek lands before target | Skip-target guard + frame discard |
| Transcript click bug | HTML update triggers cursor | Guard flag |
| Invisible buttons | Qt stylesheet breaks inheritance | Remove stylesheet |
| LLM garbage | Model too small | Upgrade to 3B |
| LLM copies text | Negative examples backfire | Positive only |
| Download 404 | Model split into parts | Single-file variant |

---

## Key Technical Decisions

| Decision | Why |
|----------|-----|
| Custom FFmpeg player | Qt Multimedia plugins not bundled with OBS |
| whisper.cpp static lib | Eliminates Python dependency |
| llama-cli subprocess | Avoids ggml symbol conflicts |
| Frame discarding after seek | Handles keyframe granularity |
| Audio fade-in | Masks discontinuity artifacts |
| Position-based skip guard | More reliable than time debounce |
| Remove button stylesheets | Qt inheritance is all-or-nothing |
| 3B+ model minimum | Smaller can't follow complex prompts |
| 3000 char transcript limit | Shorter = better instruction-following |

---

## Lessons Learned

### Platform
- Don't rely on machine-specific paths (Homebrew)
- Use bundled dependencies (OBS has FFmpeg)
- Python runtime dependency = user friction

### Qt
- Stylesheets break ALL theme inheritance, not just what you set
- Signals fire on programmatic changes, not just user actions
- Guard flags beat signal disconnect/reconnect

### FFmpeg
- Seeking is keyframe-based—always land before target
- Building a proper player requires attention to buffering and timing
- Audio fading masks artifacts that flushing can't fix

### LLM
- Sub-3B models can't follow complex instructions
- Negative examples backfire with small models
- Chat templates are model-specific
- Large HuggingFace models may be split files

### Debugging
- When stuck in loops, reframe as expert consultation
- Logging everywhere is essential
- Some issues need architectural changes, not quick fixes
