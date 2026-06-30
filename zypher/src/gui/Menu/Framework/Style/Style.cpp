#include <gui_pch.hpp>

void Style::Update() {
    if (!overrideAccent)
        accentColor = Color(139, 92, 246);

    if (!overrideStyle)
    {
        shadow = Color(5, 0, 12);
        background = Color(18, 14, 28);
        outline = Color(35, 28, 52);
        header = Color(22, 17, 36);
        container = Color(20, 15, 32);
        containerHeader = Color(26, 20, 42);
        headerText = Color(230, 220, 245);
        text = Color(220, 210, 240);
        dimmedText = Color(110, 100, 140);
        widgetBackground = Color(28, 22, 44);
        widgetBackgroundHovered = Color(36, 28, 56);
        warning = Color(255, 200, 50);
    }

    /*shadow = Color(8, 8, 12);
    background = Color(22, 20, 32);
    outline = Color(40, 35, 52);
    header = Color(30, 26, 42);
    container = Color(26, 22, 38);
    containerHeader = Color(34, 30, 46);
    headerText = Color(235, 225, 250);
    text = Color(225, 215, 240);
    dimmedText = Color(120, 110, 145);
    widgetBackground = Color(37, 34, 53);
    widgetBackgroundHovered = Color(46, 42, 63);
    warning = Color(255, 200, 50);*/

    /*
    dark / grey

    shadow = Color(0, 0, 0);
    background = Color(20, 20, 20);
    outline = Color(30, 30, 30);
    header = Color(23, 23, 23);
    container = Color(22, 22, 22);
    containerHeader = Color(25, 25, 25);
    headerText = Color(210, 210, 210);
    text = Color(210, 210, 210);
    dimmedText = Color(90, 90, 90);
    widgetBackground = Color(25, 25, 25);
    widgetBackgroundHovered = Color(27, 27, 27);
    warning = Color(255, 200, 50);*/

    /*
    original blue tinted
    *         accentColor = Color(10, 170, 255);

    shadow = Color(0, 0, 5);
    background = Color(30, 30, 35);
    outline = Color(40, 40, 45);
    header = Color(33, 33, 38);
    container = Color(32, 32, 37);
    containerHeader = Color(35, 35, 40);
    headerText = Color(220, 220, 225);
    text = Color(220, 220, 225);
    dimmedText = Color(100, 100, 105);
    widgetBackground = Color(35, 35, 40);
    widgetBackgroundHovered = Color(37, 37, 42);
    */

    if (!GImGui)
        return;

    static float interp = 0.0f;
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(accentColor.r / 255.0f, accentColor.g / 255.0f, accentColor.b / 255.0f, h, s, v);
    interp = std::clamp(interp + (5.0f * ImGui::GetIO().DeltaTime * (s < 0.5f ? 1.0f : -1.0f)), 0.0f, 1.0f);
    checkmark = Color::White().Lerp(Color::Black(), interp);
}
