// Standalone Windows handoff; no Qt or application-local DLLs are loaded here.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <bcrypt.h>
#include <cstdlib>
#include <cwchar>
#include <shellapi.h>
#include <string>
#include <vector>
#include <windows.h>
static std::wstring quote(const std::wstring &value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out += c;
            slashes = 0;
        } else {
            out.append(slashes, L'\\');
            slashes = 0;
            out += c;
        }
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}
static bool launch(const std::wstring &exe, std::wstring args, bool wait, DWORD *code) {
    std::wstring command = quote(exe) + L" " + args;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &child))
        return false;
    CloseHandle(child.hThread);
    if (wait) {
        for (;;) {
            const DWORD state =
                MsgWaitForMultipleObjects(1, &child.hProcess, FALSE, INFINITE, QS_ALLINPUT);
            if (state == WAIT_OBJECT_0)
                break;
            if (state == WAIT_OBJECT_0 + 1) {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            } else {
                WaitForSingleObject(child.hProcess, INFINITE);
                break;
            }
        }
        GetExitCodeProcess(child.hProcess, code);
    }
    CloseHandle(child.hProcess);
    return true;
}
static HWND progressWindow(bool english) {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = DefWindowProcW;
    type.hInstance = instance;
    type.lpszClassName = L"AirletUpdateProgress";
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, type.lpszClassName, L"Airlet PDF", WS_CAPTION | WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN) - 460) / 2, (GetSystemMetrics(SM_CYSCREEN) - 150) / 2, 460,
        150, nullptr, nullptr, instance, nullptr);
    if (window) {
        HWND label = CreateWindowExW(
            0, L"STATIC",
            english ? L"Installing update. Please wait...\nAirlet PDF will reopen automatically."
                    : L"Instaluji aktualizaci. Vyčkejte prosím...\nAirlet PDF se znovu otevře.",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 15, 30, 425, 65, window, nullptr, instance, nullptr);
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                     TRUE);
        UpdateWindow(window);
        SetForegroundWindow(window);
    }
    return window;
}
static bool verify(HANDLE file, const std::wstring &expected) {
    if (expected.size() != 64)
        return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 512LL * 1024 * 1024)
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD needed = 0, length = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length),
                                sizeof(length), &needed, 0) >= 0;
    std::vector<UCHAR> object(length), bytes(65536);
    UCHAR digest[32]{};
    if (ok)
        ok = BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) >= 0;
    DWORD read = 0;
    while (ok) {
        if (!ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr)) {
            ok = false;
            break;
        }
        if (!read)
            break;
        ok = BCryptHashData(hash, bytes.data(), read, 0) >= 0;
    }
    if (ok)
        ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::wstring actual;
    const wchar_t *hex = L"0123456789abcdef";
    for (UCHAR byte : digest) {
        actual += hex[byte >> 4];
        actual += hex[byte & 15];
    }
    return ok && actual == expected;
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv && count == 4 && std::wstring(argv[1]) == L"--verify") {
        HANDLE file = CreateFileW(argv[2], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool ok = file != INVALID_HANDLE_VALUE && verify(file, argv[3]);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        LocalFree(argv);
        return ok ? 0 : 1;
    }
    if (!argv || count != 7) {
        if (argv)
            LocalFree(argv);
        return 1;
    }
    const DWORD parent = wcstoul(argv[1], nullptr, 10);
    const std::wstring installer = argv[2], app = argv[3], sha = argv[4], language = argv[5],
                       document = argv[6];
    LocalFree(argv);
    const bool english = language == L"en";
    auto message = [&](const wchar_t *cs, const wchar_t *en) {
        MessageBoxW(nullptr, english ? en : cs, L"Airlet PDF", MB_OK | MB_ICONINFORMATION);
    };
    const auto split = app.find_last_of(L"\\/");
    if (split == std::wstring::npos ||
        _wcsicmp(app.substr(split + 1).c_str(), L"AirletPDF.exe") != 0)
        return 1;
    struct Cleanup {
        std::wstring installer;
        ~Cleanup() {
            DeleteFileW(installer.c_str());
            const auto folder = installer.substr(0, installer.find_last_of(L"\\/"));
            HANDLE marker = CreateFileW((folder + L"\\completed").c_str(), GENERIC_WRITE, 0,
                                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (marker != INVALID_HANDLE_VALUE)
                CloseHandle(marker);
        }
    } cleanup{installer};
    HANDLE source = OpenProcess(SYNCHRONIZE, FALSE, parent);
    if (source) {
        const auto result = WaitForSingleObject(source, 120000);
        CloseHandle(source);
        if (result != WAIT_OBJECT_0) {
            message(L"Airlet PDF se nepodařilo ukončit. Aktualizace nebyla spuštěna.",
                    L"Airlet PDF did not close. The update was not started.");
            return 1;
        }
    } else if (GetLastError() != ERROR_INVALID_PARAMETER)
        return 1;
    // Deny writes/deletion while validating AND executing this exact installer.
    HANDLE file = CreateFileW(installer.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE || !verify(file, sha)) {
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        message(L"Instalátor neprošel kontrolou. Aktualizace nebyla spuštěna.",
                L"Installer verification failed. The update was not started.");
        DWORD ignored = 0;
        launch(app, document.empty() ? L"" : quote(document), false, &ignored);
        return 1;
    }
    HWND progress = progressWindow(english);
    DWORD code = 1;
    const bool started = launch(
        installer,
        L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /RESTARTEXITCODE=3010 /SP- /CLOSEAPPLICATIONS "
        L"/NORESTARTAPPLICATIONS /DIR=" +
            quote(app.substr(0, split)),
        true, &code);
    CloseHandle(file);
    if (progress)
        DestroyWindow(progress);
    if (!started || (code != 0 && code != 3010))
        message(L"Aktualizace se nezdařila. Aplikace se znovu otevře.",
                L"The update failed. The application will reopen.");
    if (code == 3010)
        message(L"Aktualizace vyžaduje restart Windows. Poté spusťte Airlet PDF.",
                L"The update requires a Windows restart. Start Airlet PDF afterwards.");
    else {
        DWORD ignored = 0;
        if (!launch(app, document.empty() ? L"" : quote(document), false, &ignored))
            message(L"Spusťte prosím Airlet PDF znovu ze zástupce.",
                    L"Please start Airlet PDF again from its shortcut.");
    }
    return started && code == 0 ? 0 : 1;
}
