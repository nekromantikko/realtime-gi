#include <windows.h>
#include "system.h"
#include "renderer.h"

static bool running;
static Rendering::Renderer* rendererPtr; // Stupid hack...

LRESULT CALLBACK MainWindowCallback(_In_ HWND hwnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam) {
    LRESULT result = 0;

    switch (uMsg)
    {
        case WM_EXITSIZEMOVE:
        {
            if (rendererPtr) {
                rendererPtr->ResizeSurface();
            }
            break;
        }
        case WM_DESTROY:
        {
            running = false;
            break;
        }
        case WM_CLOSE:
        {
            running = false;
            break;
        }
        case WM_ACTIVATEAPP:
        {
            break;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            HDC deviceContext = BeginPaint(hwnd, &paint);
            PatBlt(deviceContext, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right - paint.rcPaint.left, paint.rcPaint.bottom - paint.rcPaint.top, WHITENESS);
            EndPaint(hwnd, &paint);

            break;
        }
        default:
        {
            result = DefWindowProc(hwnd, uMsg, wParam, lParam);
            break;
        }
    }

    return result;
}

int APIENTRY WinMain(_In_ HINSTANCE hInst, _In_ HINSTANCE hInstPrev, _In_ PSTR cmdline, _In_ int cmdshow) {
    WNDCLASSA windowClass = {};

    windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainWindowCallback;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = hInst; // Alternatively => GetModuleHandle(0);
    windowClass.hIcon = 0;
    windowClass.hCursor = 0;
    windowClass.hbrBackground = 0;
    windowClass.lpszMenuName = 0;
    windowClass.lpszClassName = "MainWindowClass";

    RegisterClassA(&windowClass);

    HWND windowHandle = CreateWindowExA(
        0,
        windowClass.lpszClassName,
        "Hello world",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE | WS_THICKFRAME,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1024,
        768,
        0,
        0,
        hInst,
        0
    );

    Rendering::Renderer renderer(hInst, windowHandle);
    rendererPtr = &renderer;

    MSG message;
    running = true;
    while (running) {
        while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
            }

            TranslateMessage(&message);
            DispatchMessage(&message);

            renderer.Render();
        }
    }

    return 0;
}