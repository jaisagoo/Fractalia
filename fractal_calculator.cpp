#include "fractal_calculator.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#endif

namespace {
// Points inside the main cardioid or the period-2 bulb never escape, so they can be classified
// with a closed-form test instead of running all maxIterations iterations.
bool inMainCardioidOrBulb(double real, double imaginary) {
    const double imaginarySquared = imaginary * imaginary;
    const double shiftedReal = real - 0.25;
    const double q = shiftedReal * shiftedReal + imaginarySquared;
    if (q * (q + shiftedReal) <= 0.25 * imaginarySquared) {
        return true;
    }
    const double bulbReal = real + 1.0;
    return bulbReal * bulbReal + imaginarySquared <= 0.0625;
}

unsigned int workerCount() {
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const unsigned int count = static_cast<unsigned int>(info.dwNumberOfProcessors);
#else
    const unsigned int count = std::thread::hardware_concurrency();
#endif
    return std::max(1u, std::min(count, 64u));
}

struct RenderJob {
    const FractalCalculator* calculator;
    std::uint32_t* pixels;
    int width;
    int height;
    double left;
    double top;
    double pixelWidth;
    double pixelHeight;
    int maxIterations;
    double escapeRadius;
    const std::atomic<bool>* cancel;
    int step;
    bool reuseCoarser;
    bool julia;
    double constantReal;
    double constantImaginary;
    std::atomic<int> nextRow;
    std::atomic<bool> aborted;
};

std::uint32_t colourFor(int iterations, int maxIterations) {
    if (iterations >= maxIterations) {
        return 0x00100B2A;
    }
    const double shade = static_cast<double>(iterations) / maxIterations;
    const auto red = static_cast<std::uint32_t>(25.0 + 230.0 * shade);
    const auto green = static_cast<std::uint32_t>(35.0 + 170.0 * shade * shade);
    const auto blue = static_cast<std::uint32_t>(70.0 + 180.0 * (1.0 - shade));
    // 32-bit DIB pixels are laid out as 0x00RRGGBB.
    return (red << 16) | (green << 8) | blue;
}

// Each worker repeatedly claims the next unrendered sample row, so threads that get cheap rows
// (far outside the set) simply take more of them.
//
// Only every step-th pixel in each direction is computed, and its colour fills the step x step
// block below and to the right of it. With reuseCoarser, samples that the previous (2 x step)
// pass already computed are skipped, so refining 16 -> 8 -> 4 -> 2 -> 1 costs the same in total
// as a single full-resolution render.
void renderRows(RenderJob& job) {
    const int step = job.step;
    for (;;) {
        const int y = job.nextRow.fetch_add(1) * step;
        if (y >= job.height) {
            return;
        }
        if (job.cancel != nullptr && job.cancel->load(std::memory_order_relaxed)) {
            job.aborted.store(true);
            return;
        }
        const double imaginary = job.top - y * job.pixelHeight;
        const bool rowDoneBefore = job.reuseCoarser && y % (2 * step) == 0;
        const int blockHeight = std::min(step, job.height - y);
        std::uint32_t* row = job.pixels + static_cast<std::size_t>(y) * job.width;
        for (int x = 0; x < job.width; x += step) {
            if (rowDoneBefore && x % (2 * step) == 0) {
                continue;
            }
            const double real = job.left + x * job.pixelWidth;
            const int iterations = job.julia
                ? job.calculator->escapeIterationsJulia(real, imaginary, job.constantReal,
                    job.constantImaginary, job.maxIterations, job.escapeRadius)
                : job.calculator->escapeIterations(real, imaginary, job.maxIterations, job.escapeRadius);
            const std::uint32_t colour = colourFor(iterations, job.maxIterations);
            const int blockWidth = std::min(step, job.width - x);
            for (int blockRow = 0; blockRow < blockHeight; ++blockRow) {
                std::fill_n(row + static_cast<std::size_t>(blockRow) * job.width + x, blockWidth, colour);
            }
        }
    }
}

#ifdef _WIN32
DWORD WINAPI renderThread(LPVOID parameter);
#endif

bool runRenderJob(RenderJob& job) {
    const int sampleRows = (job.height + job.step - 1) / job.step;
    const unsigned int threads = std::min(workerCount(), static_cast<unsigned int>(sampleRows));
#ifdef _WIN32
    std::vector<HANDLE> handles;
    const int priority = GetThreadPriority(GetCurrentThread());
    for (unsigned int index = 1; index < threads; ++index) {
        HANDLE handle = CreateThread(nullptr, 0, renderThread, &job, CREATE_SUSPENDED, nullptr);
        if (handle != nullptr) {
            SetThreadPriority(handle, priority);
            ResumeThread(handle);
            handles.push_back(handle);
        }
    }
    renderRows(job);
    if (!handles.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, INFINITE);
        for (HANDLE handle : handles) {
            CloseHandle(handle);
        }
    }
#else
    std::vector<std::thread> workers;
    for (unsigned int index = 1; index < threads; ++index) {
        workers.emplace_back(renderRows, std::ref(job));
    }
    renderRows(job);
    for (std::thread& worker : workers) {
        worker.join();
    }
#endif
    return !job.aborted.load();
}

#ifdef _WIN32
DWORD WINAPI renderThread(LPVOID parameter) {
    renderRows(*static_cast<RenderJob*>(parameter));
    return 0;
}
#endif
}

int FractalCalculator::escapeIterations(double real, double imaginary, int maxIterations, double escapeRadius) const {
    // Every orbit of a point in the set stays within |z| <= 2, so the shortcut is only exact
    // when the escape radius is at least 2.
    if (escapeRadius >= 2.0 && inMainCardioidOrBulb(real, imaginary)) {
        return maxIterations;
    }

    const double escapeSquared = escapeRadius * escapeRadius;
    double zReal = 0.0;
    double zImaginary = 0.0;
    double zRealSquared = 0.0;
    double zImaginarySquared = 0.0;

    // Cycle detection (Brent's method): remember z at doubling intervals. If the orbit lands back
    // on a remembered value it is trapped in a cycle and will never escape.
    double savedReal = 0.0;
    double savedImaginary = 0.0;
    int checkInterval = 8;
    int stepsSinceSave = 0;

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        zImaginary = 2.0 * zReal * zImaginary + imaginary;
        zReal = zRealSquared - zImaginarySquared + real;
        zRealSquared = zReal * zReal;
        zImaginarySquared = zImaginary * zImaginary;

        if (zRealSquared + zImaginarySquared > escapeSquared) {
            return iteration + 1;
        }

        if (std::fabs(zReal - savedReal) < 1e-15 && std::fabs(zImaginary - savedImaginary) < 1e-15) {
            return maxIterations;
        }
        if (++stepsSinceSave == checkInterval) {
            stepsSinceSave = 0;
            checkInterval *= 2;
            savedReal = zReal;
            savedImaginary = zImaginary;
        }
    }

    return maxIterations;
}

int FractalCalculator::escapeIterationsJulia(double real, double imaginary, double constantReal,
    double constantImaginary, int maxIterations, double escapeRadius) const {
    const double escapeSquared = escapeRadius * escapeRadius;
    double zReal = real;
    double zImaginary = imaginary;
    double zRealSquared = zReal * zReal;
    double zImaginarySquared = zImaginary * zImaginary;
    double savedReal = zReal;
    double savedImaginary = zImaginary;
    int checkInterval = 8;
    int stepsSinceSave = 0;

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        zImaginary = 2.0 * zReal * zImaginary + constantImaginary;
        zReal = zRealSquared - zImaginarySquared + constantReal;
        zRealSquared = zReal * zReal;
        zImaginarySquared = zImaginary * zImaginary;

        if (zRealSquared + zImaginarySquared > escapeSquared) {
            return iteration + 1;
        }
        if (std::fabs(zReal - savedReal) < 1e-15 && std::fabs(zImaginary - savedImaginary) < 1e-15) {
            return maxIterations;
        }
        if (++stepsSinceSave == checkInterval) {
            stepsSinceSave = 0;
            checkInterval *= 2;
            savedReal = zReal;
            savedImaginary = zImaginary;
        }
    }
    return maxIterations;
}

bool FractalCalculator::renderMandelbrot(std::vector<std::uint32_t>& pixels, int width, int height,
    double centerReal, double centerImaginary, double zoom,
    int maxIterations, double escapeRadius, const std::atomic<bool>* cancel,
    int step, bool reuseCoarser) const {
    if (width < 1 || height < 1) {
        pixels.clear();
        return true;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels.size() != pixelCount) {
        pixels.assign(pixelCount, 0);
        reuseCoarser = false;
    }
    step = std::max(1, step);
    const double aspectRatio = static_cast<double>(width) / static_cast<double>(height);
    const double viewHeight = 4.0 / zoom;
    const double viewWidth = viewHeight * aspectRatio;

    RenderJob job;
    job.calculator = this;
    job.pixels = pixels.data();
    job.width = width;
    job.height = height;
    job.left = centerReal - 0.5 * viewWidth;
    job.top = centerImaginary + 0.5 * viewHeight;
    job.pixelWidth = width > 1 ? viewWidth / (width - 1) : 0.0;
    job.pixelHeight = height > 1 ? viewHeight / (height - 1) : 0.0;
    job.maxIterations = maxIterations;
    job.escapeRadius = escapeRadius;
    job.cancel = cancel;
    job.step = step;
    job.reuseCoarser = reuseCoarser;
    job.julia = false;
    job.constantReal = 0.0;
    job.constantImaginary = 0.0;
    job.nextRow.store(0);
    job.aborted.store(false);

    return runRenderJob(job);
}

bool FractalCalculator::renderJulia(std::vector<std::uint32_t>& pixels, int width, int height,
    double centerReal, double centerImaginary, double zoom,
    double constantReal, double constantImaginary, int maxIterations, double escapeRadius,
    const std::atomic<bool>* cancel, int step, bool reuseCoarser) const {
    if (width < 1 || height < 1) {
        pixels.clear();
        return true;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels.size() != pixelCount) {
        pixels.assign(pixelCount, 0);
        reuseCoarser = false;
    }
    step = std::max(1, step);
    const double aspectRatio = static_cast<double>(width) / static_cast<double>(height);
    const double viewHeight = 4.0 / zoom;
    const double viewWidth = viewHeight * aspectRatio;

    RenderJob job;
    job.calculator = this;
    job.pixels = pixels.data();
    job.width = width;
    job.height = height;
    job.left = centerReal - 0.5 * viewWidth;
    job.top = centerImaginary + 0.5 * viewHeight;
    job.pixelWidth = width > 1 ? viewWidth / (width - 1) : 0.0;
    job.pixelHeight = height > 1 ? viewHeight / (height - 1) : 0.0;
    job.maxIterations = maxIterations;
    job.escapeRadius = escapeRadius;
    job.cancel = cancel;
    job.step = step;
    job.reuseCoarser = reuseCoarser;
    job.julia = true;
    job.constantReal = constantReal;
    job.constantImaginary = constantImaginary;
    job.nextRow.store(0);
    job.aborted.store(false);
    return runRenderJob(job);
}


