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

    Rendering::MeshCreateInfo cubeInfo{};
    glm::vec3 cubeVerts[] = {
        {-1,-1,-1},
        {1,-1,-1},
        {1,-1,1},
        {-1,-1,1},
        {-1,1,-1},
        {1,1,-1},
        {1,1,1},
        {-1,1,1}
    };
    Rendering::Color cubeColors[] = {
        {0,0,0,1},
        {1,0,0,1},
        {1,0,1,1},
        {0,0,1,1},
        {0,1,0,1},
        {1,1,0,1},
        {1,1,1,1},
        {0,1,1,1}
    };
    cubeInfo.vertexCount = 8;
    cubeInfo.position = cubeVerts;
    cubeInfo.color = cubeColors;
    Rendering::Triangle tris[] = {
        {0,2,1},
        {0,3,2},
        {3,7,6},
        {3,6,2},
        {6,5,2},
        {2,5,1},
        {5,0,1},
        {5,4,0},
        {4,7,0},
        {7,3,0},
        {7,4,6},
        {4,5,6}
    };
    cubeInfo.triangleCount = 12;
    cubeInfo.triangles = tris;

    Rendering::MeshHandle cubeMesh = renderer.CreateMesh("Cube", cubeInfo);
    Rendering::Transform cubeTransform = {
        {0,0,0},
        Quaternion::Identity(),
        {1,1,1}
    };

    Rendering::ShaderDataLayout shaderLayout{};
    shaderLayout.dataSize = 0;
    shaderLayout.propertyCount = 0;

    Rendering::ShaderCreateInfo shaderInfo{};
    shaderInfo.metadata.layer = Rendering::RENDER_LAYER_OPAQUE;
    shaderInfo.metadata.dataLayout = shaderLayout;
    shaderInfo.vertexInputs = (Rendering::VertexAttribFlags)(Rendering::VERTEX_POSITION_BIT | Rendering::VERTEX_COLOR_BIT);
    shaderInfo.samplerCount = 0;
    shaderInfo.vert = "shaders/vert.spv";
    shaderInfo.frag = "shaders/test_frag.spv";

    Rendering::ShaderHandle shader = renderer.CreateShader("TestShader", shaderInfo);

    Rendering::MaterialCreateInfo matInfo{};
    matInfo.metadata.shader = shader;
    matInfo.metadata.castShadows = true;

    Rendering::MaterialHandle material = renderer.CreateMaterial("TestMat", matInfo);

    Rendering::Transform camTransform = {
        {0,0,10},
        Quaternion::Identity(),
        {1,1,1}
    };
    renderer.UpdateCamera(camTransform);

    u64 time = GetTickCount64();

    MSG message;
    running = true;
    while (running) {
        while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
            }

            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        u64 newTime = GetTickCount64();
        u64 deltaTime = newTime - time;
        r32 deltaTimeSeconds = deltaTime / 1000.0f;
        time = newTime;

        r32 cubeAngle = glm::radians(time * 0.18f);
        cubeTransform.rotation = Quaternion::AngleAxis(cubeAngle, { 0,1,0 });

        renderer.DrawMesh(cubeMesh, material, cubeTransform);

        renderer.Render();
    }

    return 0;
}