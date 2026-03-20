---
layout: default
title: Architecture
---

# Engine Architecture

This page explains the engine as a set of connected subsystems rather than as isolated files

## 1. Entry Point And Startup

The main executable starts in `src/main.cpp`

```cpp
quill::Backend::start();
quill::Frontend::create_or_get_logger("sdl", console_sink);
SDL_Manager* sdl_ptr = nullptr;
sdl_ptr = &SDL_Manager::sdl();
initialize();
```

Source: `src/main.cpp:879-899`

This startup sequence establishes four things immediately

- logging exists before the engine starts doing work
- SDL is initialized through the singleton `SDL_Manager`
- the user hook `initialize()` is called
- the rest of the engine can assume there is a valid runtime environment

The user hook is intentionally tiny right now

```cpp
void initialize() {
    // objective A boot hook for future scene setup
}
```

Source: `src/AppLayer.cpp:6-8`

That means the current engine is still framework-first rather than gameplay-first

## 2. Public Engine API Layer

The public API is intentionally narrow and lives in `src/Engine.h` and `src/Engine.cpp`

Its job is not to implement systems itself

Its job is to expose a clean API over the lower-level utility state and object storage

Example

```cpp
std::uint64_t getDeltaTime() {
    return utility::deltaTimeMs();
}

float getDeltaSeconds() {
    return utility::deltaSeconds();
}
```

Source: `src/Engine.cpp:9-15`

And object spawning is just a thin wrapper over `RtsUnit` creation

```cpp
auto object = std::make_unique<RtsUnit>(render_element, position, linear_velocity, angular_velocity);
return utility::addGameObject(std::move(object)) > 0;
```

Source: `src/Engine.cpp:17-23`

So architecturally the `Engine` layer is a facade

## 3. Runtime State Layer

The real mutable runtime state lives in `src/Utility.cpp`

It stores

- frame delta in milliseconds
- frame delta in seconds
- the vector of active `GameObject` instances

This is the central state bridge between the main loop and the simulation

The important consequence is that `src/main.cpp` calculates time, then the engine stores it once, and all later update code reads from the same source of truth

## 4. Object System

The base type is `GameObject`

```cpp
class GameObject {
public:
    virtual ~GameObject() = default;
    virtual void update(float delta_seconds) = 0;
    glm::mat4 getModel() const;
```

Source: `src/GameObject.h`

This tells you the core object model

- objects are polymorphic
- each object advances itself per frame
- each object can produce a model matrix for rendering

The current concrete object is `RtsUnit`

```cpp
void RtsUnit::update(float delta_seconds) {
    integrateVelocity(delta_seconds);
    integrateAngularVelocity(delta_seconds);
}
```

Source: `src/RtsUnit.cpp:11-14`

So the current simulation model is intentionally simple

- linear velocity updates position
- angular velocity updates orientation

## 5. Transform And Spatial Layer

The file pair `src/SceneGraph.h` and `src/SceneGraph.cpp` is one of the most important architectural pieces

It does two separate jobs

1. parent-child transform propagation
2. BVH-based spatial indexing

The class comment explains the intent directly

```cpp
 * 1. keep a parent-child transform tree so child nodes inherit parent motion
 * 2. build a bounding volume hierarchy over active object nodes for broad spatial queries
```

Source: `src/SceneGraph.h`

This distinction matters

- the transform tree answers "where is this object in world space after inheritance"
- the BVH answers "which objects are worth testing or rendering in this region"

The per-frame integration point is here

```cpp
scene_graph.setLocalTransformByObject(item.window_id,
                                      getModelForRenderElement(item.window_id));
scene_graph.updateWorldTransforms();
scene_graph.rebuildSpatialIndex();
```

Source: `src/main.cpp:500-510`

That three-step sequence is the center of the engine

## 6. Rendering Layer

Rendering is distributed across several files with very clear roles

- `MeshDiscovery`
  Finds candidate mesh files on disk
- `MeshLoader`
  Parses `.meshbin`
- `Shape`
  Owns CPU-side mesh arrays plus OpenGL VAO and VBO handles
- `ShaderProgram`
  Compiles and links GLSL
- `Texture2D`
  Loads BMP textures or creates a checker fallback
- `Renderer3D`
  Queues and submits draw commands

The renderer itself is intentionally small

```cpp
for (const RenderCommand& command : queue_) {
    glBindVertexArray(command.shape->getVAO());
    glUniformMatrix4fv(proj_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.projection));
    glUniformMatrix4fv(view_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.view));
    glUniformMatrix4fv(model_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.model));
    glDrawArrays(GL_TRIANGLES, 0, command.shape->getVertexCount());
}
```

Source: `src/Renderer3D.cpp:69-88`

So the renderer is queue-based, but still deliberately straightforward

## 7. Window And Context Layer

`src/SDL_Manager.cpp` owns SDL lifecycle and all OpenGL contexts

Important design decisions

- it is a singleton
- each window gets its own OpenGL context
- the first window acts like the main window for quitting

Context creation happens here

```cpp
SDL_Window *window =
    SDL_CreateWindow(title.c_str(), x, y, width, height, flags);
SDL_GLContext context = SDL_GL_CreateContext(window);
SDL_GL_MakeCurrent(window, context)
```

Source: `src/SDL_Manager.cpp:157-185`

That design is what enables the multi-window mesh viewer in the main executable

## 8. Audio Layer

Audio uses SDL's callback model instead of a polling model

The callback bridge is explicit

```cpp
void sound_callback(void* userdata, Uint8* stream, int len) {
    auto* sound_system = static_cast<SoundSystem*>(userdata);
    sound_system->mixToStream(stream, len);
}
```

Source: `src/SoundSystem.cpp:22-28`

Architecturally this means

- sounds are preloaded or loaded on demand
- playback requests are queued
- the audio device thread pulls data through the callback

## 9. Demo Layer

The Doom-style demo in `demo/DoomDemo.cpp` is not a separate engine

It is a concrete example of using the same core pieces

- SDL window creation
- renderer setup
- `SceneGraph`
- mesh loading
- sound playback
- gameplay state

That is useful because it shows how the engine modules are expected to be composed by future game code