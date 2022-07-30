//------------------------------------------------------------------------------
//  cube-glfw.c
//  Shader uniform updates.
//------------------------------------------------------------------------------
#define HANDMADE_MATH_IMPLEMENTATION
#define HANDMADE_MATH_NO_SSE
#include "HandmadeMath.h"
#define SOKOL_IMPL
#define SOKOL_GLCORE33
#include "sokol_gfx.h"
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#include <stdio.h>
#include <string.h>
/* a uniform block with a model-view-projection matrix */
typedef struct {
    hmm_mat4 mvp;
} params_t;

typedef struct {
    void * start;
    int num;
    int max;
    int element_size;
} array_t;

struct path_s;

typedef struct path_s {
    hmm_vec3 position;
    hmm_vec3 direction;
    hmm_vec3 up;
    bool is_leaf;
    const struct path_s * last_path;
} path_t;

typedef struct {
    GLFWwindow * window;
    long frame;
    array_t vertices;
    int floats_per_vertex;
    array_t paths;
    sg_bindings bind;
    sg_pipeline pip;
    sg_pass_action pass_action;
    hmm_mat4 view_proj;
    float rx;
    float ry;
} app_t;

array_t array_create(int element_size, int num) {
    return (array_t){
        .start = malloc(element_size * num),
        .num = 0,
        .max = num,
        .element_size = element_size  
    };
}

void array_destroy(array_t *array) {
    free(array->start);
    array->start = NULL;
    array->num = 0;
    array->max = 0;
}

bool array_can_alloc(const array_t *array, int num) {
    return array->num + num < array->max;
}

void * array_alloc(array_t * array, int num) {
    void * start = array->start + array->num * array->element_size;
    array->num += num;
    return start;
}

path_t * path_alloc(array_t * array, int num) {
    return (path_t *)array_alloc(array, num);
}

path_t * path_get(array_t * array, int index) {
    path_t *p = array->start;
    return &p[index];
}

hmm_vec3 * vertex_alloc(array_t * array, int num) {
    return (hmm_vec3 *)array_alloc(array, num);
}

hmm_vec3 * vertex_get(array_t * array, int index) {
    hmm_vec3 *p = array->start;
    return &p[index];
}

void init(app_t * app) {
    const int WIDTH = 800;
    const int HEIGHT = 600;
    app->frame = 0;

    /* create GLFW window and initialize GL */
    glfwInit();
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* w = glfwCreateWindow(WIDTH, HEIGHT, "Sokol Cube GLFW", 0, 0);
    glfwMakeContextCurrent(w);
    app->window = w;
    glfwSwapInterval(1);

    /* setup sokol_gfx */
    sg_desc desc = {0};
    sg_setup(&desc);
    assert(sg_isvalid());

    app->paths = array_create(sizeof(path_t), 1000);

    app->floats_per_vertex = 3;
    app->vertices = array_create(sizeof(hmm_vec3), app->paths.max * 18);

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
            "#version 330\n"
            "uniform mat4 mvp;\n"
            "layout(location=0) in vec4 position;\n"
            "void main() {\n"
            "  gl_Position = mvp * position;\n"
            "}\n",
        .fs.source =
            "#version 330\n"
            "out vec4 frag_color;\n"
            "void main() {\n"
            "  frag_color = vec4(1.0, 0.0, 0.0, 1.0);\n"
            "}\n"
    });

    /* create pipeline object */
    app->pip = sg_make_pipeline(&(sg_pipeline_desc){
        .layout = {
            /* test to provide buffer stride, but no attr offsets */
            .buffers[0].stride = app->floats_per_vertex * 4,
            .attrs = {
                [0].format=SG_VERTEXFORMAT_FLOAT3,
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
    app->pass_action = (sg_pass_action){ 0 };

    /* view-projection matrix */
    hmm_mat4 proj = HMM_Perspective(60.0f, (float)WIDTH/(float)HEIGHT, 0.01f, 10.0f);
    hmm_mat4 view = HMM_LookAt(HMM_Vec3(0.0f, 1.5f, 6.0f), HMM_Vec3(0.0f, 0.0f, 0.0f), HMM_Vec3(0.0f, 1.0f, 0.0f));
    app->view_proj = HMM_MultiplyMat4(proj, view);
}

bool should_quit(app_t * app) {
    return glfwWindowShouldClose(app->window);
}

float rand_float(float s) {
    return (rand() * s) / RAND_MAX;
}

hmm_vec3 rand_vec(float s) {
    float h = s / 2.0f;
    return (hmm_vec3) {
        .X = rand_float(s) - h,
        .Y = rand_float(s) - h,
        .Z = rand_float(s) - h,
    };
}

hmm_vec4 expand(hmm_vec3 a, float w) {
    return (hmm_vec4) {
        .X = a.X,
        .Y = a.Y,
        .Z = a.Z,
        .W = w
    };
}

hmm_vec3 truncate(hmm_vec4 a) {
    return (hmm_vec3) {
        .X = a.X,
        .Y = a.Y,
        .Z = a.Z
    };    
}

hmm_mat4 mat_from_axes(
    hmm_vec3 x,
    hmm_vec3 y,
    hmm_vec3 z,
    hmm_vec3 t
    ) {
    hmm_mat4 Result = {0};

    Result.Elements[0][0] = x.X;
    Result.Elements[0][1] = x.Y;
    Result.Elements[0][2] = x.Z;

    Result.Elements[1][0] = y.X;
    Result.Elements[1][1] = y.Y;
    Result.Elements[1][2] = y.Z;

    Result.Elements[2][0] = z.X;
    Result.Elements[2][1] = z.Y;
    Result.Elements[2][2] = z.Z;

    Result.Elements[3][0] = t.X;
    Result.Elements[3][1] = t.Y;
    Result.Elements[3][2] = t.Z;
    Result.Elements[3][3] = 1.0f;

    return (Result);
}

hmm_vec3 transform(hmm_mat4 m, hmm_vec3 v) {
    return truncate(HMM_MultiplyMat4ByVec4(m, expand(v, 1.0f)));
}

void axes_from_dir_up(hmm_vec3 dir, hmm_vec3 up,
                hmm_vec3 *x, hmm_vec3 *y, hmm_vec3 *z) {
    *y = HMM_NormalizeVec3(dir);
    *x = HMM_Cross(*y, HMM_NormalizeVec3(up));
    *z = HMM_Cross(*x, *y);
 }

void new_path(const path_t * parent, path_t * child) {
    hmm_vec3 x, y, z;
    axes_from_dir_up(parent->direction, parent->up, &x, &y, &z);
    // perturb direction randomly
    hmm_vec3 direction = HMM_MultiplyVec3f( 
        HMM_NormalizeVec3(HMM_AddVec3(y, rand_vec(1.0f))),
        0.1f);

    *child = (path_t){
        .position = HMM_AddVec3(parent->position, parent->direction),
        .direction = direction,
        .up = z,
        .is_leaf = true,
        .last_path = parent
    };
}

void new_paths(array_t * paths) {
    if (paths->num == 0) {
        path_t *path = path_alloc(paths, 1);
        *path = (path_t){
            .position =  (hmm_vec3){.X = 0, .Y = 0.0f, .Z = 0.0f},
            .direction = (hmm_vec3){.X = 0, .Y = 1.0f, .Z = 0.0f},
            .up =  (hmm_vec3){.X =  0, .Y = 0.0f, .Z = 1.0f},
            .is_leaf = true,
            .last_path = path
        };
        return;
    }

    const int num_path = paths->num;
    for(int i = 0; i < num_path; i++) {
        path_t *path = path_get(paths, i);;
        if (path->is_leaf) {
            path->is_leaf = false;
            if (!array_can_alloc(paths, 1)) {
                continue;
            }
            new_path(path, path_alloc(paths, 1));
       }
    }
}

void add_cylinder(app_t * app, const path_t * path) {
    const int N = 18;
    if (!array_can_alloc(&app->vertices, N)) {
        return;
    }
    // a 2D triangle
    const float pi = 3.1416f;
    float a = cos(pi / 3.0f);
    float b = sin(pi / 3.0f);
    hmm_vec3 va = HMM_MultiplyVec3f((hmm_vec3){0.0f, 0.0f, -1.0f}, 0.1f);
    hmm_vec3 vb = HMM_MultiplyVec3f((hmm_vec3){  -b, 0.0f,     a}, 0.1f);
    hmm_vec3 vc = HMM_MultiplyVec3f((hmm_vec3){   b, 0.0f,     a}, 0.1f);
 
    hmm_vec3 x, y, z;
    const path_t *last_path = path->last_path;
    axes_from_dir_up(last_path->direction, last_path->up, &x, &y, &z);
    hmm_mat4 m0 = mat_from_axes(x, y, z, path->position);

    hmm_vec3 position = HMM_AddVec3(path->position, path->direction);
    axes_from_dir_up(path->direction, path->up, &x, &y, &z);
    hmm_mat4 m1 = mat_from_axes(x, y, z, position);

    // a three sided cylinder
    hmm_vec3 v0 = transform(m0, va);
    hmm_vec3 v1 = transform(m0, vb);
    hmm_vec3 v2 = transform(m0, vc);
    hmm_vec3 v01 = transform(m1, va); 
    hmm_vec3 v11 = transform(m1, vb);
    hmm_vec3 v21 = transform(m1, vc);

    hmm_vec3 triangles[] = {
        v0, v1, v01,
        v1, v11, v01,
        v1, v2, v11,
        v2, v21, v11,
        v2, v0, v21,
        v0, v01, v21
    };
    assert(sizeof(triangles) / sizeof(hmm_vec3) == N);

    memcpy(vertex_alloc(&app->vertices, N),
        triangles, N * sizeof(hmm_vec3));
}

void new_geometry(app_t * app) {
    for(int i = 0; i < app->paths.num; i++) {
        path_t *path = path_get(&app->paths, i);
        if (path->is_leaf) {
            add_cylinder(app, path);
        }
    }
}

void upload_vertices(app_t * app) {
    size_t size = 
        app->vertices.num * app->floats_per_vertex * sizeof(float);

    if (app->frame > 0) {
        sg_destroy_buffer(app->bind.vertex_buffers[0]);
    }
    sg_buffer vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = (sg_range){vertex_get(&app->vertices, 0), size}
    });
    app->bind = (sg_bindings) {
        .vertex_buffers[0] = vbuf
    };
}

void update(app_t * app) {
    /* rotated model matrix */
    app->rx += 0.1f; app->ry += 0.2f;

    if (app->frame % 60 != 0) {
        return;
    }

    new_paths(&app->paths);
    new_geometry(app);
    upload_vertices(app);
}

void render(app_t * app) {
    params_t vs_params;
    hmm_mat4 rxm = HMM_Rotate(app->rx, HMM_Vec3(1.0f, 0.0f, 0.0f));
    hmm_mat4 rym = HMM_Rotate(app->ry, HMM_Vec3(0.0f, 1.0f, 0.0f));
    hmm_mat4 model = HMM_MultiplyMat4(rxm, rym);

    /* model-view-projection matrix for vertex shader */
    vs_params.mvp = HMM_MultiplyMat4(app->view_proj, model);

    int cur_width, cur_height;
    glfwGetFramebufferSize(app->window, &cur_width, &cur_height);
    sg_begin_default_pass(&app->pass_action, cur_width, cur_height);
    sg_apply_pipeline(app->pip);
    sg_apply_bindings(&app->bind);
    sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, &SG_RANGE(vs_params));
    sg_draw(0, app->vertices.num, 1);
    sg_end_pass();
    sg_commit();
    glfwSwapBuffers(app->window);
    glfwPollEvents();
    app->frame++;
}

void terminate(app_t *app) {
    array_destroy(&app->vertices);
    array_destroy(&app->paths);
    sg_shutdown();
    glfwTerminate();
}

int main() {
    app_t app;
    init(&app);
    while(!should_quit(&app)) {
        update(&app);
        render(&app);
    }
    terminate(&app);
    return 0;
}
