#pragma once
#include <cstdint>
#include <string>
// Restored from SAN14-Companion/san14_companion/app.py's MCP tab.
namespace mcp_help {
inline constexpr const char* endpoint="http://127.0.0.1:8765/mcp";
inline constexpr const char* install=u8"请帮我添加并启用“三国志14”MCP，类型为 Streamable HTTP，地址：http://127.0.0.1:8765/mcp。完成后测试连接；如需重启请告诉我。";
inline std::string endpoint_for(uint32_t port){return "http://127.0.0.1:"+std::to_string(port?port:8765)+"/mcp";}
inline std::string install_for(uint32_t port){return std::string(u8"请帮我添加并启用“三国志14”MCP，类型为 Streamable HTTP，地址：")+endpoint_for(port)+u8"。完成后测试连接；如需重启请告诉我。";}
inline constexpr const char* prompts[]={
    u8"请告诉我三国志14 MCP都能帮我做什么",
    u8"请为我分析天下大势",
    u8"请直接接管我的势力帮我玩",
    u8"请帮我找到我的势力里所有有名声个性的武将"
};
inline constexpr const char* connection=u8"连接 AI 智能体：Claude Code、Codex、WorkBuddy、DSH 等。选择支持 Streamable HTTP 的客户端，添加三国志14 MCP，填写上方本机地址并测试连接。服务运行在游戏所在电脑。";
inline constexpr const char* preparation=u8"在模组管理中启用 MCP → 载入存档并进入大地图 → 将安装指令发给 AI。";
inline constexpr const char* usage=u8"连接后把示例指令粘贴给 AI。分析和操作前请载入存档、进入大地图；未进入战局时不能读取完整局势。接管指令会授权 AI 操作游戏，请按你的意愿选择。";
}
