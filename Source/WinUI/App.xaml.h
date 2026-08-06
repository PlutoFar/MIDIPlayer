#pragma once

#include "App.xaml.g.h"
#include "winrt/Microsoft.Windows.AppLifecycle.h"
#include "winrt/MidiPlayer.h"

#include <atomic>
#include <string>
#include <thread>
#include <windows.h>

namespace winrt::MidiPlayer::implementation
{
    struct App : AppT<App>
    {
        App();
        ~App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        void HandleActivation(Microsoft::Windows::AppLifecycle::AppActivationArguments const& args);
        void StartIpcServer();
        void StopIpcServer();
        void HandleIpcPath(std::wstring path);

        Microsoft::Windows::AppLifecycle::AppInstance appInstance{ nullptr };
        winrt::event_token activatedToken{};
        winrt::MidiPlayer::MainWindow mainWindow{ nullptr };
        Microsoft::UI::Xaml::Window window{ nullptr };
        HANDLE singleInstanceMutex{ nullptr };
        std::atomic<bool> ipcStop{ false };
        std::thread ipcThread;
    };
}
