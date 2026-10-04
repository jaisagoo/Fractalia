#include<math.h>
#include "complex_number_calculator.h"
#include "visualiser.h"
#include "fractal_calculator.h"
#include<iostream>

// This function visualises a complex number by calculating its magnitude and angle, and then printing the results to the console.

void Visualiser::visualiseComplexNumber(double real, double imaginary) {
    double magnitude = calculator.calculateMagnitude(real, imaginary);
    double angle = calculator.calculateAngle(real, imaginary);

    std::cout << "Complex Number: " << real << " + " << imaginary << "i" << std::endl;
    std::cout << "Magnitude: " << magnitude << std::endl;
    std::cout << "Angle (in radians): " << angle << std::endl;
}

// This function computes the Mandelbrot set (z_1^{z_2} + c) for a given complex number z_1, 
// real number z_2, complex number c and an integer maxium number of iterations.

void Visualiser::visualiseMandelbrot(double z1_real, double z1_imaginary, double z2, double c_real, double c_imaginary, int maxIterations) {
    int iterations = computeMandelbrot(z1_real, z1_imaginary, c_real, maxIterations);

    std::cout << "Mandelbrot Set Computation:" << std::endl;
    std::cout << "z_1: " << z1_real << " + " << z1_imaginary << "i" << std::endl;
    std::cout << "z_2: " << z2 << std::endl;
    std::cout << "c: " << c_real << " + " << c_imaginary << "i" << std::endl;
    std::cout << "Max Iterations: " << maxIterations << std::endl;
    std::cout << "Iterations until escape: " << iterations << std::endl;
}

int main() {
    Visualiser visualiser;

    // Example usage of visualiseComplexNumber
    visualiser.visualiseComplexNumber(3.0, 4.0);

    // Example usage of visualiseMandelbrot
    visualiser.visualiseMandelbrot(0.0, 0.0, 2.0, -0.7, 0.27015, 1000);

    return 0;
}