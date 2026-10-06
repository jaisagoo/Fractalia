#include "visualiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifndef ODS_NOFOCUSRECT
#define ODS_NOFOCUSRECT 0x0200
#endif
#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY 5
#endif

namespace {
constexpr int renderButtonId = 1001;
constexpr int resetButtonId = 1002;
constexpr int canvasId = 1003;
constexpr int mandelbrotButtonId = 1004;
constexpr int juliaButtonId = 1005;
constexpr int firstInputId = 2001;

constexpr const char* windowClassName = "FractaliaVisualiser";
constexpr const char* canvasClassName = "FractaliaCanvas";

// Menu layout (client coordinates of the main window).
constexpr int margin = 12;
constexpr int panelLeft = margin;
constexpr int panelTop = margin;
constexpr int panelWidth = 300;
constexpr int panelPadding = 16;
constexpr int contentLeft = panelLeft + panelPadding;
constexpr int contentRight = panelLeft + panelWidth - panelPadding;
constexpr int titleTop = panelTop + 16;
constexpr int selectorTop = panelTop + 48;
constexpr int selectorHeight = 32;
constexpr int firstFieldTop = selectorTop + selectorHeight + 16;
constexpr int fieldSpacing = 40;
constexpr int fieldCount = 7;            // All fields; the last two (Julia c) only show for the Julia set.
constexpr int mandelbrotFieldCount = 5;
constexpr int boxLeft = contentLeft + 140;
constexpr int boxHeight = 28;
constexpr int buttonHeight = 34;
constexpr int buttonGap = 12;
constexpr int hintHeight = 60;
constexpr int statusHeight = 40;

// Everything below the fields moves up or down with the number of fields showing.
constexpr int buttonTopFor(int fields) { return firstFieldTop + fields * fieldSpacing + 8; }
constexpr int separatorTopFor(int fields) { return buttonTopFor(fields) + buttonHeight + 16; }
constexpr int hintTopFor(int fields) { return separatorTopFor(fields) + 12; }
constexpr int statusTopFor(int fields) { return hintTopFor(fields) + hintHeight + 8; }
constexpr int panelHeightFor(int fields) { return statusTopFor(fields) + statusHeight + 12 - panelTop; }
constexpr int canvasLeft = panelLeft + panelWidth + margin;
constexpr int canvasTop = margin;
constexpr int minimumCanvasWidth = 240;

// Colours.
const COLORREF backgroundColor = RGB(17, 17, 22);
const COLORREF panelColor = RGB(30, 31, 38);
const COLORREF panelBorderColor = RGB(52, 54, 64);
const COLORREF inputColor = RGB(20, 21, 27);
const COLORREF inputBorderColor = RGB(70, 72, 86);
const COLORREF textColor = RGB(226, 228, 236);
const COLORREF mutedTextColor = RGB(150, 154, 170);
const COLORREF accentColor = RGB(92, 104, 235);
const COLORREF accentPressedColor = RGB(68, 78, 190);
const COLORREF secondaryColor = RGB(50, 52, 63);
const COLORREF secondaryPressedColor = RGB(38, 40, 49);

constexpr double minimumZoom = 0.05;
constexpr double maximumZoom = 100000000000000.0;
constexpr double zoomPerWheelNotch = 1.2;

// Progressive rendering: each view is drawn first with one sample per block of up to
// coarsestStep x coarsestStep pixels, then refined by halving the block size down to 1.
// The starting block size is picked so the first image takes about firstPassTargetMs.
constexpr UINT renderFinishedMessage = WM_APP + 1;
constexpr int coarsestStep = 16;
constexpr double firstPassTargetMs = 8.0;

// Complex-plane width/height of one canvas pixel, matching FractalCalculator's mapping.
double pixelWidth(double zoom, int width, int height) {
    return (4.0 / zoom) * width / height / std::max(1, width - 1);
}

double pixelHeight(double zoom, int height) {
    return (4.0 / zoom) / std::max(1, height - 1);
}

RECT inputBoxRect(int index) {
    const int top = firstFieldTop + index * fieldSpacing;
    return RECT{boxLeft, top, contentRight, top + boxHeight};
}

void fillRect(HDC deviceContext, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(deviceContext, &rect, brush);
    DeleteObject(brush);
}

void frameRect(HDC deviceContext, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FrameRect(deviceContext, &rect, brush);
    DeleteObject(brush);
}

POINT pointFromLParam(LPARAM lParam) {
    return POINT{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
}
}

Visualiser::Visualiser() : cancelRender_(false) {
    InitializeCriticalSection(&renderLock_);
    savedViews_[static_cast<int>(SetType::Mandelbrot)] = defaultView(SetType::Mandelbrot);
    savedViews_[static_cast<int>(SetType::Julia)] = defaultView(SetType::Julia);
    view_ = defaultView(SetType::Mandelbrot);
}

Visualiser::~Visualiser() {
    stopRenderThread();
    DeleteCriticalSection(&renderLock_);
    HGDIOBJ objects[] = {font_, titleFont_, backgroundBrush_, panelBrush_, inputBrush_};
    for (HGDIOBJ object : objects) {
        if (object != nullptr) {
            DeleteObject(object);
        }
    }
}

int Visualiser::run(HINSTANCE instance, int showCommand) {
    createResources();

    WNDCLASSA windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = Visualiser::windowProcedure;
    windowClass.lpszClassName = windowClassName;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = backgroundBrush_;
    RegisterClassA(&windowClass);

    // The fractal canvas is its own window class rather than a STATIC control: STATIC controls
    // are transparent to hit-testing, so they never receive mouse clicks or drags.
    WNDCLASSA canvasClass{};
    canvasClass.hInstance = instance;
    canvasClass.lpfnWndProc = Visualiser::canvasProcedure;
    canvasClass.lpszClassName = canvasClassName;
    canvasClass.style = CS_DBLCLKS;
    canvasClass.hCursor = LoadCursor(nullptr, IDC_CROSS);
    canvasClass.hbrBackground = nullptr;
    RegisterClassA(&canvasClass);

    RECT workArea{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workAreaWidth = workArea.right - workArea.left;
    const int workAreaHeight = workArea.bottom - workArea.top;
    const int windowWidth = std::min(1240, std::max(720, workAreaWidth - 40));
    const int windowHeight = std::min(820, std::max(520, workAreaHeight - 40));

    CreateWindowExA(0, windowClassName, "Fractalia - Fractal Visualiser",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        nullptr, nullptr, instance, this);
    if (window_ == nullptr) {
        return 1;
    }
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);

    MSG message{};
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        // Gives the menu Tab navigation between fields and Enter-to-render.
        if (IsDialogMessageA(window_, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return static_cast<int>(message.wParam);
}

void Visualiser::createResources() {
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    font_ = CreateFontA(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    titleFont_ = CreateFontA(-MulDiv(11, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    backgroundBrush_ = CreateSolidBrush(backgroundColor);
    panelBrush_ = CreateSolidBrush(panelColor);
    inputBrush_ = CreateSolidBrush(inputColor);

    HDC measure = CreateCompatibleDC(nullptr);
    HGDIOBJ previousFont = SelectObject(measure, font_);
    TEXTMETRICA metrics{};
    GetTextMetricsA(measure, &metrics);
    lineHeight_ = std::max(14, static_cast<int>(metrics.tmHeight));
    SelectObject(measure, previousFont);
    DeleteDC(measure);
}

void Visualiser::createControls() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(window_, GWLP_HINSTANCE));
    auto setFont = [](HWND control, HFONT font) {
        SendMessageA(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    };
    auto addLabel = [&](const char* text, int x, int y, int width, int height, HFONT font) {
        HWND label = CreateWindowA("STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            x, y, width, height, window_, nullptr, instance, nullptr);
        setFont(label, font);
        return label;
    };

    titleLabel_ = addLabel("FRACTAL PARAMETERS", contentLeft, titleTop, contentRight - contentLeft,
        lineHeight_ + 8, titleFont_);

    // Mandelbrot | Julia switch: two owner-drawn buttons drawn as one segmented control.
    const int segmentWidth = (contentRight - contentLeft) / 2;
    mandelbrotButton_ = CreateWindowA("BUTTON", "Mandelbrot", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        contentLeft, selectorTop, segmentWidth, selectorHeight, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(mandelbrotButtonId)), instance, nullptr);
    juliaButton_ = CreateWindowA("BUTTON", "Julia", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        contentLeft + segmentWidth, selectorTop, contentRight - contentLeft - segmentWidth, selectorHeight,
        window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(juliaButtonId)), instance, nullptr);
    setFont(mandelbrotButton_, font_);
    setFont(juliaButton_, font_);

    HWND* inputs[fieldCount] = {&centerRealInput_, &centerImaginaryInput_, &zoomInput_,
        &iterationsInput_, &escapeRadiusInput_, &juliaRealInput_, &juliaImaginaryInput_};
    const char* labels[fieldCount] = {"Centre real", "Centre imaginary", "Zoom", "Max iterations", "Escape radius",
        "Julia c real", "Julia c imaginary"};
    for (int index = 0; index < fieldCount; ++index) {
        const RECT box = inputBoxRect(index);
        const int textTop = box.top + (boxHeight - lineHeight_) / 2;
        fieldLabels_[index] = addLabel(labels[index], contentLeft, textTop, boxLeft - contentLeft - 8,
            lineHeight_ + 2, font_);
        // The edit sits inside a box painted by paintMenu, so its text is vertically centred.
        *inputs[index] = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            box.left + 8, textTop, (box.right - box.left) - 16, lineHeight_, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(firstInputId + index)), instance, nullptr);
        setFont(*inputs[index], font_);
        SendMessageA(*inputs[index], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    }
    writeInputs();

    // Buttons, hint and status are positioned by layoutMenu, which depends on the selected set.
    renderButton_ = CreateWindowA("BUTTON", "Render", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(renderButtonId)), instance, nullptr);
    resetButton_ = CreateWindowA("BUTTON", "Reset", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(resetButtonId)), instance, nullptr);
    setFont(renderButton_, font_);
    setFont(resetButton_, font_);

    hintLabel_ = addLabel("", 0, 0, 1, 1, font_);
    statusLabel_ = addLabel("Ready", 0, 0, 1, 1, font_);
    layoutMenu();

    canvas_ = CreateWindowExA(0, canvasClassName, "", WS_CHILD | WS_VISIBLE,
        canvasLeft, canvasTop, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(canvasId)),
        instance, this);
}

void Visualiser::layoutCanvas(int clientWidth, int clientHeight) {
    const int width = std::max(1, clientWidth - canvasLeft - margin);
    const int height = std::max(1, clientHeight - canvasTop - margin);
    MoveWindow(canvas_, canvasLeft, canvasTop, width, height, TRUE);
}

int Visualiser::visibleFieldCount() const {
    return setType_ == SetType::Julia ? fieldCount : mandelbrotFieldCount;
}

// Shows the Julia c fields only for the Julia set and moves everything below them to fit.
void Visualiser::layoutMenu() {
    const int fields = visibleFieldCount();
    HWND inputs[fieldCount] = {centerRealInput_, centerImaginaryInput_, zoomInput_, iterationsInput_,
        escapeRadiusInput_, juliaRealInput_, juliaImaginaryInput_};
    for (int index = mandelbrotFieldCount; index < fieldCount; ++index) {
        const int show = index < fields ? SW_SHOW : SW_HIDE;
        ShowWindow(fieldLabels_[index], show);
        ShowWindow(inputs[index], show);
    }

    const int buttonWidth = (contentRight - contentLeft - buttonGap) / 2;
    const int buttonTop = buttonTopFor(fields);
    MoveWindow(renderButton_, contentLeft, buttonTop, buttonWidth, buttonHeight, FALSE);
    MoveWindow(resetButton_, contentRight - buttonWidth, buttonTop, buttonWidth, buttonHeight, FALSE);
    MoveWindow(hintLabel_, contentLeft, hintTopFor(fields), contentRight - contentLeft, hintHeight, FALSE);
    MoveWindow(statusLabel_, contentLeft, statusTopFor(fields), contentRight - contentLeft, statusHeight, FALSE);
    SetWindowTextA(hintLabel_, setType_ == SetType::Mandelbrot
        ? "Scroll over the image to zoom at the cursor.\nHold the right mouse button and drag to pan.\n"
          "Double-click a point to open its Julia set."
        : "Scroll over the image to zoom at the cursor.\nHold the right mouse button and drag to pan.\n"
          "Julia c is the constant in z = z^2 + c.");

    // Repaint the whole panel area (old and new heights), including the controls that moved.
    RECT panelArea{0, 0, panelLeft + panelWidth + margin, panelTop + panelHeightFor(fieldCount) + margin};
    RedrawWindow(window_, &panelArea, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void Visualiser::paintMenu(HDC deviceContext) {
    const int fields = visibleFieldCount();
    const RECT panel{panelLeft, panelTop, panelLeft + panelWidth, panelTop + panelHeightFor(fields)};
    FillRect(deviceContext, &panel, panelBrush_);
    frameRect(deviceContext, panel, panelBorderColor);

    HWND focused = GetFocus();
    HWND inputs[fieldCount] = {centerRealInput_, centerImaginaryInput_, zoomInput_, iterationsInput_,
        escapeRadiusInput_, juliaRealInput_, juliaImaginaryInput_};
    for (int index = 0; index < fields; ++index) {
        const RECT box = inputBoxRect(index);
        FillRect(deviceContext, &box, inputBrush_);
        frameRect(deviceContext, box, focused == inputs[index] ? accentColor : inputBorderColor);
    }

    const int separatorTop = separatorTopFor(fields);
    const RECT separator{contentLeft, separatorTop, contentRight, separatorTop + 1};
    fillRect(deviceContext, separator, panelBorderColor);
}

void Visualiser::drawButton(const DRAWITEMSTRUCT& item) {
    HDC deviceContext = item.hDC;
    const RECT bounds = item.rcItem;
    FillRect(deviceContext, &bounds, panelBrush_);

    if (item.hwndItem == mandelbrotButton_ || item.hwndItem == juliaButton_) {
        drawSetButton(item);
        return;
    }

    const bool primary = item.hwndItem == renderButton_;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill = primary ? (pressed ? accentPressedColor : accentColor)
                                  : (pressed ? secondaryPressedColor : secondaryColor);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ previousBrush = SelectObject(deviceContext, brush);
    HGDIOBJ previousPen = SelectObject(deviceContext, pen);
    RoundRect(deviceContext, bounds.left, bounds.top, bounds.right, bounds.bottom, 8, 8);
    SelectObject(deviceContext, previousBrush);
    SelectObject(deviceContext, previousPen);
    DeleteObject(brush);
    DeleteObject(pen);

    char text[32]{};
    GetWindowTextA(item.hwndItem, text, sizeof(text));
    RECT textBounds = bounds;
    if (pressed) {
        OffsetRect(&textBounds, 0, 1);
    }
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, RGB(255, 255, 255));
    HGDIOBJ previousFont = SelectObject(deviceContext, font_);
    DrawTextA(deviceContext, text, -1, &textBounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(deviceContext, previousFont);

    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        RECT focusBounds = bounds;
        InflateRect(&focusBounds, -4, -4);
        SetTextColor(deviceContext, RGB(0, 0, 0));
        SetBkColor(deviceContext, RGB(255, 255, 255));
        DrawFocusRect(deviceContext, &focusBounds);
    }
}

// One half of the Mandelbrot | Julia switch. The selected half is filled and underlined.
void Visualiser::drawSetButton(const DRAWITEMSTRUCT& item) {
    HDC deviceContext = item.hDC;
    const RECT bounds = item.rcItem;
    const bool isMandelbrot = item.hwndItem == mandelbrotButton_;
    const bool selected = isMandelbrot == (setType_ == SetType::Mandelbrot);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;

    fillRect(deviceContext, bounds, selected ? secondaryColor : (pressed ? secondaryPressedColor : inputColor));
    frameRect(deviceContext, bounds, inputBorderColor);
    if (selected) {
        const RECT underline{bounds.left + 1, bounds.bottom - 3, bounds.right - 1, bounds.bottom - 1};
        fillRect(deviceContext, underline, accentColor);
    }

    char text[32]{};
    GetWindowTextA(item.hwndItem, text, sizeof(text));
    RECT textBounds = bounds;
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, selected ? textColor : mutedTextColor);
    HGDIOBJ previousFont = SelectObject(deviceContext, font_);
    DrawTextA(deviceContext, text, -1, &textBounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(deviceContext, previousFont);

    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        RECT focusBounds = bounds;
        InflateRect(&focusBounds, -4, -4);
        SetTextColor(deviceContext, RGB(0, 0, 0));
        SetBkColor(deviceContext, RGB(255, 255, 255));
        DrawFocusRect(deviceContext, &focusBounds);
    }
}

bool Visualiser::readInputs() {
    auto readDouble = [](HWND input, double& output) {
        char text[64]{};
        GetWindowTextA(input, text, sizeof(text));
        char* end = nullptr;
        output = std::strtod(text, &end);
        while (end != nullptr && (*end == ' ' || *end == '\t')) {
            ++end;
        }
        return end != text && *end == '\0' && std::isfinite(output);
    };
    auto readInteger = [](HWND input, int& output) {
        char text[64]{};
        GetWindowTextA(input, text, sizeof(text));
        char* end = nullptr;
        const long value = std::strtol(text, &end, 10);
        while (end != nullptr && (*end == ' ' || *end == '\t')) {
            ++end;
        }
        output = static_cast<int>(std::max(-1L, std::min(value, 1000000L)));
        return end != text && *end == '\0';
    };

    View view = view_;
    if (!readDouble(centerRealInput_, view.centerReal)) {
        setStatus("Centre real must be a number.");
        return false;
    }
    if (!readDouble(centerImaginaryInput_, view.centerImaginary)) {
        setStatus("Centre imaginary must be a number.");
        return false;
    }
    if (!readDouble(zoomInput_, view.zoom) || view.zoom <= 0.0) {
        setStatus("Zoom must be a number greater than 0.");
        return false;
    }
    if (!readInteger(iterationsInput_, view.maxIterations) || view.maxIterations < 1) {
        setStatus("Max iterations must be a whole number of at least 1.");
        return false;
    }
    if (!readDouble(escapeRadiusInput_, view.escapeRadius) || view.escapeRadius <= 0.0) {
        setStatus("Escape radius must be a number greater than 0.");
        return false;
    }
    if (setType_ == SetType::Julia) {
        if (!readDouble(juliaRealInput_, view.juliaReal)) {
            setStatus("Julia c real must be a number.");
            return false;
        }
        if (!readDouble(juliaImaginaryInput_, view.juliaImaginary)) {
            setStatus("Julia c imaginary must be a number.");
            return false;
        }
    }
    view_ = view;
    return true;
}

Visualiser::View Visualiser::defaultView(SetType type) {
    View view;
    if (type == SetType::Julia) {
        view.centerReal = 0.0;
        view.centerImaginary = 0.0;
    }
    return view;
}

// Each set keeps its own view, so switching back and forth returns to where you were.
void Visualiser::switchSet(SetType type) {
    if (type == setType_) {
        return;
    }
    endPan();
    savedViews_[static_cast<int>(setType_)] = view_;
    setType_ = type;
    view_ = savedViews_[static_cast<int>(type)];
    layoutMenu();
    writeInputs();
    setStatus(setType_ == SetType::Julia ? "Julia set selected." : "Mandelbrot set selected.");
    requestRender();
}

// Double-clicking the Mandelbrot set opens the Julia set whose constant c is the clicked point.
void Visualiser::openJuliaAt(int cursorX, int cursorY) {
    RECT bounds{};
    GetClientRect(canvas_, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (setType_ != SetType::Mandelbrot || width < 2 || height < 2) {
        return;
    }
    View julia = defaultView(SetType::Julia);
    julia.juliaReal = view_.centerReal + (cursorX - 0.5 * (width - 1)) * pixelWidth(view_.zoom, width, height);
    julia.juliaImaginary = view_.centerImaginary - (cursorY - 0.5 * (height - 1)) * pixelHeight(view_.zoom, height);
    julia.maxIterations = view_.maxIterations;
    julia.escapeRadius = view_.escapeRadius;
    savedViews_[static_cast<int>(SetType::Julia)] = julia;
    switchSet(SetType::Julia);
}

void Visualiser::writeInputs() {
    writePositionInputs();
    char value[64]{};
    std::snprintf(value, sizeof(value), "%d", view_.maxIterations);
    SetWindowTextA(iterationsInput_, value);
    std::snprintf(value, sizeof(value), "%.15g", view_.escapeRadius);
    SetWindowTextA(escapeRadiusInput_, value);
    std::snprintf(value, sizeof(value), "%.15g", view_.juliaReal);
    SetWindowTextA(juliaRealInput_, value);
    std::snprintf(value, sizeof(value), "%.15g", view_.juliaImaginary);
    SetWindowTextA(juliaImaginaryInput_, value);
}

void Visualiser::writePositionInputs() {
    char value[64]{};
    std::snprintf(value, sizeof(value), "%.15g", view_.centerReal);
    SetWindowTextA(centerRealInput_, value);
    std::snprintf(value, sizeof(value), "%.15g", view_.centerImaginary);
    SetWindowTextA(centerImaginaryInput_, value);
    std::snprintf(value, sizeof(value), "%.15g", view_.zoom);
    SetWindowTextA(zoomInput_, value);
}

// Hands the current view to the render thread. Any pass still running for an older view is
// abandoned, so scrolling or dragging never waits for a render to finish.
void Visualiser::requestRender() {
    if (canvas_ == nullptr) {
        return;
    }
    RECT bounds{};
    GetClientRect(canvas_, &bounds);

    EnterCriticalSection(&renderLock_);
    pendingRequest_.view = view_;
    pendingRequest_.type = setType_;
    pendingRequest_.width = bounds.right - bounds.left;
    pendingRequest_.height = bounds.bottom - bounds.top;
    pendingRequest_.id = ++latestRequestId_;
    requestPending_ = true;
    cancelRender_.store(true);
    LeaveCriticalSection(&renderLock_);
    SetEvent(renderWakeEvent_);

    // Repaint straight away: the previous image is redrawn scaled/shifted to the new view
    // until the first pass of the new render arrives.
    InvalidateRect(canvas_, nullptr, FALSE);
}

void Visualiser::startRenderThread() {
    renderWakeEvent_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    renderThread_ = CreateThread(nullptr, 0, renderThreadEntry, this, 0, nullptr);
}

void Visualiser::stopRenderThread() {
    if (renderThread_ == nullptr) {
        return;
    }
    EnterCriticalSection(&renderLock_);
    stopRenderThread_ = true;
    cancelRender_.store(true);
    LeaveCriticalSection(&renderLock_);
    SetEvent(renderWakeEvent_);
    WaitForSingleObject(renderThread_, INFINITE);
    CloseHandle(renderThread_);
    CloseHandle(renderWakeEvent_);
    renderThread_ = nullptr;
    renderWakeEvent_ = nullptr;
}

DWORD WINAPI Visualiser::renderThreadEntry(LPVOID parameter) {
    static_cast<Visualiser*>(parameter)->renderThreadLoop();
    return 0;
}

void Visualiser::renderThreadLoop() {
    // Rendering soaks up every core; a slightly lower priority keeps the window responsive.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::vector<std::uint32_t> buffer;
    double millisecondsPerPixel = 0.0;  // Measured from the last completed render.

    for (;;) {
        WaitForSingleObject(renderWakeEvent_, INFINITE);

        RenderRequest request;
        EnterCriticalSection(&renderLock_);
        if (stopRenderThread_) {
            LeaveCriticalSection(&renderLock_);
            return;
        }
        const bool haveRequest = requestPending_;
        if (haveRequest) {
            request = pendingRequest_;
            requestPending_ = false;
            cancelRender_.store(false);
        }
        LeaveCriticalSection(&renderLock_);
        if (!haveRequest || request.width < 1 || request.height < 1) {
            continue;
        }

        // Start coarse enough that the first image appears within about firstPassTargetMs.
        const double pixelCount = static_cast<double>(request.width) * request.height;
        const double estimatedMs = millisecondsPerPixel * pixelCount;
        int step = 1;
        while (step < coarsestStep && estimatedMs / (step * step) > firstPassTargetMs) {
            step *= 2;
        }

        const DWORD started = GetTickCount();
        bool reuseCoarser = false;
        for (; step >= 1; step /= 2) {
            const View& view = request.view;
            const bool rendered = request.type == SetType::Mandelbrot
                ? calculator_.renderMandelbrot(buffer, request.width, request.height, view.centerReal,
                    view.centerImaginary, view.zoom, view.maxIterations, view.escapeRadius,
                    &cancelRender_, step, reuseCoarser)
                : calculator_.renderJulia(buffer, request.width, request.height, view.centerReal,
                    view.centerImaginary, view.zoom, view.juliaReal, view.juliaImaginary,
                    view.maxIterations, view.escapeRadius, &cancelRender_, step, reuseCoarser);
            if (!rendered) {
                break;  // A newer request arrived; the loop picks it up straight away.
            }
            reuseCoarser = true;
            const DWORD elapsed = GetTickCount() - started;
            if (step == 1) {
                millisecondsPerPixel = std::max<double>(elapsed, 1.0) / pixelCount;
            }

            EnterCriticalSection(&renderLock_);
            finishedImage_.pixels = buffer;
            finishedImage_.view = request.view;
            finishedImage_.type = request.type;
            finishedImage_.width = request.width;
            finishedImage_.height = request.height;
            finishedImage_.step = step;
            finishedImage_.id = request.id;
            finishedImage_.elapsedMs = elapsed;
            finishedImageReady_ = true;
            LeaveCriticalSection(&renderLock_);
            PostMessageA(window_, renderFinishedMessage, 0, 0);
        }
    }
}

void Visualiser::takeRenderedImage() {
    EnterCriticalSection(&renderLock_);
    const bool ready = finishedImageReady_;
    if (ready) {
        std::swap(displayedImage_, finishedImage_);
        finishedImageReady_ = false;
    }
    LeaveCriticalSection(&renderLock_);
    if (!ready) {
        return;
    }

    if (displayedImage_.step == 1 && displayedImage_.id == latestRequestId_) {
        char message[128]{};
        std::snprintf(message, sizeof(message), "Rendered %d x %d in %lu ms\nZoom %.6g",
            displayedImage_.width, displayedImage_.height,
            static_cast<unsigned long>(displayedImage_.elapsedMs), displayedImage_.view.zoom);
        setStatus(message);
    }
    InvalidateRect(canvas_, nullptr, FALSE);
}

void Visualiser::paintCanvas() {
    PAINTSTRUCT paint{};
    HDC deviceContext = BeginPaint(canvas_, &paint);
    RECT bounds{};
    GetClientRect(canvas_, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;

    if (width > 0 && height > 0) {
        // Draw into an off-screen bitmap first so panning and resizing don't flicker.
        HDC memoryContext = CreateCompatibleDC(deviceContext);
        HBITMAP bitmap = CreateCompatibleBitmap(deviceContext, width, height);
        HGDIOBJ previousBitmap = SelectObject(memoryContext, bitmap);
        FillRect(memoryContext, &bounds, backgroundBrush_);

        const RenderedImage& image = displayedImage_;
        if (image.type == setType_ && !image.pixels.empty() && image.width > 0 && image.height > 0) {
            // The image may be of a slightly different view (the user has zoomed or panned since
            // it was requested), so map its corners into the current view and stretch it there.
            const double canvasPixelWidth = pixelWidth(view_.zoom, width, height);
            const double canvasPixelHeight = pixelHeight(view_.zoom, height);
            const double imagePixelWidth = pixelWidth(image.view.zoom, image.width, image.height);
            const double imagePixelHeight = pixelHeight(image.view.zoom, image.height);
            const double canvasLeft = view_.centerReal - 0.5 * canvasPixelWidth * (width - 1);
            const double canvasTop = view_.centerImaginary + 0.5 * canvasPixelHeight * (height - 1);
            const double imageLeft = image.view.centerReal - 0.5 * imagePixelWidth * (image.width - 1);
            const double imageTop = image.view.centerImaginary + 0.5 * imagePixelHeight * (image.height - 1);

            const double scaleX = imagePixelWidth / canvasPixelWidth;
            const double scaleY = imagePixelHeight / canvasPixelHeight;
            const double originX = (imageLeft - canvasLeft) / canvasPixelWidth - 0.5 * scaleX + 0.5;
            const double originY = (canvasTop - imageTop) / canvasPixelHeight - 0.5 * scaleY + 0.5;
            const double left = std::floor(originX + 0.5);
            const double top = std::floor(originY + 0.5);
            const double right = std::floor(originX + image.width * scaleX + 0.5);
            const double bottom = std::floor(originY + image.height * scaleY + 0.5);
            const double limit = 1 << 26;  // Stay well inside GDI's coordinate range.

            if (std::fabs(left) < limit && std::fabs(top) < limit && std::fabs(right) < limit &&
                std::fabs(bottom) < limit && right > left && bottom > top) {
                BITMAPINFO bitmapInfo{};
                bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bitmapInfo.bmiHeader.biWidth = image.width;
                bitmapInfo.bmiHeader.biHeight = -image.height;
                bitmapInfo.bmiHeader.biPlanes = 1;
                bitmapInfo.bmiHeader.biBitCount = 32;
                bitmapInfo.bmiHeader.biCompression = BI_RGB;
                SetStretchBltMode(memoryContext, COLORONCOLOR);
                StretchDIBits(memoryContext, static_cast<int>(left), static_cast<int>(top),
                    static_cast<int>(right - left), static_cast<int>(bottom - top),
                    0, 0, image.width, image.height, image.pixels.data(), &bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
            }
        }

        BitBlt(deviceContext, 0, 0, width, height, memoryContext, 0, 0, SRCCOPY);
        SelectObject(memoryContext, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryContext);
    }
    EndPaint(canvas_, &paint);
}

void Visualiser::zoomAtCursor(int cursorX, int cursorY, double zoomFactor) {
    RECT bounds{};
    GetClientRect(canvas_, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width < 2 || height < 2) {
        return;
    }

    const double newZoom = std::max(minimumZoom, std::min(maximumZoom, view_.zoom * zoomFactor));
    if (newZoom == view_.zoom) {
        setStatus(newZoom >= maximumZoom ? "Maximum zoom reached." : "Minimum zoom reached.");
        return;
    }

    // Keep the complex number under the cursor fixed while the scale changes.
    const double aspectRatio = static_cast<double>(width) / height;
    const double horizontalPosition = static_cast<double>(cursorX) / (width - 1) - 0.5;
    const double verticalPosition = 0.5 - static_cast<double>(cursorY) / (height - 1);
    const double viewHeight = 4.0 / view_.zoom;
    const double cursorReal = view_.centerReal + horizontalPosition * viewHeight * aspectRatio;
    const double cursorImaginary = view_.centerImaginary + verticalPosition * viewHeight;
    const double newViewHeight = 4.0 / newZoom;

    view_.zoom = newZoom;
    view_.centerReal = cursorReal - horizontalPosition * newViewHeight * aspectRatio;
    view_.centerImaginary = cursorImaginary - verticalPosition * newViewHeight;
    writePositionInputs();
    requestRender();
}

void Visualiser::beginPan(POINT point) {
    panning_ = true;
    panStart_ = point;
    panStartView_ = view_;
    SetCapture(canvas_);
    SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
}

// The view follows the mouse continuously; each move is a new (progressive) render request.
void Visualiser::updatePan(POINT point) {
    if (!panning_) {
        return;
    }
    RECT bounds{};
    GetClientRect(canvas_, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width < 2 || height < 2) {
        return;
    }
    const int deltaX = point.x - panStart_.x;
    const int deltaY = point.y - panStart_.y;
    view_.centerReal = panStartView_.centerReal - deltaX * pixelWidth(view_.zoom, width, height);
    view_.centerImaginary = panStartView_.centerImaginary + deltaY * pixelHeight(view_.zoom, height);
    writePositionInputs();
    requestRender();
}

void Visualiser::endPan() {
    if (!panning_) {
        return;
    }
    panning_ = false;
    if (GetCapture() == canvas_) {
        ReleaseCapture();
    }
}

void Visualiser::setStatus(const std::string& message) {
    if (statusLabel_ != nullptr) {
        SetWindowTextA(statusLabel_, message.c_str());
    }
}

LRESULT CALLBACK Visualiser::canvasProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createData = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createData->lpCreateParams));
    }
    auto* visualiser = reinterpret_cast<Visualiser*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (visualiser == nullptr || visualiser->canvas_ != window) {
        return DefWindowProcA(window, message, wParam, lParam);
    }
    return visualiser->handleCanvasMessage(message, wParam, lParam);
}

LRESULT Visualiser::handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paintCanvas();
        return 0;
    case WM_SIZE:
        requestRender();
        return 0;
    case WM_LBUTTONDBLCLK:
        openJuliaAt(pointFromLParam(lParam).x, pointFromLParam(lParam).y);
        return 0;
    case WM_RBUTTONDOWN:
        beginPan(pointFromLParam(lParam));
        return 0;
    case WM_MOUSEMOVE:
        updatePan(pointFromLParam(lParam));
        return 0;
    case WM_RBUTTONUP:
        endPan();
        return 0;
    case WM_CAPTURECHANGED:
        endPan();
        return 0;
    default:
        // WM_MOUSEWHEEL falls through: DefWindowProc forwards it to the main window, which zooms.
        return DefWindowProcA(canvas_, message, wParam, lParam);
    }
}

LRESULT CALLBACK Visualiser::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createData = reinterpret_cast<CREATESTRUCTA*>(lParam);
        auto* created = static_cast<Visualiser*>(createData->lpCreateParams);
        created->window_ = window;
        SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
        // WM_NCCREATE must reach DefWindowProc, otherwise the title bar text is never set.
        return DefWindowProcA(window, message, wParam, lParam);
    }
    auto* visualiser = reinterpret_cast<Visualiser*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (visualiser == nullptr) {
        return DefWindowProcA(window, message, wParam, lParam);
    }
    return visualiser->handleWindowMessage(message, wParam, lParam);
}

LRESULT Visualiser::handleWindowMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        startRenderThread();
        createControls();
        return 0;
    case renderFinishedMessage:
        takeRenderedImage();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC deviceContext = BeginPaint(window_, &paint);
        paintMenu(deviceContext);
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC deviceContext = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        const bool muted = control == hintLabel_ || control == statusLabel_;
        SetTextColor(deviceContext, muted ? mutedTextColor : textColor);
        SetBkColor(deviceContext, panelColor);
        return reinterpret_cast<LRESULT>(panelBrush_);
    }
    case WM_CTLCOLOREDIT: {
        HDC deviceContext = reinterpret_cast<HDC>(wParam);
        SetTextColor(deviceContext, textColor);
        SetBkColor(deviceContext, inputColor);
        return reinterpret_cast<LRESULT>(inputBrush_);
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item->CtlType == ODT_BUTTON) {
            drawButton(*item);
            return TRUE;
        }
        break;
    }
    case DM_GETDEFID:
        // Pressing Enter in any field "clicks" Render (via IsDialogMessage in the message loop).
        return MAKELRESULT(renderButtonId, DC_HASDEFID);
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id >= firstInputId && id < firstInputId + fieldCount) {
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                RECT box = inputBoxRect(id - firstInputId);
                InvalidateRect(window_, &box, FALSE);
            }
            return 0;
        }
        if (code != BN_CLICKED) {
            return 0;
        }
        if (id == mandelbrotButtonId) {
            switchSet(SetType::Mandelbrot);
        } else if (id == juliaButtonId) {
            switchSet(SetType::Julia);
        } else if (id == renderButtonId || id == IDOK) {
            if (readInputs()) {
                requestRender();
            }
        } else if (id == resetButtonId) {
            view_ = defaultView(setType_);
            writeInputs();
            requestRender();
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // Wheel messages go to the focused window (or the window under the cursor) and bubble up
        // to here, so handle them centrally and only zoom when the cursor is over the image.
        POINT cursor = pointFromLParam(lParam);
        ScreenToClient(canvas_, &cursor);
        RECT bounds{};
        GetClientRect(canvas_, &bounds);
        if (!panning_ && PtInRect(&bounds, cursor)) {
            const double notches = static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            zoomAtCursor(cursor.x, cursor.y, std::pow(zoomPerWheelNotch, notches));
        }
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            layoutCanvas(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT minimum{0, 0, canvasLeft + minimumCanvasWidth + margin, panelTop + panelHeightFor(fieldCount) + margin};
        AdjustWindowRectEx(&minimum, WS_OVERLAPPEDWINDOW, FALSE, 0);
        limits->ptMinTrackSize.x = minimum.right - minimum.left;
        limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
        return 0;
    }
    case WM_DESTROY:
        stopRenderThread();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(window_, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    Visualiser visualiser;
    return visualiser.run(instance, showCommand);
}

