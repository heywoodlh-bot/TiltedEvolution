#pragma once

#include <chrono>

#include <input/GamepadBindings.h>

struct OverlayService;

/**
 * @brief Handles input handling for the UI.
 */
struct InputService
{
    InputService(OverlayService& aOverlay) noexcept;
    ~InputService() noexcept;

    void Update() noexcept;

    static LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void SetGameInputBlocked(bool aBlocked) noexcept;

    TP_NOCOPYMOVE(InputService);

private:
    GamepadBindings::ToggleBinding m_gamepadToggle;
    uint16_t m_previousGamepadButtons{};
    int8_t m_gamepadNavigation{};
    std::chrono::steady_clock::time_point m_nextGamepadRepeat{};
};
