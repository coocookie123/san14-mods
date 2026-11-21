#pragma once
#include "common.h"
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace installer_game_builds {

// Standard PE64 section-invariant metadata (zero private addresses/offsets).
// Matches repackaged/unpacked/re-signed copies of the exact same compiled build.
struct PeFingerprint {
    uint32_t time_date_stamp = 0;
    std::string_view rsds_guid_hex;
    std::string_view rdata_sha256;
    std::string_view pdata_sha256;

    constexpr bool empty() const noexcept {
        return time_date_stamp == 0 || rsds_guid_hex.empty() ||
               rdata_sha256.empty() || pdata_sha256.empty();
    }
};

struct OwnedPeFingerprint {
    uint32_t time_date_stamp = 0;
    std::string rsds_guid_hex;
    std::string rdata_sha256;
    std::string pdata_sha256;

    PeFingerprint view() const noexcept {
        return {time_date_stamp, rsds_guid_hex, rdata_sha256, pdata_sha256};
    }
};

struct GameBuild {
    const wchar_t* exe_name;
    const wchar_t* label;
    uint32_t game_build;
    std::string_view sha256;
    PeFingerprint pe_fingerprint{};
};

// Executable fingerprints from the 0.2.1 installer for version identification.
// GameAdapter independently decides whether it can run on the detected game.
inline constexpr GameBuild known_builds[] = {
    {L"SAN14CE_SC.exe", L"CE 1.0.2.0", 25953386,
     "2e49348aa1b9be5cfca651e71c8d77b2e4bd2d11387edc0b0236f4352458a239",
     {0x6ad06c1bu,
      "86d6f88b33ef1a478cdb18073f401425",
      "4ee00ea719d2e88f3771c097916f9f533c6a42d106ee3cd65df2ef9bcc67f905",
      "313b07b0bae8a7bc9b149308ab268c5b0def189eb6d4b091b71782aa21b7d655"}},
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

inline const GameBuild* select_by_fingerprint(const wchar_t* exe_name, const PeFingerprint& fp) noexcept {
    if (fp.empty()) return nullptr;
    for (const auto& build : known_builds) {
        if (build.pe_fingerprint.empty()) continue;
        if (exe_name && exe_name[0] && _wcsicmp(exe_name, build.exe_name) != 0) continue;
        if (build.pe_fingerprint.time_date_stamp == fp.time_date_stamp &&
            build.pe_fingerprint.rsds_guid_hex == fp.rsds_guid_hex &&
            build.pe_fingerprint.rdata_sha256 == fp.rdata_sha256 &&
            build.pe_fingerprint.pdata_sha256 == fp.pdata_sha256) {
            return &build;
        }
    }
    return nullptr;
}

inline bool extract_pe_fingerprint(const std::vector<unsigned char>& bytes, OwnedPeFingerprint& out) noexcept {
    out = {};
    try {
        if (bytes.size() < sizeof(IMAGE_DOS_HEADER)) return false;
        IMAGE_DOS_HEADER dos{};
        std::memcpy(&dos, bytes.data(), sizeof(dos));
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))) return false;
        const uint64_t nt_off = static_cast<uint64_t>(dos.e_lfanew);
        if (nt_off > bytes.size() || sizeof(IMAGE_NT_HEADERS64) > bytes.size() - nt_off) return false;
        IMAGE_NT_HEADERS64 nt{};
        std::memcpy(&nt, bytes.data() + static_cast<size_t>(nt_off), sizeof(nt));
        if (nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
            nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96) {
            return false;
        }
        const uint64_t sec_off = nt_off + 4u + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
        const uint64_t sec_bytes = static_cast<uint64_t>(nt.FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
        if (sec_off > bytes.size() || sec_bytes > bytes.size() - sec_off) return false;

        std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
        std::memcpy(sections.data(), bytes.data() + static_cast<size_t>(sec_off), static_cast<size_t>(sec_bytes));

        out.time_date_stamp = nt.FileHeader.TimeDateStamp;
        for (const auto& sec : sections) {
            const bool is_rdata = std::memcmp(sec.Name, ".rdata\0\0", 8) == 0;
            const bool is_pdata = std::memcmp(sec.Name, ".pdata\0\0", 8) == 0;
            if (!is_rdata && !is_pdata) continue;
            if (sec.SizeOfRawData == 0 || sec.PointerToRawData > bytes.size() ||
                sec.SizeOfRawData > bytes.size() - sec.PointerToRawData) {
                return false;
            }
            const auto hash = digest(bytes.data() + sec.PointerToRawData, sec.SizeOfRawData);
            if (is_rdata && out.rdata_sha256.empty()) out.rdata_sha256 = hash;
            else if (is_pdata && out.pdata_sha256.empty()) out.pdata_sha256 = hash;
        }
        if (out.rdata_sha256.empty() || out.pdata_sha256.empty()) return false;

        auto rva_to_offset = [&](uint32_t rva, uint32_t span, size_t& raw_out) -> bool {
            for (const auto& sec : sections) {
                if (rva < sec.VirtualAddress) continue;
                const uint64_t delta = static_cast<uint64_t>(rva) - sec.VirtualAddress;
                if (delta >= sec.SizeOfRawData || span > sec.SizeOfRawData - delta) continue;
                const uint64_t raw = static_cast<uint64_t>(sec.PointerToRawData) + delta;
                if (raw > bytes.size() || span > bytes.size() - raw) return false;
                raw_out = static_cast<size_t>(raw);
                return true;
            }
            return false;
        };

        if (nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) return false;
        const auto& dbg = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        if (dbg.VirtualAddress == 0 || dbg.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) return false;
        size_t dbg_off = 0;
        if (!rva_to_offset(dbg.VirtualAddress, dbg.Size, dbg_off)) return false;

        const size_t entry_count = (std::min)(static_cast<size_t>(dbg.Size / sizeof(IMAGE_DEBUG_DIRECTORY)), size_t(64));
        static constexpr char kHex[] = "0123456789abcdef";
        for (size_t i = 0; i < entry_count; ++i) {
            IMAGE_DEBUG_DIRECTORY entry{};
            std::memcpy(&entry, bytes.data() + dbg_off + i * sizeof(IMAGE_DEBUG_DIRECTORY), sizeof(entry));
            if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.SizeOfData < 24u) continue;
            size_t cv_off = 0;
            if (entry.PointerToRawData != 0) {
                if (entry.PointerToRawData > bytes.size() || 24u > bytes.size() - entry.PointerToRawData) continue;
                cv_off = entry.PointerToRawData;
            } else if (entry.AddressOfRawData != 0) {
                if (!rva_to_offset(entry.AddressOfRawData, 24u, cv_off)) continue;
            } else {
                continue;
            }
            if (std::memcmp(bytes.data() + cv_off, "RSDS", 4) != 0) continue;
            out.rsds_guid_hex.clear();
            out.rsds_guid_hex.reserve(32);
            for (size_t b = 0; b < 16; ++b) {
                const unsigned char byte = bytes[cv_off + 4 + b];
                out.rsds_guid_hex.push_back(kHex[byte >> 4]);
                out.rsds_guid_hex.push_back(kHex[byte & 15]);
            }
            break;
        }
        return !out.view().empty();
    } catch (...) {
        out = {};
        return false;
    }
}

inline const GameBuild* select_for_image(const wchar_t* exe_name, std::string_view sha256, const std::vector<unsigned char>& bytes) noexcept {
    if (const auto* exact = select(exe_name, sha256)) return exact;
    OwnedPeFingerprint fp;
    if (extract_pe_fingerprint(bytes, fp)) {
        return select_by_fingerprint(exe_name, fp.view());
    }
    return nullptr;
}

}  // namespace installer_game_builds
