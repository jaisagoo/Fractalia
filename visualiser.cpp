#include<math.h>
#include "complex_number_calculator.h"
#include "visualiser.h"
#include<iostream>

void Visualiser::visualiseComplexNumber(double real, double imaginary) {
    double magnitude = calculator.calculateMagnitude(real, imaginary);
    double angle = calculator.calculateAngle(real, imaginary);

    std::cout << "Complex Number: " << real << " + " << imaginary << "i" << std::endl;
    std::cout << "Magnitude: " << magnitude << std::endl;
    std::cout << "Angle (in radians): " << angle << std::endl;
}