---
layout: default
title: Home
---

# Assignment 2 Engine Guide

This documentation set replaces the older assignment-1 site and describes the engine exactly as it exists in the current codebase

The current project is a small C++ game engine skeleton aimed at RTS-style engine goals with two visible entry points

- `src/main.cpp` runs the multi-window mesh viewer and engine loop
- `demo/DoomDemo.cpp` runs a small gameplay demo built on the same rendering and spatial systems

The fastest way to understand the project is to read these pages in order

1. [Architecture](./architecture.html)
2. [Runtime Flow](./runtime-flow.html)
3. [Module Reference](./module-reference.html)
4. [Mathematics](./mathematics.html)
5. [Objective B Report](./objective-b-report.html)
6. [Doom Demo Walkthrough](./demo-guide.html)

## What The Engine Currently Does

At a high level, the engine already supports

- frame delta tracking
- an engine-facing `GameObject` API
- velocity and angular-velocity updates
- a custom quaternion rotation class
- mesh discovery and mesh loading from `.meshbin`
- OpenGL rendering with VAO and VBO management
- texture and shader setup
- multi-window SDL plus OpenGL context management
- event-driven audio playback through an SDL callback mixer
- a transform hierarchy with BVH-based spatial queries

## The Single Most Important Idea

The codebase is easiest to understand if you keep this data flow in mind

```text
SDL window + input
    -> Engine frame delta
    -> GameObject updates
    -> model matrices
    -> SceneGraph world transforms
    -> BVH rebuild
    -> render queue
    -> OpenGL draw calls
```

That exact flow is visible in `src/main.cpp`

```cpp
updateActiveGameObjects();
sync_scene_graph_with_objects(render_items, scene_graph);
render_all_windows(sdl, elapsed_seconds, clear_color, scene_graph, render_items);
sdl.updateWindows();
```

Source: `src/main.cpp:1064-1071`

## Project Layout

The most important folders are

- `src/`
  The engine code and the main executable
- `demo/`
  The gameplay demo built on the engine
- `tests/`
  Objective tests for object math and the scene structure
- `tools/`
  Small benchmark support code
- `blender/`
  Mesh assets and the helper script that produced `.meshbin` files

## Recommended Reading Strategy

If you are explaining the engine in a review meeting, walk through it in this order

1. `src/main.cpp`
2. `src/Engine.cpp`
3. `src/GameObject.cpp`
4. `src/SceneGraph.cpp`
5. `src/Renderer3D.cpp`
6. `src/MeshLoader.cpp` and `src/Shape.cpp`
7. `src/SoundSystem.cpp`
8. `demo/DoomDemo.cpp`

That order mirrors how data moves during execution

## Current Scope And Missing Systems

This is an engine skeleton, not a complete RTS engine yet

It does **not** currently include

- pathfinding
- unit selection
- command issuing
- terrain systems
- physics resolution
- animation blending
- networking

What it does include is the mathematical and architectural base those systems would build on
