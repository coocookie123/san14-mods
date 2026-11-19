#pragma once
#include <cstdint>
#include <cwchar>
#include <string_view>

namespace installer_game_builds {

struct GameBuild {
    const wchar_t* exe_name;
    const wchar_t* label;
    uint32_t game_build;
    std::string_view sha256;
};

// Executable fingerprints from the 0.2.1 installer for version identification.
// GameAdapter independently decides whether it can run on the detected game.
inline constexpr GameBuild known_builds[] = {
    {L"SAN14CE_SC.exe", L"CE 1.0.2.0", 25953386,
     "2e49348aa1b9be5cfca651e71c8d77b2e4bd2d11387edc0b0236f4352458a239"},
    {L"SAN14CE_SC.exe", L"CE 1.0.0.0", 25953384,
     "2cae6d6acb9ff362caa19162a6158afdb73e1a3e957a6347ed52ad902eb8aa3b"},
    {L"SAN14CE_TC.exe", L"CE 1.0.2.0", 25953387,
     "e4be0f6c0ea3feec02f05e725a7a6be84d70cd1cc0bb96931a3656fbac125810"},
    {L"SAN14CE_TC.exe", L"CE 1.0.0.0", 25953385,
     "3c5e772bb4b2524d855971f4820656ec16da42f048247d9b0b9296d7964760fe"},
    {L"SAN14PK_SC.exe", L"PK 1.0.11.0", 10110001,
     "42d53bb42c033c6027b6da75e8077f4170f4d684abb0f57483a661225d052025"},
    {L"SAN14PK_TC.exe", L"PK 1.0.11.0", 10110002,
     "c3435f55b3ece9e5fe720a846d4e5a557e699aeb7b534d64e2ff384fa8eeca35"},
    {L"SAN14_SC.exe", L"Base 1.0.26.0", 10260001,
     "71194f07ac24feb5a1338a12994087100cbbee587dcfe490b20fc5dba6e2ac9e"},
    {L"SAN14_TC.exe", L"Base 1.0.26.0", 10260002,
     "05d9f9e0a94fd51ae5dfb93470a7ad0ec1b8ea5abc331d8a15df2165d08b51a5"},
};

inline const GameBuild* select(const wchar_t* exe_name, std::string_view sha256) noexcept {
    for (const auto& build : known_builds) {
        if (_wcsicmp(exe_name, build.exe_name) == 0 && sha256 == build.sha256) return &build;
    }
    return nullptr;
}

}  // namespace installer_game_builds
