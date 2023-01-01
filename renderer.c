#include "mymath.h"

#define SOKOL_IMPL
#define SOKOL_GLES3
#include "sokol_gfx.h"

#include <stdio.h>
#include <string.h>
/* a uniform block with a model-view-projection matrix */
typedef struct {
    mat4s mvp;
} params_t;

typedef struct {
    vec3s position;
    vec3s normal;
    vec3s colour;
} vertex_t;

#define POD
#define NOT_INTEGRAL
#define T vertex_t
#include <ctl/vector.h>

typedef struct {
    long frame;
    vec_vertex_t vertices;
    int floats_per_vertex;
    sg_bindings bind;
    sg_pipeline pip;
    sg_pass_action pass_action;
    mat4s view_proj;
    float rx;
    float ry;
} renderer_t;

void renderer_free(renderer_t ** renderer) {
    if (*renderer) {
        vec_vertex_t_free(&(*renderer)->vertices);
        sg_shutdown();
        free(*renderer);
        *renderer = NULL;
    }
}

renderer_t * renderer_init(int width, int height) {
    /* setup sokol_gfx */
    sg_desc desc = {0};
    sg_setup(&desc);
    assert(sg_isvalid());

    renderer_t * renderer = malloc(sizeof(renderer_t));

    *renderer = (renderer_t){
        .frame = 0,
        .floats_per_vertex = sizeof(vertex_t) / sizeof(float),
        .vertices = vec_vertex_t_init()
    };

/*
    // create an index buffer for the cube
    uint16_t indices[] = {
        0, 1, 2,  0, 2, 3,
        6, 5, 4,  7, 6, 4,
        8, 9, 10,  8, 10, 11,
        14, 13, 12,  15, 14, 12,
        16, 17, 18,  16, 18, 19,
        22, 21, 20,  23, 22, 20
    };
    sg_buffer ibuf = sg_make_buffer(&(sg_buffer_desc){
        .type = SG_BUFFERTYPE_INDEXBUFFER,
        .data = SG_RANGE(indices)
    });

    // resource bindings struct
    app->bind = (sg_bindings){
        .index_buffer = ibuf
    };*/

    /* create shader */
    sg_shader shd = sg_make_shader(&(sg_shader_desc) {
        .vs.uniform_blocks[0] = {
            .size = sizeof(params_t),
            .uniforms = {
                [0] = { .name="mvp", .type=SG_UNIFORMTYPE_MAT4 }
            }
        },
        /* NOTE: since the shader defines explicit attribute locations,
           we don't need to provide an attribute name lookup table in the shader
        */
        .vs.source =
            "#version 310 es\n"
            "uniform mat4 mvp;\n"
            "layout(location=0) in vec4 position;\n"
            "layout(location=1) in vec3 normal;\n"
            "layout(location=2) in vec3 colour;\n"
            "out vec3 vnormal;\n" 
            "out vec3 vcolour;\n" 
            "void main() {\n"
            "  vnormal = normal;\n"
            "  vcolour = colour;\n"
            "  gl_Position = mvp * position;\n"
            "}\n",
        .fs.source =
            "#version 310 es\n"
            "precision mediump float;\n"
            "in vec3 vnormal;\n"
            "in vec3 vcolour;\n"
            "out vec4 frag_color;\n"
            "void main() {\n"
            "  vec3 light_dir = vec3(0.5, -0.5, 0.0);\n"
            "  vec3 light_colour = vec3(0.9, 0.9, 0.7);\n"
            "  vec3 ambient_colour = vec3(0.7, 0.9, 0.9);\n"
            "  float lambert = dot(light_dir, vnormal);\n"
            "  frag_color = vec4(vcolour * (lambert * light_colour + ambient_colour), 1.0);\n"
            "}\n"
    });

    /* create pipeline object */
    renderer->pip = sg_make_pipeline(&(sg_pipeline_desc){
        .layout = {
            /* test to provide buffer stride, but no attr offsets */
            .buffers[0].stride = renderer->floats_per_vertex * 4,
            .attrs = {
                [0].format=SG_VERTEXFORMAT_FLOAT3,
                [1].format=SG_VERTEXFORMAT_FLOAT3,
                [2].format=SG_VERTEXFORMAT_FLOAT3,
            }
        },
        .shader = shd,
        .index_type = SG_INDEXTYPE_NONE,
        .depth = {
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true,
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    });

    /* default pass action */
    renderer->pass_action = (sg_pass_action){ 0 };

    /* view-projection matrix */
    mat4s proj = glms_perspective(glm_rad(60.0f), (float)width/(float)height, 0.5f, 20.0f);
    mat4s view = glms_lookat((vec3s){0.0f, 2.5f, 6.0f}, (vec3s){0.0f, 1.0f, 0.0f}, (vec3s){0.0f, 1.0f, 0.0f});
    renderer->view_proj = glms_mat4_mul(proj, view);

    return renderer;
}


static void add_cylinder(vec_vertex_t * vertices, mat4s m0, float r0, mat4s m1, float r1) {
    const int N = 18;
    // a 2D triangle
    const float pi = 3.1416f;
    float a = cos(pi / 3.0f);
    float b = sin(pi / 3.0f);
    vec3s va = (vec3s){0.0f, 0.0f, -1.0f};
    vec3s vb = (vec3s){  -b, 0.0f,     a};
    vec3s vc = (vec3s){   b, 0.0f,     a};
    vec3s colour = (vec3s){1.0f, 1.0f, 1.0f};
 
    // a three sided cylinder
    vertex_t v0 =  (vertex_t) {transform(m0, glms_vec3_scale(va, r0)), transform_normal(m0, va), colour};
    vertex_t v1 =  (vertex_t) {transform(m0, glms_vec3_scale(vb, r0)), transform_normal(m0, vb), colour};
    vertex_t v2 =  (vertex_t) {transform(m0, glms_vec3_scale(vc, r0)), transform_normal(m0, vc), colour};
    vertex_t v01 = (vertex_t) {transform(m1, glms_vec3_scale(va, r1)), transform_normal(m1, va), colour}; 
    vertex_t v11 = (vertex_t) {transform(m1, glms_vec3_scale(vb, r1)), transform_normal(m1, vb), colour};
    vertex_t v21 = (vertex_t) {transform(m1, glms_vec3_scale(vc, r1)), transform_normal(m1, vc), colour};

    vertex_t triangles[] = {
        v0, v1, v01,
        v1, v11, v01,
        v1, v2, v11,
        v2, v21, v11,
        v2, v0, v21,
        v0, v01, v21
    };
    assert(sizeof(triangles) / sizeof(vertex_t) == N);

    for (int i = 0; i < N; i++) {
        vec_vertex_t_push_back(vertices, triangles[i]);
    }
}

static void add_leaves(vec_vertex_t * vertices, mat4s mat, float radius) {
    // a 2D triangle
    const float pi = 3.1416f;
    float a = cos(pi / 3.0f);
    float b = sin(pi / 3.0f);
    vec3s c[3] = {
        (vec3s){0.0f, 0.0f, -1.0f},
        (vec3s){  -b, 0.0f,     a},
        (vec3s){   b, 0.0f,     a}
    };
    vec3s normal = transform_normal(mat, (vec3s){0.0f, 1.0f, 0.0f});
    vec3s colour = (vec3s){0.2f, 1.0f, 0.2f};
    const float s = 0.1f;

    // at three points around the branch we add a leaf composed of a single triangle
    for(int i = 0; i < 3; i++) { 
        vec3s p = glms_vec3_scale(c[i], radius + s * 1.5);
        for (int j = 0; j < 3; j++) {
            vec3s t = glms_vec3_add(p, glms_vec3_scale(c[j], s));
            vertex_t v = (vertex_t) {transform(mat, t), normal, colour};
            vec_vertex_t_push_back(vertices, v);
        }
    }
}

static void add_horiz_triangle(vec_vertex_t * vertices, vec3s origin, float radius,
        vec3s colour) {
    // a 2D triangle
    const float pi = 3.1416f;
    float a = cos(pi / 3.0f);
    float b = sin(pi / 3.0f);
    vec3s c[3] = {
        (vec3s){0.0f, 0.0f, -1.0f},
        (vec3s){  -b, 0.0f,     a},
        (vec3s){   b, 0.0f,     a}
    };
    vec3s normal = (vec3s){0.0f, 1.0f, 0.0f};
    for(int i = 0; i < 3; i++) { 
        vec3s t = glms_vec3_add(origin, glms_vec3_scale(c[i], radius));
        vertex_t v = (vertex_t) {t, normal, colour};
        vec_vertex_t_push_back(vertices, v);
    }
}

void renderer_clear_vertices(renderer_t * renderer) {
    vec_vertex_t_clear(&renderer->vertices);
}

void renderer_add_cylinder(renderer_t * renderer, mat4s m0, float r0, mat4s m1, float r1) {
    add_cylinder(&renderer->vertices, m0, r0, m1, r1);
}

void renderer_add_leaves(renderer_t * renderer, mat4s mat, float radius) {
    add_leaves(&renderer->vertices, mat, radius);
} 

void renderer_add_contact_shadow(renderer_t * renderer, vec3s origin, float radius) {
    add_horiz_triangle(&renderer->vertices, origin, radius, (vec3s){0.6f, 0.6f, 0.6f});
} 

void renderer_add_ground_plane(renderer_t * renderer, float radius) {
    add_horiz_triangle(&renderer->vertices, (vec3s){0.0f, -0.1f, 0.0f},
        radius, (vec3s){0.7f, 0.7f, 0.7f});
}

void renderer_upload_vertices(renderer_t * renderer) {
    size_t size = 
        vec_vertex_t_size(&renderer->vertices) * renderer->floats_per_vertex * sizeof(float);

    if (renderer->frame > 0) {
        sg_destroy_buffer(renderer->bind.vertex_buffers[0]);
    }
    sg_buffer vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = (sg_range){vec_vertex_t_data(&renderer->vertices), size}
    });
    renderer->bind = (sg_bindings) {
        .vertex_buffers[0] = vbuf
    };
}

void renderer_update(renderer_t * renderer) {
    /* rotated model matrix */
    // app->rx += 0.1f; 
    renderer->ry += 0.2f;
}

void renderer_render(renderer_t * renderer, int cur_width, int cur_height) {
    params_t vs_params;
    mat4s rxm = glms_quat_mat4(glms_quatv(glm_rad(renderer->rx), (vec3s){1.0f, 0.0f, 0.0f}));
    mat4s rym = glms_quat_mat4(glms_quatv(glm_rad(renderer->ry), (vec3s){0.0f, 1.0f, 0.0f}));
    mat4s model = glms_mat4_mul(rxm, rym);

    /* model-view-projection matrix for vertex shader */
    vs_params.mvp = glms_mat4_mul(renderer->view_proj, model);

    sg_begin_default_pass(&renderer->pass_action, cur_width, cur_height);
    sg_apply_pipeline(renderer->pip);
    sg_apply_bindings(&renderer->bind);
    sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, &SG_RANGE(vs_params));
    sg_draw(0, vec_vertex_t_size(&renderer->vertices), 1);
    sg_end_pass();
    sg_commit();
    renderer->frame++;
}


