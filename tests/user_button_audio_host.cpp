#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

// Exercise public STOP/replay transport paths in the real engine. The host
// fixture replaces HAL transport only; it does not implement another player.
#include "../src/audio_codec.cpp"
#include "../src/codec_control.cpp"
#include "../src/user_button_player.cpp"
#include "player_presentation.h"
#include "../include/song_def.h"

namespace {
unsigned checks = 0;
unsigned groups = 0;
void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
#define CHECK(condition) check((condition), #condition)

BaseType_t scheduler = taskSCHEDULER_NOT_STARTED;
int controlTaskToken = 0;
unsigned takes = 0;
unsigned gives = 0;

struct Cursor {
    uint32_t next;
    uint32_t frames;
    uint32_t gap;
    uint32_t wavePhase;
    uint32_t step;
    bool finished;
};

Cursor cursor()
{
    return {nextNote, noteFramesRemaining, gapFramesRemaining,
            phase, phaseStep, sourceFinished};
}

bool sameCursor(const Cursor &left, const Cursor &right)
{
    return left.next == right.next && left.frames == right.frames &&
           left.gap == right.gap && left.wavePhase == right.wavePhase &&
           left.step == right.step && left.finished == right.finished;
}
Cursor lockedCursor = {};
} // namespace

struct StateTestMutex {
    bool held = false;
};
static StateTestMutex testMutex;

namespace StateTestRtos {
uint32_t interruptNumber = 0;
void AssertFailed(const char *condition) { throw std::runtime_error(condition); }
}

BaseType_t xTaskGetSchedulerState() { return scheduler; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return &controlTaskToken; }

SemaphoreHandle_t xSemaphoreCreateMutex()
{
    CHECK(scheduler == taskSCHEDULER_NOT_STARTED);
    return &testMutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{
    CHECK(!hostInIrq && StateTestRtos::interruptNumber == 0);
    CHECK(scheduler == taskSCHEDULER_RUNNING);
    CHECK(mutex == &testMutex && wait == portMAX_DELAY && !mutex->held);
    mutex->held = true;
    lockedCursor = cursor();
    ++takes;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    CHECK(!hostInIrq && StateTestRtos::interruptNumber == 0);
    CHECK(mutex == &testMutex && mutex->held);
    CHECK(sameCursor(lockedCursor, cursor())); // Synthesis stays outside the lock.
    mutex->held = false;
    ++gives;
    return pdTRUE;
}

namespace {
void gpioProbe()
{
    CHECK(testMutex.held); // Every run-time mute/unmute I2C write is serialized.
    hostOnGpioWrite = gpioProbe;
}

void callback(unsigned half)
{
    CHECK(!testMutex.held);
    const unsigned gpioBefore = hostGpioCalls;
    const unsigned stopBefore = hostStopCalls;
    const unsigned takesBefore = takes;
    const unsigned givesBefore = gives;
    hostInIrq = true;
    StateTestRtos::interruptNumber = 16;
    if (half == 0) HAL_I2S_TxHalfCpltCallback(&hi2s3);
    else HAL_I2S_TxCpltCallback(&hi2s3);
    StateTestRtos::interruptNumber = 0;
    hostInIrq = false;
    CHECK(hostGpioCalls == gpioBefore && hostStopCalls == stopBefore);
    CHECK(takes == takesBefore && gives == givesBefore);
}

std::vector<int16_t> bufferCopy()
{
    CHECK(hostMemory != nullptr && hostTransferWords == 2048);
    return std::vector<int16_t>(hostMemory, hostMemory + hostTransferWords);
}

void checkStopped()
{
    CHECK(Audio_GetPlaybackState() == AUDIO_STOPPED);
    CHECK(!streamActive && playingSong == nullptr);
    CHECK(!hostTransportRunning && !hostRequestsEnabled);
    CHECK(!hostIrqEnabled && !hostI2sEnabled);
    CHECK((hi2s3.Instance->CR2 & SPI_CR2_TXDMAEN) == 0);
    CHECK(!testMutex.held && takes == gives);
}

void resetTransport()
{
    hostAck = true;
    hostStartResult = hostPauseResult = hostResumeResult = hostStopResult = HAL_OK;
    Audio_StopSong();
    CHECK(Audio_GetPlaybackState() == AUDIO_STOPPED);
    CHECK(!testMutex.held && takes == gives);
}

void testBootstrap()
{
    ++groups;
    CHECK(AudioCodec_Init());
    CHECK(takes == 0 && gives == 0);
    CodecControl_Init();
    scheduler = taskSCHEDULER_RUNNING;
    hostOnGpioWrite = gpioProbe;
}

void testPlayingStopHasNoDeferredRestart()
{
    ++groups;
    resetTransport();
    CHECK(Audio_StartSong(&FUR_ELISE));
    CHECK(playingSong == &FUR_ELISE);
    for (unsigned half = 0; half < 6; ++half) callback(half % 2);
    const unsigned stopsBefore = hostStopCalls;
    const unsigned locksBefore = takes;
    Audio_StopSong();
    CHECK(hostStopCalls == stopsBefore + 1 && takes == locksBefore + 1);
    checkStopped();
    const Cursor stoppedCursor = cursor();
    const auto stoppedBuffer = bufferCopy();
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
        Audio_Service();
        callback(cycle % 2); // A late DMA notification cannot restart playback.
        checkStopped();
        CHECK(sameCursor(stoppedCursor, cursor()));
        CHECK(std::equal(stoppedBuffer.begin(), stoppedBuffer.end(), hostMemory));
    }
    CHECK(!Audio_ResumeSong());
    checkStopped();
}

void testPausedStopAndReplay()
{
    ++groups;
    resetTransport();
    CHECK(Audio_StartSong(&CANNON_IN_D));
    const auto beginning = bufferCopy();
    const Cursor beginningCursor = cursor();
    callback(0);
    hostNdtr = 1638;
    CHECK(Audio_PauseSong());
    CHECK(Audio_GetPlaybackState() == AUDIO_PAUSED && !hostRequestsEnabled);
    const unsigned stopsBefore = hostStopCalls;
    Audio_StopSong();
    CHECK(hostStopCalls == stopsBefore + 1);
    checkStopped();
    CHECK(!Audio_ResumeSong()); // Dedicated STOP does not behave like pause/resume.
    CHECK(Audio_StartSong(&CANNON_IN_D));
    CHECK(playingSong == &CANNON_IN_D && hostNdtr == 2048);
    CHECK(sameCursor(beginningCursor, cursor()));
    CHECK(std::equal(beginning.begin(), beginning.end(), hostMemory));
    Audio_StopSong();
    checkStopped();
}

void testReplayRestartsTheFirstNote()
{
    ++groups;
    resetTransport();
    float notes[] = {1000.0f / 440.0f, 1000.0f / 659.0f, 1000.0f / 880.0f};
    float beats[] = {0.05f, 0.15f, 0.05f}; // 100, 300 and 100 ms plus note gaps.
    Song score("replay fixture", "", notes, beats, 0.25f, 3);
    CHECK(Audio_StartSong(&score));
    CHECK(nextNote == 1 && noteFramesRemaining == 4410 - 1024);
    const auto beginning = bufferCopy();
    const Cursor beginningCursor = cursor();
    for (unsigned half = 0; half < 10; ++half) callback(half % 2);
    CHECK(nextNote == 2); // Advance beyond note zero before STOP.
    CHECK(!std::equal(beginning.begin(), beginning.end(), hostMemory));
    Audio_StopSong();
    checkStopped();
    CHECK(Audio_StartSong(&score));
    CHECK(playingSong == &score && hostNdtr == 2048);
    CHECK(nextNote == 1 && noteFramesRemaining == 4410 - 1024);
    CHECK(sameCursor(beginningCursor, cursor()));
    CHECK(std::equal(beginning.begin(), beginning.end(), hostMemory));
    Audio_StopSong();
    checkStopped();
}

void testNaturalCompletionCanReplay()
{
    ++groups;
    resetTransport();
    float notes[] = {1000.0f / 440.0f, 1000.0f / 659.0f};
    float beats[] = {0.01f, 0.01f}; // Each note clamps to 20 ms, then a 20 ms gap.
    Song score("completion fixture", "", notes, beats, 0.25f, 2);
    CHECK(Audio_StartSong(&score));
    const auto beginning = bufferCopy();
    const Cursor beginningCursor = cursor();
    for (unsigned half = 0; half < 7; ++half) {
        CHECK(Audio_GetPlaybackState() == AUDIO_PLAYING);
        callback(half % 2);
    }
    CHECK(Audio_GetPlaybackState() == AUDIO_FINISHED);
    CHECK(nextNote == 2 && hostTransportRunning);
    Audio_Service(); // Foreground EOF cleanup does not erase external song identity.
    CHECK(Audio_GetPlaybackState() == AUDIO_FINISHED);
    CHECK(!hostTransportRunning && !streamActive && playingSong == nullptr);
    Audio_StopSong(); // Same completion acknowledgement used by the player owner.
    checkStopped();
    CHECK(Audio_StartSong(&score));
    CHECK(playingSong == &score && hostNdtr == 2048);
    CHECK(sameCursor(beginningCursor, cursor()));
    CHECK(std::equal(beginning.begin(), beginning.end(), hostMemory));
    Audio_StopSong();
    checkStopped();
}

void testAllEightSongIdentities()
{
    ++groups;
    resetTransport();
    Song *const scores[] = {
        &FUR_ELISE, &CANNON_IN_D, &MINUET_IN_G_MAJOR, &TURKISH_MARCH,
        &NOCTRUNE_IN_E_FLAT, &WALTZ_NO2, &NOCTRUNE_IN_C_SHARP_MAJOR, &SYMPHONY_NO40
    };
    for (Song *score : scores) {
        CHECK(Audio_StartSong(score));
        const auto beginning = bufferCopy();
        const Cursor beginningCursor = cursor();
        callback(0);
        callback(1);
        Audio_StopSong();
        checkStopped();
        CHECK(Audio_StartSong(score));
        CHECK(playingSong == score && hostNdtr == 2048);
        CHECK(sameCursor(beginningCursor, cursor()));
        CHECK(std::equal(beginning.begin(), beginning.end(), hostMemory));
        Audio_StopSong();
        checkStopped();
    }
}

void testPauseResumeStillPreservesPosition()
{
    ++groups;
    resetTransport();
    CHECK(Audio_StartSong(&MINUET_IN_G_MAJOR));
    callback(0);
    hostNdtr = 1638;
    const auto savedBuffer = bufferCopy();
    const Cursor savedCursor = cursor();
    const unsigned stopsBefore = hostStopCalls;
    CHECK(Audio_PauseSong());
    CHECK(Audio_GetPlaybackState() == AUDIO_PAUSED && hostNdtr == 1638);
    CHECK(sameCursor(savedCursor, cursor()));
    CHECK(std::equal(savedBuffer.begin(), savedBuffer.end(), hostMemory));
    CHECK(Audio_ResumeSong());
    CHECK(Audio_GetPlaybackState() == AUDIO_PLAYING && hostNdtr == 1638);
    CHECK(hostStopCalls == stopsBefore && sameCursor(savedCursor, cursor()));
    CHECK(std::equal(savedBuffer.begin(), savedBuffer.end(), hostMemory));
    Audio_StopSong();
    checkStopped();
}

void testStopFailureAndReplayFailureStaySafe()
{
    ++groups;
    resetTransport();
    CHECK(Audio_StartSong(&FUR_ELISE));
    hostStopResult = HAL_ERROR;
    Audio_StopSong();
    CHECK(Audio_GetPlaybackState() == AUDIO_ERROR && !audioReady);
    CHECK(!streamActive && playingSong == nullptr && !hostIrqEnabled);
    CHECK(!hostI2sEnabled && (hi2s3.Instance->CR2 & SPI_CR2_TXDMAEN) == 0);
    CHECK(!testMutex.held && takes == gives);
    hostStopResult = HAL_OK;
    CHECK(!Audio_StartSong(&FUR_ELISE)); // Existing transport recovery guard remains.
    hostTransportRunning = hostRequestsEnabled = false; // Simulate board reset.
    hostOnGpioWrite = nullptr;
    scheduler = taskSCHEDULER_NOT_STARTED;
    CHECK(AudioCodec_Init());
    scheduler = taskSCHEDULER_RUNNING;
    hostOnGpioWrite = gpioProbe;
    resetTransport();

    hostStartResult = HAL_ERROR;
    CHECK(!Audio_StartSong(&FUR_ELISE));
    CHECK(Audio_GetPlaybackState() == AUDIO_ERROR);
    CHECK(!hostTransportRunning && !hostIrqEnabled && !streamActive);
    CHECK(!testMutex.held && takes == gives);
    resetTransport();

    hostAck = false;
    CHECK(!Audio_StartSong(&FUR_ELISE));
    CHECK(Audio_GetPlaybackState() == AUDIO_ERROR && !hostTransportRunning);
    CHECK(!testMutex.held && takes == gives);
    resetTransport();
}

PlayerStateSnapshot confirmedControl(PlayerMode mode, uint8_t song)
{
    PlayerStateSnapshot control;
    control.mode = control.background = mode;
    control.playback = mode == PlayerMode::Playing ? PlayerPlayback::Playing :
                       mode == PlayerMode::Paused ? PlayerPlayback::Paused :
                                                   PlayerPlayback::Idle;
    control.hasCurrentSong = true;
    control.currentSong = song;
    control.pendingSong = 6;
    control.previewBits = 3;
    control.capturedBits = 2;
    control.acceptedVolume = 73;
    control.acceptedVolumeValid = true;
    control.volumeRevision = 29;
    control.lastCompletion = PlayerPlayback::Finished;
    return control;
}

void checkRetainedControl(const PlayerStateSnapshot &control, uint8_t song)
{
    CHECK(control.hasCurrentSong && control.currentSong == song);
    CHECK(control.pendingSong == 6 && control.previewBits == 3 && control.capturedBits == 2);
    CHECK(control.acceptedVolumeValid && control.acceptedVolume == 73);
    CHECK(control.volumeRevision == 29);
}

void testApplyPlayingAndPausedStop()
{
    ++groups;
    resetTransport();
    for (const PlayerMode mode : {PlayerMode::Playing, PlayerMode::Paused}) {
        CHECK(Audio_StartSong(&NOCTRUNE_IN_E_FLAT));
        if (mode == PlayerMode::Paused) CHECK(Audio_PauseSong());
        PlayerStateSnapshot control = confirmedControl(mode, 4);
        control.timeoutVisible = true;
        const unsigned stopsBefore = hostStopCalls;
        CHECK(UserButtonPlayer_Apply(UserButtonAction::Stop, control,
                                    &NOCTRUNE_IN_E_FLAT, 1234) == UserButtonPlayerResult::Stopped);
        CHECK(hostStopCalls == stopsBefore + 1);
        checkStopped();
        CHECK(control.mode == PlayerMode::Idle && control.background == PlayerMode::Idle);
        CHECK(control.playback == PlayerPlayback::Idle && !control.timeoutVisible);
        CHECK(control.phase == PlayerSelectionPhase::None);
        checkRetainedControl(control, 4);
        CHECK(control.lastCompletion == PlayerPlayback::Finished);
        const auto &cancel = control.events[static_cast<uint8_t>(PlayerEventKind::PresentationCancelled)];
        CHECK(cancel.revision != 0 && cancel.timestamp == 1234);
        CHECK(!Audio_ResumeSong());
    }
}

void testApplyNoActionAndNoHistory()
{
    ++groups;
    resetTransport();
    PlayerStateSnapshot noHistory;
    noHistory.currentSong = 5; // Must not manufacture a first selection or default 000.
    noHistory.timeoutVisible = true;
    CHECK(UserButtonPlayer_Apply(UserButtonAction::Replay, noHistory,
                                &WALTZ_NO2, 10) == UserButtonPlayerResult::Ignored);
    CHECK(!noHistory.hasCurrentSong && noHistory.currentSong == 5);
    CHECK(noHistory.mode == PlayerMode::Idle && noHistory.timeoutVisible);
    CHECK(noHistory.eventSequence == 0 && Audio_GetPlaybackState() == AUDIO_STOPPED);

    PlayerStateSnapshot known = confirmedControl(PlayerMode::Idle, 5);
    CHECK(UserButtonPlayer_Apply(UserButtonAction::Replay, known,
                                nullptr, 20) == UserButtonPlayerResult::Ignored);
    checkRetainedControl(known, 5);
    CHECK(known.eventSequence == 0 && Audio_GetPlaybackState() == AUDIO_STOPPED);

    CHECK(Audio_StartSong(&WALTZ_NO2));
    known = confirmedControl(PlayerMode::Playing, 5);
    const unsigned stopsBefore = hostStopCalls;
    const unsigned locksBefore = takes;
    CHECK(UserButtonPlayer_Apply(UserButtonAction::None, known,
                                &WALTZ_NO2, 30) == UserButtonPlayerResult::Ignored);
    CHECK(known.mode == PlayerMode::Playing && known.eventSequence == 0);
    CHECK(Audio_GetPlaybackState() == AUDIO_PLAYING && playingSong == &WALTZ_NO2);
    CHECK(hostStopCalls == stopsBefore && takes == locksBefore);
    Audio_StopSong();
    checkStopped();
}

void testApplyReplaySameSongFromBeginning()
{
    ++groups;
    resetTransport();
    CHECK(Audio_StartSong(&NOCTRUNE_IN_C_SHARP_MAJOR));
    const auto beginning = bufferCopy();
    const Cursor beginningCursor = cursor();
    for (unsigned half = 0; half < 10; ++half) callback(half % 2);
    Audio_StopSong();
    PlayerStateSnapshot control = confirmedControl(PlayerMode::Idle, 6);
    control.timeoutVisible = true;
    CHECK(UserButtonPlayer_Apply(UserButtonAction::Replay, control,
                                &NOCTRUNE_IN_C_SHARP_MAJOR, 3210) == UserButtonPlayerResult::Replayed);
    CHECK(control.mode == PlayerMode::Playing && control.background == PlayerMode::Playing);
    CHECK(control.playback == PlayerPlayback::Playing && !control.timeoutVisible);
    CHECK(playingSong == &NOCTRUNE_IN_C_SHARP_MAJOR && hostNdtr == 2048);
    CHECK(sameCursor(beginningCursor, cursor()));
    CHECK(std::equal(beginning.begin(), beginning.end(), hostMemory));
    checkRetainedControl(control, 6);
    CHECK(control.lastCompletion == PlayerPlayback::Finished);
    CHECK(control.events[static_cast<uint8_t>(PlayerEventKind::PresentationCancelled)].timestamp == 3210);
    Audio_StopSong();
    checkStopped();
}

void testApplyReplayAfterCompletion()
{
    ++groups;
    resetTransport();
    float notes[] = {1000.0f / 440.0f, 1000.0f / 659.0f};
    float beats[] = {0.01f, 0.01f};
    Song score("finished fixture", "", notes, beats, 0.25f, 2);
    CHECK(Audio_StartSong(&score));
    const auto beginning = bufferCopy();
    const Cursor beginningCursor = cursor();
    for (unsigned half = 0; half < 7; ++half) callback(half % 2);
    CHECK(Audio_GetPlaybackState() == AUDIO_FINISHED);
    Audio_Service();
    Audio_StopSong();
    PlayerStateSnapshot control = confirmedControl(PlayerMode::Idle, 2);
    PlayerState_RecordEvent(control, PlayerEventKind::Completed, 500);
    CHECK(UserButtonPlayer_Apply(UserButtonAction::Replay, control,
                                &score, 700) == UserButtonPlayerResult::Replayed);
    CHECK(control.mode == PlayerMode::Playing && control.playback == PlayerPlayback::Playing);
    checkRetainedControl(control, 2);
    CHECK(control.lastCompletion == PlayerPlayback::Finished);
    CHECK(control.events[static_cast<uint8_t>(PlayerEventKind::Completed)].timestamp == 500);
    CHECK(playingSong == &score && sameCursor(beginningCursor, cursor()));
    CHECK(std::equal(beginning.begin(), beginning.end(), hostMemory));
    Audio_StopSong();
    checkStopped();
}

void testApplyNormalPresentationCancelsOldOverlay()
{
    ++groups;
    resetTransport();
    CHECK(Audio_StartSong(&FUR_ELISE));
    Song *const songs[] = {&FUR_ELISE};
    PlayerPresentationObserver presenter(songs, 1);
    PlayerStateSnapshot control = confirmedControl(PlayerMode::Playing, 0);
    PlayerState_RecordEvent(control, PlayerEventKind::VolumeChanged, 100, true);
    PlayerPresentationFrame frame = presenter.Observe(control, 100);
    CHECK(frame.screen == PlayerPresentationScreen::Volume && frame.led == PLAYER_LED_PLAYING);
    CHECK(UserButtonPlayer_Apply(UserButtonAction::Stop, control,
                                &FUR_ELISE, 120) == UserButtonPlayerResult::Stopped);
    frame = presenter.Observe(control, 120);
    CHECK(frame.screen == PlayerPresentationScreen::Idle && frame.led == PLAYER_LED_IDLE);
    CHECK(frame.view.playback == UiPlaybackState::Idle);
    CHECK(frame.actionCount == 1);
    CHECK(frame.actions[0].kind == PlayerPresentationActionKind::CancelTransient);
    CHECK(UserButtonPlayer_Apply(UserButtonAction::Replay, control,
                                &FUR_ELISE, 140) == UserButtonPlayerResult::Replayed);
    frame = presenter.Observe(control, 140);
    CHECK(frame.screen == PlayerPresentationScreen::Playing && frame.led == PLAYER_LED_PLAYING);
    CHECK(frame.view.playback == UiPlaybackState::Playing && frame.view.currentSong == &FUR_ELISE);
    CHECK(frame.view.volume == 73);
    CHECK(frame.actionCount == 1);
    CHECK(frame.actions[0].kind == PlayerPresentationActionKind::CancelTransient);
    Audio_StopSong();
    checkStopped();
}

void testApplyFailuresRetainIdentityAndReportOutcome()
{
    ++groups;
    resetTransport();
    CHECK(Audio_StartSong(&FUR_ELISE));
    PlayerStateSnapshot control = confirmedControl(PlayerMode::Playing, 0);
    hostStopResult = HAL_ERROR;
    CHECK(UserButtonPlayer_Apply(UserButtonAction::Stop, control,
                                &FUR_ELISE, 900) == UserButtonPlayerResult::Stopped);
    CHECK(control.mode == PlayerMode::Idle && control.background == PlayerMode::Idle);
    CHECK(control.playback == PlayerPlayback::Error); // Preserve failed cleanup evidence.
    CHECK(Audio_GetPlaybackState() == AUDIO_ERROR && !audioReady);
    checkRetainedControl(control, 0);
    CHECK(!hostIrqEnabled && !hostI2sEnabled && !streamActive);
    CHECK(!testMutex.held && takes == gives);
    hostStopResult = HAL_OK;
    hostTransportRunning = hostRequestsEnabled = false;
    hostOnGpioWrite = nullptr;
    scheduler = taskSCHEDULER_NOT_STARTED;
    CHECK(AudioCodec_Init());
    scheduler = taskSCHEDULER_RUNNING;
    hostOnGpioWrite = gpioProbe;
    resetTransport();

    control = confirmedControl(PlayerMode::Idle, 0);
    hostStartResult = HAL_ERROR;
    CHECK(UserButtonPlayer_Apply(UserButtonAction::Replay, control,
                                &FUR_ELISE, 1000) == UserButtonPlayerResult::ReplayFailed);
    CHECK(control.mode == PlayerMode::Idle && control.background == PlayerMode::Idle);
    CHECK(control.playback == PlayerPlayback::Idle && control.lastCompletion == PlayerPlayback::Error);
    checkRetainedControl(control, 0);
    CHECK(control.events[static_cast<uint8_t>(PlayerEventKind::PlaybackError)].timestamp == 1000);
    CHECK(control.events[static_cast<uint8_t>(PlayerEventKind::PresentationCancelled)].timestamp == 1000);
    CHECK(PlayerState_EventAfter(
        control.events[static_cast<uint8_t>(PlayerEventKind::PlaybackError)].revision,
        control.events[static_cast<uint8_t>(PlayerEventKind::PresentationCancelled)].revision));
    CHECK(!hostTransportRunning && !hostIrqEnabled && !streamActive);
    CHECK(!testMutex.held && takes == gives);
    resetTransport();
}
} // namespace

int main()
{
    testBootstrap();
    testPlayingStopHasNoDeferredRestart();
    testPausedStopAndReplay();
    testReplayRestartsTheFirstNote();
    testNaturalCompletionCanReplay();
    testAllEightSongIdentities();
    testPauseResumeStillPreservesPosition();
    testStopFailureAndReplayFailureStaySafe();
    testApplyPlayingAndPausedStop();
    testApplyNoActionAndNoHistory();
    testApplyReplaySameSongFromBeginning();
    testApplyReplayAfterCompletion();
    testApplyNormalPresentationCancelsOldOverlay();
    testApplyFailuresRetainIdentityAndReportOutcome();
    hostOnGpioWrite = nullptr;
    std::printf("PASS: USER_BUTTON audio STOP/replay: %u groups, %u checks\n", groups, checks);
}
