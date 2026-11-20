#include "common.h"
#include "government_setting_migration.h"
#include "archive_extract.h"
#include "wire_json.h"
#include "installer_game_builds.h"
#include "installer_version.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <functional>
#include <shellapi.h>

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr const wchar_t* kSupportedExeNames[] = {
    L"SAN14CE_SC.exe",
    L"SAN14CE_TC.exe",
    L"SAN14PK_SC.exe",
    L"SAN14PK_TC.exe",
    L"SAN14_SC.exe",
    L"SAN14_TC.exe",
};
constexpr char kPayloadMagic[8] = {'S', '1', '4', 'P', 'K', 'G', '0', '1'};
constexpr wchar_t kSetupTitle[] = L"三国志14 模组管理器 安装程序 v" SAN14_INSTALLER_VERSION L"（测试版）";
constexpr wchar_t kTwoCityBundle[] = L"SAN14TwoCity";
constexpr char kTwoCityModDir[] = "SAN14Mods/two-city-all-stars-mod/";
constexpr const char* kCompanionKeys[] = {
    "disable_tribal_officer_gifts", "disable_foreign_trade", "disable_tribal_troop_replenish",
    "editable_scenario_settings", "fine_expert_mode", "weighted_government_auto_assign",
    "preserve_captive_post_and_return", "allow_away_post_assignment", "shared_domestic_duty_slots",
    "allow_subordinate_command_officers", "allow_subordinate_base_operations",
    "allow_person_group_district_officers", "weighted_office_auto_assign", "raise_officer_merit_cap",
    "block_ai_district_split", "disable_elder_portraits",
    "skip_turn_reports", "defer_disaster_reports", "hide_war_book_discovery_message",
    "global_death_notices", "ultimate_manual_tactics",
};

#pragma pack(push, 1)
struct PayloadFooter {
    char magic[8];
    uint64_t offset;
    uint64_t size;
    unsigned char sha256[32];
};
#pragma pack(pop)
static_assert(sizeof(PayloadFooter) == 56, "Unexpected footer size");

// Win32 backup paths may exceed MAX_PATH once a license path is appended.
// Keep the extended path syntax out of the serialized installation record.
fs::path extended_path(const fs::path& path) {
    auto absolute = fs::absolute(path).lexically_normal().make_preferred().native();
    if (absolute.rfind(L"\\\\?\\", 0) == 0) return absolute;
    if (absolute.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + absolute.substr(2);
    return L"\\\\?\\" + absolute;
}

struct ScopedTempDir {
    fs::path path;
    ~ScopedTempDir() {
        if (!path.empty()) {
            std::error_code ec;
            fs::remove_all(extended_path(path), ec);
        }
    }
};

std::string to_hex(const unsigned char* bytes, size_t n) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(hex[bytes[i] >> 4]);
        out.push_back(hex[bytes[i] & 15]);
    }
    return out;
}

void write_bytes_atomic(const fs::path& p, const std::vector<unsigned char>& bytes) {
    write_atomic(p, bytes.data(), bytes.size());
}

void plain_path(const fs::path& p) {
    auto abs = fs::absolute(p);
    fs::path cur;
    for (const auto& part : abs) {
        cur /= part;
        const auto path_to_check = cur.native().size() >= 240 ? extended_path(cur) : cur;
        DWORD attrs = GetFileAttributesW(path_to_check.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            throw std::runtime_error("路径包含符号链接/重解析点，已拒绝: " + utf8(cur.wstring()));
        }
    }
}

// Callers validate the relative path with owned_target before backing it up.
fs::path backup_file(const fs::path& root, const std::string& relative) {
    const auto path = root / wide(relative);
    plain_path(path);
    return extended_path(path);
}

fs::path owned_target(const fs::path& game, const std::string& rel) {
    if (rel.empty() || rel.find(':') != std::string::npos || rel.find('\\') != std::string::npos) {
        throw std::runtime_error("非法的安装相对路径: " + rel);
    }
    std::istringstream ss(rel);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty() || part == "." || part == "..") {
            throw std::runtime_error("非法的路径段: " + rel);
        }
    }
    if (!(rel == "dinput8.dll" || rel.rfind("SAN14Mods/", 0) == 0 || rel.rfind("SAN14Base/", 0) == 0)) {
        throw std::runtime_error("受管文件越界: " + rel);
    }
    fs::path target = game / wide(rel);
    plain_path(target.parent_path());
    if (fs::exists(target)) plain_path(target);
    fs::path canon_game = fs::weakly_canonical(game);
    fs::path canon_target = fs::weakly_canonical(target);
    if (canon_target == canon_game || !inside(canon_game, canon_target)) {
        throw std::runtime_error("目标路径逃逸出游戏目录: " + rel);
    }
    return target;
}

bool game_running() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("无法枚举系统进程");
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    BOOL ok = Process32FirstW(snap, &entry);
    bool found = false;
    while (ok) {
        for (const wchar_t* exe_name : kSupportedExeNames) {
            if (_wcsicmp(entry.szExeFile, exe_name) == 0) {
                found = true;
                break;
            }
        }
        if (found) break;
        ok = Process32NextW(snap, &entry);
    }
    CloseHandle(snap);
    return found;
}

fs::path extract_embedded_payload(ScopedTempDir& temp_guard) {
    fs::path self = module_path(nullptr);
    std::ifstream in(self, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("无法读取安装程序自身文件");
    uint64_t total_size = static_cast<uint64_t>(in.tellg());
    if (total_size < sizeof(PayloadFooter)) {
        throw std::runtime_error("安装包内缺少嵌入的管理器负载");
    }
    PayloadFooter footer{};
    in.seekg(static_cast<std::streamoff>(total_size - sizeof(PayloadFooter)));
    if (!in.read(reinterpret_cast<char*>(&footer), sizeof(footer))) {
        throw std::runtime_error("无法读取安装包校验尾");
    }
    if (std::memcmp(footer.magic, kPayloadMagic, sizeof(kPayloadMagic)) != 0 ||
        footer.size == 0 || footer.size > 512ull * 1024 * 1024 ||
        footer.offset > total_size || footer.size > total_size - footer.offset ||
        footer.offset + footer.size + sizeof(PayloadFooter) != total_size) {
        throw std::runtime_error("安装程序未包含有效的管理器数据包");
    }
    std::vector<unsigned char> payload(static_cast<size_t>(footer.size));
    in.seekg(static_cast<std::streamoff>(footer.offset));
    if (!in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(footer.size))) {
        throw std::runtime_error("读取内置管理器数据包失败");
    }
    std::string actual_sha = digest(payload.data(), payload.size());
    std::string expected_sha = to_hex(footer.sha256, sizeof(footer.sha256));
    if (actual_sha != expected_sha) {
        throw std::runtime_error("内置数据包 SHA-256 校验失败，安装文件可能已损坏");
    }

    fs::path temp_root = fs::temp_directory_path() /
        (L"san14-setup-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(temp_root);
    temp_guard.path = temp_root;

    fs::path archive = temp_root / L"payload.zip";
    fs::path extracted = temp_root / L"package";
    write_bytes_atomic(archive, payload);
    archive_extract::extract_zip(archive, extracted, temp_root, "解压内置管理器数据包失败");

    fs::path mod_packages = extracted / L"SAN14ModPackages";
    if (fs::is_directory(mod_packages)) {
        for (const auto& dir : fs::directory_iterator(mod_packages)) {
            if (!dir.is_directory()) continue;
            std::wstring mod_name = dir.path().filename().wstring();
            for (const auto& item : fs::directory_iterator(dir.path())) {
                if (item.is_regular_file() && item.path().extension() == L".s14mod") {
                    archive_extract::extract_zip(item.path(), extracted / L"SAN14Mods" / mod_name, temp_root, "解压内置模组包失败");
                }
            }
        }
    }
    return extracted;
}

std::string timestamp_tag() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::ostringstream os;
    os << std::setfill('0')
       << std::setw(4) << st.wYear
       << std::setw(2) << st.wMonth
       << std::setw(2) << st.wDay << "-"
       << std::setw(2) << st.wHour
       << std::setw(2) << st.wMinute
       << std::setw(2) << st.wSecond << "-"
       << std::setw(3) << st.wMilliseconds;
    return os.str();
}

void import_companion_settings(const fs::path& data_dir) {
    fs::path target = data_dir / L"settings.ini";
    if (fs::exists(target)) { migrate_government_setting(data_dir); return; }
    wchar_t appdata[MAX_PATH]{};
    WireJson old = WireJson::object();
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata))) {
        fs::path src = fs::path(appdata) / L"SAN14-Companion/settings.json";
        if (fs::exists(src)) {
            try {
                auto bytes = read_file(src, 1024 * 1024);
                size_t offset = (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
                old = WireJson::parse(std::string(bytes.begin() + offset, bytes.end()));
            } catch (...) {
                old = WireJson::object();
            }
        }
    }
    std::wstring text = L"[manager]\r\ncompanion-mod/enabled=1\r\n";
    for (const char* key : kCompanionKeys) {
        bool enabled = old.is_object() && old.contains(key) && old[key].is_boolean() && old[key].get<bool>();
        text += L"companion-mod/" + wide(key) + L"=" + (enabled ? L"1" : L"0") + L"\r\n";
    }
    std::vector<unsigned char> utf16_bytes;
    utf16_bytes.reserve(2 + text.size() * 2);
    utf16_bytes.push_back(0xFF);
    utf16_bytes.push_back(0xFE);
    for (wchar_t ch : text) {
        utf16_bytes.push_back(static_cast<unsigned char>(ch & 0xFF));
        utf16_bytes.push_back(static_cast<unsigned char>((ch >> 8) & 0xFF));
    }
    write_bytes_atomic(target, utf16_bytes);
    migrate_government_setting(data_dir);
}

struct DetectedGame {
    fs::path exe_path;
    const installer_game_builds::GameBuild* profile = nullptr;
    std::string sha256;
    bool found_candidate = false;
};

struct OperationCancelled : std::runtime_error { using std::runtime_error::runtime_error; };

struct InstallPolicy {
    bool allow_unknown_version = false;
    bool repair_changed_files = false;
    bool reinstall_missing_game = false;
    std::function<bool(const std::wstring&, const std::wstring&)> confirm;
    void require(bool allowed, const std::wstring& title, const std::wstring& message) const {
        if (allowed || (confirm && confirm(title, message))) return;
        throw OperationCancelled(utf8(message) + "\n操作已取消，未修改安装文件。");
    }
};

// A disconnected drive or an unreadable directory is not evidence of deletion.
void require_missing_game_directory(const fs::path& old_game) {
    if (!old_game.is_absolute() || !fs::is_directory(old_game.root_path())) {
        throw std::runtime_error("无法访问旧游戏所在磁盘，请连接磁盘并核对旧目录后重试。");
    }
    if (fs::exists(old_game)) {
        throw std::runtime_error("已有安装记录指向其他游戏目录: " + utf8(old_game.wstring()) +
            "\n旧目录仍存在，请选择原目录更新，或先恢复／卸载原安装。");
    }
}

void confirm_changed_files(const InstallPolicy& policy, const std::vector<std::string>& changed, bool restoring) {
    if (changed.empty()) return;
    std::wstring message = L"检测到已安装文件被修改或缺失：\n";
    for (size_t i = 0; i < (std::min)(changed.size(), size_t(8)); ++i) message += wide(changed[i]) + L"\n";
    if (changed.size() > 8) message += L"……共 " + std::to_wstring(changed.size()) + L" 项\n";
    message += restoring
        ? L"\n继续将先备份当前文件，再恢复安装前文件并卸载管理器。现有设置和存档保留。\n是否继续？"
        : L"\n继续将先备份当前文件，再修复／更新管理器。现有设置和存档保留。\n是否继续？";
    policy.require(policy.repair_changed_files, L"备份并继续", message);
}

DetectedGame detect_supported_game(const fs::path& game_dir) {
    DetectedGame first_mismatch;
    std::error_code ec;
    for (const wchar_t* exe_name : kSupportedExeNames) {
        fs::path exe_path = game_dir / exe_name;
        if (!fs::is_regular_file(exe_path, ec)) continue;
        std::string sha = hash_file(exe_path);
        const auto* profile = installer_game_builds::select(exe_name, sha);
        if (profile != nullptr) {
            return {exe_path, profile, sha, true};
        }
        if (!first_mismatch.found_candidate) {
            first_mismatch = {exe_path, nullptr, sha, true};
        }
    }
    return first_mismatch;
}

bool stage_two_city(const fs::path& game, const fs::path& package,
                    const installer_game_builds::GameBuild* profile) {
    const fs::path bundle = package / kTwoCityBundle;
    if (!fs::exists(bundle) || !profile || profile->game_build != 25953386) return false;

    const fs::path base = game / L"0010_SC/scedaexce01.s14";
    plain_path(base);
    if (!fs::is_regular_file(base) ||
        hash_file(base) != "79f11f0ccefc2bc4752163941d5fd7b9b7ccf9cf8e952aa00f226c7c4030c93c") {
        throw std::runtime_error("未找到与双城全明星对战匹配的原版剧本01。请通过 Steam 验证游戏文件完整性后重试。");
    }

    const fs::path mod = package / wide(kTwoCityModDir);
    const fs::path compiled = L"__san14_content__/definition.json";
    const fs::path definition = bundle / compiled;
    const auto bytes = read_file(definition, 2 * 1024 * 1024);
    const auto metadata = WireJson::parse(bytes.begin(), bytes.end());
    const auto& native = metadata.at("scenario").at("native_file");
    const std::string sha = native.at("sha256").get<std::string>();
    if (sha.size() != 64 || !std::all_of(sha.begin(), sha.end(), [](char digit) {
            return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
        }) || native.at("path") != "__san14_content__/" + sha + ".s14") {
        throw std::runtime_error("双城全明星对战的剧本清单无效。");
    }
    const fs::path output = bundle / L"generated.s14";
    const fs::path author = bundle / L"runtime/author.exe";
    using archive_extract::quote_arg;
    const auto result = archive_extract::run(author, quote_arg(base.wstring()) + L" " +
        quote_arg(definition.wstring()) + L" -o " + quote_arg(output.wstring()), bundle, 60000);
    if (!result.launched || result.timed_out || result.exit_code != 0 ||
        !fs::is_regular_file(output) || hash_file(output) != sha) {
        throw std::runtime_error("生成双城全明星剧本失败。请验证游戏文件并重新运行安装程序。");
    }
    fs::create_directories((mod / compiled).parent_path());
    fs::copy_file(bundle / L"mod.json", mod / L"mod.json");
    fs::copy_file(bundle / L"README.md", mod / L"README.md");
    fs::copy_file(definition, mod / compiled);
    fs::rename(output, mod / L"__san14_content__" / wide(sha + ".s14"));
    return true;
}

std::string install_from_package(const fs::path& game_input, const fs::path& package_dir, const fs::path& data_dir, bool check_running = true, const InstallPolicy& policy = {}, fs::path* backup_output = nullptr) {
    if (!fs::exists(game_input) || !fs::is_directory(game_input)) {
        throw std::runtime_error("所选游戏目录不存在");
    }
    fs::path game = fs::canonical(game_input);
    DetectedGame detected = detect_supported_game(game);
    if (!detected.found_candidate) {
        throw std::runtime_error("目录中未找到受支持的三国志14主程序 (CE/PK/原版 简繁，暂不支持曲海金戈)");
    }
    if (check_running && game_running()) {
        throw std::runtime_error("检测到三国志14正在运行，请先正常退出游戏再安装。");
    }
    if (detected.profile == nullptr) {
        policy.require(policy.allow_unknown_version, L"版本兼容性提示",
            L"无法识别目前的版本，可能不兼容。\n检测到文件：" + detected.exe_path.filename().wstring() +
            L"\n\n主程序被修改或游戏版本不同，都可能出现此提示；这不代表游戏不是正版。"
            L"\n继续仅表示安装管理器，启动后仍需通过适配检查；不通过时管理器不会启用。\n是否继续？");
    }
    const bool embedded_adapter=fs::exists(package_dir/L"SAN14Base/GameAdapter");
    if(embedded_adapter){
        auto bytes=read_file(package_dir/L"SAN14Base/GameAdapter/adapter.json",64*1024);
        auto info=WireJson::parse(bytes.begin(),bytes.end());
        if(info.value("schema_version",0)!=1||info.value("abi_version",0)!=1||info.value("sdk_abi_version",0)!=65536||info.value("binary",std::string{})!="GameAdapter.dll"||!info.value("embedded_commands",false)||
           !fs::is_regular_file(package_dir/L"SAN14Base/GameAdapter/GameAdapter.dll")||hash_file(package_dir/L"SAN14Base/GameAdapter/GameAdapter.dll")!=info.value("sha256",std::string{})||fs::exists(package_dir/L"SAN14Base/commands"))throw std::runtime_error("GameAdapter 数据包不完整或校验不符");
    }
    if (!fs::is_regular_file(package_dir / L"dinput8.dll") ||
        !fs::is_directory(package_dir / L"SAN14Mods/companion-mod") ||
        (!embedded_adapter&&(!fs::is_regular_file(package_dir / L"SAN14Base/commands/native-engine.js") ||
        !fs::is_regular_file(package_dir / L"SAN14Base/commands/native-engine.metadata.json"))) ||
        !fs::is_regular_file(package_dir / L"SAN14Base/worker/san14-managed-worker.exe") ||
        !fs::is_directory(package_dir / L"SAN14Base/worker/_internal")) {
        throw std::runtime_error("管理器数据包不完整");
    }

    const bool staged_two_city = stage_two_city(game, package_dir, detected.profile);
    std::map<std::string, std::vector<unsigned char>> targets;
    targets["dinput8.dll"] = read_file(package_dir / L"dinput8.dll");
    for (const wchar_t* folder : {L"SAN14Mods", L"SAN14Base"}) {
        fs::path root = package_dir / folder;
        if (!fs::exists(root)) continue;
        for (const auto& item : fs::recursive_directory_iterator(root)) {
            if (!item.is_regular_file()) continue;
            std::string rel = utf8(fs::relative(item.path(), package_dir).generic_wstring());
            owned_target(game, rel);
            targets[rel] = read_file(item.path());
        }
    }

    fs::path owner_path = data_dir / L"installation.json";
    WireJson old_owner;
    std::vector<unsigned char> previous_owner;
    fs::path missing_game;
    bool has_old = fs::is_regular_file(owner_path);
    if (has_old) {
        auto bytes = read_file(owner_path, 8 * 1024 * 1024);
        old_owner = WireJson::parse(std::string(bytes.begin(), bytes.end()));
        fs::path recorded_game = wide(old_owner.at("game_dir").get<std::string>());
        if (!recorded_game.is_absolute()) throw std::runtime_error("安装记录中的游戏目录不是绝对路径。");
        fs::path old_game = fs::weakly_canonical(recorded_game);
        if (old_game != game) {
            require_missing_game_directory(old_game);
            // A moved manager cannot become the original HR baseline of a fresh install.
            if (old_owner.at("installed").contains("dinput8.dll") && fs::is_regular_file(game / L"dinput8.dll") &&
                hash_file(game / L"dinput8.dll") == old_owner.at("installed").at("dinput8.dll").get<std::string>()) {
                throw std::runtime_error("所选目录含有旧管理器文件，可能是搬动后的游戏。请保留旧记录及备份，先核对迁移，不能按全新游戏安装。");
            }
            policy.require(policy.reinstall_missing_game, L"在新目录安装",
                L"旧安装记录中的游戏目录已不存在：\n" + old_game.wstring() +
                L"\n\n是否安装到当前选择的新目录？\n" + game.wstring() +
                L"\n\n继续会保留旧安装记录和全部备份，为新游戏重新建立安装记录及原始文件备份。现有设置保留。\n"
                L"旧游戏的 HR 文件不会恢复到新游戏；如果只是磁盘未连接，请取消并连接磁盘。\n是否继续？");
            missing_game = old_game;
            previous_owner = std::move(bytes);
            has_old = false;
        }
    }
    if (has_old) {
        const auto& installed = old_owner.at("installed");
        std::vector<std::string> changed;
        for (auto it = installed.begin(); it != installed.end(); ++it) {
            fs::path p = owned_target(game, it.key());
            if (fs::exists(p) && !fs::is_regular_file(p)) throw std::runtime_error("受管文件路径不是普通文件: " + it.key());
            if (!fs::is_regular_file(p) || hash_file(p) != it.value().get<std::string>()) {
                changed.push_back(it.key());
            }
        }
        confirm_changed_files(policy, changed, false);
    }

    for (auto it = targets.begin(); it != targets.end();) {
        const auto& [rel, data] = *it;
        const fs::path path = owned_target(game, rel);
        const bool owned = has_old && old_owner.at("installed").contains(rel);
        if (owned || rel == "dinput8.dll" || !fs::exists(path)) {
            ++it;
            continue;
        }
        // A previously installed, matching optional Mod remains owned by its original installer.
        if (staged_two_city && rel.rfind(kTwoCityModDir, 0) == 0 && read_file(path) == data) {
            it = targets.erase(it);
            continue;
        }
        if (has_old || read_file(path) != data) {
            throw std::runtime_error("存在归属不明的冲突文件，拒绝覆盖: " + rel);
        }
        ++it;
    }

    fs::path worker_dir = game / L"SAN14Base/worker";
    if (fs::exists(worker_dir)) {
        std::set<std::string> known;
        for (const auto& [rel, _] : targets) known.insert(lower(rel));
        if (has_old) {
            for (auto it = old_owner.at("installed").begin(); it != old_owner.at("installed").end(); ++it) {
                known.insert(lower(it.key()));
            }
        }
        for (const auto& item : fs::recursive_directory_iterator(worker_dir)) {
            if (!item.is_regular_file()) continue;
            std::string rel = utf8(fs::relative(item.path(), game).generic_wstring());
            owned_target(game, rel);
            if (!known.count(lower(rel))) {
                throw std::runtime_error("发现未知的 worker 文件，拒绝覆盖升级: " + rel);
            }
        }
    }

    std::set<std::string> obsolete;
    if (has_old) {
        for (auto it = old_owner.at("installed").begin(); it != old_owner.at("installed").end(); ++it) {
            if (!targets.count(it.key())) obsolete.insert(it.key());
        }
    }
    std::map<std::string, std::pair<bool, std::vector<unsigned char>>> obsolete_values;
    if (!obsolete.empty()) {
        fs::path original = fs::canonical(wide(old_owner.at("original_backup").get<std::string>()));
        if (!inside(fs::weakly_canonical(data_dir / L"backups"), original)) {
            throw std::runtime_error("原始备份路径非法");
        }
        auto before_bytes = read_file(original / L"before.json", 8 * 1024 * 1024);
        WireJson baseline = WireJson::parse(std::string(before_bytes.begin(), before_bytes.end()));
        for (const auto& rel : obsolete) {
            owned_target(game, rel);
            if (baseline.contains(rel) && !baseline[rel].is_null()) {
                fs::path src = backup_file(original, rel);
                auto val = read_file(src);
                if (digest(val.data(), val.size()) != baseline[rel].get<std::string>()) {
                    throw std::runtime_error("原始备份校验失败: " + rel);
                }
                obsolete_values[rel] = {true, std::move(val)};
            } else {
                obsolete_values[rel] = {false, {}};
            }
        }
    }

    std::set<std::string> affected = obsolete;
    for (const auto& [rel, _] : targets) affected.insert(rel);
    if (!previous_owner.empty()) {
        require_missing_game_directory(missing_game);
        if (read_file(owner_path) != previous_owner) throw std::runtime_error("安装记录已变化，请重新打开安装器后重试。");
    }
    if (check_running && game_running()) throw std::runtime_error("游戏已启动，请退出游戏后重试。");
    std::map<std::string, std::pair<bool, std::vector<unsigned char>>> snapshot;
    fs::path backup = data_dir / L"backups" / wide(timestamp_tag());
    fs::create_directories(backup);
    WireJson before = WireJson::object();
    for (const auto& rel : affected) {
        fs::path p = owned_target(game, rel);
        if (fs::exists(p)) {
            auto bytes = read_file(p);
            write_bytes_atomic(backup_file(backup, rel), bytes);
            before[rel] = digest(bytes.data(), bytes.size());
            snapshot[rel] = {true, std::move(bytes)};
        } else {
            before[rel] = nullptr;
            snapshot[rel] = {false, {}};
        }
    }
    auto before_dump = before.dump(2);
    write_atomic(backup / L"before.json", before_dump.data(), before_dump.size());
    if (has_old) write_bytes_atomic(backup / L"installation.json", read_file(owner_path));
    if (!previous_owner.empty()) write_bytes_atomic(backup / L"previous-installation.json", previous_owner);
    if (check_running && game_running()) throw std::runtime_error("游戏已启动，请退出游戏后重试。");

    std::vector<std::string> written;
    try {
        for (const auto& rel : obsolete) {
            fs::path target = owned_target(game, rel);
            if (!obsolete_values[rel].first) {
                if (fs::exists(target)) fs::remove(target);
            } else {
                write_bytes_atomic(target, obsolete_values[rel].second);
            }
            written.push_back(rel);
        }
        std::vector<std::string> order;
        for (const auto& [rel, _] : targets) {
            if (rel != "dinput8.dll") order.push_back(rel);
        }
        order.push_back("dinput8.dll");
        for (const auto& rel : order) {
            write_bytes_atomic(owned_target(game, rel), targets[rel]);
            written.push_back(rel);
        }
        std::string version = SAN14_INSTALLER_VERSION;
        fs::path pkg_meta = package_dir / L"package.json";
        if (fs::is_regular_file(pkg_meta)) {
            auto b = read_file(pkg_meta, 1024 * 1024);
            auto j = WireJson::parse(std::string(b.begin(), b.end()));
            if (j.contains("version") && j["version"].is_string()) version = j["version"].get<std::string>();
        }
        WireJson installed = WireJson::object();
        for (const auto& [rel, data] : targets) {
            installed[rel] = digest(data.data(), data.size());
        }
        WireJson record = {
            {"schema", 1},
            {"version", version},
            {"game_dir", utf8(game.wstring())},
            {"backup", utf8(backup.wstring())},
            {"original_backup", has_old ? old_owner.at("original_backup").get<std::string>() : utf8(backup.wstring())},
            {"installed", installed},
            {"game_exe", utf8(detected.exe_path.filename().wstring())},
            {"game_sha256", detected.sha256},
            {"game_build", detected.profile ? detected.profile->game_build : 0},
            {"unrecognized_game", detected.profile == nullptr},
        };
        if (!previous_owner.empty()) record["previous_installation"] = utf8((backup / L"previous-installation.json").wstring());
        auto rec_dump = record.dump(2);
        write_atomic(owner_path, rec_dump.data(), rec_dump.size());
    } catch (...) {
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            try {
                fs::path p = owned_target(game, *it);
                if (!snapshot[*it].first) {
                    if (fs::exists(p)) fs::remove(p);
                } else {
                    write_bytes_atomic(p, snapshot[*it].second);
                }
            } catch (...) {
            }
        }
        throw;
    }

    import_companion_settings(data_dir);
    if (backup_output) *backup_output = backup;
    return utf8(game.wstring());
}

std::string restore_installation(const fs::path& data_dir, bool check_running = true, const InstallPolicy& policy = {}, fs::path* backup_output = nullptr) {
    fs::path owner_path = data_dir / L"installation.json";
    if (!fs::is_regular_file(owner_path)) {
        throw std::runtime_error("未找到安装记录 (installation.json)，无需恢复");
    }
    auto owner_bytes = read_file(owner_path, 8 * 1024 * 1024);
    WireJson record = WireJson::parse(std::string(owner_bytes.begin(), owner_bytes.end()));
    fs::path game = fs::canonical(wide(record.at("game_dir").get<std::string>()));
    if (check_running && game_running()) {
        throw std::runtime_error("检测到三国志14正在运行，请先退出游戏再恢复");
    }
    fs::path backup = fs::canonical(wide(record.at("original_backup").get<std::string>()));
    if (!inside(fs::weakly_canonical(data_dir / L"backups"), backup)) {
        throw std::runtime_error("原始备份路径非法");
    }
    auto prev_bytes = read_file(backup / L"before.json", 8 * 1024 * 1024);
    WireJson previous = WireJson::parse(std::string(prev_bytes.begin(), prev_bytes.end()));

    std::vector<std::string> installed_keys;
    std::vector<std::string> changed;
    const auto& installed = record.at("installed");
    for (auto it = installed.begin(); it != installed.end(); ++it) {
        fs::path p = owned_target(game, it.key());
        if (fs::exists(p) && !fs::is_regular_file(p)) throw std::runtime_error("受管文件路径不是普通文件: " + it.key());
        if (!fs::is_regular_file(p) || hash_file(p) != it.value().get<std::string>()) {
            changed.push_back(it.key());
        }
        installed_keys.push_back(it.key());
    }
    confirm_changed_files(policy, changed, true);
    for (auto it = previous.begin(); it != previous.end(); ++it) {
        owned_target(game, it.key());
        if (!it.value().is_null()) {
            fs::path src = backup_file(backup, it.key());
            if (!fs::is_regular_file(src) ||
                hash_file(src) != it.value().get<std::string>()) {
                throw std::runtime_error("备份文件损坏或缺失: " + it.key());
            }
        }
    }
    // Save the complete pre-uninstall state, including changed and missing files.
    if (check_running && game_running()) throw std::runtime_error("游戏已启动，请退出游戏后重试。");
    fs::path recovery = data_dir / L"backups" / wide("uninstall-" + timestamp_tag());
    fs::create_directories(recovery);
    WireJson before = WireJson::object();
    std::map<std::string, std::pair<bool, std::vector<unsigned char>>> snapshot;
    for (const auto& rel : installed_keys) {
        fs::path p = owned_target(game, rel);
        bool exists = fs::is_regular_file(p);
        auto bytes = exists ? read_file(p) : std::vector<unsigned char>{};
        if (exists) {
            write_bytes_atomic(backup_file(recovery, rel), bytes);
        }
        before[rel] = exists ? WireJson(digest(bytes.data(), bytes.size())) : WireJson(nullptr);
        snapshot[rel] = {exists, std::move(bytes)};
    }
    auto before_dump = before.dump(2);
    write_atomic(recovery / L"before.json", before_dump.data(), before_dump.size());
    write_bytes_atomic(recovery / L"installation.json", owner_bytes);
    if (check_running && game_running()) throw std::runtime_error("游戏已启动，请退出游戏后重试。");
    std::vector<std::string> written;
    try {
        for (auto it = installed_keys.rbegin(); it != installed_keys.rend(); ++it) {
            fs::path p = owned_target(game, *it);
            if (!previous.contains(*it) || previous[*it].is_null()) {
                if (fs::exists(p)) fs::remove(p);
            } else {
                write_bytes_atomic(p, read_file(backup_file(backup, *it)));
            }
            written.push_back(*it);
        }
        fs::rename(owner_path, data_dir / wide("restored-" + timestamp_tag() + ".json"));
    } catch (...) {
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            fs::path p = owned_target(game, *it);
            if (snapshot[*it].first) write_bytes_atomic(p, snapshot[*it].second);
            else if (fs::exists(p)) fs::remove(p);
        }
        throw;
    }
    if (backup_output) *backup_output = recovery;
    return utf8(game.wstring());
}

std::wstring unescape_vdf(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            out.push_back(raw[++i]);
        } else {
            out.push_back(raw[i]);
        }
    }
    return wide(out);
}

std::wstring detect_game_directory() {
    auto check_dir = [](const fs::path& dir) -> std::wstring {
        std::error_code ec;
        if (dir.empty()) return L"";
        for (const wchar_t* exe_name : kSupportedExeNames) {
            if (fs::is_regular_file(dir / exe_name, ec)) {
                return dir.wstring();
            }
        }
        return L"";
    };

    try {
        fs::path owner = local_data() / L"installation.json";
        if (fs::is_regular_file(owner)) {
            auto b = read_file(owner, 4 * 1024 * 1024);
            auto j = WireJson::parse(std::string(b.begin(), b.end()));
            if (j.contains("game_dir") && j["game_dir"].is_string()) {
                auto found = check_dir(wide(j["game_dir"].get<std::string>()));
                if (!found.empty()) return found;
            }
        }
    } catch (...) {
    }

    std::vector<fs::path> steam_roots;
    auto read_reg = [&](HKEY hive, const wchar_t* subkey, const wchar_t* value) {
        wchar_t buf[MAX_PATH]{};
        DWORD bytes = sizeof(buf);
        if (RegGetValueW(hive, subkey, value, RRF_RT_REG_SZ, nullptr, buf, &bytes) == ERROR_SUCCESS && buf[0]) {
            steam_roots.emplace_back(buf);
        }
    };
    read_reg(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    read_reg(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    read_reg(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");

    std::vector<fs::path> libraries = steam_roots;
    for (const auto& root : steam_roots) {
        fs::path vdf = root / L"steamapps\\libraryfolders.vdf";
        if (!fs::is_regular_file(vdf)) continue;
        try {
            auto bytes = read_file(vdf, 2 * 1024 * 1024);
            std::istringstream ss(std::string(bytes.begin(), bytes.end()));
            std::string line;
            while (std::getline(ss, line)) {
                size_t k1 = line.find("\"path\"");
                if (k1 == std::string::npos) continue;
                size_t q1 = line.find('"', k1 + 6);
                if (q1 == std::string::npos) continue;
                size_t q2 = line.find('"', q1 + 1);
                if (q2 == std::string::npos) continue;
                libraries.emplace_back(unescape_vdf(line.substr(q1 + 1, q2 - q1 - 1)));
            }
        } catch (...) {
        }
    }

    for (const auto& lib : libraries) {
        for (const wchar_t* folder : {L"Romance_of_the_Three_Kingdoms_14", L"Romance of the Three Kingdoms 14", L"SAN14"}) {
            auto found = check_dir(lib / L"steamapps\\common" / folder);
            if (!found.empty()) return found;
        }
    }
    for (wchar_t drive = L'C'; drive <= L'Z'; ++drive) {
        std::wstring root(1, drive);
        root += L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        for (const wchar_t* suffix : {
            L"Program Files (x86)\\Steam\\steamapps\\common\\Romance_of_the_Three_Kingdoms_14",
            L"Program Files (x86)\\Steam\\steamapps\\common\\Romance of the Three Kingdoms 14",
            L"Steam\\steamapps\\common\\Romance_of_the_Three_Kingdoms_14",
            L"Steam\\steamapps\\common\\Romance of the Three Kingdoms 14",
            L"SteamLibrary\\steamapps\\common\\Romance_of_the_Three_Kingdoms_14",
            L"SteamLibrary\\steamapps\\common\\Romance of the Three Kingdoms 14",
        }) {
            auto found = check_dir(fs::path(root) / suffix);
            if (!found.empty()) return found;
        }
    }
    return L"";
}

// --- Win32 GUI ---
constexpr int ID_EDIT_DIR = 101;
constexpr int ID_BTN_BROWSE = 102;
constexpr int ID_BTN_INSTALL = 103;
constexpr int ID_BTN_RESTORE = 104;
constexpr int ID_BTN_CLOSE = 105;
constexpr int ID_STATUS = 106;

struct GuiState {
    HWND hwnd = nullptr;
    HWND edit_dir = nullptr;
    HWND status_label = nullptr;
    HFONT font = nullptr;
    HFONT title_font = nullptr;
};

GuiState g_ui;

void set_status(const std::wstring& msg) {
    SetWindowTextW(g_ui.status_label, msg.c_str());
}

void refresh_status_from_dir() {
    wchar_t buf[32768]{};
    GetWindowTextW(g_ui.edit_dir, buf, 32768);
    fs::path dir(buf);
    if (dir.empty()) {
        set_status(L"请选择三国志14游戏根目录。\r\n安装器可识别部分 CE、PK 和原版简繁版本，实际兼容性由 GameAdapter 检测。");
        return;
    }
    std::error_code ec;
    try {
        DetectedGame detected = detect_supported_game(dir);
        if (!detected.found_candidate) {
            set_status(L"所选目录中未找到受支持的三国志14主程序 (CE/PK/原版 简繁，暂不支持曲海金戈)，请重新选择。");
            return;
        }
        if (detected.profile != nullptr) {
            bool installed = fs::is_regular_file(local_data() / L"installation.json", ec);
            std::wstring desc = std::wstring(detected.profile->exe_name) + L" (" + detected.profile->label + L")";
            set_status(installed
                ? L"已识别 " + desc + L"。\r\n当前已有安装记录：同目录可升级或恢复／卸载；若旧游戏目录已删除，点击“安装 / 更新管理器”可确认在新目录安装，并保留旧记录和备份。"
                : L"已识别 " + desc + L"。\r\n首次安装会自动备份现有 dinput8.dll，并保留 HR 数据与导入伴侣设置。");
        } else {
            set_status(L"检测到 " + detected.exe_path.filename().wstring() + L"。\r\n无法识别目前的版本，可能不兼容。点击安装后可选择是否继续；不代表游戏不是正版。");
        }
    } catch (const std::exception& e) {
        set_status(L"检查游戏文件失败: " + wide(e.what()));
    }
}

void browse_folder() {
    IFileDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD opts = 0;
        dialog->GetOptions(&opts);
        dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"选择三国志14游戏根目录（CE / PK / 原版）");
        if (SUCCEEDED(dialog->Show(g_ui.hwnd))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    SetWindowTextW(g_ui.edit_dir, path);
                    CoTaskMemFree(path);
                    refresh_status_from_dir();
                }
                item->Release();
            }
        }
        dialog->Release();
    }
}

void on_install_clicked() {
    wchar_t buf[32768]{};
    GetWindowTextW(g_ui.edit_dir, buf, 32768);
    try {
        set_status(L"正在校验游戏版本并解压管理器数据包，请稍候...");
        UpdateWindow(g_ui.hwnd);
        ScopedTempDir temp;
        fs::path pkg = extract_embedded_payload(temp);
        InstallPolicy policy;
        policy.confirm = [](const std::wstring& title, const std::wstring& message) {
            return MessageBoxW(g_ui.hwnd, message.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
        };
        fs::path saved;
        std::string installed_dir = install_from_package(fs::path(buf), pkg, local_data(), true, policy, &saved);
        const bool unknown = detect_supported_game(fs::path(buf)).profile == nullptr;
        std::wstring message = L"管理器文件已安装到：\r\n" + wide(installed_dir) + L"\r\n本次备份：" + saved.wstring() +
            (unknown ? L"\r\n当前版本尚未识别；启动后将进行适配检查，不通过时会提示管理器未启用。"
                     : L"\r\n请从 Steam 正常启动游戏，在主菜单点击“模组管理”。");
        if (fs::is_directory(pkg / kTwoCityBundle)) {
            message += fs::is_regular_file(pkg / wide(kTwoCityModDir) / L"mod.json")
                ? L"\r\n双城全明星对战已准备好。可在主菜单“模组管理”中启用。"
                : L"\r\n双城全明星对战适用于 Steam 简中《威力加强传承版》Build 25953386。";
        }
        if (fs::is_regular_file(saved / L"previous-installation.json")) {
            message += L"\r\n旧安装记录已保留到：" + (saved / L"previous-installation.json").wstring() + L"\r\n旧备份已保留，新游戏使用独立的原始文件备份。";
        }
        set_status(message);
        MessageBoxW(g_ui.hwnd, message.c_str(), L"安装完成", MB_OK | MB_ICONINFORMATION);
    } catch (const OperationCancelled&) {
        set_status(L"已取消安装，未修改安装文件。");
    } catch (const std::exception& e) {
        std::wstring msg = wide(e.what());
        set_status(L"安装失败：\r\n" + msg);
        MessageBoxW(g_ui.hwnd, msg.c_str(), L"安装失败", MB_OK | MB_ICONERROR);
    }
}

void on_restore_clicked() {
    if (MessageBoxW(g_ui.hwnd, L"确定要卸载 Mod 管理器并恢复安装前的原始文件（包括原 HR dinput8.dll）吗？",
                    L"确认恢复", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    try {
        InstallPolicy policy;
        policy.confirm = [](const std::wstring& title, const std::wstring& message) {
            return MessageBoxW(g_ui.hwnd, message.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
        };
        fs::path saved;
        std::string restored = restore_installation(local_data(), true, policy, &saved);
        std::wstring message = L"已恢复安装前文件并卸载管理器：\r\n" + wide(restored) + L"\r\n卸载前文件已备份到：" + saved.wstring();
        set_status(message);
        MessageBoxW(g_ui.hwnd, message.c_str(), L"恢复完成", MB_OK | MB_ICONINFORMATION);
    } catch (const OperationCancelled&) {
        set_status(L"已取消卸载，未修改安装文件。");
    } catch (const std::exception& e) {
        std::wstring msg = wide(e.what());
        set_status(L"恢复失败：\r\n" + msg);
        MessageBoxW(g_ui.hwnd, msg.c_str(), L"恢复失败", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            g_ui.font = CreateFontIndirectW(&ncm.lfMessageFont);
            ncm.lfMessageFont.lfHeight = static_cast<LONG>(ncm.lfMessageFont.lfHeight * 1.25);
            ncm.lfMessageFont.lfWeight = FW_BOLD;
            g_ui.title_font = CreateFontIndirectW(&ncm.lfMessageFont);
        }
        auto set_font = [](HWND ctrl, HFONT f) { if (f) SendMessageW(ctrl, WM_SETFONT, (WPARAM)f, TRUE); };

        HWND title = CreateWindowW(L"STATIC", kSetupTitle,
            WS_CHILD | WS_VISIBLE, 20, 16, 540, 28, hwnd, nullptr, nullptr, nullptr);
        set_font(title, g_ui.title_font ? g_ui.title_font : g_ui.font);

        HWND lbl = CreateWindowW(L"STATIC", L"游戏根目录（CE / PK / 原版，暂不支持曲海金戈）：",
            WS_CHILD | WS_VISIBLE, 20, 56, 540, 22, hwnd, nullptr, nullptr, nullptr);
        set_font(lbl, g_ui.font);

        g_ui.edit_dir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 82, 446, 26, hwnd, (HMENU)(INT_PTR)ID_EDIT_DIR, nullptr, nullptr);
        set_font(g_ui.edit_dir, g_ui.font);

        HWND browse = CreateWindowW(L"BUTTON", L"浏览...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 476, 81, 88, 28, hwnd, (HMENU)(INT_PTR)ID_BTN_BROWSE, nullptr, nullptr);
        set_font(browse, g_ui.font);

        g_ui.status_label = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            20, 122, 544, 118, hwnd, (HMENU)(INT_PTR)ID_STATUS, nullptr, nullptr);
        set_font(g_ui.status_label, g_ui.font);

        HWND install_btn = CreateWindowW(L"BUTTON", L"安装 / 更新管理器",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 20, 256, 180, 34, hwnd, (HMENU)(INT_PTR)ID_BTN_INSTALL, nullptr, nullptr);
        set_font(install_btn, g_ui.font);

        HWND restore_btn = CreateWindowW(L"BUTTON", L"恢复原 HR / 卸载",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 214, 256, 170, 34, hwnd, (HMENU)(INT_PTR)ID_BTN_RESTORE, nullptr, nullptr);
        set_font(restore_btn, g_ui.font);

        HWND close_btn = CreateWindowW(L"BUTTON", L"退出",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 464, 256, 100, 34, hwnd, (HMENU)(INT_PTR)ID_BTN_CLOSE, nullptr, nullptr);
        set_font(close_btn, g_ui.font);

        std::wstring detected = detect_game_directory();
        if (!detected.empty()) SetWindowTextW(g_ui.edit_dir, detected.c_str());
        refresh_status_from_dir();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_BROWSE: browse_folder(); break;
        case ID_BTN_INSTALL: on_install_clicked(); break;
        case ID_BTN_RESTORE: on_restore_clicked(); break;
        case ID_BTN_CLOSE: DestroyWindow(hwnd); break;
        case ID_EDIT_DIR:
            if (HIWORD(wp) == EN_KILLFOCUS) refresh_status_from_dir();
            break;
        }
        return 0;
    case WM_DESTROY:
        if (g_ui.font) DeleteObject(g_ui.font);
        if (g_ui.title_font) DeleteObject(g_ui.title_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int run_cli(int argc, wchar_t** argv) {
    bool do_install = false, do_restore = false;
    fs::path game_dir;
    fs::path data_dir = local_data();
    InstallPolicy policy;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--install" && i + 1 < argc) {
            do_install = true;
            game_dir = argv[++i];
        } else if (arg == L"--restore") {
            do_restore = true;
        } else if (arg == L"--data-dir" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == L"--allow-unknown-version") {
            policy.allow_unknown_version = true;
        } else if (arg == L"--repair-changed-files") {
            policy.repair_changed_files = true;
        } else if (arg == L"--reinstall-missing-game") {
            policy.reinstall_missing_game = true;
        }
    }
    if (!do_install && !do_restore) return -1;
    AttachConsole(ATTACH_PARENT_PROCESS);
    try {
        if (do_install) {
            ScopedTempDir temp;
            fs::path pkg = extract_embedded_payload(temp);
            install_from_package(game_dir, pkg, data_dir, true, policy);
        } else {
            restore_installation(data_dir, true, policy);
        }
        return 0;
    } catch (const std::exception& e) {
        std::string msg = std::string(e.what()) + "\n";
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        if (err && err != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(err, msg.data(), static_cast<DWORD>(msg.size()), &written, nullptr);
        }
        return 1;
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        int cli = run_cli(argc, argv);
        LocalFree(argv);
        if (cli >= 0) return cli;
    } else if (argv) {
        LocalFree(argv);
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.hIcon = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"San14ModManagerSetupWindow";
    RegisterClassExW(&wc);

    RECT rc{0, 0, 584, 310};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowW(
        wc.lpszClassName, kSetupTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr);
    g_ui.hwnd = hwnd;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
