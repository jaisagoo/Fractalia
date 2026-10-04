#include<math.h>
#include<iostream>
#include "complex_number_calculator.h"

double ComplexNumberCalculator::calculateMagnitude(double real, double imaginary) {
    return sqrt(real * real + imaginary * imaginary);
}

double ComplexNumberCalculator::calculateAngle(double real, double imaginary) {
    return atan2(imaginary, real);
}

double ComplexNumberCalculator::calculateRealPart(double magnitude, double angle) {
    return magnitude * cos(angle);
}

double ComplexNumberCalculator::calculateImaginaryPart(double magnitude, double angle) {
    return magnitude * sin(angle);
}