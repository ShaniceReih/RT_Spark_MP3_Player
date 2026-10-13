# RT-Spark Personal MP3 Player

A real-time embedded music player for the **RT-Spark STM32F407ZG** board, built with **FreeRTOS**, STM32Cube and PlatformIO. The final firmware uses the ES8388 audio codec, onboard LCD, four selection buttons, a dedicated USER_BUTTON, potentiometer volume control and external RGB LEDs.

The current, physically verified submission firmware is **`rtos_stage6_user_button`**. It plays eight songs from synthesized note/beat data; it does not decode MP3 files.

## Features

- Exactly three FreeRTOS application tasks
- Eight-song binary selection with capture, release-all gating and a five-second confirmation window
- Continuous synthesized stereo PCM playback through I2S3 and audio DMA
- ES8388 audio codec
- Dedicated PA0 USER_BUTTON for Stop / Replay from beginning
- Long-UP Pause / Resume from the retained playback position
- Potentiometer-controlled volume with filtering and deadband
- Onboard 240 x 240 ST7789 LCD with a purple/lavender music-player UI
- Song selection, confirmation, playing, paused and timeout screens
- Volume overlay and volume bar
- External RGB playback indicators
- Runtime task, stack and heap monitoring
- Host unit and regression tests

## FreeRTOS Architecture

The final application has exactly three application tasks. USER_BUTTON belongs to `PollButtonsTask`; it does not add a fourth task.

| Task | Priority | Stack | Responsibility |
| --- | ---: | ---: | --- |
| `PollButtonsTask` | 3 | 1,024 words | Selection buttons, USER_BUTTON, song selection, player state, audio transport/service and state publication |
| `AdjustVolumeTask` | 2 | 768 words | PF6 ADC sampling, filtering, accepted volume and codec volume writes |
| `UpdateLcdLedsTask` | 1 | 1,024 words | All runtime LCD/PlayerUI rendering and external RGB LED updates |

Audio DMA callbacks remain interrupt-driven. They do not call FreeRTOS APIs, and DMA1 Stream5 remains at interrupt priority 5.

### Synchronization

Separate mutexes protect player-state publication, ES8388 codec access and LCD access. The UI task copies a consistent player snapshot, releases the state mutex, and then renders under the LCD mutex. The existing five-second confirmation timeout remains in the player controller; it uses no FreeRTOS software timer.

## System Architecture

```text
                     +----------------------+
                     |       FreeRTOS       |
                     +----------+-----------+
                                |
             +------------------+------------------+
             |                  |                  |
             v                  v                  v
   +------------------+ +------------------+ +----------------------+
   | PollButtonsTask  | | AdjustVolumeTask | | UpdateLcdLedsTask    |
   | Priority 3       | | Priority 2       | | Priority 1           |
   +--------+---------+ +--------+---------+ +----------+-----------+
            |                    |                      |
            v                    v                      v
   Selection + USER_BUTTON  PF6 / ADC3              LCD + RGB LEDs
   Song control             Volume filter           Player interface
   Player state             ES8388 volume           Status display
            |
            v
      Audio transport
            |
            v
      I2S3 + DMA
            |
            v
         ES8388
            |
            v
       Audio output
```

## Hardware

### Main Board

- RT-Spark with STM32F407ZGT6, ARM Cortex-M4
- Onboard 240 x 240 ST7789 LCD
- ES8388 audio codec

### Additional Components

- 10 kOhm potentiometer
- External red, green and blue LEDs with current-limiting resistors
- Speaker / headphones
- Normally-open external USER_BUTTON

### USER_BUTTON Wiring

```text
PA0 / P2 pin 12 ---- normally-open pushbutton ---- GND / P2 pin 6
```

PA0 is configured as `GPIO_MODE_INPUT` with `GPIO_PULLUP`. The STM32 internal pull-up holds the released button HIGH; pressing the button connects PA0 to GND and reads LOW. No external pull-up resistor is required.

The control task polls at approximately 20 ms intervals and uses 25 ms debounce. A held button produces one action. If held at boot, it produces no new press action and must be released before a later press can act. USER_BUTTON uses polling only, with no EXTI or button interrupt.

## Pin Mapping

### Pushbuttons

| Control | STM32 pin / connector | Function |
| --- | --- | --- |
| UP | PC5 | Capture / confirm; long-UP Pause / Resume |
| DOWN | PC1 | Selection bit 2 |
| LEFT | PC0 | Selection bit 1 |
| RIGHT | PC4 | Selection bit 0 |
| USER_BUTTON | PA0 / P2 pin 12 | Active LOW, internal pull-up; Stop / Replay from beginning |

### Potentiometer

| Function | Pin / peripheral |
| --- | --- |
| Volume input | PF6 |
| ADC | ADC3 Channel 4 |
| Resolution | 12 bits, raw values 0-4095 |

### External RGB LEDs

| Color | STM32 pin |
| --- | --- |
| RED | PA8 |
| GREEN | PA1 |
| BLUE | PB14 |

| Player state | LED indication |
| --- | --- |
| Playing | BLUE on |
| Paused | RED on |
| Selecting / confirming | GREEN on |
| Idle | All off |

### Audio and UART

| Peripheral / signal | STM32 pin |
| --- | --- |
| ES8388 software I2C SCL | PF1 |
| ES8388 software I2C SDA | PF0 |
| I2S3 WS | PA15 |
| I2S3 CK | PB3 |
| I2S3 SD | PB5 |
| I2S3 MCLK | PC7 |
| UART1 TX | PA9 |
| UART1 RX | PA10 |

Audio uses **DMA1 Stream5, Channel 0**. The onboard LCD uses its existing FSMC interface.

## Song Selection

DOWN, LEFT and RIGHT form a three-bit binary song number. UP captures and confirms it.

| Binary | Song |
| --- | --- |
| 000 | Fur Elise - Beethoven |
| 001 | Canon In D - Pachebelbel |
| 010 | Minuet in G major - Bach |
| 011 | Turkish March - Mozart |
| 100 | Nocturne in E flat - Chopin |
| 101 | Waltz No. 2 - Shostakovich |
| 110 | Nocturne in C sharp - Chopin |
| 111 | Symphony No. 40 - Mozart |

### Selection Procedure

1. Set the desired binary value using DOWN, LEFT and RIGHT.
2. Press UP to capture the selection.
3. Release all selection buttons.
4. Press UP again within five seconds to confirm.
5. The selected song begins playing.

For example, LEFT + RIGHT gives `011`, Turkish March - Mozart. The captured candidate remains fixed during confirmation. If confirmation is not received within five seconds, the controller cancels the selection and displays the timeout notice.

## Playback Controls

### Long UP: Pause / Resume

Hold UP for approximately one second, then release, to toggle playback:

| Current state | Long-UP result |
| --- | --- |
| PLAYING | PAUSED |
| PAUSED | PLAYING, continuing from the retained playback position |

### Dedicated USER_BUTTON: Stop / Replay from Beginning

| Current situation | Qualified USER_BUTTON press |
| --- | --- |
| PLAYING | STOP / IDLE; retain the confirmed song |
| PAUSED | STOP / IDLE; retain the confirmed song |
| IDLE with a previously confirmed song | Replay that same song from the beginning |
| IDLE before any song has been confirmed | No action |
| After natural song completion | Replay the last confirmed song from the beginning |

Each STOP or REPLAY requires a separate press. Holding the button after STOP does not immediately replay the song; release rearms the input.

Selection has priority. USER_BUTTON is ignored/consumed while selection is active, including capture, release-all and the five-second confirmation phase. A press held during selection does not fire later when selection ends or times out. Release the button, then press again for a normal action. Simultaneous selection inputs take priority over USER_BUTTON.

**Long UP = Pause / Resume. USER_BUTTON = Stop / Replay from beginning.**

## Volume Control

Volume comes from the potentiometer on PF6 / ADC3 Channel 4. The implementation uses 12-bit ADC sampling, a four-sample filter, deadband protection, accepted-volume tracking and codec-safe volume writes. The logical volume range is 0-100%.

The physically observed potentiometer range is approximately **8-100%**. This observed range has intentionally not been recalibrated during the FreeRTOS migration or USER_BUTTON integration. Volume adjustment remains available while playing and paused.

## LCD Interface

The onboard 240 x 240 ST7789 LCD presents startup, audio-ready, idle, binary selection, captured-song/release-all, confirmation countdown, now-playing, paused and timeout screens, plus a temporary volume overlay.

The final firmware includes the purple/lavender renderer in `src/player_ui_purple.cpp`. It uses a dark-purple background, lavender accents, music notes, play/pause/speaker icons, rounded visual panels, binary selection boxes and a purple volume bar. Title wrapping, bounded text rendering and cached partial updates keep the interface readable without redrawing the full LCD every polling cycle.

`UpdateLcdLedsTask` remains the only runtime owner of the UI and RGB LED presentation.

## Audio Architecture

Songs use synthesized musical-note and beat tables. Audio runs through the following path:

```text
Song note / beat tables
        |
        v
PCM waveform generation
        |
        v
Stereo audio buffer
        |
        v
I2S3 + DMA1 Stream5
        |
        v
ES8388 codec
        |
        v
Speaker / headphones
```

DMA maintains audio output while the three application tasks handle controls, volume and presentation.

## Building the Project

Use PlatformIO from the project root. The final environment is `rtos_stage6_user_button`, and `platformio.ini` sets it as `default_envs`.

Build the final firmware using the default:

```sh
pio run
```

Build it explicitly:

```sh
pio run -e rtos_stage6_user_button
```

Upload through the configured ST-Link interface:

```sh
pio run -e rtos_stage6_user_button -t upload
```

Open the serial monitor:

```sh
pio device monitor -e rtos_stage6_user_button --baud 115200
```

Close the serial monitor before uploading. Reset the board after opening the monitor to capture startup messages, including `STAGE 6: USER_BUTTON STOP/REPLAY ACTIVE`.

### Recovery and Test Environments

All nine environments remain available:

| Environment | Purpose |
| --- | --- |
| `black_f407zg` | Original bare-metal player recovery |
| `rtos_stage1_smoke` | FreeRTOS kernel / tick smoke test |
| `rtos_stage2_player` | Full player under the scheduler with three task shells |
| `rtos_stage3_player` | Control-state task separation |
| `rtos_stage4_player` | Dedicated volume task and codec synchronization |
| `rtos_stage5_player` | Completed three-task split with the original UI |
| `rtos_stage5_purple_ui` | Purple UI checkpoint before USER_BUTTON integration |
| `user_button_smoke` | Isolated PA0 hardware / debounce test |
| `rtos_stage6_user_button` | **Final verified submission firmware; default environment** |

For example, restore the purple checkpoint with:

```sh
pio run -e rtos_stage5_purple_ui -t upload
```

To build every environment in one command:

```sh
pio run -e black_f407zg -e rtos_stage1_smoke -e rtos_stage2_player -e rtos_stage3_player -e rtos_stage4_player -e rtos_stage5_player -e rtos_stage5_purple_ui -e user_button_smoke -e rtos_stage6_user_button
```

## Testing and Validation

Final validation passed:

- All **nine PlatformIO environments** build successfully.
- All **14 host test suites** pass.
- Builds and host validation have **zero warnings/errors**.
- The default and explicit Stage-6 builds produce identical firmware.
- The eight older recovery/test environments remain unchanged; their BIN files match the committed pre-cleanup firmware rebuilt in an isolated directory.

Host test commands and scope are documented in [tests/README.md](tests/README.md). The suites cover audio DMA, selection, legacy and purple UI rendering, potentiometer filtering/ADC, player state, codec synchronization, presentation, formatting, USER_BUTTON debounce, policy and audio transport integration.

### Physical Verification

The final Stage-6 USER_BUTTON firmware passed physical testing on the RT-Spark board:

- PA0 press/release detection, debounce and boot-held suppression
- PLAYING -> STOP and PAUSED -> STOP
- Replay of the retained song from the beginning
- Natural song completion followed by replay
- Repeated STOP / REPLAY cycles
- USER_BUTTON suppression during active selection
- Held presses consumed during selection and still suppressed after timeout
- Stable release rearming the button
- Unchanged long-UP Pause / Resume
- Stable RTOS heap and task stack headroom

Broader physical verification also passed: all eight binary song selections, capture/release-all/confirmation, the five-second timeout, potentiometer volume while playing and paused, LCD screens and overlay, RGB LED transitions, song completion and continuous audio. The three tasks remained active with stable heap/stacks and no observed scheduler freeze, HardFault or reboot loop.

The development-stage checklists remain in [docs/user_button_smoke_testing.md](docs/user_button_smoke_testing.md), [docs/user_button_player_testing.md](docs/user_button_player_testing.md), [docs/purple_ui_testing.md](docs/purple_ui_testing.md) and [docs/freertos_testing.md](docs/freertos_testing.md). Those guides retain their stage-specific history; the completed final status is recorded here.

### Final Resource Usage

| Firmware | Flash | Static RAM |
| --- | ---: | ---: |
| **Final `rtos_stage6_user_button`** | **48,384 bytes** | **48,408 bytes** |
| Historical `rtos_stage5_purple_ui` checkpoint | 47,344 bytes | 48,392 bytes |

### Historical Stage-5 Runtime Sample

During Stage-5 physical testing, the tasks had positive stack headroom and stable heap usage. These sample values are historical measurements, not new Stage-6 measurements:

| Diagnostic | Approximate value |
| --- | ---: |
| BUTTON_STACK | 570 words free |
| VOLUME_STACK | 715 words free |
| LCD_STACK | 670 words free |
| HEAP | 19,816 bytes free |
| MIN_HEAP | 19,816 bytes free |

Stage-6 physical testing likewise confirmed stable heap and stacks without introducing a fourth application task.

## Project Structure

```text
RT_Spark_MP3_Player_ButtonVolume/
|-- include/
|   |-- FreeRTOSConfig.h
|   |-- song.h, song_def.h, song_selection.h
|   |-- player_state.h, player_presentation.h, potentiometer_filter.h
|   `-- user_button_debounce.h, user_button_policy.h
|-- src/
|   |-- main.cpp
|   |-- audio_codec.cpp / .h
|   |-- player_tasks*.cpp / .h, player_state*.cpp
|   |-- codec_control.cpp / .h, lcd_control.cpp / .h
|   |-- lcd_display.cpp / .h, player_ui.cpp, player_ui_purple.cpp
|   |-- player_presentation.cpp, player_leds.cpp / .h
|   |-- potentiometer_volume.cpp / .h
|   |-- user_button_player.cpp, user_button_player.h
|   `-- user_button_smoke.cpp
|-- tests/
|   |-- audio, selection, state, UI, volume and synchronization host tests
|   |-- user_button_smoke_host.cpp
|   |-- user_button_policy_host.cpp, user_button_audio_host.cpp
|   `-- README.md
|-- docs/
|   |-- FreeRTOS migration and hardware testing guides
|   |-- purple_ui_testing.md
|   |-- user_button_smoke_testing.md
|   `-- user_button_player_testing.md
|-- scripts/
|-- platformio.ini
`-- README.md
```

## Development History

The player was built and physically verified before an incremental FreeRTOS migration.

```text
Working embedded player
        |
        v
FreeRTOS kernel integration
        |
        v
Player moved under scheduler
        |
        v
Button/state task separation
        |
        v
Dedicated volume task
        |
        v
Dedicated LCD/LED task
        |
        v
Purple music-player UI
        |
        v
Dedicated USER_BUTTON hardware smoke test
        |
        v
Stage-6 stop/replay integration
        |
        v
Physical Stage-6 verification
```

The Git tag **`stage5-verified`** preserves the historical Stage-5 checkpoint. Stage 6 is the current final firmware.

## Current Status

| Item | Status |
| --- | --- |
| FreeRTOS integration | COMPLETE |
| Three application tasks | COMPLETE |
| Eight-song selection | COMPLETE |
| Audio playback | COMPLETE |
| Potentiometer volume | COMPLETE |
| LCD interface | COMPLETE |
| RGB LED indicators | COMPLETE |
| Purple PlayerUI | COMPLETE |
| Dedicated USER_BUTTON | COMPLETE |
| Stop / replay behavior | COMPLETE |
| Physical Stage-6 tests | PASSED |
| Host regression tests | PASSED |


## Laboratory Report

- [Laboratory Activity 2 - Personal MP3 Player Report](docs/Laboratory_Activity_2_MP3_Player_Report.pdf)



## Development Environment

- Visual Studio Code
- PlatformIO
- STM32Cube
- FreeRTOS
- GCC ARM Embedded Toolchain
- RT-Spark STM32F407ZG

## Author

Developed as part of the BCA182 Embedded Systems Programming laboratory activities.
