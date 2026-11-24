# Test strategy and failure-mode inventory

This file is the risk register required by the writing-tests workflow. Each
test binds to one or more numbered failure modes (FM). A test that cannot be
bound to a row below should not exist; a row with no test must carry a reason.

Red/green calibration and run history are recorded at the bottom; update them
when a test or a fix changes.

## Tested object

- `angelsound_common`: `wav`, `practice`, `recordlist`, `storage_pure`, `ui_layout`
  (pure C, no SDL device needed); `audio_format` needs SDL headers only.
- `angelsound_audio`: `audio.c` (SDL device boundary) — desktop smoke covers it.
- `storage.c` / `ui.c` / `main.c`: exercised by the desktop `--smoke` on three
  CI platforms. Android JNI/SAF glue is **not** device-tested here (see EXCLUDED).

Entry points: CTest `test_*` executables + `AngelSound --smoke` in CI.

## Failure modes and bindings

| ID | Class / mechanism | Result category | Injection / observation | Tests |
|---|---|---|---|---|
| FM-1 | WAV header length fields left at zero after recording | corrupt file / bad duration | write samples then re-read header; assert RIFF/data sizes and byte rate | `test_wav` |
| FM-2 | 8/16/32-bit float WAV formats misparsed | false corruption | synthesize formats with hand-built headers; assert decoded fields | `test_wav` |
| FM-3 | WAVE_FORMAT_EXTENSIBLE SubFormat GUID misread (old bug: offset 18 vs 24) | false corruption | 0xFFFE header with PCM tag at +24; assert PCM accepted | `test_wav` (regression) |
| FM-4 | Malformed / truncated / oversized-declared data chunk read out of bounds or crash | crash | "not a wav", 24-bit unsupported, data length 100 in 48-byte file; assert rejection/clamping | `test_wav` |
| FM-5 | Practice timer double-counts duplicate take-stop or counts idle time | wrong status | stop twice, start twice, reset; assert takes/session/last and warn threshold | `test_practice` |
| FM-6 | 5-minute warning fires repeatedly or never | wrong status | cross PRACTICE_WARN_MS, mark_warned, reset; assert one-shot | `test_practice` |
| FM-7 | Record list duplicates, wrong index after delete, bad sort order | wrong selection | add/sort/find/remove/invalid-index/clear; assert indices | `test_recordlist` |
| FM-8 | Settings file corrupted by partial line, CRLF, BOM, missing key | wrong folder | parse variants incl. BOM, CRLF, empty, too-small buffer; assert value or zero | `test_storage_pure` |
| FM-9 | Recording name collisions overwrite existing take | data loss | suffix_name with 0..N; join with/without separator, cap overflow | `test_storage_pure` |
| FM-10 | SAF tree URI stored in a filesystem-path field (platform mix-up) | wrong folder | classify content:// vs path; assert storage_is_saf | `test_storage_pure` |
| FM-11 | Capture format conversion sneaks in / unsupported device format silently converted | wrong audio | reject signed-8, unsigned-16, big-endian, 24-bit; accept U8/S16LE/S32LE/F32LE | `test_audio_format` |
| FM-12 | UI buttons overlap, fall off screen, active state wrong, hit test wrong | UI unusable | layout 10 buttons at all audio states; assert bounds, no overlap, enablement, hits | `test_ui_layout` |
| FM-13 | List selected index out of range after folder change / empty folder | UI crash | clamp with 30 files, empty list, out-of-range selected/scroll | `test_ui_layout` |
| FM-14 | Long names overflow clip buffer | crash/garbage | clip with cap 4 and max 6; assert safe "..."/empty | `test_ui_layout` |
| FM-15 | Recording lost when app goes to background (Android) | data loss | event_watch stops+commits before sleep; desktop smoke exercises same path via shared code | code + smoke; **真机未测** |
| FM-16 | Microphone permission denied → silent failure | wrong status | state machine pending/granted/denied + toast; desktop smoke has permission absent | code + smoke |
| FM-17 | SDL init/window/renderer unavailable in CI | false test failure | smoke degrades to init-only, prints video/renderer; three-platform CI | `--smoke` |
| FM-18 | MSVC/C99 compound literal portability | build failure | cl 19.44 builds on school host; CI windows-latest | CI matrix |
| FM-19 | Android Gradle/SDL/CMake wiring broken | build failure | assembleDebug in CI job; three real failures caught and fixed: retired `tools` SDK package, stale gradle-wrapper.jar (wrapper validation), `DOWNLOAD_EXTRACT_TIMESTAMP` on CMake 3.22; AAR/Prefab migration replaces the vendored SDL Java tree and FetchContent SDL with the official SDL3 3.4.20 AAR, downloaded and SHA-256 checked in CI | CI android job |
| FM-20 | SAF folder picker result never delivered | wrong state | reproduced on ZTE A2020U Pro (Android 10) on pre-AAR and AAR builds. Root cause: Android destroys/recreates the activity while the SAF picker is on top; SDL's default `SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY=0` exits the process, and the native main loop also treated Android SDL_EVENT_QUIT as program exit. Fixes: (1) manifest meta-data `SDL_ENV.SDL_ANDROID_ALLOW_RECREATE_ACTIVITY=1` so SDL reads it in onCreate before the recreate guard, (2) ignore SDL_EVENT_QUIT on Android so the native loop survives, (3) launchMode singleTop. Verified on-device twice: after Allow, `settings.txt` contains the picked `content://...` URI, PID stays stable, URI survives force-stop/relaunch | on-device ZTE A2020U Pro |

EXCLUDED (with reason):
- Real microphone capture / actual WAV byte content from device: CI uses dummy
  audio; needs a human with a device. Same for public-Music SAF picker UX.
- Power-loss/crash during WAV write: no journaling in this app; a truncated
  take is a legal outcome (FM-4 ensures readers don't crash). Not injecting.
- Concurrency stress on SDL audio callbacks: SDL owns the callback thread; our
  loop drains with SDL API; fuzzing that boundary needs a device harness.

## Invariants checked after every run

- No test leaves `test_*.wav` behind (each case ends with `remove(path)`).
- `wav_read` never returns `data_bytes` past the file's own declared chunk.
- UI layout always exactly 10 buttons, all inside 720x460, no overlap.
- `recordlist_free` leaves count=0 and names=NULL.

## Run layers

- L0 (per edit, local): `py S:\apps\check\check_c.py <project>` (clangd+clice)
  then build & `ctest` on the school Windows host (gcc + MSVC where available).
- L2 (per push): GitHub Actions build-test (Windows/macOS/Ubuntu) + android
  assembleDebug.
- L6: regression tests for FM-3 and FM-6 are permanent members of L0.

## Red/green evidence

- FM-3: before moving the WAVE_FORMAT_EXTENSIBLE SubFormat read from fmt+18 to
  fmt+24 the test would reject a valid PCM extensible WAV (observed red with
  the hand-built fixture). After fix: green.
- FM-6: `practice_should_warn` initially failed a test because the earlier
  2.5s take was not reset in the test fixture; fixing the fixture made both
  the reset path and the one-shot assertion green (regression kept).
- FM-11: initial `audio_format_supported` accepted unsigned 16-bit PCM, which
  WAV cannot represent; the red case `SDL_AUDIO_U16LE` (later replaced with
  the raw 0x8110-equivalent) caught it; adding the signed check made it green.
- Revert-fix check 2026-11-23 on school MinGW gcc 16.2: temporarily reverting
  the WAVE_FORMAT_EXTENSIBLE offset made `wav` fail 5 assertions; reverting the
  signed-PCM check made `audio_format` fail its U16 case; restoring both gave
  6/6 passed, 0 compiler warnings, smoke `video=offscreen renderer=yes`.
- Android JNI/SAF code path has no local emulator: classified NEEDS_PORT locally;
  CI `android (assembleDebug)` is the compile-level gate.

## Run log

- 2026-11-23, school Win11-HV1 via PC2 anchor, MinGW ucrt64 gcc 16.2 / SDL3 3.4.20,
  L0: CHECK PASS (clangd 16/16 + clice 0 findings on the local host), CMake build
  0 warnings, CTest 6/6 passed, smoke OK. Git tree fingerprint recorded in the
  commit above this run.
- 2026-11-23, CI run 45085852673 (L2): windows/ubuntu/macos build-test all green
  (6/6 ctest, smoke renderer=yes each), android assembleDebug green and APK
  artifact uploaded (2.6 MB). Android compile-level gate now passes; runtime
  (mic, SAF) still NEEDS_PORT to a real device.
- 2026-11-23, failure triage: red-green above is code-failure evidence, not an
  environment failure; WSL UNC header access was an environment issue and was
  removed by copying mingw headers into `S:\apps\check\mingw-include` (not counted
  as a test failure).
