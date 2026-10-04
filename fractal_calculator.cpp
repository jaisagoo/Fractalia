#include<math.h>
#include "complex_number_calculator.h"
#include "visualiser.h"
#include<iostream>

// This function computes values of the mandelbrot set for given inputs z1 (complex), z2 (real), c (complex), and maxIterations (int).
// It returns the number of iterations it took for the magnitude of z to exceed 2, or maxIterations if it never does.
int computeMandelbrot(double z1, double z2, double c, int maxIterations) {
    ComplexNumberCalculator calculator;
    double zReal = z1;
    double zImaginary = z2;
    int iterations = 0;

    while (iterations < maxIterations) {
        // Calculate the magnitude of z
        double magnitude = calculator.calculateMagnitude(zReal, zImaginary);
        
        // If the magnitude exceeds 2, break the loop
        if (magnitude > 2.0) {
            break;
        }

        // Calculate the next values of z using the Mandelbrot formula: z = z^2 + c
        double newZReal = calculator.calculateRealPart(magnitude, calculator.calculateAngle(zReal, zImaginary)) + c;
        double newZImaginary = calculator.calculateImaginaryPart(magnitude, calculator.calculateAngle(zReal, zImaginary));

        // Update z for the next iteration
        zReal = newZReal;
        zImaginary = newZImaginary;

        iterations++;
    }

    return iterations;
}