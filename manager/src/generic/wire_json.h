#pragma once
// IPC uses general JSON including floating-point values. Manifest admission
// intentionally keeps its separate strict, bounded integer-only parser.
#include <nlohmann/json.hpp>
using WireJson=nlohmann::json;
