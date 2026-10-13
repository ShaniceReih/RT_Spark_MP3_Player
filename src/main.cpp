#include "stm32f4xx_hal.h"
#include "song_def.h"
#include "audio_codec.h"
#include "song_selection.h"
#include "lcd_display.h"
#include "player_ui.h"
#include "potentiometer_volume.h"
#include "potentiometer_filter.h"
#include "player_leds.h"
#include "rtos_support.h"
#include "player_tasks.h"

#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON
#include "user_button_player.h"
#endif

#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
#include "player_state.h"
#if defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER
#include "codec_control.h"
#if defined(RTOS_STAGE5_PLAYER) && RTOS_STAGE5_PLAYER
#include "lcd_control.h"
#endif
#endif
#endif

#if defined(RTOS_STAGE2_PLAYER) && RTOS_STAGE2_PLAYER
#include "FreeRTOS.h"
#include "task.h"
#endif

#include <cstring>
#include <cstdio>

/* =========================================================
   UART
   ========================================================= */

UART_HandleTypeDef huart1;

/* =========================================================
   SysTick
   ========================================================= */

#if !(defined(RTOS_STAGE1_SMOKE) && RTOS_STAGE1_SMOKE) && \
    !(defined(RTOS_STAGE2_PLAYER) && RTOS_STAGE2_PLAYER)
extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}
#endif

/* =========================================================
   UART
   ========================================================= */

static void UART1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&huart1);
}

static void uart_print(const char *text)
{
    HAL_UART_Transmit(
        &huart1,
        (uint8_t *)text,
        std::strlen(text),
        HAL_MAX_DELAY
    );
}

static void PlayerLeds_StartupTest()
{
    uart_print("LED TEST: RED\r\n");
    PlayerLeds_SetState(PLAYER_LED_PAUSED);
    HAL_Delay(500);
    PlayerLeds_SetState(PLAYER_LED_IDLE);

    uart_print("LED TEST: GREEN\r\n");
    PlayerLeds_SetState(PLAYER_LED_SELECTING);
    HAL_Delay(500);
    PlayerLeds_SetState(PLAYER_LED_IDLE);

    uart_print("LED TEST: BLUE\r\n");
    PlayerLeds_SetState(PLAYER_LED_PLAYING);
    HAL_Delay(500);
    PlayerLeds_SetState(PLAYER_LED_IDLE);
    uart_print("LED TEST COMPLETE\r\n");
}

/* =========================================================
   Buttons

   UP    = Button 1 = SELECT / CONFIRM
   DOWN  = Button 2 = bit 2
   LEFT  = Button 3 = bit 1
   RIGHT = Button 4 = bit 0
   ========================================================= */

static void Buttons_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin =
        GPIO_PIN_0 |
        GPIO_PIN_1 |
        GPIO_PIN_4 |
        GPIO_PIN_5;

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON
// Seed before lengthy startup diagnostics; PollButtonsTask then owns this input.
static UserButtonPolicy userButtonInput;
static bool userButtonPreviousRaw = false;

static void UserButton_Init()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_Delay(1); // Same pull-up settling delay as the verified smoke test.
    userButtonPreviousRaw = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET;
    userButtonInput.initialize(userButtonPreviousRaw, HAL_GetTick());
    uart_print(userButtonPreviousRaw
        ? "USER_BUTTON INIT: PA0 INPUT_PULLUP RAW=LOW\r\n"
        : "USER_BUTTON INIT: PA0 INPUT_PULLUP RAW=HIGH\r\n");
}
#endif

/* =========================================================
   Songs
   ========================================================= */

Song *songs[8] =
{
    &FUR_ELISE,
    &CANNON_IN_D,
    &MINUET_IN_G_MAJOR,
    &TURKISH_MARCH,
    &NOCTRUNE_IN_E_FLAT,
    &WALTZ_NO2,
    &NOCTRUNE_IN_C_SHARP_MAJOR,
    &SYMPHONY_NO40
};

/* =========================================================
   UART Song Name
   ========================================================= */

static void UART_PrintSong(uint8_t index)
{
    // The supplied title strings already contain their hyphen separators.
    // Join them with one space, preserving their actual spelling.
    const std::string &first = songs[index]->name1;
    const char *second = songs[index]->name2.c_str();
    size_t length = first.length();
    while (length > 0 && first[length - 1] == ' ') --length;
    while (*second == ' ') ++second;
    char title[80];
    std::snprintf(title, sizeof(title), "%.*s %s\r\n",
                  (int)length, first.c_str(), second);
    uart_print(title);
}

static void UART_PrintBinary(uint8_t song)
{
    char text[24];
    std::snprintf(text, sizeof(text), "Binary: %u%u%u\r\n",
                  (song >> 2) & 1, (song >> 1) & 1, song & 1);
    uart_print(text);
}

static void UART_PrintSelectionCaptured(uint8_t song)
{
    char text[160];
    std::snprintf(text, sizeof(text),
        "\r\n------------------------------\r\n"
        "SONG SELECTION CAPTURED\r\n"
        "DOWN  = %u\r\nLEFT  = %u\r\nRIGHT = %u\r\n"
        "Binary: %u%u%u\r\nSong index: %u\r\nSong number: %u\r\nSong: ",
        (song >> 2) & 1, (song >> 1) & 1, song & 1,
        (song >> 2) & 1, (song >> 1) & 1, song & 1,
        song, song + 1);
    uart_print(text);
    UART_PrintSong(song);
    uart_print("Release all buttons.\r\n"
               "Press UP again within 5 seconds after release to confirm.\r\n"
               "------------------------------\r\n");
}

static void UART_PrintPotentiometer(const PotentiometerReading &reading)
{
    char text[160];
    std::snprintf(text, sizeof(text),
        "ADC: %u / %lu  FILTERED: %u\r\n"
        "Volume: %u PCT\r\n"
        "OBSERVED MIN: %u  MAX: %u\r\n",
        static_cast<unsigned>(reading.raw),
        static_cast<unsigned long>(PotentiometerFilter::AdcMaximum),
        static_cast<unsigned>(reading.filteredRaw),
        static_cast<unsigned>(reading.volume),
        static_cast<unsigned>(reading.observedMinimum),
        static_cast<unsigned>(reading.observedMaximum));
    uart_print(text);
}

static void UART_ReportPotentiometerError(PotentiometerResult result, uint32_t now)
{
    if (result != PotentiometerResult::InitializationFailed &&
        result != PotentiometerResult::ConversionFailed) return;
    static bool reported = false;
    static uint32_t lastReport = 0;
    if (reported && now - lastReport < 5000) return;
    reported = true;
    lastReport = now;
    uart_print(result == PotentiometerResult::InitializationFailed
        ? "POTENTIOMETER INITIALIZATION FAILED - RETRYING; VOLUME RETAINED\r\n"
        : "POTENTIOMETER ADC READ FAILED - VOLUME RETAINED\r\n");
}

#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
static PlayerPlayback SnapshotPlayback(AudioPlaybackState playback)
{
    return playback == AUDIO_PLAYING ? PlayerPlayback::Playing :
           playback == AUDIO_PAUSED ? PlayerPlayback::Paused :
           playback == AUDIO_FINISHED ? PlayerPlayback::Finished :
           playback == AUDIO_ERROR ? PlayerPlayback::Error : PlayerPlayback::Idle;
}

#if defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER
static void UART_PrintPlayerSnapshot(const PlayerStateSnapshot &snapshot)
{
    const char *mode = snapshot.mode == PlayerMode::Selecting ? "SELECTING" :
                       snapshot.mode == PlayerMode::Playing ? "PLAYING" :
                       snapshot.mode == PlayerMode::Paused ? "PAUSED" : "IDLE";
    char text[192];
    std::snprintf(text, sizeof(text),
        "STATE SNAPSHOT: REV=%lu STATE=%s CURRENT_INDEX=%u VALID=%u PHASE=%u EVENTS=%lu VOL=%u VOL_REV=%lu\r\n",
        static_cast<unsigned long>(snapshot.revision), mode,
        static_cast<unsigned>(snapshot.currentSong),
        static_cast<unsigned>(snapshot.hasCurrentSong),
        static_cast<unsigned>(snapshot.phase),
        static_cast<unsigned long>(snapshot.eventSequence),
        static_cast<unsigned>(snapshot.acceptedVolume),
        static_cast<unsigned long>(snapshot.volumeRevision));
    uart_print(text);
}
#else
static void UART_PrintPlayerSnapshot(const PlayerStateSnapshot &snapshot)
{
    const char *mode = snapshot.mode == PlayerMode::Selecting ? "SELECTING" :
                       snapshot.mode == PlayerMode::Playing ? "PLAYING" :
                       snapshot.mode == PlayerMode::Paused ? "PAUSED" : "IDLE";
    char text[160];
    std::snprintf(text, sizeof(text),
        "STATE SNAPSHOT: REV=%lu STATE=%s CURRENT_INDEX=%u VALID=%u PHASE=%u EVENTS=%lu\r\n",
        static_cast<unsigned long>(snapshot.revision), mode,
        static_cast<unsigned>(snapshot.currentSong),
        static_cast<unsigned>(snapshot.hasCurrentSong),
        static_cast<unsigned>(snapshot.phase),
        static_cast<unsigned long>(snapshot.eventSequence));
    uart_print(text);
}
#endif
#endif

/* =========================================================
   MAIN
   ========================================================= */

#if defined(RTOS_STAGE2_PLAYER) && RTOS_STAGE2_PLAYER
static void RunPlayerLoop(bool codecInitialized) __attribute__((noreturn));
#if defined(RTOS_STAGE5_PLAYER) && RTOS_STAGE5_PLAYER
static void RunPlayerControlLoopStage5(bool codecInitialized) __attribute__((noreturn));
#endif
#endif

int main(void)
{
    HAL_Init();
#if defined(RTOS_STAGE2_PLAYER) && RTOS_STAGE2_PLAYER
    SystemCoreClockUpdate(); // Read the existing clock tree before RTOS use.
#endif

    UART1_Init();
#if defined(RTOS_STAGE2_PLAYER) && RTOS_STAGE2_PLAYER
    RtosSupport_PreparePlayerMode(&huart1);
#endif
#if defined(RTOS_STAGE1_SMOKE) && RTOS_STAGE1_SMOKE
    RtosSupport_StartSmokeTest(&huart1); // Isolated diagnostic; never returns.
#endif
#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON
    UserButton_Init();
#endif
    PlayerLeds_Init();
    PlayerLeds_StartupTest(); // Temporary verification, before any audio starts.
    Buttons_Init();
    LCD_Init();

    PlayerUI_ShowStartup();

    uart_print("\r\nPF6 -> ADC3 CHANNEL 4\r\nADC: 12 BIT (0-4095)\r\n");
    if (PotentiometerVolume_Init())
        uart_print("POTENTIOMETER INITIALIZED\r\n");
    else
        UART_ReportPotentiometerError(
            PotentiometerResult::InitializationFailed, HAL_GetTick());

    // Prime with valid conversions only, before any audio is started.
    for (uint32_t i = 0; i < PotentiometerFilter::SampleCount; ++i)
    {
        const PotentiometerResult result =
            PotentiometerVolume_Update(HAL_GetTick(), true);
        UART_ReportPotentiometerError(result, HAL_GetTick());
    }
    const PotentiometerReading startupVolume = PotentiometerVolume_GetReading();
    if (startupVolume.valid) UART_PrintPotentiometer(startupVolume);
    else uart_print("NO VALID POTENTIOMETER SAMPLE - STARTUP VOLUME 0 PCT\r\n");
    bool codecInitialized = false;

    /* -----------------------------------------------------
       AUDIO CODEC CHECK + INITIALIZATION
       ----------------------------------------------------- */

    uart_print(
        "\r\nChecking RT-Spark audio codec...\r\n"
    );

    if (AudioCodec_Probe())
    {
        uart_print(
            "AUDIO CODEC: ES8388 DETECTED!\r\n"
        );

        uart_print(
            "Initializing audio output...\r\n"
        );

        if (AudioCodec_Init())
        {
            codecInitialized = true;
            // The codec initializes at logical zero. Apply this same accepted
            // setting to audio and the later UI only after initialization.
            AudioCodec_SetVolume(startupVolume.volume);
            uart_print(
                "AUDIO OUTPUT INITIALIZED!\r\n"
            );

            PlayerUI_ShowAudioReady(HAL_GetTick());

            /* Keep the startup tone for this checkpoint. */
            uart_print(
                "Playing test tone...\r\n"
            );

            if (Audio_PlayTestTone(1000))
            {
                uart_print(
                    "TEST TONE COMPLETE.\r\n"
                );
            }
            else
            {
                uart_print(
                    "TEST TONE FAILED.\r\n"
                );
            }
        }
        else
        {
            uart_print(
                "AUDIO INITIALIZATION FAILED!\r\n"
            );
            PlayerUI_ShowAudioFailure(HAL_GetTick());
        }
    }
    else
    {
        uart_print(
            "AUDIO CODEC: NOT DETECTED!\r\n"
        );
        PlayerUI_ShowAudioFailure(HAL_GetTick());
    }

#if defined(RTOS_STAGE2_PLAYER) && RTOS_STAGE2_PLAYER
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
    PlayerState_Init(startupVolume.volume, startupVolume.valid);
#if defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER
    CodecControl_Init(); // Both mutexes follow the startup test tone.
#if defined(RTOS_STAGE5_PLAYER) && RTOS_STAGE5_PLAYER
    LcdControl_Init(); // Pre-scheduler drawing is already complete.
#endif
#endif
#endif
    PlayerTasks_Create(&huart1, codecInitialized, RunPlayerLoop);
#endif

    uart_print("\r\n");
    uart_print("==============================\r\n");
    uart_print("     RT-SPARK MP3 PLAYER\r\n");
    uart_print("==============================\r\n\r\n");
    uart_print("AUDIO SOURCE: SYNTHESIZED PCM (NO MP3 DECODER)\r\n\r\n");

    uart_print("PLAYING MODE\r\n");
    uart_print("PF6 potentiometer = Volume (0-100 PCT)\r\n");
    uart_print("LEFT / RIGHT = Song-selection bits only\r\n");
    uart_print("Short UP alone = Select song 000\r\n");
    uart_print("Hold UP alone for 1 second, release = Pause / Resume\r\n\r\n");
#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON
    uart_print("USER_BUTTON PA0 = Stop / Replay from beginning\r\n"
               "USER_BUTTON is ignored during song selection.\r\n\r\n");
#endif

    uart_print("SONG SELECTION MODE\r\n");
    uart_print("DOWN  = bit 2\r\n");
    uart_print("LEFT  = bit 1\r\n");
    uart_print("RIGHT = bit 0\r\n");
    uart_print("Hold desired bits, press UP once to capture.\r\n");
    uart_print("Release ALL buttons, then press UP within 5 seconds to confirm.\r\n");
    uart_print("A held confirmation press will not pause playback.\r\n\r\n");

#if defined(RTOS_STAGE2_PLAYER) && RTOS_STAGE2_PLAYER
    PlayerTasks_StartScheduler();
}

/* Only Stage 2 places the following unchanged loop on PollButtonsTask.
   Other environments retain the original main() scope for recovery. */
static void RunPlayerLoop(bool codecInitialized)
{
#endif
#if defined(RTOS_STAGE5_PLAYER) && RTOS_STAGE5_PLAYER
    RunPlayerControlLoopStage5(codecInitialized);
#else
    enum PlayerState
    {
        STATE_IDLE,
        STATE_SELECTING,
        STATE_PLAYING,
        STATE_PAUSED
    };

    PlayerState state = STATE_IDLE;
    PlayerState returnState = STATE_IDLE;
    SongSelectionInput selectionInput;
    uint8_t pendingSong = 0;
    uint8_t currentSong = 0;

    bool timeoutScreenVisible = false;
    uint32_t timeoutScreenStart = 0;
    char message[100];

#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
    PlayerState_BindPublisher();
    PlayerStateSnapshot publicState; // Private outgoing data, not a second controller.
    uint32_t snapshotDiagnosticAt = HAL_GetTick();
#if defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER
    (void)codecInitialized; // Stage4 uses the task-owned bootstrap flag for volume writes.
    uint32_t seenVolumeRevision = 0;
    uint32_t seenVolumeDiagnostic = 0;
    uint32_t seenVolumeError = 0;
#endif
    uart_print("STAGE 3: BUTTON/STATE OWNERSHIP ACTIVE\r\n");
#endif

    while (1)
    {
        Audio_Service();
        AudioPlaybackState audioState = Audio_GetPlaybackState();
        if (audioState == AUDIO_FINISHED || audioState == AUDIO_ERROR)
        {
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            publicState.lastCompletion = SnapshotPlayback(audioState);
            PlayerState_RecordEvent(publicState, audioState == AUDIO_FINISHED
                ? PlayerEventKind::Completed : PlayerEventKind::PlaybackError, HAL_GetTick());
#endif
            uart_print(audioState == AUDIO_FINISHED
                ? "\r\nAUDIO: SONG COMPLETE\r\n"
                : "\r\nAUDIO: PLAYBACK ERROR - STOPPED\r\n");
            Audio_StopSong();

            if (state == STATE_SELECTING)
            {
                // Keep the pending choice; timeout must not resurrect an
                // old song that ended while waiting for release/confirmation.
                returnState = STATE_IDLE;
            }
            else
            {
                state = STATE_IDLE;
                timeoutScreenVisible = false;
                PlayerUI_CancelTransient();
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
                PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
#endif
            }
        }

#if defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER
        PlayerStateSnapshot volumeSnapshot;
        PlayerState_GetSnapshot(volumeSnapshot);
        uint8_t volumeLevel = volumeSnapshot.acceptedVolume; // Read-only display copy.
#else
        const PotentiometerResult potResult =
            PotentiometerVolume_Update(HAL_GetTick());
        const PotentiometerReading potReading = PotentiometerVolume_GetReading();
        // A snapshot of the filter's one accepted volume; never a separate
        // display value. It remains valid through every player state.
        const uint8_t volumeLevel = potReading.volume;

#endif

        // Read all inputs independently. Active-low pins become logical bits.
        SelectionButtons raw;
        raw.up = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_5) == GPIO_PIN_RESET;
        raw.down = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET;
        raw.left = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET;
        raw.right = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_RESET;
        const uint32_t inputNow = HAL_GetTick();
        SelectionEvents input = selectionInput.update(
            inputNow, raw, state == STATE_PLAYING || state == STATE_PAUSED);

        if (input.captured)
        {
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            PlayerState_RecordEvent(publicState, PlayerEventKind::Captured, inputNow);
#endif
            returnState = state;
            state = STATE_SELECTING;
            pendingSong = selectionInput.pendingSong();
            timeoutScreenVisible = false;
            PlayerUI_CancelTransient();
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
#endif
            UART_PrintSelectionCaptured(pendingSong);
        }

        if (input.readyToConfirm)
        {
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            publicState.confirmationStart = inputNow;
            publicState.confirmationDeadline = inputNow + 5000U;
            PlayerState_RecordEvent(publicState, PlayerEventKind::ConfirmationReady, inputNow);
#endif
            PlayerUI_ConfirmationReady(inputNow);
            uart_print("\r\nALL BUTTONS RELEASED\r\n"
                       "CONFIRMATION WINDOW: 5 SECONDS\r\n"
                       "Press UP to confirm.\r\n");
        }

        if (input.timedOut)
        {
            state = returnState;
            uart_print("\r\n------------------------------\r\n"
                       "SELECTION TIMEOUT\r\n"
                       "Song change cancelled.\r\n"
                       "------------------------------\r\n");
            timeoutScreenVisible = true;
            timeoutScreenStart = HAL_GetTick();
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            PlayerState_RecordEvent(publicState, PlayerEventKind::Timeout, timeoutScreenStart);
#endif
        }

        if (input.confirmed)
        {
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            publicState.hasCurrentSong = true;
            PlayerState_RecordEvent(publicState, PlayerEventKind::Confirmed, inputNow);
#endif
            PlayerUI_CancelTransient();
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
#endif
            currentSong = pendingSong;
            state = STATE_PLAYING;
            timeoutScreenVisible = false;
            uart_print("\r\n------------------------------\r\n"
                       "SONG CONFIRMED\r\n");
            UART_PrintBinary(currentSong);
            uart_print("Now playing: ");
            UART_PrintSong(currentSong);
            std::snprintf(message, sizeof(message), "Volume: %u%%\r\n", volumeLevel);
            uart_print(message);
            uart_print("------------------------------\r\n");

#if !(defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER)
            if (codecInitialized) AudioCodec_SetVolume(volumeLevel);
#endif
            if (Audio_StartSong(songs[currentSong]))
            {
                uart_print("\r\nAUDIO: STARTING SONG (CONTINUOUS PCM/DMA)\r\n");
            }
            else
            {
                uart_print("\r\nAUDIO: SONG START FAILED\r\n");
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
                publicState.lastCompletion = PlayerPlayback::Error;
                PlayerState_RecordEvent(publicState, PlayerEventKind::PlaybackError, HAL_GetTick());
#endif
                Audio_StopSong();
                state = STATE_IDLE;
                PlayerUI_CancelTransient();
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
                PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
#endif
            }
        }

        // Only UP-alone gestures begun in normal playback can reach this.
        // Capture/confirmation gestures are consumed by the input controller.
        if (input.pauseResume)
        {
            PlayerUI_CancelTransient();
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
#endif
            timeoutScreenVisible = false;
            if (state == STATE_PLAYING)
            {
                if (Audio_PauseSong())
                {
                    state = STATE_PAUSED;
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
                    PlayerState_RecordEvent(publicState, PlayerEventKind::Paused, HAL_GetTick());
#endif
                    uart_print("\r\nPLAYER PAUSED\r\n");
                }
                else
                {
                    uart_print("\r\nAUDIO: PAUSE FAILED\r\n");
                }
            }
            else if (state == STATE_PAUSED)
            {
                if (Audio_ResumeSong())
                {
                    state = STATE_PLAYING;
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
                    PlayerState_RecordEvent(publicState, PlayerEventKind::Resumed, HAL_GetTick());
#endif
                    uart_print("\r\nPLAYER RESUMED\r\nNow playing: ");
                    UART_PrintSong(currentSong);
                }
                else
                {
                    uart_print("\r\nAUDIO: RESUME FAILED\r\n");
                }
            }
        }

#if defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER
        // Volume task owns ADC/filter/codec writes. Poll only prints copies.
        VolumeTaskDiagnostics diagnostics;
        PlayerTasks_GetVolumeDiagnostics(diagnostics);
        if (diagnostics.changedRevision != seenVolumeDiagnostic)
        {
            UART_PrintPotentiometer(diagnostics.changedReading);
            seenVolumeDiagnostic = diagnostics.changedRevision;
        }
        if (diagnostics.errorRevision != seenVolumeError)
        {
            UART_ReportPotentiometerError(diagnostics.error, diagnostics.errorAt);
            seenVolumeError = diagnostics.errorRevision;
        }
#else
        // Ignore legacy input.volumeSteps; LEFT/RIGHT remain selection bits.
        if (potResult == PotentiometerResult::VolumeChanged)
        {
            if (codecInitialized) AudioCodec_SetVolume(volumeLevel);
            UART_PrintPotentiometer(potReading);
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
            const uint32_t volumeEventAt = HAL_GetTick();
            PlayerState_RecordEvent(publicState, PlayerEventKind::VolumeChanged, volumeEventAt,
                                    state == STATE_PLAYING || state == STATE_PAUSED);
#endif
            if (state == STATE_PLAYING || state == STATE_PAUSED)
            {
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
                PlayerUI_VolumeChanged(volumeEventAt);
#else
                PlayerUI_VolumeChanged(HAL_GetTick());
#endif
                timeoutScreenVisible = false;
            }
        }
        UART_ReportPotentiometerError(potResult, HAL_GetTick());

#endif

        // Retain the existing notice duration; input can supersede it.
        if (timeoutScreenVisible && HAL_GetTick() - timeoutScreenStart >= 1000)
            timeoutScreenVisible = false;

        // Selection owns the indication even if the old song ends meanwhile.
        // Re-read audio after current events so a late EOF/error cannot leave
        // a stale playing/paused LED. Volume overlays do not enter this mapping.
        PlayerLedState ledState = PLAYER_LED_IDLE;
        if (state == STATE_SELECTING)
            ledState = PLAYER_LED_SELECTING;
        else
        {
            const AudioPlaybackState latestAudioState = Audio_GetPlaybackState();
            if (latestAudioState == AUDIO_PLAYING)
                ledState = PLAYER_LED_PLAYING;
            else if (latestAudioState == AUDIO_PAUSED)
                ledState = PLAYER_LED_PAUSED;
        }
        PlayerLeds_SetState(ledState);

        // Render only after audio servicing and all current input events.
        PlayerUiView view;
        const PlayerState background = state == STATE_SELECTING ? returnState : state;
        view.playback = background == STATE_PLAYING ? UiPlaybackState::Playing :
                        background == STATE_PAUSED ? UiPlaybackState::Paused :
                        UiPlaybackState::Idle;
        view.currentSong = songs[currentSong];
        view.currentIndex = currentSong;
        view.volume = volumeLevel;
        view.candidateIndex = state == STATE_SELECTING ? pendingSong : raw.songBits();
        view.candidateSong = songs[view.candidateIndex];
        if (state == STATE_SELECTING)
            view.selection = selectionInput.waitingForRelease()
                             ? UiSelectionState::Release : UiSelectionState::Confirm;
        view.timeoutVisible = timeoutScreenVisible;
#if defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER
        // Sample public fields outside the mutex; publish one consistent copy.
        publicState.observedAt = HAL_GetTick();
        publicState.mode = state == STATE_SELECTING ? PlayerMode::Selecting :
                           state == STATE_PLAYING ? PlayerMode::Playing :
                           state == STATE_PAUSED ? PlayerMode::Paused : PlayerMode::Idle;
        publicState.background = background == STATE_PLAYING ? PlayerMode::Playing :
                                 background == STATE_PAUSED ? PlayerMode::Paused : PlayerMode::Idle;
        publicState.currentSong = currentSong;
        publicState.pendingSong = pendingSong;
        publicState.previewBits = raw.songBits();
        publicState.capturedBits = pendingSong;
        publicState.phase = state != STATE_SELECTING ? PlayerSelectionPhase::None :
                            selectionInput.waitingForRelease() ? PlayerSelectionPhase::Release :
                            PlayerSelectionPhase::Confirm;
        publicState.timeoutVisible = timeoutScreenVisible;
        publicState.timeoutStart = timeoutScreenStart;
        publicState.playback = SnapshotPlayback(Audio_GetPlaybackState());
        PlayerState_Publish(publicState);
        PlayerState_GetSnapshot(volumeSnapshot);
        volumeLevel = volumeSnapshot.acceptedVolume;
        if (volumeSnapshot.volumeRevision != seenVolumeRevision)
        {
            const PlayerEventStamp &volumeEvent = volumeSnapshot.events[
                static_cast<uint8_t>(PlayerEventKind::VolumeChanged)];
            const PlayerEventStamp &cancelEvent = volumeSnapshot.events[
                static_cast<uint8_t>(PlayerEventKind::PresentationCancelled)];
            if (volumeEvent.overlayEligible &&
                (state == STATE_PLAYING || state == STATE_PAUSED) &&
                !PlayerState_EventAfter(cancelEvent.revision, volumeEvent.revision))
            {
                PlayerUI_VolumeChanged(volumeEvent.timestamp);
                if (timeoutScreenVisible) PlayerState_DismissTimeoutNotice();
                timeoutScreenVisible = false;
            }
            seenVolumeRevision = volumeSnapshot.volumeRevision;
        }
        view.volume = volumeLevel;
        view.timeoutVisible = timeoutScreenVisible;

        if (publicState.observedAt - snapshotDiagnosticAt >= 30000U)
        {
            PlayerStateSnapshot published;
            if (PlayerState_GetSnapshot(published)) UART_PrintPlayerSnapshot(published);
            snapshotDiagnosticAt = publicState.observedAt;
        }
#endif
        PlayerUI_Update(view, HAL_GetTick());

#if !(defined(RTOS_STAGE4_PLAYER) && RTOS_STAGE4_PLAYER)
#if defined(RTOS_STAGE3_PLAYER) && RTOS_STAGE3_PLAYER
        // Sample public fields outside the mutex; publish one consistent copy.
        publicState.observedAt = HAL_GetTick();
        publicState.mode = state == STATE_SELECTING ? PlayerMode::Selecting :
                           state == STATE_PLAYING ? PlayerMode::Playing :
                           state == STATE_PAUSED ? PlayerMode::Paused : PlayerMode::Idle;
        publicState.background = background == STATE_PLAYING ? PlayerMode::Playing :
                                 background == STATE_PAUSED ? PlayerMode::Paused : PlayerMode::Idle;
        publicState.currentSong = currentSong;
        publicState.pendingSong = pendingSong;
        publicState.previewBits = raw.songBits();
        publicState.capturedBits = pendingSong;
        publicState.phase = state != STATE_SELECTING ? PlayerSelectionPhase::None :
                            selectionInput.waitingForRelease() ? PlayerSelectionPhase::Release :
                            PlayerSelectionPhase::Confirm;
        publicState.timeoutVisible = timeoutScreenVisible;
        publicState.timeoutStart = timeoutScreenStart;
        publicState.acceptedVolume = volumeLevel;
        publicState.acceptedVolumeValid = potReading.valid;
        publicState.playback = SnapshotPlayback(Audio_GetPlaybackState());
        PlayerState_Publish(publicState);

        if (publicState.observedAt - snapshotDiagnosticAt >= 30000U)
        {
            PlayerStateSnapshot published;
            if (PlayerState_GetSnapshot(published)) UART_PrintPlayerSnapshot(published);
            snapshotDiagnosticAt = publicState.observedAt;
        }
#endif
#endif

#if defined(RTOS_STAGE2_PLAYER) && RTOS_STAGE2_PLAYER
        PlayerTasks_ReportHealth(HAL_GetTick());
        vTaskDelay(pdMS_TO_TICKS(20));
#else
        HAL_Delay(20);
#endif
    }
#endif
}

#if defined(RTOS_STAGE5_PLAYER) && RTOS_STAGE5_PLAYER
/* Same control/transport/event flow as Stage4, with presentation calls removed. */
static void RunPlayerControlLoopStage5(bool codecInitialized)
{
    enum PlayerState
    {
        STATE_IDLE,
        STATE_SELECTING,
        STATE_PLAYING,
        STATE_PAUSED
    };

    PlayerState state = STATE_IDLE;
    PlayerState returnState = STATE_IDLE;
    SongSelectionInput selectionInput;
    uint8_t pendingSong = 0;
    uint8_t currentSong = 0;

    bool timeoutScreenVisible = false;
    uint32_t timeoutScreenStart = 0;
    char message[100];

    PlayerState_BindPublisher();
    PlayerStateSnapshot publicState; // Private outgoing data, not a second controller.
    uint32_t snapshotDiagnosticAt = HAL_GetTick();
    (void)codecInitialized; // Runtime volume writes use the volume task bootstrap flag.
    uint32_t seenControlVolumeRevision = 0;
    uint32_t seenVolumeDiagnostic = 0;
    uint32_t seenVolumeError = 0;
    uart_print("STAGE 5: CONTROL TASK ACTIVE\r\n");
#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON
    uart_print("STAGE 6: USER_BUTTON STOP/REPLAY ACTIVE\r\n");
#endif

    while (1)
    {
        Audio_Service();
        AudioPlaybackState audioState = Audio_GetPlaybackState();
        if (audioState == AUDIO_FINISHED || audioState == AUDIO_ERROR)
        {
            publicState.lastCompletion = SnapshotPlayback(audioState);
            PlayerState_RecordEvent(publicState, audioState == AUDIO_FINISHED
                ? PlayerEventKind::Completed : PlayerEventKind::PlaybackError, HAL_GetTick());
            uart_print(audioState == AUDIO_FINISHED
                ? "\r\nAUDIO: SONG COMPLETE\r\n"
                : "\r\nAUDIO: PLAYBACK ERROR - STOPPED\r\n");
            Audio_StopSong();

            if (state == STATE_SELECTING)
            {
                // Keep the pending choice; timeout must not resurrect an
                // old song that ended while waiting for release/confirmation.
                returnState = STATE_IDLE;
            }
            else
            {
                state = STATE_IDLE;
                timeoutScreenVisible = false;
                PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
            }
        }

        PlayerStateSnapshot volumeSnapshot;
        PlayerState_GetSnapshot(volumeSnapshot);
        uint8_t volumeLevel = volumeSnapshot.acceptedVolume; // Read-only display copy.

        // Read all inputs independently. Active-low pins become logical bits.
        SelectionButtons raw;
        raw.up = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_5) == GPIO_PIN_RESET;
        raw.down = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET;
        raw.left = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET;
        raw.right = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_RESET;
#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON
        const bool rawUserButton = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET;
        const bool selectionWasActive = selectionInput.selecting();
#endif
        const uint32_t inputNow = HAL_GetTick();
        SelectionEvents input = selectionInput.update(
            inputNow, raw, state == STATE_PLAYING || state == STATE_PAUSED);
#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON
        // Raw selection inputs reserve priority even before their debounce
        // resolves. Include terminal events so a flow ending now still wins.
        const bool selectionPriority = selectionWasActive || selectionInput.selecting() ||
            raw.anyPressed() || input.captured || input.readyToConfirm ||
            input.confirmed || input.timedOut || input.pauseResume;
        const UserButtonAction userAction = userButtonInput.update(
            rawUserButton, inputNow, selectionPriority,
            state == STATE_PLAYING || state == STATE_PAUSED, publicState.hasCurrentSong);
#endif

        if (input.captured)
        {
            PlayerState_RecordEvent(publicState, PlayerEventKind::Captured, inputNow);
            returnState = state;
            state = STATE_SELECTING;
            pendingSong = selectionInput.pendingSong();
            timeoutScreenVisible = false;
            PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
            UART_PrintSelectionCaptured(pendingSong);
        }

        if (input.readyToConfirm)
        {
            publicState.confirmationStart = inputNow;
            publicState.confirmationDeadline = inputNow + 5000U;
            PlayerState_RecordEvent(publicState, PlayerEventKind::ConfirmationReady, inputNow);
            uart_print("\r\nALL BUTTONS RELEASED\r\n"
                       "CONFIRMATION WINDOW: 5 SECONDS\r\n"
                       "Press UP to confirm.\r\n");
        }

        if (input.timedOut)
        {
            state = returnState;
            uart_print("\r\n------------------------------\r\n"
                       "SELECTION TIMEOUT\r\n"
                       "Song change cancelled.\r\n"
                       "------------------------------\r\n");
            timeoutScreenVisible = true;
            timeoutScreenStart = HAL_GetTick();
            PlayerState_RecordEvent(publicState, PlayerEventKind::Timeout, timeoutScreenStart);
        }

        if (input.confirmed)
        {
            publicState.hasCurrentSong = true;
            PlayerState_RecordEvent(publicState, PlayerEventKind::Confirmed, inputNow);
            PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
            currentSong = pendingSong;
            state = STATE_PLAYING;
            timeoutScreenVisible = false;
            uart_print("\r\n------------------------------\r\n"
                       "SONG CONFIRMED\r\n");
            UART_PrintBinary(currentSong);
            uart_print("Now playing: ");
            UART_PrintSong(currentSong);
            std::snprintf(message, sizeof(message), "Volume: %u%%\r\n", volumeLevel);
            uart_print(message);
            uart_print("------------------------------\r\n");

            if (Audio_StartSong(songs[currentSong]))
            {
                uart_print("\r\nAUDIO: STARTING SONG (CONTINUOUS PCM/DMA)\r\n");
            }
            else
            {
                uart_print("\r\nAUDIO: SONG START FAILED\r\n");
                publicState.lastCompletion = PlayerPlayback::Error;
                PlayerState_RecordEvent(publicState, PlayerEventKind::PlaybackError, HAL_GetTick());
                Audio_StopSong();
                state = STATE_IDLE;
                PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
            }
        }

        // Only UP-alone gestures begun in normal playback can reach this.
        // Capture/confirmation gestures are consumed by the input controller.
        if (input.pauseResume)
        {
            PlayerState_RecordEvent(publicState, PlayerEventKind::PresentationCancelled, HAL_GetTick());
            timeoutScreenVisible = false;
            if (state == STATE_PLAYING)
            {
                if (Audio_PauseSong())
                {
                    state = STATE_PAUSED;
                    PlayerState_RecordEvent(publicState, PlayerEventKind::Paused, HAL_GetTick());
                    uart_print("\r\nPLAYER PAUSED\r\n");
                }
                else
                {
                    uart_print("\r\nAUDIO: PAUSE FAILED\r\n");
                }
            }
            else if (state == STATE_PAUSED)
            {
                if (Audio_ResumeSong())
                {
                    state = STATE_PLAYING;
                    PlayerState_RecordEvent(publicState, PlayerEventKind::Resumed, HAL_GetTick());
                    uart_print("\r\nPLAYER RESUMED\r\nNow playing: ");
                    UART_PrintSong(currentSong);
                }
                else
                {
                    uart_print("\r\nAUDIO: RESUME FAILED\r\n");
                }
            }
        }

#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON
        // All selection/long-UP processing has priority. Apply at most one
        // USER_BUTTON transport action, then publish through the existing store.
        const UserButtonPlayerResult userResult = UserButtonPlayer_Apply(
            userAction, publicState, songs[currentSong], inputNow);
        if (userResult != UserButtonPlayerResult::Ignored)
        {
            state = publicState.mode == PlayerMode::Playing ? STATE_PLAYING : STATE_IDLE;
            returnState = state;
            timeoutScreenVisible = false;
            if (userResult == UserButtonPlayerResult::Stopped)
                uart_print("\r\nUSER_BUTTON: STOP\r\n");
            else
            {
                uart_print("\r\nUSER_BUTTON: REPLAY\r\n");
                UART_PrintBinary(currentSong);
                uart_print("Replay from beginning: ");
                UART_PrintSong(currentSong);
                uart_print(userResult == UserButtonPlayerResult::Replayed
                    ? "AUDIO: STARTING SONG (CONTINUOUS PCM/DMA)\r\n"
                    : "AUDIO: SONG START FAILED\r\n");
            }
        }
#endif

        // Volume task owns ADC/filter/codec writes. Poll only prints copies.
        VolumeTaskDiagnostics diagnostics;
        PlayerTasks_GetVolumeDiagnostics(diagnostics);
        if (diagnostics.changedRevision != seenVolumeDiagnostic)
        {
            UART_PrintPotentiometer(diagnostics.changedReading);
            seenVolumeDiagnostic = diagnostics.changedRevision;
        }
        if (diagnostics.errorRevision != seenVolumeError)
        {
            UART_ReportPotentiometerError(diagnostics.error, diagnostics.errorAt);
            seenVolumeError = diagnostics.errorRevision;
        }

        // Retain the existing notice duration; input can supersede it.
        if (timeoutScreenVisible && HAL_GetTick() - timeoutScreenStart >= 1000)
            timeoutScreenVisible = false;

        const PlayerState background = state == STATE_SELECTING ? returnState : state;

        // Sample public fields outside the mutex; publish one consistent copy.
        publicState.observedAt = HAL_GetTick();
        publicState.mode = state == STATE_SELECTING ? PlayerMode::Selecting :
                           state == STATE_PLAYING ? PlayerMode::Playing :
                           state == STATE_PAUSED ? PlayerMode::Paused : PlayerMode::Idle;
        publicState.background = background == STATE_PLAYING ? PlayerMode::Playing :
                                 background == STATE_PAUSED ? PlayerMode::Paused : PlayerMode::Idle;
        publicState.currentSong = currentSong;
        publicState.pendingSong = pendingSong;
        publicState.previewBits = raw.songBits();
        publicState.capturedBits = pendingSong;
        publicState.phase = state != STATE_SELECTING ? PlayerSelectionPhase::None :
                            selectionInput.waitingForRelease() ? PlayerSelectionPhase::Release :
                            PlayerSelectionPhase::Confirm;
        publicState.timeoutVisible = timeoutScreenVisible;
        publicState.timeoutStart = timeoutScreenStart;
        publicState.playback = SnapshotPlayback(Audio_GetPlaybackState());
        PlayerState_Publish(publicState);
        PlayerState_GetSnapshot(volumeSnapshot);
        volumeLevel = volumeSnapshot.acceptedVolume;
        if (volumeSnapshot.volumeRevision != seenControlVolumeRevision)
        {
            const PlayerEventStamp &volumeEvent = volumeSnapshot.events[
                static_cast<uint8_t>(PlayerEventKind::VolumeChanged)];
            const PlayerEventStamp &cancelEvent = volumeSnapshot.events[
                static_cast<uint8_t>(PlayerEventKind::PresentationCancelled)];
            if (volumeEvent.overlayEligible &&
                (state == STATE_PLAYING || state == STATE_PAUSED) &&
                !PlayerState_EventAfter(cancelEvent.revision, volumeEvent.revision))
            {
                if (timeoutScreenVisible) PlayerState_DismissTimeoutNotice();
                timeoutScreenVisible = false;
            }
            seenControlVolumeRevision = volumeSnapshot.volumeRevision;
        }

        if (publicState.observedAt - snapshotDiagnosticAt >= 30000U)
        {
            PlayerStateSnapshot published;
            if (PlayerState_GetSnapshot(published)) UART_PrintPlayerSnapshot(published);
            snapshotDiagnosticAt = publicState.observedAt;
        }


        PlayerTasks_ReportHealth(HAL_GetTick());
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif
