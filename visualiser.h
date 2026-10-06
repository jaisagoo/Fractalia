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
        MandelbrotFormula formula;      // z -> a z^b + c z + d from z0 (Mandelbrot only).
        ColourSettings colouring;       // How points outside the set are coloured.
    };

    enum class SetType {
        Mandelbrot,
        Julia
    };

    // The values a parameter's slider runs between, chosen by the user (like Desmos).
    struct SliderRange {
        double minimum = 0.0;
        double maximum = 1.0;
    };

    // Centre re/im, zoom, iterations, escape radius, colour cycle, Julia c re/im, then Mandelbrot
    // formula constants a re/im, exponent b, c re/im, z0 re/im.
    static constexpr int fieldCount = 15;

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
    static LRESULT CALLBACK panelProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK sliderProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI renderThreadEntry(LPVOID parameter);
    LRESULT handleWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handlePanelMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleSliderMessage(HWND slider, int field, UINT message, WPARAM wParam, LPARAM lParam);

    void createResources();
    void createControls();
    void layoutCanvas();
    void paintMenu(HDC deviceContext);
    void drawButton(const DRAWITEMSTRUCT& item);
    void paintCanvas();

    int fieldTop(int field) const;
    int colouringRowTop() const;
    int formulaHeadingTop() const;
    bool isFieldVisible(int field) const;
    int fieldsBottom() const;
    RECT valueBoxRect(int field) const;
    RECT minimumBoxRect(int field) const;
    RECT maximumBoxRect(int field) const;
    RECT sliderRect(int field) const;
    void layoutMenu();
    void positionMenuControls();
    void scrollMenuTo(int position);
    void scrollMenuToShow(RECT area);
    void invalidateMenuArea(RECT area);
    void drawToggleButton(const DRAWITEMSTRUCT& item, bool selected);
    void paintSlider(HWND slider, int field);
    bool readInputs();
    bool parseField(int field, double& value) const;
    double fieldValue(int field) const;
    void setFieldValue(int field, double value);
    void onValueEdited(int field);
    void onRangeEdited(int field);
    double sliderFraction(int field) const;
    void setFieldFromSlider(int field, double fraction);
    SliderRange& range(int field);
    static SliderRange defaultRange(SetType type, int field);
    static View defaultView(SetType type);
    void switchSet(SetType type);
    void openJuliaAt(int cursorX, int cursorY);
    void writeInputs();
    void writePositionInputs();
    void writeField(int field);
    void writeRange(int field);
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
    HWND formulaLabel_ = nullptr;
    HWND colouringLabel_ = nullptr;
    HWND smoothButton_ = nullptr;
    HWND bandsButton_ = nullptr;
    HWND mandelbrotButton_ = nullptr;
    HWND juliaButton_ = nullptr;
    HWND renderButton_ = nullptr;
    HWND resetButton_ = nullptr;
    HWND panel_ = nullptr;                      // Scrollable container for the whole menu.
    HWND fieldLabels_[fieldCount] = {};
    HWND valueInputs_[fieldCount] = {};
    HWND minimumInputs_[fieldCount] = {};       // Left end of each slider.
    HWND maximumInputs_[fieldCount] = {};       // Right end of each slider.
    HWND sliders_[fieldCount] = {};
    int menuScroll_ = 0;                        // Pixels the menu is scrolled down by.
    int menuVisibleHeight_ = 0;
    int canvasLeft_ = 0;
    bool writingInputs_ = false;                // Ignore EN_CHANGE while the program sets text.

    HFONT font_ = nullptr;
    HFONT titleFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HBRUSH inputBrush_ = nullptr;
    int lineHeight_ = 16;

    View view_;
    SetType setType_ = SetType::Mandelbrot;
    View savedViews_[2];  // The last view of each set, indexed by SetType.
    SliderRange ranges_[2][fieldCount];  // Slider ranges for each set.
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
