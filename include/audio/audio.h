#ifndef AUDIO_H
#define AUDIO_H

// ============================================================================
// AUDIO SYSTEM
// Windows winmm backend.  Volumes are 0.0 - 1.0.
// SFX:   WAV files played asynchronously via PlaySound.
// Music: any format supported by MCI (WAV / MP3 / MIDI) via mciSendString.
// Files that don't exist are silently skipped.
// ============================================================================

void audio_init(void);
void audio_cleanup(void);

// Volume control (call after loading settings)
void audio_set_master_volume(float v);  // Affects both SFX and music
void audio_set_music_volume(float v);
void audio_set_sfx_volume(float v);

// Sound effects — async, fire-and-forget
// path: relative path to .wav, e.g. "Sounds/hit.wav"
void audio_play_sfx(const char* path);
typedef enum { AUDIO_COMBAT_CAST=0,AUDIO_COMBAT_RELEASE,AUDIO_COMBAT_HIT,
               AUDIO_COMBAT_RECOVERY,AUDIO_COMBAT_CUE_COUNT } AudioCombatCue;
void audio_play_combat_cue(AudioCombatCue cue);

// Background music — loops by default
// path: relative path to .wav/.mp3/.mid
void audio_play_music(const char* path, int loop);
void audio_stop_music(void);

#endif // AUDIO_H
