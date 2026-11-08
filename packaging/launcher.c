/* Portable launcher for unmodified FDH2/UxPlay (Windows UCRT64, console). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>

static wchar_t *join_path(const wchar_t *a, const wchar_t *b) {
    size_t len = wcslen(a) + wcslen(b) + 3;
    wchar_t *result = (wchar_t *)calloc(len, sizeof(wchar_t));
    if (result) swprintf(result, len, L"%ls\\%ls", a, b);
    return result;
}

static int prepend_path(const wchar_t *path) {
    DWORD n = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t *old = (wchar_t *)calloc((size_t)n + 1, sizeof(wchar_t));
    if (!old) return 0;
    if (n > 1) GetEnvironmentVariableW(L"PATH", old, n);
    size_t len = wcslen(path) + wcslen(old) + 3;
    wchar_t *combined = (wchar_t *)calloc(len, sizeof(wchar_t));
    if (!combined) { free(old); return 0; }
    swprintf(combined, len, L"%ls;%ls", path, old);
    BOOL ok = SetEnvironmentVariableW(L"PATH", combined);
    free(combined);
    free(old);
    return ok != 0;
}

static const wchar_t *forwarded_args(void) {
    const wchar_t *p = GetCommandLineW();
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') ++p;
        if (*p) ++p;
    } else {
        while (*p && !iswspace(*p)) ++p;
    }
    while (iswspace(*p)) ++p;
    return p;
}

typedef struct {
    HANDLE read_pipe;
    HANDLE console_output;
    HANDLE log_file;
} reader_context;

static DWORD WINAPI relay_output(LPVOID user) {
    reader_context *ctx = (reader_context *)user;
    char buf[8192];
    DWORD count;
    while (ReadFile(ctx->read_pipe, buf, sizeof(buf), &count, NULL) && count) {
        DWORD written;
        if (ctx->console_output != INVALID_HANDLE_VALUE && ctx->console_output)
            WriteFile(ctx->console_output, buf, count, &written, NULL);
        if (ctx->log_file != INVALID_HANDLE_VALUE && ctx->log_file)
            WriteFile(ctx->log_file, buf, count, &written, NULL);
    }
    return 0;
}

static void show_exit_message(DWORD code, unsigned long long seconds, const wchar_t *log_path) {
    fwprintf(stderr,
             L"\nUxPlay ended before it was ready, or stopped unexpectedly.\n"
             L"Process exit code: 0x%08lX (%lu), runtime: %llu seconds\n"
             L"Saved diagnostic log: %ls\n"
             L"Check Windows Defender Firewall, UDP 5353, and other AirPlay/mDNS services.\n",
             code, code, seconds, log_path);
    if (GetConsoleWindow()) {
        fwprintf(stderr, L"\nPress Enter to close this window...\n");
        fflush(stderr);
        /* Keep the console visible for double-click use, even if upstream exits 0. */
        (void)getwchar();
    }
}

int wmain(void) {
    wchar_t *self = (wchar_t *)calloc(32768, sizeof(wchar_t));
    if (!self) return 1;
    DWORD n = GetModuleFileNameW(NULL, self, 32768);
    if (!n || n >= 32767) { fwprintf(stderr, L"Cannot locate UxPlay.exe\n"); return 1; }
    wchar_t *sep = wcsrchr(self, L'\\');
    if (!sep) { fwprintf(stderr, L"Invalid UxPlay.exe path\n"); return 1; }
    *sep = 0;

    wchar_t *runtime = join_path(self, L"runtime");
    wchar_t *exe = runtime ? join_path(runtime, L"uxplay.exe") : NULL;
    wchar_t *plugins = runtime ? join_path(runtime, L"plugins") : NULL;
    wchar_t *scanner = runtime ? join_path(runtime, L"gst-plugin-scanner.exe") : NULL;
    wchar_t *gio = runtime ? join_path(runtime, L"gio") : NULL;
    wchar_t *cert = runtime ? join_path(runtime, L"ca-bundle.crt") : NULL;
    wchar_t *schemas = runtime ? join_path(runtime, L"schemas") : NULL;
    wchar_t *log_path = join_path(self, L"UxPlay-last-run.log");
    if (!runtime || !exe || !plugins || !scanner || !gio || !cert || !schemas || !log_path) {
        fwprintf(stderr, L"UxPlay launcher: out of memory\n"); return 1;
    }
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"Original UxPlay binary missing: %ls\n", exe);
        show_exit_message(2, 0, log_path);
        return 2;
    }
    if (!prepend_path(runtime) ||
        !SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", plugins) ||
        !SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH_1_0", plugins) ||
        !SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER", scanner) ||
        !SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER_1_0", scanner)) {
        fwprintf(stderr, L"Unable to configure portable runtime (error %lu)\n", GetLastError());
        show_exit_message(3, 0, log_path);
        return 3;
    }
    if (GetFileAttributesW(gio) != INVALID_FILE_ATTRIBUTES)
        SetEnvironmentVariableW(L"GIO_EXTRA_MODULES", gio);
    if (GetFileAttributesW(cert) != INVALID_FILE_ATTRIBUTES)
        SetEnvironmentVariableW(L"SSL_CERT_FILE", cert);
    if (GetFileAttributesW(schemas) != INVALID_FILE_ATTRIBUTES)
        SetEnvironmentVariableW(L"GSETTINGS_SCHEMA_DIR", schemas);

    const wchar_t *args = forwarded_args();
    size_t cmdlen = wcslen(exe) + wcslen(args) + 8;
    wchar_t *cmd = (wchar_t *)calloc(cmdlen, sizeof(wchar_t));
    if (!cmd) return 1;
    swprintf(cmd, cmdlen, L"\"%ls\" %ls", exe, args);

    HANDLE logfile = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (logfile == INVALID_HANDLE_VALUE)
        fwprintf(stderr, L"Cannot write %ls (error %lu); continuing without log.\n",
                 log_path, GetLastError());
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE read_pipe = NULL, write_pipe = NULL;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0) ||
        !SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        fwprintf(stderr, L"Cannot create diagnostic pipe (error %lu)\n", GetLastError());
        show_exit_message(4, 0, log_path);
        return 4;
    }

    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    ULONGLONG started = GetTickCount64();
    BOOL launched = CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0,
                                   NULL, runtime, &si, &pi);
    CloseHandle(write_pipe);
    if (!launched) {
        fwprintf(stderr, L"Cannot launch uxplay.exe (error %lu)\n", GetLastError());
        CloseHandle(read_pipe);
        if (logfile != INVALID_HANDLE_VALUE) CloseHandle(logfile);
        show_exit_message(5, 0, log_path);
        return 5;
    }

    reader_context context = { read_pipe, GetStdHandle(STD_OUTPUT_HANDLE), logfile };
    HANDLE reader = CreateThread(NULL, 0, relay_output, &context, 0, NULL);
    if (!reader) {
        /* Fallback: relay output from this thread. */
        relay_output(&context);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    ULONGLONG elapsed = GetTickCount64() - started;
    if (reader) {
        DWORD done = WaitForSingleObject(reader, 5000);
        if (done == WAIT_TIMEOUT) {
            CancelSynchronousIo(reader);
            CloseHandle(read_pipe);
            read_pipe = NULL;
            WaitForSingleObject(reader, 3000);
        }
        CloseHandle(reader);
    }
    if (read_pipe) CloseHandle(read_pipe);
    if (logfile != INVALID_HANDLE_VALUE) CloseHandle(logfile);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!*args && (code != 0 || elapsed < 12000))
        show_exit_message(code, (unsigned long long)(elapsed / 1000), log_path);
    else if (code != 0)
        fwprintf(stderr, L"UxPlay exited with code 0x%08lX. See %ls\n", code, log_path);
    free(self); free(runtime); free(exe); free(plugins); free(scanner);
    free(gio); free(cert); free(schemas); free(log_path); free(cmd);
    return (int)code;
}
