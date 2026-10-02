#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace GamepadBindings
{
enum class Button : uint16_t
{
    None = 0x0000,
    DPadUp = 0x0001,
    DPadDown = 0x0002,
    DPadLeft = 0x0004,
    DPadRight = 0x0008,
    Start = 0x0010,
    Back = 0x0020,
    LeftThumb = 0x0040,
    RightThumb = 0x0080,
    LeftShoulder = 0x0100,
    RightShoulder = 0x0200,
    A = 0x1000,
    B = 0x2000,
    X = 0x4000,
    Y = 0x8000,
};

constexpr Button operator|(const Button aLeft, const Button aRight) noexcept
{
    return static_cast<Button>(static_cast<uint16_t>(aLeft) | static_cast<uint16_t>(aRight));
}

constexpr uint16_t ToMask(const Button aButton) noexcept
{
    return static_cast<uint16_t>(aButton);
}

struct ToggleBinding
{
    Button Modifier{Button::Back};
    Button Primary{Button::Start};

    [[nodiscard]] constexpr bool IsPressed(const uint16_t aCurrent, const uint16_t aPrevious) const noexcept
    {
        const uint16_t binding = ToMask(Modifier | Primary);
        return (aCurrent & binding) == binding && (aPrevious & binding) != binding;
    }
};

[[nodiscard]] constexpr bool EqualsIgnoreCase(const std::string_view aLeft, const std::string_view aRight) noexcept
{
    if (aLeft.size() != aRight.size())
        return false;

    for (size_t i = 0; i < aLeft.size(); ++i)
    {
        const char left = aLeft[i] >= 'a' && aLeft[i] <= 'z' ? static_cast<char>(aLeft[i] - ('a' - 'A')) : aLeft[i];
        const char right = aRight[i] >= 'a' && aRight[i] <= 'z' ? static_cast<char>(aRight[i] - ('a' - 'A')) : aRight[i];
        if (left != right)
            return false;
    }

    return true;
}

[[nodiscard]] constexpr std::optional<Button> ParseButton(const std::string_view aName) noexcept
{
    if (EqualsIgnoreCase(aName, "NONE")) return Button::None;
    if (EqualsIgnoreCase(aName, "DPAD_UP")) return Button::DPadUp;
    if (EqualsIgnoreCase(aName, "DPAD_DOWN")) return Button::DPadDown;
    if (EqualsIgnoreCase(aName, "DPAD_LEFT")) return Button::DPadLeft;
    if (EqualsIgnoreCase(aName, "DPAD_RIGHT")) return Button::DPadRight;
    if (EqualsIgnoreCase(aName, "START")) return Button::Start;
    if (EqualsIgnoreCase(aName, "BACK")) return Button::Back;
    if (EqualsIgnoreCase(aName, "LEFT_THUMB")) return Button::LeftThumb;
    if (EqualsIgnoreCase(aName, "RIGHT_THUMB")) return Button::RightThumb;
    if (EqualsIgnoreCase(aName, "LEFT_SHOULDER")) return Button::LeftShoulder;
    if (EqualsIgnoreCase(aName, "RIGHT_SHOULDER")) return Button::RightShoulder;
    if (EqualsIgnoreCase(aName, "A")) return Button::A;
    if (EqualsIgnoreCase(aName, "B")) return Button::B;
    if (EqualsIgnoreCase(aName, "X")) return Button::X;
    if (EqualsIgnoreCase(aName, "Y")) return Button::Y;
    return std::nullopt;
}
} // namespace GamepadBindings
