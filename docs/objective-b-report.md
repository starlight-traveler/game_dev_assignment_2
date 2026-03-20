---
layout: default
title: Objective B Report
---

# Objective B Report

This page documents the current Objective B implementation and the benchmark report required by the assignment

## Short Answer

Yes, the current codebase already completes the main implementation requirements for Objective B

It includes

- an abstract `GameObject` base class
- private object state for position, rotation, render id, and velocities
- a custom quaternion class
- engine-managed storage of active `GameObject` instances
- a benchmark comparing two polymorphism strategies
- a unit test covering quaternion rotation, delta-time access, and object update plus lookup


## What In The Code Satisfies Objective B

### 1. Abstract GameObject Class

The `GameObject` class is polymorphic because it defines a pure virtual update function

```cpp
virtual ~GameObject() = default;
virtual void update(float delta_seconds) = 0;
```

Source: `src/GameObject.h:22-28`

The class also stores the required baseline object state

```cpp
glm::vec3 position_;
Quaternion rotation_;
std::uint32_t render_element_;
glm::vec3 linear_velocity_;
glm::vec3 angular_velocity_;
```

Source: `src/GameObject.h:69-73`

That matches the assignment requirement for

- position
- quaternion rotation
- render element id
- linear velocity
- angular velocity

### 2. Model Matrix And Render Lookup

The assignment asked for a model matrix interface and a safe getter for the render element

The model matrix is implemented here

```cpp
const glm::mat4 translation = glm::translate(glm::mat4(1.0f), position_);
const glm::mat4 rotation = static_cast<glm::mat4>(rotation_);
return translation * rotation;
```

Source: `src/GameObject.cpp:16-20`

And the render id getter is here

```cpp
std::uint32_t GameObject::getRenderElement() const {
    return render_element_;
}
```

Source: `src/GameObject.cpp:22-24`

### 3. Active GameObject Storage

The assignment asked for an array-like managed collection of active objects

The current code uses a `std::vector<std::unique_ptr<GameObject>>`

```cpp
std::vector<std::unique_ptr<GameObject>> g_game_objects;
```

Source: `src/Utility.cpp:13`

This satisfies the assignment language that a `std::vector` is acceptable

Update traversal also matches the required engine-managed loop

```cpp
for (const std::unique_ptr<GameObject>& object : g_game_objects) {
    if (!object) {
        continue;
    }
    object->update(delta_seconds);
}
```

Source: `src/Utility.cpp:49-55`

### 4. Custom Quaternion Class

The assignment required a custom quaternion for full credit

The current code does that in `src/Quaternion.cpp`

Axis-angle construction

```cpp
const float half_angle = angle_radians * 0.5f;
const float sin_half = glm::sin(half_angle);
const float cos_half = glm::cos(half_angle);
quaternion_.x = normalized_axis.x * sin_half;
quaternion_.y = normalized_axis.y * sin_half;
quaternion_.z = normalized_axis.z * sin_half;
quaternion_.w = cos_half;
```

Source: `src/Quaternion.cpp:22-31`

Hamilton product

```cpp
const float x = (w1 * x2) + (x1 * w2) + (y1 * z2) - (z1 * y2);
const float y = (w1 * y2) - (x1 * z2) + (y1 * w2) + (z1 * x2);
const float z = (w1 * z2) + (x1 * y2) - (y1 * x2) + (z1 * w2);
const float w = (w1 * w2) - (x1 * x2) - (y1 * y2) - (z1 * z2);
```

Source: `src/Quaternion.cpp:48-53`

Vector rotation through `q * p * q.conjugate()`

```cpp
const Quaternion point(rhs.x, rhs.y, rhs.z, 0.0f);
const Quaternion rotated = (*this) * point * conjugate();
```

Source: `src/Quaternion.cpp:56-59`

### 5. One Concrete Derived Object Type

The current derived type is `RtsUnit`

```cpp
void RtsUnit::update(float delta_seconds) {
    integrateVelocity(delta_seconds);
    integrateAngularVelocity(delta_seconds);
}
```

Source: `src/RtsUnit.cpp:11-14`

This is enough to demonstrate the polymorphic update path

### 6. Objective B Test Coverage

The current unit test checks the key mathematical and engine behaviors for Objective B

- quaternion rotation
- delta-time API passthrough
- object update plus model lookup

The test executable is in `tests/ObjectiveB_test.cpp`

## Benchmark Report

The assignment required benchmarking two polymorphism approaches and choosing one

The current benchmark tool already does that in `tools/ObjectiveB_Benchmark.cpp`

It compares

1. runtime polymorphism using `GameObject` plus virtual dispatch
2. a function-pointer-based object update approach

### Benchmark Methodology

Independent variable

- update dispatch mechanism
  - virtual dispatch through `GameObject`
  - direct function pointer stored in a plain data object

Dependent variable

- elapsed time per iteration measured in microseconds

Controlled variables

- object count was fixed at 50
- iteration count was fixed at 10,000
- delta time per update call was fixed at `0.016f`
- motion data was the same in both methods

These values come directly from the benchmark tool

```cpp
const std::size_t object_count = 50;
const std::size_t iteration_count = 10000;
const float delta_seconds = 0.016f;
```

Source: `tools/ObjectiveB_Benchmark.cpp:149-151`

The benchmark collects one time sample per iteration and writes both methods to CSV

```cpp
file << "iteration,virtual_dispatch_us,function_pointer_us\n";
for (std::size_t i = 0; i < virtual_samples.size(); ++i) {
    file << i << "," << virtual_samples[i] << "," << function_samples[i] << "\n";
}
```

Source: `tools/ObjectiveB_Benchmark.cpp:134-137`

### Metric

The metric is elapsed time per outer benchmark iteration

- units: microseconds
- recorded for 10,000 iterations
- output file: [objective_b_benchmark.csv](/Users/ryanpaillet/Documents/game_dev/game_dev_assignment_2/docs/objective_b_benchmark.csv)

### Actual Results

I ran the benchmark in the current workspace and generated `docs/objective_b_benchmark.csv`

Measured summary

| Method | Average us | Min us | Max us |
| --- | ---: | ---: | ---: |
| Virtual dispatch | 1.093890 | 0.958000 | 3.667000 |
| Function pointer | 0.894950 | 0.791000 | 16.333000 |

These values came from summarizing the generated CSV after the benchmark completed

### Visual Comparison

The chart below compares the average cost of the two methods

<svg width="520" height="180" viewBox="0 0 520 180" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Objective B average benchmark times">
  <rect x="0" y="0" width="520" height="180" fill="#f8f8f4"/>
  <text x="24" y="26" font-size="18" font-family="Georgia, serif" fill="#222">Average Update Cost per Iteration</text>
  <text x="24" y="48" font-size="12" font-family="Georgia, serif" fill="#555">Lower is better, measured in microseconds</text>
  <text x="24" y="92" font-size="14" font-family="Georgia, serif" fill="#222">Virtual dispatch</text>
  <rect x="170" y="76" width="262" height="22" fill="#3f6c51"/>
  <text x="440" y="92" font-size="12" font-family="Georgia, serif" fill="#222">1.0939 us</text>
  <text x="24" y="136" font-size="14" font-family="Georgia, serif" fill="#222">Function pointer</text>
  <rect x="170" y="120" width="214" height="22" fill="#b86a42"/>
  <text x="392" y="136" font-size="12" font-family="Georgia, serif" fill="#222">0.8950 us</text>
</svg>

### Interpretation

The benchmark shows that the function-pointer approach is slightly faster on average in this very small isolated test

However, the assignment is not only about raw dispatch speed

The codebase also needs

- object-specific state
- encapsulated update logic
- easy growth into additional object types

That matters because the assignment itself warns that function pointers alone are not enough when future object types need extra memory and specialized state

### Conclusion

The current code should continue using the virtual `GameObject` approach as the main engine path

Reasoning

- it already satisfies the assignment cleanly
- it supports subclass-specific state naturally
- it keeps update logic encapsulated
- the measured performance gap in this benchmark is small
- the function-pointer approach remains useful as a comparison baseline, but it is not the best long-term engine structure

In other words

> The benchmark shows that function-pointer dispatch can be a little faster in isolation, but the current engine correctly chooses the virtual `GameObject` design because it is more extensible and better aligned with how future game object types will grow.

## Reproduction Steps

To regenerate the benchmark CSV

```bash
cmake --build build --target objective_b_benchmark
./build/objective_b_benchmark docs/objective_b_benchmark.csv
```

To rerun the Objective B unit tests

```bash
cmake --build build --target objective_b_test
./build/objective_b_test
```

## Final Assessment

So the answer is

- yes, the implementation side of Objective B is present
- yes, the benchmark comparison is present
- yes, the tests are present
- the written report is now included in the docs site
