#pragma once

// The panel without the window (the library AccountPanelCore): the accounts of Steam joined with the caches of Deadlock, the
// files of the panel (the folder of the game set by hand, the accounts taken off the list, the command of the program) and the
// actions (the switch, the switch with the game or with the program). The window (main.cpp) and the command line (Cli.cpp) are
// made of it. Windows and Linux.
//
// The files of the panel (Paths_t::ConfigDir):
//   account_panel.txt          the folder Deadlock or game/citadel, one line (when it is not found by itself)
//   account_panel_hidden.txt   the account ids taken off the list, one per line (Steam and the game are not touched)
//   account_panel_launch.txt   the command of the program after the login (Launch.hpp)

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Launch.hpp"
#include "SoCache.hpp"
#include "Steam.hpp"

namespace Accounts
{
	namespace fs = std::filesystem;

	struct Paths_t
	{
		fs::path ConfigDir;		// the files of the panel
		fs::path BaseDir;		// "./" of the command: the folder of the program
	};

	// Windows: both are the folder of the exe. Linux: the files next to the program when they are there (a portable folder),
	// else $XDG_CONFIG_HOME/account-panel (~/.config/account-panel). An empty ConfigDir is the default one.
	auto DefaultPaths( const fs::path& ConfigDir = {} ) -> Paths_t;

	// the folder of the data of the DLL (x64/Release: the fonts, heroes/): next to the program or from the tools folder
	auto FindDataDir() -> fs::path;

	// the names of the heroes by the id (heroes/abilities.txt of the folder of the data: H|id|...|name)
	auto ReadHeroNames( const fs::path& DataDir ) -> std::map<uint32_t , std::string>;

	// ------------------------------------------------------------ the files of the panel
	auto ReadHidden( const Paths_t& Paths ) -> std::set<uint32_t>;
	auto SetHidden( const Paths_t& Paths , uint32_t AccountId , bool Hide ) -> bool;

	// the folder Deadlock or game/citadel -> game/citadel; empty - not a folder
	auto CitadelOf( const fs::path& Dir ) -> fs::path;

	// account_panel.txt: game/citadel, empty - not set or the folder is gone
	auto ReadGameDir( const Paths_t& Paths ) -> fs::path;
	auto WriteGameDir( const Paths_t& Paths , const fs::path& Dir ) -> bool;	// empty - removed (the search by itself)

	auto ReadLaunchLine( const Paths_t& Paths ) -> std::string;
	auto WriteLaunchLine( const Paths_t& Paths , const std::string& Line ) -> bool;

	// ------------------------------------------------------------ the accounts
	struct Row_t
	{
		uint32_t AccountId = 0;
		bool Hidden = false;		// taken off the list by the user
		bool HasSteam = false;		// in loginusers.vdf: Steam knows it
		Steam::Account_t Steam;
		SoCache::Account_t Data;	// Data.Valid - Deadlock was played on it here
	};

	struct Snapshot_t
	{
		Steam::Info_t Info;
		fs::path CitadelDir;
		bool CitadelManual = false;		// from account_panel.txt
		bool SteamRunning = false;
		std::vector<Row_t> Rows;
		int HiddenCount = 0;			// of the accounts found now
	};

	// Steam and the caches read again. The hidden ones are in Rows only with ShowHidden. The order: the logged in account, then by
	// the last login into Steam, then by the cache.
	auto Load( const Paths_t& Paths , bool ShowHidden ) -> Snapshot_t;

	// An account by a word: "#N" (the N-th row of Rows), the account id, SteamID64, [U:1:id], the login, the nick, then the start
	// of the login or the nick (the case is not looked at). nullptr and Error when none or more than one.
	auto Find( const Snapshot_t& Snapshot , const std::string& Query , std::string& Error ) -> const Row_t*;

	auto IsActive( const Snapshot_t& Snapshot , const Row_t& Row ) -> bool;

	// Steam can log into it by itself; false and the reason in Why when not
	auto CanSwitch( const Row_t& Row , std::string* pWhy = nullptr ) -> bool;

	// the state of the account (the list shows the first one)
	enum ESeverity
	{
		SEVERITY_NONE ,		// no data
		SEVERITY_OK ,
		SEVERITY_WARN ,
		SEVERITY_BAD ,
	};

	struct Status_t
	{
		std::string Text;
		ESeverity Severity = SEVERITY_NONE;
	};

	auto AccountStatus( const Row_t& Row , int64_t Now ) -> std::vector<Status_t>;

	// ------------------------------------------------------------ the actions
	enum EAction
	{
		ACTION_SWITCH ,
		ACTION_SWITCH_GAME ,		// and Deadlock (-applaunch)
		ACTION_SWITCH_PROGRAM ,		// the login (Steam without the window), Steam closed, then the program of the command
		ACTION_PROGRAM ,			// the account is logged in: Steam closed, then the program
		ACTION_LOGIN ,				// only the login, then Steam closed (--login)
	};

	// The whole action in the thread of the caller (it takes seconds: the exit of Steam, the login). Callbacks.Status gets the steps,
	// Message the last line (the result or the error, with the time it took). WaitMs - how long the login is waited for.
	// The program and the login: Steam starts without its window, logs in and is closed (-shutdown, it saves the login) before the
	// program; KeepSteam - Steam stays in the tray, the program starts while it runs.
	auto Run( const Steam::Info_t& Info , const Steam::Account_t& Account , EAction Action , const Launch::Command_t& Command ,
			  const Steam::SwitchCallbacks_t& Callbacks , std::string& Message , int WaitMs = 120000 , bool KeepSteam = false ) -> bool;

	// ------------------------------------------------------------ the text
	auto Now() -> int64_t;
	auto FormatDate( int64_t Unix ) -> std::string;		// "31.12.2026 23:59", "—" for 0
	auto FormatDuration( int64_t Seconds ) -> std::string;	// "2 дн. 4 ч"
	auto DisplayName( const Row_t& Row ) -> std::string;	// the nick, else the login, else the id
}
