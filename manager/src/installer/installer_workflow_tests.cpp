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
    assert(!ce.pe_fingerprint.empty());
    assert(installer_game_builds::select_by_fingerprint(L"SAN14CE_SC.exe", ce.pe_fingerprint) == &ce);
    assert(installer_game_builds::select_by_fingerprint(L"san14ce_sc.EXE", ce.pe_fingerprint) == &ce);
    assert(!installer_game_builds::select_by_fingerprint(L"SAN14CE_TC.exe", ce.pe_fingerprint));
    auto bad_fp = ce.pe_fingerprint;
    bad_fp.time_date_stamp ^= 1u;
    assert(!installer_game_builds::select_by_fingerprint(L"SAN14CE_SC.exe", bad_fp));
    bad_fp = ce.pe_fingerprint;
    bad_fp.rsds_guid_hex = "00000000000000000000000000000000";
    assert(!installer_game_builds::select_by_fingerprint(L"SAN14CE_SC.exe", bad_fp));
    bad_fp = ce.pe_fingerprint;
    bad_fp.rdata_sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    assert(!installer_game_builds::select_by_fingerprint(L"SAN14CE_SC.exe", bad_fp));
    bad_fp = ce.pe_fingerprint;
    bad_fp.pdata_sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    assert(!installer_game_builds::select_by_fingerprint(L"SAN14CE_SC.exe", bad_fp));

    // Malformed / non-PE blobs must safely fail fingerprint extraction without throwing.
    {
        installer_game_builds::OwnedPeFingerprint fp;
        const std::vector<unsigned char> garbage{'M', 'Z', 0, 1, 2, 3};
        assert(!installer_game_builds::extract_pe_fingerprint(garbage, fp));
        assert(!installer_game_builds::select_for_image(L"SAN14CE_SC.exe", "badsha", garbage));
    }

    // Synthetic minimal PE64 with .rdata, .pdata, TimeDateStamp, and CodeView RSDS entry
    // verifies extraction logic even in CI environments without local game binaries.
    {
        std::vector<unsigned char> pe(0x1000, 0);
        IMAGE_DOS_HEADER dos{};
        dos.e_magic = IMAGE_DOS_SIGNATURE;
        dos.e_lfanew = 0x80;
        std::memcpy(pe.data(), &dos, sizeof(dos));

        IMAGE_NT_HEADERS64 nt{};
        nt.Signature = IMAGE_NT_SIGNATURE;
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
        nt.FileHeader.NumberOfSections = 2;
        nt.FileHeader.TimeDateStamp = 0x12345678u;
        nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress = 0x2000;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size = sizeof(IMAGE_DEBUG_DIRECTORY);
        std::memcpy(pe.data() + 0x80, &nt, sizeof(nt));

        const size_t sec_off = 0x80 + 4 + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64);
        IMAGE_SECTION_HEADER secs[2]{};
        std::memcpy(secs[0].Name, ".rdata", 6);
        secs[0].VirtualAddress = 0x2000;
        secs[0].SizeOfRawData = 0x200;
        secs[0].PointerToRawData = 0x400;
        std::memcpy(secs[1].Name, ".pdata", 6);
        secs[1].VirtualAddress = 0x3000;
        secs[1].SizeOfRawData = 0x200;
        secs[1].PointerToRawData = 0x600;
        std::memcpy(pe.data() + sec_off, secs, sizeof(secs));

        IMAGE_DEBUG_DIRECTORY dbg{};
        dbg.Type = IMAGE_DEBUG_TYPE_CODEVIEW;
        dbg.SizeOfData = 28;
        dbg.AddressOfRawData = 0x2040;
        dbg.PointerToRawData = 0x440;
        std::memcpy(pe.data() + 0x400, &dbg, sizeof(dbg));
        std::memcpy(pe.data() + 0x440, "RSDS", 4);
        for (size_t i = 0; i < 16; ++i) pe[0x444 + i] = static_cast<unsigned char>(0x10 + i);
        pe[0x600] = 0xAA;

        installer_game_builds::OwnedPeFingerprint fp;
        assert(installer_game_builds::extract_pe_fingerprint(pe, fp));
        assert(fp.time_date_stamp == 0x12345678u);
        assert(fp.rsds_guid_hex == "101112131415161718191a1b1c1d1e1f");
        assert(fp.rdata_sha256 == digest(pe.data() + 0x400, 0x200));
        assert(fp.pdata_sha256 == digest(pe.data() + 0x600, 0x200));
    }

    // Optional real-binary verification against local SteamStub / unpacked CE 1.0.2.0 executables
    // (resolved via SAN14_TEST_STEAM_EXE / SAN14_TEST_UNPACKED_EXE or %USERPROFILE%\Downloads;
    // gracefully skipped when absent, e.g. in CI).
    {
        auto env_path = [](const wchar_t* name) -> fs::path {
            wchar_t buf[32768]{};
            const DWORD n = GetEnvironmentVariableW(name, buf, 32768);
            return (n > 0 && n < 32768) ? fs::path(std::wstring(buf, n)) : fs::path{};
        };
        const fs::path profile_dir = env_path(L"USERPROFILE");
        fs::path steam_exe = env_path(L"SAN14_TEST_STEAM_EXE");
        if (steam_exe.empty() && !profile_dir.empty()) steam_exe = profile_dir / L"Downloads" / L"SAN14CE_SC.exe";
        fs::path unpacked_exe = env_path(L"SAN14_TEST_UNPACKED_EXE");
        if (unpacked_exe.empty() && !profile_dir.empty()) unpacked_exe = profile_dir / L"Downloads" / L"SAN14CE_SC (1).exe";
        std::error_code ec;
        if (!steam_exe.empty() && fs::is_regular_file(steam_exe, ec)) {
            auto bytes = read_file(steam_exe);
            auto sha = digest(bytes.data(), bytes.size());
            assert(installer_game_builds::select(L"SAN14CE_SC.exe", sha) == &ce);
            installer_game_builds::OwnedPeFingerprint fp;
            assert(installer_game_builds::extract_pe_fingerprint(bytes, fp));
            assert(installer_game_builds::select_by_fingerprint(L"SAN14CE_SC.exe", fp.view()) == &ce);
            assert(installer_game_builds::select_for_image(L"SAN14CE_SC.exe", sha, bytes) == &ce);
        }
        if (!unpacked_exe.empty() && fs::is_regular_file(unpacked_exe, ec)) {
            auto bytes = read_file(unpacked_exe);
            auto sha = digest(bytes.data(), bytes.size());
            assert(installer_game_builds::select(L"SAN14CE_SC.exe", sha) == nullptr);
            installer_game_builds::OwnedPeFingerprint fp;
            assert(installer_game_builds::extract_pe_fingerprint(bytes, fp));
            assert(installer_game_builds::select_by_fingerprint(L"SAN14CE_SC.exe", fp.view()) == &ce);
            assert(installer_game_builds::select_for_image(L"SAN14CE_SC.exe", sha, bytes) == &ce);
        }
    }
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

    // Optional Two City scenario staging must be strictly non-fatal and return structured statuses.
    {
        const auto tc_root = fixture.path / L"two-city";
        const auto tc_game = tc_root / L"game";
        const auto tc_pkg = tc_root / L"pkg";
        const std::string fake_s14 = "expected compiled two-city scenario payload";
        const std::string fake_sha = digest(fake_s14.data(), fake_s14.size());
        const std::string valid_def = std::string(R"({"scenario":{"native_file":{"path":"__san14_content__/)") +
                                      fake_sha + R"(.s14","sha256":")" + fake_sha + R"("}}})";

        // 1. NotBundled when SAN14TwoCity does not exist.
        assert(stage_two_city(tc_game, tc_pkg, &ce) == TwoCityStageStatus::NotBundled);
        assert(two_city_status_message(TwoCityStageStatus::NotBundled).empty());

        // Populate a Two City bundle without author.exe.
        put(tc_pkg / kTwoCityBundle / L"mod.json", R"({"id":"two-city-all-stars-mod"})");
        put(tc_pkg / kTwoCityBundle / L"README.md", "readme");
        put(tc_pkg / kTwoCityBundle / L"__san14_content__/definition.json", valid_def);

        // 2. SkippedUnsupportedBuild when profile is null or non-25953386.
        assert(stage_two_city(tc_game, tc_pkg, nullptr) == TwoCityStageStatus::SkippedUnsupportedBuild);
        assert(stage_two_city(tc_game, tc_pkg, &installer_game_builds::known_builds[1]) == TwoCityStageStatus::SkippedUnsupportedBuild);

        // 3. SkippedMissingBase when 0010_SC/scedaexce01.s14 is absent.
        assert(stage_two_city(tc_game, tc_pkg, &ce) == TwoCityStageStatus::SkippedMissingBase);
        const auto missing_msg = two_city_status_message(TwoCityStageStatus::SkippedMissingBase);
        assert(missing_msg.find(L"缺少") != std::wstring::npos && missing_msg.find(L"0010_SC/scedaexce01.s14") != std::wstring::npos);

        // 4. SkippedModifiedBase when 0010_SC/scedaexce01.s14 exists with a modified hash.
        put(tc_game / L"0010_SC/scedaexce01.s14", "user modified scenario 01");
        assert(stage_two_city(tc_game, tc_pkg, &ce) == TwoCityStageStatus::SkippedModifiedBase);
        const auto modified_msg = two_city_status_message(TwoCityStageStatus::SkippedModifiedBase);
        assert(modified_msg.find(L"已被修改") != std::wstring::npos && modified_msg.find(L"0010_SC/scedaexce01.s14") != std::wstring::npos);
        assert(missing_msg != modified_msg);

        // 5. SkippedAlreadyValid when already installed in game dir with expected output SHA-256
        //    (even when base scedaexce01.s14 is modified or missing!).
        put(tc_game / wide(kTwoCityModDir) / L"__san14_content__" / wide(fake_sha + ".s14"), fake_s14);
        assert(stage_two_city(tc_game, tc_pkg, &ce) == TwoCityStageStatus::SkippedAlreadyValid);
        assert(fs::is_regular_file(tc_pkg / wide(kTwoCityModDir) / L"mod.json"));
        assert(hash_file(tc_pkg / wide(kTwoCityModDir) / L"__san14_content__" / wide(fake_sha + ".s14")) == fake_sha);

        // 6. SkippedAlreadyValid when already staged in package.
        fs::remove_all(tc_game / wide(kTwoCityModDir));
        assert(stage_two_city(tc_game, tc_pkg, &ce) == TwoCityStageStatus::SkippedAlreadyValid);

        // 7. FailedGeneration on malformed definition.json (must not throw!).
        fs::remove_all(tc_pkg / wide(kTwoCityModDir));
        put(tc_pkg / kTwoCityBundle / L"__san14_content__/definition.json", "{broken json");
        assert(stage_two_city(tc_game, tc_pkg, &ce) == TwoCityStageStatus::FailedGeneration);
        assert(!fs::exists(tc_pkg / wide(kTwoCityModDir)));

        // 8. End-to-end install_from_package with bundled SAN14TwoCity never aborts core install.
        const auto e2e_game = tc_root / L"e2e-game";
        const auto e2e_pkg = tc_root / L"e2e-pkg";
        const auto e2e_state = tc_root / L"e2e-state";
        put(e2e_game / L"SAN14CE_SC.exe", "modified test executable");
        put(e2e_game / L"dinput8.dll", "original HR");
        put(e2e_pkg / L"dinput8.dll", "new manager");
        put(e2e_pkg / rel, "new companion");
        put(e2e_pkg / L"SAN14Base/commands/native-engine.js", "command");
        put(e2e_pkg / L"SAN14Base/commands/native-engine.metadata.json", "{}");
        put(e2e_pkg / L"SAN14Base/worker/san14-managed-worker.exe", "worker");
        put(e2e_pkg / L"SAN14Base/worker/_internal/runtime.dll", "runtime");
        put(e2e_pkg / kTwoCityBundle / L"mod.json", R"({"id":"two-city-all-stars-mod"})");
        put(e2e_pkg / kTwoCityBundle / L"README.md", "readme");
        put(e2e_pkg / kTwoCityBundle / L"__san14_content__/definition.json", valid_def);
        TwoCityStageStatus status = TwoCityStageStatus::NotBundled;
        install_from_package(e2e_game, e2e_pkg, e2e_state, false, accept, nullptr, &status);
        assert(status == TwoCityStageStatus::SkippedUnsupportedBuild);
        assert(get(e2e_game / L"dinput8.dll") == "new manager");
        assert(get(e2e_game / rel) == "new companion");
    }
    return 0;
}
