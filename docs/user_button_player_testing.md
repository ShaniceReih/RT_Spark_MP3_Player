# Stage 6: dedicated USER_BUTTON stop/replay

The user physically verified the PA0 smoke test and authorized this player
integration. `rtos_stage6_user_button` inherits `rtos_stage5_purple_ui` and
adds only `RTOS_STAGE6_USER_BUTTON=1`. All older environments remain available.
This stage adds stop/replay; it does not add a software confirmation timer.

## Wiring and input ownership

PA0 / P2 pin 12 connects through the normally-open pushbutton to GND / P2 pin 6.
PA0 uses `GPIO_MODE_INPUT`, `GPIO_PULLUP` and active-low input. The internal
pull-up remains the physically verified wiring: HIGH released, LOW pressed.

Startup seeds the input before the existing LED/audio startup delays. A button
held at boot produces no action and must be released before a normal press.
The unchanged `user_button_debounce.h` qualifies levels for 25 ms, with the
existing control task's approximately 20 ms polling cadence. Held levels do
not repeatedly trigger actions.

`PollButtonsTask` exclusively polls PA0 and applies its transport actions.
The existing three tasks, priorities and stacks are unchanged:

| Task | Priority | Stack words |
| --- | ---: | ---: |
| `PollButtonsTask` | 3 | 1,024 |
| `AdjustVolumeTask` | 2 | 768 |
| `UpdateLcdLedsTask` | 1 | 1,024 |

There is no fourth application task, button ISR or EXTI. Runtime LCD/LED writes
remain exclusively in `UpdateLcdLedsTask`, under the existing LCD mutex where
appropriate. PF6 processing and codec volume writes remain in the volume task.

## Approved behavior

| Current situation | Qualified USER_BUTTON press |
| --- | --- |
| Playing | Safe stop, retain confirmed identity, publish Idle; LEDs off |
| Paused | Stop without resuming, retain identity, publish Idle |
| Idle with previous confirmed song | Start that same song from the beginning; Playing/BLUE |
| Idle with no previous confirmed song | Ignore; never default to song 000 |
| Natural completion | Retain identity; next press replays the completed song |
| Captured / release-all / confirmation | Consume the input without a player action |

Long-UP continues to pause/resume at the existing position. Selection takes
priority over USER_BUTTON, including input captured or ended in the same cycle.
Any raw UP/DOWN/LEFT/RIGHT input also reserves priority before its selection
debounce resolves; a simultaneous long-UP event wins too. A raw USER_BUTTON
press observed during that priority is consumed immediately, including presses
that would qualify only after selection ends. It requires the same verified
filter's stable release before a later press can act.

The new policy wraps the verified debounce helper; it does not implement a
second debounce algorithm. Consuming a selection gesture seeds that filter as
already held once, so its normal RELEASED transition rearms the input.

## Audio and state publication

`UserButtonPlayer_Apply` operates only on the control task's private outgoing
snapshot and calls the existing `Audio_StopSong` / `Audio_StartSong` APIs. The
start path resets synthesis to note zero and prefills both DMA halves. No
private audio indices are changed by the player integration.

The existing `hasCurrentSong` and `currentSong` data retain the last confirmed
identity. No public state fields, event kinds, mutexes or parallel UI state
were added. The existing PresentationCancelled event dismisses old transient
views; the normal snapshot publication and presentation observer supply Idle,
Playing and the physical LED state.

Audio calls run without the state mutex held and retain the existing short
codec mutex transactions. A stop cleanup failure remains visible as an audio
error for the existing control-loop error path. A failed replay returns Idle,
records the existing PlaybackError event and retains the replayable identity.
DMA callbacks, IRQ priority 5, SysTick and all kernel configuration are unchanged.

UART prints `USER_BUTTON: STOP` or `USER_BUTTON: REPLAY` once per action.
Replay also prints the existing song name/index. Startup instructions distinguish
long-UP Pause/Resume from USER_BUTTON Stop/Replay from beginning.

## Commands

Run from the project root in PowerShell:

```powershell
$playerPio = 'C:\Users\tanqu\.platformio\penv\Scripts\platformio.exe'
& $playerPio run -e rtos_stage6_user_button
& $playerPio run -e rtos_stage6_user_button -t upload
& $playerPio device monitor -e rtos_stage6_user_button --baud 115200
```

Close the monitor before uploading. Reset after opening the monitor to capture
startup. The coding agent does not upload firmware for this stage.

Rollback to the verified purple player:

```powershell
& $playerPio run -e rtos_stage5_purple_ui -t upload
& $playerPio device monitor -e rtos_stage5_purple_ui --baud 115200
```

The `user_button_smoke` environment remains available for electrical diagnosis.
Native policy/transport commands are documented in `tests/README.md`.

## Build and host verification passed

All nine environments build without warnings or errors: Stage 6, purple Stage 5,
USER_BUTTON smoke, bare-metal recovery and RTOS stages 1 through 5. All eight
recovery BIN SHA256 hashes match the committed pre-integration HEAD, rebuilt
in an isolated temporary checkout. The original purple and smoke BIN/ELF hashes
also remain byte-for-byte identical to their saved pre-edit artifacts.

| Firmware | PlatformIO Flash bytes | Static RAM bytes |
| --- | ---: | ---: |
| Verified purple Stage 5 | 47,344 | 48,392 |
| `rtos_stage6_user_button` | 48,256 | 48,404 |
| Integration delta | **+912** | **+12** |

All 14 host suites pass with warnings treated as errors: 12 existing suites
plus the two new suites. USER_BUTTON policy covers 21 groups / 640 checks;
actual action-handler/audio/presentation integration covers 14 groups /
11,369 checks. The unchanged physical-smoke debounce and purple UI suites
pass as well. Host fixtures verify state and drawing decisions, not real switch
noise, scheduling latency or audible continuity.

The final source audit finds 71 protected original files unchanged. The Stage-6
ELF has exactly the existing three application task entries and one each
SysTick, SVC, PendSV and DMA1 Stream5 handler. Priorities, stack allocations,
mutex implementations, verified debounce, GPIO mappings, audio/ADC/selection
drivers and purple rendering are unchanged. Physical stack high-water values
must be measured on the board; unchanged allocation sizes do not establish them.

Exactly nine files are changed or added:

| File | Change |
| --- | --- |
| `platformio.ini` | Append the Stage-6 environment |
| `src/main.cpp` | Stage-6 guarded GPIO initialization, control polling/action and UART instructions |
| `tests/README.md` | Add native policy/transport verification commands |
| `include/user_button_policy.h` | New policy wrapping the unchanged debounce helper |
| `src/user_button_player.h` | New control-task action API |
| `src/user_button_player.cpp` | New guarded safe transport/private snapshot action handler |
| `tests/user_button_policy_host.cpp` | New policy/controller regression fixture |
| `tests/user_button_audio_host.cpp` | New real audio/codec/presentation integration fixture |
| `docs/user_button_player_testing.md` | This evidence, commands and board checklist |

No firmware was uploaded. Physical STOP/REPLAY verification remains pending.

## Physical STOP/REPLAY verification pending

1. Boot with no confirmed song and press USER_BUTTON: remain Idle, no default
   song and no STOP/REPLAY action. Hold it: no repeated action.
2. Select a song normally. During playback, press USER_BUTTON: audio stops,
   BLUE turns off and the normal idle screen returns. Hold: no automatic replay.
3. Release and press again: the same song restarts from its first note with
   normal Now Playing and BLUE. Repeat on all eight song identities.
4. Long-UP pause/resume several times: position is preserved. While paused,
   USER_BUTTON stops; its next separate press replays from the beginning.
5. Let a song finish naturally. USER_BUTTON must replay that completed song.
6. Press/hold USER_BUTTON during capture, release-all and confirmation. The
   background audio, frozen candidate and five-second deadline stay unchanged.
   After confirmation/timeout, a held USER_BUTTON still does nothing. Release
   stably and press again to act.
7. Test simultaneous selection/USER_BUTTON and a press begun just before
   confirmation timeout: selection wins and no delayed action fires afterward.
8. Hold USER_BUTTON while resetting. No boot STOP/REPLAY; release, confirm a
   song normally, then a later fresh press works.
9. Verify unchanged 000-111 selection, timeout notices, PF6 volume while playing
   and paused, purple UI/overlay, external RGB colors and audio continuity.
10. Capture multiple RTOS HEALTH reports during mixed use: stable heap,
    positive stack headroom, advancing task counters, no freeze or HardFault.

Stop after build/host verification and wait for this physical test. Do not
change the verified debounce, purple rendering or other player behavior.
