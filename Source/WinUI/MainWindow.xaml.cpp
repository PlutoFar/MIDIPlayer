#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "MainWindow.xaml.g.hpp"

#include "Core/PluginLoadNotification.h"

#include <algorithm>
#include <filesystem>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;
namespace StoragePickers = Microsoft::Windows::Storage::Pickers;

namespace
{
    constexpr wchar_t kIconPlay = L'\uE768';
    constexpr wchar_t kIconPause = L'\uE769';
    constexpr wchar_t kIconVolMute = L'\uE992';
    constexpr wchar_t kIconVolLow = L'\uE994';
    constexpr wchar_t kIconVolHigh = L'\uE995';
    constexpr wchar_t kIconPin = L'\uE718';
    constexpr wchar_t kIconPinned = L'\uE840';
    constexpr int kModeToastDurationMs = 1400;

    // 播放模式 -> (字形, 名称)
    struct ModeInfo { wchar_t glyph; const wchar_t* name; };
    ModeInfo modeInfo(int mode, bool listStyle = false)
    {
        switch (mode)
        {
        case 2: return { L'\uE8EE', L"列表循环" };
        case 3: return { L'\uE8ED', L"单曲循环" };
        case 4: return { L'\uE8B1', L"随机播放" };
        default: return { listStyle ? L'\uE8FD' : L'\uEA42', L"连续播放" };
        }
    }

    hstring glyphStr(wchar_t g) { return hstring(std::wstring(1, g)); }

    StoragePickers::FileSavePicker playlistSavePicker(Microsoft::UI::WindowId const& windowId)
    {
        StoragePickers::FileSavePicker picker(windowId);
        picker.Title(L"保存播放列表");
        picker.SettingsIdentifier(L"PlaylistSave");
        picker.SuggestedStartLocation(StoragePickers::PickerLocationId::DocumentsLibrary);
        picker.FileTypeChoices().Insert(
            L"JSON 播放列表",
            winrt::single_threaded_vector<hstring>({ L".json" }));
        picker.InitialFileTypeIndex(0);
        picker.DefaultFileExtension(L".json");
        picker.SuggestedFileName(L"playlist");
        picker.ShowOverwritePrompt(true);
        return picker;
    }

    hstring fileNameFromPath(hstring const& path)
    {
        const auto name = std::filesystem::path(std::wstring(path)).filename().wstring();
        return hstring(name.empty() ? std::wstring(path) : name);
    }

    hstring exportFileName(std::wstring const& trackTitle)
    {
        const auto name = std::filesystem::path(trackTitle).stem().wstring();
        return hstring(name.empty() ? L"export" : name);
    }

    hstring pluginDisplayName(midi::WinPlugin const& plugin)
    {
        std::wstring text = plugin.name;
        if (!plugin.manufacturer.empty())
            text += L" · " + plugin.manufacturer;
        if (!plugin.formatName.empty())
            text += L" · " + plugin.formatName;
        return hstring(text);
    }

    winrt::Windows::UI::Color colorFromArgb(unsigned int argb)
    {
        winrt::Windows::UI::Color c;
        c.A = (uint8_t)((argb >> 24) & 0xFF);
        c.R = (uint8_t)((argb >> 16) & 0xFF);
        c.G = (uint8_t)((argb >> 8) & 0xFF);
        c.B = (uint8_t)(argb & 0xFF);
        return c;
    }
}

namespace winrt::MidiPlayer::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        Title(L"MIDI 播放器");
        SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop());
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        m_core = std::make_unique<midi::WinCore>();
        m_core->init();
        m_settings = std::make_unique<midi::WinUiSettingsAdapter>();
        m_settings->applyBackground();

        m_accentBrush = SolidColorBrush();
        m_accentBrush.Color(colorFromArgb(0xFF0078D4));

        // 播放列表数据源（可观察，支持拖拽重排）。
        m_items = winrt::single_threaded_observable_vector<Windows::Foundation::IInspectable>();
        PlaylistView().ItemsSource(m_items);
        m_items.VectorChanged({ this, &MainWindow::OnItemsVectorChanged });

        // 滑块事件（代码订阅，避免 XAML 中处理顺序问题）。
        ProgressSlider().ValueChanged({ this, &MainWindow::OnProgressChanged });
        ProgressSlider().PointerPressed({ this, &MainWindow::OnProgressPointerPressed });
        ProgressSlider().PointerReleased({ this, &MainWindow::OnProgressPointerReleased });
        ProgressSlider().PointerCaptureLost({ this, &MainWindow::OnProgressPointerCaptureLost });
        ProgressSlider().PointerEntered({ this, &MainWindow::OnProgressPointerMoved });
        ProgressSlider().PointerMoved({ this, &MainWindow::OnProgressPointerMoved });
        ProgressSlider().PointerExited({ this, &MainWindow::OnProgressPointerExited });
        VolumeSlider().ValueChanged({ this, &MainWindow::OnVolumeChanged });

        // 默认选中乐器库页。
        Nav().SelectedItem(Nav().MenuItems().GetAt(0));
        SelectPanel(L"library");

        auto queue = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        m_toastTimer = queue.CreateTimer();
        m_toastTimer.Interval(std::chrono::milliseconds(kModeToastDurationMs));
        m_toastTimer.IsRepeating(false);
        m_toastTimer.Tick([this](auto&&, auto&&) { ModeToast().IsOpen(false); });

        m_timer = queue.CreateTimer();
        m_timer.Interval(std::chrono::milliseconds(33));
        m_timer.Tick([this](auto&&, auto&&) {
            if (m_core) m_core->tick(m_seekHoldFrames > 0);
            Refresh();
        });
        m_timer.Start();

        ApplyFonts();
        Refresh();
    }

    // ---- 导航 / 窗口 ----

    void MainWindow::OnNavSelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& args)
    {
        if (auto item = args.SelectedItem().try_as<NavigationViewItem>())
        {
            auto tag = winrt::unbox_value_or<hstring>(item.Tag(), L"");
            SelectPanel(tag);
        }
    }

    void MainWindow::SelectPanel(hstring const& tag)
    {
        LibraryPanel().Visibility(Visibility::Collapsed);
        PlaylistPanel().Visibility(Visibility::Collapsed);
        BackgroundPanel().Visibility(Visibility::Collapsed);
        FontPanel().Visibility(Visibility::Collapsed);
        AudioPanel().Visibility(Visibility::Collapsed);

        if (tag == L"playlist") { PlaylistPanel().Visibility(Visibility::Visible); PageTitle().Text(L"播放列表"); }
        else if (tag == L"background")
        {
            BackgroundPanel().Visibility(Visibility::Visible); PageTitle().Text(L"背景");
            InitBackgroundControls();
            RebuildRecentBackgrounds();
            if (m_settings) RebuildPalette(m_settings->state());
        }
        else if (tag == L"fonts") { FontPanel().Visibility(Visibility::Visible); PageTitle().Text(L"字体"); InitFontControls(); }
        else if (tag == L"settings") { AudioPanel().Visibility(Visibility::Visible); PageTitle().Text(L"设置"); InitAudioControls(); }
        else { LibraryPanel().Visibility(Visibility::Visible); PageTitle().Text(L"乐器库"); }
    }

    void MainWindow::OnPinToggled(IInspectable const&, RoutedEventArgs const&)
    {
        auto checked = PinButton().IsChecked();
        bool pinned = checked && checked.Value();
        if (auto presenter = this->AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
            presenter.IsAlwaysOnTop(pinned);
        if (m_settings) m_settings->setAlwaysOnTop(pinned);
        PinIcon().Glyph(glyphStr(pinned ? kIconPinned : kIconPin));
    }

    // ---- 头部 / 插件 ----

    void MainWindow::OnPluginComboChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_rebuildingPlugins || !m_core) return;
        const int idx = PluginCombo().SelectedIndex();
        if (idx < 0 || idx >= (int)m_pluginIds.size()) return;

        PluginCombo().IsEnabled(false);
        ScanButton().IsEnabled(false);
        if (!m_core->loadAsync(m_pluginIds[(size_t)idx]))
        {
            NoticeBar().Severity(InfoBarSeverity::Error);
            NoticeBar().Title(L"无法启动插件加载");
            NoticeBar().Message(L"扫描、加载或导出任务正在运行。");
            NoticeBar().IsOpen(true);
        }
        Refresh();
    }

    void MainWindow::OnScan(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_core) m_core->scanAsync();
        m_lastPluginCount = -1;
        Refresh();
    }

    void MainWindow::OnUnload(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_core)
        {
            if (m_core->state().workerCrashed) m_core->terminateCrashedWorker();
            m_core->unload();
        }
        Refresh();
    }

    void MainWindow::OnEditor(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_core) m_core->openEditor();
    }

    void MainWindow::OnExport(IInspectable const&, RoutedEventArgs const&) { PickAndExportAsync(); }

    void MainWindow::OnCancelExport(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_core) m_core->cancelExport();
        ExportCancelButton().IsEnabled(false);
    }

    void MainWindow::OpenMidiFromShell(hstring const& path)
    {
        if (!m_core || path.empty()) return;
        const bool ok = m_core->openMidi(std::wstring(path));
        if (!ok)
        {
            NoticeBar().Severity(InfoBarSeverity::Error);
            NoticeBar().Title(L"MIDI 打开失败");
            NoticeBar().Message(L"无法打开启动参数中的 MIDI 文件。");
            NoticeBar().IsOpen(true);
        }
        m_lastTrackCount = -1;
        Refresh();
    }

    // ---- 播放列表 ----

    void MainWindow::OnAddFiles(IInspectable const&, RoutedEventArgs const&) { PickAndAddAsync(); }
    void MainWindow::OnSaveList(IInspectable const&, RoutedEventArgs const&) { SaveListAsync(); }
    void MainWindow::OnLoadList(IInspectable const&, RoutedEventArgs const&) { LoadListAsync(); }

    void MainWindow::OnClearPlaylist(IInspectable const&, RoutedEventArgs const&)
    {
        ConfirmClearPlaylistAsync();
    }

    void MainWindow::OnPlaylistDoubleTapped(IInspectable const&, Input::DoubleTappedRoutedEventArgs const&)
    {
        auto idx = PlaylistView().SelectedIndex();
        if (m_core && idx >= 0) m_core->playTrackAt(idx);
        Refresh();
    }

    void MainWindow::OnPlaylistRightTapped(IInspectable const&, Input::RightTappedRoutedEventArgs const& e)
    {
        // 让右键命中的行成为选中行，使上下文操作作用其上。
        auto src = e.OriginalSource().try_as<DependencyObject>();
        while (src)
        {
            if (auto lvi = src.try_as<ListViewItem>())
            {
                auto idx = PlaylistView().IndexFromContainer(lvi);
                if (idx >= 0) { PlaylistView().SelectedIndex(idx); m_ctxIndex = idx; }
                break;
            }
            src = Media::VisualTreeHelper::GetParent(src);
        }
    }

    void MainWindow::OnCtxPlay(IInspectable const&, RoutedEventArgs const&)
    {
        auto idx = PlaylistView().SelectedIndex();
        if (m_core && idx >= 0) m_core->playTrackAt(idx);
        Refresh();
    }

    void MainWindow::OnCtxRemove(IInspectable const&, RoutedEventArgs const&)
    {
        auto idx = PlaylistView().SelectedIndex();
        if (m_core && idx >= 0) { m_core->removeTrack(idx); m_lastTrackCount = -1; Refresh(); }
    }

    void MainWindow::OnCtxReveal(IInspectable const&, RoutedEventArgs const&)
    {
        auto idx = PlaylistView().SelectedIndex();
        if (m_core && idx >= 0) m_core->revealTrack(idx);
    }

    void MainWindow::OnPlaylistContainerContentChanging(
        ListViewBase const&, ContainerContentChangingEventArgs const& args)
    {
        if (auto item = args.ItemContainer().try_as<ListViewItem>())
        {
            const bool isPlayingRow =
                !args.InRecycleQueue() && (int)args.ItemIndex() == m_lastTrackIndex;
            item.Background(isPlayingRow ? m_accentBrush.as<Brush>() : nullptr);
        }
    }

    void MainWindow::OnPlayAccelerator(
        KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& args)
    {
        if (m_core) m_core->togglePlay();
        args.Handled(true);
        Refresh();
    }

    void MainWindow::OnOpenAccelerator(
        KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        PickAndAddAsync();
    }

    void MainWindow::OnSaveAccelerator(
        KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        SaveListAsync();
    }

    // 拖拽重排：observable_vector 先 Removed(old) 再 Inserted(new)，配对成 moveTrack。
    void MainWindow::OnItemsVectorChanged(Windows::Foundation::Collections::IObservableVector<IInspectable> const&,
                                          Windows::Foundation::Collections::IVectorChangedEventArgs const& args)
    {
        if (m_rebuilding) return;
        using Windows::Foundation::Collections::CollectionChange;
        switch (args.CollectionChange())
        {
        case CollectionChange::ItemRemoved:
            m_removedIndex = (int)args.Index();
            break;
        case CollectionChange::ItemInserted:
            if (m_removedIndex >= 0)
            {
                int to = (int)args.Index();
                if (m_core)
                {
                    m_core->moveTrack(m_removedIndex, to);
                    m_lastTrackCount = -1;
                    m_lastTrackIndex = -2;
                    Refresh();
                }
                m_removedIndex = -1;
            }
            break;
        default:
            m_removedIndex = -1;
            break;
        }
    }

    // ---- 走带 ----

    void MainWindow::OnPrev(IInspectable const&, RoutedEventArgs const&) { if (m_core) m_core->prev(); Refresh(); }
    void MainWindow::OnNext(IInspectable const&, RoutedEventArgs const&) { if (m_core) m_core->next(); Refresh(); }
    void MainWindow::OnStop(IInspectable const&, RoutedEventArgs const&) { if (m_core) m_core->stop(); Refresh(); }
    void MainWindow::OnPlayPause(IInspectable const&, RoutedEventArgs const&) { if (m_core) m_core->togglePlay(); Refresh(); }

    void MainWindow::OnLoopMode(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_core) return;
        int next = (m_core->state().playMode % 4) + 1;
        m_core->setPlayMode(next);
        auto info = modeInfo(next,
            m_settings && m_settings->sequentialIconListStyle());
        ShowModeToast(info.name);
        Refresh();
    }

    void MainWindow::OnMute(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_core) m_core->toggleMute();
        Refresh();
    }

    void MainWindow::OnProgressChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& e)
    {
        if (m_suppressProgress) return;
        m_pendingSeekValue = e.NewValue();
        m_seekPending = true;
        if (!m_userSeeking && ProgressSlider().FocusState() == FocusState::Keyboard)
            CommitPendingSeek();
    }

    void MainWindow::OnProgressPointerPressed(IInspectable const&, PointerRoutedEventArgs const&)
    {
        m_userSeeking = true;
    }

    void MainWindow::OnProgressPointerReleased(IInspectable const&, PointerRoutedEventArgs const&)
    {
        CommitPendingSeek();
        m_userSeeking = false;
    }

    void MainWindow::OnProgressPointerCaptureLost(IInspectable const&, PointerRoutedEventArgs const&)
    {
        CommitPendingSeek();
        m_userSeeking = false;
    }

    void MainWindow::CommitPendingSeek()
    {
        if (!m_seekPending || !m_core) return;
        m_seekPending = false;
        m_seekHoldFrames = 15;
        m_core->seek(m_pendingSeekValue);
    }

    void MainWindow::OnProgressPointerMoved(IInspectable const&, PointerRoutedEventArgs const& e)
    {
        UpdateProgressTooltip(e);
    }

    void MainWindow::OnProgressPointerExited(IInspectable const&, PointerRoutedEventArgs const&)
    {
        HideProgressTooltip();
    }

    void MainWindow::UpdateProgressTooltip(PointerRoutedEventArgs const& e)
    {
        if (!m_core || !ProgressSlider().IsEnabled())
        {
            HideProgressTooltip();
            return;
        }

        auto st = m_core->state();
        const double durationSamples = st.durationSamples;
        const double width = ProgressSlider().ActualWidth();
        double sampleRate = m_core->sampleRate();
        if (!st.hasSequence || durationSamples <= 0.0 || width <= 0.0)
        {
            HideProgressTooltip();
            return;
        }
        if (sampleRate <= 0.0)
            sampleRate = 44100.0;

        auto position = e.GetCurrentPoint(ProgressSlider()).Position();
        double x = position.X;
        if (x < 0.0) x = 0.0;
        if (x > width) x = width;

        const double ratio = x / width;
        const double totalSeconds = durationSamples / sampleRate;
        double seconds = ratio * totalSeconds;
        if (seconds < 0.0) seconds = 0.0;
        if (seconds > totalSeconds) seconds = totalSeconds;

        ProgressSlider().SetValue(ToolTipService::ToolTipProperty(),
                                  box_value(FormatTime(seconds)));
    }

    void MainWindow::HideProgressTooltip()
    {
        ProgressSlider().ClearValue(ToolTipService::ToolTipProperty());
    }

    void MainWindow::OnVolumeChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& e)
    {
        if (m_suppressVolume) return;
        if (m_core) m_core->volume((float)e.NewValue());
    }

    void MainWindow::ShowModeToast(hstring const& text)
    {
        m_toastTimer.Stop();
        m_toastTimer.Interval(std::chrono::milliseconds(kModeToastDurationMs));
        ModeToast().Severity(InfoBarSeverity::Informational);
        ModeToast().Title(text);
        ModeToast().IsOpen(true);
        m_toastTimer.Start();
    }

    void MainWindow::ShowPluginLoadToast(std::wstring const& pluginName)
    {
        m_toastTimer.Stop();
        m_toastTimer.Interval(std::chrono::milliseconds(midi::pluginLoadSuccessToastDurationMs));
        ModeToast().Severity(InfoBarSeverity::Success);
        ModeToast().Title(hstring(midi::makePluginLoadSuccessToastTitle(pluginName)));
        ModeToast().IsOpen(true);
        m_toastTimer.Start();
    }

    void MainWindow::ApplyBackgroundVisuals(midi::WinUiSettingsState const& st)
    {
        if (st.bgHasImage && !st.bgImagePath.empty())
        {
            if (st.bgImagePath != m_lastBgPath)
            {
                m_lastBgPath = st.bgImagePath;
                std::wstring p = st.bgImagePath;
                for (auto& ch : p) if (ch == L'\\') ch = L'/';
                Imaging::BitmapImage bmp;
                bmp.UriSource(Windows::Foundation::Uri(hstring(L"file:///" + p)));
                BgImage().Source(bmp);
                BgImage().Visibility(Visibility::Visible);
                BgOverlay().Visibility(Visibility::Visible);
            }
            BgOverlay().Fill(SolidColorBrush(colorFromArgb(st.bgOverlay)));
            BgOverlay().Opacity(st.bgOverlayOpacity);
        }
        else if (!m_lastBgPath.empty())
        {
            m_lastBgPath.clear();
            BgImage().Source(nullptr);
            BgImage().Visibility(Visibility::Collapsed);
            BgOverlay().Visibility(Visibility::Collapsed);
        }

        if (st.bgAccent != m_lastAccent)
        {
            m_lastAccent = st.bgAccent;
            auto col = colorFromArgb(st.bgAccent);
            if (m_accentBrush) m_accentBrush.Color(col);
            auto res = Application::Current().Resources();
            for (auto key : { L"AccentPrimaryBrush", L"SliderTrackValueFill",
                              L"SliderTrackValueFillPointerOver", L"SliderTrackValueFillPressed",
                              L"SliderTrackValueFillDisabled",
                              L"NavigationViewSelectionIndicatorForeground" })
            {
                auto boxed = box_value(hstring(key));
                if (res.HasKey(boxed))
                    if (auto b = res.Lookup(boxed).try_as<SolidColorBrush>())
                        b.Color(col);
            }
        }
    }

    fire_and_forget MainWindow::PickBackgroundAsync()
    {
        auto lifetime = get_strong();
        StoragePickers::FileOpenPicker picker(this->AppWindow().Id());
        picker.Title(L"选择背景图片");
        picker.SettingsIdentifier(L"BackgroundImage");
        picker.SuggestedStartLocation(StoragePickers::PickerLocationId::PicturesLibrary);
        picker.FileTypeChoices().Insert(
            L"图片文件",
            winrt::single_threaded_vector<hstring>(
                { L".png", L".jpg", L".jpeg", L".bmp", L".gif" }));
        picker.InitialFileTypeIndex(0);
        picker.ViewMode(StoragePickers::PickerViewMode::Thumbnail);
        auto file = co_await picker.PickSingleFileAsync();
        if (file && m_settings)
        {
            m_settings->setBackgroundImage(std::wstring(file.Path()));
            m_lastBgPath.clear(); // 强制刷新
            BgPathLabel().Text(fileNameFromPath(file.Path()));
            RebuildRecentBackgrounds();
        }
    }

    // ---- 背景设置 ----

    void MainWindow::InitBackgroundControls()
    {
        if (!m_settings) return;
        m_bgInit = true;
        auto st = m_settings->state();
        BlurModeCombo().SelectedIndex(m_settings->blurMode() - 1);
        BlurRadiusSlider().Value(m_settings->blurRadius());
        OverlaySlider().Value(st.bgOverlayOpacity);
        MonetToggle().IsChecked(m_settings->monetEnabled());
        SeqIconToggle().IsChecked(m_settings->sequentialIconListStyle());
        RememberToggle().IsChecked(m_settings->rememberWindowBounds());
        BgPathLabel().Text(st.bgHasImage ? hstring(L"已设置背景") : hstring(L"未选择图片"));
        m_bgInit = false;
    }

    void MainWindow::RebuildRecentBackgrounds()
    {
        if (!m_settings) return;
        RecentBgPanel().Children().Clear();
        auto list = m_settings->recentBackgrounds();
        int n = 0;
        for (auto const& path : list)
        {
            if (n++ >= 8) break;
            std::wstring p = path;
            for (auto& ch : p) if (ch == L'\\') ch = L'/';
            Imaging::BitmapImage bmp;
            bmp.DecodePixelWidth(176);
            bmp.UriSource(Windows::Foundation::Uri(hstring(L"file:///" + p)));
            Image img; img.Source(bmp); img.Stretch(Stretch::UniformToFill);
            Button btn; btn.Width(88); btn.Height(50); btn.Padding({}); btn.Content(img);
            std::wstring orig = path;
            btn.Click([this, orig](auto&&, auto&&) {
                if (m_settings) { m_settings->setBackgroundImage(orig); m_lastBgPath.clear(); }
            });
            RecentBgPanel().Children().Append(btn);
        }
    }

    void MainWindow::RebuildPalette(midi::WinUiSettingsState const& st)
    {
        PalettePanel().Children().Clear();
        if (!m_settings || !m_settings->monetEnabled()) return;
        for (auto argb : st.bgPalette)
        {
            Button btn; btn.Width(28); btn.Height(28); btn.Padding({});
            btn.Background(SolidColorBrush(colorFromArgb(argb)));
            unsigned int a = argb;
            btn.Click([this, a](auto&&, auto&&) { if (m_settings) m_settings->setAccentColor(a); });
            PalettePanel().Children().Append(btn);
        }
    }

    void MainWindow::OnBgSelect(IInspectable const&, RoutedEventArgs const&) { PickBackgroundAsync(); }

    void MainWindow::OnBgClear(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_settings) m_settings->clearBackground();
        m_lastBgPath.clear();
        BgPathLabel().Text(L"未选择图片");
        m_bgInit = true; MonetToggle().IsChecked(false); m_bgInit = false;
        PalettePanel().Children().Clear();
    }

    void MainWindow::OnBlurModeChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_bgInit || !m_settings) return;
        int idx = BlurModeCombo().SelectedIndex();
        if (idx >= 0) m_settings->setBlurMode(idx + 1);
    }

    void MainWindow::OnBlurRadiusChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& e)
    {
        if (m_bgInit || !m_settings) return;
        m_settings->setBlurRadius((int)e.NewValue());
    }

    void MainWindow::OnOverlayChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& e)
    {
        if (m_bgInit || !m_settings) return;
        m_settings->setOverlayOpacity((float)e.NewValue());
    }

    void MainWindow::OnMonetToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_bgInit || !m_settings) return;
        auto c = MonetToggle().IsChecked();
        m_settings->setMonetEnabled(c && c.Value());
    }

    void MainWindow::OnSeqIconToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_bgInit || !m_settings) return;
        auto c = SeqIconToggle().IsChecked();
        m_settings->setSequentialIconListStyle(c && c.Value());
        m_lastPlayMode = -1;
        Refresh();
    }

    void MainWindow::OnRememberToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_bgInit || !m_settings) return;
        auto c = RememberToggle().IsChecked();
        m_settings->setRememberWindowBounds(c && c.Value());
    }

    // ---- 音频设置 ----

    void MainWindow::InitAudioControls()
    {
        if (!m_core) return;
        m_settingsInit = true;
        AudioStatusLabel().Text(hstring(m_core->audioStatus()));

        AudioDeviceCombo().Items().Clear();
        auto devs = m_core->audioOutputDevices();
        auto cur = m_core->currentAudioDevice();
        int sel = -1, i = 0;
        for (auto const& d : devs) { AudioDeviceCombo().Items().Append(box_value(hstring(d))); if (d == cur) sel = i; ++i; }
        if (sel >= 0) AudioDeviceCombo().SelectedIndex(sel);

        SampleRateCombo().Items().Clear();
        auto srs = m_core->sampleRates(); int csr = m_core->currentSampleRate(); sel = -1; i = 0;
        for (auto r : srs) { wchar_t b[16]; swprintf_s(b, L"%d", r); SampleRateCombo().Items().Append(box_value(hstring(b))); if (r == csr) sel = i; ++i; }
        if (sel >= 0) SampleRateCombo().SelectedIndex(sel);

        BufferSizeCombo().Items().Clear();
        auto bs = m_core->bufferSizes(); int cbs = m_core->currentBufferSize(); sel = -1; i = 0;
        for (auto b : bs) { wchar_t s[16]; swprintf_s(s, L"%d", b); BufferSizeCombo().Items().Append(box_value(hstring(s))); if (b == cbs) sel = i; ++i; }
        if (sel >= 0) BufferSizeCombo().SelectedIndex(sel);
        FileAssociationButton().Content(box_value(
            m_core->isMidiFileAssociated() ? hstring(L"取消 MIDI 文件关联")
                                           : hstring(L"关联 MIDI 文件")));
        m_settingsInit = false;
    }

    void MainWindow::OnFileAssociation(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_core) return;
        const bool enable = !m_core->isMidiFileAssociated();
        if (!m_core->setMidiFileAssociated(enable))
        {
            NoticeBar().Title(L"文件关联失败");
            NoticeBar().Message(L"无法更新当前用户的 MIDI 文件关联。");
            NoticeBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
            NoticeBar().IsOpen(true);
            return;
        }
        FileAssociationButton().Content(box_value(
            enable ? hstring(L"取消 MIDI 文件关联")
                   : hstring(L"关联 MIDI 文件")));
    }

    void MainWindow::OnAudioDeviceChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_settingsInit || !m_core) return;
        auto item = AudioDeviceCombo().SelectedItem();
        if (item) { m_core->setAudioDevice(winrt::unbox_value<hstring>(item).c_str()); InitAudioControls(); }
    }

    void MainWindow::OnSampleRateChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_settingsInit || !m_core) return;
        auto item = SampleRateCombo().SelectedItem();
        if (item) { m_core->setSampleRate(_wtoi(winrt::unbox_value<hstring>(item).c_str())); AudioStatusLabel().Text(hstring(m_core->audioStatus())); }
    }

    void MainWindow::OnBufferSizeChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_settingsInit || !m_core) return;
        auto item = BufferSizeCombo().SelectedItem();
        if (item) { m_core->setBufferSize(_wtoi(winrt::unbox_value<hstring>(item).c_str())); AudioStatusLabel().Text(hstring(m_core->audioStatus())); }
    }

    void MainWindow::OnTestSound(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_core) m_core->playTestSound();
    }

    // ---- 字体设置 ----

    void MainWindow::InitFontControls()
    {
        if (!m_settings) return;
        m_settingsInit = true;
        UiFontCombo().Items().Clear();
        PlaylistFontCombo().Items().Clear();
        auto fonts = m_settings->systemFonts();
        auto ui = m_settings->uiFontName();
        auto pl = m_settings->playlistFontName();
        int uiSel = -1, plSel = -1, i = 0;
        for (auto const& f : fonts)
        {
            auto h = hstring(f);
            UiFontCombo().Items().Append(box_value(h));
            PlaylistFontCombo().Items().Append(box_value(h));
            if (f == ui) uiSel = i;
            if (f == pl) plSel = i;
            ++i;
        }
        if (uiSel >= 0) UiFontCombo().SelectedIndex(uiSel);
        if (plSel >= 0) PlaylistFontCombo().SelectedIndex(plSel);
        UiFontSizeSlider().Value(m_settings->uiFontSize());
        PlaylistFontSizeSlider().Value(m_settings->playlistFontSize());
        m_settingsInit = false;
    }

    void MainWindow::OnUiFontChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_settingsInit || !m_settings) return;
        auto item = UiFontCombo().SelectedItem();
        if (item) { m_settings->setUiFontName(winrt::unbox_value<hstring>(item).c_str()); ApplyFonts(); }
    }

    void MainWindow::OnUiFontSizeChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& e)
    {
        if (m_settingsInit || !m_settings) return;
        m_settings->setUiFontSize((float)e.NewValue());
        ApplyFonts();
    }

    void MainWindow::OnPlaylistFontChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_settingsInit || !m_settings) return;
        auto item = PlaylistFontCombo().SelectedItem();
        if (item) { m_settings->setPlaylistFontName(winrt::unbox_value<hstring>(item).c_str()); ApplyFonts(); }
    }

    void MainWindow::OnPlaylistFontSizeChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& e)
    {
        if (m_settingsInit || !m_settings) return;
        m_settings->setPlaylistFontSize((float)e.NewValue());
        ApplyFonts();
    }

    void MainWindow::ApplyFonts()
    {
        if (!m_settings) return;
        auto ui = m_settings->uiFontName();
        if (!ui.empty()) Nav().FontFamily(Media::FontFamily(hstring(ui)));
        Nav().FontSize(m_settings->uiFontSize());
        auto pl = m_settings->playlistFontName();
        if (!pl.empty()) PlaylistView().FontFamily(Media::FontFamily(hstring(pl)));
        PlaylistView().FontSize(m_settings->playlistFontSize());
    }

    // ---- 拖放 / 崩溃恢复 ----

    void MainWindow::OnRootDragOver(IInspectable const&, DragEventArgs const& e)
    {
        if (e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
            e.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
    }

    void MainWindow::OnRootDrop(IInspectable const&, DragEventArgs const& e)
    {
        OnRootDropAsync(e);
    }

    void MainWindow::OnTrackClipSizeChanged(IInspectable const&, SizeChangedEventArgs const& e)
    {
        Media::RectangleGeometry geo;
        geo.Rect(Windows::Foundation::Rect{ 0, 0, e.NewSize().Width, e.NewSize().Height });
        TrackClip().Clip(geo);
    }

    void MainWindow::OnRootLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        double scale = 1.0;
        if (auto xr = RootGrid().XamlRoot()) scale = xr.RasterizationScale();
        if (auto aw = this->AppWindow())
        {
            auto saved = m_settings ? m_settings->windowState() : midi::WinWindowState{};
            auto area = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
                aw.Id(), Microsoft::UI::Windowing::DisplayAreaFallback::Primary);
            auto wa = area.WorkArea();
            int w = std::clamp((int)(saved.width * scale), (int)(800 * scale), wa.Width);
            int h = std::clamp((int)(saved.height * scale), (int)(600 * scale), wa.Height);
            int x = saved.remember ? (int)(saved.x * scale) : wa.X + (wa.Width - w) / 2;
            int y = saved.remember ? (int)(saved.y * scale) : wa.Y + (wa.Height - h) / 2;
            x = std::clamp(x, wa.X, wa.X + wa.Width - w);
            y = std::clamp(y, wa.Y, wa.Y + wa.Height - h);
            aw.MoveAndResize({ x, y, w, h });

            if (auto presenter = aw.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
            {
                presenter.IsAlwaysOnTop(saved.alwaysOnTop);
                PinButton().IsChecked(saved.alwaysOnTop);
                PinIcon().Glyph(glyphStr(saved.alwaysOnTop ? kIconPinned : kIconPin));
                if (saved.remember && saved.maximized)
                    presenter.Maximize();
            }

            aw.Changed([this](auto const& sender,
                              Microsoft::UI::Windowing::AppWindowChangedEventArgs const& args)
            {
                if (m_windowResizeGuard) return;
                if (args.DidSizeChange())
                {
                    double currentScale = 1.0;
                    if (auto xr = RootGrid().XamlRoot()) currentScale = xr.RasterizationScale();
                    auto size = sender.Size();
                    const int minWidth = (int)(800 * currentScale);
                    const int minHeight = (int)(600 * currentScale);
                    if (size.Width < minWidth || size.Height < minHeight)
                    {
                        m_windowResizeGuard = true;
                        sender.Resize({ (std::max)(size.Width, minWidth),
                                        (std::max)(size.Height, minHeight) });
                        m_windowResizeGuard = false;
                    }
                }

                if (args.DidPositionChange() || args.DidSizeChange() ||
                    args.DidPresenterChange())
                    SaveWindowState();
            });
            aw.Closing([this](auto const&,
                              Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
            {
                if (!m_allowClose && m_core && m_core->state().hasUnsavedChanges)
                {
                    args.Cancel(true);
                    ConfirmCloseAsync();
                    return;
                }
                SaveWindowState();
            });
        }
    }

    void MainWindow::SaveWindowState()
    {
        if (!m_settings) return;
        auto state = m_settings->windowState();
        if (!state.remember) return;

        auto aw = this->AppWindow();
        if (!aw) return;
        auto presenter = aw.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        state.maximized = presenter &&
            presenter.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Maximized;
        state.alwaysOnTop = presenter && presenter.IsAlwaysOnTop();
        if (!state.maximized)
        {
            double scale = 1.0;
            if (auto xr = RootGrid().XamlRoot()) scale = xr.RasterizationScale();
            auto pos = aw.Position();
            auto size = aw.Size();
            state.x = (int)(pos.X / scale);
            state.y = (int)(pos.Y / scale);
            state.width = (int)(size.Width / scale);
            state.height = (int)(size.Height / scale);
        }
        m_settings->saveWindowState(state);
    }

    fire_and_forget MainWindow::OnRootDropAsync(DragEventArgs e)
    {
        auto lifetime = get_strong();
        if (!e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
            co_return;
        auto def = e.GetDeferral();
        auto items = co_await e.DataView().GetStorageItemsAsync();
        std::vector<std::wstring> paths;
        for (auto const& it : items)
        {
            std::wstring p(it.Path());
            auto dot = p.find_last_of(L'.');
            if (dot != std::wstring::npos)
            {
                std::wstring ext = p.substr(dot);
                for (auto& c : ext) c = (wchar_t)towlower(c);
                if (ext == L".mid" || ext == L".midi") paths.push_back(p);
            }
        }
        if (!paths.empty() && m_core) { m_core->addFiles(paths); m_lastTrackCount = -1; Refresh(); }
        def.Complete();
    }

    fire_and_forget MainWindow::ShowCrashDialogAsync()
    {
        auto lifetime = get_strong();
        ContentDialog dlg;
        dlg.XamlRoot(RootGrid().XamlRoot());
        dlg.Title(box_value(hstring(L"插件进程已崩溃")));
        dlg.Content(box_value(hstring(L"VST 插件子进程发生崩溃。可卸载并重新加载插件以恢复。")));
        dlg.PrimaryButtonText(L"卸载并恢复");
        dlg.CloseButtonText(L"关闭");
        auto r = co_await dlg.ShowAsync();
        if (m_core && m_core->state().workerCrashed)
            m_core->terminateCrashedWorker();

        if (r == ContentDialogResult::Primary && m_core)
        {
            m_core->unload();
        }
        Refresh();
    }

    // ---- 文件选择协程 ----

    fire_and_forget MainWindow::PickAndAddAsync()
    {
        auto lifetime = get_strong();
        StoragePickers::FileOpenPicker picker(this->AppWindow().Id());
        picker.Title(L"添加 MIDI 文件");
        picker.SettingsIdentifier(L"MidiFiles");
        picker.SuggestedStartLocation(StoragePickers::PickerLocationId::MusicLibrary);
        picker.FileTypeChoices().Insert(
            L"MIDI 文件",
            winrt::single_threaded_vector<hstring>({ L".mid", L".midi" }));
        picker.InitialFileTypeIndex(0);
        auto files = co_await picker.PickMultipleFilesAsync();
        if (files.Size() > 0 && m_core)
        {
            std::vector<std::wstring> paths;
            for (auto const& f : files) paths.push_back(std::wstring(f.Path()));
            const int added = m_core->addFiles(paths);
            if (added < (int)paths.size())
            {
                NoticeBar().Severity(InfoBarSeverity::Informational);
                NoticeBar().Title(L"部分文件未添加");
                NoticeBar().Message(L"重复文件或无效 MIDI 文件已忽略。");
                NoticeBar().IsOpen(true);
            }
            m_lastTrackCount = -1;
            Refresh();
        }
    }

    fire_and_forget MainWindow::ConfirmClearPlaylistAsync()
    {
        auto lifetime = get_strong();
        if (!m_core || m_core->state().tracks.empty()) co_return;
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(hstring(L"清空播放列表")));
        dialog.Content(box_value(hstring(L"所有曲目将从当前列表移除。")));
        dialog.PrimaryButtonText(L"清空");
        dialog.CloseButtonText(L"取消");
        if (co_await dialog.ShowAsync() == ContentDialogResult::Primary && m_core)
        {
            m_core->clearPlaylist();
            m_lastTrackCount = -1;
            Refresh();
        }
    }

    fire_and_forget MainWindow::ConfirmCloseAsync()
    {
        auto lifetime = get_strong();
        if (!m_core) co_return;
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(hstring(L"播放列表尚未保存")));
        dialog.Content(box_value(hstring(L"退出前保存当前播放列表。")));
        dialog.PrimaryButtonText(L"保存并退出");
        dialog.SecondaryButtonText(L"不保存并退出");
        dialog.CloseButtonText(L"取消");
        const auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::Primary)
        {
            auto picker = playlistSavePicker(this->AppWindow().Id());
            picker.Title(L"退出前保存播放列表");
            auto file = co_await picker.PickSaveFileAsync();
            if (!file || !m_core->saveList(std::wstring(file.Path())))
                co_return;
        }
        else if (result != ContentDialogResult::Secondary)
        {
            co_return;
        }

        m_allowClose = true;
        SaveWindowState();
        this->Close();
    }

    fire_and_forget MainWindow::PickAndExportAsync()
    {
        auto lifetime = get_strong();
        if (!m_core) co_return;

        auto st = m_core->state();
        int trackIndex = PlaylistView().SelectedIndex();
        if (trackIndex < 0) trackIndex = st.trackIndex;
        if (trackIndex < 0 && !st.tracks.empty()) trackIndex = 0;
        if (trackIndex < 0 || trackIndex >= (int)st.tracks.size())
        {
            NoticeBar().Severity(InfoBarSeverity::Warning);
            NoticeBar().Title(L"导出不可用");
            NoticeBar().Message(L"当前没有可导出的 MIDI 曲目。");
            NoticeBar().IsOpen(true);
            co_return;
        }

        StackPanel options;
        options.Spacing(10);

        TextBlock formatLabel;
        formatLabel.Text(L"文件格式");
        ComboBox formatCombo;
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            formatCombo, L"导出文件格式");
        for (auto text : { L"WAV", L"FLAC", L"Ogg Vorbis" })
            formatCombo.Items().Append(box_value(hstring(text)));
        formatCombo.SelectedIndex(0);
        options.Children().Append(formatLabel);
        options.Children().Append(formatCombo);

        TextBlock sampleRateLabel;
        sampleRateLabel.Text(L"采样率");
        ComboBox sampleRateCombo;
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            sampleRateCombo, L"导出采样率");
        for (auto text : { L"44100 Hz", L"48000 Hz", L"88200 Hz", L"96000 Hz", L"192000 Hz" })
            sampleRateCombo.Items().Append(box_value(hstring(text)));
        sampleRateCombo.SelectedIndex(3);
        options.Children().Append(sampleRateLabel);
        options.Children().Append(sampleRateCombo);

        TextBlock bitDepthLabel;
        bitDepthLabel.Text(L"位深度");
        ComboBox bitDepthCombo;
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            bitDepthCombo, L"导出位深度");
        auto rebuildBitDepths = [bitDepthCombo](int formatIndex)
        {
            bitDepthCombo.Items().Clear();
            if (formatIndex == 2)
            {
                bitDepthCombo.Items().Append(box_value(hstring(L"32-bit Float")));
                bitDepthCombo.SelectedIndex(0);
                return;
            }
            bitDepthCombo.Items().Append(box_value(hstring(L"16-bit")));
            bitDepthCombo.Items().Append(box_value(hstring(L"24-bit")));
            if (formatIndex == 0)
                bitDepthCombo.Items().Append(box_value(hstring(L"32-bit Float")));
            bitDepthCombo.SelectedIndex(1);
        };
        rebuildBitDepths(0);
        options.Children().Append(bitDepthLabel);
        options.Children().Append(bitDepthCombo);

        TextBlock qualityLabel;
        qualityLabel.Text(L"压缩质量");
        ComboBox qualityCombo;
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            qualityCombo, L"导出压缩质量");
        for (auto text : { L"标准", L"较高", L"最高" })
            qualityCombo.Items().Append(box_value(hstring(text)));
        qualityCombo.SelectedIndex(1);
        qualityCombo.IsEnabled(false);
        options.Children().Append(qualityLabel);
        options.Children().Append(qualityCombo);

        ToggleSwitch autoTailToggle;
        autoTailToggle.Header(box_value(hstring(L"自动检测尾音")));
        autoTailToggle.IsOn(true);
        options.Children().Append(autoTailToggle);
        TextBlock tailLabel;
        tailLabel.Text(L"固定尾音时长（秒）");
        Slider tailSlider;
        tailSlider.Minimum(0.0);
        tailSlider.Maximum(10.0);
        tailSlider.StepFrequency(0.5);
        tailSlider.Value(3.0);
        tailSlider.IsEnabled(false);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            tailSlider, L"固定尾音时长");
        options.Children().Append(tailLabel);
        options.Children().Append(tailSlider);

        formatCombo.SelectionChanged(
            [rebuildBitDepths, qualityCombo](auto const& sender, auto const&)
            {
                const int formatIndex = sender.as<ComboBox>().SelectedIndex();
                rebuildBitDepths(formatIndex);
                qualityCombo.IsEnabled(formatIndex != 0);
            });
        autoTailToggle.Toggled([tailSlider](auto const& sender, auto const&)
        {
            tailSlider.IsEnabled(!sender.as<ToggleSwitch>().IsOn());
        });

        ContentDialog optionsDialog;
        optionsDialog.XamlRoot(RootGrid().XamlRoot());
        optionsDialog.Title(box_value(hstring(L"导出音频")));
        optionsDialog.Content(options);
        optionsDialog.PrimaryButtonText(L"选择保存位置");
        optionsDialog.CloseButtonText(L"取消");
        if (co_await optionsDialog.ShowAsync() != ContentDialogResult::Primary)
            co_return;

        const int formatIndex = formatCombo.SelectedIndex();
        const std::wstring formatName =
            formatIndex == 1 ? L"FLAC" : formatIndex == 2 ? L"Ogg Vorbis" : L"WAV";
        const wchar_t* extension =
            formatIndex == 1 ? L".flac" : formatIndex == 2 ? L".ogg" : L".wav";
        const double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        const int sampleRateIndex = std::clamp(sampleRateCombo.SelectedIndex(), 0, 4);
        const int selectedBitIndex = (std::max)(0, bitDepthCombo.SelectedIndex());
        const int bitDepth = formatIndex == 2 ? 32 : selectedBitIndex == 0 ? 16 :
                             selectedBitIndex == 1 ? 24 : 32;
        const bool useFloatingPoint = formatIndex == 2 ||
                                      (formatIndex == 0 && bitDepth == 32);

        StoragePickers::FileSavePicker picker(this->AppWindow().Id());
        picker.Title(L"导出音频");
        picker.SettingsIdentifier(L"AudioExport");
        picker.SuggestedStartLocation(StoragePickers::PickerLocationId::MusicLibrary);
        auto extensions = winrt::single_threaded_vector<hstring>({ extension });
        picker.FileTypeChoices().Insert(hstring(formatName + L" 音频"), extensions);
        picker.InitialFileTypeIndex(0);
        picker.DefaultFileExtension(extension);
        picker.SuggestedFileName(exportFileName(st.tracks[(size_t)trackIndex]));
        picker.ShowOverwritePrompt(true);
        auto file = co_await picker.PickSaveFileAsync();
        if (!file) co_return;

        midi::WinExportRequest request;
        request.trackIndex = trackIndex;
        request.targetPath = std::wstring(file.Path());
        request.formatName = formatName;
        request.sampleRate = sampleRates[sampleRateIndex];
        request.bitDepth = bitDepth;
        request.useFloatingPoint = useFloatingPoint;
        request.autoTail = autoTailToggle.IsOn();
        request.fixedTailSeconds = tailSlider.Value();
        request.qualityIndex = (std::max)(0, qualityCombo.SelectedIndex());
        request.title = std::wstring(st.tracks[(size_t)trackIndex]);

        if (!m_core->exportAudio(request))
        {
            NoticeBar().Severity(InfoBarSeverity::Error);
            NoticeBar().Title(L"导出失败");
            NoticeBar().Message(L"导出任务未启动。");
            NoticeBar().IsOpen(true);
            co_return;
        }

        ExportBar().Severity(InfoBarSeverity::Informational);
        ExportBar().Title(L"正在导出");
        ExportBar().Message(L"正在写入音频文件。");
        ExportProgressBar().Value(0);
        ExportCancelButton().IsEnabled(true);
        ExportBar().IsOpen(true);
        Refresh();
    }

    fire_and_forget MainWindow::LoadListAsync()
    {
        auto lifetime = get_strong();
        StoragePickers::FileOpenPicker picker(this->AppWindow().Id());
        picker.Title(L"加载播放列表");
        picker.SettingsIdentifier(L"PlaylistOpen");
        picker.SuggestedStartLocation(StoragePickers::PickerLocationId::DocumentsLibrary);
        picker.FileTypeChoices().Insert(
            L"JSON 播放列表",
            winrt::single_threaded_vector<hstring>({ L".json" }));
        picker.InitialFileTypeIndex(0);
        auto file = co_await picker.PickSingleFileAsync();
        if (file && m_core)
        {
            if (m_core->loadList(std::wstring(file.Path())))
            {
                m_lastTrackCount = -1;
                Refresh();
            }
            else
            {
                const auto error = m_core->state().playlistError;
                NoticeBar().Severity(InfoBarSeverity::Error);
                NoticeBar().Title(L"无法加载播放列表");
                NoticeBar().Message(error.empty() ? hstring(L"播放列表格式无效或文件不可读。")
                                                  : hstring(error));
                NoticeBar().IsOpen(true);
            }
        }
    }

    fire_and_forget MainWindow::SaveListAsync()
    {
        auto lifetime = get_strong();
        auto picker = playlistSavePicker(this->AppWindow().Id());
        auto file = co_await picker.PickSaveFileAsync();
        if (file && m_core && !m_core->saveList(std::wstring(file.Path())))
        {
            const auto error = m_core->state().playlistError;
            NoticeBar().Severity(InfoBarSeverity::Error);
            NoticeBar().Title(L"无法保存播放列表");
            NoticeBar().Message(error.empty() ? hstring(L"目标文件不可写。")
                                              : hstring(error));
            NoticeBar().IsOpen(true);
        }
    }

    // ---- 刷新 ----

    hstring MainWindow::FormatTime(double seconds)
    {
        if (seconds < 0 || seconds > 360000) seconds = 0;
        int s = (int)seconds;
        wchar_t buf[32];
        if (s >= 3600)
            swprintf_s(buf, L"%d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60);
        else
            swprintf_s(buf, L"%d:%02d", s / 60, s % 60);
        return hstring(buf);
    }

    void MainWindow::RebuildPlaylist(midi::WinState const& st)
    {
        m_rebuilding = true;
        auto sel = PlaylistView().SelectedIndex();
        m_items.Clear();
        int i = 1;
        for (size_t index = 0; index < st.tracks.size(); ++index)
        {
            wchar_t num[16];
            swprintf_s(num, L"%d. ", i++);
            const bool available = index >= st.trackAvailable.size() ||
                                   st.trackAvailable[index];
            m_items.Append(box_value(hstring(num) + hstring(st.tracks[index]) +
                                     (available ? hstring{} : hstring(L"（文件不可用）"))));
        }
        if (sel >= 0 && sel < (int)st.tracks.size())
            PlaylistView().SelectedIndex(sel);
        PlaylistEmptyDropZone().Visibility(st.tracks.empty() ? Visibility::Visible
                                                             : Visibility::Collapsed);
        m_rebuilding = false;
    }

    void MainWindow::Refresh()
    {
        if (!m_core) return;
        auto st = m_core->state();

        if (m_settings)
            ApplyBackgroundVisuals(m_settings->state());

        // worker 崩溃：弹一次恢复对话框
        if (st.workerCrashed && !m_crashShown) { m_crashShown = true; ShowCrashDialogAsync(); }
        if (!st.workerCrashed) m_crashShown = false;

        // 首次运行 / 设备回退：顶部通知一次
        if (!m_noticeShown)
        {
            if (st.firstRunAudio)
            {
                m_noticeShown = true;
                NoticeBar().Severity(InfoBarSeverity::Warning);
                NoticeBar().Title(L"首次运行");
                NoticeBar().Message(L"已自动选择音频设备，可在“设置”中调整。");
                NoticeBar().IsOpen(true);
            }
            else if (st.deviceFallback)
            {
                m_noticeShown = true;
                NoticeBar().Severity(InfoBarSeverity::Warning);
                NoticeBar().Title(L"音频设备");
                NoticeBar().Message(L"原音频设备不可用，已回退到默认设备。");
                NoticeBar().IsOpen(true);
            }
        }

        // 扫描指示
        ScanSpinner().IsActive(st.scanning);
        ScanSpinner().Visibility(st.scanning ? Visibility::Visible : Visibility::Collapsed);
        ScanButton().IsEnabled(!st.scanning && !st.loadInProgress);
        PluginCombo().IsEnabled(!st.scanning && !st.loadInProgress);

        if (st.scanFinished && st.scanSessionId != m_lastScanSessionId)
        {
            m_lastScanSessionId = st.scanSessionId;
            NoticeBar().Severity(st.scanSucceeded ? InfoBarSeverity::Success
                                                  : InfoBarSeverity::Error);
            NoticeBar().Title(st.scanSucceeded ? L"插件扫描完成"
                                               : L"插件扫描失败");
            NoticeBar().Message(hstring(st.scanMessage));
            NoticeBar().IsOpen(true);
        }

        if (st.loadFinished && st.loadSessionId != m_lastLoadSessionId)
        {
            m_lastLoadSessionId = st.loadSessionId;
            if (st.loadSucceeded)
                ShowPluginLoadToast(st.loadMessage);
            else
            {
                NoticeBar().Severity(InfoBarSeverity::Error);
                NoticeBar().Title(L"插件加载失败");
                NoticeBar().Message(hstring(st.loadMessage));
                NoticeBar().IsOpen(true);
            }
        }

        // 乐器下拉（仅列表数量变化时重建，按 id 恢复选中）
        if ((int)st.plugins.size() != m_lastPluginCount)
        {
            m_lastPluginCount = (int)st.plugins.size();
            std::wstring prevId;
            int prevSel = PluginCombo().SelectedIndex();
            if (prevSel >= 0 && prevSel < (int)m_pluginIds.size()) prevId = m_pluginIds[(size_t)prevSel];

            m_rebuildingPlugins = true;
            m_pluginIds.clear();
            PluginCombo().Items().Clear();
            int reselect = -1;
            for (auto const& p : st.plugins)
            {
                if (p.id == prevId) reselect = (int)m_pluginIds.size();
                m_pluginIds.push_back(p.id);
                PluginCombo().Items().Append(box_value(pluginDisplayName(p)));
            }
            if (reselect >= 0) PluginCombo().SelectedIndex(reselect);
            m_rebuildingPlugins = false;
        }

        // 状态/可用性
        bool canPlay = st.pluginLoaded && !st.workerCrashed;
        LibraryHint().Text(st.loadInProgress ? hstring(L"正在加载插件…")
                           : st.workerCrashed ? hstring(L"插件进程已崩溃，请重新加载")
                           : st.pluginLoaded ? (L"已加载: " + hstring(st.pluginName))
                           : hstring(L"选择一个 VST3 乐器插件开始演奏"));
        UnloadButton().IsEnabled(st.pluginLoaded || st.workerCrashed);
        EditorButton().IsEnabled(canPlay);
        ExportButton().IsEnabled(canPlay && st.hasSequence && !st.exportActive);
        PlayButton().IsEnabled(canPlay && st.hasSequence);
        StopButton().IsEnabled(canPlay && st.hasSequence);
        PrevButton().IsEnabled(canPlay && !st.tracks.empty());
        NextButton().IsEnabled(canPlay && !st.tracks.empty());
        PlayIcon().Glyph(glyphStr(st.playing ? kIconPause : kIconPlay));

        if (st.exportActive)
        {
            ExportBar().Severity(InfoBarSeverity::Informational);
            ExportBar().Title(L"正在导出");
            ExportBar().Message(L"正在写入音频文件。");
            ExportProgressBar().Value((double)st.exportProgress * 100.0);
            ExportCancelButton().IsEnabled(true);
            ExportBar().IsOpen(true);
        }
        else if (st.exportFinished && !st.exportMessage.empty() &&
                 st.exportSessionId != m_lastExportSessionId)
        {
            m_lastExportSessionId = st.exportSessionId;
            ExportBar().Severity(st.exportSucceeded ? InfoBarSeverity::Success
                                                    : InfoBarSeverity::Warning);
            ExportBar().Title(st.exportSucceeded ? hstring(L"导出完成")
                                                 : hstring(L"导出未完成"));
            ExportBar().Message(hstring(st.exportMessage));
            ExportProgressBar().Value(st.exportSucceeded ? 100.0 : 0.0);
            ExportCancelButton().IsEnabled(false);
            ExportBar().IsOpen(true);
        }

        // 播放模式图标
        if (st.playMode != m_lastPlayMode)
        {
            m_lastPlayMode = st.playMode;
            const bool listStyle =
                m_settings && m_settings->sequentialIconListStyle();
            LoopModeIcon().Glyph(glyphStr(modeInfo(st.playMode, listStyle).glyph));
            LoopModeButton().SetValue(ToolTipService::ToolTipProperty(),
                                      box_value(hstring(L"播放模式: ") +
                                                modeInfo(st.playMode, listStyle).name));
        }

        // 播放列表
        if ((int)st.tracks.size() != m_lastTrackCount)
        {
            m_lastTrackCount = (int)st.tracks.size();
            RebuildPlaylist(st);
            wchar_t cnt[32]; swprintf_s(cnt, L"%d 个曲目", (int)st.tracks.size());
            TrackCountLabel().Text(cnt);
        }

        // 当前播放行高亮（与选中态区分），并在切歌时滚动可见
        if (st.trackIndex != m_lastTrackIndex)
        {
            int total = (int)st.tracks.size();
            m_lastTrackIndex = st.trackIndex;
            for (int i = 0; i < total; ++i)
            {
                if (auto c = PlaylistView().ContainerFromIndex(i).try_as<ListViewItem>())
                    c.Background(i == st.trackIndex ? m_accentBrush.as<Brush>() : nullptr);
            }
            if (st.trackIndex >= 0 && st.trackIndex < total)
                PlaylistView().ScrollIntoView(m_items.GetAt(st.trackIndex));
        }

        // 曲名 + 时间
        TrackLabel().Text(st.midiName.empty() ? hstring(L"未加载") : hstring(st.midiName));

        // 曲名跑马灯：超出可视宽度时左右往返滚动
        double natural = TrackLabel().ActualWidth();
        double avail = TrackClip().ActualWidth();
        if (natural > avail + 2.0)
        {
            double maxOff = -(natural - avail + 16.0);
            m_scrollPos += m_scrollDir * 0.8;
            if (m_scrollPos <= maxOff) { m_scrollPos = maxOff; m_scrollDir = 1; }
            else if (m_scrollPos >= 0.0) { m_scrollPos = 0.0; m_scrollDir = -1; }
            TrackScroll().X(m_scrollPos);
        }
        else
        {
            m_scrollPos = 0.0; m_scrollDir = -1;
            TrackScroll().X(0.0);
        }
        double dur = st.durationSamples;
        double sr = dur > 0 ? m_core->sampleRate() : 44100.0;
        if (dur > 0)
            TimeLabel().Text(FormatTime(st.positionSamples / sr) + L" / " + FormatTime(dur / sr));
        else
        {
            TimeLabel().Text(L"0:00 / 0:00");
            HideProgressTooltip();
        }

        // 进度（用户拖动后短暂不回填）
        if (m_seekHoldFrames > 0) { --m_seekHoldFrames; }
        else
        {
            m_suppressProgress = true;
            ProgressSlider().Value(dur > 0 ? st.positionSamples / dur : 0.0);
            m_suppressProgress = false;
        }

        // 音量 + 静音图标
        m_suppressVolume = true;
        VolumeSlider().Value(st.volume);
        m_suppressVolume = false;
        wchar_t volGlyph = (st.muted || st.volume <= 0.001f) ? kIconVolMute
                           : (st.volume <= 0.5f) ? kIconVolLow : kIconVolHigh;
        MuteIcon().Glyph(glyphStr(volGlyph));
    }
}
