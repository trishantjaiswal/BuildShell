# 🧠 C++ Memory Tracker  
### A Runtime Memory Leak Detection Tool Built from First Principles

> *Understanding memory is more important than memorizing tools.*

---

## 📌 Overview

**Memory Tracker** is a lightweight C++ runtime tool that detects **memory leaks and incorrect deallocations** by intercepting global dynamic memory operations (`new`, `delete`, `new[]`, `delete[]`).

Instead of relying on external debuggers like Valgrind, this project focuses on **building the core mechanism from scratch** to deeply understand how memory tracking tools work at runtime.

The implementation is intentionally **low-level, explicit, and transparent**, emphasizing correctness and runtime behavior over abstraction.

---

## 🎯 Why This Project Matters

Most C++ projects *use* memory debugging tools.  
This project **builds one**.

It demonstrates:
- A clear understanding of the **heap allocation lifecycle**
- Correct global operator overloading in modern C++
- Handling of subtle runtime pitfalls (recursion, STL self-allocation)
- A systems-level debugging mindset

---

## ✨ Features

- Intercepts **all global dynamic allocations**
- Tracks allocation metadata at runtime:
  - address
  - size
  - allocation type (`new` vs `new[]`)
- Detects:
  - memory leaks
  - mismatched deallocations
  - invalid / double deletes
- Automatically prints a memory report at program exit
- Clean separation between interface (`.h`) and implementation (`.cpp`)

---

## 🧠 Design Highlights

### 1. Global `new` / `delete` Interception
The tracker overrides all global allocation and deallocation operators, ensuring **complete coverage** of heap usage.

### 2. Malloc-based Internal Storage
To avoid infinite recursion caused by STL containers allocating memory internally, the tracker uses:
- a custom `malloc`-based allocator
- a `thread_local` re-entrancy guard

This prevents the tracker from tracking its **own allocations**.

### 3. Safe Shutdown Reporting
Memory reports are printed using **C-style I/O (`fprintf`)** instead of `std::cout` to avoid undefined behavior during static destruction.

This mirrors how real-world runtime tools are implemented.

---

## 🛠 Build & Run

### Compile
```bash
g++ -std=c++17 -O0 -Iinclude src/memory_tracker.cpp examples/leak_demo.cpp -o demo
 Run ./demo
```

## Sample Output
Total allocations : 3
Total frees       : 2
Bytes allocated   : 48
Bytes freed       : 44

MEMORY LEAKS DETECTED
Leaked block at 0x55c8f2a3c2a0 of size 4 bytes

## Non-Obvious Challenges Solved
-This project explicitly handles several advanced C++ pitfalls:
-Sized delete overloads introduced in C++14
-Infinite recursion in global operator overloading
-STL self-allocation during tracking
-Unsafe I/O during program teardown

## Author 
Mohit Gunani
IIT BHU
