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
    MandelbrotFormula formula;   // Mandelbrot only.
    bool classicFormula;
    ColourSettings colouring;
    std::atomic<int> nextRow;
    std::atomic<bool> aborted;
};

// A looping gradient (deep blue -> blue -> near white -> orange -> crimson -> back), precomputed
// into a table. 32-bit DIB pixels are laid out as 0x00RRGGBB.
constexpr int paletteSize = 2048;

const std::uint32_t* palette() {
    static std::uint32_t table[paletteSize];
    static bool built = false;
    if (!built) {
        struct Stop { double position; double red, green, blue; };
        const Stop stops[] = {
            {0.00, 0, 7, 100}, {0.16, 32, 107, 203}, {0.42, 237, 255, 255}, {0.6425, 255, 170, 0},
            {0.8575, 160, 30, 50}, {1.00, 0, 7, 100},
        };
        for (int index = 0; index < paletteSize; ++index) {
            const double position = static_cast<double>(index) / paletteSize;
            int stop = 0;
            while (stops[stop + 1].position < position) {
                ++stop;
            }
            const Stop& from = stops[stop];
            const Stop& to = stops[stop + 1];
            double t = (position - from.position) / (to.position - from.position);
            t = t * t * (3.0 - 2.0 * t);  // Ease between stops so the gradient has no visible kinks.
            const auto channel = [t](double a, double b) {
                return static_cast<std::uint32_t>(a + (b - a) * t + 0.5);
            };
            table[index] = (channel(from.red, to.red) << 16) | (channel(from.green, to.green) << 8) |
                channel(from.blue, to.blue);
        }
        built = true;
    }
    return table;
}

std::uint32_t colourFor(int iterations, double smoothCount, int maxIterations, const ColourSettings& colouring) {
    if (iterations >= maxIterations) {
        return 0x00100B2A;  // Inside the set.
    }
    const double count = colouring.smooth ? smoothCount : static_cast<double>(iterations);
    double position = count / std::max(1e-9, colouring.cycleLength);
    position -= std::floor(position);
    const int index = std::min(paletteSize - 1, std::max(0, static_cast<int>(position * paletteSize)));
    return palette()[index];
}

// The continuous escape count for an orbit that has just escaped at step `count` with value z.
// The orbit is followed a few more steps until |z| is large (where |z| grows like |z|^degree each
// step), then the fractional part comes from where |z| lies between successive steps.
template <typename Step>
double smoothEscapeCount(int count, double zReal, double zImaginary, double escapeRadius, double degree,
    Step step) {
    const double bigRadius = std::max(1000.0, escapeRadius);
    const double bigSquared = bigRadius * bigRadius;
    int steps = count;
    for (int extra = 0; extra < 64 && zReal * zReal + zImaginary * zImaginary < bigSquared; ++extra) {
        step(zReal, zImaginary);
        ++steps;
    }
    const double sizeSquared = zReal * zReal + zImaginary * zImaginary;
    if (degree <= 1.0 || !(sizeSquared > 1.0) || !std::isfinite(sizeSquared)) {
        return steps;
    }
    const double logSize = 0.5 * std::log(sizeSquared);
    return steps + 1.0 - std::log(logSize / std::log(bigRadius)) / std::log(degree);
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
            double smoothCount = 0.0;
            double* smoothOutput = job.colouring.smooth ? &smoothCount : nullptr;
            const int iterations = job.julia
                ? job.calculator->escapeIterationsJulia(real, imaginary, job.constantReal,
                    job.constantImaginary, job.maxIterations, job.escapeRadius, smoothOutput)
                : job.classicFormula
                ? job.calculator->escapeIterations(real, imaginary, job.maxIterations, job.escapeRadius,
                    smoothOutput)
                : job.calculator->escapeIterationsGeneral(real, imaginary, job.formula, job.maxIterations,
                    job.escapeRadius, smoothOutput);
            const std::uint32_t colour = colourFor(iterations, smoothCount, job.maxIterations, job.colouring);
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

int FractalCalculator::escapeIterations(double real, double imaginary, int maxIterations, double escapeRadius,
    double* smoothCount) const {
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
            if (smoothCount != nullptr) {
                *smoothCount = smoothEscapeCount(iteration + 1, zReal, zImaginary, escapeRadius, 2.0,
                    [real, imaginary](double& re, double& im) {
                        const double nextReal = re * re - im * im + real;
                        im = 2.0 * re * im + imaginary;
                        re = nextReal;
                    });
            }
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
    double constantImaginary, int maxIterations, double escapeRadius, double* smoothCount) const {
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
            if (smoothCount != nullptr) {
                *smoothCount = smoothEscapeCount(iteration + 1, zReal, zImaginary, escapeRadius, 2.0,
                    [constantReal, constantImaginary](double& re, double& im) {
                        const double nextReal = re * re - im * im + constantReal;
                        im = 2.0 * re * im + constantImaginary;
                        re = nextReal;
                    });
            }
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

namespace {
// z^n for a whole number n, by repeated squaring. Negative n gives 1 / z^|n|.
void integerPower(double& real, double& imaginary, long exponent) {
    const bool invert = exponent < 0;
    unsigned long remaining = static_cast<unsigned long>(invert ? -exponent : exponent);
    double resultReal = 1.0;
    double resultImaginary = 0.0;
    double baseReal = real;
    double baseImaginary = imaginary;
    while (remaining > 0) {
        if (remaining & 1UL) {
            const double nextReal = resultReal * baseReal - resultImaginary * baseImaginary;
            resultImaginary = resultReal * baseImaginary + resultImaginary * baseReal;
            resultReal = nextReal;
        }
        remaining >>= 1;
        if (remaining > 0) {
            const double nextReal = baseReal * baseReal - baseImaginary * baseImaginary;
            baseImaginary = 2.0 * baseReal * baseImaginary;
            baseReal = nextReal;
        }
    }
    if (invert) {
        const double size = resultReal * resultReal + resultImaginary * resultImaginary;
        resultReal /= size;
        resultImaginary = -resultImaginary / size;
    }
    real = resultReal;
    imaginary = resultImaginary;
}

// z^b for any real b, using the principal branch (angle in (-pi, pi]).
void realPower(double& real, double& imaginary, double exponent) {
    const double size = std::sqrt(real * real + imaginary * imaginary);
    if (size == 0.0) {
        real = exponent > 0.0 ? 0.0 : (exponent == 0.0 ? 1.0 : HUGE_VAL);
        imaginary = 0.0;
        return;
    }
    const double newSize = std::pow(size, exponent);
    const double newAngle = exponent * std::atan2(imaginary, real);
    real = newSize * std::cos(newAngle);
    imaginary = newSize * std::sin(newAngle);
}
}

// The general iteration z -> a z^b + c z + d from z = start, with d the point being tested. The
// cardioid/bulb shortcut only holds for the classic formula, but cycle detection still works for
// any fixed map.
int FractalCalculator::escapeIterationsGeneral(double real, double imaginary, const MandelbrotFormula& formula,
    int maxIterations, double escapeRadius, double* smoothCount) const {
    const double aReal = formula.aReal;
    const double aImaginary = formula.aImaginary;
    const double cReal = formula.cReal;
    const double cImaginary = formula.cImaginary;
    const double exponent = formula.exponent;
    const double roundedExponent = std::round(exponent);
    const bool wholeExponent = std::fabs(exponent - roundedExponent) < 1e-12 && std::fabs(roundedExponent) <= 1e6;
    const long integerExponent = static_cast<long>(roundedExponent);

    // Past a large enough |z|, the a z^b term outgrows everything else and |z| increases every
    // step, so the orbit must escape. Below that an orbit can still come back, so the escape test
    // never uses a smaller radius. (Only possible for b > 1; otherwise the user's radius is used.)
    const double aSize = std::sqrt(aReal * aReal + aImaginary * aImaginary);
    const double cSize = std::sqrt(cReal * cReal + cImaginary * cImaginary);
    const double dSize = std::sqrt(real * real + imaginary * imaginary);
    double radius = escapeRadius;
    if (aSize > 1e-12 && exponent > 1.0) {
        const double linear = 1.0 + cSize;
        if (exponent == 2.0) {
            // Exact root of |a| r^2 - (1 + |c|) r - |d| = 0.
            radius = std::max(radius, (linear + std::sqrt(linear * linear + 4.0 * aSize * dSize)) / (2.0 * aSize));
        } else {
            // Enough that |a| r^b >= 2 (1 + |c|) r and |a| r^b >= 2 |d|.
            radius = std::max(radius, std::pow(2.0 * linear / aSize, 1.0 / (exponent - 1.0)));
            radius = std::max(radius, std::pow(2.0 * dSize / aSize, 1.0 / exponent));
        }
    }
    const double escapeSquared = radius * radius;

    double zReal = formula.startReal;
    double zImaginary = formula.startImaginary;
    double savedReal = zReal;
    double savedImaginary = zImaginary;
    int checkInterval = 8;
    int stepsSinceSave = 0;

    // One step of z -> a z^b + c z + d.
    const auto step = [&](double& re, double& im) {
        double powerReal = re;
        double powerImaginary = im;
        if (wholeExponent) {
            integerPower(powerReal, powerImaginary, integerExponent);
        } else {
            realPower(powerReal, powerImaginary, exponent);
        }
        const double nextReal = aReal * powerReal - aImaginary * powerImaginary +
            cReal * re - cImaginary * im + real;
        im = aReal * powerImaginary + aImaginary * powerReal + cReal * im + cImaginary * re + imaginary;
        re = nextReal;
    };

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        step(zReal, zImaginary);

        // Also catches overflow/NaN (e.g. a negative exponent at z = 0): treat it as escaped.
        const double sizeSquared = zReal * zReal + zImaginary * zImaginary;
        if (!(sizeSquared <= escapeSquared)) {
            if (smoothCount != nullptr) {
                *smoothCount = smoothEscapeCount(iteration + 1, zReal, zImaginary, radius, exponent, step);
            }
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
    int maxIterations, double escapeRadius, const MandelbrotFormula& formula,
    const ColourSettings& colouring, const std::atomic<bool>* cancel, int step, bool reuseCoarser) const {
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
    job.formula = formula;
    job.classicFormula = formula.isClassic();
    job.colouring = colouring;
    job.nextRow.store(0);
    job.aborted.store(false);

    return runRenderJob(job);
}

bool FractalCalculator::renderJulia(std::vector<std::uint32_t>& pixels, int width, int height,
    double centerReal, double centerImaginary, double zoom,
    double constantReal, double constantImaginary, int maxIterations, double escapeRadius,
    const ColourSettings& colouring, const std::atomic<bool>* cancel, int step, bool reuseCoarser) const {
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
    job.formula = MandelbrotFormula();
    job.classicFormula = true;
    job.colouring = colouring;
    job.nextRow.store(0);
    job.aborted.store(false);
    return runRenderJob(job);
}


