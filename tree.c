#include "mymath.h"

#include "renderer.h"

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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
    GLFWwindow * window;
    long frame;
    renderer_t *renderer;
    bool is_growing;
    bool has_leader;
    vec_path_t paths;
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

    renderer_t * renderer = renderer_init(WIDTH, HEIGHT);

    int num_paths = 1000;
    *app = (app_t){
        .frame = 0,
        .window = w,
	.renderer = renderer,
	.paths = vec_path_t_init(),
        .is_growing = true,
        .has_leader = true,
    };
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

void add_cylinder(renderer_t * renderer, const path_t * last_path, const path_t * path) {
    vec3s x, y, z;
    axes_from_dir_up(last_path->direction, last_path->up, &x, &y, &z);
    mat4s m0 = mat_from_axes(x, y, z, path->position);
    float r0 = last_path->radius;

    vec3s position = glms_vec3_add(path->position, path->direction);
    axes_from_dir_up(path->direction, path->up, &x, &y, &z);
    mat4s m1 = mat_from_axes(x, y, z, position);
    float r1 = path->radius;

    renderer_add_cylinder(renderer, m0, r0, m1, r1);
}

void new_geometry(app_t * app) {
    renderer_clear_vertices(app->renderer);
    foreach(vec_path_t, &app->paths, it) {
        path_t *path = it.ref;
		path_t *last_path = vec_path_t_at(&app->paths, path->last_path);
        add_cylinder(app->renderer, last_path, path);
    }
}

void update(app_t * app) {
	renderer_update(app->renderer);

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
    renderer_upload_vertices(app->renderer);
}

void render(app_t * app) {
    int cur_width, cur_height;
    glfwGetFramebufferSize(app->window, &cur_width, &cur_height);
    renderer_render(app->renderer, cur_width, cur_height);
    glfwSwapBuffers(app->window);
    glfwPollEvents();
    app->frame++;
}

void terminate(app_t *app) {
	renderer_free(&app->renderer);
    vec_path_t_free(&app->paths);
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
