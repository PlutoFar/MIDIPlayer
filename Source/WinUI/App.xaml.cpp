#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <algorithm>
#include <filesystem>
#include <shellapi.h>
#include <vector>

#include "winrt/Microsoft.Windows.AppLifecycle.h"
#include "winrt/Windows.ApplicationModel.Activation.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::Windows::AppLifecycle;

namespace
{
    constexpr wchar_t kSingleInstanceMutex[] = L"Local\\MidiPlayer.WinUI.SingleInstance";
    constexpr wchar_t kSingleInstancePipe[] = L"\\\\.\\pipe\\MidiPlayer.WinUI.SingleInstance";

    std::wstring lowerCopy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](wchar_t c) { return (wchar_t)towlower(c); });
        return value;
    }

    bool isSupportedMidiPath(std::wstring const& path)
    {
        if (path.empty()) return false;
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;

        const auto ext = lowerCopy(std::filesystem::path(path).extension().wstring());
        return ext == L".mid" || ext == L".midi";
    }

    std::vector<std::wstring> splitCommandLine(std::wstring const& commandLine)
    {
        std::vector<std::wstring> tokens;
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(commandLine.c_str(), &argc);
        if (argv == nullptr) return tokens;
        for (int i = 0; i < argc; ++i)
            tokens.emplace_back(argv[i]);
        LocalFree(argv);
        return tokens;
    }

    std::wstring findMidiPath(std::vector<std::wstring> const& tokens,
                              bool skipExecutable,
                              std::wstring const& currentDirectory = {})
    {
        const size_t start = skipExecutable && !tokens.empty() ? 1 : 0;
        for (size_t i = start; i < tokens.size(); ++i)
        {
            const auto& token = tokens[i];
            if (token.empty() || token[0] == L'-' || token[0] == L'/') continue;

            std::filesystem::path candidate(token);
            if (candidate.is_relative() && !currentDirectory.empty())
                candidate = std::filesystem::path(currentDirectory) / candidate;

            const auto path = candidate.wstring();
            if (isSupportedMidiPath(path)) return path;
        }
        return {};
    }

    std::wstring findMidiPathInLaunchArgs(hstring const& args)
    {
        auto path = findMidiPath(splitCommandLine(std::wstring(args)), false);
        if (!path.empty()) return path;
        return findMidiPath(splitCommandLine(GetCommandLineW()), true);
    }

    std::wstring findMidiPathInActivation(AppActivationArguments const& args)
    {
        auto data = args.Data();
        if (auto commandLine =
                data.try_as<Windows::ApplicationModel::Activation::ICommandLineActivatedEventArgs>())
        {
            auto op = commandLine.Operation();
            return findMidiPath(splitCommandLine(std::wstring(op.Arguments())), false,
                                std::wstring(op.CurrentDirectoryPath()));
        }
        return {};
    }

    void sendPathToPrimary(std::wstring const& path)
    {
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            HANDLE pipe = CreateFileW(kSingleInstancePipe, GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                const DWORD bytes =
                    static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
                WriteFile(pipe, path.c_str(), bytes, &written, nullptr);
                FlushFileBuffers(pipe);
                CloseHandle(pipe);
                return;
            }
            Sleep(100);
        }
    }
}

namespace winrt::MidiPlayer::implementation
{
    App::App()
    {
        InitializeComponent();
    }

    App::~App()
    {
        StopIpcServer();
        if (singleInstanceMutex)
        {
            ReleaseMutex(singleInstanceMutex);
            CloseHandle(singleInstanceMutex);
            singleInstanceMutex = nullptr;
        }
    }

    void App::OnLaunched(LaunchActivatedEventArgs const& args)
    {
        singleInstanceMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
        if (singleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            const auto midiPath = findMidiPathInLaunchArgs(args.Arguments());
            sendPathToPrimary(midiPath);
            ExitProcess(0);
            return;
        }

        appInstance = AppInstance::FindOrRegisterForKey(L"MidiPlayer.SingleInstance");
        if (!appInstance.IsCurrent())
        {
            auto current = AppInstance::GetCurrent();
            appInstance.RedirectActivationToAsync(current.GetActivatedEventArgs()).get();
            ExitProcess(0);
            return;
        }

        activatedToken = appInstance.Activated([this](auto const&, AppActivationArguments const& activationArgs)
        {
            if (window)
            {
                auto queue = window.DispatcherQueue();
                if (queue)
                {
                    queue.TryEnqueue([this, activationArgs]() { HandleActivation(activationArgs); });
                    return;
                }
            }
            HandleActivation(activationArgs);
        });

        mainWindow = make<MainWindow>();
        window = mainWindow;
        window.Activate();
        StartIpcServer();

        const auto midiPath = findMidiPathInLaunchArgs(args.Arguments());
        if (!midiPath.empty())
            mainWindow.OpenMidiFromShell(hstring(midiPath));
    }

    void App::HandleActivation(AppActivationArguments const& args)
    {
        if (!mainWindow) return;
        const auto midiPath = findMidiPathInActivation(args);
        window.Activate();
        if (!midiPath.empty())
            mainWindow.OpenMidiFromShell(hstring(midiPath));
    }

    void App::StartIpcServer()
    {
        if (ipcThread.joinable())
            return;

        ipcStop = false;
        ipcThread = std::thread([this]()
        {
            while (!ipcStop.load())
            {
                HANDLE pipe = CreateNamedPipeW(
                    kSingleInstancePipe,
                    PIPE_ACCESS_INBOUND,
                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                    1,
                    4096,
                    4096,
                    1000,
                    nullptr);

                if (pipe == INVALID_HANDLE_VALUE)
                {
                    Sleep(100);
                    continue;
                }

                const BOOL connected =
                    ConnectNamedPipe(pipe, nullptr) ||
                    GetLastError() == ERROR_PIPE_CONNECTED;

                if (connected && !ipcStop.load())
                {
                    std::wstring path;
                    wchar_t buffer[2048] = {};
                    DWORD bytesRead = 0;
                    if (ReadFile(pipe, buffer, sizeof(buffer) - sizeof(wchar_t),
                                 &bytesRead, nullptr) && bytesRead > 0)
                    {
                        buffer[bytesRead / sizeof(wchar_t)] = L'\0';
                        path = buffer;
                    }
                    HandleIpcPath(std::move(path));
                }

                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
            }
        });
    }

    void App::StopIpcServer()
    {
        ipcStop = true;
        sendPathToPrimary({});
        if (ipcThread.joinable())
            ipcThread.join();
    }

    void App::HandleIpcPath(std::wstring path)
    {
        if (!window || !mainWindow)
            return;

        auto queue = window.DispatcherQueue();
        if (!queue)
            return;

        queue.TryEnqueue([this, path = std::move(path)]()
        {
            if (window)
                window.Activate();
            if (!path.empty() && mainWindow)
                mainWindow.OpenMidiFromShell(hstring(path));
        });
    }
}
