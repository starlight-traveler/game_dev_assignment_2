/**
 * @file DoomDemo.cpp
 * @brief Basic Doom-style box fight demo built on top of the assignment engine systems
 */
#include <GL/glew.h>
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "MeshLoader.h"
#include "Renderer3D.h"
#include "SceneGraph.h"
#include "SDL_Manager.h"
#include "Shape.h"
#include "SoundSystem.h"

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/sinks/ConsoleSink.h"

// short sleep literals like 1ms come from this namespace
using namespace std::chrono_literals;

namespace {
// stable ids used to register static world objects in the scene graph
constexpr std::uint32_t kFloorObjectId = 1001;
constexpr std::uint32_t kWallNorthObjectId = 1002;
constexpr std::uint32_t kWallSouthObjectId = 1003;
constexpr std::uint32_t kWallEastObjectId = 1004;
constexpr std::uint32_t kWallWestObjectId = 1005;
constexpr std::uint32_t kEnemyObjectId = 2001;

// half-width of the square arena measured from world origin in X and Z
constexpr float kArenaHalfExtent = 8.5f;
// simple radius used when clamping the player to the arena
constexpr float kPlayerRadius = 0.35f;
// simple sphere radius used for the enemy broad bounds and hit test
constexpr float kEnemyRadius = 0.9f;
// distance where the enemy is considered to be in melee range
constexpr float kEnemyContactRange = 1.35f;

/**
 * @brief Runtime state for the first-person player
 */
struct PlayerState {
    // world-space player feet position
    glm::vec3 position;
    // yaw rotates around the up axis
    float yaw_deg;
    // pitch rotates camera up and down
    float pitch_deg;
    // walking speed in world units per second
    float move_speed;
    // mouse sensitivity measured as degrees per pixel
    float look_sensitivity;
    // current player health
    int health;
    // remaining time before the next shot is allowed
    float shot_cooldown_s;
};

/**
 * @brief Runtime state for one enemy target
 */
struct EnemyState {
    // world-space enemy position
    glm::vec3 position;
    // current enemy health
    int health;
    // chase speed in world units per second
    float speed;
    // cooldown between enemy contact hits
    float attack_cooldown_s;
};

/**
 * @brief Renderable object metadata linked to BVH nodes
 */
struct RenderableObject {
    // raw pointer is safe here because Shape objects are owned for the whole demo lifetime
    const Shape* shape;
    // whether the mesh provides texture coordinates at attribute location 2
    bool use_mesh_uv;
};

/**
 * @brief Returns a shared logger for this translation unit
 * @return Pointer to logger instance
 */
quill::Logger* get_logger() {
    // try to reuse the logger created during startup
    quill::Logger* logger = quill::Frontend::get_logger("sdl");
    if (!logger) {
        // if startup has not created one yet, make a fallback console logger
        auto console_sink =
            quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
        logger = quill::Frontend::create_or_get_logger("sdl", console_sink);
    }
    // return the logger pointer so helper functions can log safely
    return logger;
}

/**
 * @brief Finds the first existing path from an ordered list
 * @param candidates Candidate path strings
 * @return First existing path or empty string
 */
std::string find_first_existing_path(const std::vector<std::string>& candidates) {
    // scan the list in priority order
    for (const std::string& path : candidates) {
        // choose the first file that actually exists on disk
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    // no candidate worked
    return {};
}

/**
 * @brief Builds perspective projection for a window
 * @param window Window to query for drawable size
 * @return Projection matrix
 */
glm::mat4 build_projection(SDL_Window* window) {
    // safe defaults in case the window is missing
    int width = 1;
    int height = 1;
    if (window) {
        // drawable size is preferred because it respects high-DPI back buffers
        SDL_GL_GetDrawableSize(window, &width, &height);
        if (width <= 0 || height <= 0) {
            // if drawable size fails, fall back to logical window size
            SDL_GetWindowSize(window, &width, &height);
        }
    }
    // guard against divide-by-zero when computing aspect ratio
    width = std::max(width, 1);
    height = std::max(height, 1);
    // standard aspect ratio math
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    // field of view, aspect ratio, near plane, far plane
    return glm::perspective(glm::radians(72.0f), aspect, 0.05f, 120.0f);
}

/**
 * @brief Computes normalized camera forward direction from yaw and pitch
 * @param yaw_deg Yaw in degrees
 * @param pitch_deg Pitch in degrees
 * @return Unit forward direction
 */
glm::vec3 camera_forward(float yaw_deg, float pitch_deg) {
    // glm trig expects radians, not degrees
    const float yaw = glm::radians(yaw_deg);
    const float pitch = glm::radians(pitch_deg);
    glm::vec3 direction{};
    // spherical-angle style camera math
    direction.x = std::cos(pitch) * std::cos(yaw);
    direction.y = std::sin(pitch);
    direction.z = std::cos(pitch) * std::sin(yaw);
    // keep movement and ray logic stable by normalizing
    return glm::normalize(direction);
}

/**
 * @brief Computes camera right direction projected to the XZ plane
 * @param yaw_deg Yaw in degrees
 * @return Unit right direction on XZ plane
 */
glm::vec3 camera_right_xz(float yaw_deg) {
    // ignore pitch so strafing stays parallel to the ground
    const glm::vec3 fwd = camera_forward(yaw_deg, 0.0f);
    // cross forward with world up to get a right vector
    glm::vec3 right = glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f));
    // if the vector degenerates, fall back to a safe axis
    if (glm::dot(right, right) <= 0.000001f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    // normalize before using it for movement
    return glm::normalize(right);
}

/**
 * @brief Builds view matrix from first-person state
 * @param player Player state
 * @return View matrix
 */
glm::mat4 build_view(const PlayerState& player) {
    // eye point is slightly above the player base so the view feels first-person
    const glm::vec3 eye = player.position + glm::vec3(0.0f, 0.9f, 0.0f);
    // compute the direction the player is looking
    const glm::vec3 fwd = camera_forward(player.yaw_deg, player.pitch_deg);
    // compute the right vector on the ground plane
    const glm::vec3 right = camera_right_xz(player.yaw_deg);
    // recompute up so the camera basis is orthonormal
    const glm::vec3 up = glm::normalize(glm::cross(right, fwd));
    // build the standard look-at matrix
    return glm::lookAt(eye, eye + fwd, up);
}

/**
 * @brief Keeps the player inside the arena bounds
 * @param player Player state to clamp
 */
void clamp_player_to_arena(PlayerState& player) {
    // account for radius so the player body does not clip through the walls
    const float min_bound = -kArenaHalfExtent + kPlayerRadius;
    const float max_bound = kArenaHalfExtent - kPlayerRadius;
    // clamp horizontal movement to the arena square
    player.position.x = std::clamp(player.position.x, min_bound, max_bound);
    player.position.z = std::clamp(player.position.z, min_bound, max_bound);
    // keep the player grounded
    player.position.y = 0.0f;
}

/**
 * @brief Solves ray-sphere intersection for hitscan weapons
 * @param ray_origin Ray origin in world space
 * @param ray_dir Ray direction normalized
 * @param sphere_center Sphere center in world space
 * @param sphere_radius Sphere radius
 * @return True when the ray intersects the sphere in front of origin
 */
bool ray_hits_sphere(const glm::vec3& ray_origin, const glm::vec3& ray_dir,
                     const glm::vec3& sphere_center, float sphere_radius) {
    // vector from sphere center to ray origin
    const glm::vec3 to_center = ray_origin - sphere_center;
    // quadratic coefficient for the linear term after simplifying with normalized ray direction
    const float b = glm::dot(to_center, ray_dir);
    // quadratic constant term
    const float c = glm::dot(to_center, to_center) - sphere_radius * sphere_radius;
    // discriminant decides whether the ray intersects the sphere at all
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        // negative discriminant means no real intersection
        return false;
    }
    // solve both intersection distances along the ray
    const float sqrt_disc = std::sqrt(discriminant);
    const float t_near = -b - sqrt_disc;
    const float t_far = -b + sqrt_disc;
    // an intersection counts if either solution is in front of the camera
    return t_near >= 0.0f || t_far >= 0.0f;
}

/**
 * @brief Draws a simple crosshair using scissor + clear calls
 * @param window Target window
 */
void draw_crosshair(SDL_Window* window) {
    // cannot draw without a valid window
    if (!window) {
        return;
    }

    // query window size so the crosshair can be centered
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window, &width, &height);
    // center pixel location
    const int cx = width / 2;
    const int cy = height / 2;
    // half-length of each crosshair arm
    const int arm = 9;
    // rectangle thickness for the crosshair lines
    const int thickness = 2;

    // scissor lets the code clear tiny rectangles instead of drawing UI geometry
    glEnable(GL_SCISSOR_TEST);
    auto draw_rect = [height](int x, int y, int w, int h) {
        // OpenGL scissor space starts at the bottom-left, window space here is top-left
        glScissor(x, height - (y + h), w, h);
        // use a bright color so the crosshair stays visible
        glClearColor(0.92f, 0.92f, 0.92f, 1.0f);
        // clear just the scissored rectangle
        glClear(GL_COLOR_BUFFER_BIT);
    };

    // horizontal line of the crosshair
    draw_rect(cx - arm, cy - thickness / 2, arm * 2, thickness);
    // vertical line of the crosshair
    draw_rect(cx - thickness / 2, cy - arm, thickness, arm * 2);
    // restore default state after the overlay draw
    glDisable(GL_SCISSOR_TEST);
}

/**
 * @brief Writes current gameplay health values to window title
 * @param window Window handle
 * @param player Player runtime state
 * @param enemy Enemy runtime state
 * @param has_won Win flag
 * @param has_lost Lose flag
 */
void update_window_title(SDL_Window* window, const PlayerState& player, const EnemyState& enemy,
                         bool has_won, bool has_lost) {
    // no title can be changed if the window is gone
    if (!window) {
        return;
    }

    // default state while the fight is active
    std::string status = "Fight";
    if (has_won) {
        status = "Victory";
    } else if (has_lost) {
        status = "Defeat";
    }

    // show the current state and basic controls in the title bar
    const std::string title =
        "DOOM Demo | State: " + status +
        " | Player HP: " + std::to_string(player.health) +
        " | Enemy HP: " + std::to_string(std::max(enemy.health, 0)) +
        " | WASD move, mouse look, LMB/Space shoot";
    SDL_SetWindowTitle(window, title.c_str());
}

/**
 * @brief Processes player keyboard movement for one frame
 * @param player Player state to mutate
 * @param dt_seconds Delta seconds
 */
void update_player_movement(PlayerState& player, float dt_seconds) {
    // SDL stores the current keyboard state as a flat array
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (!keys) {
        return;
    }

    // accumulate movement direction from input before normalizing
    glm::vec3 move_dir(0.0f);
    // forward and right are computed from yaw only so movement stays on the ground plane
    const glm::vec3 fwd_xz = camera_forward(player.yaw_deg, 0.0f);
    const glm::vec3 right_xz = camera_right_xz(player.yaw_deg);
    if (keys[SDL_SCANCODE_W]) {
        move_dir += fwd_xz;
    }
    if (keys[SDL_SCANCODE_S]) {
        move_dir -= fwd_xz;
    }
    if (keys[SDL_SCANCODE_D]) {
        move_dir += right_xz;
    }
    if (keys[SDL_SCANCODE_A]) {
        move_dir -= right_xz;
    }

    // only move if some key contributed a direction
    if (glm::dot(move_dir, move_dir) > 0.000001f) {
        // normalize so diagonal movement is not faster than straight movement
        move_dir = glm::normalize(move_dir);
        float speed = player.move_speed;
        // left or right shift gives a small speed boost
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
            speed *= 1.45f;
        }
        // standard Euler integration for movement
        player.position += move_dir * speed * dt_seconds;
    }
    // keep the final position inside the arena
    clamp_player_to_arena(player);
}

/**
 * @brief Updates enemy chase and melee attack behavior
 * @param enemy Enemy state to mutate
 * @param player Player state to mutate when damaged
 * @param dt_seconds Delta seconds
 * @param has_won Win flag
 * @param has_lost Lose flag
 */
void update_enemy_logic(EnemyState& enemy, PlayerState& player, float dt_seconds,
                        bool has_won, bool has_lost) {
    // once the enemy is dead or the match is over, skip all enemy logic
    if (enemy.health <= 0 || has_won || has_lost) {
        return;
    }

    // tick the attack cooldown down toward zero
    if (enemy.attack_cooldown_s > 0.0f) {
        enemy.attack_cooldown_s -= dt_seconds;
    }

    // vector pointing from enemy to player
    const glm::vec3 to_player = player.position - enemy.position;
    // scalar distance used for chase and attack decisions
    const float distance = glm::length(to_player);
    if (distance > kEnemyContactRange + 0.1f && distance > 0.00001f) {
        // normalize direction so speed stays constant independent of distance
        const glm::vec3 dir = to_player / distance;
        // move the enemy toward the player
        enemy.position += dir * enemy.speed * dt_seconds;
    }

    // clamp the enemy to the same square arena using the enemy radius
    const float min_bound = -kArenaHalfExtent + kEnemyRadius;
    const float max_bound = kArenaHalfExtent - kEnemyRadius;
    enemy.position.x = std::clamp(enemy.position.x, min_bound, max_bound);
    enemy.position.z = std::clamp(enemy.position.z, min_bound, max_bound);
    // keep the enemy on the floor plane
    enemy.position.y = 0.0f;

    // recompute distance after clamping in case the clamp changed the position
    const glm::vec3 corrected = player.position - enemy.position;
    const float corrected_distance = glm::length(corrected);
    if (corrected_distance <= kEnemyContactRange && enemy.attack_cooldown_s <= 0.0f) {
        // apply contact damage when the enemy is close enough and the cooldown expired
        player.health = std::max(0, player.health - 10);
        // reset the cooldown so damage is not applied every single frame
        enemy.attack_cooldown_s = 0.8f;
    }
}
}  // namespace

/**
 * @brief Entry point for Doom-like demo executable
 * @return Process exit code
 */
int main() {
    // start the asynchronous logging backend before any logging calls
    quill::Backend::start();
    // create a console sink so logs go to stdout or stderr
    auto console_sink =
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
    // register the logger name used by the rest of the project
    quill::Frontend::create_or_get_logger("sdl", console_sink);

    // use the shared SDL singleton for window and GL context ownership
    SDL_Manager* sdl_ptr = nullptr;
    try {
        sdl_ptr = &SDL_Manager::sdl();
    } catch (const std::exception& ex) {
        // if SDL cannot boot, the demo cannot continue
        LOG_ERROR(get_logger(), "SDL manager init failed: {}", ex.what());
        return 1;
    }
    // unwrap the singleton pointer into a reference for cleaner use below
    SDL_Manager& sdl = *sdl_ptr;

    // create one gameplay window at a fixed size and location
    if (!sdl.spawnWindowAt("DOOM Demo", 1280, 720, 140, 90, SDL_TRUE)) {
        LOG_ERROR(get_logger(), "Failed to spawn demo window");
        return 1;
    }
    // bind that window's GL context before any renderer or mesh upload work
    if (!sdl.makeOpenGLCurrentAt(0)) {
        LOG_ERROR(get_logger(), "Failed to bind OpenGL context for demo window");
        return 1;
    }

    // relative mouse mode hides the cursor and reports deltas, which is ideal for FPS look
    SDL_SetRelativeMouseMode(SDL_TRUE);
    // request vsync to reduce tearing and keep the demo frame pacing smoother
    SDL_GL_SetSwapInterval(1);

    // renderer owns the shader program, texture, and queued draw flow
    Renderer3D renderer{};
    // locate the vertex shader in either run-from-root or run-from-build layouts
    const std::string vertex_shader = find_first_existing_path({
        (std::filesystem::path("src") / "shaders" / "world.vert").string(),
        (std::filesystem::path("..") / "src" / "shaders" / "world.vert").string()
    });
    // locate the fragment shader using the same search pattern
    const std::string fragment_shader = find_first_existing_path({
        (std::filesystem::path("src") / "shaders" / "world.frag").string(),
        (std::filesystem::path("..") / "src" / "shaders" / "world.frag").string()
    });
    // locate the surface texture used by the world meshes
    const std::string texture_path = find_first_existing_path({
        (std::filesystem::path("blender") / "surface.bmp").string(),
        (std::filesystem::path("..") / "blender" / "surface.bmp").string()
    });
    // stop immediately if the core render assets cannot be initialized
    if (vertex_shader.empty() || fragment_shader.empty() ||
        !renderer.initialize(vertex_shader, fragment_shader, texture_path)) {
        LOG_ERROR(get_logger(), "Renderer initialization failed for doom demo");
        return 1;
    }

    // pick a box mesh for the room geometry
    const std::string box_mesh_path = find_first_existing_path({
        (std::filesystem::path("blender") / "box.meshbin").string(),
        (std::filesystem::path("..") / "blender" / "box.meshbin").string()
    });
    // pick a monkey mesh for the enemy and fall back to the box if it is missing
    const std::string enemy_mesh_path = find_first_existing_path({
        (std::filesystem::path("blender") / "monkey.meshbin").string(),
        (std::filesystem::path("..") / "blender" / "monkey.meshbin").string(),
        box_mesh_path
    });
    // upload the world and enemy meshes to GPU-backed Shape objects
    std::unique_ptr<Shape> box_mesh = load_mesh_from_meshbin(box_mesh_path);
    std::unique_ptr<Shape> enemy_mesh = load_mesh_from_meshbin(enemy_mesh_path);
    if (!box_mesh || !enemy_mesh) {
        LOG_ERROR(get_logger(), "Failed to load required meshes for doom demo");
        return 1;
    }

    // sound system opens the audio device and keeps a callback mixer alive
    SoundSystem sound_system{};
    // sound library indices default to -1 when the file is unavailable
    int gun_sound_index = -1;
    int hit_sound_index = -1;
    // search for the gunshot wav in several convenient content folders
    const std::string gun_wav = find_first_existing_path({
        (std::filesystem::path("audio") / "gunshot.wav").string(),
        (std::filesystem::path("assets") / "gunshot.wav").string(),
        (std::filesystem::path("blender") / "gunshot.wav").string()
    });
    // search for the hit confirm wav in the same way
    const std::string hit_wav = find_first_existing_path({
        (std::filesystem::path("audio") / "hit.wav").string(),
        (std::filesystem::path("assets") / "hit.wav").string(),
        (std::filesystem::path("blender") / "hit.wav").string()
    });
    // preload the gun sound if audio is available
    if (sound_system.isReady() && !gun_wav.empty() && sound_system.loadSound(gun_wav)) {
        gun_sound_index = 0;
    }
    // preload the hit sound next so it gets the next library slot
    if (sound_system.isReady() && !hit_wav.empty() && sound_system.loadSound(hit_wav)) {
        hit_sound_index = (gun_sound_index >= 0) ? 1 : 0;
    }

    // scene graph stores transform hierarchy plus the BVH used for broad culling
    SceneGraph scene_graph{};
    // leaf size is small because the demo only has a handful of objects
    scene_graph.setMaxLeafObjects(4);

    // map object ids to render metadata so the render queue can turn ids into draw calls
    std::unordered_map<std::uint32_t, RenderableObject> world_objects{};
    // reserve a little extra space to avoid a rehash during setup
    world_objects.reserve(8);
    // register the floor using the shared box mesh
    world_objects.emplace(kFloorObjectId, RenderableObject{
        box_mesh.get(), box_mesh->hasAttribute(2)});
    // register the north wall
    world_objects.emplace(kWallNorthObjectId, RenderableObject{
        box_mesh.get(), box_mesh->hasAttribute(2)});
    // register the south wall
    world_objects.emplace(kWallSouthObjectId, RenderableObject{
        box_mesh.get(), box_mesh->hasAttribute(2)});
    // register the east wall
    world_objects.emplace(kWallEastObjectId, RenderableObject{
        box_mesh.get(), box_mesh->hasAttribute(2)});
    // register the west wall
    world_objects.emplace(kWallWestObjectId, RenderableObject{
        box_mesh.get(), box_mesh->hasAttribute(2)});
    // register the enemy mesh
    world_objects.emplace(kEnemyObjectId, RenderableObject{
        enemy_mesh.get(), enemy_mesh->hasAttribute(2)});

    // floor transform places a thin box slightly below the origin and scales it into the arena floor
    const glm::mat4 floor_model =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.7f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(18.0f, 0.8f, 18.0f));
    // north wall transform
    const glm::mat4 wall_north_model =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.15f, -9.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(18.0f, 3.4f, 0.8f));
    // south wall transform
    const glm::mat4 wall_south_model =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.15f, 9.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(18.0f, 3.4f, 0.8f));
    // east wall transform
    const glm::mat4 wall_east_model =
        glm::translate(glm::mat4(1.0f), glm::vec3(9.0f, 1.15f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.8f, 3.4f, 18.0f));
    // west wall transform
    const glm::mat4 wall_west_model =
        glm::translate(glm::mat4(1.0f), glm::vec3(-9.0f, 1.15f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.8f, 3.4f, 18.0f));

    // insert static world geometry into the transform tree and BVH source list
    scene_graph.createNode(scene_graph.rootNodeId(), kFloorObjectId, floor_model, 13.0f);
    scene_graph.createNode(scene_graph.rootNodeId(), kWallNorthObjectId, wall_north_model, 9.5f);
    scene_graph.createNode(scene_graph.rootNodeId(), kWallSouthObjectId, wall_south_model, 9.5f);
    scene_graph.createNode(scene_graph.rootNodeId(), kWallEastObjectId, wall_east_model, 9.5f);
    scene_graph.createNode(scene_graph.rootNodeId(), kWallWestObjectId, wall_west_model, 9.5f);

    // initialize the first-person player state
    PlayerState player{};
    // spawn near the south side of the room facing inward
    player.position = glm::vec3(0.0f, 0.0f, 6.5f);
    // yaw -90 points roughly down the negative Z axis in this setup
    player.yaw_deg = -90.0f;
    // a slight downward pitch helps show the floor and enemy immediately
    player.pitch_deg = -8.0f;
    // walking speed
    player.move_speed = 5.2f;
    // mouse sensitivity
    player.look_sensitivity = 0.12f;
    // full health at spawn
    player.health = 100;
    // player can fire immediately
    player.shot_cooldown_s = 0.0f;

    // initialize the single enemy state
    EnemyState enemy{};
    // spawn the enemy near the opposite side of the room
    enemy.position = glm::vec3(0.0f, 0.0f, -4.0f);
    // full enemy health
    enemy.health = 100;
    // chase speed
    enemy.speed = 2.05f;
    // enemy can attack immediately if close enough
    enemy.attack_cooldown_s = 0.0f;

    // add the enemy as a dynamic scene object with a scaled model matrix
    scene_graph.createNode(
        scene_graph.rootNodeId(),
        kEnemyObjectId,
        glm::translate(glm::mat4(1.0f), enemy.position) * glm::scale(glm::mat4(1.0f), glm::vec3(0.85f)),
        kEnemyRadius);

    // these flags drive the game loop and title text
    bool has_won = false;
    bool has_lost = false;
    bool exit = false;
    bool fire_requested = false;

    // steady clock avoids issues with wall-clock adjustments
    using Clock = std::chrono::steady_clock;
    // initialize the previous-frame timestamp for delta time math
    Clock::time_point prev_time = Clock::now();
    // reusable SDL event object
    SDL_Event event{};

    // main game loop
    while (!exit) {
        // measure frame time at the very start of the loop
        const Clock::time_point now = Clock::now();
        float dt_seconds = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;
        // clamp huge frame hitches so movement and cooldowns do not jump wildly
        dt_seconds = std::clamp(dt_seconds, 0.0f, 0.050f);

        // process all pending SDL events before simulation
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                // OS-level close request
                exit = true;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    // user closed the single demo window
                    exit = true;
                } else if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                           event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    // update the OpenGL viewport when the window size changes
                    int draw_w = 1;
                    int draw_h = 1;
                    SDL_Window* window = sdl.windowAt(0);
                    if (window) {
                        SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
                        glViewport(0, 0, std::max(draw_w, 1), std::max(draw_h, 1));
                    }
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    // escape exits the demo
                    exit = true;
                }
                if (event.key.keysym.sym == SDLK_SPACE) {
                    // pressing space queues a shot for this frame
                    fire_requested = true;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    // left click also queues a shot
                    fire_requested = true;
                }
                break;
            case SDL_MOUSEMOTION:
                // relative mouse movement rotates the camera
                player.yaw_deg += static_cast<float>(event.motion.xrel) * player.look_sensitivity;
                // invert Y so moving the mouse upward looks upward
                player.pitch_deg -= static_cast<float>(event.motion.yrel) * player.look_sensitivity;
                // clamp pitch to avoid flipping over
                player.pitch_deg = std::clamp(player.pitch_deg, -85.0f, 85.0f);
                break;
            default:
                // all other events are ignored for this small demo
                break;
            }
        }

        // apply keyboard movement after event processing
        update_player_movement(player, dt_seconds);
        // update enemy chase and melee logic
        update_enemy_logic(enemy, player, dt_seconds, has_won, has_lost);

        // tick the player weapon cooldown down toward zero
        if (player.shot_cooldown_s > 0.0f) {
            player.shot_cooldown_s -= dt_seconds;
        }

        // fire if input requested a shot and the gameplay state allows it
        if (fire_requested && !has_won && !has_lost && player.shot_cooldown_s <= 0.0f) {
            if (gun_sound_index >= 0) {
                // play the gunshot sound if it was preloaded successfully
                sound_system.playSound(gun_sound_index);
            }
            // fire from eye height rather than from the floor position
            const glm::vec3 eye = player.position + glm::vec3(0.0f, 0.9f, 0.0f);
            // use the current view direction as the shot direction
            const glm::vec3 dir = camera_forward(player.yaw_deg, player.pitch_deg);
            // test the enemy hit sphere only if the enemy is still alive
            if (enemy.health > 0 && ray_hits_sphere(eye, dir, enemy.position + glm::vec3(0.0f, 0.75f, 0.0f),
                                                    kEnemyRadius)) {
                // subtract damage but never go below zero
                enemy.health = std::max(0, enemy.health - 25);
                if (hit_sound_index >= 0) {
                    // play a hit confirmation sound when the ray connects
                    sound_system.playSound(hit_sound_index);
                }
            }
            // enforce a short delay before the next shot
            player.shot_cooldown_s = 0.23f;
        }
        // clear the one-frame fire request after evaluating it
        fire_requested = false;

        if (enemy.health <= 0 && !has_won) {
            // mark the win state only once
            has_won = true;
            // remove the enemy from the spatial structure so it stops rendering
            scene_graph.removeNodeByObject(kEnemyObjectId);
        }
        if (player.health <= 0 && !has_lost) {
            // player death transitions the demo into the lose state
            has_lost = true;
        }

        if (enemy.health > 0) {
            // rebuild the enemy model matrix from current position and facing direction
            const glm::mat4 enemy_model =
                glm::translate(glm::mat4(1.0f), enemy.position) *
                glm::rotate(glm::mat4(1.0f), std::atan2(player.position.x - enemy.position.x,
                                                        player.position.z - enemy.position.z),
                            glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.85f));
            // push the new enemy transform into the scene structure
            scene_graph.setLocalTransformByObject(kEnemyObjectId, enemy_model);
        }

        // propagate parent-child transforms through the hierarchy
        scene_graph.updateWorldTransforms();
        // rebuild the BVH over the active object bounds for current-frame culling
        scene_graph.rebuildSpatialIndex();

        // fetch the single demo window for viewport and projection math
        SDL_Window* window = sdl.windowAt(0);
        int draw_w = 1;
        int draw_h = 1;
        if (window) {
            // drawable size is the actual framebuffer size
            SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
        }
        // build the camera projection matrix
        const glm::mat4 projection = build_projection(window);
        // build the camera view matrix from the current player state
        const glm::mat4 view = build_view(player);
        // clear the frame and set up depth testing
        renderer.beginFrame(std::max(draw_w, 1), std::max(draw_h, 1), 0.06f, 0.06f, 0.08f);

        // collect visible objects from the scene graph
        std::vector<std::uint32_t> render_queue{};
        scene_graph.render(render_queue, player.position, 40.0f);
        // place the light slightly above the player so the room stays lit around the camera
        const glm::vec3 light_position = player.position + glm::vec3(0.0f, 3.4f, 0.0f);
        for (std::uint32_t object_id : render_queue) {
            // translate object ids into the actual mesh pointer and UV flag
            const auto object_it = world_objects.find(object_id);
            if (object_it == world_objects.end() || !object_it->second.shape) {
                continue;
            }

            // package all draw data for this object into one render command
            RenderCommand command{};
            command.shape = object_it->second.shape;
            command.model = scene_graph.worldTransformForObject(object_id);
            command.view = view;
            command.projection = projection;
            command.light_position = light_position;
            command.use_mesh_uv = object_it->second.use_mesh_uv;
            // queue the draw instead of issuing it immediately
            renderer.enqueue(command);
        }
        // submit the queued world draw calls
        renderer.drawQueue();
        // draw the simple 2D crosshair overlay
        draw_crosshair(window);
        // update the window title with current health and match state
        update_window_title(window, player, enemy, has_won, has_lost);

        // swap the back buffer to the screen
        sdl.updateWindows();
        if (sdl.shouldQuit() || sdl.windowCount() == 0) {
            // if SDL requests shutdown or the window disappears, end the loop
            exit = true;
        }

        // tiny sleep keeps the loop from spinning unnecessarily hard in this assignment demo
        std::this_thread::sleep_for(1ms);
    }

    // normal exit path
    return 0;
}
