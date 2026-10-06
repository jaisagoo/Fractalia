#ifndef VISUALISER_H
#define VISUALISER_H

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "fractal_calculator.h"

class Visualiser {
public:
    Visualiser();
    ~Visualiser();
    int run(HINSTANCE instance, int showCommand);

private:
    struct View {
        double centerReal = -0.5;
        double centerImaginary = 0.0;
        double zoom = 1.0;
        int maxIterations = 300;
        double escapeRadius = 2.0;
        double juliaReal = -0.8;        // Julia constant c (ignored for the Mandelbrot set).
        double juliaImaginary = 0.156;
    };

    enum class SetType {
        Mandelbrot,
        Julia
    };

    struct RenderRequest {
        View view;
        SetType type = SetType::Mandelbrot;
        int width = 0;
        int height = 0;
        unsigned int id = 0;
    };

    // One finished pass from the render thread: a complete image of `view`, computed at one
    // sample per step x step block.
    struct RenderedImage {
        std::vector<std::uint32_t> pixels;
        View view;
        SetType type = SetType::Mandelbrot;
        int width = 0;
        int height = 0;
        int step = 0;
        unsigned int id = 0;
        DWORD elapsedMs = 0;
    };

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK canvasProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI renderThreadEntry(LPVOID parameter);
    LRESULT handleWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createResources();
    void createControls();
    void layoutCanvas(int clientWidth, int clientHeight);
    void paintMenu(HDC deviceContext);
    void drawButton(const DRAWITEMSTRUCT& item);
    void paintCanvas();

    int visibleFieldCount() const;
    void layoutMenu();
    void drawSetButton(const DRAWITEMSTRUCT& item);
    bool readInputs();
    static View defaultView(SetType type);
    void switchSet(SetType type);
    void openJuliaAt(int cursorX, int cursorY);
    void writeInputs();
    void writePositionInputs();
    void requestRender();
    void startRenderThread();
    void stopRenderThread();
    void renderThreadLoop();
    void takeRenderedImage();
    void zoomAtCursor(int cursorX, int cursorY, double zoomFactor);
    void beginPan(POINT point);
    void updatePan(POINT point);
    void endPan();
    void setStatus(const std::string& message);

    HWND window_ = nullptr;
    HWND canvas_ = nullptr;
    HWND titleLabel_ = nullptr;
    HWND hintLabel_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND mandelbrotButton_ = nullptr;
    HWND juliaButton_ = nullptr;
    HWND renderButton_ = nullptr;
    HWND resetButton_ = nullptr;
    HWND centerRealInput_ = nullptr;
    HWND centerImaginaryInput_ = nullptr;
    HWND zoomInput_ = nullptr;
    HWND iterationsInput_ = nullptr;
    HWND escapeRadiusInput_ = nullptr;
    HWND juliaRealInput_ = nullptr;
    HWND juliaImaginaryInput_ = nullptr;
    HWND fieldLabels_[7] = {};

    HFONT font_ = nullptr;
    HFONT titleFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HBRUSH inputBrush_ = nullptr;
    int lineHeight_ = 16;

    View view_;
    SetType setType_ = SetType::Mandelbrot;
    View savedViews_[2];  // The last view of each set, indexed by SetType.
    RenderedImage displayedImage_;  // UI thread only.
    unsigned int latestRequestId_ = 0;

    // Shared with the render thread; guarded by renderLock_ unless atomic.
    FractalCalculator calculator_;
    CRITICAL_SECTION renderLock_;
    HANDLE renderThread_ = nullptr;
    HANDLE renderWakeEvent_ = nullptr;
    RenderRequest pendingRequest_;
    bool requestPending_ = false;
    bool stopRenderThread_ = false;
    RenderedImage finishedImage_;
    bool finishedImageReady_ = false;
    std::atomic<bool> cancelRender_;

    bool panning_ = false;
    POINT panStart_{};
    View panStartView_;
};

#endif
