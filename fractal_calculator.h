#ifndef FRACTAL_CALCULATOR_H
#define FRACTAL_CALCULATOR_H

#include <cstdint>
#include <vector>

class FractalCalculator {
public:
    int escapeIterations(double real, double imaginary, int maxIterations, double escapeRadius) const;
    void renderMandelbrot(std::vector<std::uint32_t>& pixels, int width, int height,
        double centerReal, double centerImaginary, double zoom,
        int maxIterations, double escapeRadius) const;
};

#endif