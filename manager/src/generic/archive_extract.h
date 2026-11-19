#pragma once
#include "common.h"

// Shared ZIP extraction for Setup payloads and admitted .s14mod packages.
namespace archive_extract {
struct Tools { fs::path tar, powershell; };

inline Tools system_tools() {
    wchar_t system[MAX_PATH]{};
    auto length = GetSystemDirectoryW(system, MAX_PATH);
    if (!length || length >= MAX_PATH) throw std::runtime_error("无法定位系统解压工具目录");
    return {fs::path(system) / L"tar.exe", fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe"};
}

inline std::wstring quote_arg(const std::wstring& text) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (auto c : text) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}

inline std::wstring ps_literal(const fs::path& path) {
    std::wstring out = L"'";
    for (auto c : fs::absolute(path).wstring()) {
        out += c;
        if (c == L'\'') out += c;
    }
    return out + L'\'';
}

inline std::wstring encoded_command(const std::wstring& script) {
    // PowerShell -EncodedCommand requires Base64 of UTF-16LE, without a BOM.
    static_assert(sizeof(wchar_t) == 2, "Windows UTF-16 required");
    static const wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto bytes = reinterpret_cast<const unsigned char*>(script.data());
    const size_t size = script.size() * sizeof(wchar_t);
    std::wstring out;
    for (size_t i = 0; i < size; i += 3) {
        unsigned value = unsigned(bytes[i]) << 16;
        if (i + 1 < size) value |= unsigned(bytes[i + 1]) << 8;
        if (i + 2 < size) value |= bytes[i + 2];
        out += alphabet[(value >> 18) & 63];
        out += alphabet[(value >> 12) & 63];
        out += i + 1 < size ? alphabet[(value >> 6) & 63] : L'=';
        out += i + 2 < size ? alphabet[value & 63] : L'=';
    }
    return out;
}

struct Result { bool launched; DWORD error, exit_code; bool timed_out; };
inline Result run(const fs::path& executable, const std::wstring& arguments,
                  const fs::path& work_dir, DWORD timeout) {
    auto command = quote_arg(executable.wstring()) + L" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, work_dir.c_str(), &startup, &process)) {
        return {false, GetLastError(), 0, false};
    }
    CloseHandle(process.hThread);
    auto waited = WaitForSingleObject(process.hProcess, timeout);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return {true, 0, code, waited != WAIT_OBJECT_0};
}

inline void extract_zip(const fs::path& archive, const fs::path& destination,
                        const fs::path& work_dir, const char* error_message,
                        DWORD timeout = 60000, const Tools& tools = system_tools()) {
    fs::create_directories(destination);
    auto result = run(tools.tar, L"-xf " + quote_arg(fs::absolute(archive).wstring()) +
                      L" -C " + quote_arg(fs::absolute(destination).wstring()), work_dir, timeout);
    const char* extractor = "tar.exe";
    if (!result.launched) {
        const auto tar_error = result.error;
        // Literal paths plus UTF-16 transport preserve Unicode and shell metacharacters.
        auto script = L"$ErrorActionPreference='Stop';try {"
                      L"Add-Type -AssemblyName System.IO.Compression.FileSystem;"
                      L"[System.IO.Compression.ZipFile]::ExtractToDirectory(" +
                      ps_literal(archive) + L"," + ps_literal(destination) +
                      L");exit 0}catch {exit 1}";
        result = run(tools.powershell, L"-NoLogo -NoProfile -NonInteractive -EncodedCommand " +
                     encoded_command(script), work_dir, timeout);
        extractor = "PowerShell/.NET";
        if (!result.launched) {
            throw std::runtime_error(std::string(error_message) +
                "：系统解压工具均无法启动（tar.exe Win32=" + std::to_string(tar_error) +
                ", PowerShell Win32=" + std::to_string(result.error) + "）");
        }
    }
    // Never retry after a started extractor fails: it may have written partial files.
    if (result.timed_out || result.exit_code != 0) {
        throw std::runtime_error(std::string(error_message) + "（" + extractor +
            (result.timed_out ? " 超时" : " exit=" + std::to_string(result.exit_code)) + "）");
    }
}
}  // namespace archive_extract
