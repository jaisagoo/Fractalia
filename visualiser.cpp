#include "visualiser.h"
#include <algorithm>
#include <cstdlib>

namespace {
constexpr int renderButtonId = 1001;
constexpr int resetButtonId = 1002;
constexpr int canvasId = 1003;
}

int Visualiser::run(HINSTANCE instance, int showCommand) {
    WNDCLASSA windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = Visualiser::windowProcedure;
    windowClass.lpszClassName = "FractaliaVisualiser";
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassA(&windowClass);

    RECT workArea{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workAreaWidth = workArea.right - workArea.left;
    const int workAreaHeight = workArea.bottom - workArea.top;
    const int windowWidth = std::min(1240, std::max(720, workAreaWidth - 40));
    const int windowHeight = std::min(820, std::max(520, workAreaHeight - 40));

    window_ = CreateWindowExA(0, windowClass.lpszClassName, "Fractalia - Mandelbrot Visualiser",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        nullptr, nullptr, instance, this);
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);

    MSG message{};
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return static_cast<int>(message.wParam);
}

void Visualiser::createControls() {
    CreateWindowA("STATIC", "MANDELBROT PARAMETERS", WS_CHILD | WS_VISIBLE,
        24, 20, 280, 24, window_, nullptr, nullptr, nullptr);

    auto addField = [this](const char* label, const char* value, int y, HWND* input) {
        CreateWindowA("STATIC", label, WS_CHILD | WS_VISIBLE, 24, y, 150, 22,
            window_, nullptr, nullptr, nullptr);
        *input = CreateWindowA("EDIT", value, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            180, y - 3, 125, 26, window_, nullptr, nullptr, nullptr);
    };

    addField("Centre real", "-0.5", 62, &centerRealInput_);
    addField("Centre imaginary", "0.0", 102, &centerImaginaryInput_);
    addField("Zoom", "1.0", 142, &zoomInput_);
    addField("Max iterations", "300", 182, &iterationsInput_);
    addField("Escape radius", "2.0", 222, &escapeRadiusInput_);

    CreateWindowA("BUTTON", "Render", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        24, 270, 130, 34, window_, reinterpret_cast<HMENU>(renderButtonId), nullptr, nullptr);
    CreateWindowA("BUTTON", "Reset", WS_CHILD | WS_VISIBLE,
        170, 270, 135, 34, window_, reinterpret_cast<HMENU>(resetButtonId), nullptr, nullptr);
    statusLabel_ = CreateWindowA("STATIC", "Ready", WS_CHILD | WS_VISIBLE,
        24, 325, 285, 45, window_, nullptr, nullptr, nullptr);

    canvas_ = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        335, 20, canvasWidth_, canvasHeight_, window_, reinterpret_cast<HMENU>(canvasId), nullptr, nullptr);
}

bool Visualiser::readInputs() {
    auto readDouble = [](HWND input, double& output) {
        char text[64]{};
        GetWindowTextA(input, text, sizeof(text));
        char* end = nullptr;
        output = std::strtod(text, &end);
        return end != text && *end == '\0';
    };
    auto readInteger = [](HWND input, int& output) {
        char text[64]{};
        GetWindowTextA(input, text, sizeof(text));
        char* end = nullptr;
        const long value = std::strtol(text, &end, 10);
        output = static_cast<int>(value);
        return end != text && *end == '\0';
    };

    double centerReal = 0.0;
    double centerImaginary = 0.0;
    double zoom = 0.0;
    double escapeRadius = 0.0;
    int maxIterations = 0;
    if (!readDouble(centerRealInput_, centerReal) || !readDouble(centerImaginaryInput_, centerImaginary) ||
        !readDouble(zoomInput_, zoom) || !readInteger(iterationsInput_, maxIterations) ||
        !readDouble(escapeRadiusInput_, escapeRadius) || zoom <= 0.0 || maxIterations < 1 || escapeRadius <= 0.0) {
        SetWindowTextA(statusLabel_, "Enter valid numeric values.");
        return false;
    }

    calculator_.renderMandelbrot(pixels_, canvasWidth_, canvasHeight_, centerReal, centerImaginary,
        zoom, maxIterations, escapeRadius);
    SetWindowTextA(statusLabel_, "Rendered Mandelbrot set.");
    InvalidateRect(canvas_, nullptr, FALSE);
    return true;
}

void Visualiser::render() {
    readInputs();
}

void Visualiser::paintCanvas(HDC deviceContext) {
    RECT bounds{};
    GetClientRect(canvas_, &bounds);
    canvasWidth_ = std::max(1, static_cast<int>(bounds.right - bounds.left));
    canvasHeight_ = std::max(1, static_cast<int>(bounds.bottom - bounds.top));
    if (pixels_.empty()) {
        return;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = canvasWidth_;
    bitmapInfo.bmiHeader.biHeight = -canvasHeight_;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(deviceContext, 0, 0, canvasWidth_, canvasHeight_, 0, 0,
        canvasWidth_, canvasHeight_, pixels_.data(), &bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
}

void Visualiser::setStatus(const std::string&) {
}

LRESULT CALLBACK Visualiser::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* visualiser = reinterpret_cast<Visualiser*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* createData = reinterpret_cast<CREATESTRUCTA*>(lParam);
        visualiser = static_cast<Visualiser*>(createData->lpCreateParams);
        visualiser->window_ = window;
        SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(visualiser));
        visualiser->createControls();
        visualiser->render();
        return TRUE;
    }
    if (visualiser == nullptr) {
        return DefWindowProcA(window, message, wParam, lParam);
    }

    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wParam) == renderButtonId) {
            visualiser->render();
        } else if (LOWORD(wParam) == resetButtonId) {
            SetWindowTextA(visualiser->centerRealInput_, "-0.5");
            SetWindowTextA(visualiser->centerImaginaryInput_, "0.0");
            SetWindowTextA(visualiser->zoomInput_, "1.0");
            SetWindowTextA(visualiser->iterationsInput_, "300");
            SetWindowTextA(visualiser->escapeRadiusInput_, "2.0");
            visualiser->render();
        }
        return 0;
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw->CtlID == canvasId) {
            visualiser->paintCanvas(draw->hDC);
            return TRUE;
        }
        break;
    }
    case WM_SIZE: {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        MoveWindow(visualiser->canvas_, 335, 20, std::max(1, width - 360), std::max(1, height - 55), TRUE);
        visualiser->canvasWidth_ = std::max(1, width - 360);
        visualiser->canvasHeight_ = std::max(1, height - 55);
        visualiser->render();
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    Visualiser visualiser;
    return visualiser.run(instance, showCommand);
}
