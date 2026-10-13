# Stage-6 PA0 diagnostic build

The physical Stage-6 STOP test failed: song 000 played, but pressing the
independently verified PA0 button produced no USER_BUTTON action message.
The observed 58/60/58 volume messages are accepted ADC changes and are separate
evidence. This diagnostic work leaves the ADC, overlay and purple UI unchanged.

## Source and compiled-firmware audit

| Requested point | Finding |
| --- | --- |
| Integrated initialization called | Stage-6 guarded `main()` calls `UserButton_Init()` before LED/LCD/ADC/audio startup; the original ELF confirms the call and initialization code |
| GPIOA clock | `__HAL_RCC_GPIOA_CLK_ENABLE()` is called |
| PA0 configuration | Pin 0, `GPIO_MODE_INPUT`, `GPIO_PULLUP`; the original compiled code passes mode 0 and pull 1 |
| Later reconfiguration | None found; later GPIOA initialization selects PA1/PA8 for LEDs and PA15 for I2S, preserving PA0; UART PA9/PA10 is initialized earlier |
| Runtime read | Exactly `HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)` in the PollButtonsTask-owned control loop |
| Polarity | `GPIO_PIN_RESET` means pressed; SET means released |
| Initial filter state | Seeded from the actual PA0 read after the verified 1 ms pull-up settling delay |
| Filter lifetime | Global static `UserButtonPolicy userButtonInput` embeds one persistent `UserButtonDebounce`; it is not recreated per iteration |
| Update cadence | The policy calls that same filter once every normal control iteration, followed by the existing 20 ms RTOS delay |
| Pressed reaches policy | The policy tests confirm qualified Pressed produces Stop in normal Playing/Paused; diagnostics now expose the actual inner transition |
| Playing versus suppression | Playing only supplies `playbackActive`; suppression separately depends on selection phase, selection GPIOs and input events |

No GPIOA reset, deinitialization, clock-disable or port-wide configuration
overwrite was found. Framework MSP hooks linked into this image are empty
defaults. The usual Cube HAL GPIO initialization changes only selected pins.

The exact physical failure remains unconfirmed. One candidate path is a raw
selection input or active selection flow consuming USER_BUTTON before its press
qualifies. This intentionally seeds the existing filter as held and generates
no Pressed event until release/rearm. The added flag output identifies that
path without changing it.

## Temporary UART observations

Only Stage 6 emits these messages. Startup prints the actual initial PA0 level:

```text
USER_BUTTON INIT: PA0 INPUT_PULLUP RAW=HIGH
```

LOW is printed instead if held. Once the control task begins, after all startup
peripheral initialization, it reads the real clock/configuration registers:

```text
USER_BUTTON GPIO CHECK: CLOCK=1 MODE=0 PULL=1 RAW=HIGH
```

Expected values are GPIOA clock enabled, PA0 input mode 0 and pull-up 1. This
check reads registers; it does not repair or reconfigure them.

A normal press/release during playback should produce this trace:

```text
USER_BUTTON RAW: LOW
USER_BUTTON DEBOUNCED: PRESSED
USER_BUTTON POLICY INPUT: MODE=PLAYING SELECTION=0
USER_BUTTON POLICY FLAGS: UP=0 DOWN=0 LEFT=0 RIGHT=0 FLOW=0 CONSUMED=0
USER_BUTTON POLICY: STOP
USER_BUTTON: STOP
USER_BUTTON RAW: HIGH
USER_BUTTON DEBOUNCED: RELEASED
```

`USER_BUTTON: STOP` is the existing action message. All other lines in that
trace are diagnostic additions. Raw lines print only when the sampled PA0
level changes; debounce lines print only for actual qualified transitions.
Policy context/result prints on a qualified press or once when selection
consumes a new raw press. Held/unchanged levels do not print every loop.

Policy results also include `REPLAY`, `SUPPRESSED SELECTION` and `NONE`.
Context modes include IDLE, PLAYING, PAUSED and SELECTING. `SELECTION` is the
actual combined priority decision. UP/DOWN/LEFT/RIGHT are the independently
sampled selection inputs; FLOW indicates an active selection phase before or
after the controller update, and CONSUMED reports the release-required latch.
Terminal selection/long-UP events can also set SELECTION in that cycle.

The `UserButtonPolicyTrace` object is a temporary observation output, not a
second debounce instance. It copies the transition from the existing
`button.update()` call and observes selection consumption. The optional trace
argument does not change STOP/REPLAY decisions.

## Reading the failing trace

- No RAW edge on a physical press: the integrated GPIO sample did not change;
  inspect the startup clock/mode/pull/raw values first.
- RAW LOW but no Pressed: inspect release/rearm, raw bounce or
  `SUPPRESSED SELECTION`; a consumed selection gesture does not qualify a new
  press by design.
- Pressed with SELECTION=1: the policy context/flags identify the selection
  priority that blocks the action.
- Pressed with MODE=PLAYING, SELECTION=0 and POLICY STOP: the input reached the
  transport action path. A missing subsequent existing STOP message narrows
  investigation to completion of that path.

## Validation and stop condition

Only `src/main.cpp`, `include/user_button_policy.h` and this new guide changed
for this diagnostic turn. Verified debounce, selection/controller logic,
transport handler, audio/DMA, ADC/filter, purple UI, GPIO mappings, tasks,
priorities/stacks, mutexes and PlatformIO configuration are unchanged.

`rtos_stage6_user_button` builds with zero warnings/errors, using 49,244 bytes
of PlatformIO-reported Flash and 48,408 bytes of static RAM. The diagnostic
increase over the normal Stage-6 image is 988 bytes Flash and 4 bytes RAM.

Existing policy (21 groups/640 checks), real audio integration (14 groups/
11,369 checks) and smoke debounce (9 groups/328 checks) pass. Temporary trace
tests pass 28 checks. Both trace-enabled and default policy calls match the
pre-diagnostic action sequence across 2,097,152 comparison inputs. Purple and
smoke BIN/ELF files remain unchanged.

Build from the project root:

```powershell
& 'C:\Users\tanqu\.platformio\penv\Scripts\platformio.exe' run -e rtos_stage6_user_button
```

No firmware was uploaded. Stop after the diagnostic build; use the physical
UART trace to identify the failing layer before changing player behavior.
