#pragma once

#include "Core.h"
#include <memory>
#include "spdlog/spdlog.h"

namespace Atom
{
	class Log
	{
	public:
		static void Init();

		inline static std::shared_ptr<spdlog::logger>& GetLogger() { return s_Logger; }

	private:
		static std::shared_ptr<spdlog::logger> s_Logger; // declaration
	};
}

// LOG MACRO
#define ATOM_ERROR(...) ::Atom::Log::GetLogger()->error(__VA_ARGS__)
#define ATOM_INFO(...) ::Atom::Log::GetLogger()->info(__VA_ARGS__)
#define ATOM_WARN(...) ::Atom::Log::GetLogger()->warn(__VA_ARGS__)
#define ATOM_TRACE(...) ::Atom::Log::GetLogger()->trace(__VA_ARGS__)