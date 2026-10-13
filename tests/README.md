# Native audio DMA verification

The harness includes the real src/audio_codec.cpp and actual song definitions.
The stm32f4xx_hal.h fixture simulates only HAL calls and DMA transport. PlatformIO
does not compile this directory into the firmware.

Run from the project root with a native C++17 compiler (PowerShell):

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -Wno-missing-field-initializers -I tests -I include tests/audio_dma_host.cpp -o "$env:TEMP\rt_spark_audio_dma_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_audio_dma_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Assertions cover stereo PCM and note gaps, short final tails, exact half/ring EOF,
pause/resume cursor preservation, restarting while paused, all 72 Fur Elise score
events, late callbacks, asynchronous DMA faults during codec unmute, and failed
start/pause/resume/cleanup operations. GPIO/I2C and blocking transport calls fail
an assertion if invoked from a simulated audio IRQ.

The 20 ms minimum duration is 882 frames, so a whole song shorter than the
512-frame half is unreachable. The short-rest fixture covers its 370-frame final
tail. This simulation does not measure real IRQ deadlines, I2S clock behavior,
codec acknowledgements, or audible clicks; those require the RT-Spark board.

# Native song-selection verification

This harness exercises the real `include/song_selection.h` controller with
millisecond input samples and no HAL fixtures. Run from the project root:

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -I include tests/song_selection_host.cpp -o "$env:TEMP\rt_spark_song_selection_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_song_selection_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Assertions cover all eight song indices from idle and the shared playing/paused
input path, frozen candidates, staggered release of every button, confirmation
holds, short song-1 selection versus long pause/resume, debounce, deadline
boundaries and tick wrap, and suppression of volume changes in selection chords.
The unchanged controller still emits legacy standalone `volumeSteps` events;
the current player ignores those events, so LEFT/RIGHT cannot adjust volume.
Physical switch timing, LCD output, UART messages and audible playback still need
the board checks in the project documentation.

# Native player UI verification

This harness compiles the actual `src/player_ui.cpp` separately and uses the
actual song metadata. Fake LCD functions record text/rectangle calls and reject
unsupported glyphs, zero-area rectangles and drawing outside the 240x240 display.
Run from the project root:

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -I src -I include tests/player_ui_host.cpp src/player_ui.cpp -o "$env:TEMP\rt_spark_player_ui_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_player_ui_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Checks cover all eight real titles/composers and binary boxes, title wrapping and
font fallback, frozen captured selection projected from the real input controller,
release gating, exact countdown boundaries and tick wrap, controller-owned expiry,
overlay timing/reset/interruption, actual supplied volume values and bar widths,
unchanged-snapshot suppression and partial updates for preview, countdown, volume,
pause/resume and overlay state labels. The fake LCD checks API geometry rather than
pixel-level font rendering; display flicker, render duration, bus behavior, codec
acknowledgements and audible playback require the RT-Spark board.

# Native potentiometer-filter verification

This harness exercises the real `include/potentiometer_filter.h` independently
of ADC hardware. Run from the project root:

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -I include tests/potentiometer_volume_host.cpp -o "$env:TEMP\rt_spark_potentiometer_volume_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_potentiometer_volume_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Checks cover startup averaging without zero padding, first low/high readings,
four-sample history eviction, rounded 12-bit mapping, clamping before narrowing,
noise around percentage boundaries, the two-point accepted-volume deadband,
true 0/100 endpoints from adjacent accepted values, response within four valid
samples, retained state when failed conversions are omitted, and monotonic full
knob sweeps. `addSample()` reports only an accepted logical-volume change; an
initial valid zero reading establishes validity without emitting a change.

This verifies the filter contract, rather than HAL fault handling or actual PF6
conversion results. ADC wiring, measured minimum/maximum, stationary-knob noise,
codec/LCD agreement and audio continuity require the physical checks in
`docs/potentiometer_volume_testing.md`.

# Native potentiometer ADC verification

This harness compiles the actual `src/potentiometer_volume.cpp` through the test
and substitutes a separate ADC HAL fixture. It does not replace the existing
audio HAL fixture or change the target build. Run from the project root:

    & 'C:\mingw32\bin\g++.exe' -std=c++11 -Wall -Wextra -Werror -pedantic -I tests/adc_stubs -I src -I include tests/potentiometer_adc_host.cpp -o "$env:TEMP\rt_spark_potentiometer_adc_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_potentiometer_adc_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

The 15 scenarios cover the PF6/ADC3/channel-4 setup, no ADC DMA, every checked
HAL failure, stopping after failed start/poll, stop-failure recovery, preservation
of accepted volume and filter history, one-second initialization retries, 20 ms
sampling, forced startup conversions, tick wrap, startup zero without valid
samples, later recovery and raw extrema from successful conversions only.
This verifies acquisition control flow; physical ADC/noise/timing behavior and
audible output require the board guide.

# Native player-state snapshot verification

This harness compiles the actual Stage-3 state store and public event helpers.
Its separate FreeRTOS fixture probes mutex boundaries and substitutes scheduler,
task identity and interrupt context. Run from the project root:

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -Werror -pedantic -DRTOS_STAGE3_PLAYER=1 -I tests/state_stubs -I include tests/player_state_host.cpp src/player_state.cpp -o "$env:TEMP\rt_spark_player_state_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_player_state_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Nine groups cover initial volume/idle state, delayed-reader event retention,
cancellation/volume ordering and overlay eligibility, independent snapshot
copies, complete values at mutex boundaries, owner/ISR/context rejection,
the real controller's same-update capture/confirmation-ready behavior,
completion while selecting followed by timeout, and event/tick wrap.
The fixture verifies the store's publication contract; actual kernel scheduling,
interrupt latency, stack headroom and board behavior require the Stage-3 checks
in `docs/freertos_testing.md`.

# Native Stage-4 state-merge verification

The Stage-4 store uses the existing scheduler/mutex fixture to check its data
contract, rather than simulate actual kernel mutex scheduling. Run:

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -Werror -pedantic -DRTOS_STAGE4_PLAYER=1 -I tests/state_stubs -I include tests/player_state_stage4_host.cpp src/player_state_stage4.cpp -o "$env:TEMP\rt_spark_player_state_stage4_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_player_state_stage4_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Checks cover newer volume surviving stale control publication, independent
control/volume updates, accepted-value revisions, first-valid zero without a
false event, deadband filtering, invalid-value retention, 0-100 bounds, coherent
copies, canonical event ordering, caller ownership, event wrap and timeout-notice
dismissal without modifying volume or its events.

# Native Stage-4 codec-lock coverage

This harness includes the actual audio driver and codec synchronization module:

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic -DRTOS_STAGE4_PLAYER=1 -I tests/state_stubs -I tests -I src -I include tests/codec_control_host.cpp -o "$env:TEMP\rt_spark_codec_control_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_codec_control_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Checks cover prescheduler no-op locks, creation/context rejection, balanced
mute/unmute transactions and NACK paths, unlocked song-buffer preparation,
the atomic two-register volume pair, actual DMA refill while that lock is held,
and pause/resume/stop/EOF/error-service coverage. Native fixtures validate which
calls are protected; priority inheritance, timing and audible continuity require
the Stage-4 board checklist.

# Native Stage-5 presentation decisions

This pure observer uses public snapshots and immutable song metadata; it does
not execute STM32 LCD hardware or mutate controller state.

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -Werror -pedantic -DRTOS_STAGE4_PLAYER=1 -I src -I include tests/player_presentation_host.cpp src/player_presentation.cpp -o "$env:TEMP\rt_spark_player_presentation_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_player_presentation_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Checks cover playing/paused/selection projections, external LED priority, frozen
selection versus live preview, timeout backgrounds, once-only volume revisions,
overlay expiry/restoration, ordered retained events, delayed/superseded notices,
original confirmation timestamps, timestamp/revision wrap, unchanged input and
confirmation recovery from the public phase/start payload. Existing rendering
and low-level hardware remain covered by their earlier suites and board tests.

# Native Stage-5 bounded formatting guard

This harness checks the actual wrapper's output and scheduler-call contract.
It does not model a real kernel scheduler or hardware interrupt timing.

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -Werror -pedantic -DRTOS_STAGE5_PLAYER=1 -I tests/state_stubs tests/format_guard_host.cpp src/format_guard.cpp -o "$env:TEMP\rt_spark_format_guard_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_format_guard_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Checks cover pre-scheduler bypass, balanced and nested scheduler suspension,
resume/yield behavior, unchanged integer/string formats, truncation/return value,
buffer boundaries, size-zero queries and rejection of ISR formatting. Stage 5
alone wraps snprintf at link time; the existing PlayerUI and kernel/newlib
configuration are unchanged, and audio interrupts stay enabled during formatting.

# Native purple PlayerUI verification

This presentation-only fixture compiles the purple renderer with the same
PlayerUI API and actual song metadata. The legacy renderer is also supplied to
the command so its theme guard is checked; existing UI tests still use the
legacy renderer without the theme flag.

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -Werror -pedantic -DPLAYER_UI_PURPLE_THEME=1 -I src -I include tests/player_ui_purple_host.cpp src/player_ui.cpp src/player_ui_purple.cpp -o "$env:TEMP\rt_spark_player_ui_purple_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_player_ui_purple_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Checks cover all eight titles/composers and binary cards, bounded text and icon
geometry, long-title fallback, startup status duration, the real controller's
release gate and confirmation deadline, countdown and overlay tick wrap,
accepted-volume bar widths including 0 and 100, screen restoration and partial
updates without repeated full-screen clearing. The fixture checks LCD calls;
actual color/readability, flicker, draw time, audio continuity and RTOS headroom
require the board checklist in `docs/purple_ui_testing.md`.

# Native Stage-6 USER_BUTTON verification

The policy fixture combines the unchanged debounce and selection controller
with the new USER_BUTTON suppression policy. Run:

    & 'C:\mingw32\bin\g++.exe' -std=c++11 -O0 -g -Wall -Wextra -Werror -pedantic -I include tests/user_button_policy_host.cpp -o "$env:TEMP\rt_spark_user_button_policy_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_user_button_policy_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

It covers qualified stop/replay decisions, idle without history, release-only
rearming, all selection phases and terminal cycles, simultaneous controls,
presses qualified after selection ends, unchanged long-UP and boot-held behavior,
bounce and tick wrap. It does not emulate physical switch noise.

The transport fixture includes the actual Stage-6 action handler, audio engine
and codec synchronization module, and compiles the actual presentation observer:

    & 'C:\mingw32\bin\g++.exe' -std=c++17 -O0 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic -DRTOS_STAGE4_PLAYER=1 -DRTOS_STAGE6_USER_BUTTON=1 -I tests/state_stubs -I tests -I src -I include tests/user_button_audio_host.cpp src/player_presentation.cpp -o "$env:TEMP\rt_spark_user_button_audio_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$env:TEMP\rt_spark_user_button_audio_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

Checks cover playing/paused STOP to Idle, retained song identity, no-history
ignore, fresh first-note PCM for all eight songs, replay after natural completion,
unchanged pause/resume cursors, safe transport/codec failures, cancellation of old
overlays and normal Idle/all-off versus Playing/BLUE presentation. The source
fixture includes `user_button_player.cpp` directly; do not compile it again in
this host command. Board checks remain in `docs/user_button_player_testing.md`.
