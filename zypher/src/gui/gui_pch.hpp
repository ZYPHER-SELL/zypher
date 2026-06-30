#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <timeapi.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <DirectXMath.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

#include <d3d11.h>
#include <dxgi.h>

#include <SDK/Math/Vector2.hpp>
#include <SDK/Math/Vector3.hpp>
#include <SDK/Math/Vector4.hpp>
#include <SDK/Math/Vector4f.hpp>
#include <SDK/Math/Matrix3x4.hpp>
#include <SDK/Math/Transform.hpp>
#include <SDK/Math/Math.hpp>

#include <Menu/Framework/Color.hpp>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <Menu/Framework/Render/imgui/imgui.h>
#include <Menu/Framework/Render/imgui/imgui_internal.h>
#include <Menu/Framework/Render/imgui/imgui_impl_dx11.h>
#include <Menu/Framework/Render/imgui/imgui_impl_win32.h>

#ifndef IMGUI_ENABLE_FREETYPE
  #ifndef ImGuiFreeTypeBuilderFlags_MonoHinting
    #define ImGuiFreeTypeBuilderFlags_MonoHinting   0
  #endif
  #ifndef ImGuiFreeTypeBuilderFlags_Monochrome
    #define ImGuiFreeTypeBuilderFlags_Monochrome    0
  #endif
  #ifndef ImGuiFreeTypeBuilderFlags_LoadColor
    #define ImGuiFreeTypeBuilderFlags_LoadColor     0
  #endif
  #ifndef ImGuiFreeTypeBuilderFlags_Bold
    #define ImGuiFreeTypeBuilderFlags_Bold          0
  #endif
  #ifndef ImGuiFreeTypeBuilderFlags_Oblique
    #define ImGuiFreeTypeBuilderFlags_Oblique       0
  #endif
#endif

#include <Utilities/Security/XorStr/XorStr.hpp>

#include <Menu/Framework/Input/Input.hpp>
#include <Menu/Framework/Render/Render.hpp>
#include <Menu/Framework/Render/Assets/font_awesome.hpp>
#include <Menu/Framework/Style/Style.hpp>

#include <Menu/Framework/Object/Object.hpp>
#include <Menu/Framework/Window/Window.hpp>

#include <Menu/Framework/Container/Container.hpp>
#include <Menu/Framework/Tab/Tab.hpp>

#include <Menu/Framework/Widgets/Popup/Popup.hpp>
#include <Menu/Framework/Widgets/Label/Label.hpp>
#include <Menu/Framework/Widgets/Button/Button.hpp>
#include <Menu/Framework/Widgets/Checkbox/Checkbox.hpp>
#include <Menu/Framework/Widgets/Slider/Slider.hpp>
#include <Menu/Framework/Widgets/Dropdown/Dropdown.hpp>
#include <Menu/Framework/Widgets/MultiDropdown/MultiDropdown.hpp>
#include <Menu/Framework/Widgets/ColorPicker/ColorPicker.hpp>
#include <Menu/Framework/Widgets/Listbox/Listbox.hpp>
#include <Menu/Framework/Widgets/TextInput/TextInput.hpp>
#include <Menu/Framework/Widgets/ConfirmationButton/ConfirmationButton.hpp>
#include <Menu/Framework/Tab/Subtab.hpp>
#include <Menu/Framework/Widgets/Seperator/Seperator.hpp>
#include <Menu/Framework/Widgets/Keybind/Keybind.hpp>

#include <Menu/Framework/UI/UI.hpp>

namespace Memory {

    inline HWND hwnd = nullptr;
}

namespace Overlay {

    inline ID3D11Device*        g_pd3dDevice        = nullptr;
    inline ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
}

