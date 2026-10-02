#pragma once
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <tracy/Tracy.hpp>

// Measurement markers shared by the synchronous and asynchronous builds.
// No scheduling, loading or waiting is performed here.
namespace TextureBenchmark {
inline bool active = false;
inline bool completed = false;
inline std::chrono::steady_clock::time_point finished;
inline uint64_t uploadFence = 0;
inline std::chrono::steady_clock::time_point start;
inline void Begin(const std::string& path) {
    if (path.find("TextureStreaming1000.json") == std::string::npos) return;
    active = true;
    completed = false;
    uploadFence = 0;
    start = std::chrono::steady_clock::now();
    TracyMessageL("BENCHMARK_LOAD_BEGIN");
    FrameMarkStart("Texture scene loading");
    std::cout << "BENCHMARK_LOAD_BEGIN" << std::endl;
}
inline void Ready() {
    if (!active) return;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    TracyPlot("Benchmark load elapsed ms", ms);
    TracyMessageL("BENCHMARK_ALL_TEXTURES_READY");
    FrameMarkEnd("Texture scene loading");
    std::cout << "BENCHMARK_ALL_TEXTURES_READY elapsed_ms=" << ms << std::endl;
    active = false;
    completed = true;
    finished = std::chrono::steady_clock::now();
}
}
