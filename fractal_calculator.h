#ifndef FRACTAL_CALCULATOR_H
#define FRACTAL_CALCULATOR_H

#include <atomic>
#include <cstdint>
#include <vector>

class FractalCalculator {
public:
    int escapeIterations(double real, double imaginary, int maxIterations, double escapeRadius) const;
    int escapeIterationsJulia(double real, double imaginary, double constantReal, double constantImaginary,
        int maxIterations, double escapeRadius) const;

    // Renders width x height pixels (0x00RRGGBB) using every CPU core. If cancel is given and becomes
    // true part-way through, rendering stops early and the function returns false.
    //
    // step > 1 computes only every step-th pixel in each direction and fills step x step blocks,
    // giving a quick low-resolution image. With reuseCoarser, pixels already computed by a pass
    // with twice the step (same view and size, same buffer) are kept rather than recomputed.
    bool renderMandelbrot(std::vector<std::uint32_t>& pixels, int width, int height,
        double centerReal, double centerImaginary, double zoom,
        int maxIterations, double escapeRadius, const std::atomic<bool>* cancel = nullptr,
        int step = 1, bool reuseCoarser = false) const;
    bool renderJulia(std::vector<std::uint32_t>& pixels, int width, int height,
        double centerReal, double centerImaginary, double zoom,
        double constantReal, double constantImaginary, int maxIterations, double escapeRadius,
        const std::atomic<bool>* cancel = nullptr, int step = 1, bool reuseCoarser = false) const;
};

#endif
