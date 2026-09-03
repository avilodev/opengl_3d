#include "audio/audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

// ============================================================================
// STATE
// ============================================================================

static float g_master = 1.0f;
static float g_music  = 1.0f;
static float g_sfx    = 1.0f;
static int   g_music_open = 0;  // 1 if MCI alias "bgm" is open
#define CUE_RATE 22050
#define CUE_SAMPLES 2646
static unsigned char g_combat_cues[AUDIO_COMBAT_CUE_COUNT][44+CUE_SAMPLES*2];

static void write_u16(unsigned char*p,uint16_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void write_u32(unsigned char*p,uint32_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static void build_combat_cue(AudioCombatCue cue){
    unsigned char*b=g_combat_cues[cue];memcpy(b,"RIFF",4);write_u32(b+4,36+CUE_SAMPLES*2);
    memcpy(b+8,"WAVEfmt ",8);write_u32(b+16,16);write_u16(b+20,1);write_u16(b+22,1);
    write_u32(b+24,CUE_RATE);write_u32(b+28,CUE_RATE*2);write_u16(b+32,2);write_u16(b+34,16);
    memcpy(b+36,"data",4);write_u32(b+40,CUE_SAMPLES*2);
    const float base[AUDIO_COMBAT_CUE_COUNT]={180,620,105,330};
    for(int i=0;i<CUE_SAMPLES;i++){float t=(float)i/CUE_RATE,u=(float)i/CUE_SAMPLES;
        float frequency=base[cue];if(cue==AUDIO_COMBAT_CAST)frequency*=1+1.6f*u;
        else if(cue==AUDIO_COMBAT_RELEASE)frequency*=1-.35f*u;
        else if(cue==AUDIO_COMBAT_RECOVERY)frequency*=1+.18f*sinf(u*12.56637f);
        float envelope=(1-u)*(cue==AUDIO_COMBAT_HIT?1.0f:fminf(1,u*8));
        float wave=sinf(6.2831853f*frequency*t);
        if(cue==AUDIO_COMBAT_HIT)wave=wave>=0?1:-1;
        int16_t sample=(int16_t)(wave*envelope*9000);write_u16(b+44+i*2,(uint16_t)sample);
    }
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

// Convert 0.0-1.0 to Windows WORD volume (0-0xFFFF) and pack stereo
static DWORD vol_to_dword(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    DWORD w = (DWORD)(v * 0xFFFF);
    return (w << 16) | w;
}

// Push master volume to the default wave output device
static void push_wave_volume(void) {
    waveOutSetVolume(NULL, vol_to_dword(g_master * g_sfx));
}

// Push music volume to the open MCI stream
static void push_music_volume(void) {
    if (!g_music_open) return;
    int vol = (int)(g_master * g_music * 1000.0f);  // MCI range 0-1000
    if (vol < 0)    vol = 0;
    if (vol > 1000) vol = 1000;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "setaudio bgm volume to %d", vol);
    mciSendStringA(cmd, NULL, 0, NULL);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void audio_init(void) {
    for(int cue=0;cue<AUDIO_COMBAT_CUE_COUNT;cue++)build_combat_cue((AudioCombatCue)cue);
    printf("[AUDIO] Initialized (winmm backend)\n");
}

void audio_play_combat_cue(AudioCombatCue cue){
    if(cue<0||cue>=AUDIO_COMBAT_CUE_COUNT)return;
    PlaySoundA((LPCSTR)g_combat_cues[cue],NULL,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);
}

void audio_cleanup(void) {
    audio_stop_music();
    printf("[AUDIO] Cleaned up\n");
}

void audio_set_master_volume(float v) {
    g_master = (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
    push_wave_volume();
    push_music_volume();
}

void audio_set_music_volume(float v) {
    g_music = (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
    push_music_volume();
}

void audio_set_sfx_volume(float v) {
    g_sfx = (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
    push_wave_volume();
}

void audio_play_sfx(const char* path) {
    if (!path || path[0] == '\0') return;
    // PlaySoundA is async; if file missing it silently fails (SND_NODEFAULT)
    PlaySoundA(path, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

void audio_play_music(const char* path, int loop) {
    if (!path || path[0] == '\0') return;

    // Close any previously opened stream
    if (g_music_open) {
        mciSendStringA("stop bgm",  NULL, 0, NULL);
        mciSendStringA("close bgm", NULL, 0, NULL);
        g_music_open = 0;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "open \"%s\" alias bgm", path);
    MCIERROR err = mciSendStringA(cmd, NULL, 0, NULL);
    if (err != 0) {
        printf("[AUDIO] Could not open music: %s (err %lu)\n", path, (unsigned long)err);
        return;
    }
    g_music_open = 1;
    push_music_volume();

    if (loop) {
        mciSendStringA("play bgm repeat", NULL, 0, NULL);
    } else {
        mciSendStringA("play bgm", NULL, 0, NULL);
    }
    printf("[AUDIO] Playing music: %s\n", path);
}

void audio_stop_music(void) {
    if (!g_music_open) return;
    mciSendStringA("stop bgm",  NULL, 0, NULL);
    mciSendStringA("close bgm", NULL, 0, NULL);
    g_music_open = 0;
}

