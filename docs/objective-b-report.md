---
layout: default
title: Objective B Report
---

# Objective B Report

Objective B implementation notes and benchmark results for the current codebase

## Objective B Status

The current codebase completes the main implementation requirements for Objective B

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

### Methodology

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

The methodology is intentionally controlled

- both methods update 50 objects
- both methods run for 10,000 benchmark iterations
- both methods use the same position, linear velocity, angular velocity, and quaternion rotation logic
- the only intended difference is how the update call is dispatched

That makes the dispatch strategy the independent variable and the elapsed time per iteration the dependent variable

The two code paths are structurally similar by design

Virtual dispatch path

```cpp
for (const std::unique_ptr<GameObject>& object : objects) {
    object->update(delta_seconds);
}
```

Source: `tools/ObjectiveB_Benchmark.cpp:62-64`

Function-pointer path

```cpp
for (FunctionObject& object : objects) {
    object.update_fn(object, delta_seconds);
}
```

Source: `tools/ObjectiveB_Benchmark.cpp:100-102`

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

In other words, each recorded sample is the time required to update all 50 objects once under one dispatch strategy

### Actual Results

The benchmark was run in the current workspace and generated `docs/objective_b_benchmark.csv`

Measured summary

| Method | Average us | Median us | Min us | P95 us | Max us | Std Dev us |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Virtual dispatch | 1.093890 | 1.084000 | 0.958000 | 1.167000 | 3.667000 | 0.052175 |
| Function pointer | 0.894950 | 0.875000 | 0.791000 | 0.917000 | 16.333000 | 0.294683 |

These values came from summarizing the generated CSV after the benchmark completed

Important reading of the table

- the function-pointer path is faster on average
- the median result also favors the function-pointer path
- the function-pointer path has much larger worst-case spikes and much larger variance
- the virtual path is slower on average, but the spread of results is much tighter

### Graphs

The first chart compares the average cost of the two methods

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

The second chart shows 100 bucketed averages across the full 10,000-iteration run

- each plotted point is the average of 100 consecutive iterations
- lower lines are better
- the chart shows both the general separation between the methods and the occasional instability spikes

<svg width="700" height="340" viewBox="0 0 700 340" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Objective B bucketed benchmark trend">
  <rect x="0" y="0" width="700" height="340" fill="#f8f8f4"/>
  <text x="28" y="28" font-size="18" font-family="Georgia, serif" fill="#222">Bucketed Iteration Trend</text>
  <text x="28" y="48" font-size="12" font-family="Georgia, serif" fill="#555">100 points, each averaging 100 benchmark iterations</text>
  <line x1="60" y1="40" x2="60" y2="260" stroke="#777" stroke-width="1"/>
  <line x1="60" y1="260" x2="620" y2="260" stroke="#777" stroke-width="1"/>
  <line x1="60" y1="40" x2="620" y2="40" stroke="#ddd" stroke-width="1"/>
  <line x1="60" y1="150" x2="620" y2="150" stroke="#e5e5e5" stroke-width="1"/>
  <line x1="60" y1="260" x2="620" y2="260" stroke="#ddd" stroke-width="1"/>
  <text x="18" y="44" font-size="11" font-family="Georgia, serif" fill="#555">1.223</text>
  <text x="18" y="154" font-size="11" font-family="Georgia, serif" fill="#555">1.022</text>
  <text x="18" y="264" font-size="11" font-family="Georgia, serif" fill="#555">0.821</text>
  <text x="60" y="282" font-size="11" font-family="Georgia, serif" fill="#555">0</text>
  <text x="320" y="282" font-size="11" font-family="Georgia, serif" fill="#555">5,000</text>
  <text x="590" y="282" font-size="11" font-family="Georgia, serif" fill="#555">10,000</text>
  <polyline fill="none" stroke="#3f6c51" stroke-width="2"
    points="60.0,40.0 65.7,43.4 71.3,63.0 77.0,109.8 82.6,110.7 88.3,106.0 93.9,108.2 99.6,110.1 105.3,109.7 110.9,107.4 116.6,102.8 122.2,106.7 127.9,109.1 133.5,110.1 139.2,105.1 144.8,95.5 150.5,87.3 156.2,92.3 161.8,104.9 167.5,105.5 173.1,92.7 178.8,100.0 184.4,108.6 190.1,107.1 195.8,103.9 201.4,108.2 207.1,109.1 212.7,107.9 218.4,110.6 224.0,111.0 229.7,106.9 235.4,108.9 241.0,104.4 246.7,109.5 252.3,108.4 258.0,110.5 263.6,106.0 269.3,109.8 274.9,107.8 280.6,106.1 286.3,106.4 291.9,107.6 297.6,105.5 303.2,107.5 308.9,105.9 314.5,107.8 320.2,106.7 325.9,108.1 331.5,110.0 337.2,109.0 342.8,109.2 348.5,108.2 354.1,110.7 359.8,105.7 365.5,105.8 371.1,107.6 376.8,108.4 382.4,109.6 388.1,107.7 393.7,107.3 399.4,106.6 405.1,106.5 410.7,107.5 416.4,109.6 422.0,107.1 427.7,108.2 433.3,111.7 439.0,108.9 444.6,109.6 450.3,106.4 456.0,105.5 461.6,102.3 467.3,109.6 472.9,106.0 478.6,109.7 484.2,109.1 489.9,106.9 495.6,111.1 501.2,102.9 506.9,104.2 512.5,106.7 518.2,109.1 523.8,107.9 529.5,108.5 535.2,105.5 540.8,104.9 546.5,108.9 552.1,94.1 557.8,120.8 563.4,154.5 569.1,158.4 574.7,157.7 580.4,158.4 586.1,158.1 591.7,159.1 597.4,157.7 603.0,159.3 608.7,155.8 614.3,154.7 620.0,158.6"/>
  <polyline fill="none" stroke="#b86a42" stroke-width="2"
    points="60.0,222.4 65.7,224.0 71.3,220.6 77.0,219.9 82.6,221.2 88.3,221.3 93.9,224.7 99.6,222.4 105.3,220.8 110.9,222.2 116.6,222.4 122.2,223.3 127.9,221.5 133.5,227.4 139.2,219.9 144.8,224.4 150.5,225.6 156.2,224.9 161.8,216.2 167.5,221.5 173.1,227.0 178.8,220.3 184.4,225.6 190.1,220.6 195.8,227.6 201.4,220.3 207.1,219.9 212.7,220.6 218.4,224.9 224.0,223.1 229.7,220.3 235.4,224.2 241.0,226.5 246.7,221.5 252.3,219.9 258.0,221.5 263.6,217.8 269.3,215.6 274.9,220.6 280.6,222.4 286.3,219.0 291.9,219.9 297.6,223.5 303.2,222.6 308.9,219.2 314.5,223.8 320.2,133.5 325.9,218.3 331.5,223.8 337.2,219.4 342.8,201.7 348.5,220.4 354.1,225.3 359.8,218.5 365.5,217.8 371.1,223.5 376.8,222.6 382.4,221.5 388.1,218.8 393.7,219.2 399.4,222.8 405.1,224.0 410.7,221.0 416.4,220.3 422.0,221.0 427.7,219.7 433.3,221.3 439.0,214.0 444.6,221.2 450.3,221.9 456.0,224.0 461.6,223.1 467.3,220.1 472.9,218.3 478.6,146.1 484.2,217.6 489.9,174.1 495.6,221.7 501.2,211.0 506.9,211.2 512.5,199.6 518.2,208.9 523.8,213.7 529.5,221.0 535.2,220.3 540.8,148.8 546.5,145.8 552.1,220.3 557.8,224.7 563.4,222.6 569.1,227.0 574.7,218.8 580.4,222.8 586.1,226.3 591.7,260.0 597.4,259.8 603.0,257.5 608.7,254.5 614.3,259.8 620.0,260.0"/>
  <circle cx="640" cy="90" r="5" fill="#3f6c51"/>
  <text x="652" y="94" font-size="12" font-family="Georgia, serif" fill="#222">Virtual dispatch</text>
  <circle cx="640" cy="114" r="5" fill="#b86a42"/>
  <text x="652" y="118" font-size="12" font-family="Georgia, serif" fill="#222">Function pointer</text>
</svg>

### Interpretation

The benchmark shows that the function-pointer approach is slightly faster on average in this very small isolated test

The graph also shows a second useful detail that the average alone hides

- the virtual-dispatch results cluster tightly in a narrow band
- the function-pointer results are usually lower, but they spike more sharply

So the result is not simply "one method is always better"

- function-pointer dispatch wins on average cost
- virtual dispatch shows more stable timing in this specific benchmark run

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
- the measured average advantage of the function-pointer method is roughly `0.199 us` per 50-object iteration
- the function-pointer approach remains useful as a comparison baseline, but it is not the best long-term engine structure

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