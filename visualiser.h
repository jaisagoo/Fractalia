#ifndef VISUALISER_H
#define VISUALISER_H

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "fractal_calculator.h"

class Visualiser {
public:
    int run(HINSTANCE instance, int showCommand);

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void render();
    bool readInputs();
    void createControls();
    void paintCanvas(HDC deviceContext);
    void setStatus(const std::string& message);

    HWND window_ = nullptr;
    HWND canvas_ = nullptr;
    HWND centerRealInput_ = nullptr;
    HWND centerImaginaryInput_ = nullptr;
    HWND zoomInput_ = nullptr;
    HWND iterationsInput_ = nullptr;
    HWND escapeRadiusInput_ = nullptr;
    HWND statusLabel_ = nullptr;
    std::vector<std::uint32_t> pixels_;
    FractalCalculator calculator_;
    int canvasWidth_ = 900;
    int canvasHeight_ = 650;
};

#endif