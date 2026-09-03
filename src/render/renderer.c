#define STB_TRUETYPE_IMPLEMENTATION
#include "render/stb_truetype.h"
#include "render/renderer.h"
#include "render/gl_buffers.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x, y, u, v, r, g, b, a; } QuadVertex;

static QuadVertex g_vertices[RENDERER_MAX_BATCH_QUADS * 4u];
static uint16_t g_indices[RENDERER_MAX_BATCH_QUADS * 6u];
static uint32_t g_quad_count;
static unsigned int g_batch_texture;
static int g_batch_textured;
static int g_have_batch;
static RenderLayer g_layer;
static RenderStats g_stats;
static stbtt_bakedchar g_baked_chars[96];
static GLuint g_font_texture;
static GLuint g_white_texture;
static int g_log_stats;
static float g_log_interval = 1.0f;
static double g_last_log;
static double g_frame_started;
static GLuint g_stream_vbo;
static GLuint g_index_buffer;
static int g_gpu_buffers;
static int g_state_textured = -1;
static GLuint g_bound_texture;


static void set_vertex_arrays(const void* base) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(QuadVertex), (const char*)base + offsetof(QuadVertex, x));
    glTexCoordPointer(2, GL_FLOAT, sizeof(QuadVertex), (const char*)base + offsetof(QuadVertex, u));
    glColorPointer(4, GL_FLOAT, sizeof(QuadVertex), (const char*)base + offsetof(QuadVertex, r));
}

static void disable_vertex_arrays(void) {
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void prepare_batch(int textured, unsigned int texture) {
    if (g_have_batch && (g_batch_textured != textured ||
                         (textured && g_batch_texture != texture))) {
        renderer_flush();
    }
    if (!g_have_batch) {
        g_batch_textured = textured;
        g_batch_texture = texture;
        g_have_batch = 1;
    }
}

static void apply_material(int textured, GLuint texture) {
    if (g_state_textured != textured) {
        if (textured) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
        g_state_textured = textured;
        g_stats.state_changes++;
    }
    if (textured && g_bound_texture != texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        g_bound_texture = texture;
        g_stats.texture_changes++;
        g_stats.state_changes++;
    }
}

static void append_quad(float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1,
                        float r, float g, float b, float a,
                        int textured, unsigned int texture) {
    if (g_quad_count >= RENDERER_MAX_BATCH_QUADS) {
        g_stats.buffer_flushes++;
        renderer_flush();
    }
    prepare_batch(textured, texture);
    QuadVertex* q = &g_vertices[g_quad_count * 4u];
    QuadVertex built[4] = {
        {x, y, u0, v0, r,g,b,a}, {x+w, y, u1, v0, r,g,b,a},
        {x+w, y+h, u1, v1, r,g,b,a}, {x, y+h, u0, v1, r,g,b,a}
    };
    memcpy(q, built, sizeof(built));
    g_quad_count++;
    g_stats.submitted_quads++;
    g_stats.submitted_vertices += 4u;
    g_stats.layer_quads[g_layer]++;
}

static void append_rotated_quad(float cx,float cy,float w,float h,float angle,
                                float u0,float v0,float u1,float v1,
                                float r,float g,float b,float a,
                                unsigned int texture) {
    if(g_quad_count>=RENDERER_MAX_BATCH_QUADS){g_stats.buffer_flushes++;renderer_flush();}
    prepare_batch(1,texture);
    float c=cosf(angle),s=sinf(angle),hw=w*.5f,hh=h*.5f;
    const float lx[4]={-hw,hw,hw,-hw},ly[4]={-hh,-hh,hh,hh};
    const float us[4]={u0,u1,u1,u0},vs[4]={v0,v0,v1,v1};
    QuadVertex*q=&g_vertices[g_quad_count*4u];
    for(int i=0;i<4;i++)q[i]=(QuadVertex){cx+lx[i]*c-ly[i]*s,
        cy+lx[i]*s+ly[i]*c,us[i],vs[i],r,g,b,a};
    g_quad_count++;g_stats.submitted_quads++;g_stats.submitted_vertices+=4u;
    g_stats.layer_quads[g_layer]++;
}

void renderer_draw_rect_rotated(float cx,float cy,float w,float h,float angle,
    float r,float g,float b,float a){append_rotated_quad(cx,cy,w,h,angle,
        0,0,1,1,r,g,b,a,g_white_texture);}

void renderer_init(int width, int height) {
    /* Init creates resources only. Leaving GL_BLEND and GL_TEXTURE_2D enabled
       here would leak UI state into the 3D pass; renderer_begin_frame owns it. */
    unsigned char white[4]={255,255,255,255};
    glGenTextures(1,&g_white_texture);glBindTexture(GL_TEXTURE_2D,g_white_texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    gl_buffers_init();
    g_gpu_buffers = gl_buffers_available();
    for (uint32_t i = 0; i < RENDERER_MAX_BATCH_QUADS; ++i) {
        uint16_t base = (uint16_t)(i * 4u);
        uint16_t* out = &g_indices[i * 6u];
        out[0]=base; out[1]=base+1; out[2]=base+2;
        out[3]=base; out[4]=base+2; out[5]=base+3;
    }
    if (g_gpu_buffers) {
        g_stream_vbo = gl_buffer_create_stream(gl_buffer_array_target(),
                                               sizeof(g_vertices));
        g_index_buffer = gl_buffer_create_static(gl_buffer_index_target(),
                                                 g_indices, sizeof(g_indices));
        if (!g_stream_vbo || !g_index_buffer) {
            gl_buffer_delete(&g_stream_vbo);
            gl_buffer_delete(&g_index_buffer);
            g_gpu_buffers = 0;
        }
    }
    printf("Renderer initialized: %dx%d (batch capacity %u quads, backend %s)\n",
           width, height, RENDERER_MAX_BATCH_QUADS,
           g_gpu_buffers ? "indexed VBO" : "immediate fallback");
}

void renderer_begin_frame(void) {
    renderer_flush();
    /* The UI is a flat, blended overlay. Claim that state explicitly rather
       than assuming whoever drew last left it this way. */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_stats = (RenderStats){0};
    g_layer = RENDER_LAYER_WORLD;
    g_state_textured=-1;g_bound_texture=0;
    g_frame_started=glfwGetTime();
}

void renderer_begin_layer(RenderLayer layer) {
    if (layer != g_layer) renderer_flush();
    g_layer = layer;
}

void renderer_flush(void) {
    if (!g_quad_count) { g_have_batch = 0; return; }
    double started = glfwGetTime();
    apply_material(g_batch_textured,g_batch_texture);
    if (g_gpu_buffers) {
        gl_buffer_stream(gl_buffer_array_target(), g_stream_vbo, g_vertices,
                         g_quad_count * 4u * sizeof(QuadVertex));
        gl_buffer_bind(gl_buffer_index_target(), g_index_buffer);
        set_vertex_arrays(NULL);
        glDrawElements(GL_TRIANGLES, (GLsizei)(g_quad_count * 6u),
                       GL_UNSIGNED_SHORT, NULL);
        disable_vertex_arrays();
        gl_buffer_bind(gl_buffer_index_target(), 0);
        gl_buffer_bind(gl_buffer_array_target(), 0);
    } else {
        glBegin(GL_TRIANGLES);
        for (uint32_t i = 0; i < g_quad_count; ++i) {
            const uint16_t* ix = &g_indices[i * 6u];
            for (int k = 0; k < 6; ++k) {
                const QuadVertex* v = &g_vertices[ix[k]];
                glColor4f(v->r, v->g, v->b, v->a);
                if (g_batch_textured) glTexCoord2f(v->u, v->v);
                glVertex2f(v->x, v->y);
            }
        }
        glEnd();
        g_stats.immediate_vertices += g_quad_count * 6u;
    }
    g_stats.draw_batches++;
    g_stats.layer_batches[g_layer]++;
    g_quad_count = 0;
    g_have_batch = 0;
    g_stats.submit_cpu_ms += (glfwGetTime() - started) * 1000.0;
}

void renderer_end_frame(void) {
    renderer_flush();
    double renderer_elapsed=(glfwGetTime()-g_frame_started)*1000.0;
    g_stats.build_cpu_ms=renderer_elapsed-g_stats.submit_cpu_ms;
    if(g_stats.build_cpu_ms<0)g_stats.build_cpu_ms=0;
    if (g_log_stats && glfwGetTime() - g_last_log >= g_log_interval) {
        printf("[RENDER] quads=%llu vertices=%llu batches=%u state=%u overflows=%u immediate=%u build=%.3fms submit=%.3fms layers=%u/%u/%u/%u\n",
               (unsigned long long)g_stats.submitted_quads,
               (unsigned long long)g_stats.submitted_vertices, g_stats.draw_batches,
               g_stats.state_changes, g_stats.buffer_flushes,
               g_stats.immediate_vertices, g_stats.build_cpu_ms,
               g_stats.submit_cpu_ms, g_stats.layer_batches[0],
               g_stats.layer_batches[1], g_stats.layer_batches[2],
               g_stats.layer_batches[3]);
        g_last_log = glfwGetTime();
    }
}
int renderer_stats_logging_enabled(void) { return g_log_stats; }

void renderer_set_stats_logging(int enabled, float interval) {
    g_log_stats = enabled;
    if (interval > 0.0f) g_log_interval = interval;
}
void renderer_clear(float r,float g,float b) { renderer_flush(); glClearColor(r,g,b,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); }
void renderer_set_screen_projection(int width,int height) { renderer_flush(); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,width,height,0,-1,1); glMatrixMode(GL_MODELVIEW); glLoadIdentity(); }
void renderer_draw_rect(float x,float y,float w,float h,float r,float g,float b,float a) { append_quad(x,y,w,h,0,0,1,1,r,g,b,a,1,g_white_texture); }
void renderer_draw_sprite_uv_tinted(float x,float y,float w,float h,unsigned int t,float u0,float v0,float u1,float v1,float r,float g,float b,float a) { append_quad(x,y,w,h,u0,v0,u1,v1,r,g,b,a,1,t); }
void renderer_draw_sprite(float x,float y,float w,float h,unsigned int t) { renderer_draw_sprite_uv_tinted(x,y,w,h,t,0,0,1,1,1,1,1,1); }
void renderer_draw_sprite_uv(float x,float y,float w,float h,unsigned int t,float u0,float v0,float u1,float v1) { renderer_draw_sprite_uv_tinted(x,y,w,h,t,u0,v0,u1,v1,1,1,1,1); }
void renderer_draw_sprite_uv_tinted_rotated(float cx,float cy,float w,float h,
    unsigned int t,float u0,float v0,float u1,float v1,float r,float g,float b,
    float a,float angle){append_rotated_quad(cx,cy,w,h,angle,u0,v0,u1,v1,r,g,b,a,t);}

void renderer_static_mesh_init(RendererStaticMesh* mesh) { if(mesh) memset(mesh,0,sizeof(*mesh)); }
void renderer_static_mesh_destroy(RendererStaticMesh* mesh) {
    if(!mesh)return;
    renderer_flush();
    gl_buffer_delete(&mesh->vertex_buffer);
    free(mesh->fallback_quads);
    memset(mesh,0,sizeof(*mesh));
}
int renderer_static_mesh_build(RendererStaticMesh* mesh,const RenderQuad* quads,unsigned int count,int textured,unsigned int texture){
    if(!mesh||(!quads&&count)||count>RENDERER_MAX_BATCH_QUADS)return 0;
    renderer_static_mesh_destroy(mesh);
    mesh->quad_count=count;mesh->textured=textured;mesh->texture_id=textured&&!texture?g_white_texture:texture;
    if(!count)return 1;
    QuadVertex* vertices=malloc((size_t)count*4u*sizeof(*vertices));
    if(!vertices)return 0;
    for(unsigned int i=0;i<count;i++){const RenderQuad*q=&quads[i];QuadVertex*v=&vertices[i*4u];
        v[0]=(QuadVertex){q->x,q->y,q->u0,q->v0,q->r,q->g,q->b,q->a};
        v[1]=(QuadVertex){q->x+q->width,q->y,q->u1,q->v0,q->r,q->g,q->b,q->a};
        v[2]=(QuadVertex){q->x+q->width,q->y+q->height,q->u1,q->v1,q->r,q->g,q->b,q->a};
        v[3]=(QuadVertex){q->x,q->y+q->height,q->u0,q->v1,q->r,q->g,q->b,q->a};}
    mesh->vertex_buffer = gl_buffer_create_static(gl_buffer_array_target(),
        vertices, (size_t)count*4u*sizeof(*vertices));
    if(mesh->vertex_buffer)free(vertices);else mesh->fallback_quads=vertices;
    return 1;
}
void renderer_static_mesh_draw(const RendererStaticMesh* mesh){
    if(!mesh||!mesh->quad_count)return;
    renderer_flush();double started=glfwGetTime();
    apply_material(mesh->textured,mesh->texture_id);
    if(mesh->vertex_buffer){gl_buffer_bind(gl_buffer_array_target(),mesh->vertex_buffer);gl_buffer_bind(gl_buffer_index_target(),g_index_buffer);set_vertex_arrays(NULL);glDrawElements(GL_TRIANGLES,(GLsizei)(mesh->quad_count*6u),GL_UNSIGNED_SHORT,NULL);disable_vertex_arrays();gl_buffer_bind(gl_buffer_index_target(),0);gl_buffer_bind(gl_buffer_array_target(),0);}
    else {const QuadVertex*v=mesh->fallback_quads;glBegin(GL_TRIANGLES);for(unsigned int i=0;i<mesh->quad_count;i++)for(int k=0;k<6;k++){const QuadVertex*p=&v[i*4u+(g_indices[k])];glColor4f(p->r,p->g,p->b,p->a);if(mesh->textured)glTexCoord2f(p->u,p->v);glVertex2f(p->x,p->y);}glEnd();g_stats.immediate_vertices+=mesh->quad_count*6u;}
    g_stats.submitted_quads+=mesh->quad_count;g_stats.submitted_vertices+=(uint64_t)mesh->quad_count*4u;g_stats.layer_quads[g_layer]+=mesh->quad_count;g_stats.draw_batches++;g_stats.layer_batches[g_layer]++;g_stats.submit_cpu_ms+=(glfwGetTime()-started)*1000.0;
}
void renderer_text_cache_destroy(RendererTextCache* cache){if(cache){free(cache->quads);memset(cache,0,sizeof(*cache));}}
static float measure_text(const char*s){float width=0;for(;*s;s++)if((unsigned char)*s>=32&&(unsigned char)*s<128)width+=g_baked_chars[(unsigned char)*s-32].xadvance;return width;}
int renderer_text_cache_build(RendererTextCache* cache,float x,float y,const char*s){
    if(!cache||!s)return 0;
    renderer_text_cache_destroy(cache);
    size_t length=strlen(s);
    RenderQuad*quads=length?malloc(length*sizeof(*quads)):NULL;
    if(length&&!quads)return 0;
    unsigned int count=0;float start=x;
    for(;*s;s++)if((unsigned char)*s>=32&&(unsigned char)*s<128){stbtt_aligned_quad q;stbtt_GetBakedQuad(g_baked_chars,512,512,(unsigned char)*s-32,&x,&y,&q,1);quads[count++]=(RenderQuad){q.x0,q.y0,q.x1-q.x0,q.y1-q.y0,q.s0,q.t0,q.s1,q.t1,1,1,1,1};}
    cache->quads=quads;cache->quad_count=count;cache->width=x-start;return 1;
}
int renderer_text_cache_build_centered(RendererTextCache*cache,float x,float y,float width,float height,const char*s){float tw=measure_text(s);return renderer_text_cache_build(cache,x+(width-tw)*.5f,y+height*.5f+6,s);}
void renderer_text_cache_draw(const RendererTextCache*cache){if(!cache)return;const RenderQuad*quads=cache->quads;for(unsigned int i=0;i<cache->quad_count;i++){const RenderQuad*q=&quads[i];append_quad(q->x,q->y,q->width,q->height,q->u0,q->v0,q->u1,q->v1,q->r,q->g,q->b,q->a,1,g_font_texture);}}
float renderer_ui_scale(int viewport_height) {
    float scale = (float)viewport_height / RENDERER_UI_REFERENCE_HEIGHT;
    /* Never shrink below the authored size, and stop growing past 4K so the
       panel does not swallow the screen on very tall displays. */
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 3.0f) scale = 3.0f;
    return scale;
}

void renderer_font_init(const char* path,float size) {
    unsigned char* ttf=malloc(1u<<20), *bitmap=malloc(512u*512u);
    if(!ttf||!bitmap){ free(ttf); free(bitmap); return; }
    FILE* f=fopen(path,"rb"); if(!f){printf("Font not found: %s\n",path);free(ttf);free(bitmap);return;}
    fread(ttf,1,1u<<20,f); fclose(f);
    stbtt_BakeFontBitmap(ttf,0,size,bitmap,512,512,32,96,g_baked_chars);
    glGenTextures(1,&g_font_texture); glBindTexture(GL_TEXTURE_2D,g_font_texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_ALPHA,512,512,0,GL_ALPHA,GL_UNSIGNED_BYTE,bitmap);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    free(ttf); free(bitmap);
}

void renderer_draw_text(float x,float y,const char* s) {
    while(*s){ if((unsigned char)*s>=32&&(unsigned char)*s<128){ stbtt_aligned_quad q; stbtt_GetBakedQuad(g_baked_chars,512,512,*s-32,&x,&y,&q,1); append_quad(q.x0,q.y0,q.x1-q.x0,q.y1-q.y0,q.s0,q.t0,q.s1,q.t1,1,1,1,1,1,g_font_texture); } ++s; }
}
void renderer_draw_text_centered(float x,float y,float w,float h,const char* s) { renderer_draw_text(x+(w-measure_text(s))*.5f,y+h*.5f+6,s); }
float renderer_text_width(const char* s){ return s ? measure_text(s) : 0.0f; }
void renderer_draw_text_tinted(float x,float y,float scale,const char* s,float r,float g,float b,float a) {
    if(!s) return;
    /* The cursor is walked at the baked size from the origin and each quad is
       then placed at x,y and scaled about it. Advancing the real x by a scaled
       amount instead would accumulate the scale into the kerning and pull the
       string apart at large sizes. */
    float cursor_x=0, cursor_y=0;
    for(;*s;++s){
        unsigned char c=(unsigned char)*s;
        if(c<32||c>=128) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(g_baked_chars,512,512,c-32,&cursor_x,&cursor_y,&q,1);
        append_quad(x+q.x0*scale, y+q.y0*scale,
                    (q.x1-q.x0)*scale, (q.y1-q.y0)*scale,
                    q.s0,q.t0,q.s1,q.t1, r,g,b,a, 1, g_font_texture);
    }
}

static void immediate_fan(float cx,float cy,float radius,float start,float sweep,float r,float g,float b,float a,int segments){
    renderer_flush(); apply_material(0,0); glColor4f(r,g,b,a); glBegin(GL_TRIANGLE_FAN); glVertex2f(cx,cy);
    for(int i=0;i<=segments;++i){float angle=start+sweep*(float)i/(float)segments;glVertex2f(cx+cosf(angle)*radius,cy+sinf(angle)*radius);} glEnd(); g_stats.draw_batches++;
}
void renderer_draw_circle(float cx,float cy,float radius,float r,float g,float b,float a,int segments){if(segments<3)segments=3;immediate_fan(cx,cy,radius,0,6.2831853f,r,g,b,a,segments);}
void renderer_cleanup(void){renderer_flush();gl_buffer_delete(&g_stream_vbo);gl_buffer_delete(&g_index_buffer);if(g_font_texture)glDeleteTextures(1,&g_font_texture);if(g_white_texture)glDeleteTextures(1,&g_white_texture);g_font_texture=g_white_texture=0;}
