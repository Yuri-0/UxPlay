/*
 * Portable launcher for unmodified FDH2/UxPlay.
 * Compiled with MSYS2 UCRT64: gcc -O2 -Wall -Wextra -municode ...
 * Double-click this console-subsystem EXE to start the original receiver.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>

static void fail(const wchar_t *message) {
    fwprintf(stderr, L"UxPlay portable launcher: %ls (Windows error %lu)\n",
             message, GetLastError());
    fflush(stderr);
}

static wchar_t *path_join(const wchar_t *a, const wchar_t *b) {
    size_t na = wcslen(a), nb = wcslen(b);
    wchar_t *s = (wchar_t *)malloc((na + nb + 2) * sizeof(wchar_t));
    if (!s) return NULL;
    wcscpy(s, a);
    if (na && s[na - 1] != L'\\') wcscat(s, L"\\");
    wcscat(s, b);
    return s;
}

static int set_runtime_path(const wchar_t *runtime) {
    DWORD oldlen = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t *old = (wchar_t *)calloc((size_t)oldlen + 1, sizeof(wchar_t));
    if (!old) return 0;
    if (oldlen > 1) GetEnvironmentVariableW(L"PATH", old, oldlen);
    size_t nr = wcslen(runtime), no = wcslen(old);
    wchar_t *combined = (wchar_t *)malloc((nr + no + 2) * sizeof(wchar_t));
    if (!combined) { free(old); return 0; }
    wcscpy(combined, runtime);
    wcscat(combined, L";");
    wcscat(combined, old);
    BOOL ok = SetEnvironmentVariableW(L"PATH", combined);
    free(combined); free(old);
    return ok != 0;
}

static const wchar_t *original_args(void) {
    const wchar_t *s = GetCommandLineW();
    if (*s == L'"') {
        s++;
        while (*s && *s != L'"') s++;
        if (*s == L'"') s++;
    } else {
        while (*s && !iswspace(*s)) s++;
    }
    while (iswspace(*s)) s++;
    return s;
}

int wmain(void) {
    DWORD cap = 32768;
    wchar_t *launcher = (wchar_t *)calloc(cap, sizeof(wchar_t));
    if (!launcher) return 1;
    DWORD n = GetModuleFileNameW(NULL, launcher, cap);
    if (!n || n >= cap - 1) { fail(L"cannot locate UxPlay.exe"); free(launcher); return 1; }
    wchar_t *slash = wcsrchr(launcher, L'\\');
    if (!slash) { fail(L"invalid launcher path"); free(launcher); return 1; }
    *slash = 0;

    wchar_t *runtime = path_join(launcher, L"runtime");
    wchar_t *exe = runtime ? path_join(runtime, L"uxplay.exe") : NULL;
    wchar_t *plugins = runtime ? path_join(runtime, L"plugins") : NULL;
    wchar_t *scanner = runtime ? path_join(runtime, L"gst-plugin-scanner.exe") : NULL;
    wchar_t *gio = runtime ? path_join(runtime, L"gio") : NULL;
    wchar_t *cert = runtime ? path_join(runtime, L"ca-bundle.crt") : NULL;
    wchar_t *schemas = runtime ? path_join(runtime, L"schemas") : NULL;
    if (!runtime || !exe || !plugins || !scanner || !gio || !cert || !schemas) {
        fwprintf(stderr, L"UxPlay: out of memory\n");
        return 1;
    }
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"Missing original binary: %ls\n", exe);
        return 1;
    }

    if (!set_runtime_path(runtime) ||
        !SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", plugins) ||
        !SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH_1_0", plugins) ||
        !SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER", scanner) ||
        !SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER_1_0", scanner)) {
        fail(L"cannot set portable environment");
        return 1;
    }
    if (GetFileAttributesW(gio) != INVALID_FILE_ATTRIBUTES)
        SetEnvironmentVariableW(L"GIO_EXTRA_MODULES", gio);
    if (GetFileAttributesW(cert) != INVALID_FILE_ATTRIBUTES)
        SetEnvironmentVariableW(L"SSL_CERT_FILE", cert);
    if (GetFileAttributesW(schemas) != INVALID_FILE_ATTRIBUTES)
        SetEnvironmentVariableW(L"GSETTINGS_SCHEMA_DIR", schemas);

    const wchar_t *args = original_args();
    size_t nc = wcslen(exe) + wcslen(args) + 6;
    wchar_t *cmd = (wchar_t *)calloc(nc, sizeof(wchar_t));
    if (!cmd) return 1;
    swprintf(cmd, nc, L"\"%ls\" %ls", exe, args);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, runtime, &si, &pi)) {
        fail(L"cannot start original uxplay.exe");
        free(cmd); return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    free(cmd); free(runtime); free(exe); free(plugins); free(scanner);
    free(gio); free(cert); free(schemas); free(launcher);
    return (int)exit_code;
}
