#pragma once

#include <juce_core/juce_core.h>

inline juce::String getMidiFileProgId() {
  return "ModernMidiPlayer.MIDIFile";
}

inline juce::String makeMidiFileAssociationCommand(const juce::String &exePath) {
  return exePath.quoted() + " \"%1\"";
}

#if JUCE_WINDOWS
extern "C" {
typedef unsigned long DWORD;
typedef void *HKEY;
typedef unsigned char BYTE;
typedef BYTE *LPBYTE;

#ifndef HKEY_CURRENT_USER
#define HKEY_CURRENT_USER ((HKEY)(unsigned long long)0x80000001)
#endif

#ifndef KEY_READ
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#endif

#ifndef REG_SZ
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_SZ 1
#endif

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif

__declspec(dllimport) long __stdcall
RegOpenKeyExW(HKEY hKey, const wchar_t *lpSubKey, DWORD ulOptions,
              DWORD samDesired, HKEY *phkResult);
__declspec(dllimport) long __stdcall
RegCreateKeyExW(HKEY hKey, const wchar_t *lpSubKey, DWORD Reserved,
                wchar_t *lpClass, DWORD dwOptions, DWORD samDesired,
                void *lpSecurityAttributes, HKEY *phkResult,
                DWORD *lpdwDisposition);
__declspec(dllimport) long __stdcall
RegSetValueExW(HKEY hKey, const wchar_t *lpValueName, DWORD Reserved,
               DWORD dwType, const BYTE *lpData, DWORD cbData);
__declspec(dllimport) long __stdcall
RegQueryValueExW(HKEY hKey, const wchar_t *lpValueName, DWORD *lpReserved,
                 DWORD *lpType, LPBYTE lpData, DWORD *lpcbData);
__declspec(dllimport) long __stdcall RegCloseKey(HKEY hKey);
__declspec(dllimport) long __stdcall
RegDeleteValueW(HKEY hKey, const wchar_t *lpValueName);
__declspec(dllimport) long __stdcall RegDeleteTreeW(HKEY hKey,
                                                    const wchar_t *lpSubKey);

#ifndef SHCNE_ASSOCCHANGED
#define SHCNE_ASSOCCHANGED 0x08000000L
#define SHCNF_IDLIST 0x0000
#endif
__declspec(dllimport) void __stdcall SHChangeNotify(long wEventId,
                                                    unsigned int uFlags,
                                                    const void *dwItem1,
                                                    const void *dwItem2);
}

inline bool isMidiFileAssociatedToSelf() {
  HKEY hKey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.mid", 0,
                    KEY_READ, &hKey) != ERROR_SUCCESS)
    return false;

  wchar_t value[256] = {};
  DWORD size = sizeof(value);
  DWORD type = 0;
  bool result = false;

  if (RegQueryValueExW(hKey, nullptr, nullptr, &type, (LPBYTE)value, &size) ==
      ERROR_SUCCESS) {
    if (juce::String(value) == getMidiFileProgId()) {
      RegCloseKey(hKey);
      hKey = nullptr;
      if (RegOpenKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\ModernMidiPlayer.MIDIFile"
                        L"\\shell\\open\\command",
                        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t cmdValue[1024] = {};
        DWORD cmdSize = sizeof(cmdValue);
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type, (LPBYTE)cmdValue,
                             &cmdSize) == ERROR_SUCCESS) {
          const auto exePath = juce::File::getSpecialLocation(
                                   juce::File::currentExecutableFile)
                                   .getFullPathName();
          result = juce::String(cmdValue).containsIgnoreCase(exePath);
        }
      }
    }
  }

  if (hKey)
    RegCloseKey(hKey);
  return result;
}

inline bool setFileAssociationRegistryValue(const wchar_t *subKey,
                                            const juce::String &value) {
  HKEY hKey = nullptr;
  DWORD disposition = 0;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey,
                      &disposition) != ERROR_SUCCESS)
    return false;

  auto wideValue = value.toWideCharPointer();
  auto byteLen = (DWORD)((wcslen(wideValue) + 1) * sizeof(wchar_t));
  const bool success =
      RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE *)wideValue,
                     byteLen) == ERROR_SUCCESS;
  RegCloseKey(hKey);
  return success;
}

inline bool registerMidiFileAssociation() {
  const auto exePath =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile)
          .getFullPathName();
  const auto command = makeMidiFileAssociationCommand(exePath);
  const auto progId = getMidiFileProgId();

  bool ok = true;
  ok &= setFileAssociationRegistryValue(L"Software\\Classes\\.mid", progId);
  ok &= setFileAssociationRegistryValue(L"Software\\Classes\\.midi", progId);
  ok &= setFileAssociationRegistryValue(
      L"Software\\Classes\\ModernMidiPlayer.MIDIFile",
      L"MIDI \u97F3\u4E50\u6587\u4EF6");
  ok &= setFileAssociationRegistryValue(
      L"Software\\Classes\\ModernMidiPlayer.MIDIFile\\shell\\open\\command",
      command);
  ok &= setFileAssociationRegistryValue(
      L"Software\\Classes\\ModernMidiPlayer.MIDIFile\\DefaultIcon",
      exePath.quoted() + ",0");

  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return ok;
}

inline void removeMidiFileAssociation() {
  auto deleteRegValue = [](const wchar_t *subKey) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_WRITE, &hKey) ==
        ERROR_SUCCESS) {
      RegDeleteValueW(hKey, nullptr);
      RegCloseKey(hKey);
    }
  };

  deleteRegValue(L"Software\\Classes\\.mid");
  deleteRegValue(L"Software\\Classes\\.midi");
  RegDeleteTreeW(HKEY_CURRENT_USER,
                 L"Software\\Classes\\ModernMidiPlayer.MIDIFile");
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#else
inline bool isMidiFileAssociatedToSelf() { return false; }
inline bool registerMidiFileAssociation() { return false; }
inline void removeMidiFileAssociation() {}
#endif
