#ifndef RENDERER_H
#define RENDERER_H

typedef struct renderer_s renderer_t;

void renderer_free(renderer_t ** renderer); 

renderer_t * renderer_init(int width, int height); 

void renderer_clear_vertices(renderer_t * renderer); 

void renderer_add_cylinder(renderer_t * renderer, mat4s m0, float r0, mat4s m1, float r1); 

void renderer_add_leaves(renderer_t * renderer, mat4s mat, float radius); 

void renderer_upload_vertices(renderer_t * renderer); 

void renderer_update(renderer_t * renderer);

void renderer_render(renderer_t * renderer, int cur_width, int cur_height);

#endif
