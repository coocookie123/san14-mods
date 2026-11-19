#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
// Installation-time owner migration. Keep old data; never override the new owner.
inline void migrate_government_setting(const std::filesystem::path& data_dir) {
    const auto ini = data_dir / L"settings.ini";
    if (!std::filesystem::exists(ini)) return;
    constexpr auto new_key = L"ai-iq-mod/weighted_government_auto_assign";
    wchar_t value[16]{};
    GetPrivateProfileStringW(L"manager", new_key, L"", value, 16, ini.c_str());
    if (*value) return;
    GetPrivateProfileStringW(L"manager", L"companion-mod/weighted_government_auto_assign", L"", value, 16, ini.c_str());
    const auto blob = data_dir / L"plugins/companion-mod/companion.pref.weighted_government_auto_assign";
    std::error_code ec;
    if (std::filesystem::file_size(blob, ec) == 1 && !ec) {
        std::ifstream input(blob, std::ios::binary); char byte = 0;
        if (input.get(byte)) { value[0] = byte ? L'1' : L'0'; value[1] = 0; }
    }
    if ((value[0] == L'0' || value[0] == L'1') && !value[1]) {
        if (!WritePrivateProfileStringW(L"manager", new_key, value, ini.c_str()))
            throw std::runtime_error("Cannot migrate government setting");
    }
}
