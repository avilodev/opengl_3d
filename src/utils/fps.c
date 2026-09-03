#include "utils/fps.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static void sleep_milliseconds(unsigned int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec request = {(time_t)(milliseconds / 1000u),
                               (long)(milliseconds % 1000u) * 1000000L};
    nanosleep(&request, NULL);
#endif
}

void fps_init(FPSCounter* fps, int target_fps) {
    fps->last_time = glfwGetTime();
    fps->delta_time = 0.0;
    fps->frame_count = 0;
    fps->fps_timer = 0.0;
    fps->current_fps = 0.0;
    fps->target_fps = target_fps;
}

void fps_update(FPSCounter* fps) {
    double current_time = glfwGetTime();
    fps->delta_time = current_time - fps->last_time;
    
    // Frame rate limiting
    if (fps->target_fps > 0) {
        double target_frame_time = 1.0 / fps->target_fps;
        
        // Sleep for the bulk of the remainder, leaving only a short final spin
        // to avoid the old full-frame busy wait.
        while (fps->delta_time < target_frame_time) {
            double remaining = target_frame_time - fps->delta_time;
            if (remaining > 0.0015) {
                sleep_milliseconds((unsigned int)((remaining - 0.0005) * 1000.0));
            }
            current_time = glfwGetTime();
            fps->delta_time = current_time - fps->last_time;
        }
    }
    
    fps->last_time = current_time;
    
    // Update FPS counter
    fps->frame_count++;
    fps->fps_timer += fps->delta_time;
    
    // Update FPS every second
    if (fps->fps_timer >= 1.0) {
        fps->current_fps = fps->frame_count / fps->fps_timer;
        fps->frame_count = 0;
        fps->fps_timer = 0.0;
    }
}

double fps_get_current(const FPSCounter* fps) {
    return fps->current_fps;
}

double fps_get_delta_time(const FPSCounter* fps) {
    return fps->delta_time;
}

int fps_should_render(FPSCounter* fps) {
    if (fps->target_fps <= 0) {
        return 1; // Unlimited FPS
    }
    
    double target_frame_time = 1.0 / fps->target_fps;
    return fps->delta_time >= target_frame_time;
}
