#ifndef FRACTAL_CALCULATOR_H
#define FRACTAL_CALCULATOR_H

#include <atomic>
#include <cstdint>
#include <vector>

// The Mandelbrot iteration z(n+1) = a z(n)^b + c z(n) + d, starting from z(0) = start, where d is
// the point being tested. a, c and start are complex and the exponent b is real; the classic set
// is a = 1, b = 2, c = 0, start = 0. Non-integer exponents use the principal branch of z^b.
struct MandelbrotFormula {
    double aReal = 1.0;
    double aImaginary = 0.0;
    double exponent = 2.0;
    double cReal = 0.0;
    double cImaginary = 0.0;
    double startReal = 0.0;
    double startImaginary = 0.0;

    bool isClassic() const {
        return aReal == 1.0 && aImaginary == 0.0 && exponent == 2.0 && cReal == 0.0 && cImaginary == 0.0 &&
            startReal == 0.0 && startImaginary == 0.0;
    }
};

// How points outside the set are coloured. Colours repeat every cycleLength iterations.
//   smooth: by a continuous escape count (the iteration count plus how far past the escape radius
//           the orbit landed), giving smooth gradients that show how fast each point blows up.
//   bands:  by the whole number of iterations, giving flat bands whose edges are where |z_n|
//           crosses the escape radius (so the escape radius changes their shape).
struct ColourSettings {
    bool smooth = true;
    double cycleLength = 24.0;
};

class FractalCalculator {
public:
    // Each returns the iteration at which the orbit escaped, or maxIterations if it never did. If
    // smoothCount is given and the orbit escaped, it receives the continuous escape count.
    int escapeIterations(double real, double imaginary, int maxIterations, double escapeRadius,
        double* smoothCount = nullptr) const;
    int escapeIterationsGeneral(double real, double imaginary, const MandelbrotFormula& formula,
        int maxIterations, double escapeRadius, double* smoothCount = nullptr) const;
    int escapeIterationsJulia(double real, double imaginary, double constantReal, double constantImaginary,
        int maxIterations, double escapeRadius, double* smoothCount = nullptr) const;

    // Renders width x height pixels (0x00RRGGBB) using every CPU core. If cancel is given and becomes
    // true part-way through, rendering stops early and the function returns false.
    //
    // step > 1 computes only every step-th pixel in each direction and fills step x step blocks,
    // giving a quick low-resolution image. With reuseCoarser, pixels already computed by a pass
    // with twice the step (same view and size, same buffer) are kept rather than recomputed.
    bool renderMandelbrot(std::vector<std::uint32_t>& pixels, int width, int height,
        double centerReal, double centerImaginary, double zoom,
        int maxIterations, double escapeRadius, const MandelbrotFormula& formula,
        const ColourSettings& colouring, const std::atomic<bool>* cancel = nullptr,
        int step = 1, bool reuseCoarser = false) const;
    bool renderJulia(std::vector<std::uint32_t>& pixels, int width, int height,
        double centerReal, double centerImaginary, double zoom,
        double constantReal, double constantImaginary, int maxIterations, double escapeRadius,
        const ColourSettings& colouring, const std::atomic<bool>* cancel = nullptr, int step = 1,
        bool reuseCoarser = false) const;
};

#endif
