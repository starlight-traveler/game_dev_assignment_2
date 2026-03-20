---
layout: default
title: Design Tradeoffs
---

# Design Tradeoffs

This page collects the main design choices in the current engine and the tradeoffs behind them

## 1. Thin Engine API Over Shared Utility State

Choice

- keep `Engine` as a narrow public facade
- keep the mutable runtime state in `utility`

Files

- `src/Engine.h`
- `src/Engine.cpp`
- `src/Utility.h`
- `src/Utility.cpp`

Why this was chosen

- the public engine interface stays small and easy to explain
- low-level storage details stay out of most gameplay-facing code
- the main loop and object code read from one shared source of truth for delta time and active objects

Tradeoff

- simple to use now, but introduces global-style shared state
- easy for an assignment-scale engine, but less ideal than a fuller dependency-injected runtime for larger projects

Why it still makes sense here

- it minimizes boilerplate
- it matches the assignment requirement to expose engine services like delta time and object management

## 2. Virtual `GameObject` Base Class Instead Of Function-Pointer-Only Objects

Choice

- use an abstract `GameObject` base class with virtual `update`

Files

- `src/GameObject.h`
- `src/GameObject.cpp`
- `src/RtsUnit.h`
- `src/RtsUnit.cpp`
- `docs/objective-b-report.md`

Why this was chosen

- different object types can hold different private state
- update logic stays encapsulated inside the object type
- the engine can store polymorphic objects in one container

Tradeoff

- virtual dispatch has a runtime cost
- objects are heap-allocated through `std::unique_ptr`, which is less cache-friendly than a tightly packed plain-data array

Why it still makes sense here

- the benchmark in Objective B shows the function-pointer path is only slightly faster in isolation
- the virtual-object design is much easier to extend into richer gameplay objects

## 3. Custom Quaternion Class Instead Of Relying Entirely On GLM Quaternions

Choice

- implement a custom `Quaternion` type

Files

- `src/Quaternion.h`
- `src/Quaternion.cpp`

Why this was chosen

- it satisfies the assignment requirement directly
- the rotation math becomes visible and explainable
- axis-angle construction, Hamilton product, conjugation, and matrix conversion are all explicit in the code

Tradeoff

- more code to maintain than using a library type directly
- more room for math bugs if implemented carelessly

Why it still makes sense here

- it demonstrates understanding of the underlying math
- it keeps the engine educational and review-friendly

## 4. `std::vector<std::unique_ptr<GameObject>>` Instead Of A Custom Memory Pool

Choice

- store active objects in a vector of unique pointers

Files

- `src/Utility.cpp`

Why this was chosen

- simple ownership model
- automatic cleanup through RAII
- no custom allocator or free-list logic needed yet

Tradeoff

- object memory is not as tightly packed as a custom pool
- less cache locality than a struct-of-arrays or dense arena design

Why it still makes sense here

- it is acceptable under the assignment
- it keeps object lifetime management clear while the engine is still small

## 5. Composition-Based Renderer Instead Of Inheritance-Heavy Rendering Types

Choice

- `Renderer3D` has-a `ShaderProgram`, `Texture2D`, uniform locations, and a render queue

Files

- `src/Renderer3D.h`
- `src/Renderer3D.cpp`
- `src/ShaderProgram.h`
- `src/ShaderProgram.cpp`
- `src/Texture2D.h`
- `src/Texture2D.cpp`
- `docs/objective-c-guide.md`

Why this was chosen

- rendering state is encapsulated in one class
- gameplay code does not need to manipulate raw shader and texture state directly
- composition keeps the design simpler than multiple inheritance or a renderer-is-a-shader style design

Tradeoff

- the renderer is currently specialized for one world-style render path
- not yet as flexible as a full material system with many shaders and textures per mesh

Why it still makes sense here

- it satisfies Objective C cleanly
- it leaves room to grow into multiple render passes later

## 6. Render Queue Instead Of Immediate Draw Calls Everywhere

Choice

- build `RenderCommand` values first, then execute them in `Renderer3D::drawQueue`

Files

- `src/Renderer3D.h`
- `src/Renderer3D.cpp`

Why this was chosen

- separates "what should be drawn" from "how OpenGL is called"
- centralizes GPU state changes
- creates a natural place to add sorting or batching later

Tradeoff

- slightly more bookkeeping than issuing immediate draw calls
- still not fully optimized because commands are not yet sorted by material or shader

Why it still makes sense here

- it keeps rendering code out of gameplay logic
- it is a strong midpoint between simplicity and extensibility

## 7. Header-Described Mesh Layouts Instead Of Only One Fixed Vertex Format

Choice

- support both a legacy fixed layout and a newer mesh-header format that describes attribute counts

Files

- `src/MeshLoader.cpp`
- `src/Shape.h`
- `src/Shape.cpp`
- `docs/objective-c-guide.md`

Why this was chosen

- it meets the assignment baseline while also supporting the flexible-layout extension
- meshes can evolve beyond positions and normals without rewriting the whole loader

Tradeoff

- more parsing logic
- more attribute setup complexity in `Shape`

Why it still makes sense here

- it future-proofs the mesh path more than a hard-coded format would
- it is still small enough to understand at the code level

## 8. BMP Texture Loading Through SDL Instead Of Broader Image Support

Choice

- use `SDL_LoadBMP` and convert to RGBA before upload

Files

- `src/Texture2D.cpp`

Why this was chosen

- BMP loading is available directly through SDL
- no extra image dependency was needed
- the upload path stays simple and explicit

Tradeoff

- BMP is less flexible and less storage-efficient than formats like PNG
- no alpha-aware content pipeline or compressed texture pipeline yet

Why it still makes sense here

- it satisfies the assignment requirement with minimal dependency overhead
- the texture upload process stays easy to explain in review

## 9. One Texture Per `Renderer3D` Instead Of A Material System

Choice

- bind one renderer-owned texture before the draw loop

Files

- `src/Renderer3D.cpp`
- `src/Texture2D.cpp`
- `src/shaders/world.frag`

Why this was chosen

- the texture-unit logic stays simple
- the renderer demonstrates `sampler2D`, `glActiveTexture`, `glBindTexture`, and `glUniform1i` cleanly

Tradeoff

- limits per-object material variety
- not yet suitable for a large game with many materials or texture swaps per frame

Why it still makes sense here

- it is enough for the current assignment demo
- the design can later evolve into texture handles or material descriptors inside each render command

## 10. Scene Graph Separate From The Object Pool

Choice

- do not store the scene graph as the same structure as the active object memory

Files

- `src/SceneGraph.h`
- `src/SceneGraph.cpp`
- `docs/objective-e-guide.md`

Why this was chosen

- simulation storage and spatial organization solve different problems
- objects can remain easy to update while the scene graph is rebuilt or traversed independently
- matches the assignment guidance directly

Tradeoff

- data must be synchronized from objects into the scene graph
- there is duplication between simulation identity and spatial representation

Why it still makes sense here

- it keeps the engine modular
- it avoids forcing all object memory organization to match spatial-query needs

## 11. Transform Tree Plus BVH Instead Of One Structure Doing Everything

Choice

- use the `SceneGraph` tree for parent-child transform inheritance
- use a BVH for broad spatial queries

Files

- `src/SceneGraph.h`
- `src/SceneGraph.cpp`
- `docs/objective-e-guide.md`

Why this was chosen

- parent-child transforms and spatial acceleration are related but distinct jobs
- one tree preserves hierarchical motion
- one BVH accelerates query pruning

Tradeoff

- more logic than a single naive list
- the engine has to maintain both world-transform propagation and BVH rebuild behavior

Why it still makes sense here

- it addresses the assignment’s transform and spatial requirements at the same time
- it keeps the explanation of each subsystem cleaner

## 12. Rebuild-The-BVH-Each-Frame Instead Of Incremental Updates

Choice

- gather active object-backed nodes and rebuild the spatial index every frame

Files

- `src/SceneGraph.cpp`

Why this was chosen

- straightforward implementation
- easier to reason about than mutation-heavy insert, remove, and rebalance logic
- consistent with the assignment’s note that rebuilding can be acceptable

Tradeoff

- extra per-frame work
- not as efficient as a more advanced dynamic BVH for very large scenes

Why it still makes sense here

- the current engine scale is small enough that clarity matters more than absolute performance
- it is easier to verify and explain during review

## 13. Bounding Spheres Expanded To AABBs Instead Of Tighter Oriented Bounds

Choice

- each object stores a `bounding_radius`
- the BVH expands that radius into an axis-aligned box

Files

- `src/SceneGraph.h`
- `src/SceneGraph.cpp`

Why this was chosen

- simple per-object broad bound
- cheap overlap tests
- easy to derive from position plus radius

Tradeoff

- less precise than oriented boxes or mesh-aware bounds
- may include more empty space, so some queries do extra work

Why it still makes sense here

- broad-phase structures favor cheap tests over perfect tightness
- the current implementation stays small and readable

## 14. SDL Callback Audio Instead Of A More Complex Audio Graph

Choice

- use SDL’s callback-driven audio device and mix active sounds into the callback buffer

Files

- `src/Sound.h`
- `src/Sound.cpp`
- `src/SoundSystem.h`
- `src/SoundSystem.cpp`

Why this was chosen

- fits SDL well
- the engine can preload sounds and trigger them from gameplay events
- the mixer logic stays explicit and contained

Tradeoff

- limited compared to a full bus-based or effect-chain audio engine
- current format support and mixing features are intentionally narrow

Why it still makes sense here

- it satisfies the assignment requirement for event-driven object-based playback
- it is enough for the current demo and easy to reason about

## 15. Singleton SDL Manager Instead Of Passing Context Objects Everywhere

Choice

- use `SDL_Manager::sdl()` as a singleton owner of SDL lifecycle and window/context state

Files

- `src/SDL_Manager.h`
- `src/SDL_Manager.cpp`

Why this was chosen

- simple global access to window and OpenGL context management
- avoids threading SDL state through every subsystem

Tradeoff

- singleton access increases coupling
- less flexible than explicit ownership and dependency passing

Why it still makes sense here

- the engine is small and assignment-oriented
- the multi-window viewer is easier to manage with one central SDL owner

## 16. Current Theme Across The Whole Engine

The main pattern in these choices is consistent

- prefer clarity over maximum optimization
- prefer composition over inheritance-heavy designs
- separate subsystems by responsibility
- keep assignment features explicit and explainable
- leave room for later extension without building a full production engine too early

That is the strongest high-level explanation of the project’s architecture
