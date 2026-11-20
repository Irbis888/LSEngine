#pragma once
#include <Windows.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "ThirdParty/renderdoc/renderdoc_app.h"

// Opt-in frame timing for render measurements. Enabled only when LS_BENCHMARK_CSV is set:
//   LS_BENCHMARK_CSV       output CSV path (one row per measured frame)
//   LS_BENCHMARK_WARMUP    frames after the scene is ready that are not recorded (default 300)
//   LS_BENCHMARK_FRAMES    recorded frames (default 1000); then the application quits
//   LS_RENDERDOC_CAPTURE=1 also capture the first measured frame when launched under RenderDoc
//                          (renderdoccmd capture ...); the application quits with code 4 without it
// The header is self-contained so the same file can be dropped into older engine revisions.
class FrameBenchmark
{
public:
    static FrameBenchmark& Instance()
    {
        static FrameBenchmark benchmark;
        return benchmark;
    }

    bool Enabled() const
    {
        return !mOutputPath.empty();
    }

    // Called once per loop iteration before any frame work.
    void FrameBoundary()
    {
        if (!Enabled())
        {
            return;
        }
        const Clock::time_point now = Clock::now();
        if (mState == State::Measuring && mHasFrameStart)
        {
            Sample sample;
            sample.frameMilliseconds = Milliseconds(mFrameStart, now);
            sample.cpuFrameMilliseconds = mCpuFrameMilliseconds;
            sample.drawMilliseconds = mDrawMilliseconds;
            mSamples.push_back(sample);
            if (mSamples.size() >= mMeasuredFrames)
            {
                Finish();
            }
        }
        if (mState == State::Warmup && mSceneReady)
        {
            ++mWarmupDone;
            if (mWarmupDone >= mWarmupFrames)
            {
                mState = State::Measuring;
                if (mRenderDocCapture)
                {
                    TriggerRenderDocCapture();
                }
            }
        }
        mFrameStart = now;
        mHasFrameStart = true;
        mDrawMilliseconds = 0.0;
        mCpuFrameMilliseconds = 0.0;
    }

    // Scene and resource loading finished; warmup starts counting from here.
    // A load that starts during warmup restarts the warmup.
    void SetSceneReady(bool ready)
    {
        if (!Enabled())
        {
            return;
        }
        if (ready && mState == State::Waiting)
        {
            mSceneReady = true;
            mState = State::Warmup;
            std::cout << "FRAME_BENCHMARK_SCENE_READY" << std::endl;
        }
        else if (!ready && mState == State::Warmup)
        {
            mSceneReady = false;
            mWarmupDone = 0;
            mState = State::Waiting;
        }
    }

    void BeginDraw()
    {
        mDrawStart = Clock::now();
    }

    void EndDraw()
    {
        mDrawMilliseconds += Milliseconds(mDrawStart, Clock::now());
    }

    // Called after the frame was submitted and presented.
    void EndCpuFrame()
    {
        if (mHasFrameStart)
        {
            mCpuFrameMilliseconds = Milliseconds(mFrameStart, Clock::now());
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    enum class State
    {
        Waiting,
        Warmup,
        Measuring,
        Done
    };

    struct Sample
    {
        double frameMilliseconds = 0.0;
        double cpuFrameMilliseconds = 0.0;
        double drawMilliseconds = 0.0;
    };

    FrameBenchmark()
    {
        mOutputPath = ReadVariable("LS_BENCHMARK_CSV");
        const std::string warmup = ReadVariable("LS_BENCHMARK_WARMUP");
        const std::string frames = ReadVariable("LS_BENCHMARK_FRAMES");
        if (!warmup.empty())
        {
            mWarmupFrames = std::stoul(warmup);
        }
        if (!frames.empty())
        {
            mMeasuredFrames = std::stoul(frames);
            if (mMeasuredFrames == 0)
            {
                mMeasuredFrames = 1;
            }
        }
        mSamples.reserve(mMeasuredFrames);
        mRenderDocCapture = ReadVariable("LS_RENDERDOC_CAPTURE") == "1";
    }

    // RenderDoc injects renderdoc.dll when it launches the process; the engine never loads it itself.
    void TriggerRenderDocCapture()
    {
        HMODULE module = GetModuleHandleA("renderdoc.dll");
        if (module != nullptr)
        {
            const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
            if (getApi != nullptr && getApi(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void**>(&mRenderDoc)) == 1)
            {
                // Captures the frame that starts now, up to its Present.
                mRenderDoc->TriggerCapture();
                std::cout << "RENDERDOC_CAPTURE_TRIGGERED" << std::endl;
                return;
            }
        }
        std::cout << "RENDERDOC_NOT_LOADED: launch through renderdoccmd capture" << std::endl;
        mRenderDocMissing = true;
    }

    void ReportRenderDocCaptures()
    {
        if (mRenderDoc == nullptr)
        {
            return;
        }
        const uint32_t count = mRenderDoc->GetNumCaptures();
        for (uint32_t index = 0; index < count; ++index)
        {
            uint32_t length = 0;
            mRenderDoc->GetCapture(index, nullptr, &length, nullptr);
            std::string path(length, '\0');
            mRenderDoc->GetCapture(index, path.data(), &length, nullptr);
            path.resize(path.find('\0') == std::string::npos ? path.size() : path.find('\0'));
            std::cout << "RENDERDOC_CAPTURE " << path << std::endl;
        }
    }

    static std::string ReadVariable(const char* name)
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
        {
            return {};
        }
        std::string result(value);
        free(value);
        return result;
    }

    static double Milliseconds(Clock::time_point begin, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }

    void Finish()
    {
        mState = State::Done;
        std::ofstream file(mOutputPath, std::ios::trunc);
        file << "frame,frame_ms,cpu_frame_ms,draw_ms\n";
        for (size_t index = 0; index < mSamples.size(); ++index)
        {
            const Sample& sample = mSamples[index];
            file << index << ',' << sample.frameMilliseconds << ',' << sample.cpuFrameMilliseconds << ','
                << sample.drawMilliseconds << '\n';
        }
        file.close();
        std::cout << (file ? "FRAME_BENCHMARK_DONE " : "FRAME_BENCHMARK_WRITE_FAILED ") << mOutputPath << std::endl;
        ReportRenderDocCaptures();
        int exitCode = 0;
        if (!file)
        {
            exitCode = 3;
        }
        else if (mRenderDocMissing)
        {
            exitCode = 4;
        }
        PostQuitMessage(exitCode);
    }

    std::string mOutputPath;
    size_t mWarmupFrames = 300;
    size_t mMeasuredFrames = 1000;
    size_t mWarmupDone = 0;
    State mState = State::Waiting;
    bool mSceneReady = false;
    bool mHasFrameStart = false;
    Clock::time_point mFrameStart;
    Clock::time_point mDrawStart;
    double mDrawMilliseconds = 0.0;
    double mCpuFrameMilliseconds = 0.0;
    std::vector<Sample> mSamples;
    bool mRenderDocCapture = false;
    bool mRenderDocMissing = false;
    RENDERDOC_API_1_1_2* mRenderDoc = nullptr;
};
