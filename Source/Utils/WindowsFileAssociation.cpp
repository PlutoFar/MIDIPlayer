#include "WindowsFileAssociation.h"
#include <vector>

#if JUCE_WINDOWS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

namespace {
constexpr auto progIdKey = LR"(Software\Classes\ModernMidiPlayer.MIDIFile)";
constexpr auto applicationKey = LR"(Software\Classes\Applications\MidiPlayer.exe)";
constexpr auto capabilitiesKey = LR"(Software\ModernMidiPlayer\Capabilities)";

juce::Result setString(const juce::String &path, const wchar_t *name,
                       const juce::String &value) {
  HKEY key = nullptr;
  auto error = RegCreateKeyExW(HKEY_CURRENT_USER, path.toWideCharPointer(), 0,
                              nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                              nullptr, &key, nullptr);
  if (error == ERROR_SUCCESS) {
    const auto *wide = value.toWideCharPointer();
    error = RegSetValueExW(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(wide),
                          static_cast<DWORD>((wcslen(wide) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
  }
  if (error != ERROR_SUCCESS)
    return juce::Result::fail(L"无法注册文件关联：" + path +
                              L" (Windows " + juce::String(error) + ")");
  return juce::Result::ok();
}

juce::String associationExecutable(const wchar_t *extension) {
  DWORD length = 0;
  if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, extension, L"open",
                        nullptr, &length) != S_FALSE || length == 0)
    return {};
  std::vector<wchar_t> executable(length);
  if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, extension, L"open",
                        executable.data(), &length) != S_OK)
    return {};
  return juce::String(executable.data());
}
} // namespace
#endif

bool isMidiFileAssociatedToSelf() {
#if JUCE_WINDOWS
  const auto executable = juce::File::getSpecialLocation(
      juce::File::currentExecutableFile).getFullPathName();
  return associationExecutable(L".mid").equalsIgnoreCase(executable) &&
         associationExecutable(L".midi").equalsIgnoreCase(executable);
#else
  return false;
#endif
}

juce::Result registerMidiFileAssociation() {
#if JUCE_WINDOWS
  const auto executable = juce::File::getSpecialLocation(
      juce::File::currentExecutableFile).getFullPathName();
  const auto command = makeMidiFileAssociationCommand(executable);
  const auto icon = executable.quoted() + ",0";
  const auto progId = getMidiFileProgId();
  struct Entry { juce::String path; const wchar_t *name; juce::String value; };
  const Entry entries[] = {
      {progIdKey, nullptr, L"MIDI 音乐文件"},
      {juce::String(progIdKey) + R"(\shell\open\command)", nullptr, command},
      {juce::String(progIdKey) + R"(\DefaultIcon)", nullptr, icon},
      {applicationKey, L"FriendlyAppName", L"MIDI 播放器"},
      {juce::String(applicationKey) + R"(\shell\open\command)", nullptr, command},
      {juce::String(applicationKey) + R"(\DefaultIcon)", nullptr, icon},
      {juce::String(applicationKey) + R"(\SupportedTypes)", L".mid", ""},
      {juce::String(applicationKey) + R"(\SupportedTypes)", L".midi", ""},
      {LR"(Software\Classes\.mid\OpenWithProgids)", L"ModernMidiPlayer.MIDIFile", ""},
      {LR"(Software\Classes\.midi\OpenWithProgids)", L"ModernMidiPlayer.MIDIFile", ""},
      {capabilitiesKey, L"ApplicationName", L"MIDI 播放器"},
      {capabilitiesKey, L"ApplicationDescription", L"MIDI 文件播放器与 VST3 乐器宿主"},
      {capabilitiesKey, L"ApplicationIcon", icon},
      {juce::String(capabilitiesKey) + R"(\FileAssociations)", L".mid", progId},
      {juce::String(capabilitiesKey) + R"(\FileAssociations)", L".midi", progId},
      {LR"(Software\RegisteredApplications)", L"ModernMidiPlayer", capabilitiesKey},
  };
  for (const auto &entry : entries) {
    const auto result = setString(entry.path, entry.name, entry.value);
    if (result.failed()) {
      SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
      return result;
    }
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return juce::Result::ok();
#else
  return juce::Result::fail("Windows file associations are unavailable");
#endif
}
