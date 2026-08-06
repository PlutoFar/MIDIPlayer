#pragma once

#include "MainWindow.g.h"

#include "Core/WinBridge.h"
#include "Core/WinUiSettingsAdapter.h"

#include <memory>
#include <vector>

namespace winrt::MidiPlayer::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        // 导航 / 窗口
        void OnNavSelectionChanged(Microsoft::UI::Xaml::Controls::NavigationView const&,
                                   Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
        void OnPinToggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // 头部 / 插件
        void OnPluginComboChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnScan(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnload(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnEditor(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnExport(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancelExport(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenMidiFromShell(hstring const& path);

        // 播放列表
        void OnAddFiles(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnClearPlaylist(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSaveList(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnLoadList(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlaylistDoubleTapped(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&);
        void OnPlaylistRightTapped(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const&);
        void OnCtxPlay(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCtxRemove(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCtxReveal(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlaylistContainerContentChanging(Microsoft::UI::Xaml::Controls::ListViewBase const&, Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const&);

        void OnPlayAccelerator(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnOpenAccelerator(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnSaveAccelerator(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);

        // 走带
        void OnPrev(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayPause(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNext(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStop(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnLoopMode(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMute(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // 背景设置
        void OnBgSelect(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBgClear(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBlurModeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnBlurRadiusChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void OnOverlayChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void OnMonetToggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeqIconToggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRememberToggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // 音频设置
        void OnAudioDeviceChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnSampleRateChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnBufferSizeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnTestSound(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFileAssociation(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // 字体设置
        void OnUiFontChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnUiFontSizeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void OnPlaylistFontChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnPlaylistFontSizeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);

        // 拖放
        void OnRootDragOver(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        void OnRootDrop(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        void OnTrackClipSizeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnRootLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void Refresh();
        void SelectPanel(hstring const& tag);
        void ShowModeToast(hstring const& text);
        void ShowPluginLoadToast(std::wstring const& pluginName);
        void RebuildPlaylist(midi::WinState const& st);
        void OnItemsVectorChanged(Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> const&,
                                  Windows::Foundation::Collections::IVectorChangedEventArgs const&);
        void OnProgressChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void OnProgressPointerPressed(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnProgressPointerReleased(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnProgressPointerCaptureLost(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnProgressPointerMoved(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnProgressPointerExited(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnVolumeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void UpdateProgressTooltip(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void CommitPendingSeek();
        void HideProgressTooltip();
        winrt::fire_and_forget PickAndAddAsync();
        winrt::fire_and_forget ConfirmClearPlaylistAsync();
        winrt::fire_and_forget ConfirmCloseAsync();
        winrt::fire_and_forget PickAndExportAsync();
        winrt::fire_and_forget SaveListAsync();
        winrt::fire_and_forget LoadListAsync();
        winrt::fire_and_forget PickBackgroundAsync();
        void ApplyBackgroundVisuals(midi::WinUiSettingsState const& st);
        void InitBackgroundControls();
        void RebuildRecentBackgrounds();
        void RebuildPalette(midi::WinUiSettingsState const& st);
        void InitAudioControls();
        void InitFontControls();
        void ApplyFonts();
        void SaveWindowState();
        winrt::fire_and_forget OnRootDropAsync(Microsoft::UI::Xaml::DragEventArgs e);
        winrt::fire_and_forget ShowCrashDialogAsync();
        static hstring FormatTime(double seconds);

        std::unique_ptr<midi::WinCore> m_core;
        std::unique_ptr<midi::WinUiSettingsAdapter> m_settings;
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_toastTimer{ nullptr };
        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> m_items{ nullptr };

        std::vector<std::wstring> m_pluginIds;
        int m_lastPluginCount{ -1 };
        std::uint64_t m_lastScanSessionId{ 0 };
        std::uint64_t m_lastLoadSessionId{ 0 };
        int m_lastTrackCount{ -1 };
        int m_lastTrackIndex{ -2 };
        int m_lastPlayMode{ -1 };
        std::uint64_t m_lastExportSessionId{ 0 };
        int m_ctxIndex{ -1 };
        int m_removedIndex{ -1 };
        bool m_suppressVolume{ false };
        bool m_suppressProgress{ false };
        bool m_userSeeking{ false };
        bool m_seekPending{ false };
        double m_pendingSeekValue{ 0.0 };
        bool m_rebuilding{ false };
        bool m_rebuildingPlugins{ false };
        int m_seekHoldFrames{ 0 };
        std::wstring m_lastBgPath;
        unsigned int m_lastAccent{ 0 };
        bool m_bgInit{ false };
        bool m_settingsInit{ false };
        bool m_crashShown{ false };
        bool m_noticeShown{ false };
        bool m_windowResizeGuard{ false };
        bool m_allowClose{ false };
        double m_scrollPos{ 0.0 };
        int m_scrollDir{ -1 };
        Microsoft::UI::Xaml::Media::SolidColorBrush m_accentBrush{ nullptr };
    };
}

namespace winrt::MidiPlayer::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
