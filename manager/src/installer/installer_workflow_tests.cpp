// Exercise the same native install/restore functions used by Setup's buttons.
#include "installer_exe.cpp"
#include <cassert>

int main() {
    ScopedTempDir fixture;
    fixture.path = fs::temp_directory_path() / (L"s14-setup-test-" + std::to_wstring(GetCurrentProcessId()));
    assert(!fs::exists(fixture.path));
    const auto game = fixture.path / L"game";
    const auto package = fixture.path / L"package";
    const auto state = fixture.path / L"state" / std::wstring(55, L'p');
    auto put = [](const fs::path& path, const std::string& value) {
        fs::create_directories(path.parent_path());
        write_atomic(path, value.data(), value.size());
    };
    auto get = [](const fs::path& path) {
        auto bytes = read_file(path);
        return std::string(bytes.begin(), bytes.end());
    };
    auto rejects = [](auto action) {
        bool rejected = false;
        try { action(); } catch (const std::exception&) { rejected = true; }
        assert(rejected);
    };
    for (const auto& build : installer_game_builds::known_builds) {
        assert(installer_game_builds::select(build.exe_name, build.sha256) == &build);
        assert(!installer_game_builds::select(build.exe_name, "unknown fingerprint"));
        assert(!installer_game_builds::select(L"unsupported.exe", build.sha256));
    }
    const auto& ce = installer_game_builds::known_builds[0];
    assert(ce.game_build == 25953386 && installer_game_builds::select(L"san14ce_sc.EXE", ce.sha256) == &ce);
    put(game / L"SAN14CE_SC.exe", "modified test executable");
    put(game / L"dinput8.dll", "original HR");
    put(package / L"dinput8.dll", "new manager");
    const auto rel = L"SAN14Mods/companion-mod/bin/win-x64/companion.dll";
    put(package / rel, "new companion");
    put(package / L"SAN14Base/commands/native-engine.js", "command");
    put(package / L"SAN14Base/commands/native-engine.metadata.json", "{}");
    put(package / L"SAN14Base/worker/san14-managed-worker.exe", "worker");
    put(package / L"SAN14Base/worker/_internal/runtime.dll", "runtime");
    const auto license = fs::path(L"SAN14Base/worker/_internal/licenses") / std::wstring(60, L'a') /
                         std::wstring(52, L'b') / L"LICENSE";
    put(package / license, "runtime license text");

    InstallPolicy decline;
    unsigned questions = 0;
    decline.confirm = [&](const std::wstring& title, const std::wstring& message) {
        ++questions;
        assert(title == L"版本兼容性提示");
        assert(message.find(L"无法识别目前的版本，可能不兼容") != std::wstring::npos);
        return false;
    };
    rejects([&] { install_from_package(game, package, state, false, decline); });
    assert(questions == 1 && !fs::exists(state));
    assert(get(game / L"dinput8.dll") == "original HR");

    InstallPolicy accept;
    accept.allow_unknown_version = true;
    accept.repair_changed_files = true;
    fs::path first_backup;
    install_from_package(game, package, state, false, accept, &first_backup);
    auto first = WireJson::parse(get(state / L"installation.json"));
    assert(first["unrecognized_game"] == true && first["game_build"] == 0);
    assert(first["game_sha256"] == hash_file(game / L"SAN14CE_SC.exe"));
    assert(get(first_backup / L"dinput8.dll") == "original HR");
    const auto settings = get(state / L"settings.ini");
    put(game / rel, "user changed companion");
    fs::remove(game / L"dinput8.dll");
    const auto owner_before = get(state / L"installation.json");
    InstallPolicy no_repair;
    no_repair.allow_unknown_version = true;
    rejects([&] { install_from_package(game, package, state, false, no_repair); });
    rejects([&] { restore_installation(state, false, no_repair); });
    assert(get(state / L"installation.json") == owner_before);
    assert(get(game / rel) == "user changed companion");

    fs::path repair_backup;
    questions = 0;
    InstallPolicy repair;
    repair.allow_unknown_version = true;
    repair.confirm = [&](const std::wstring& title, const std::wstring& message) {
        ++questions;
        assert(title == L"备份并继续" && message.find(L"共") == std::wstring::npos);
        assert(message.find(L"companion.dll") != std::wstring::npos);
        return true;
    };
    install_from_package(game, package, state, false, repair, &repair_backup);
    assert(questions == 1);
    assert(get(repair_backup / rel) == "user changed companion");
    assert(get(repair_backup / L"installation.json") == owner_before);
    assert(WireJson::parse(get(repair_backup / L"before.json"))["dinput8.dll"].is_null());
    assert(WireJson::parse(get(state / L"installation.json"))["original_backup"] == first["original_backup"]);
    assert(get(state / L"settings.ini") == settings);

    put(game / L"dinput8.dll", "user changed entry");
    fs::remove(game / rel);
    fs::path uninstall_backup;
    restore_installation(state, false, accept, &uninstall_backup);
    assert((uninstall_backup / license).native().size() >= MAX_PATH);
    assert(get(extended_path(uninstall_backup / license)) == "runtime license text");
    assert(get(uninstall_backup / L"dinput8.dll") == "user changed entry");
    assert(WireJson::parse(get(uninstall_backup / L"before.json"))[utf8(fs::path(rel).generic_wstring())].is_null());
    assert(get(game / L"dinput8.dll") == "original HR");
    assert(!fs::exists(state / L"installation.json"));
    assert(get(state / L"settings.ini") == settings);
    assert(get(game / L"SAN14CE_SC.exe") == "modified test executable");

    install_from_package(game, package, state, false, accept);
    // Deleted old game: decline and failures retain ownership; success uses a
    // separate HR baseline, archives the exact old record, and keeps settings.
    const auto replacement = fixture.path / L"new-game";
    put(replacement / L"SAN14CE_SC.exe", "modified test executable");
    put(replacement / L"dinput8.dll", "new game HR");
    const auto old_record = get(state / L"installation.json");
    const auto old_json = WireJson::parse(old_record);
    const auto old_backup = fs::path(wide(old_json.at("original_backup").get<std::string>()));
    InstallPolicy reinstall = accept;
    reinstall.reinstall_missing_game = true;
    // Even explicit consent cannot bypass an existing old directory.
    rejects([&] { install_from_package(replacement, package, state, false, reinstall); });
    assert(get(state / L"installation.json") == old_record);
    fs::rename(game, fixture.path / L"removed-game");
    const auto count_backups = [&] { return std::distance(fs::directory_iterator(state / L"backups"), fs::directory_iterator{}); };
    const auto backup_count = count_backups();
    rejects([&] { install_from_package(replacement, package, state, false, accept); });
    assert(count_backups() == backup_count);
    InstallPolicy decline_new = accept;
    questions = 0;
    decline_new.confirm = [&](const std::wstring& title, const std::wstring& message) {
        ++questions;
        assert(title == L"在新目录安装" && message.find(L"旧安装记录中的游戏目录已不存在") != std::wstring::npos);
        return false;
    };
    rejects([&] { install_from_package(replacement, package, state, false, decline_new); });
    assert(questions == 1 && count_backups() == backup_count);
    assert(get(state / L"installation.json") == old_record);
    put(replacement / L"dinput8.dll", "new manager");
    rejects([&] { install_from_package(replacement, package, state, false, reinstall); });
    put(replacement / L"dinput8.dll", "new game HR");
    put(replacement / rel, "independent plugin");
    rejects([&] { install_from_package(replacement, package, state, false, reinstall); });
    assert(get(state / L"installation.json") == old_record);
    fs::remove(replacement / rel);
    InstallPolicy reappeared = accept;
    reappeared.confirm = [&](const std::wstring&, const std::wstring&) { fs::create_directory(game); return true; };
    rejects([&] { install_from_package(replacement, package, state, false, reappeared); });
    fs::remove(game);
    // A deterministic late owner-commit failure must roll back all game writes.
    fs::create_directory(state / L"installation.json.tmp");
    rejects([&] { install_from_package(replacement, package, state, false, reinstall); });
    fs::remove(state / L"installation.json.tmp");
    assert(get(state / L"installation.json") == old_record);
    assert(get(replacement / L"dinput8.dll") == "new game HR");
    assert(!fs::exists(replacement / rel));
    fs::path new_backup;
    questions = 0;
    InstallPolicy accept_new = accept;
    accept_new.confirm = [&](const std::wstring& title, const std::wstring&) { ++questions; assert(title == L"在新目录安装"); return true; };
    install_from_package(replacement, package, state, false, accept_new, &new_backup);
    assert(questions == 1);
    auto new_record = WireJson::parse(get(state / L"installation.json"));
    assert(new_record["original_backup"] == utf8(new_backup.wstring()));
    assert(new_record["original_backup"] != old_json["original_backup"]);
    assert(get(new_backup / L"previous-installation.json") == old_record);
    assert(get(old_backup / L"dinput8.dll") == "original HR");
    assert(get(new_backup / L"dinput8.dll") == "new game HR");
    assert(get(state / L"settings.ini") == settings);
    restore_installation(state, false, accept);
    assert(get(replacement / L"dinput8.dll") == "new game HR");
    assert(get(new_backup / L"previous-installation.json") == old_record);
    fs::rename(fixture.path / L"removed-game", game);
    // Restore the old live ownership solely to continue path-validation tests.
    put(state / L"installation.json", old_record);
    auto owner = WireJson::parse(get(state / L"installation.json"));
    owner["installed"]["../outside.bin"] = "fake";
    put(state / L"installation.json", owner.dump());
    rejects([&] { restore_installation(state, false, accept); });
    rejects([&] { install_from_package(game, package, state, false, accept); });
    assert(get(game / L"dinput8.dll") == "new manager");
    return 0;
}
