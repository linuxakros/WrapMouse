#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>

#include <atomic>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <cmath>
#include <utility>
#include <fstream>
#include <sstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")

// ============================================================
// Tray
// ============================================================

constexpr UINT WM_TRAYICON = WM_APP + 1;

constexpr UINT ID_ENABLE  = 1001;
constexpr UINT ID_EXIT    = 1003;
constexpr UINT ID_FIND_CURSOR = 1004;
constexpr UINT ID_START_WITH_WINDOWS = 1005;
constexpr UINT ID_PORTAL_EFFECT = 1006;
constexpr UINT ID_CROSSHAIR = 1007;
constexpr UINT ID_LANGUAGE = 1008;
constexpr UINT ID_LANGUAGE_BASE = 1100;

constexpr wchar_t DEFAULT_LANGUAGE[] = L"en";

constexpr UINT WM_PORTAL_EFFECT = WM_APP + 2;

constexpr DWORD DEFAULT_ENABLED = 0;
constexpr DWORD DEFAULT_FIND_CURSOR_ENABLED = 0;
constexpr DWORD DEFAULT_FIND_CURSOR_MAX_GAP_MS = 25;
constexpr DWORD DEFAULT_PORTAL_EFFECT_ENABLED = 0;
constexpr DWORD DEFAULT_CROSSHAIR_HORIZONTAL_LENGTH = 0xFFFFFFFF;
constexpr DWORD DEFAULT_CROSSHAIR_VERTICAL_LENGTH = 0xFFFFFFFF;
constexpr DWORD DEFAULT_CROSSHAIR_COLOR = 0x0000FF00;

constexpr DWORD DEFAULT_PORTAL_MIN_LENGTH = 40;
constexpr DWORD DEFAULT_PORTAL_MAX_LENGTH = 120;
constexpr DWORD DEFAULT_PORTAL_MIN_THICKNESS = 4;
constexpr DWORD DEFAULT_PORTAL_MAX_THICKNESS = 6;

// Couleurs Portal : format Registre 0x00RRGGBB.
constexpr DWORD DEFAULT_PORTAL_WRAP_DEPARTURE_COLOR = 0x001EAAFF; // Bleu cyan
constexpr DWORD DEFAULT_PORTAL_WRAP_ARRIVAL_COLOR = 0x00FF9619;   // Orange
constexpr DWORD DEFAULT_PORTAL_DIRECT_DEPARTURE_COLOR = 0x0000E5A0; // Turquoise
constexpr DWORD DEFAULT_PORTAL_DIRECT_ARRIVAL_COLOR = 0x00FFD500;   // Jaune doré

constexpr UINT PORTAL_EFFECT_DURATION_MS = 220;
constexpr UINT PORTAL_EFFECT_TIMER_MS = 16;

std::atomic<bool> g_running{ true };
std::atomic<bool> g_enabled{ false };
std::atomic<bool> g_findCursorEnabled{ false };
std::atomic<bool> g_portalEffectEnabled{ false };
std::atomic<bool> g_crosshairEnabled{ false };
std::atomic<bool> g_crosshairFeatureEnabled{ false };

HWND g_mainWindow = nullptr;
HWND g_crosshairHorizontalWindow = nullptr;
HWND g_crosshairVerticalWindow = nullptr;
HINSTANCE g_instance = nullptr;
DWORD g_findCursorMaxGapMs = DEFAULT_FIND_CURSOR_MAX_GAP_MS;

DWORD g_portalMinLength = DEFAULT_PORTAL_MIN_LENGTH;
DWORD g_portalMaxLength = DEFAULT_PORTAL_MAX_LENGTH;
DWORD g_portalMinThickness = DEFAULT_PORTAL_MIN_THICKNESS;
DWORD g_portalMaxThickness = DEFAULT_PORTAL_MAX_THICKNESS;
DWORD g_portalWrapDepartureColor = DEFAULT_PORTAL_WRAP_DEPARTURE_COLOR;
DWORD g_portalWrapArrivalColor = DEFAULT_PORTAL_WRAP_ARRIVAL_COLOR;
DWORD g_portalDirectDepartureColor = DEFAULT_PORTAL_DIRECT_DEPARTURE_COLOR;
DWORD g_portalDirectArrivalColor = DEFAULT_PORTAL_DIRECT_ARRIVAL_COLOR;
DWORD g_crosshairHorizontalLength = DEFAULT_CROSSHAIR_HORIZONTAL_LENGTH;
DWORD g_crosshairVerticalLength = DEFAULT_CROSSHAIR_VERTICAL_LENGTH;
DWORD g_crosshairColor = DEFAULT_CROSSHAIR_COLOR;


struct LanguageInfo
{
    std::wstring id;
    std::wstring name;
};

struct LanguageTexts
{
    std::wstring wrap;
    std::wstring shortcut;
    std::wstring options;
    std::wstring enable;
    std::wstring findCursor;
    std::wstring findCursorShortcut;
    std::wstring crosshair;
    std::wstring crosshairShortcut;
    std::wstring portalEffect;
    std::wstring startWithWindows;
    std::wstring language;
    std::wstring exit;
    std::wstring alreadyRunning;
    std::wstring windowTitle;
    std::wstring trayTooltip;
};

std::wstring g_iniPath;
std::vector<LanguageInfo> g_languages;
LanguageTexts g_texts;
std::wstring g_language = DEFAULT_LANGUAGE;


constexpr wchar_t REGISTRY_RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t REGISTRY_RUN_VALUE[] = L"WrapMouse";

bool IsStartWithWindowsEnabled()
{
    HKEY key = nullptr;
    wchar_t value[1024]{};
    DWORD type = 0;
    DWORD size = sizeof(value);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    const LONG result = RegQueryValueExW(key, REGISTRY_RUN_VALUE, nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);

    return result == ERROR_SUCCESS && type == REG_SZ && value[0] != L'\0';
}

void SetStartWithWindows(bool enabled)
{
    HKEY key = nullptr;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    if (enabled)
    {
        wchar_t exePath[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        if (length > 0 && length < MAX_PATH)
        {
            const std::wstring command = L"\"" + std::wstring(exePath, length) + L"\"";
            RegSetValueExW(
                key,
                REGISTRY_RUN_VALUE,
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))
            );
        }
    }
    else
    {
        RegDeleteValueW(key, REGISTRY_RUN_VALUE);
    }

    RegCloseKey(key);
}

std::wstring GetIniPath()
{
    wchar_t exePath[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    if (length == 0 || length >= MAX_PATH)
    {
        return L"WrapMouse.ini";
    }

    std::wstring path(exePath, length);
    const size_t separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos)
    {
        path.resize(separator + 1);
    }
    else
    {
        path.clear();
    }

    return path + L"WrapMouse.ini";
}

void WriteUtf8BomFile(const std::wstring& path, const std::wstring& content)
{
    std::ofstream file(path, std::ios::binary);

    if (!file)
    {
        return;
    }

    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.write(reinterpret_cast<const char*>(bom), sizeof(bom));

    const int size = WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);

    if (size <= 0)
    {
        return;
    }

    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), utf8.data(), size, nullptr, nullptr);
    file.write(utf8.data(), utf8.size());
}

void EnsureLanguageIni()
{
    g_iniPath = GetIniPath();

    if (GetFileAttributesW(g_iniPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        return;
    }

    const std::wstring content =
        L"; WrapMouse configuration file\r\n"
        L"; Values in this file are read by WrapMouse at startup.\r\n"
        L"\r\n"
        L"[Languages]\r\n"
        L"English=en\r\n"
        L"Français=fr\r\n"
        L"\r\n"
        L"[Settings]\r\n"
        L"; Selected interface language (en: English, fr: French).\r\n"
        L"Language=en\r\n"
        L"\r\n"
        L"[Wrap]\r\n"
        L"; Enable or disable this feature (0: disabled, 1: enabled).\r\n"
        L"Enabled=0\r\n"
        L"\r\n"
        L"[FindCursor]\r\n"
        L"; Enable or disable this feature (0: disabled, 1: enabled).\r\n"
        L"Enabled=0\r\n"
        L"; Maximum time between the two arrow key presses in milliseconds.\r\n"
        L"MaxGapMs=25\r\n"
        L"\r\n"
        L"[Crosshair]\r\n"
        L"; Enable or disable this feature (0: disabled, 1: enabled).\r\n"
        L"Enabled=0\r\n"
        L"; Horizontal line length (-1: full virtual screen width, otherwise length in pixels).\r\n"
        L"HorizontalLength=-1\r\n"
        L"; Vertical line length (-1: full virtual screen height, otherwise length in pixels).\r\n"
        L"VerticalLength=-1\r\n"
        L"; Crosshair color in hexadecimal RGB format.\r\n"
        L"Color=00FF00\r\n"
        L"\r\n"
        L"[Portal]\r\n"
        L"; Enable or disable this feature (0: disabled, 1: enabled).\r\n"
        L"Enabled=0\r\n"
        L"; Minimum Portal line length in pixels.\r\n"
        L"MinLength=40\r\n"
        L"; Maximum Portal line length in pixels.\r\n"
        L"MaxLength=120\r\n"
        L"; Minimum Portal line thickness in pixels.\r\n"
        L"MinThickness=4\r\n"
        L"; Maximum Portal line thickness in pixels.\r\n"
        L"MaxThickness=6\r\n"
        L"; Departure color when the cursor wraps around the screen, in hexadecimal RGB format.\r\n"
        L"WrapDepartureColor=1EAAFF\r\n"
        L"; Arrival color when the cursor wraps around the screen, in hexadecimal RGB format.\r\n"
        L"WrapArrivalColor=FF9619\r\n"
        L"; Departure color when naturally crossing to another monitor, in hexadecimal RGB format.\r\n"
        L"DirectDepartureColor=00E5A0\r\n"
        L"; Arrival color when naturally crossing to another monitor, in hexadecimal RGB format.\r\n"
        L"DirectArrivalColor=FFD500\r\n"
        L"\r\n"
        L"[en]\r\n"
        L"Wrap=Wrap\r\n"
        L"Shortcut=Shortcut\r\n"
        L"Options=Options\r\n"
        L"Enable=Enable\r\n"
        L"FindCursor=Enable center cursor\r\n"
        L"FindCursorShortcut=◁ + ▷\r\n"
        L"Crosshair=Enable crosshair\r\n"
        L"CrosshairShortcut=△ + ▽\r\n"
        L"PortalEffect=Portal effect\r\n"
        L"StartWithWindows=Start with Windows\r\n"
        L"Language=⚐ Language\r\n"
        L"Exit=Exit\r\n"
        L"AlreadyRunning=An instance of WrapMouse is already running.\r\n"
        L"\r\n"
        L"[fr]\r\n"
        L"Wrap=Wrap\r\n"
        L"Shortcut=Raccourcis\r\n"
        L"Options=Options\r\n"
        L"Enable=Activer\r\n"
        L"FindCursor=Activer centrer le curseur\r\n"
        L"FindCursorShortcut=◁ + ▷\r\n"
        L"Crosshair=Activer le viseur\r\n"
        L"CrosshairShortcut=△ + ▽\r\n"
        L"PortalEffect=Effet Portal\r\n"
        L"StartWithWindows=Démarrer avec Windows\r\n"
        L"Language=⚐ Langue\r\n"
        L"Exit=Quitter\r\n"
        L"AlreadyRunning=Une instance de WrapMouse est déjà en cours d'exécution.\r\n";
    WriteUtf8BomFile(g_iniPath, content);
}

std::wstring ReadUtf8IniFile()
{
    std::ifstream file(g_iniPath, std::ios::binary);

    if (!file)
    {
        return {};
    }

    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF)
    {
        data.erase(0, 3);
    }

    if (data.empty())
    {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        data.data(),
        static_cast<int>(data.size()),
        nullptr,
        0);

    if (length <= 0)
    {
        return {};
    }

    std::wstring content(length, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        data.data(),
        static_cast<int>(data.size()),
        content.data(),
        length);

    return content;
}

std::wstring TrimIniValue(const std::wstring& value)
{
    size_t first = 0;
    size_t last = value.size();

    while (first < last && (value[first] == L' ' || value[first] == L'\t'))
    {
        ++first;
    }

    while (last > first && (value[last - 1] == L' ' || value[last - 1] == L'\t'))
    {
        --last;
    }

    return value.substr(first, last - first);
}

std::vector<LanguageInfo> LoadLanguagesFromIni()
{
    std::vector<LanguageInfo> languages;
    const std::wstring content = ReadUtf8IniFile();

    if (content.empty())
    {
        return languages;
    }

    std::wstring currentSection;
    size_t position = 0;

    while (position <= content.size())
    {
        size_t end = content.find(L'\n', position);
        if (end == std::wstring::npos)
        {
            end = content.size();
        }

        std::wstring line = content.substr(position, end - position);

        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }

        line = TrimIniValue(line);

        if (!line.empty() && line.front() == L'[' && line.back() == L']')
        {
            currentSection = line.substr(1, line.size() - 2);
        }
        else if (currentSection == L"Languages" && !line.empty() && line.front() != L';')
        {
            const size_t separator = line.find(L'=');

            if (separator != std::wstring::npos)
            {
                const std::wstring name = TrimIniValue(line.substr(0, separator));
                const std::wstring id = TrimIniValue(line.substr(separator + 1));

                if (!name.empty() && !id.empty())
                {
                    languages.push_back({ id, name });
                }
            }
        }

        if (end == content.size())
        {
            break;
        }

        position = end + 1;
    }

    return languages;
}

bool HasLanguage(const std::wstring& language)
{
    for (const auto& info : g_languages)
    {
        if (info.id == language)
        {
            return true;
        }
    }

    return false;
}

std::wstring ReadIniString(const std::wstring& section, const wchar_t* key, const wchar_t* defaultValue)
{
    const std::wstring content = ReadUtf8IniFile();

    if (content.empty())
    {
        return defaultValue;
    }

    std::wstring currentSection;
    size_t position = 0;

    while (position <= content.size())
    {
        size_t end = content.find(L'\n', position);
        if (end == std::wstring::npos)
        {
            end = content.size();
        }

        std::wstring line = content.substr(position, end - position);

        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }

        line = TrimIniValue(line);

        if (!line.empty() && line.front() == L'[' && line.back() == L']')
        {
            currentSection = line.substr(1, line.size() - 2);
        }
        else if (currentSection == section && !line.empty() && line.front() != L';')
        {
            const size_t separator = line.find(L'=');

            if (separator != std::wstring::npos)
            {
                const std::wstring currentKey = TrimIniValue(line.substr(0, separator));

                if (currentKey == key)
                {
                    std::wstring value = line.substr(separator + 1);
                    const size_t comment = value.find(L';');
                    if (comment != std::wstring::npos)
                    {
                        value.resize(comment);
                    }
                    return TrimIniValue(value);
                }
            }
        }

        if (end == content.size())
        {
            break;
        }

        position = end + 1;
    }

    return defaultValue;
}

DWORD ReadIniDword(const std::wstring& section, const wchar_t* key, DWORD defaultValue)
{
    const std::wstring value = ReadIniString(section, key, L"");

    if (value.empty())
    {
        return defaultValue;
    }

    wchar_t* end = nullptr;
    const long long number = wcstoll(value.c_str(), &end, 0);

    if (end == value.c_str() || *end != L'\0' || number < 0)
    {
        if (value == L"-1")
        {
            return 0xFFFFFFFF;
        }

        return defaultValue;
    }

    if (number > 0xFFFFFFFFLL)
    {
        return defaultValue;
    }

    return static_cast<DWORD>(number);
}

DWORD ReadIniHexColor(const std::wstring& section, const wchar_t* key, DWORD defaultValue)
{
    const std::wstring value = ReadIniString(section, key, L"");

    if (value.empty())
    {
        return defaultValue;
    }

    wchar_t* end = nullptr;
    const unsigned long number = wcstoul(value.c_str(), &end, 16);

    if (end == value.c_str() || *end != L'\0' || number > 0xFFFFFFUL)
    {
        return defaultValue;
    }

    return static_cast<DWORD>(number);
}

void WriteIniValue(const std::wstring& section, const std::wstring& key, const std::wstring& value)
{
    std::wstring content = ReadUtf8IniFile();

    if (content.empty())
    {
        return;
    }

    std::vector<std::wstring> lines;
    size_t position = 0;

    while (position <= content.size())
    {
        size_t end = content.find(L'\n', position);
        if (end == std::wstring::npos)
        {
            end = content.size();
        }

        std::wstring line = content.substr(position, end - position);
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        lines.push_back(line);

        if (end == content.size())
        {
            break;
        }

        position = end + 1;
    }

    std::wstring currentSection;
    bool replaced = false;

    for (std::wstring& line : lines)
    {
        const std::wstring trimmed = TrimIniValue(line);

        if (!trimmed.empty() && trimmed.front() == L'[' && trimmed.back() == L']')
        {
            currentSection = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }

        if (currentSection != section || trimmed.empty() || trimmed.front() == L';')
        {
            continue;
        }

        const size_t separator = line.find(L'=');
        if (separator == std::wstring::npos)
        {
            continue;
        }

        if (TrimIniValue(line.substr(0, separator)) != key)
        {
            continue;
        }

        const std::wstring oldValue = line.substr(separator + 1);
        const size_t comment = oldValue.find(L';');
        const std::wstring commentPart = comment == std::wstring::npos ? L"" : oldValue.substr(comment);
        line = key + L"=" + value + (commentPart.empty() ? L"" : L" " + commentPart);
        replaced = true;
        break;
    }

    if (!replaced)
    {
        return;
    }

	std::wstring updated;

	for (size_t i = 0; i < lines.size(); ++i)
	{
		updated += lines[i];

		if (i + 1 < lines.size())
		{
			updated += L"\r\n";
		}
	}

	WriteUtf8BomFile(g_iniPath, updated);
}

void EnsureConfigurationIni()
{
    const bool settingsMissing = ReadIniString(L"Settings", L"Language", L"\x01") == L"\x01";
    const bool wrapMissing = ReadIniString(L"Wrap", L"Enabled", L"\x01") == L"\x01";

    if (settingsMissing || wrapMissing)
    {
        std::wstring content = ReadUtf8IniFile();

        content +=
            L"\r\n[Settings]\r\n"
            L"Language=en ; Selected interface language (en: English, fr: French).\r\n"
            L"\r\n[Wrap]\r\n"
            L"Enabled=0 ; Enable or disable this feature (0: disabled, 1: enabled).\r\n"
            L"\r\n[FindCursor]\r\n"
            L"Enabled=0 ; Enable or disable this feature (0: disabled, 1: enabled).\r\n"
            L"\r\n"
            L"MaxGapMs=25 ; Maximum time between the two arrow key presses, in milliseconds.\r\n"
            L"\r\n[Crosshair]\r\n"
            L"Enabled=0 ; Enable or disable this feature (0: disabled, 1: enabled).\r\n"
            L"\r\n"
            L"HorizontalLength=-1 ; -1: full virtual screen width, otherwise length in pixels.\r\n"
            L"\r\n"
            L"VerticalLength=-1 ; -1: full virtual screen height, otherwise length in pixels.\r\n"
            L"\r\n"
            L"Color=00FF00 ; Crosshair color in hexadecimal RGB format.\r\n"
            L"\r\n[Portal]\r\n"
            L"Enabled=0 ; Enable or disable this feature (0: disabled, 1: enabled).\r\n"
            L"\r\n"
            L"MinLength=40 ; Minimum Portal line length in pixels.\r\n"
            L"\r\n"
            L"MaxLength=120 ; Maximum Portal line length in pixels.\r\n"
            L"\r\n"
            L"MinThickness=4 ; Minimum Portal line thickness in pixels.\r\n"
            L"\r\n"
            L"MaxThickness=6 ; Maximum Portal line thickness in pixels.\r\n"
            L"\r\n"
            L"WrapDepartureColor=1EAAFF ; Departure color when the cursor wraps around the screen, in hexadecimal RGB format.\r\n"
            L"\r\n"
            L"WrapArrivalColor=FF9619 ; Arrival color when the cursor wraps around the screen, in hexadecimal RGB format.\r\n"
            L"\r\n"
            L"DirectDepartureColor=00E5A0 ; Departure color when naturally crossing to another monitor, in hexadecimal RGB format.\r\n"
            L"\r\n"

            L"DirectArrivalColor=FFD500 ; Arrival color when naturally crossing to another monitor, in hexadecimal RGB format.\r\n";

        WriteUtf8BomFile(g_iniPath, content);
    }
}

void LoadLanguageTexts()
{
    g_texts.wrap = ReadIniString(g_language, L"Wrap", L"Wrap");
    g_texts.shortcut = ReadIniString(g_language, L"Shortcut", L"Shortcut");
    g_texts.options = ReadIniString(g_language, L"Options", L"Options");
    g_texts.enable = ReadIniString(g_language, L"Enable", L"Enable");
    g_texts.findCursor = ReadIniString(g_language, L"FindCursor", L"Find cursor");
    g_texts.findCursorShortcut = ReadIniString(g_language, L"FindCursorShortcut", L"◁ + ▷");
    g_texts.crosshair = ReadIniString(g_language, L"Crosshair", L"Crosshair");
    g_texts.crosshairShortcut = ReadIniString(g_language, L"CrosshairShortcut", L"△ + ▽");
    g_texts.portalEffect = ReadIniString(g_language, L"PortalEffect", L"Portal effect");
    g_texts.startWithWindows = ReadIniString(g_language, L"StartWithWindows", L"Start with Windows");
    g_texts.language = ReadIniString(g_language, L"Language", L"Language");
    g_texts.exit = ReadIniString(g_language, L"Exit", L"Exit");
    g_texts.alreadyRunning = ReadIniString(g_language, L"AlreadyRunning", L"An instance of WrapMouse is already running.");
    g_texts.windowTitle = L"WrapMouse";
    g_texts.trayTooltip = L"WrapMouse";
}

void LoadLanguage()
{
    EnsureLanguageIni();
    EnsureConfigurationIni();
    g_languages = LoadLanguagesFromIni();

    if (g_languages.empty())
    {
        g_languages.push_back({ L"en", L"English" });
        g_languages.push_back({ L"fr", L"Français" });
    }

    g_language = ReadIniString(L"Settings", L"Language", DEFAULT_LANGUAGE);

    if (!HasLanguage(g_language))
    {
        g_language = g_languages.front().id;
        WriteIniValue(L"Settings", L"Language", g_language);
    }

    LoadLanguageTexts();
}

void UpdateTrayTooltip(HWND hwnd)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    lstrcpynW(nid.szTip, g_texts.trayTooltip.c_str(), ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void SetLanguage(const std::wstring& language)
{
    if (!HasLanguage(language))
    {
        return;
    }

    g_language = language;
    WriteIniValue(L"Settings", L"Language", g_language);
    LoadLanguageTexts();

    if (g_mainWindow)
    {
        UpdateTrayTooltip(g_mainWindow);
    }
}

void LoadSettings()
{
    g_enabled = ReadIniDword(L"Wrap", L"Enabled", DEFAULT_ENABLED) != 0;
    g_findCursorEnabled = ReadIniDword(L"FindCursor", L"Enabled", DEFAULT_FIND_CURSOR_ENABLED) != 0;
    g_findCursorMaxGapMs = ReadIniDword(L"FindCursor", L"MaxGapMs", DEFAULT_FIND_CURSOR_MAX_GAP_MS);
    g_portalEffectEnabled = ReadIniDword(L"Portal", L"Enabled", DEFAULT_PORTAL_EFFECT_ENABLED) != 0;
    g_portalMinLength = ReadIniDword(L"Portal", L"MinLength", DEFAULT_PORTAL_MIN_LENGTH);
    g_portalMaxLength = ReadIniDword(L"Portal", L"MaxLength", DEFAULT_PORTAL_MAX_LENGTH);
    g_portalMinThickness = ReadIniDword(L"Portal", L"MinThickness", DEFAULT_PORTAL_MIN_THICKNESS);
    g_portalMaxThickness = ReadIniDword(L"Portal", L"MaxThickness", DEFAULT_PORTAL_MAX_THICKNESS);
    g_portalWrapDepartureColor = ReadIniHexColor(L"Portal", L"WrapDepartureColor", DEFAULT_PORTAL_WRAP_DEPARTURE_COLOR);
    g_portalWrapArrivalColor = ReadIniHexColor(L"Portal", L"WrapArrivalColor", DEFAULT_PORTAL_WRAP_ARRIVAL_COLOR);
    g_portalDirectDepartureColor = ReadIniHexColor(L"Portal", L"DirectDepartureColor", DEFAULT_PORTAL_DIRECT_DEPARTURE_COLOR);
    g_portalDirectArrivalColor = ReadIniHexColor(L"Portal", L"DirectArrivalColor", DEFAULT_PORTAL_DIRECT_ARRIVAL_COLOR);
    g_crosshairFeatureEnabled = ReadIniDword(L"Crosshair", L"Enabled", 0) != 0;
    g_crosshairHorizontalLength = ReadIniDword(L"Crosshair", L"HorizontalLength", DEFAULT_CROSSHAIR_HORIZONTAL_LENGTH);
    g_crosshairVerticalLength = ReadIniDword(L"Crosshair", L"VerticalLength", DEFAULT_CROSSHAIR_VERTICAL_LENGTH);
    g_crosshairColor = ReadIniHexColor(L"Crosshair", L"Color", DEFAULT_CROSSHAIR_COLOR);

    if (g_findCursorMaxGapMs == 0)
    {
        g_findCursorMaxGapMs = DEFAULT_FIND_CURSOR_MAX_GAP_MS;
    }

    if (g_portalMinLength > g_portalMaxLength)
    {
        g_portalMinLength = g_portalMaxLength;
    }

    if (g_portalMinThickness > g_portalMaxThickness)
    {
        g_portalMinThickness = g_portalMaxThickness;
    }
}



struct PortalEffectRequest
{
    POINT point;
    bool horizontal;
    bool orange;
    bool screenChange;
};

struct PortalEffectWindow
{
    POINT point{};
    bool horizontal = false;
    bool orange = false;
    bool screenChange = false;
    DWORD startTime = 0;
};

void DrawPortalEffect(HWND hwnd, const PortalEffectWindow& effect, float progress)
{
    const int padding = 12;

    // La surface suit l'orientation du portail :
    // gauche/droite = ligne verticale, haut/bas = ligne horizontale.
    const int lineSize = static_cast<int>(g_portalMaxLength) + padding * 2;
    const int thicknessSize = static_cast<int>(g_portalMaxThickness) * 4 + padding * 2;

    const int width = std::max(32, effect.horizontal ? lineSize : thicknessSize);
    const int height = std::max(32, effect.horizontal ? thicknessSize : lineSize);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateDIBSection(memoryDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

    if (!memoryDc || !bitmap || !bits)
    {
        if (bitmap)
        {
            DeleteObject(bitmap);
        }

        if (memoryDc)
        {
            DeleteDC(memoryDc);
        }

        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }

        return;
    }

    SelectObject(memoryDc, bitmap);

    const float centerX = width * 0.5f;
    const float centerY = height * 0.5f;

    // Longueur entièrement pilotée par le Registre.
    const float lineLength =
        static_cast<float>(g_portalMinLength) +
        static_cast<float>(g_portalMaxLength - g_portalMinLength) * progress;

    const float halfLength = lineLength * 0.5f;

    // Épaisseur entièrement pilotée par le Registre.
    const float thickness =
        static_cast<float>(g_portalMinThickness) +
        static_cast<float>(g_portalMaxThickness - g_portalMinThickness) * progress;

    const float halfThickness = thickness * 0.5f;

    // Animation : apparition rapide, puis disparition.
    const float pulse = std::sin(progress * 3.14159265f);

    const DWORD color = effect.screenChange
        ? (effect.orange ? g_portalDirectArrivalColor : g_portalDirectDepartureColor)
        : (effect.orange ? g_portalWrapArrivalColor : g_portalWrapDepartureColor);

    const BYTE baseR = static_cast<BYTE>((color >> 16) & 0xFF);
    const BYTE baseG = static_cast<BYTE>((color >> 8) & 0xFF);
    const BYTE baseB = static_cast<BYTE>(color & 0xFF);

    BYTE* pixels = static_cast<BYTE*>(bits);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float dx = x - centerX;
            const float dy = y - centerY;

            // Distance par rapport à la ligne centrale.
            // Pour un passage gauche/droite, la ligne est verticale.
            // Pour un passage haut/bas, la ligne est horizontale.
            const float along = effect.horizontal ? dx : dy;
            const float across = effect.horizontal ? dy : dx;

            float lineMask = 0.0f;

            if (std::fabs(along) <= halfLength)
            {
                // Coeur de la ligne + halo léger.
                const float coreSigma = std::max(0.5f, halfThickness * 0.35f);
                const float glowSigma = std::max(1.0f, halfThickness * 1.5f);

                lineMask =
                    0.95f * std::exp(-(across * across) / (2.0f * coreSigma * coreSigma)) +
                    0.45f * std::exp(-(across * across) / (2.0f * glowSigma * glowSigma));
            }

            const float edge = std::min(1.0f, lineMask);
            const float alphaFloat = edge * pulse * 230.0f;
            const BYTE alpha = static_cast<BYTE>(std::min(255.0f, alphaFloat));

            const size_t index = (static_cast<size_t>(y) * width + x) * 4;
            pixels[index + 0] = static_cast<BYTE>(baseB * alpha / 255.0f);
            pixels[index + 1] = static_cast<BYTE>(baseG * alpha / 255.0f);
            pixels[index + 2] = static_cast<BYTE>(baseR * alpha / 255.0f);
            pixels[index + 3] = alpha;
        }
    }

    POINT topLeft{
        effect.point.x - width / 2,
        effect.point.y - height / 2
    };

    SIZE size{ width, height };
    POINT source{ 0, 0 };
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(
        hwnd,
        screenDc,
        &topLeft,
        &size,
        memoryDc,
        &source,
        0,
        &blend,
        ULW_ALPHA
    );

    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
}

LRESULT CALLBACK PortalWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PortalEffectWindow* effect = reinterpret_cast<PortalEffectWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
        case WM_NCCREATE:
        {
            const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }

        case WM_TIMER:
        {
            if (!effect)
            {
                return 0;
            }

            const DWORD elapsed = GetTickCount() - effect->startTime;

            if (elapsed >= PORTAL_EFFECT_DURATION_MS)
            {
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
                return 0;
            }

            const float progress = static_cast<float>(elapsed) / PORTAL_EFFECT_DURATION_MS;
            DrawPortalEffect(hwnd, *effect, progress);
            return 0;
        }

        case WM_NCHITTEST:
        {
            return HTTRANSPARENT;
        }

        case WM_ERASEBKGND:
        {
            return 1;
        }

        case WM_DESTROY:
        {
            if (effect)
            {
                delete effect;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }

            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowPortalEffect(const POINT& point, bool horizontal, bool orange, bool screenChange = false)
{
    if (!g_portalEffectEnabled || !g_mainWindow)
    {
        return;
    }

    PortalEffectRequest* request = new PortalEffectRequest{ point, horizontal, orange, screenChange };

    if (!PostMessageW(g_mainWindow, WM_PORTAL_EFFECT, 0, reinterpret_cast<LPARAM>(request)))
    {
        delete request;
    }
}

void ShowDirectMonitorTransition(HMONITOR previousMonitor, const POINT& previousPoint, HMONITOR currentMonitor, const POINT& currentPoint)
{
    if (!previousMonitor || !currentMonitor || previousMonitor == currentMonitor)
    {
        return;
    }

    MONITORINFO previousInfo{};
    MONITORINFO currentInfo{};
    previousInfo.cbSize = sizeof(previousInfo);
    currentInfo.cbSize = sizeof(currentInfo);

    if (!GetMonitorInfoW(previousMonitor, &previousInfo) ||
        !GetMonitorInfoW(currentMonitor, &currentInfo))
    {
        return;
    }

    const LONG dx = currentPoint.x - previousPoint.x;
    const LONG dy = currentPoint.y - previousPoint.y;
    const bool horizontal = std::abs(dx) >= std::abs(dy);

    POINT departure = previousPoint;
    POINT arrival = currentPoint;

    if (horizontal)
    {
        if (dx > 0)
        {
            departure.x = previousInfo.rcMonitor.right - 1;
            arrival.x = currentInfo.rcMonitor.left + 1;
        }
        else
        {
            departure.x = previousInfo.rcMonitor.left;
            arrival.x = currentInfo.rcMonitor.right - 1;
        }
    }
    else
    {
        if (dy > 0)
        {
            departure.y = previousInfo.rcMonitor.bottom - 1;
            arrival.y = currentInfo.rcMonitor.top + 1;
        }
        else
        {
            departure.y = previousInfo.rcMonitor.top;
            arrival.y = currentInfo.rcMonitor.bottom - 1;
        }
    }

    ShowPortalEffect(departure, !horizontal, false, true);
    ShowPortalEffect(arrival, !horizontal, true, true);
}

void CreatePortalEffectWindow(const PortalEffectRequest& request)
{
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    PortalEffectWindow* effect = new PortalEffectWindow{};
    effect->point = request.point;
    effect->horizontal = request.horizontal;
    effect->orange = request.orange;
    effect->screenChange = request.screenChange;
    effect->startTime = GetTickCount();

    const int lineSize = static_cast<int>(g_portalMaxLength) + 24;
    const int thicknessSize = static_cast<int>(g_portalMaxThickness) * 4 + 24;
    const int windowWidth = std::max(32, request.horizontal ? lineSize : thicknessSize);
    const int windowHeight = std::max(32, request.horizontal ? thicknessSize : lineSize);

    HWND hwnd = CreateWindowExW(
        exStyle,
        L"WrapMousePortalEffect",
        L"",
        WS_POPUP,
        0,
        0,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        g_instance,
        effect
    );

    if (!hwnd)
    {
        delete effect;
        return;
    }

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        0,
        0,
        windowWidth,
        windowHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );

    DrawPortalEffect(hwnd, *effect, 0.0f);
    SetTimer(hwnd, 1, PORTAL_EFFECT_TIMER_MS, nullptr);
}

bool InitializePortalEffects(HINSTANCE hInstance)
{
    g_instance = hInstance;

    WNDCLASSW wc{};
    wc.hInstance = hInstance;
    wc.lpfnWndProc = PortalWindowProc;
    wc.lpszClassName = L"WrapMousePortalEffect";

    return RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// ============================================================
// Viseur
// ============================================================

struct CrosshairLine
{
    POINT point{};
    bool horizontal = true;
};

void DrawCrosshairLine(HWND hwnd, const CrosshairLine& line)
{
    const int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    const DWORD lengthValue = line.horizontal ? g_crosshairHorizontalLength : g_crosshairVerticalLength;
    const bool maximum = lengthValue == 0xFFFFFFFF;
    const int fullLength = line.horizontal ? virtualWidth : virtualHeight;
    const int length = maximum ? fullLength : std::max(1, static_cast<int>(lengthValue));
    const int width = line.horizontal ? length : 1;
    const int height = line.horizontal ? 1 : length;

    POINT topLeft{};
    if (maximum)
    {
        topLeft.x = line.horizontal ? virtualLeft : line.point.x;
        topLeft.y = line.horizontal ? line.point.y : virtualTop;
    }
    else
    {
        topLeft.x = line.horizontal ? line.point.x - width / 2 : line.point.x;
        topLeft.y = line.horizontal ? line.point.y : line.point.y - height / 2;
    }

    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;

    if (!screenDc || !memoryDc)
    {
        if (memoryDc)
        {
            DeleteDC(memoryDc);
        }

        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }

        return;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(memoryDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

    if (!bitmap || !bits)
    {
        if (bitmap)
        {
            DeleteObject(bitmap);
        }

        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    SelectObject(memoryDc, bitmap);
    ZeroMemory(bits, static_cast<size_t>(width) * height * 4);

    const BYTE red = static_cast<BYTE>((g_crosshairColor >> 16) & 0xFF);
    const BYTE green = static_cast<BYTE>((g_crosshairColor >> 8) & 0xFF);
    const BYTE blue = static_cast<BYTE>(g_crosshairColor & 0xFF);
    const int center = maximum
        ? (line.horizontal ? line.point.x - virtualLeft : line.point.y - virtualTop)
        : length / 2;

    BYTE* pixels = static_cast<BYTE*>(bits);

    for (int position = 0; position < length; ++position)
    {
        if (position == center)
        {
            continue;
        }

        const size_t index = static_cast<size_t>(position) * 4;
        pixels[index + 0] = blue;
        pixels[index + 1] = green;
        pixels[index + 2] = red;
        pixels[index + 3] = 255;
    }

    SIZE size{ width, height };
    POINT source{ 0, 0 };
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(
        hwnd,
        nullptr,
        &topLeft,
        &size,
        memoryDc,
        &source,
        0,
        &blend,
        ULW_ALPHA);

    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
}

void UpdateCrosshairWindows();

LRESULT CALLBACK CrosshairWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CrosshairLine* line = reinterpret_cast<CrosshairLine*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
        case WM_NCCREATE:
        {
            const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }

        case WM_TIMER:
        {
            if (hwnd == g_crosshairHorizontalWindow && g_crosshairEnabled)
            {
                UpdateCrosshairWindows();
            }

            return 0;
        }

        case WM_NCHITTEST:
        {
            return HTTRANSPARENT;
        }

        case WM_MOUSEACTIVATE:
        {
            return MA_NOACTIVATE;
        }

        case WM_ERASEBKGND:
        {
            return 1;
        }

        case WM_DESTROY:
        {
            if (line)
            {
                delete line;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }

            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool InitializeCrosshair(HINSTANCE hInstance)
{
    WNDCLASSW wc{};
    wc.hInstance = hInstance;
    wc.lpfnWndProc = CrosshairWindowProc;
    wc.lpszClassName = L"WrapMouseCrosshair";

    return RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void UpdateCrosshairWindows()
{
    if (!g_crosshairEnabled)
    {
        return;
    }

    POINT p{};
    if (!GetCursorPos(&p))
    {
        return;
    }

    if (g_crosshairHorizontalWindow)
    {
        CrosshairLine* line = reinterpret_cast<CrosshairLine*>(GetWindowLongPtrW(g_crosshairHorizontalWindow, GWLP_USERDATA));
        if (line)
        {
            line->point = p;
            DrawCrosshairLine(g_crosshairHorizontalWindow, *line);
        }
    }

    if (g_crosshairVerticalWindow)
    {
        CrosshairLine* line = reinterpret_cast<CrosshairLine*>(GetWindowLongPtrW(g_crosshairVerticalWindow, GWLP_USERDATA));
        if (line)
        {
            line->point = p;
            DrawCrosshairLine(g_crosshairVerticalWindow, *line);
        }
    }

    if (g_crosshairHorizontalWindow)
    {
        SetWindowPos(g_crosshairHorizontalWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    if (g_crosshairVerticalWindow)
    {
        SetWindowPos(g_crosshairVerticalWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void SetCrosshairEnabled(bool enabled)
{
    g_crosshairEnabled = enabled;

    if (enabled)
    {
        if (g_crosshairHorizontalWindow)
        {
            SetTimer(g_crosshairHorizontalWindow, 1, 5, nullptr);
        }

        UpdateCrosshairWindows();
    }
    else
    {
        if (g_crosshairHorizontalWindow)
        {
            KillTimer(g_crosshairHorizontalWindow, 1);
            SetWindowPos(g_crosshairHorizontalWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_HIDEWINDOW);
            InvalidateRect(g_crosshairHorizontalWindow, nullptr, TRUE);
        }

        if (g_crosshairVerticalWindow)
        {
            SetWindowPos(g_crosshairVerticalWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_HIDEWINDOW);
            InvalidateRect(g_crosshairVerticalWindow, nullptr, TRUE);
        }
    }
}

void SetCrosshairFeatureEnabled(bool enabled)
{
    g_crosshairFeatureEnabled = enabled;
    WriteIniValue(L"Crosshair", L"Enabled", enabled ? L"1" : L"0");

    if (!enabled)
    {
        SetCrosshairEnabled(false);
    }
}

// ============================================================
// Moniteurs
// ============================================================

struct Monitor
{
    RECT rect;
};

std::vector<Monitor> g_monitors;

// ============================================================
// Charge l'icône depuis shell32.dll
// ============================================================

HICON LoadShell32Icon()
{
    wchar_t systemDir[MAX_PATH]{};

    UINT length = GetSystemDirectoryW(
        systemDir,
        MAX_PATH
    );

    if (length == 0 || length >= MAX_PATH)
    {
        return nullptr;
    }

    std::wstring path(systemDir);
    path += L"\\shell32.dll";

    HICON largeIcon = nullptr;
    HICON smallIcon = nullptr;

    UINT result = ExtractIconExW(
        path.c_str(),

        // Index choisi
        34,

        &largeIcon,
        &smallIcon,

        1
    );

    if (result == 0 ||
        result == static_cast<UINT>(-1))
    {
        if (largeIcon)
        {
            DestroyIcon(largeIcon);
        }

        if (smallIcon)
        {
            DestroyIcon(smallIcon);
        }

        return nullptr;
    }

    if (largeIcon)
    {
        DestroyIcon(largeIcon);
    }

    return smallIcon;
}

// ============================================================
// Enumeration des moniteurs
// ============================================================

BOOL CALLBACK EnumMonitorProc(
    HMONITOR,
    HDC,
    LPRECT rect,
    LPARAM)
{
    g_monitors.push_back({ *rect });

    return TRUE;
}

void RefreshMonitors()
{
    g_monitors.clear();

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        EnumMonitorProc,
        0
    );
}

// ============================================================
// Trouve le moniteur contenant un point
// ============================================================

const Monitor* FindMonitorAt(
    LONG x,
    LONG y)
{
    for (const auto& monitor : g_monitors)
    {
        const RECT& r = monitor.rect;

        if (x >= r.left &&
            x <  r.right &&
            y >= r.top &&
            y <  r.bottom)
        {
            return &monitor;
        }
    }

    return nullptr;
}

// ============================================================
// Retourne le moniteur Windows sous le point.
//
// Très utile pour savoir si le bord actuel est partagé
// avec un autre écran.
// ============================================================

HMONITOR WindowsMonitorAt(
    LONG x,
    LONG y)
{
    POINT p{ x, y };

    return MonitorFromPoint(
        p,
        MONITOR_DEFAULTTONULL
    );
}

// ============================================================
// Distance entre une coordonnée et une plage
//
// 0 = dans la plage.
// ============================================================

LONG DistanceToRange(
    LONG value,
    LONG minValue,
    LONG maxValue)
{
    if (value < minValue)
    {
        return minValue - value;
    }

    if (value >= maxValue)
    {
        return value - maxValue + 1;
    }

    return 0;
}

// ============================================================
// Cible à droite
//
// On privilégie les écrans couvrant Y.
//
// À défaut, on choisit l'écran dont la plage verticale
// est la plus proche.
// ============================================================

const Monitor* FindRightTarget(LONG y)
{
    const Monitor* best = nullptr;

    LONG bestDistance =
        std::numeric_limits<LONG>::max();

    LONG bestRight =
        std::numeric_limits<LONG>::min();

    for (const auto& monitor : g_monitors)
    {
        const RECT& r = monitor.rect;

        LONG distance =
            DistanceToRange(
                y,
                r.top,
                r.bottom
            );

        if (!best ||
            distance < bestDistance ||
            (distance == bestDistance &&
             r.right > bestRight))
        {
            best = &monitor;
            bestDistance = distance;
            bestRight = r.right;
        }
    }

    return best;
}

// ============================================================
// Cible à gauche
// ============================================================

const Monitor* FindLeftTarget(LONG y)
{
    const Monitor* best = nullptr;

    LONG bestDistance =
        std::numeric_limits<LONG>::max();

    LONG bestLeft =
        std::numeric_limits<LONG>::max();

    for (const auto& monitor : g_monitors)
    {
        const RECT& r = monitor.rect;

        LONG distance =
            DistanceToRange(
                y,
                r.top,
                r.bottom
            );

        if (!best ||
            distance < bestDistance ||
            (distance == bestDistance &&
             r.left < bestLeft))
        {
            best = &monitor;
            bestDistance = distance;
            bestLeft = r.left;
        }
    }

    return best;
}

// ============================================================
// Cible en bas
// ============================================================

const Monitor* FindBottomTarget(LONG x)
{
    const Monitor* best = nullptr;

    LONG bestDistance =
        std::numeric_limits<LONG>::max();

    LONG bestBottom =
        std::numeric_limits<LONG>::min();

    for (const auto& monitor : g_monitors)
    {
        const RECT& r = monitor.rect;

        LONG distance =
            DistanceToRange(
                x,
                r.left,
                r.right
            );

        if (!best ||
            distance < bestDistance ||
            (distance == bestDistance &&
             r.bottom > bestBottom))
        {
            best = &monitor;
            bestDistance = distance;
            bestBottom = r.bottom;
        }
    }

    return best;
}

// ============================================================
// Cible en haut
// ============================================================

const Monitor* FindTopTarget(LONG x)
{
    const Monitor* best = nullptr;

    LONG bestDistance =
        std::numeric_limits<LONG>::max();

    LONG bestTop =
        std::numeric_limits<LONG>::max();

    for (const auto& monitor : g_monitors)
    {
        const RECT& r = monitor.rect;

        LONG distance =
            DistanceToRange(
                x,
                r.left,
                r.right
            );

        if (!best ||
            distance < bestDistance ||
            (distance == bestDistance &&
             r.top < bestTop))
        {
            best = &monitor;
            bestDistance = distance;
            bestTop = r.top;
        }
    }

    return best;
}

void FindCursor()
{
    POINT p{};

    if (!GetCursorPos(&p))
    {
        return;
    }

    HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);

    if (!GetMonitorInfoW(monitor, &mi))
    {
        return;
    }

    const LONG x = (mi.rcWork.left + mi.rcWork.right) / 2;
    const LONG y = (mi.rcWork.top + mi.rcWork.bottom) / 2;

    SetCursorPos(x, y);
}

// ============================================================
// Boucle souris
// ============================================================

DWORD WINAPI MouseThread(LPVOID)
{
    DWORD lastRefresh = 0;
    bool previousLeft = false;
    bool previousRight = false;
    bool previousUp = false;
    bool previousDown = false;
    DWORD lastArrowTime = 0;
    int lastArrow = 0;
    DWORD lastVerticalArrowTime = 0;
    int lastVerticalArrow = 0;
    POINT lastMousePos{};
    bool haveLastMousePos = false;
    HMONITOR lastMonitor = nullptr;
    bool ignoreNextMonitorChange = false;
    POINT previousMousePos{};
    bool previousMousePosValid = false;

    while (g_running)
    {
        POINT currentMousePos{};
        if (GetCursorPos(&currentMousePos))
        {
            previousMousePos = lastMousePos;
            previousMousePosValid = haveLastMousePos;

            const bool mouseMoved =
                previousMousePosValid &&
                (currentMousePos.x != previousMousePos.x || currentMousePos.y != previousMousePos.y);

            lastMousePos = currentMousePos;
            haveLastMousePos = true;

            const bool leftDown = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
            const bool rightDown = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

            const bool leftPressed = leftDown && !previousLeft;
            const bool rightPressed = rightDown && !previousRight;

            previousLeft = leftDown;
            previousRight = rightDown;

            if (!mouseMoved)
            {
                const DWORD keyNow = GetTickCount();

                const bool upDown = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
                const bool downDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
                const bool upPressed = upDown && !previousUp;
                const bool downPressed = downDown && !previousDown;

                previousUp = upDown;
                previousDown = downDown;

                if (g_findCursorEnabled)
                {
                    if (leftPressed && rightPressed)
                    {
                        FindCursor();
                        lastArrow = 0;
                    }
                    else if (leftPressed)
                    {
                        if (lastArrow == 2 && keyNow - lastArrowTime <= g_findCursorMaxGapMs)
                        {
                            FindCursor();
                            lastArrow = 0;
                        }
                        else
                        {
                            lastArrow = 1;
                            lastArrowTime = keyNow;
                        }
                    }
                    else if (rightPressed)
                    {
                        if (lastArrow == 1 && keyNow - lastArrowTime <= g_findCursorMaxGapMs)
                        {
                            FindCursor();
                            lastArrow = 0;
                        }
                        else
                        {
                            lastArrow = 2;
                            lastArrowTime = keyNow;
                        }
                    }

                    if (lastArrow != 0 && keyNow - lastArrowTime > g_findCursorMaxGapMs)
                    {
                        lastArrow = 0;
                    }
                }
                else
                {
                    lastArrow = 0;
                }

                if (g_crosshairFeatureEnabled)
                {
                    if (upPressed && downPressed)
                    {
                        SetCrosshairEnabled(!g_crosshairEnabled.load());
                        lastVerticalArrow = 0;
                    }
                    else if (upPressed)
                    {
                        if (lastVerticalArrow == 2 && keyNow - lastVerticalArrowTime <= g_findCursorMaxGapMs)
                        {
                            SetCrosshairEnabled(!g_crosshairEnabled.load());
                            lastVerticalArrow = 0;
                        }
                        else
                        {
                            lastVerticalArrow = 1;
                            lastVerticalArrowTime = keyNow;
                        }
                    }
                    else if (downPressed)
                    {
                        if (lastVerticalArrow == 1 && keyNow - lastVerticalArrowTime <= g_findCursorMaxGapMs)
                        {
                            SetCrosshairEnabled(!g_crosshairEnabled.load());
                            lastVerticalArrow = 0;
                        }
                        else
                        {
                            lastVerticalArrow = 2;
                            lastVerticalArrowTime = keyNow;
                        }
                    }

                    if (lastVerticalArrow != 0 && keyNow - lastVerticalArrowTime > g_findCursorMaxGapMs)
                    {
                        lastVerticalArrow = 0;
                    }
                }
                else
                {
                    lastVerticalArrow = 0;
                }
            }
            else if (mouseMoved)

            {
                lastArrow = 0;
                lastVerticalArrow = 0;
            }
        }

        if (g_enabled)
        {
            // ------------------------------------------------
            // Rafraîchissement de la liste des écrans
            // ------------------------------------------------

            DWORD now = GetTickCount();

            if (g_monitors.empty() ||
                now - lastRefresh >= 1000)
            {
                RefreshMonitors();
                lastRefresh = now;
            }

            POINT p{};

            if (GetCursorPos(&p))
            {
                const HMONITOR currentMonitor =
                    MonitorFromPoint(p, MONITOR_DEFAULTTONULL);

                if (lastMonitor && currentMonitor && currentMonitor != lastMonitor)
                {
                    if (ignoreNextMonitorChange)
                    {
                        ignoreNextMonitorChange = false;
                    }
                    else if (previousMousePosValid)
                    {
                        ShowDirectMonitorTransition(lastMonitor, previousMousePos, currentMonitor, p);
                    }
                }
                else if (ignoreNextMonitorChange)
                {
                    ignoreNextMonitorChange = false;
                }

                lastMonitor = currentMonitor;

                // ========================================================
                // CAS 1 :
                // UN SEUL ÉCRAN
                //
                // On reprend EXACTEMENT la logique originale.
                // ========================================================

                if (g_monitors.size() <= 1)
                {
                    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
                    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
                    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                    const int right = left + width;
                    const int bottom = top + height;

                    auto WrapCursor = [&](const POINT& departure, const POINT& arrival, bool horizontal)
                    {
                        if (g_crosshairEnabled)
                        {
                            if (g_crosshairHorizontalWindow)
                            {
                                ShowWindow(g_crosshairHorizontalWindow, SW_HIDE);
                            }

                            if (g_crosshairVerticalWindow)
                            {
                                ShowWindow(g_crosshairVerticalWindow, SW_HIDE);
                            }
                        }

                        ShowPortalEffect(departure, horizontal, false);
                        SetCursorPos(arrival.x, arrival.y);
                        ignoreNextMonitorChange = true;
                        lastMonitor = MonitorFromPoint(arrival, MONITOR_DEFAULTTONULL);
                        lastMousePos = arrival;
                        ShowPortalEffect(arrival, horizontal, true);

                        if (g_crosshairEnabled)
                        {
                            UpdateCrosshairWindows();
                        }
                    };

                    if (p.x <= left)
                    {
                        WrapCursor({ p.x, p.y }, { right - 1, p.y }, false);
                    }
                    else if (p.x >= right - 1)
                    {
                        WrapCursor({ p.x, p.y }, { left + 1, p.y }, false);
                    }
                    else if (p.y <= top)
                    {
                        WrapCursor({ p.x, p.y }, { p.x, bottom - 1 }, true);
                    }
                    else if (p.y >= bottom - 1)
                    {
                        WrapCursor({ p.x, p.y }, { p.x, top + 1 }, true);
                    }
                }
                else
                {
                    const Monitor* current = FindMonitorAt(p.x, p.y);

                    if (current)
                    {
                        const RECT& r = current->rect;

                        auto WrapCursor = [&](const POINT& departure, const POINT& arrival, bool horizontal)
                        {
                            if (g_crosshairEnabled)
                            {
                                if (g_crosshairHorizontalWindow)
                                {
                                    ShowWindow(g_crosshairHorizontalWindow, SW_HIDE);
                                }

                                if (g_crosshairVerticalWindow)
                                {
                                    ShowWindow(g_crosshairVerticalWindow, SW_HIDE);
                                }
                            }

                            ShowPortalEffect(departure, horizontal, false);
                            SetCursorPos(arrival.x, arrival.y);
                            ignoreNextMonitorChange = true;
                            lastMonitor = MonitorFromPoint(arrival, MONITOR_DEFAULTTONULL);
                            lastMousePos = arrival;
                            ShowPortalEffect(arrival, horizontal, true);

                            if (g_crosshairEnabled)
                            {
                                UpdateCrosshairWindows();
                            }
                        };

                        if (p.x <= r.left)
                        {
                            HMONITOR neighbour = WindowsMonitorAt(r.left - 1, p.y);

                            if (!neighbour)
                            {
                                const Monitor* target = FindRightTarget(p.y);

                                if (target)
                                {
                                    LONG y = std::max(target->rect.top, std::min(p.y, target->rect.bottom - 1));
                                    WrapCursor({ p.x, p.y }, { target->rect.right - 1, y }, false);
                                }
                            }
                        }
                        else if (p.x >= r.right - 1)
                        {
                            HMONITOR neighbour = WindowsMonitorAt(r.right, p.y);

                            if (!neighbour)
                            {
                                const Monitor* target = FindLeftTarget(p.y);

                                if (target)
                                {
                                    LONG y = std::max(target->rect.top, std::min(p.y, target->rect.bottom - 1));
                                    WrapCursor({ p.x, p.y }, { target->rect.left + 1, y }, false);
                                }
                            }
                        }
                        else if (p.y <= r.top)
                        {
                            HMONITOR neighbour = WindowsMonitorAt(p.x, r.top - 1);

                            if (!neighbour)
                            {
                                const Monitor* target = FindBottomTarget(p.x);

                                if (target)
                                {
                                    LONG x = std::max(target->rect.left, std::min(p.x, target->rect.right - 1));
                                    WrapCursor({ p.x, p.y }, { x, target->rect.bottom - 1 }, true);
                                }
                            }
                        }
                        else if (p.y >= r.bottom - 1)
                        {
                            HMONITOR neighbour = WindowsMonitorAt(p.x, r.bottom);

                            if (!neighbour)
                            {
                                const Monitor* target = FindTopTarget(p.x);

                                if (target)
                                {
                                    LONG x = std::max(target->rect.left, std::min(p.x, target->rect.right - 1));
                                    WrapCursor({ p.x, p.y }, { x, target->rect.top + 1 }, true);
                                }
                            }
                        }
                    }
                }
            }
        }

        Sleep(g_crosshairEnabled ? 1 : 5);
    }

    return 0;
}

// ============================================================
// Menu Tray
// ============================================================

void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();

    if (!menu)
    {
        return;
    }

	HMENU WrapMenu = CreatePopupMenu();
	if (WrapMenu)
	{
		AppendMenuW(WrapMenu, MF_STRING | (g_enabled ? MF_CHECKED : 0), ID_ENABLE, g_texts.enable.c_str());
		
		AppendMenuW(WrapMenu, MF_SEPARATOR, 0, nullptr);
		
		if (g_enabled)
		{
			AppendMenuW(WrapMenu, MF_STRING | (g_portalEffectEnabled ? MF_CHECKED : 0), ID_PORTAL_EFFECT, g_texts.portalEffect.c_str());	
		}
		else
		{
			AppendMenuW(WrapMenu, MF_STRING | (g_portalEffectEnabled ? MF_CHECKED : 0) | MF_GRAYED, ID_PORTAL_EFFECT, g_texts.portalEffect.c_str());	
		}
		
		AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(WrapMenu), g_texts.wrap.c_str());
	}
	
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    
	HMENU ShortcutsOption = CreatePopupMenu();
	if (ShortcutsOption)
	{
		AppendMenuW(ShortcutsOption, MF_STRING | (g_findCursorEnabled ? MF_CHECKED : 0), ID_FIND_CURSOR, (g_texts.findCursor + L"\t" + g_texts.findCursorShortcut).c_str());
		AppendMenuW(ShortcutsOption, MF_STRING | (g_crosshairFeatureEnabled ? MF_CHECKED : 0), ID_CROSSHAIR, (g_texts.crosshair + L"\t" + g_texts.crosshairShortcut).c_str());
		AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(ShortcutsOption), g_texts.shortcut.c_str());
	}
	
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    
	HMENU OptionsMenu = CreatePopupMenu();
	if (OptionsMenu)
	{
		AppendMenuW(OptionsMenu, MF_STRING | (IsStartWithWindowsEnabled() ? MF_CHECKED : 0), ID_START_WITH_WINDOWS, g_texts.startWithWindows.c_str());
		AppendMenuW(OptionsMenu, MF_SEPARATOR, 0, nullptr);
		
		HMENU languageMenu = CreatePopupMenu();
		if (languageMenu)
		{
			for (size_t i = 0; i < g_languages.size(); ++i)
			{
				const UINT id = ID_LANGUAGE_BASE + static_cast<UINT>(i);
				AppendMenuW(languageMenu, MF_STRING | (g_languages[i].id == g_language ? MF_CHECKED : 0), id, g_languages[i].name.c_str());
			}

			AppendMenuW(OptionsMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(languageMenu), g_texts.language.c_str());
		}

		AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(OptionsMenu), g_texts.options.c_str());
	}

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, g_texts.exit.c_str());

    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    
    DestroyMenu(menu);
}

// ============================================================
// Fenêtre cachée
// ============================================================

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_COMMAND:
        {
            const UINT command = LOWORD(wParam);

            if (command >= ID_LANGUAGE_BASE && command < ID_LANGUAGE_BASE + g_languages.size())
            {
                SetLanguage(g_languages[command - ID_LANGUAGE_BASE].id);
                return 0;
            }

            switch (command)
            {
				case ID_ENABLE:
					g_enabled = !g_enabled.load();
					WriteIniValue(L"Wrap", L"Enabled", g_enabled ? L"1" : L"0");
					break;

                case ID_FIND_CURSOR:
                    g_findCursorEnabled = !g_findCursorEnabled.load();
                    WriteIniValue(L"FindCursor", L"Enabled", g_findCursorEnabled ? L"1" : L"0");
                    break;

                case ID_CROSSHAIR:
                    SetCrosshairFeatureEnabled(!g_crosshairFeatureEnabled.load());
                    break;

                case ID_PORTAL_EFFECT:
                    g_portalEffectEnabled = !g_portalEffectEnabled.load();
                    WriteIniValue(L"Portal", L"Enabled", g_portalEffectEnabled ? L"1" : L"0");
                    break;

                case ID_START_WITH_WINDOWS:
                    SetStartWithWindows(!IsStartWithWindowsEnabled());
                    break;

                case ID_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }

            return 0;
        }


        case WM_PORTAL_EFFECT:
        {
            PortalEffectRequest* request = reinterpret_cast<PortalEffectRequest*>(lParam);

            if (request)
            {
                CreatePortalEffectWindow(*request);
                delete request;
            }

            return 0;
        }

        case WM_TRAYICON:
        {
            if (lParam == WM_RBUTTONUP)
            {
                ShowTrayMenu(hwnd);
                return 0;
            }

            return 0;
        }

        case WM_DISPLAYCHANGE:
        {
            /*
             * Le thread souris rafraîchit sa liste toutes les secondes.
             */
            return 0;
        }

        case WM_DESTROY:
        {
            g_running = false;

            PostQuitMessage(0);

            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
// WinMain
// ============================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    LoadLanguage();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Global\\WrapMouse");

    if (!mutex)
    {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, g_texts.alreadyRunning.c_str(), g_texts.windowTitle.c_str(), MB_OK | MB_ICONWARNING);
		
        CloseHandle(mutex);
        return 0;
    }	

    LoadSettings();
    RefreshMonitors();
    timeBeginPeriod(1);

    // --------------------------------------------------------
    // Classe de fenêtre cachée
    // --------------------------------------------------------

    constexpr wchar_t CLASS_NAME[] = L"WrapMouseWindow";


    WNDCLASSW wc{};

    wc.hInstance = hInstance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassW(&wc))
    {
        return 1;
    }

    if (!InitializePortalEffects(hInstance))
    {
        return 1;
    }

    if (!InitializeCrosshair(hInstance))
    {
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, g_texts.windowTitle.c_str(), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        return 1;
    }

    g_mainWindow = hwnd;

    CrosshairLine* horizontalLine = new CrosshairLine{};
    horizontalLine->horizontal = true;

    g_crosshairHorizontalWindow = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"WrapMouseCrosshair",
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        hInstance,
        horizontalLine
    );

    CrosshairLine* verticalLine = new CrosshairLine{};
    verticalLine->horizontal = false;

    g_crosshairVerticalWindow = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"WrapMouseCrosshair",
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        hInstance,
        verticalLine
    );

    if (!g_crosshairHorizontalWindow || !g_crosshairVerticalWindow)
    {
        if (g_crosshairHorizontalWindow)
        {
            DestroyWindow(g_crosshairHorizontalWindow);
            g_crosshairHorizontalWindow = nullptr;
        }
        else
        {
            delete horizontalLine;
        }

        if (g_crosshairVerticalWindow)
        {
            DestroyWindow(g_crosshairVerticalWindow);
            g_crosshairVerticalWindow = nullptr;
        }
        else
        {
            delete verticalLine;
        }

        DestroyWindow(hwnd);
        return 1;
    }

    SetTimer(g_crosshairHorizontalWindow, 1, 5, nullptr);
    ShowWindow(g_crosshairHorizontalWindow, SW_HIDE);
    ShowWindow(g_crosshairVerticalWindow, SW_HIDE);

    // --------------------------------------------------------
    // Icône Tray
    // --------------------------------------------------------

    HICON trayIcon = LoadShell32Icon();

    // Fallback silencieux
    if (!trayIcon)
    {
        trayIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(IDI_APPLICATION));
    }

    NOTIFYICONDATAW nid{};

    nid.cbSize = sizeof(nid);

    nid.hWnd = hwnd;

    nid.uID = 1;

    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;

    nid.uCallbackMessage = WM_TRAYICON;

    nid.hIcon = trayIcon;

    lstrcpynW(nid.szTip, g_texts.trayTooltip.c_str(), ARRAYSIZE(nid.szTip));

    if (!Shell_NotifyIconW(NIM_ADD, &nid))
    {
        if (trayIcon)
        {
            DestroyIcon(trayIcon);
        }

        DestroyWindow(hwnd);
        return 1;
    }

    // --------------------------------------------------------
    // Thread souris
    // --------------------------------------------------------

    HANDLE mouseThread = CreateThread(nullptr, 0, MouseThread, nullptr, 0, nullptr);

    if (!mouseThread)
    {
        Shell_NotifyIconW(NIM_DELETE, &nid);

        if (trayIcon)
        {
            DestroyIcon(trayIcon);
        }

        DestroyWindow(hwnd);
        return 1;
    }

    // --------------------------------------------------------
    // Boucle Windows / Tray
    // --------------------------------------------------------

    MSG msg{};

    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // --------------------------------------------------------
    // Arrêt
    // --------------------------------------------------------

    g_running = false;
    timeEndPeriod(1);

    WaitForSingleObject(mouseThread, 1000);

    CloseHandle(mouseThread);

    // --------------------------------------------------------
    // Retirer l'icône du tray
    // --------------------------------------------------------

    if (g_crosshairEnabled)
    {
        SetCrosshairEnabled(false);
    }

    if (g_crosshairHorizontalWindow)
    {
        KillTimer(g_crosshairHorizontalWindow, 1);
        DestroyWindow(g_crosshairHorizontalWindow);
        g_crosshairHorizontalWindow = nullptr;
    }

    if (g_crosshairVerticalWindow)
    {
        KillTimer(g_crosshairVerticalWindow, 1);
        DestroyWindow(g_crosshairVerticalWindow);
        g_crosshairVerticalWindow = nullptr;
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);

    if (trayIcon)
    {
        DestroyIcon(trayIcon);
    }

	CloseHandle(mutex);
	return 0;
}