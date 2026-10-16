#pragma once

// What the panel needs from the system, for Windows and for Linux: the processes by the name, the start of a program on its own
// (the panel may exit, the program stays), the folders, the time. The text is UTF-8 everywhere.

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace Platform
{
	namespace fs = std::filesystem;

	using Pid_t = uint32_t;

	// the processes of this user by the name of the exe ("steam.exe"; Linux: the comm or the file of argv[0], "deadlock.exe" of Proton),
	// the case is not looked at
	auto ProcessIds( const std::string& Name ) -> std::vector<Pid_t>;

	// Windows: TerminateProcess; Linux: SIGTERM, or SIGKILL when Force
	auto Terminate( Pid_t Id , bool Force ) -> bool;

	// the program without waiting for it (Linux: its own session, the input and the output to /dev/null); a bare name is from the PATH.
	// Error - the text of the system on a failure
	auto Spawn( const fs::path& Program , const std::vector<std::string>& Arguments , const fs::path& Directory , std::string& Error ) -> bool;

	// a bare name from the PATH (Windows: .exe is added), empty - none
	auto FindInPath( const std::string& Name ) -> fs::path;

	auto ExeDir() -> fs::path;
	auto HomeDir() -> fs::path;
	auto Env( const char* szName ) -> std::string;

	// stdin is a terminal (the questions can be asked)
	auto IsTerminal() -> bool;

	auto LocalTime( int64_t Unix , std::tm& Out ) -> bool;

	// the text of the last error of the system (GetLastError / errno)
	auto LastErrorText() -> std::string;
}
