# Isolated USER_BUTTON hardware smoke test

`user_button_smoke` is a standalone hardware test. It inherits the board,
STM32Cube and ST-Link settings from `black_f407zg`, but its source filter builds
only `src/user_button_smoke.cpp`. It does not link the player or FreeRTOS.
Existing player environments and their source files remain unchanged.

## Wiring and initialization

```text
PA0 / P2 pin 12 -- normally-open pushbutton -- GND / P2 pin 6
```

The test enables the GPIOA clock and configures only PA0 as `GPIO_MODE_INPUT`
with `GPIO_PULLUP`. It uses the STM32 internal pull-up; an external pull-up is
not required for this test. Released is HIGH and pressed is LOW.

UART1 uses the existing PA9/PA10 mapping at 115200 baud, 8N1. The test initializes
HAL, UART1 and PA0 only. It uses the usual HAL SysTick for timing and never
configures a button interrupt or EXTI. No application task is created.

## Debounce and boot-held behavior

The loop calls `HAL_Delay(20)` between samples. A new raw level must persist
for at least 25 ms after its first observed sample before becoming stable.
Every raw change resets that window. At the 20 ms sampling cadence this
normally accepts the change on the second subsequent poll, about 40 ms after
its first observation. Unsigned elapsed-time subtraction handles tick wrap.

Only accepted transitions print `USER_BUTTON: PRESSED` or
`USER_BUTTON: RELEASED`. Holding either level produces no repeated messages.
There is no player action associated with either event.

After a brief pull-up settling delay, the initial GPIO reading seeds both the
stable and candidate states before printing the banner. Boot does not emit a
transition. If the button is LOW at boot, the diagnostic reports that it is
held; a debounced HIGH release must occur before a later LOW can emit PRESSED.

## Build, upload, monitor and recovery

Run from the project root in PowerShell:

```powershell
$buttonPio = 'C:\Users\tanqu\.platformio\penv\Scripts\platformio.exe'
& $buttonPio run -e user_button_smoke
& $buttonPio run -e user_button_smoke -t upload
& $buttonPio device monitor -e user_button_smoke --baud 115200
```

Close an existing serial monitor before uploading. Open the monitor and reset
the board to capture startup. A released boot prints:

```text
USER_BUTTON SMOKE TEST
PA0 configured INPUT_PULLUP
Released level: HIGH
Pressed level: LOW
BOOT: BUTTON RELEASED
```

Subsequent physical presses/releases should print one line per stable edge:

```text
USER_BUTTON: PRESSED
USER_BUTTON: RELEASED
```

A held boot instead prints `BOOT: BUTTON HELD - RELEASE BEFORE NEXT PRESS`,
with no initial `USER_BUTTON: PRESSED` line.

To return to the verified purple player:

```powershell
& $buttonPio run -e rtos_stage5_purple_ui -t upload
& $buttonPio device monitor -e rtos_stage5_purple_ui --baud 115200
```

## Host verification

```powershell
& 'C:\mingw32\bin\g++.exe' -std=c++11 -Wall -Wextra -Werror -pedantic -I include tests/user_button_smoke_host.cpp -o "$env:TEMP\rt_spark_user_button_smoke_host.exe"
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
& "$env:TEMP\rt_spark_user_button_smoke_host.exe"
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
```

The host fixture exercises quiet startup/holds, stable press/release,
24/25 ms boundaries, 20 ms polling, press/release bounce, boot-held rearming,
tick wrap and independent filter instances. Physical wiring and switch behavior
still require the board checks below.

## Build verification passed

Both requested environments build without warnings or errors:

| Environment | Flash bytes | Static RAM bytes |
| --- | ---: | ---: |
| `user_button_smoke` | 3,348 | 116 |
| `rtos_stage5_purple_ui` | 47,344 | 48,392 |

The rebuilt purple BIN and ELF SHA256 hashes both match their pre-test hashes
exactly. All 43 original source/header/build-script files are unchanged; the
existing environment definitions are unchanged. The native debounce fixture
passes 9 groups / 328 checks under C++11 with warnings treated as errors.

Only `platformio.ini` was modified, by appending the smoke environment. Four
files were added: `include/user_button_debounce.h`, `src/user_button_smoke.cpp`,
`tests/user_button_smoke_host.cpp` and this guide. No firmware was uploaded.

## Physical verification pending

1. Boot with the button released. Confirm the banner and released boot status,
   then leave it untouched for ten seconds: no transition messages.
2. Press and hold for several seconds: exactly one PRESSED. Release and wait:
   exactly one RELEASED, with no repeated idle messages.
3. Repeat ten deliberate presses/releases. Confirm one event per edge and no
   extra events from switch bounce.
4. Hold the button while resetting the board. Confirm the held boot status and
   no PRESSED event while still held. Release: one RELEASED. Press again:
   one normal PRESSED. Release: one RELEASED.
5. Restore `rtos_stage5_purple_ui` and confirm the verified player still works.

STOP/REPLAY remains unimplemented. Wait for physical button verification and
explicit authorization before adding any player behavior.
