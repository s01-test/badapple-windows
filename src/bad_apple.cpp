#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <ShellScalingApi.h>
#include <dwmapi.h>
#include <mmsystem.h>

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <string>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

// Constant definitions
#pragma comment(linker, "/subsystem:windows /entry:mainCRTStartup")

namespace BadApple {


    namespace Constants {
        constexpr int IDI_MAIN_ICON = 101;
        constexpr int IDR_VIDEO_BIN = 102;
        constexpr int IDR_WAVE_BGM  = 103;
        
        constexpr int MAX_PHYSICAL_WINDOWS = 150;
        constexpr const wchar_t* CLASS_NAME_BG = L"BadApple_BG";
        constexpr const wchar_t* CLASS_NAME_PIXEL = L"BadApple_Pixel";
    }


    // Utilities (Simple COM smart pointer)

    template <typename T>
    class ComPtr {
        T* ptr_ = nullptr;
    public:
        ComPtr() = default;
        ~ComPtr() { Release(); }
        ComPtr(const ComPtr&) = delete;
        ComPtr& operator=(const ComPtr&) = delete;

        T** GetAddressOf() { Release(); return &ptr_; }
        T* Get() const { return ptr_; }
        T* operator->() const { return ptr_; }
        
        void Release() {
            if (ptr_) { ptr_->Release(); ptr_ = nullptr; }
        }
        operator bool() const { return ptr_ != nullptr; }
    };


    struct RectData { uint16_t x, y, w, h; };
    struct RunData { uint16_t y, x, len; };
    struct FrameData { 
        std::vector<RectData> big_rects; 
        std::vector<RunData> runs; 
    };
    struct VideoData {
        uint32_t width, height, fps;
        std::vector<FrameData> frames;
    };


    // Resource management

    class ResourceLoader {
    public:
        static VideoData LoadVideoData(HINSTANCE hInstance) {
            HRSRC hRes = FindResource(hInstance, MAKEINTRESOURCE(Constants::IDR_VIDEO_BIN), RT_RCDATA);
            if (!hRes) throw std::runtime_error("Video resource not found.");

            HGLOBAL hData = LoadResource(hInstance, hRes);
            if (!hData) throw std::runtime_error("Video resource load failed.");

            void* pData = LockResource(hData);
            DWORD dwSize = SizeofResource(hInstance, hRes);
            
            return Parse(pData, dwSize);
        }

    private:
        static VideoData Parse(const void* data, size_t size) {
            VideoData vData;
            const uint8_t* ptr = static_cast<const uint8_t*>(data);
            const uint8_t* end = ptr + size;

            auto read_u32 = [&](uint32_t& val) {
                if (ptr + sizeof(uint32_t) > end) throw std::runtime_error("Unexpected EOF");
                memcpy(&val, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            };
            auto read_u16 = [&](uint16_t& val) {
                if (ptr + sizeof(uint16_t) > end) throw std::runtime_error("Unexpected EOF");
                memcpy(&val, ptr, sizeof(uint16_t)); ptr += sizeof(uint16_t);
            };

            uint32_t frame_count;
            read_u32(vData.width); read_u32(vData.height); read_u32(vData.fps); read_u32(frame_count);

            vData.frames.reserve(frame_count);
            for (uint32_t i = 0; i < frame_count; ++i) {
                FrameData frame;
                uint32_t rect_len, run_len;
                read_u32(rect_len);
                frame.big_rects.resize(rect_len);
                for (auto& r : frame.big_rects) {
                    read_u16(r.x); read_u16(r.y); read_u16(r.w); read_u16(r.h);
                }
                read_u32(run_len);
                frame.runs.resize(run_len);
                for (auto& r : frame.runs) {
                    read_u16(r.y); read_u16(r.x); read_u16(r.len);
                }
                vData.frames.push_back(std::move(frame));
            }
            return vData;
        }
    };

    class SoundPlayer {
    public:
        static void Play(HINSTANCE hInstance) {
            PlaySoundW(MAKEINTRESOURCEW(Constants::IDR_WAVE_BGM), hInstance, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
        }
        static void Stop() {
            PlaySoundW(NULL, 0, 0);
        }
    };


    class Metrics {
        NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
        HFONT hCaptionFont = NULL;
        int captionHeight = 0;
        int buttonWidth = 0;
        int buttonHeight = 0;

        Metrics() {
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0);
            hCaptionFont = CreateFontIndirectW(&ncm.lfCaptionFont);
            captionHeight = std::max((int)ncm.iCaptionHeight, 20);
            buttonWidth = GetSystemMetrics(SM_CXSIZE);
            buttonHeight = GetSystemMetrics(SM_CYSIZE);
        }

    public:
        ~Metrics() { if (hCaptionFont) DeleteObject(hCaptionFont); }
        static Metrics& Get() { static Metrics instance; return instance; }

        HFONT GetFont() const { return hCaptionFont; }
        int GetCaptionHeight() const { return captionHeight; }
        int GetButtonHeight() const { return buttonHeight; }
    };


    LRESULT CALLBACK BgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_PAINT: ValidateRect(hwnd, NULL); return 0;
        case WM_ERASEBKGND: return 1;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    LRESULT CALLBACK PixelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_NCHITTEST: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            ScreenToClient(hwnd, &pt);
            if (pt.y < Metrics::Get().GetCaptionHeight()) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            int capH = Metrics::Get().GetCaptionHeight();

            FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

            if (w >= 60 && h >= capH) {
                RECT rcTitle = { 0, 0, w, capH };
                HBRUSH hBrTitle = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
                FillRect(hdc, &rcTitle, hBrTitle);
                DeleteObject(hBrTitle);

                int btnSize = std::min(capH - 4, Metrics::Get().GetButtonHeight());
                RECT rcClose = { w - btnSize - 2, 2, w - 2, 2 + btnSize };
                DrawFrameControl(hdc, &rcClose, DFC_CAPTION, DFCS_CAPTIONCLOSE | DFCS_FLAT);
                RECT rcMax = { rcClose.left - btnSize - 2, 2, rcClose.left - 2, 2 + btnSize };
                DrawFrameControl(hdc, &rcMax, DFC_CAPTION, DFCS_CAPTIONMAX | DFCS_FLAT);
                RECT rcMin = { rcMax.left - btnSize - 2, 2, rcMax.left - 2, 2 + btnSize };
                DrawFrameControl(hdc, &rcMin, DFC_CAPTION, DFCS_CAPTIONMIN | DFCS_FLAT);

                HFONT hOldFont = (HFONT)SelectObject(hdc, Metrics::Get().GetFont());
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, GetSysColor(COLOR_CAPTIONTEXT));
                RECT rcText = { 6, 0, rcMin.left - 4, capH };
                DrawTextW(hdc, L"Bad Apple", -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(hdc, hOldFont);
            }
            DrawEdge(hdc, &rc, EDGE_RAISED, BF_RECT);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }


    // Direct2D Renderer

    class D2DRenderer {
        ComPtr<ID2D1Factory> factory;
        ComPtr<ID2D1HwndRenderTarget> renderTarget;
        ComPtr<ID2D1SolidColorBrush> brush;
        float scale = 1.0f;

    public:
        void Initialize(HWND hwnd, int width, int height, float drawScale) {
            scale = drawScale;
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());

            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
            props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
            props.dpiX = 96.0f; props.dpiY = 96.0f;

            factory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)), renderTarget.GetAddressOf());
            renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush.GetAddressOf());
        }

        void DrawFrame(const FrameData& frame) {
            if (!renderTarget) return;
            
            renderTarget->BeginDraw();
            renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
            renderTarget->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(0, 0)));

            for (const auto& r : frame.big_rects) {
                D2D1_RECT_F rect = D2D1::RectF((float)r.x, (float)r.y, (float)(r.x + r.w), (float)(r.y + r.h));
                renderTarget->FillRectangle(&rect, brush.Get());
            }
            for (const auto& run : frame.runs) {
                D2D1_RECT_F rect = D2D1::RectF((float)run.x, (float)run.y, (float)(run.x + run.len), (float)(run.y + 1));
                renderTarget->FillRectangle(&rect, brush.Get());
            }
            renderTarget->EndDraw();
        }
    };


    // Physical window management (DeferredWindow)

    class DeferredWindow {
        HWND hwnd;
        int x = 0, y = 0, w = 0, h = 0;
        bool visible = false;
        bool pos_stale = true, sz_stale = true, visible_stale = true;

    public:
        DeferredWindow(HINSTANCE hInstance) {
            hwnd = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                Constants::CLASS_NAME_PIXEL, NULL, WS_POPUP,
                -10000, -10000, 0, 0, NULL, NULL, hInstance, NULL
            );
        }

        void Update(int _x, int _y, int _w, int _h, bool _visible) {
            if (visible != _visible) { visible = _visible; visible_stale = true; }
            if (!_visible) return;

            _w = std::max(1, _w); _h = std::max(1, _h);
            if (w != _w || h != _h) { w = _w; h = _h; sz_stale = true; }
            if (x != _x || y != _y) { x = _x; y = _y; pos_stale = true; }
        }

        bool IsStale() const { return pos_stale || sz_stale || visible_stale; }

        HDWP Apply(HDWP hdwp) {
            UINT flags = SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOZORDER;
            if (!sz_stale) flags |= SWP_NOSIZE;
            if (!pos_stale) flags |= SWP_NOMOVE;

            if (visible_stale) {
                flags |= (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
                if (visible) flags &= ~SWP_NOREDRAW;
            } else {
                if (sz_stale) flags &= ~SWP_NOREDRAW;
                else flags |= SWP_NOREDRAW;
            }
            pos_stale = sz_stale = visible_stale = false;
            return DeferWindowPos(hdwp, hwnd, NULL, x, y, w, h, flags);
        }
    };

    class WindowPool {
        std::vector<DeferredWindow> windows;
        int originX = 0;
        int originY = 0;
        float scale = 1.0f;

    public:
        void Initialize(HINSTANCE hInstance, HWND hParent, float drawScale) {
            scale = drawScale;
            windows.reserve(Constants::MAX_PHYSICAL_WINDOWS);
            for (int i = 0; i < Constants::MAX_PHYSICAL_WINDOWS; ++i) {
                windows.emplace_back(hInstance);
            }
            POINT pt = { 0, 0 };
            ClientToScreen(hParent, &pt);
            originX = pt.x;
            originY = pt.y;
        }

        void Update(const FrameData& frame) {
            int rectsTotal = (int)frame.big_rects.size();
            int activeCount = std::min(rectsTotal, Constants::MAX_PHYSICAL_WINDOWS);
            int staleCount = 0;

            for (int i = 0; i < Constants::MAX_PHYSICAL_WINDOWS; ++i) {
                if (i < activeCount) {
                    const auto& r = frame.big_rects[i];
                    windows[i].Update(
                        originX + (int)(r.x * scale),
                        originY + (int)(r.y * scale),
                        (int)(r.w * scale),
                        (int)(r.h * scale),
                        true
                    );
                } else {
                    windows[i].Update(0, 0, 0, 0, false);
                }
                if (windows[i].IsStale()) staleCount++;
            }

            if (staleCount > 0) {
                HDWP hdwp = BeginDeferWindowPos(staleCount);
                if (hdwp) {
                    for (auto& win : windows) {
                        if (win.IsStale()) hdwp = win.Apply(hdwp);
                    }
                    EndDeferWindowPos(hdwp);
                }
            }
        }
    };


    // Application management

    class Application {
        HINSTANCE hInstance;
        VideoData videoData;
        D2DRenderer renderer;
        WindowPool windowPool;
        HWND hBgWnd = NULL;

        void RegisterClasses() {
            HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(Constants::IDI_MAIN_ICON));
            
            WNDCLASSEXW wcBg = { sizeof(WNDCLASSEX), 0, BgWndProc, 0, 0, hInstance, hIcon, LoadCursor(NULL, IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), NULL, Constants::CLASS_NAME_BG, hIcon };
            RegisterClassExW(&wcBg);

            WNDCLASSEXW wcPx = { sizeof(WNDCLASSEX), 0, PixelWndProc, 0, 0, hInstance, hIcon, NULL, NULL, NULL, Constants::CLASS_NAME_PIXEL, hIcon };
            RegisterClassExW(&wcPx);
        }

        void CreateBackgroundWindow(float& outScale, int& outW, int& outH) {
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            
            outScale = std::min((float)screenW / videoData.width, (float)screenH / videoData.height);
            outW = (int)(videoData.width * outScale);
            outH = (int)(videoData.height * outScale);

            hBgWnd = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
                Constants::CLASS_NAME_BG, L"Bad Apple BG",
                WS_POPUP | WS_VISIBLE,
                (screenW - outW) / 2, (screenH - outH) / 2, outW, outH,
                NULL, NULL, hInstance, NULL
            );

            MARGINS margins = { -1 };
            DwmExtendFrameIntoClientArea(hBgWnd, &margins);
        }

    public:
        Application() : hInstance(GetModuleHandle(NULL)) {}

        void Run() {
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            Metrics::Get();

            videoData = ResourceLoader::LoadVideoData(hInstance);
            RegisterClasses();

            float scale;
            int targetW, targetH;
            CreateBackgroundWindow(scale, targetW, targetH);

            renderer.Initialize(hBgWnd, targetW, targetH, scale);
            windowPool.Initialize(hInstance, hBgWnd, scale);

            SoundPlayer::Play(hInstance);
            MainLoop();
            SoundPlayer::Stop();
        }

        void MainLoop() {
            auto start_time = std::chrono::high_resolution_clock::now();
            MSG msg = {};
            size_t current_frame = 0;
            bool running = true;

            while (running) {
                while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) running = false;
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
                size_t target_frame = (size_t)(std::chrono::duration<double>(elapsed).count() * videoData.fps);

                if (target_frame >= videoData.frames.size()) break;

                if (target_frame != current_frame) {
                    current_frame = target_frame;
                    const auto& frame = videoData.frames[current_frame];
                    
                    renderer.DrawFrame(frame);
                    DwmFlush();
                    windowPool.Update(frame);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
    };
}

int main() {
    try {
        BadApple::Application app;
        app.Run();
    } catch (const std::exception& e) {
        MessageBoxA(NULL, e.what(), "Error", MB_ICONERROR);
        return 1;
    }
    return 0;
}