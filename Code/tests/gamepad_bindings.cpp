#include <catch2/catch.hpp>

#include <input/GamepadBindings.h>

TEST_CASE("Gamepad toggle bindings require a new complete chord", "[gamepad.bindings]")
{
    constexpr GamepadBindings::ToggleBinding binding{
        GamepadBindings::Button::Back,
        GamepadBindings::Button::Start,
    };

    constexpr auto back = GamepadBindings::ToMask(GamepadBindings::Button::Back);
    constexpr auto chord = GamepadBindings::ToMask(GamepadBindings::Button::Back | GamepadBindings::Button::Start);

    REQUIRE_FALSE(binding.IsPressed(0, 0));
    REQUIRE_FALSE(binding.IsPressed(back, 0));
    REQUIRE(binding.IsPressed(chord, back));
    REQUIRE_FALSE(binding.IsPressed(chord, chord));
}

TEST_CASE("Gamepad bindings accept documented button names", "[gamepad.bindings]")
{
    REQUIRE(GamepadBindings::ParseButton("back") == GamepadBindings::Button::Back);
    REQUIRE(GamepadBindings::ParseButton("RIGHT_THUMB") == GamepadBindings::Button::RightThumb);
    REQUIRE_FALSE(GamepadBindings::ParseButton("turbo"));
}
