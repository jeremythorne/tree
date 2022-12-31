#include "mymath.h"

#define SOKOL_IMPL
#define SOKOL_GLES3
#include "sokol_gfx.h"
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
/* a uniform block with a model-view-projection matrix */
typedef struct {
    mat4s mvp;
} params_t;

struct path_s;

typedef struct path_s {
    vec3s position;
    vec3s direction;
    vec3s up;
    float radius;
    bool is_leader;
    bool is_leaf;
    size_t last_path;
} path_t;

#define POD
#define NOT_INTEGRAL
#define T path_t
#include <ctl/vector.h>

typedef struct {
    vec3s position;
    vec3s normal;
} vertex_t;

#define POD
#define NOT_INTEGRAL
#define T vertex_t
#include <ctl/vector.h>

typedef struct {
    GLFWwindow * window;
    long frame;
    vec_vertex_t vertices;
    int floats_per_vertex;
    bool is_growing;
    bool has_leader;
    vec_path_t paths;
    sg_bindings bind;
    sg_pipeline pip;
    sg_pass_action pass_action;
    mat4s view_proj;
    float rx;
    float ry;
} app_t;


void init(app_t * app) {
    const int WIDTH = 800;
    const int HEIGHT = 600;

    srand(time(0));

    /* create GLFW window and initialize GL */
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* w = glfwCreateWindow(WIDTH, HEIGHT, "Sokol Cube GLFW", 0, 0);
    glfwMakeContextCurrent(w);
    glfwSwapInterval(1);

    /* setup sokol_gfx */
    sg_desc desc = {0};
    sg_setup(&desc);
    assert(sg_isvalid());

    int num_paths = 1000;
    *app = (app_t){
        .frame = 0,
        .window = w,
        .paths = vec_path_t_init(),
        .floats_per_vertex = sizeof(vertex_t) / sizeof(float),
        .is_growing = true,
        .has_leader = true,
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
            "out vec3 vnormal;\n" 
            "void main() {\n"
            "  vnormal = normal;\n"
            "  gl_Position = mvp * position;\n"
            "}\n",
        .fs.source =
            "#version 310 es\n"
            "precision mediump float;\n"
            "in vec3 vnormal;\n"
            "out vec4 frag_color;\n"
            "void main() {\n"
            "  vec3 light_dir = vec3(0.5, -0.5, 0.0);\n"
            "  vec3 light_colour = vec3(0.9, 0.9, 0.7);\n"
            "  vec3 ambient_colour = vec3(0.7, 0.9, 0.9);\n"
            "  float lambert = dot(light_dir, vnormal);\n"
            "  frag_color = vec4(lambert * light_colour + ambient_colour, 1.0);\n"
            "}\n"
    });

    /* create pipeline object */
    app->pip = sg_make_pipeline(&(sg_pipeline_desc){
        .layout = {
            /* test to provide buffer stride, but no attr offsets */
            .buffers[0].stride = app->floats_per_vertex * 4,
            .attrs = {
                [0].format=SG_VERTEXFORMAT_FLOAT3,
                [1].format=SG_VERTEXFORMAT_FLOAT3,
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
    mat4s proj = glms_perspective(glm_rad(60.0f), (float)WIDTH/(float)HEIGHT, 0.01f, 10.0f);
    mat4s view = glms_lookat((vec3s){0.0f, 2.5f, 6.0f}, (vec3s){0.0f, 1.0f, 0.0f}, (vec3s){0.0f, 1.0f, 0.0f});
    app->view_proj = glms_mat4_mul(proj, view);
}

bool should_quit(app_t * app) {
    return glfwWindowShouldClose(app->window);
}

void new_path(vec_path_t * paths, const size_t parent_index, path_t * child, float radius, bool is_leader,
        bool has_leader) {
    path_t *parent = vec_path_t_at(paths, parent_index);
    vec3s x, y, z;
    axes_from_dir_up(parent->direction, parent->up, &x, &y, &z);
    // perturb direction randomly
    float perturb = is_leader ? 0.1f : 1.0f;
    float length = 0.01f;
    if (is_leader) {
        length = 0.05f;
    } else if(!has_leader) {
        length = 0.03f;
    }

    vec3s direction = glms_vec3_scale( 
        glms_vec3_normalize(glms_vec3_add(
                (vec3s){.x = 0.0f, .y = 0.1f, .z = 0.0f}, // vertical tropism
                glms_vec3_add(y, rand_vec(perturb)))),
                length);

    *child = (path_t){
        .position = glms_vec3_add(parent->position, parent->direction),
        .direction = direction,
        .up = z,
        .radius = radius,
        .is_leader = is_leader,
        .is_leaf = true,
        .last_path = parent_index
    };
}

void radial_growth(vec_path_t * paths, bool has_leader) {
    foreach(vec_path_t, paths, it) {
        path_t *path = it.ref;
        path->radius += path->is_leader || !has_leader ? 0.001f : 0.0001f;
    }
}

path_t the_shoot() {
    return (path_t){
        .position =  (vec3s){.x = 0, .y = 0.0f, .z = 0.0f},
        .direction = (vec3s){.x = 0, .y = 0.1f, .z = 0.0f},
        .up =  (vec3s){.x =  0, .y = 0.0f, .z = 1.0f},
        .radius = 0.01f,
        .is_leader = true,
        .is_leaf = true,
    };
}

bool new_paths(vec_path_t * paths, bool has_leader) {
    if (vec_path_t_empty(paths)) {
        path_t path = the_shoot();
	vec_path_t_push_back(paths, path);
        path_t *p = vec_path_t_back(paths);
	p->last_path = 0;
	return true;
    }

    const int num_paths = vec_path_t_size(paths);
    for(int i = 0; i < num_paths; i++) {
        path_t *path = vec_path_t_at(paths, i);
        if (path->is_leaf) {
            path->is_leaf = false;
            int n = rand_prob(path->is_leader ? 0.2f : 0.05f) ? 2 : 1;
            float radius = 0.01f;
            float radii[] = {radius, radius};
            bool child_is_leader = path->is_leader && has_leader;
            bool is_leader[] = {child_is_leader, false};
            if (path->is_leader && !child_is_leader) {
                printf("stop leader\n");
            }
  
            if (n == 2) {
                radii[0] = path->is_leader ? radius :
                    (rand_float(0.5f) + 0.5f) * radius;
                // sum of children = area of parent
                radii[1] = sqrt(radius * radius - radii[0] * radii[0]);
            }

            for (int j = 0; j < n; j++) {
		vec_path_t_push_back(paths, (path_t){});
                new_path(paths, i, vec_path_t_back(paths), radii[j], is_leader[j],
                        has_leader);
            }
        }
    }
    return true;
}

void add_cylinder(vec_vertex_t * vertices, const path_t * last_path, const path_t * path) {
    const int N = 18;
    // a 2D triangle
    const float pi = 3.1416f;
    float a = cos(pi / 3.0f);
    float b = sin(pi / 3.0f);
    vec3s va = (vec3s){0.0f, 0.0f, -1.0f};
    vec3s vb = (vec3s){  -b, 0.0f,     a};
    vec3s vc = (vec3s){   b, 0.0f,     a};
 
    vec3s x, y, z;
    axes_from_dir_up(last_path->direction, last_path->up, &x, &y, &z);
    mat4s m0 = mat_from_axes(x, y, z, path->position);
    float r0 = last_path->radius;

    vec3s position = glms_vec3_add(path->position, path->direction);
    axes_from_dir_up(path->direction, path->up, &x, &y, &z);
    mat4s m1 = mat_from_axes(x, y, z, position);
    float r1 = path->radius;

    // a three sided cylinder
    vertex_t v0 =  (vertex_t) {transform(m0, glms_vec3_scale(va, r0)), transform_normal(m0, va)};
    vertex_t v1 =  (vertex_t) {transform(m0, glms_vec3_scale(vb, r0)), transform_normal(m0, vb)};
    vertex_t v2 =  (vertex_t) {transform(m0, glms_vec3_scale(vc, r0)), transform_normal(m0, vc)};
    vertex_t v01 = (vertex_t) {transform(m1, glms_vec3_scale(va, r1)), transform_normal(m1, va)}; 
    vertex_t v11 = (vertex_t) {transform(m1, glms_vec3_scale(vb, r1)), transform_normal(m1, vb)};
    vertex_t v21 = (vertex_t) {transform(m1, glms_vec3_scale(vc, r1)), transform_normal(m1, vc)};

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

void new_geometry(app_t * app) {
    vec_vertex_t_clear(&app->vertices);
    foreach(vec_path_t, &app->paths, it) {
        path_t *path = it.ref;
	path_t *last_path = vec_path_t_at(&app->paths, path->last_path);
        add_cylinder(&app->vertices, last_path, path);
    }
}

void upload_vertices(app_t * app) {
    size_t size = 
        vec_vertex_t_size(&app->vertices) * app->floats_per_vertex * sizeof(float);

    if (app->frame > 0) {
        sg_destroy_buffer(app->bind.vertex_buffers[0]);
    }
    sg_buffer vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = (sg_range){vec_vertex_t_data(&app->vertices), size}
    });
    app->bind = (sg_bindings) {
        .vertex_buffers[0] = vbuf
    };
}

void update(app_t * app) {
    /* rotated model matrix */
    // app->rx += 0.1f; 
    app->ry += 0.2f;

    if (app->frame % 60 != 0) {
        return;
    }

    radial_growth(&app->paths, app->has_leader);
    app->has_leader = app->has_leader && rand_prob(0.98f);
    bool is_growing = new_paths(&app->paths, app->has_leader);
    if (!is_growing && app->is_growing) {
        printf("stopped growing\n");
    }
    app->is_growing = is_growing;
    new_geometry(app);
    upload_vertices(app);
}

void render(app_t * app) {
    params_t vs_params;
    mat4s rxm = glms_quat_mat4(glms_quatv(glm_rad(app->rx), (vec3s){1.0f, 0.0f, 0.0f}));
    mat4s rym = glms_quat_mat4(glms_quatv(glm_rad(app->ry), (vec3s){0.0f, 1.0f, 0.0f}));
    mat4s model = glms_mat4_mul(rxm, rym);

    /* model-view-projection matrix for vertex shader */
    vs_params.mvp = glms_mat4_mul(app->view_proj, model);

    int cur_width, cur_height;
    glfwGetFramebufferSize(app->window, &cur_width, &cur_height);
    sg_begin_default_pass(&app->pass_action, cur_width, cur_height);
    sg_apply_pipeline(app->pip);
    sg_apply_bindings(&app->bind);
    sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, &SG_RANGE(vs_params));
    sg_draw(0, vec_vertex_t_size(&app->vertices), 1);
    sg_end_pass();
    sg_commit();
    glfwSwapBuffers(app->window);
    glfwPollEvents();
    app->frame++;
}

void terminate(app_t *app) {
    vec_vertex_t_free(&app->vertices);
    vec_path_t_free(&app->paths);
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
