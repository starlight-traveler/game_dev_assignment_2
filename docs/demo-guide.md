---
layout: default
title: Doom Demo
---

# Doom Demo Walkthrough

The current gameplay demo in `demo/DoomDemo.cpp` is important because it shows how the engine is meant to be used in practice

It is not a separate engine

It is a concrete composition of the same core systems

## 1. What The Demo Adds

Compared with the mesh viewer in `src/main.cpp`, the demo adds

- first-person camera control
- player health
- enemy chase logic
- hitscan shooting
- win and lose states
- simple UI through the window title and crosshair

## 2. Engine Systems Reused By The Demo

The demo still uses

- `SDL_Manager`
- `Renderer3D`
- `MeshLoader`
- `SceneGraph`
- `SoundSystem`

That is the biggest architectural reason the demo matters

It proves the engine systems are reusable

## 3. Static World Setup

The arena is just a set of transformed box meshes

```cpp
const glm::mat4 floor_model =
    glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.7f, 0.0f)) *
    glm::scale(glm::mat4(1.0f), glm::vec3(18.0f, 0.8f, 18.0f));
```

Source: `demo/DoomDemo.cpp`

The same pattern is used for all four walls

This is a good example of how the engine uses transform matrices as the universal representation of placed geometry

## 4. Dynamic Enemy Setup

The enemy is inserted into the `SceneGraph` as one object with a radius

That radius does two jobs

- it defines the broad BVH bound
- it defines the hitscan target sphere

This is a simple but effective reuse of one conceptual measurement

## 5. Input Model

The demo uses two input styles

- polling for continuous keyboard movement
- events for mouse motion and fire requests

That is a useful detail to mention in an explanation because it shows the engine is not tied to one rigid input style yet

## 6. Enemy Gameplay Logic

The enemy is intentionally simple

1. chase the player if outside melee range
2. clamp to arena
3. deal contact damage if close enough and the cooldown expired

This logic lives in `update_enemy_logic`

That makes the demo a good example of gameplay code living above the engine systems rather than inside them

## 7. Shooting Model

The weapon is hitscan rather than projectile-based

The demo computes

- eye position
- forward direction
- ray-sphere intersection against the enemy

This is a lightweight example of gameplay math using the same camera and vector foundations as the engine

## 8. Render Flow In The Demo

The demo follows the same broad render flow as the main executable

1. update gameplay state
2. update enemy transform
3. update world transforms
4. rebuild BVH
5. ask for visible object ids
6. translate ids into render commands
7. draw

That makes the demo a useful teaching tool because the high-level loop is easier to see than in the multi-window viewer

## 9. Why The Demo Matters For Future RTS Work

Even though the demo is FPS-style rather than RTS-style, it still validates important engine parts that an RTS will need

- reusable mesh loading
- robust render command submission
- transform hierarchy
- spatial queries
- audio events
- frame-based updates

So the demo is less about genre and more about system integration