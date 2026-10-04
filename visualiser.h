#ifndef VISUALISER_H
#define VISUALISER_H

#include "complex_number_calculator.h"

class Visualiser {
private:
    ComplexNumberCalculator calculator;

public:
    void visualiseComplexNumber(double real, double imaginary);
};

#endif