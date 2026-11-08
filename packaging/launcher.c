/* Portable launcher for unmodified FDH2/UxPlay (Windows UCRT64, console). */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
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

/* Pick the source IPv4 address of the default route without sending packets.
 * For VPN/multihomed hosts this may not match the intended LAN: see README.
 */
static int preferred_ipv4(wchar_t *output, size_t cap) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data)) return 0;
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET) { WSACleanup(); return 0; }
    struct sockaddr_in target = {0}, local = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    target.sin_addr.s_addr = htonl(0x01010101);
    int good = connect(fd, (struct sockaddr *)&target, sizeof(target)) == 0;
    int local_len = sizeof(local);
    if (good) good = getsockname(fd, (struct sockaddr *)&local, &local_len) == 0;
    if (good) {
        unsigned char *bytes = (unsigned char *)&local.sin_addr.s_addr;
        if (bytes[0] == 0 || bytes[0] == 127 || bytes[0] >= 224) good = 0;
        if (good) swprintf(output, cap, L"%u.%u.%u.%u",
                           bytes[0], bytes[1], bytes[2], bytes[3]);
    }
    closesocket(fd);
    WSACleanup();
    return good;
}

typedef struct {
    PROCESS_INFORMATION process;
    HANDLE job;
    int started;
} ble_process;

static void start_ble(const wchar_t *ble_exe, const wchar_t *ble_file,
                      const wchar_t *ble_log, ble_process *ble) {
    wchar_t ip[64] = {0};
    if (!preferred_ipv4(ip, 64)) {
        fwprintf(stderr, L"BLE discovery unavailable: cannot select the local IPv4 address. mDNS remains active.\n");
        return;
    }
    size_t nc = wcslen(ble_exe) + wcslen(ble_file) + wcslen(ip) + 80;
    wchar_t *cmd = (wchar_t *)calloc(nc, sizeof(wchar_t));
    if (!cmd) return;
    swprintf(cmd, nc, L"\"%ls\" --path \"%ls\" --ipv4 %ls", ble_exe, ble_file, ip);

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hlog = CreateFileW(ble_log, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hlog == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"BLE discovery: cannot create log (error %lu). mDNS remains active.\n", GetLastError());
        free(cmd);
        return;
    }
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hlog;
    si.hStdError = hlog;
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
    BOOL ok = CreateProcessW(ble_exe, cmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &ble->process);
    CloseHandle(hlog);
    free(cmd);
    if (!ok) {
        fwprintf(stderr, L"BLE discovery: could not launch helper (error %lu). mDNS remains active.\n", GetLastError());
        return;
    }
    ble->started = 1;
    ble->job = CreateJobObjectW(NULL, NULL);
    if (ble->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit = {0};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(ble->job, JobObjectExtendedLimitInformation,
                                     &limit, sizeof(limit)) ||
            !AssignProcessToJobObject(ble->job, ble->process.hProcess)) {
            CloseHandle(ble->job);
            ble->job = NULL;
        }
    }
    fwprintf(stdout, L"AirPlay discovery: LAN/mDNS + Bluetooth LE (advertising %ls)\n", ip);
    fwprintf(stdout, L"Bluetooth status/log: %ls\n", ble_log);
    fflush(stdout);
}

static void stop_ble(ble_process *ble) {
    if (!ble->started) return;
    if (ble->job) {
        CloseHandle(ble->job);
        ble->job = NULL;
    } else {
        TerminateProcess(ble->process.hProcess, 0);
    }
    WaitForSingleObject(ble->process.hProcess, 3000);
    CloseHandle(ble->process.hThread);
    CloseHandle(ble->process.hProcess);
    ble->started = 0;
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
    wchar_t *ble_exe = join_path(self, L"ble\\UxPlayBLEBeacon.exe");
    wchar_t *ble_file = join_path(self, L"UxPlay-beacon.dat");
    wchar_t *ble_log = join_path(self, L"UxPlay-BLE.log");
    if (!runtime || !exe || !plugins || !scanner || !gio || !cert || !schemas || !log_path || !ble_exe || !ble_file || !ble_log) {
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
    /* In double-click mode enable the upstream -ble flag, while mDNS stays on.
       Command-line use is passed through unchanged (for -h and debugging). */
    int ble_enabled = (!*args && GetFileAttributesW(ble_exe) != INVALID_FILE_ATTRIBUTES);
    size_t cmdlen = wcslen(exe) + wcslen(args) + (ble_enabled ? wcslen(ble_file) : 0) + 48;
    wchar_t *cmd = (wchar_t *)calloc(cmdlen, sizeof(wchar_t));
    if (!cmd) return 1;
    if (ble_enabled) swprintf(cmd, cmdlen, L"\"%ls\" -ble \"%ls\"", exe, ble_file);
    else swprintf(cmd, cmdlen, L"\"%ls\" %ls", exe, args);

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

    ble_process ble = {0};
    if (ble_enabled) {
        start_ble(ble_exe, ble_file, ble_log, &ble);
    } else if (!*args) {
        fwprintf(stderr, L"Bluetooth helper not found. AirPlay discovery remains available over LAN/mDNS.\n");
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
    stop_ble(&ble);
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
    free(ble_exe); free(ble_file); free(ble_log);
    return (int)code;
}
