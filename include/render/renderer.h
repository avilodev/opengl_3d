#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>

#define RENDERER_MAX_BATCH_QUADS 8192u

typedef enum {
    RENDER_LAYER_WORLD = 0,
    RENDER_LAYER_ACTORS,
    RENDER_LAYER_EFFECTS,
    RENDER_LAYER_UI
} RenderLayer;

typedef struct {
    uint64_t submitted_quads;
    uint64_t submitted_vertices;
    uint32_t draw_batches;
    uint32_t texture_changes;
    uint32_t buffer_flushes;
    uint32_t state_changes;
    uint32_t immediate_vertices;
    uint32_t layer_batches[4];
    uint64_t layer_quads[4];
    double build_cpu_ms;
    double submit_cpu_ms;
} RenderStats;

typedef struct {
    float x, y, width, height;
    float u0, v0, u1, v1;
    float r, g, b, a;
} RenderQuad;

typedef struct {
    unsigned int vertex_buffer;
    unsigned int quad_count;
    unsigned int texture_id;
    int textured;
    void* fallback_quads;
} RendererStaticMesh;

typedef struct {
    void* quads;
    unsigned int quad_count;
    float width;
} RendererTextCache;

void renderer_init(int window_width, int window_height);
void renderer_begin_frame(void);
void renderer_begin_layer(RenderLayer layer);
void renderer_flush(void);
void renderer_end_frame(void);
void renderer_set_stats_logging(int enabled, float interval_seconds);
int renderer_stats_logging_enabled(void);
void renderer_clear(float r, float g, float b);
void renderer_set_screen_projection(int width, int height);
void renderer_draw_rect(float x, float y, float width, float height,
                        float r, float g, float b, float a);
void renderer_draw_rect_rotated(float center_x, float center_y,
                                float width, float height, float angle_radians,
                                float r, float g, float b, float a);
void renderer_draw_sprite(float x, float y, float width, float height,
                          unsigned int texture_id);
void renderer_draw_sprite_uv(float x, float y, float width, float height,
                             unsigned int texture_id,
                             float u0, float v0, float u1, float v1);
void renderer_draw_sprite_uv_tinted(float x, float y, float width, float height,
                                    unsigned int texture_id,
                                    float u0, float v0, float u1, float v1,
                                    float r, float g, float b, float a);
void renderer_draw_sprite_uv_tinted_rotated(float center_x, float center_y,
                                            float width, float height,
                                            unsigned int texture_id,
                                            float u0, float v0, float u1, float v1,
                                            float r, float g, float b, float a,
                                            float angle_radians);
void renderer_static_mesh_init(RendererStaticMesh* mesh);
int renderer_static_mesh_build(RendererStaticMesh* mesh, const RenderQuad* quads,
                               unsigned int quad_count, int textured,
                               unsigned int texture_id);
void renderer_static_mesh_draw(const RendererStaticMesh* mesh);
void renderer_static_mesh_destroy(RendererStaticMesh* mesh);
int renderer_text_cache_build(RendererTextCache* cache, float x, float y,
                              const char* text);
int renderer_text_cache_build_centered(RendererTextCache* cache, float x, float y,
                                       float width, float height, const char* text);
void renderer_text_cache_draw(const RendererTextCache* cache);
void renderer_text_cache_destroy(RendererTextCache* cache);
/* The UI is authored at 720p and scaled up, so a fixed-pixel layout stays the
   same apparent size on a 1080p, 1440p or 4K monitor instead of shrinking as
   the resolution rises. */
#define RENDERER_UI_REFERENCE_HEIGHT 720.0f
#define RENDERER_UI_BASE_FONT_SIZE 24.0f
float renderer_ui_scale(int viewport_height);
void renderer_font_init(const char* path, float size);
void renderer_draw_text(float x, float y, const char* text);
void renderer_draw_text_centered(float x, float y, float w, float h, const char* text);
/* Text at a size and a colour, laid out from (x, y) as its left baseline. The
   font is baked once at one size, so this scales the quads rather than the
   glyphs: it stays crisp shrinking and softens growing, which is the right way
   round for anything that pops in large and settles.

   `scale` is a multiplier on the baked size, not a point size. Everything the
   plain renderer_draw_text draws is this at scale 1 in opaque white. */
void renderer_draw_text_tinted(float x, float y, float scale, const char* text,
                               float r, float g, float b, float a);
/* Advance width of `text` at scale 1, for anything that has to centre it. */
float renderer_text_width(const char* text);
void renderer_draw_circle(float cx, float cy, float radius,
                          float r, float g, float b, float a, int segments);
void renderer_cleanup(void);

#endif
