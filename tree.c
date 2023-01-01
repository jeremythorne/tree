#include "mymath.h"

#include "renderer.h"

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct timespec timespec_t;

timespec_t now() {
    timespec_t now;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    return now;
}

long elapsed_ns(timespec_t start) {
    timespec_t end = now();
    return (long)(end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
}

struct path_s;

typedef struct path_s {
    vec3s position;
    vec3s direction;
    vec3s up;
    float radius;
    bool is_leader;
    bool is_leaf;
    size_t last_path;
    size_t tree;
} path_t;

#define POD
#define NOT_INTEGRAL
#define T path_t
#include <ctl/vector.h>

typedef struct tree_s {
    bool has_leader; 
    vec3s origin;
    float radius;
} tree_t;

#define POD
#define NOT_INTEGRAL
#define T tree_t
#include <ctl/vector.h>

typedef struct {
    GLFWwindow * window;
    long frame;
    renderer_t *renderer;
    bool is_growing;
    size_t num_trees;
    vec_tree_t trees;
    vec_path_t paths;
} app_t;

path_t create_shoot(vec3s origin, size_t tree) {
    return (path_t){
        .position =  glms_vec3_add(origin, (vec3s){.x = 0, .y = 0.0f, .z = 0.0f}),
        .direction = (vec3s){.x = 0, .y = 0.1f, .z = 0.0f},
        .up =  (vec3s){.x =  0, .y = 0.0f, .z = 1.0f},
        .radius = 0.01f,
        .is_leader = true,
        .is_leaf = true,
        .tree = tree
    };
}

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

    *app = (app_t){
        .frame = 0,
        .window = w,
        .renderer = renderer,
        .num_trees = 16,
        .trees = vec_tree_t_init(),
        .paths = vec_path_t_init(),
        .is_growing = true,
    };

    int A = (int)sqrt(app->num_trees);
    float off = (A - 1.0f) / 2.0f;

    for(int x = 0; x < A; x++) {
        for(int z = 0; z < A; z++) {
            int tree = x * A + z;
            vec3s root_pos =
                    glms_vec3_add(
                        rand_vec(1.0f), 
                        glms_vec3_scale(
                            glms_vec3_add(
                                (vec3s){x, 0.0f, z},
                                (vec3s){-off, 0.0f, -off}),
                            2.0f));
            root_pos.y = 0.0f;
            path_t path = create_shoot(root_pos, tree);
            vec_path_t_push_back(&app->paths, path);
            vec_tree_t_push_back(&app->trees, 
                (tree_t){
                    .has_leader = true,
                    .origin = root_pos,
                    .radius = 0.0f
            });
        }
    }

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
        .tree = parent->tree,
        .last_path = parent_index
    };
}

void radial_growth(vec_path_t * paths, vec_tree_t * trees) {
    foreach(vec_path_t, paths, it) {
        path_t *path = it.ref;
        bool has_leader = vec_tree_t_at(trees, path->tree)->has_leader;
        path->radius += path->is_leader || !has_leader ? 0.001f : 0.0001f;
    }
}

void new_paths(vec_path_t * paths, vec_tree_t * trees) {
    const int num_paths = vec_path_t_size(paths);
    for(int i = 0; i < num_paths; i++) {
        path_t *path = vec_path_t_at(paths, i);
        if (path->is_leaf) {
            path->is_leaf = false;
            int n = rand_prob(path->is_leader ? 0.2f : 0.05f) ? 2 : 1;
            float radius = 0.01f;
            float radii[] = {radius, radius};
            tree_t * tree = vec_tree_t_at(trees, path->tree);
            
            vec3s position = path->position;
            float horiz_dist_from_root = glms_vec3_norm(
                glms_vec3_sub(
                    (vec3s){position.x, 0.0f, position.z},
                    tree->origin));
            tree->radius = tree->radius > horiz_dist_from_root ? tree->radius : horiz_dist_from_root;

            bool has_leader = tree->has_leader;
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
    if (path->is_leaf) {
        renderer_add_leaves(renderer, m1, r1);
    }
}

void new_geometry(app_t * app) {
    renderer_clear_vertices(app->renderer);
    foreach(vec_path_t, &app->paths, it) {
        path_t *path = it.ref;
        path_t *last_path = vec_path_t_at(&app->paths, path->last_path);
        add_cylinder(app->renderer, last_path, path);
    }
    foreach(vec_tree_t, &app->trees, it) {
        tree_t *tree = it.ref;
        renderer_add_contact_shadow(app->renderer, tree->origin, tree->radius);
    }
    renderer_add_ground_plane(app->renderer, 60.0f);
}

void update(app_t * app) {
    renderer_update(app->renderer);

    if (app->frame % 60 != 0) {
        return;
    }

    if (!app->is_growing) {
        return;
    }

    timespec_t start = now();

    radial_growth(&app->paths, &app->trees);
    foreach(vec_tree_t, &app->trees, it) {
        tree_t *tree = it.ref;
        tree->has_leader = tree->has_leader && rand_prob(0.98f);
    }
    new_paths(&app->paths, &app->trees);
    new_geometry(app);
    renderer_upload_vertices(app->renderer);
    
    if (elapsed_ns(start) > 1e8) {
        // stop growing if taking more thn 100ms
        printf("stopped growing\n");
        app->is_growing = false;
    }
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
