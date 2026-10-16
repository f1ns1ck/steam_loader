#pragma once

// The program the panel starts after the switch of the account (account_panel_launch.txt: one line, UTF-8, the exe and its
// arguments, as in a shell). "./" and "../" at the start of the path and of an argument (or after "=" in it) are from the folder of
// the panel (Windows: ".\", "..\" too); the variables are expanded (Windows: %VAR%; Linux: $VAR, ${VAR}, and ~/ at the start of a
// word); a path with spaces goes in quotes, or else it is found by the files that exist, as CreateProcess does
// ("C:\Program Files\x.exe -a"). A bare name ("notepad") is from the folder of the panel when it is there, else from the PATH.
// The program is started in its own folder.

#include <filesystem>
#include <string>
#include <vector>

namespace Launch
{
	struct Command_t
	{
		std::filesystem::path Program;		// the full path, or a bare name for the PATH; empty - no command
		std::string Arguments;				// UTF-8, the "./" ones made full
		std::filesystem::path Directory;	// the folder of the program (the folder of the panel for a name from the PATH)
		bool Exists = false;				// Program is a file
	};

	auto Parse( const std::string& Line , const std::filesystem::path& BaseDir ) -> Command_t;

	// the line with another program, its arguments kept; a program inside BaseDir is written as ./...
	auto ReplaceProgram( const std::string& Line , const std::filesystem::path& Program , const std::filesystem::path& BaseDir ) -> std::string;

	// Windows: ShellExecuteEx, a program that asks for the rights of the administrator gets the window of UAC (COM is to be
	// initialized). Linux: fork + exec, the arguments split as sh does (the quotes, the backslash).
	auto Run( const Command_t& Command , std::string& Error ) -> bool;

	// the words of the arguments as sh splits them (Linux: what the program gets)
	auto SplitArguments( const std::string& Text ) -> std::vector<std::string>;

	auto FileName( const std::filesystem::path& Path ) -> std::string;
}
