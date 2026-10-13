#if defined(RTOS_STAGE6_USER_BUTTON) && RTOS_STAGE6_USER_BUTTON

#include "user_button_player.h"
#include "audio_codec.h"

UserButtonPlayerResult UserButtonPlayer_Apply(UserButtonAction action,
                                             PlayerStateSnapshot &control,
                                             const Song *lastConfirmedSong,
                                             uint32_t now)
{
    if (action == UserButtonAction::None ||
        (action == UserButtonAction::Replay &&
         (!control.hasCurrentSong || lastConfirmedSong == nullptr)))
        return UserButtonPlayerResult::Ignored;

    control.timeoutVisible = false;
    control.phase = PlayerSelectionPhase::None;
    PlayerState_RecordEvent(control, PlayerEventKind::PresentationCancelled, now);
    if (action == UserButtonAction::Stop)
    {
        Audio_StopSong();
        control.mode = control.background = PlayerMode::Idle;
        control.playback = Audio_GetPlaybackState() == AUDIO_ERROR
                         ? PlayerPlayback::Error : PlayerPlayback::Idle;
        return UserButtonPlayerResult::Stopped;
    }

    // Exactly the public start path used after ordinary song confirmation.
    if (Audio_StartSong(lastConfirmedSong))
    {
        control.mode = control.background = PlayerMode::Playing;
        control.playback = PlayerPlayback::Playing;
        return UserButtonPlayerResult::Replayed;
    }

    control.lastCompletion = PlayerPlayback::Error;
    PlayerState_RecordEvent(control, PlayerEventKind::PlaybackError, now);
    Audio_StopSong();
    control.mode = control.background = PlayerMode::Idle;
    control.playback = Audio_GetPlaybackState() == AUDIO_ERROR
                     ? PlayerPlayback::Error : PlayerPlayback::Idle;
    return UserButtonPlayerResult::ReplayFailed;
}

#endif // RTOS_STAGE6_USER_BUTTON
