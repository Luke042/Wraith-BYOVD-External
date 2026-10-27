#pragma once
#include <mutex>

struct Vec2
{
    float x, y;
};

struct Vec3
{
    float x, y, z;
};

std::vector<Vec2> entity_positions;
std::vector<Vec2> entity_feet_positions;
std::mutex entity_positions_mutex;

namespace overlay
{
    int screen_x = GetSystemMetrics(SM_CXSCREEN);
    int screen_y = GetSystemMetrics(SM_CYSCREEN);
    HWND g_window = nullptr;

    void Render(HWND hwnd)
    {
        HDC hdc = GetDC(hwnd);
        if (!hdc) return;

        RECT rc{ 0, 0, screen_x, screen_y };
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

        HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 255, 0));
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        HBRUSH old_brush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

        std::vector<Vec2> local_heads;
        std::vector<Vec2> local_feet;
        {
            std::lock_guard<std::mutex> lock(entity_positions_mutex);
            local_heads.swap(entity_positions);
            local_feet.swap(entity_feet_positions);
        }

        for (size_t i = 0; i < local_heads.size() && i < local_feet.size(); i++)
        {
            auto& head = local_heads[i];
            auto& feet = local_feet[i];

            float height = feet.y - head.y;
            float width = height * 0.2;

            Rectangle(hdc, (int)(head.x - width), (int)(head.y), (int)(head.x + width), (int)(feet.y));
            MoveToEx(hdc, screen_x / 2, screen_y, nullptr);
            LineTo(hdc, (int)local_feet[i].x, (int)local_feet[i].y);
            
        }

        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(pen);

        ReleaseDC(hwnd, hdc);
    }

    LRESULT __stdcall WindowProc(HWND hwnd, UINT msg, WPARAM wide_param, LPARAM long_param)
    {
        switch (msg)
        {
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
        {
            Render(hwnd);
            return 0;
        }
        }

        return DefWindowProc(hwnd, msg, wide_param, long_param);
    }

    void Initalize()
    {
        HINSTANCE instance = GetModuleHandle(0);
        const wchar_t* class_name = L"Wraith";

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = class_name;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

        if (!RegisterClassExW(&wc))
        {
            std::println("Failed to register window class.");
            std::cin.get();
            return;
        }

        HWND window = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
            class_name, L"Overlay", WS_POPUP,
            0, 0, screen_x, screen_y,
            nullptr, nullptr, instance, nullptr);

        if (!window)
        {
            std::println("Failed to create overlay.");
            std::cin.get();
            return;
        }

        SetLayeredWindowAttributes(window, RGB(0, 0, 0), 0, LWA_COLORKEY);
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);

        g_window = window;

        SetTimer(window, 1, 16, nullptr);

        MSG msg{};
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        g_window = nullptr;
    }
}

bool WorldToScreen(Vec3 world_position, Vec2& screen_position, float* matrix)
{
    float clipX = world_position.x * matrix[0] + world_position.y * matrix[4] + world_position.z * matrix[8] + matrix[12];
    float clipY = world_position.x * matrix[1] + world_position.y * matrix[5] + world_position.z * matrix[9] + matrix[13];
    float clipW = world_position.x * matrix[3] + world_position.y * matrix[7] + world_position.z * matrix[11] + matrix[15];

    if (clipW < 0.1f) return false;

    float ndcX = clipX / clipW;
    float ndcY = clipY / clipW;

    screen_position.x = (overlay::screen_x * 0.5f) * (1.0f + ndcX);
    screen_position.y = (overlay::screen_y * 0.5f) * (1.0f - ndcY);

    return true;
}