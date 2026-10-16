#pragma once

// Steam: the accounts this computer has logged in with (config/loginusers.vdf - the names, the nicks, the SteamIDs; no passwords
// or tokens are there and nothing else is read), the logged in one, the folder of Deadlock and the switch of the account the way
// TcNo Account Switcher does it: Steam is closed, AutoLoginUser gets the name of the account and Steam is started again. It logs
// in by itself only into an account whose login was remembered.
//   Windows: HKCU\Software\Valve\Steam (SteamPath, SteamExe, AutoLoginUser, ActiveProcess\ActiveUser), steam.exe.
//   Linux: the same keys in .steam/registry.vdf (the registry of the Steam for Linux; it is written only while Steam is closed),
//   the folder ~/.steam/steam or ~/.local/share/Steam (and the ones of Flatpak and Snap), the command steam / steam.sh.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Steam
{
	namespace fs = std::filesystem;

	constexpr uint32_t kDeadlockAppId = 1422450;
	constexpr uint64_t kSteamId64Base = 76561197960265728ULL;

#ifdef _WIN32
	constexpr const char* kSteamProcess = "steam.exe";
#else
	constexpr const char* kSteamProcess = "steam";
#endif
	constexpr const char* kGameProcess = "deadlock.exe";	// Linux: the same name under Proton

	struct Account_t
	{
		uint64_t SteamId = 0;
		uint32_t AccountId = 0;			// SteamId - kSteamId64Base: the id of cache_<id>.soc
		std::string AccountName;		// the login (UTF-8)
		std::string PersonaName;		// the nick (UTF-8)
		bool RememberPassword = false;	// Steam can log into it without the password
		bool MostRecent = false;
		int64_t Timestamp = 0;			// the last login (unix)
	};

	struct Info_t
	{
		fs::path SteamPath;					// the folder of Steam
		fs::path SteamExe;					// what starts Steam: steam.exe; Linux: steam from the PATH, steam.sh, flatpak or snap
		std::vector<std::string> SteamArgs;	// the arguments before the ones of the panel (flatpak: run com.valvesoftware.Steam)
		fs::path RegistryFile;				// Linux: registry.vdf
		uint32_t ActiveUser = 0;			// the account id of the logged in account, 0 - none (Steam off or not logged in)
		std::string AutoLoginUser;
	};

	auto ReadInfo() -> Info_t;
	auto ReadAccounts( const fs::path& SteamPath ) -> std::vector<Account_t>;

	// the folder game/citadel of Deadlock: the uninstall entry of the app (Windows), then the libraries of Steam; empty - not found
	auto FindCitadelDir( const fs::path& SteamPath ) -> fs::path;

	auto IsSteamRunning() -> bool;
	auto IsGameRunning() -> bool;

	// the avatar of the account that Steam keeps (config/avatarcache/<SteamID64>.png), empty - none
	auto AvatarPath( const fs::path& SteamPath , uint64_t SteamId ) -> fs::path;

	// The switch, in the thread of the caller. Status gets the steps; when Steam does not close in time, AskKill is asked (it may
	// wait for the answer of the user) whether to end the process. Returns false and the reason in Error on a failure.
	// A login that needs the password (or Steam Guard) shows the window of the login of Steam in every mode.
	struct SwitchCallbacks_t
	{
		std::function<void( const std::string& )> Status;
		std::function<bool()> AskKill;
	};

	enum EStart
	{
		START_NORMAL ,		// Steam with its window
		START_GAME ,		// and Deadlock (-applaunch)
		START_SILENT ,		// -silent: in the tray, without the window
		START_LOGIN ,		// only for the login, Steam is closed right after it: -silent, no friends and chat, no check of the files
	};

	auto SwitchAccount( const Info_t& Info , const Account_t& Account , EStart Mode , const SwitchCallbacks_t& Callbacks , std::string& Error ) -> bool;

	// Steam closes by its own command (-shutdown, it saves what it has); AskKill when it does not in 15 s. True - no Steam is running.
	auto CloseSteam( const Info_t& Info , const SwitchCallbacks_t& Callbacks , std::string& Error ) -> bool;

	// after the start of Steam: until it is logged into the account (ActiveUser - the switch clears it before the start - or
	// loginusers.vdf written after the start), polled without a pause; false - not in time
	auto WaitForLogin( const Info_t& Info , uint32_t AccountId , int Milliseconds ) -> bool;

	// Deadlock through Steam (steam://rungameid)
	auto LaunchGame( const Info_t& Info ) -> bool;

	auto Utf8( const std::wstring& Text ) -> std::string;
	auto Wide( const std::string& Text ) -> std::wstring;

	// a path as UTF-8 text and back
	auto PathText( const fs::path& Path ) -> std::string;
	auto PathFrom( const std::string& Text ) -> fs::path;
}
