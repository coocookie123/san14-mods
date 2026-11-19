#include "san14_mod.h"
#include <cstddef>
#include <cstring>

namespace {
const San14HostApi* font_host = nullptr;
bool font_active = false;
bool font_lease = false;
constexpr const char* font_setting_key="text.high_resolution";
San14StringView font_text(const char* value){return {value,static_cast<uint32_t>(std::strlen(value))};}

bool rendering_available(const San14HostApi* h) {
    return h && h->struct_size >= offsetof(San14HostApi, text_rendering) + sizeof(h->text_rendering) &&
           h->text_rendering &&
           h->text_rendering->struct_size >= offsetof(San14TextRenderingApi, get_cache_scale) + sizeof(h->text_rendering->get_cache_scale) &&
           h->text_rendering->query_caps && h->text_rendering->set_cache_scale && h->text_rendering->get_cache_scale;
}

San14Result SAN14_MOD_CALL font_load(San14ModContext*) {
    const San14BoolSetting spec{sizeof(spec),font_text(font_setting_key),font_text(u8"增强文本分辨率"),
        font_text(u8"以双倍分辨率生成文字缓存，保持界面原有大小，不改变游戏分辨率。修改后重启游戏生效。"),
        1,nullptr,nullptr,font_text(u8"常规"),SAN14_BOOL_SETTING_RESTART_REQUIRED};
    return font_host->settings.register_bool(font_host->host_context,&spec);
}

San14Result SAN14_MOD_CALL font_enable(San14ModContext*) {
    if (font_active) return SAN14_OK;
    if (!rendering_available(font_host)) return SAN14_E_UNSUPPORTED;
    int32_t enabled=0;
    auto result=font_host->settings.get_bool(font_host->host_context,font_text(font_setting_key),&enabled);
    if(result!=SAN14_OK)return result;
    if(!enabled){font_active=true;return SAN14_OK;}
    San14TextRenderingCaps caps{};
    caps.struct_size = sizeof(caps);
    result = font_host->text_rendering->query_caps(font_host->host_context, &caps);
    if (result != SAN14_OK) return result;
    if (caps.struct_size < offsetof(San14TextRenderingCaps, supported_scales) + sizeof(caps.supported_scales) ||
        !(caps.supported_scales & SAN14_TEXT_RENDERING_SCALE_2X)) return SAN14_E_UNSUPPORTED;
    result = font_host->text_rendering->set_cache_scale(font_host->host_context, 2);
    if (result == SAN14_OK) {font_active = true;font_lease=true;}
    return result;
}

San14Result SAN14_MOD_CALL font_disable(San14ModContext*) {
    if (!font_active) return SAN14_OK;
    if(!font_lease){font_active=false;return SAN14_OK;}
    if (!rendering_available(font_host)) return SAN14_E_UNSUPPORTED;
    const auto result = font_host->text_rendering->set_cache_scale(font_host->host_context, 1);
    if (result == SAN14_OK) {font_active = false;font_lease=false;}
    return result;
}

void SAN14_MOD_CALL font_unload(San14ModContext*) {
    // Host owner cleanup is the final backstop if release fails during unload.
    if (font_lease && rendering_available(font_host))
        font_host->text_rendering->set_cache_scale(font_host->host_context, 1);
    font_active = false;
    font_lease = false;
    font_host = nullptr;
}
}

extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(
    const San14HostApi* h, San14ModApi* out) {
    if (!h || !out || h->abi_version != SAN14_HOST_ABI_V1 || out->struct_size < sizeof(*out) ||
        !rendering_available(h)||h->settings.struct_size<sizeof(San14SettingsApi)||
        !h->settings.register_bool||!h->settings.get_bool) return SAN14_E_UNSUPPORTED;
    if (font_host) return SAN14_E_BUSY;
    font_host = h;
    *out = {sizeof(*out), SAN14_HOST_ABI_V1, font_load, font_enable, font_disable, font_unload};
    return SAN14_OK;
}
