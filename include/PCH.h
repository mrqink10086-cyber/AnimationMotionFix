#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// clib
#include "RE/Skyrim.h"
#include "REX/REX/Singleton.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <xbyak/xbyak.h>

using namespace std::literals;

namespace stl
{
	using namespace SKSE::stl;
}

// Plugin
#include "Plugin.h"
