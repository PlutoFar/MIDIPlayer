#include "pch.h"

#include <winrt/Microsoft.UI.Xaml.Settings.h>

int __stdcall wXamlGeneratedMain(HINSTANCE instance,
                                 HINSTANCE previous,
                                 PWSTR commandLine,
                                 int showCommand);

namespace
{
    int showRuntimeVersionError() noexcept
    {
        MessageBoxW(
            nullptr,
            L"MIDI Player 需要 Windows App Runtime 2.3.1 或兼容的更新 2.x 版本。\n\n"
            L"当前运行时无法启用应用所需的 XAML 2.3.1 优化。请安装或修复 x64 运行时后重试：\n"
            L"https://aka.ms/windowsappsdk/2.3/2.3.1/windowsappruntimeinstall-x64.exe",
            L"Windows App Runtime 版本不兼容",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        return ERROR_OLD_WIN_VERSION;
    }
}

int __stdcall wWinMain(HINSTANCE instance,
                       HINSTANCE previous,
                       PWSTR commandLine,
                       int showCommand)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        using winrt::Microsoft::UI::Xaml::Settings::XamlChangeId;
        using winrt::Microsoft::UI::Xaml::Settings::XamlOptionalChanges;

        bool enabled = true;
        enabled &= XamlOptionalChanges::EnableChange(
            XamlChangeId::DefaultStyleOptimizations);
        enabled &= XamlOptionalChanges::EnableChange(
            XamlChangeId::IconNoGridOptimization);
        enabled &= XamlOptionalChanges::EnableChange(
            XamlChangeId::OptimizeApplyStyles);
        enabled &= XamlOptionalChanges::EnableChange(
            XamlChangeId::DeferContextFlyoutInit);

        if (!enabled || !XamlOptionalChanges::Lock())
            return showRuntimeVersionError();

        return wXamlGeneratedMain(instance, previous, commandLine, showCommand);
    }
    catch (winrt::hresult_error const&)
    {
        return showRuntimeVersionError();
    }
    catch (...)
    {
        return showRuntimeVersionError();
    }
}
