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
constexpr int smoothButtonId = 1007;
constexpr int bandsButtonId = 1008;
constexpr int panelId = 1006;
constexpr int firstValueInputId = 2001;    // + field index
constexpr int firstMinimumInputId = 2101;  // + field index
constexpr int firstMaximumInputId = 2201;  // + field index
constexpr int firstSliderId = 2301;        // + field index

constexpr const char* windowClassName = "FractaliaVisualiser";
constexpr const char* canvasClassName = "FractaliaCanvas";
constexpr const char* panelClassName = "FractaliaPanel";
constexpr const char* sliderClassName = "FractaliaSlider";

// Window layout (client coordinates of the main window).
constexpr int margin = 12;
constexpr int panelLeft = margin;
constexpr int panelTop = margin;
constexpr int panelWidth = 336;   // A scroll bar is added to the right of this when the menu doesn't fit.
constexpr int canvasTop = margin;
constexpr int minimumCanvasWidth = 240;
constexpr int minimumClientHeight = 360;

// Menu layout, in the panel's own coordinates before scrolling.
constexpr int panelPadding = 16;
constexpr int contentLeft = panelPadding;
constexpr int contentRight = panelWidth - panelPadding;
constexpr int titleTop = 16;
constexpr int selectorTop = 48;
constexpr int selectorHeight = 32;
constexpr int firstFieldTop = selectorTop + selectorHeight + 16;
constexpr int fieldSpacing = 66;       // Each field: value row, then a [min] ---slider--- [max] row.
constexpr int colouringRowHeight = 40;     // "Colouring  [Smooth | Bands]" switch row.
constexpr int formulaHeadingHeight = 30;  // "Formula" heading above a, b, c and z0.
constexpr int boxLeft = contentLeft + 150;
constexpr int boxHeight = 28;
constexpr int rangeRowOffset = boxHeight + 6;
constexpr int rangeRowHeight = 22;
constexpr int rangeBoxWidth = 64;
constexpr int sliderGap = 8;
constexpr int buttonHeight = 34;
constexpr int buttonGap = 12;
constexpr int hintHeight = 60;
constexpr int statusHeight = 40;

// Everything below the fields moves up or down with where the last field ends.
constexpr int buttonTopFor(int fieldsBottom) { return fieldsBottom + 4; }
constexpr int separatorTopFor(int fieldsBottom) { return buttonTopFor(fieldsBottom) + buttonHeight + 16; }
constexpr int hintTopFor(int fieldsBottom) { return separatorTopFor(fieldsBottom) + 12; }
constexpr int statusTopFor(int fieldsBottom) { return hintTopFor(fieldsBottom) + hintHeight + 8; }
constexpr int menuHeightFor(int fieldsBottom) { return statusTopFor(fieldsBottom) + statusHeight + 12; }

// Field indices, in menu order.
constexpr int centreRealField = 0;
constexpr int centreImaginaryField = 1;
constexpr int zoomField = 2;
constexpr int iterationsField = 3;
constexpr int escapeRadiusField = 4;
constexpr int colourCycleField = 5;      // Shown under the Colouring switch, for both sets.
constexpr int juliaRealField = 6;
constexpr int juliaImaginaryField = 7;
constexpr int firstFormulaField = 8;     // Mandelbrot formula z -> a z^b + c z + d from z0:
constexpr int aRealField = 8;
constexpr int aImaginaryField = 9;
constexpr int exponentField = 10;
constexpr int cRealField = 11;
constexpr int cImaginaryField = 12;
constexpr int startRealField = 13;
constexpr int startImaginaryField = 14;

struct FieldInfo {
    const char* label;
    bool logarithmic;   // Slider moves through the range multiplicatively (used for zoom).
    bool integer;
};

const FieldInfo fieldInfo[] = {
    {"Centre real", false, false},
    {"Centre imaginary", false, false},
    {"Zoom", true, false},
    {"Max iterations", false, true},
    {"Escape radius", false, false},
    {"Colour cycle", true, false},
    {"Julia c real", false, false},
    {"Julia c imaginary", false, false},
    {"a real", false, false},
    {"a imaginary", false, false},
    {"b (exponent)", false, false},
    {"c real", false, false},
    {"c imaginary", false, false},
    {"Start z0 real", false, false},
    {"Start z0 imaginary", false, false},
};

// Unscrolled rectangles of each part of a field whose row starts at `top`, in panel coordinates.
RECT valueBoxAt(int top) {
    return RECT{boxLeft, top, contentRight, top + boxHeight};
}

RECT minimumBoxAt(int top) {
    top += rangeRowOffset;
    return RECT{contentLeft, top, contentLeft + rangeBoxWidth, top + rangeRowHeight};
}

RECT maximumBoxAt(int top) {
    top += rangeRowOffset;
    return RECT{contentRight - rangeBoxWidth, top, contentRight, top + rangeRowHeight};
}

RECT sliderAt(int top) {
    top += rangeRowOffset;
    return RECT{contentLeft + rangeBoxWidth + sliderGap, top, contentRight - rangeBoxWidth - sliderGap,
        top + rangeRowHeight};
}

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

RECT offsetRect(RECT rect, int deltaY) {
    OffsetRect(&rect, 0, deltaY);
    return rect;
}

POINT pointFromLParam(LPARAM lParam) {
    return POINT{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
}

// Parses a whole text box as a finite number (surrounding spaces allowed).
bool parseNumber(HWND input, double& output) {
    char text[64]{};
    GetWindowTextA(input, text, sizeof(text));
    char* end = nullptr;
    output = std::strtod(text, &end);
    while (end != nullptr && (*end == ' ' || *end == '\t')) {
        ++end;
    }
    return end != text && *end == '\0' && std::isfinite(output);
}
}

Visualiser::Visualiser() : cancelRender_(false) {
    InitializeCriticalSection(&renderLock_);
    savedViews_[static_cast<int>(SetType::Mandelbrot)] = defaultView(SetType::Mandelbrot);
    savedViews_[static_cast<int>(SetType::Julia)] = defaultView(SetType::Julia);
    view_ = defaultView(SetType::Mandelbrot);
    for (int type = 0; type < 2; ++type) {
        for (int field = 0; field < fieldCount; ++field) {
            ranges_[type][field] = defaultRange(static_cast<SetType>(type), field);
        }
    }
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

    WNDCLASSA panelClass{};
    panelClass.hInstance = instance;
    panelClass.lpfnWndProc = Visualiser::panelProcedure;
    panelClass.lpszClassName = panelClassName;
    panelClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    panelClass.hbrBackground = panelBrush_;
    RegisterClassA(&panelClass);

    // Sliders are drawn by the program (rather than the common-controls trackbar) so they match
    // the dark theme, hold fractional values and need no extra libraries.
    WNDCLASSA sliderClass{};
    sliderClass.hInstance = instance;
    sliderClass.lpfnWndProc = Visualiser::sliderProcedure;
    sliderClass.lpszClassName = sliderClassName;
    sliderClass.hCursor = LoadCursor(nullptr, IDC_HAND);
    sliderClass.hbrBackground = nullptr;
    RegisterClassA(&sliderClass);

    RECT workArea{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workAreaWidth = workArea.right - workArea.left;
    const int workAreaHeight = workArea.bottom - workArea.top;
    const int windowWidth = std::min(1240, std::max(720, workAreaWidth - 40));
    const int windowHeight = std::min(900, std::max(520, workAreaHeight - 40));

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

    // The whole menu lives in a child window so it can scroll when the window is too short.
    // WS_EX_CONTROLPARENT lets Tab move into and between the controls inside it.
    panel_ = CreateWindowExA(WS_EX_CONTROLPARENT, panelClassName, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL, panelLeft, panelTop, panelWidth, 1, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(panelId)), instance, this);

    auto setFont = [](HWND control, HFONT font) {
        SendMessageA(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    };
    auto addLabel = [&](const char* text, HFONT font) {
        HWND label = CreateWindowA("STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            0, 0, 1, 1, panel_, nullptr, instance, nullptr);
        setFont(label, font);
        return label;
    };
    auto addEdit = [&](int id) {
        HWND edit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 1, 1, panel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        setFont(edit, font_);
        SendMessageA(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        return edit;
    };
    auto addButton = [&](const char* text, int id) {
        HWND button = CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, panel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        setFont(button, font_);
        return button;
    };

    titleLabel_ = addLabel("FRACTAL PARAMETERS", titleFont_);
    mandelbrotButton_ = addButton("Mandelbrot", mandelbrotButtonId);
    juliaButton_ = addButton("Julia", juliaButtonId);

    // Creation order is also Tab order: value, slider minimum, slider, slider maximum.
    for (int field = 0; field < fieldCount; ++field) {
        if (field == colourCycleField) {
            colouringLabel_ = addLabel("Colouring", font_);
            smoothButton_ = addButton("Smooth", smoothButtonId);
            bandsButton_ = addButton("Bands", bandsButtonId);
        }
        fieldLabels_[field] = addLabel(fieldInfo[field].label, font_);
        valueInputs_[field] = addEdit(firstValueInputId + field);
        minimumInputs_[field] = addEdit(firstMinimumInputId + field);
        sliders_[field] = CreateWindowA(sliderClassName, "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 1, 1, panel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(firstSliderId + field)),
            instance, this);
        maximumInputs_[field] = addEdit(firstMaximumInputId + field);
    }

    formulaLabel_ = addLabel("Formula:  z = a z^b + c z + d,  d = the point", font_);

    renderButton_ = addButton("Render", renderButtonId);
    resetButton_ = addButton("Reset", resetButtonId);
    hintLabel_ = addLabel("", font_);
    statusLabel_ = addLabel("Ready", font_);

    canvas_ = CreateWindowExA(0, canvasClassName, "", WS_CHILD | WS_VISIBLE,
        0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(canvasId)), instance, this);

    writeInputs();
    for (int field = 0; field < fieldCount; ++field) {
        writeRange(field);
    }
    layoutMenu();
}

void Visualiser::layoutCanvas() {
    RECT client{};
    GetClientRect(window_, &client);
    const int width = std::max(1, static_cast<int>(client.right) - canvasLeft_ - margin);
    const int height = std::max(1, static_cast<int>(client.bottom) - canvasTop - margin);
    MoveWindow(canvas_, canvasLeft_, canvasTop, width, height, TRUE);
}

// Where a field's row starts in the (unscrolled) menu, or -1 if the field isn't shown for this set.
// Both sets show the view fields, the Colouring switch and the colour cycle; then Julia shows c,
// and Mandelbrot shows its formula constants under a "Formula" heading.
int Visualiser::fieldTop(int field) const {
    if (field <= escapeRadiusField) {
        return firstFieldTop + field * fieldSpacing;
    }
    if (field == colourCycleField) {
        return colouringRowTop() + colouringRowHeight;
    }
    const int commonBottom = colouringRowTop() + colouringRowHeight + fieldSpacing;
    if (setType_ == SetType::Julia) {
        return field == juliaRealField || field == juliaImaginaryField
            ? commonBottom + (field - juliaRealField) * fieldSpacing : -1;
    }
    if (field < firstFormulaField) {
        return -1;
    }
    return formulaHeadingTop() + formulaHeadingHeight + (field - firstFormulaField) * fieldSpacing;
}

int Visualiser::colouringRowTop() const {
    return firstFieldTop + (escapeRadiusField + 1) * fieldSpacing;
}

int Visualiser::formulaHeadingTop() const {
    return colouringRowTop() + colouringRowHeight + fieldSpacing;
}

bool Visualiser::isFieldVisible(int field) const {
    return fieldTop(field) >= 0;
}

// Bottom of the last visible field row.
int Visualiser::fieldsBottom() const {
    return setType_ == SetType::Julia
        ? fieldTop(juliaImaginaryField) + fieldSpacing
        : formulaHeadingTop() + formulaHeadingHeight + (fieldCount - firstFormulaField) * fieldSpacing;
}

RECT Visualiser::valueBoxRect(int field) const { return valueBoxAt(fieldTop(field)); }
RECT Visualiser::minimumBoxRect(int field) const { return minimumBoxAt(fieldTop(field)); }
RECT Visualiser::maximumBoxRect(int field) const { return maximumBoxAt(fieldTop(field)); }
RECT Visualiser::sliderRect(int field) const { return sliderAt(fieldTop(field)); }

// Sizes the menu panel to the window (adding a scroll bar if the menu doesn't fit), then places
// the canvas to its right.
void Visualiser::layoutMenu() {
    if (panel_ == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const int contentHeight = menuHeightFor(fieldsBottom());
    const int available = std::max(1, static_cast<int>(client.bottom) - 2 * margin);
    const bool scrolls = contentHeight > available;
    menuVisibleHeight_ = scrolls ? available : contentHeight;
    menuScroll_ = std::max(0, std::min(menuScroll_, contentHeight - menuVisibleHeight_));

    SCROLLINFO scroll{};
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMin = 0;
    scroll.nMax = contentHeight - 1;
    scroll.nPage = static_cast<UINT>(menuVisibleHeight_);
    scroll.nPos = menuScroll_;
    SetScrollInfo(panel_, SB_VERT, &scroll, TRUE);  // Hides the bar when everything fits.

    const int scrollBarWidth = scrolls ? GetSystemMetrics(SM_CXVSCROLL) : 0;
    MoveWindow(panel_, panelLeft, panelTop, panelWidth + scrollBarWidth, menuVisibleHeight_, TRUE);
    canvasLeft_ = panelLeft + panelWidth + scrollBarWidth + margin;
    layoutCanvas();

    SetWindowTextA(hintLabel_, setType_ == SetType::Mandelbrot
        ? "Scroll over the image to zoom; right-drag to pan.\nDouble-click a point to open its Julia set.\n"
          "Set each slider's range with the boxes at its ends."
        : "Scroll over the image to zoom; right-drag to pan.\nJulia c is the constant in z = z^2 + c.\n"
          "Set each slider's range with the boxes at its ends.");
    positionMenuControls();
}

// Places every menu control at its layout position, shifted up by the scroll offset.
void Visualiser::positionMenuControls() {
    const int bottom = fieldsBottom();
    const int dy = -menuScroll_;
    auto place = [](HWND control, RECT area) {
        MoveWindow(control, area.left, area.top, area.right - area.left, area.bottom - area.top, FALSE);
    };
    // Text boxes sit inside a frame painted by paintMenu, so their text is vertically centred.
    auto placeEdit = [&](HWND edit, RECT box) {
        const int textTop = box.top + (box.bottom - box.top - lineHeight_) / 2;
        place(edit, RECT{box.left + 6, textTop, box.right - 6, textTop + lineHeight_});
    };

    place(titleLabel_, offsetRect(RECT{contentLeft, titleTop, contentRight, titleTop + lineHeight_ + 8}, dy));
    const int segmentWidth = (contentRight - contentLeft) / 2;
    place(mandelbrotButton_, offsetRect(RECT{contentLeft, selectorTop, contentLeft + segmentWidth,
        selectorTop + selectorHeight}, dy));
    place(juliaButton_, offsetRect(RECT{contentLeft + segmentWidth, selectorTop, contentRight,
        selectorTop + selectorHeight}, dy));

    const bool mandelbrot = setType_ == SetType::Mandelbrot;
    ShowWindow(formulaLabel_, mandelbrot ? SW_SHOW : SW_HIDE);
    const int headingTop = formulaHeadingTop() + dy;
    place(formulaLabel_, RECT{contentLeft, headingTop + 6, contentRight, headingTop + 6 + lineHeight_ + 2});

    const int colouringTop = colouringRowTop() + dy;
    const int colouringLabelTop = colouringTop + (boxHeight - lineHeight_) / 2;
    place(colouringLabel_, RECT{contentLeft, colouringLabelTop, boxLeft - 8, colouringLabelTop + lineHeight_ + 2});
    const int toggleMiddle = boxLeft + (contentRight - boxLeft) / 2;
    place(smoothButton_, RECT{boxLeft, colouringTop, toggleMiddle, colouringTop + boxHeight});
    place(bandsButton_, RECT{toggleMiddle, colouringTop, contentRight, colouringTop + boxHeight});

    for (int field = 0; field < fieldCount; ++field) {
        const bool visible = isFieldVisible(field);
        HWND parts[] = {fieldLabels_[field], valueInputs_[field], minimumInputs_[field], sliders_[field],
            maximumInputs_[field]};
        for (HWND part : parts) {
            ShowWindow(part, visible ? SW_SHOW : SW_HIDE);
        }
        if (!visible) {
            continue;
        }
        const RECT value = offsetRect(valueBoxRect(field), dy);
        const int labelTop = value.top + (boxHeight - lineHeight_) / 2;
        place(fieldLabels_[field], RECT{contentLeft, labelTop, boxLeft - 8, labelTop + lineHeight_ + 2});
        placeEdit(valueInputs_[field], value);
        placeEdit(minimumInputs_[field], offsetRect(minimumBoxRect(field), dy));
        placeEdit(maximumInputs_[field], offsetRect(maximumBoxRect(field), dy));
        place(sliders_[field], offsetRect(sliderRect(field), dy));
    }

    const int buttonWidth = (contentRight - contentLeft - buttonGap) / 2;
    const int buttonTop = buttonTopFor(bottom) + dy;
    place(renderButton_, RECT{contentLeft, buttonTop, contentLeft + buttonWidth, buttonTop + buttonHeight});
    place(resetButton_, RECT{contentRight - buttonWidth, buttonTop, contentRight, buttonTop + buttonHeight});
    place(hintLabel_, RECT{contentLeft, hintTopFor(bottom) + dy, contentRight, hintTopFor(bottom) + dy + hintHeight});
    place(statusLabel_, RECT{contentLeft, statusTopFor(bottom) + dy, contentRight,
        statusTopFor(bottom) + dy + statusHeight});

    RedrawWindow(panel_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void Visualiser::scrollMenuTo(int position) {
    const int limit = std::max(0, menuHeightFor(fieldsBottom()) - menuVisibleHeight_);
    position = std::max(0, std::min(position, limit));
    if (position == menuScroll_) {
        return;
    }
    menuScroll_ = position;
    SetScrollPos(panel_, SB_VERT, menuScroll_, TRUE);
    positionMenuControls();
}

// Scrolls just enough to bring an (unscrolled) area of the menu into view, e.g. when tabbing.
void Visualiser::scrollMenuToShow(RECT area) {
    if (area.top - 8 < menuScroll_) {
        scrollMenuTo(area.top - 8);
    } else if (area.bottom + 8 > menuScroll_ + menuVisibleHeight_) {
        scrollMenuTo(area.bottom + 8 - menuVisibleHeight_);
    }
}

void Visualiser::invalidateMenuArea(RECT area) {
    area = offsetRect(area, -menuScroll_);
    InflateRect(&area, 1, 1);
    InvalidateRect(panel_, &area, FALSE);
}

void Visualiser::paintMenu(HDC deviceContext) {
    RECT bounds{};
    GetClientRect(panel_, &bounds);
    frameRect(deviceContext, bounds, panelBorderColor);

    const int dy = -menuScroll_;
    HWND focused = GetFocus();
    auto drawBox = [&](RECT box, HWND edit) {
        box = offsetRect(box, dy);
        FillRect(deviceContext, &box, inputBrush_);
        frameRect(deviceContext, box, focused == edit ? accentColor : inputBorderColor);
    };
    for (int field = 0; field < fieldCount; ++field) {
        if (!isFieldVisible(field)) {
            continue;
        }
        drawBox(valueBoxRect(field), valueInputs_[field]);
        drawBox(minimumBoxRect(field), minimumInputs_[field]);
        drawBox(maximumBoxRect(field), maximumInputs_[field]);
    }

    if (setType_ == SetType::Mandelbrot) {
        // A rule above the formula heading separates the formula constants from the view.
        const int ruleTop = formulaHeadingTop() + dy - 4;
        fillRect(deviceContext, RECT{contentLeft, ruleTop, contentRight, ruleTop + 1}, panelBorderColor);
    }
    const int separatorTop = separatorTopFor(fieldsBottom()) + dy;
    const RECT separator{contentLeft, separatorTop, contentRight, separatorTop + 1};
    fillRect(deviceContext, separator, panelBorderColor);
}

void Visualiser::drawButton(const DRAWITEMSTRUCT& item) {
    HDC deviceContext = item.hDC;
    const RECT bounds = item.rcItem;
    FillRect(deviceContext, &bounds, panelBrush_);

    if (item.hwndItem == mandelbrotButton_ || item.hwndItem == juliaButton_) {
        drawToggleButton(item, (item.hwndItem == mandelbrotButton_) == (setType_ == SetType::Mandelbrot));
        return;
    }
    if (item.hwndItem == smoothButton_ || item.hwndItem == bandsButton_) {
        drawToggleButton(item, (item.hwndItem == smoothButton_) == view_.colouring.smooth);
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

// One half of a two-way switch (Mandelbrot | Julia, Smooth | Bands). The selected half is filled
// and underlined.
void Visualiser::drawToggleButton(const DRAWITEMSTRUCT& item, bool selected) {
    HDC deviceContext = item.hDC;
    const RECT bounds = item.rcItem;
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

// Track with the part left of the thumb filled in the accent colour, and a round thumb.
void Visualiser::paintSlider(HWND slider, int field) {
    PAINTSTRUCT paint{};
    HDC target = BeginPaint(slider, &paint);
    RECT bounds{};
    GetClientRect(slider, &bounds);
    const int width = bounds.right;
    const int height = bounds.bottom;

    HDC deviceContext = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1, width), std::max(1, height));
    HGDIOBJ previousBitmap = SelectObject(deviceContext, bitmap);
    FillRect(deviceContext, &bounds, panelBrush_);

    const int radius = 7;
    const int trackLeft = radius;
    const int trackRight = std::max(trackLeft + 1, width - radius);
    const int middle = height / 2;
    const int thumbX = trackLeft + static_cast<int>(std::lround(sliderFraction(field) * (trackRight - trackLeft)));
    fillRect(deviceContext, RECT{trackLeft, middle - 2, trackRight, middle + 2}, inputBorderColor);
    fillRect(deviceContext, RECT{trackLeft, middle - 2, thumbX, middle + 2}, accentColor);

    const bool focused = GetFocus() == slider;
    HBRUSH brush = CreateSolidBrush(textColor);
    HPEN pen = CreatePen(PS_SOLID, focused ? 3 : 1, focused ? accentColor : textColor);
    HGDIOBJ previousBrush = SelectObject(deviceContext, brush);
    HGDIOBJ previousPen = SelectObject(deviceContext, pen);
    Ellipse(deviceContext, thumbX - radius, middle - radius, thumbX + radius + 1, middle + radius + 1);
    SelectObject(deviceContext, previousBrush);
    SelectObject(deviceContext, previousPen);
    DeleteObject(brush);
    DeleteObject(pen);

    BitBlt(target, 0, 0, width, height, deviceContext, 0, 0, SRCCOPY);
    SelectObject(deviceContext, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(deviceContext);
    EndPaint(slider, &paint);
}

Visualiser::SliderRange& Visualiser::range(int field) {
    return ranges_[static_cast<int>(setType_)][field];
}

Visualiser::SliderRange Visualiser::defaultRange(SetType type, int field) {
    const bool julia = type == SetType::Julia;
    switch (field) {
    case centreRealField: return julia ? SliderRange{-2.0, 2.0} : SliderRange{-2.0, 1.0};
    case centreImaginaryField: return julia ? SliderRange{-2.0, 2.0} : SliderRange{-1.5, 1.5};
    case zoomField: return SliderRange{0.5, 1000000.0};
    case iterationsField: return SliderRange{20.0, 2000.0};
    case escapeRadiusField: return SliderRange{2.0, 10.0};
    case colourCycleField: return SliderRange{2.0, 512.0};
    case juliaRealField: return SliderRange{-2.0, 1.0};
    case juliaImaginaryField: return SliderRange{-1.5, 1.5};
    case exponentField: return SliderRange{1.0, 8.0};
    default: return SliderRange{-2.0, 2.0};  // Formula constants a, c and z0.
    }
}

double Visualiser::fieldValue(int field) const {
    switch (field) {
    case centreRealField: return view_.centerReal;
    case centreImaginaryField: return view_.centerImaginary;
    case zoomField: return view_.zoom;
    case iterationsField: return view_.maxIterations;
    case escapeRadiusField: return view_.escapeRadius;
    case colourCycleField: return view_.colouring.cycleLength;
    case juliaRealField: return view_.juliaReal;
    case juliaImaginaryField: return view_.juliaImaginary;
    case aRealField: return view_.formula.aReal;
    case aImaginaryField: return view_.formula.aImaginary;
    case exponentField: return view_.formula.exponent;
    case cRealField: return view_.formula.cReal;
    case cImaginaryField: return view_.formula.cImaginary;
    case startRealField: return view_.formula.startReal;
    default: return view_.formula.startImaginary;
    }
}

void Visualiser::setFieldValue(int field, double value) {
    switch (field) {
    case centreRealField: view_.centerReal = value; break;
    case centreImaginaryField: view_.centerImaginary = value; break;
    case zoomField: view_.zoom = std::max(minimumZoom, std::min(maximumZoom, value)); break;
    case iterationsField:
        view_.maxIterations = static_cast<int>(std::lround(std::max(1.0, std::min(value, 1000000.0))));
        break;
    case escapeRadiusField: view_.escapeRadius = value; break;
    case colourCycleField: view_.colouring.cycleLength = value; break;
    case juliaRealField: view_.juliaReal = value; break;
    case juliaImaginaryField: view_.juliaImaginary = value; break;
    case aRealField: view_.formula.aReal = value; break;
    case aImaginaryField: view_.formula.aImaginary = value; break;
    case exponentField: view_.formula.exponent = value; break;
    case cRealField: view_.formula.cReal = value; break;
    case cImaginaryField: view_.formula.cImaginary = value; break;
    case startRealField: view_.formula.startReal = value; break;
    default: view_.formula.startImaginary = value; break;
    }
}

// Reads one value box. Returns false (leaving value unspecified) if the text isn't valid there.
bool Visualiser::parseField(int field, double& value) const {
    if (!parseNumber(valueInputs_[field], value)) {
        return false;
    }
    switch (field) {
    case zoomField: return value > 0.0;
    case iterationsField: return value >= 1.0 && value <= 1000000.0 && value == std::floor(value);
    case escapeRadiusField: return value > 0.0;
    case colourCycleField: return value > 0.0;
    default: return true;
    }
}

// Where the thumb sits, 0 (left end) to 1 (right end). Values outside the range pin to an end.
double Visualiser::sliderFraction(int field) const {
    const SliderRange& limits = ranges_[static_cast<int>(setType_)][field];
    const double value = fieldValue(field);
    double fraction = 0.0;
    if (fieldInfo[field].logarithmic) {
        fraction = (std::log(std::max(value, 1e-300)) - std::log(limits.minimum)) /
            (std::log(limits.maximum) - std::log(limits.minimum));
    } else {
        fraction = (value - limits.minimum) / (limits.maximum - limits.minimum);
    }
    return std::isfinite(fraction) ? std::max(0.0, std::min(1.0, fraction)) : 0.0;
}

void Visualiser::setFieldFromSlider(int field, double fraction) {
    fraction = std::max(0.0, std::min(1.0, fraction));
    const SliderRange& limits = range(field);
    double value = fieldInfo[field].logarithmic
        ? std::exp(std::log(limits.minimum) + fraction * (std::log(limits.maximum) - std::log(limits.minimum)))
        : limits.minimum + fraction * (limits.maximum - limits.minimum);
    if (fieldInfo[field].integer) {
        value = std::round(value);
    }
    if (value == fieldValue(field)) {
        return;
    }
    setFieldValue(field, value);
    writeField(field);
    requestRender();
}

// Typing in a value box updates the image as you type, whenever the text is a valid value.
void Visualiser::onValueEdited(int field) {
    double value = 0.0;
    if (writingInputs_ || !parseField(field, value) || value == fieldValue(field)) {
        return;
    }
    setFieldValue(field, value);
    InvalidateRect(sliders_[field], nullptr, FALSE);
    requestRender();
}

// A new slider range takes effect as soon as both ends are valid and minimum < maximum.
void Visualiser::onRangeEdited(int field) {
    double minimum = 0.0;
    double maximum = 0.0;
    if (writingInputs_ || !parseNumber(minimumInputs_[field], minimum) ||
        !parseNumber(maximumInputs_[field], maximum) || minimum >= maximum ||
        (fieldInfo[field].logarithmic && minimum <= 0.0)) {
        return;
    }
    range(field) = SliderRange{minimum, maximum};
    InvalidateRect(sliders_[field], nullptr, FALSE);
}

// Validates every visible field (for the Render button), reporting the first problem.
bool Visualiser::readInputs() {
    const char* problems[] = {
        "Centre real must be a number.",
        "Centre imaginary must be a number.",
        "Zoom must be a number greater than 0.",
        "Max iterations must be a whole number from 1 to 1000000.",
        "Escape radius must be a number greater than 0.",
        "Colour cycle must be a number greater than 0.",
        "Julia c real must be a number.",
        "Julia c imaginary must be a number.",
        "a real must be a number.",
        "a imaginary must be a number.",
        "The exponent b must be a number.",
        "c real must be a number.",
        "c imaginary must be a number.",
        "Start z0 real must be a number.",
        "Start z0 imaginary must be a number.",
    };
    double values[fieldCount] = {};
    for (int field = 0; field < fieldCount; ++field) {
        if (isFieldVisible(field) && !parseField(field, values[field])) {
            setStatus(problems[field]);
            SetFocus(valueInputs_[field]);
            return false;
        }
    }
    for (int field = 0; field < fieldCount; ++field) {
        if (!isFieldVisible(field)) {
            continue;
        }
        setFieldValue(field, values[field]);
        InvalidateRect(sliders_[field], nullptr, FALSE);
    }
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

// Each set keeps its own view and slider ranges, so switching back returns to where you were.
void Visualiser::switchSet(SetType type) {
    if (type == setType_) {
        return;
    }
    endPan();
    savedViews_[static_cast<int>(setType_)] = view_;
    setType_ = type;
    view_ = savedViews_[static_cast<int>(type)];
    writeInputs();
    for (int field = 0; field < fieldCount; ++field) {
        writeRange(field);
    }
    layoutMenu();
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

void Visualiser::writeField(int field) {
    char value[64]{};
    if (fieldInfo[field].integer) {
        std::snprintf(value, sizeof(value), "%d", static_cast<int>(fieldValue(field)));
    } else {
        std::snprintf(value, sizeof(value), "%.15g", fieldValue(field));
    }
    writingInputs_ = true;
    SetWindowTextA(valueInputs_[field], value);
    writingInputs_ = false;
    InvalidateRect(sliders_[field], nullptr, FALSE);
}

void Visualiser::writeRange(int field) {
    const SliderRange& limits = range(field);
    char value[64]{};
    writingInputs_ = true;
    std::snprintf(value, sizeof(value), "%.6g", limits.minimum);
    SetWindowTextA(minimumInputs_[field], value);
    std::snprintf(value, sizeof(value), "%.6g", limits.maximum);
    SetWindowTextA(maximumInputs_[field], value);
    writingInputs_ = false;
    InvalidateRect(sliders_[field], nullptr, FALSE);
}

void Visualiser::writeInputs() {
    for (int field = 0; field < fieldCount; ++field) {
        writeField(field);
    }
}

void Visualiser::writePositionInputs() {
    writeField(centreRealField);
    writeField(centreImaginaryField);
    writeField(zoomField);
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
                    view.centerImaginary, view.zoom, view.maxIterations, view.escapeRadius, view.formula,
                    view.colouring, &cancelRender_, step, reuseCoarser)
                : calculator_.renderJulia(buffer, request.width, request.height, view.centerReal,
                    view.centerImaginary, view.zoom, view.juliaReal, view.juliaImaginary,
                    view.maxIterations, view.escapeRadius, view.colouring, &cancelRender_, step, reuseCoarser);
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

LRESULT CALLBACK Visualiser::panelProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createData = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createData->lpCreateParams));
    }
    auto* visualiser = reinterpret_cast<Visualiser*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (visualiser == nullptr || visualiser->panel_ != window) {
        return DefWindowProcA(window, message, wParam, lParam);
    }
    return visualiser->handlePanelMessage(message, wParam, lParam);
}

LRESULT Visualiser::handlePanelMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC deviceContext = BeginPaint(panel_, &paint);
        paintMenu(deviceContext);
        EndPaint(panel_, &paint);
        return 0;
    }
    case WM_COMMAND:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_DRAWITEM:
        // The menu's controls report to the panel; handle them in one place with the main window.
        return handleWindowMessage(message, wParam, lParam);
    case WM_VSCROLL: {
        int position = menuScroll_;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: position -= 24; break;
        case SB_LINEDOWN: position += 24; break;
        case SB_PAGEUP: position -= menuVisibleHeight_; break;
        case SB_PAGEDOWN: position += menuVisibleHeight_; break;
        case SB_TOP: position = 0; break;
        case SB_BOTTOM: position = menuHeightFor(fieldsBottom()); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO scroll{};
            scroll.cbSize = sizeof(scroll);
            scroll.fMask = SIF_TRACKPOS;
            GetScrollInfo(panel_, SB_VERT, &scroll);
            position = scroll.nTrackPos;
            break;
        }
        default: break;
        }
        scrollMenuTo(position);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // Scroll the menu when the cursor is over it; otherwise let the main window zoom.
        const POINT cursor = pointFromLParam(lParam);
        RECT bounds{};
        GetWindowRect(panel_, &bounds);
        if (PtInRect(&bounds, cursor)) {
            scrollMenuTo(menuScroll_ - GET_WHEEL_DELTA_WPARAM(wParam) * 48 / WHEEL_DELTA);
            return 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcA(panel_, message, wParam, lParam);
}

LRESULT CALLBACK Visualiser::sliderProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createData = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createData->lpCreateParams));
    }
    auto* visualiser = reinterpret_cast<Visualiser*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    const int field = GetDlgCtrlID(window) - firstSliderId;
    if (visualiser == nullptr || field < 0 || field >= fieldCount || visualiser->sliders_[field] != window) {
        return DefWindowProcA(window, message, wParam, lParam);
    }
    return visualiser->handleSliderMessage(window, field, message, wParam, lParam);
}

LRESULT Visualiser::handleSliderMessage(HWND slider, int field, UINT message, WPARAM wParam, LPARAM lParam) {
    auto fractionAt = [slider](int x) {
        RECT bounds{};
        GetClientRect(slider, &bounds);
        const int radius = 7;  // Matches the thumb radius in paintSlider.
        return static_cast<double>(x - radius) / std::max(1, static_cast<int>(bounds.right) - 2 * radius);
    };

    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paintSlider(slider, field);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(slider);
        SetCapture(slider);
        setFieldFromSlider(field, fractionAt(pointFromLParam(lParam).x));
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == slider) {
            setFieldFromSlider(field, fractionAt(pointFromLParam(lParam).x));
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == slider) {
            ReleaseCapture();
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_KEYDOWN: {
        // Arrow keys nudge by 1% of the range, Page Up/Down by 10%, Home/End jump to the ends.
        double step = 0.0;
        switch (wParam) {
        case VK_LEFT: case VK_DOWN: step = -0.01; break;
        case VK_RIGHT: case VK_UP: step = 0.01; break;
        case VK_NEXT: step = -0.1; break;
        case VK_PRIOR: step = 0.1; break;
        case VK_HOME: setFieldFromSlider(field, 0.0); return 0;
        case VK_END: setFieldFromSlider(field, 1.0); return 0;
        default: return DefWindowProcA(slider, message, wParam, lParam);
        }
        if (fieldInfo[field].integer) {
            // Whole-number fields always move by at least 1.
            const SliderRange& limits = range(field);
            const double span = limits.maximum - limits.minimum;
            const double amount = std::max(1.0, std::round(std::fabs(step) * span));
            const double target = fieldValue(field) + (step < 0 ? -amount : amount);
            setFieldFromSlider(field, (target - limits.minimum) / span);
        } else {
            setFieldFromSlider(field, sliderFraction(field) + step);
        }
        return 0;
    }
    case WM_SETFOCUS:
        scrollMenuToShow(sliderRect(field));
        InvalidateRect(slider, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        InvalidateRect(slider, nullptr, FALSE);
        return 0;
    default:
        // WM_MOUSEWHEEL goes on to the panel, so the wheel scrolls the menu, not the slider.
        return DefWindowProcA(slider, message, wParam, lParam);
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
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        const bool rangeBox = id >= firstMinimumInputId && id < firstMaximumInputId + fieldCount;
        SetTextColor(deviceContext, rangeBox ? mutedTextColor : textColor);
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
        if (id >= firstValueInputId && id < firstValueInputId + fieldCount) {
            const int field = id - firstValueInputId;
            if (code == EN_CHANGE) {
                onValueEdited(field);
            } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                if (code == EN_SETFOCUS) {
                    scrollMenuToShow(valueBoxRect(field));
                }
                invalidateMenuArea(valueBoxRect(field));
            }
            return 0;
        }
        const bool minimumBox = id >= firstMinimumInputId && id < firstMinimumInputId + fieldCount;
        const bool maximumBox = id >= firstMaximumInputId && id < firstMaximumInputId + fieldCount;
        if (minimumBox || maximumBox) {
            const int field = id - (minimumBox ? firstMinimumInputId : firstMaximumInputId);
            const RECT box = minimumBox ? minimumBoxRect(field) : maximumBoxRect(field);
            if (code == EN_CHANGE) {
                onRangeEdited(field);
            } else if (code == EN_SETFOCUS) {
                scrollMenuToShow(box);
                invalidateMenuArea(box);
            } else if (code == EN_KILLFOCUS) {
                writeRange(field);  // Puts back the last valid range if the text isn't one.
                invalidateMenuArea(box);
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
        } else if (id == smoothButtonId || id == bandsButtonId) {
            view_.colouring.smooth = id == smoothButtonId;
            InvalidateRect(smoothButton_, nullptr, FALSE);
            InvalidateRect(bandsButton_, nullptr, FALSE);
            requestRender();
        } else if (id == renderButtonId || id == IDOK) {
            if (readInputs()) {
                requestRender();
            }
        } else if (id == resetButtonId) {
            view_ = defaultView(setType_);
            writeInputs();
            InvalidateRect(smoothButton_, nullptr, FALSE);
            InvalidateRect(bandsButton_, nullptr, FALSE);
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
            layoutMenu();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT minimum{0, 0, panelLeft + panelWidth + GetSystemMetrics(SM_CXVSCROLL) + margin + minimumCanvasWidth + margin,
            minimumClientHeight};
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
