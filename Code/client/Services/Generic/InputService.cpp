#include <TiltedOnlinePCH.h>

#include <array>
#include <filesystem>

#include <xinput.h>

#include <base/simpleini/SimpleIni.h>
#include <input/GamepadBindings.h>

#include <Services/InputService.h>
#include <Services/OverlayService.h>

#include <OverlayApp.hpp>

#include <DInputHook.hpp>
#include <WindowsHook.hpp>

#include <include/internal/cef_types.h>
#include <Services/ImguiService.h>
#include <Services/DiscordService.h>
#include <World.h>

#include "Games/Skyrim/Interface/MenuControls.h"
#include "Games/Skyrim/AI/Movement/PlayerControls.h"

static OverlayService* s_pOverlay = nullptr;
static UINT s_currentACP = CP_ACP;
static bool s_gameInputStateSaved = false;
static bool s_gameInputWasBlocked = false;

namespace
{
constexpr char kGamepadConfigPath[] = "Data/SkyrimTogetherReborn/config";
constexpr char kGamepadConfigFile[] = "Gamepad.ini";
constexpr char kGamepadConfigSection[] = "Gamepad";
constexpr char kGamepadConfigComment[] =
    "; Controls for the Skyrim Together overlay.\n"
    "; Use one of: NONE, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, START, BACK,\n"
    "; LEFT_THUMB, RIGHT_THUMB, LEFT_SHOULDER, RIGHT_SHOULDER, A, B, X, Y.\n"
    "; The overlay opens when both ToggleModifier and ToggleButton are held.";

using TXInputGetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

TXInputGetState GetXInputGetState() noexcept
{
    static const TXInputGetState s_getState = []() -> TXInputGetState
    {
        constexpr std::array kLibraries{"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll"};
        for (const auto* pLibrary : kLibraries)
        {
            const auto module = LoadLibraryA(pLibrary);
            if (!module)
                continue;

            const auto getState = reinterpret_cast<TXInputGetState>(GetProcAddress(module, "XInputGetState"));
            if (getState)
                return getState;

            FreeLibrary(module);
        }

        return nullptr;
    }();

    return s_getState;
}

std::filesystem::path GetGamepadConfigPath()
{
    return std::filesystem::current_path() / kGamepadConfigPath / kGamepadConfigFile;
}

void CreateDefaultGamepadConfig(const std::filesystem::path& acPath)
{
    std::error_code error;
    std::filesystem::create_directories(acPath.parent_path(), error);
    if (error)
        return;

    CSimpleIni ini(true);
    ini.SetValue(kGamepadConfigSection, nullptr, nullptr, kGamepadConfigComment);
    ini.SetValue(kGamepadConfigSection, "ToggleModifier", "BACK");
    ini.SetValue(kGamepadConfigSection, "ToggleButton", "START");
    ini.SaveFile(acPath.c_str(), true);
}

GamepadBindings::ToggleBinding LoadGamepadToggleBinding() noexcept
{
    GamepadBindings::ToggleBinding binding;

    try
    {
        const auto path = GetGamepadConfigPath();
        std::error_code error;
        if (!std::filesystem::exists(path, error))
            CreateDefaultGamepadConfig(path);
        if (error)
            return binding;

        CSimpleIni ini(true);
        if (const auto result = ini.LoadFile(path.c_str()); result != SI_Error::SI_OK)
        {
            spdlog::warn("Failed to load gamepad configuration from '{}' (error {})", path.string(), static_cast<int>(result));
            return binding;
        }

        const auto modifier = GamepadBindings::ParseButton(ini.GetValue(kGamepadConfigSection, "ToggleModifier", "BACK"));
        const auto button = GamepadBindings::ParseButton(ini.GetValue(kGamepadConfigSection, "ToggleButton", "START"));
        if (!modifier || !button || *button == GamepadBindings::Button::None)
        {
            spdlog::warn("Invalid gamepad toggle binding in '{}'; using BACK+START", path.string());
            return binding;
        }

        binding.Modifier = *modifier;
        binding.Primary = *button;
    }
    catch (...)
    {
        spdlog::warn("Failed to initialize gamepad configuration; using BACK+START");
    }

    return binding;
}

int8_t GetGamepadNavigation(const XINPUT_GAMEPAD& acGamepad) noexcept
{
    if ((acGamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || acGamepad.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        return -1;
    if ((acGamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) || acGamepad.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        return -1;
    if ((acGamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || acGamepad.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        return 1;
    if ((acGamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || acGamepad.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        return 1;
    return 0;
}
} // namespace

void ForceKillAllInput()
{
    MenuControls::GetInstance()->SetToggle(false);
}

uint32_t GetCefModifiers(uint16_t aVirtualKey)
{
    uint32_t modifiers = EVENTFLAG_NONE;

    if (GetKeyState(VK_MENU) & 0x8000)
    {
        modifiers |= EVENTFLAG_ALT_DOWN;
    }

    if (GetKeyState(VK_CONTROL) & 0x8000)
    {
        modifiers |= EVENTFLAG_CONTROL_DOWN;
    }

    if (GetKeyState(VK_SHIFT) & 0x8000)
    {
        modifiers |= EVENTFLAG_SHIFT_DOWN;
    }

    if (GetKeyState(VK_LBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_RBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_MBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_CAPITAL) & 1)
    {
        modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    }

    if (GetKeyState(VK_NUMLOCK) & 1)
    {
        modifiers |= EVENTFLAG_NUM_LOCK_ON;
    }

    if (aVirtualKey)
    {
        if (aVirtualKey == VK_RCONTROL || aVirtualKey == VK_RMENU || aVirtualKey == VK_RSHIFT)
        {
            modifiers |= EVENTFLAG_IS_RIGHT;
        }
        else if (aVirtualKey == VK_LCONTROL || aVirtualKey == VK_LMENU || aVirtualKey == VK_LSHIFT)
        {
            modifiers |= EVENTFLAG_IS_LEFT;
        }
        else if (aVirtualKey >= VK_NUMPAD0 && aVirtualKey <= VK_DIVIDE)
        {
            modifiers |= EVENTFLAG_IS_KEY_PAD;
        }
    }

    return modifiers;
}

// remember to update this when updating toggle keys
bool IsToggleKey(int aKey) noexcept
{
    return aKey == VK_RCONTROL || aKey == VK_F2;
}

bool IsDisableKey(int aKey) noexcept
{
    return aKey == VK_ESCAPE;
}

void SetUIActive(OverlayService& aOverlay, auto apRenderer, bool aActive)
{
    TiltedPhoques::DInputHook::Get().SetEnabled(aActive);
    InputService::SetGameInputBlocked(aActive);
    aOverlay.SetActive(aActive);

    // Ensures the game is actually loaded, in case the initial event was sent too early
    aOverlay.SetVersion(BUILD_COMMIT);
    aOverlay.GetOverlayApp()->ExecuteAsync("enterGame");

    apRenderer->SetCursorVisible(aActive);

    // This is to disable the Windows cursor
    while (ShowCursor(FALSE) >= 0)
        ;
}

void InjectCefKey(const uint16_t aKey, const uint32_t aModifiers = EVENTFLAG_NONE)
{
    if (!s_pOverlay || !s_pOverlay->GetActive())
        return;

    const auto pApp = s_pOverlay->GetOverlayApp();
    if (!pApp)
        return;

    const auto scanCode = static_cast<uint16_t>(MapVirtualKey(aKey, MAPVK_VK_TO_VSC));
    pApp->InjectKey(KEYEVENT_KEYDOWN, aModifiers, aKey, scanCode);
    pApp->InjectKey(KEYEVENT_KEYUP, aModifiers, aKey, scanCode);
}

void ToggleUIFromGamepad()
{
    if (!s_pOverlay || !s_pOverlay->GetInGame())
        return;

    const auto pApp = s_pOverlay->GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    SetUIActive(*s_pOverlay, pRenderer, !s_pOverlay->GetActive());
}

void ProcessKeyboard(uint16_t aKey, uint16_t aScanCode, cef_key_event_type_t aType, bool aE0, bool aE1)
{
    if (aType != KEYEVENT_CHAR)
    {
        if (!aKey || aKey == 255)
        {
            return;
        }

        if (aKey == VK_SHIFT)
        {
            aKey = static_cast<uint16_t>(MapVirtualKey(aScanCode, MAPVK_VSC_TO_VK_EX));
        }
        else if (aKey == VK_NUMLOCK)
        {
            aScanCode = static_cast<uint16_t>(MapVirtualKey(aKey, MAPVK_VK_TO_VSC) | 0x100);
        }

        if (aE1)
        {
            if (aKey == VK_PAUSE)
            {
                aScanCode = 0x45;
            }
            else
            {
                aScanCode = static_cast<uint16_t>(MapVirtualKey(aKey, MAPVK_VK_TO_VSC));
            }
        }

        if (aE0)
        {
            switch (aKey)
            {
            case VK_CONTROL: aKey = VK_RCONTROL; break;
            case VK_MENU: aKey = VK_RMENU; break;
            case VK_RETURN: aKey = VK_SEPARATOR; break;
            }
        }
        else
        {
            switch (aKey)
            {
            case VK_CONTROL: aKey = VK_LCONTROL; break;
            case VK_MENU: aKey = VK_LMENU; break;
            case VK_INSERT: aKey = VK_NUMPAD0; break;
            case VK_DELETE: aKey = VK_DECIMAL; break;
            case VK_HOME: aKey = VK_NUMPAD7; break;
            case VK_END: aKey = VK_NUMPAD1; break;
            case VK_PRIOR: aKey = VK_NUMPAD9; break;
            case VK_NEXT: aKey = VK_NUMPAD3; break;
            case VK_LEFT: aKey = VK_NUMPAD4; break;
            case VK_RIGHT: aKey = VK_NUMPAD6; break;
            case VK_UP: aKey = VK_NUMPAD8; break;
            case VK_DOWN: aKey = VK_NUMPAD2; break;
            case VK_CLEAR: aKey = VK_NUMPAD5; break;
            }
        }
    }

    auto& overlay = *s_pOverlay;

    const auto pApp = overlay.GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    const auto active = overlay.GetActive();

    spdlog::debug("ProcessKey, type: {}, key: {}, active: {}", aType, aKey, active);

    if (aType != KEYEVENT_CHAR && (IsToggleKey(aKey) || (IsDisableKey(aKey) && active)))
    {
        if (!overlay.GetInGame())
        {
            TiltedPhoques::DInputHook::Get().SetEnabled(false);
        }
        else if (aType == KEYEVENT_KEYUP)
        {
            SetUIActive(overlay, pRenderer, !active);
        }
    }
    else if (active)
    {
        pApp->InjectKey(aType, GetCefModifiers(aKey), aKey, aScanCode);
    }
}

void ProcessMouseMove(uint16_t aX, uint16_t aY)
{
    auto& overlay = *s_pOverlay;

    const auto pApp = overlay.GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    const auto active = overlay.GetActive();

    if (active)
    {
        pApp->InjectMouseMove(aX, aY, GetCefModifiers(0));
    }
}

void ProcessMouseButton(uint16_t aX, uint16_t aY, cef_mouse_button_type_t aButton, bool aDown)
{
    auto& overlay = *s_pOverlay;

    const auto pApp = overlay.GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    const auto active = overlay.GetActive();

    if (active)
    {
        pApp->InjectMouseButton(aX, aY, aButton, !aDown, GetCefModifiers(0));
    }
}

void ProcessMouseWheel(uint16_t aX, uint16_t aY, int16_t aZ)
{
    auto& overlay = *s_pOverlay;

    const auto pApp = overlay.GetOverlayApp();
    if (!pApp)
        return;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    const auto active = overlay.GetActive();

    if (active)
    {
        pApp->InjectMouseWheel(aX, aY, aZ, GetCefModifiers(0));
    }
}

UINT GetRealACP()
{
    // Get the keyboard layout for the current thread.
    HKL keybdLayout = GetKeyboardLayout(0);

    // Extract the language ID from it, contained in its low-order word.
    int langID = LOWORD(keybdLayout);

    // Call the GetLocaleInfo function to retrieve the default ANSI code page
    // associated with that language ID.
    UINT acp = CP_ACP;
    GetLocaleInfo(MAKELCID(langID, SORT_DEFAULT),
        LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
        (LPTSTR) &acp,
        sizeof(acp) / sizeof(TCHAR));
    return acp;
}

LRESULT CALLBACK InputService::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    const auto pApp = s_pOverlay->GetOverlayApp();
    if (!pApp)
        return 0;

    const auto pClient = pApp->GetClient();
    if (!pClient)
        return 0;

    const auto pRenderer = pClient->GetOverlayRenderHandler();
    if (!pRenderer)
        return 0;

    auto& discord = World::Get().ctx().at<DiscordService>();
    discord.WndProcHandler(hwnd, uMsg, wParam, lParam);

    const bool active = s_pOverlay->GetActive();
    if (active)
    {
        auto& imgui = World::Get().ctx().at<ImguiService>();
        imgui.WndProcHandler(hwnd, uMsg, wParam, lParam);
    }

    POINT position;

    GetCursorPos(&position);
    ScreenToClient(GetActiveWindow(), &position);

    ProcessMouseMove(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y));

    if (uMsg == WM_INPUT)
    {
        RAWINPUT input;
        UINT size = sizeof(RAWINPUT);

        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));

        if (active)
        {
            auto& imgui = World::Get().ctx().at<ImguiService>();
            imgui.RawInputHandler(input);
        }

        if (input.header.dwType == RIM_TYPEKEYBOARD)
        {
            const auto keyboard = input.data.keyboard;

            ProcessKeyboard(keyboard.VKey, keyboard.MakeCode, keyboard.Flags & RI_KEY_BREAK ? KEYEVENT_KEYUP : KEYEVENT_KEYDOWN, keyboard.Flags & RI_KEY_E0, keyboard.Flags & RI_KEY_E1);
        }
        else if (input.header.dwType == RIM_TYPEMOUSE)
        {
            const auto mouse = input.data.mouse;

            if (mouse.usButtonFlags & RI_MOUSE_WHEEL)
            {
                ProcessMouseWheel(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), (int16_t)mouse.usButtonData);
            }

            if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_LEFT, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_LEFT, false);
            }

            if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_RIGHT, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_RIGHT, false);
            }

            if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_MIDDLE, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_MIDDLE, false);
            }
        }
    }
    else if (uMsg == WM_CHAR)
    {
        uint16_t scancode = (lParam >> 16) & 0xFF;
        uint16_t virtualKey = static_cast<uint16_t>(wParam);
        if (!IsWindowUnicode(hwnd))
        {
            wchar_t wch;
            ::MultiByteToWideChar(s_currentACP, MB_PRECOMPOSED, reinterpret_cast<char*>(&virtualKey), 2, &wch, sizeof(wchar_t));
            virtualKey = wch;
        }
        ProcessKeyboard(virtualKey, scancode, KEYEVENT_CHAR, false, false);
    }
    // If the player tabs out/in with UI visible, this WndProc doesn't run during mouse or keyboard events.
    // When player tabs in, force the UI state
    else if (uMsg == WM_SETFOCUS && s_pOverlay->GetActive())
    {
        TiltedPhoques::DInputHook::Get().SetEnabled(true);
        s_pOverlay->SetActive(true);
        pRenderer->SetCursorVisible(true);
    }
    else if (uMsg == WM_INPUTLANGCHANGE)
    {
        s_currentACP = GetRealACP();
        spdlog::info("Input language changed, current ACP: {}", s_currentACP);
    }

    return 0;
}

InputService::InputService(OverlayService& aOverlay) noexcept
    : m_gamepadToggle(LoadGamepadToggleBinding())
{
    s_pOverlay = &aOverlay;
    s_currentACP = GetRealACP();
}

InputService::~InputService() noexcept
{
    SetGameInputBlocked(false);
    s_pOverlay = nullptr;
}

void InputService::Update() noexcept
{
    const auto getState = GetXInputGetState();
    if (!getState)
        return;

    XINPUT_STATE state{};
    if (getState(0, &state) != ERROR_SUCCESS)
    {
        m_previousGamepadButtons = 0;
        m_gamepadNavigation = 0;
        return;
    }

    const auto buttons = state.Gamepad.wButtons;
    const auto previousButtons = m_previousGamepadButtons;
    m_previousGamepadButtons = buttons;

    if (!s_pOverlay || !s_pOverlay->GetInGame())
    {
        SetGameInputBlocked(false);
        return;
    }

    if (m_gamepadToggle.IsPressed(buttons, previousButtons))
    {
        ToggleUIFromGamepad();
        m_gamepadNavigation = 0;
        return;
    }

    if (!s_pOverlay->GetActive())
        return;

    const auto pressedButtons = static_cast<uint16_t>(buttons & ~previousButtons);
    if (pressedButtons & XINPUT_GAMEPAD_A)
        InjectCefKey(VK_RETURN);
    if (pressedButtons & XINPUT_GAMEPAD_B)
    {
        // Match the existing keyboard Escape behavior: close the overlay and return control to the game.
        const auto scanCode = static_cast<uint16_t>(MapVirtualKey(VK_ESCAPE, MAPVK_VK_TO_VSC));
        ProcessKeyboard(VK_ESCAPE, scanCode, KEYEVENT_KEYDOWN, false, false);
        ProcessKeyboard(VK_ESCAPE, scanCode, KEYEVENT_KEYUP, false, false);
        return;
    }

    const auto navigation = GetGamepadNavigation(state.Gamepad);
    const auto now = std::chrono::steady_clock::now();
    if (navigation != m_gamepadNavigation)
    {
        m_gamepadNavigation = navigation;
        m_nextGamepadRepeat = now + std::chrono::milliseconds(300);
    }
    else if (navigation && now < m_nextGamepadRepeat)
    {
        return;
    }
    else if (navigation)
    {
        m_nextGamepadRepeat = now + std::chrono::milliseconds(100);
    }

    if (navigation)
        InjectCefKey(VK_TAB, navigation < 0 ? EVENTFLAG_SHIFT_DOWN : EVENTFLAG_NONE);
}

void InputService::SetGameInputBlocked(const bool aBlocked) noexcept
{
    const auto pPlayerControls = PlayerControls::GetInstance();
    if (!pPlayerControls)
    {
        if (!aBlocked)
            s_gameInputStateSaved = false;
        return;
    }

    if (aBlocked)
    {
        if (!s_gameInputStateSaved)
        {
            s_gameInputWasBlocked = pPlayerControls->bBlockPlayerInput;
            s_gameInputStateSaved = true;
        }
        pPlayerControls->SetBlockPlayerInput(true);
    }
    else if (s_gameInputStateSaved)
    {
        pPlayerControls->SetBlockPlayerInput(s_gameInputWasBlocked);
        s_gameInputStateSaved = false;
    }
}
