#include "fractal_calculator.h"
#include <cstddef>

int FractalCalculator::escapeIterations(double real, double imaginary, int maxIterations, double escapeRadius) const {
    double zReal = 0.0;
    double zImaginary = 0.0;
    const double escapeSquared = escapeRadius * escapeRadius;

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        const double nextReal = zReal * zReal - zImaginary * zImaginary + real;
        const double nextImaginary = 2.0 * zReal * zImaginary + imaginary;
        zReal = nextReal;
        zImaginary = nextImaginary;

        if (zReal * zReal + zImaginary * zImaginary > escapeSquared) {
            return iteration + 1;
        }
    }

    return maxIterations;
}

void FractalCalculator::renderMandelbrot(std::vector<std::uint32_t>& pixels, int width, int height,
    double centerReal, double centerImaginary, double zoom,
    int maxIterations, double escapeRadius) const {
    pixels.resize(static_cast<std::size_t>(width * height));
    const double aspectRatio = static_cast<double>(width) / static_cast<double>(height);
    const double viewHeight = 4.0 / zoom;
    const double viewWidth = viewHeight * aspectRatio;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double real = centerReal + (static_cast<double>(x) / (width - 1) - 0.5) * viewWidth;
            const double imaginary = centerImaginary + (0.5 - static_cast<double>(y) / (height - 1)) * viewHeight;
            const int iterations = escapeIterations(real, imaginary, maxIterations, escapeRadius);
            std::uint32_t color = 0x00100B2A;
            if (iterations < maxIterations) {
                const double shade = static_cast<double>(iterations) / maxIterations;
                const auto red = static_cast<std::uint32_t>(25.0 + 230.0 * shade);
                const auto green = static_cast<std::uint32_t>(35.0 + 170.0 * shade * shade);
                const auto blue = static_cast<std::uint32_t>(70.0 + 180.0 * (1.0 - shade));
                color = red | (green << 8) | (blue << 16);
            }
            pixels[static_cast<std::size_t>(y * width + x)] = color;
        }
    }
}