#pragma once

// The command line of the panel: account-panel (Linux, Windows) and AccountPanel.exe with arguments. --help has the list.

#include <string>
#include <vector>

namespace Cli
{
	constexpr const char* kVersion = "1.1 (2026-10-17)";

	// the arguments without the name of the program (UTF-8); the code of the exit: 0 - done, 1 - failed, 2 - wrong arguments
	auto Run( const std::vector<std::string>& Args ) -> int;
}
