#ifndef COMPLEX_NUMBER_CALCULATOR_H
#define COMPLEX_NUMBER_CALCULATOR_H

#include<math.h>

class ComplexNumberCalculator {
public:
    double calculateMagnitude(double real, double imaginary);
    double calculateAngle(double real, double imaginary);
    double calculateRealPart(double magnitude, double angle);
    double calculateImaginaryPart(double magnitude, double angle);
};

#endif