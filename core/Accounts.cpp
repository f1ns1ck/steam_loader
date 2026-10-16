#include "Accounts.hpp"

#include "Platform.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <thread>

namespace Accounts
{
	namespace
	{
		constexpr const char* kGameDirFile = "account_panel.txt";
		constexpr const char* kHiddenFile = "account_panel_hidden.txt";
		constexpr const char* kLaunchFile = "account_panel_launch.txt";

		auto Exists( const fs::path& Path ) -> bool
		{
			std::error_code Ec;
			return fs::exists( Path , Ec );
		}

		auto ReadLine( const fs::path& Path ) -> std::string
		{
			std::ifstream File( Path , std::ios::binary );
			std::string Line;

			if ( !File || !std::getline( File , Line ) )
				return {};

			// the BOM of Notepad
			if ( Line.rfind( "\xEF\xBB\xBF" , 0 ) == 0 )
				Line.erase( 0 , 3 );

			while ( !Line.empty() && ( Line.back() == '\r' || Line.back() == '\n' ) )
				Line.pop_back();

			return Line;
		}

		auto WriteText( const Paths_t& Paths , const char* szFile , const std::string& Text ) -> bool
		{
			std::error_code Ec;
			fs::create_directories( Paths.ConfigDir , Ec );

			std::ofstream File( Paths.ConfigDir / szFile , std::ios::binary | std::ios::trunc );
			File << Text;
			return static_cast<bool>( File );
		}

		auto ParseUInt( const std::string& Text , uint64_t& Out ) -> bool
		{
			if ( Text.empty() || Text.size() > 20 || !std::all_of( Text.begin() , Text.end() , []( char C ) { return C >= '0' && C <= '9'; } ) )
				return false;

			Out = std::strtoull( Text.c_str() , nullptr , 10 );
			return true;
		}

		// the case of the latin and the cyrillic letters is not looked at
		auto Fold( const std::string& Text ) -> std::wstring
		{
			std::wstring Out = Steam::Wide( Text );

			for ( auto& C : Out )
			{
				if ( ( C >= L'A' && C <= L'Z' ) || ( C >= 0x410 && C <= 0x42F ) )
					C = static_cast<wchar_t>( C + 0x20 );
				else if ( C == 0x401 )
					C = 0x451;
			}

			return Out;
		}

		auto Names( const std::vector<const Row_t*>& Rows ) -> std::string
		{
			std::string Out;

			for ( size_t i = 0; i < Rows.size() && i < 8; i++ )
				Out += ( i ? ", " : "" ) + Rows[i]->Steam.AccountName + ( Rows[i]->Steam.PersonaName.empty() ? "" : " (" + Rows[i]->Steam.PersonaName + ")" );

			if ( Rows.size() > 8 )
				Out += ", …";

			return Out;
		}
	}

	auto DefaultPaths( const fs::path& ConfigDir ) -> Paths_t
	{
		Paths_t Paths;
		Paths.BaseDir = Platform::ExeDir();
		Paths.ConfigDir = Paths.BaseDir;

#ifndef _WIN32
		const bool Portable = Exists( Paths.BaseDir / kGameDirFile ) || Exists( Paths.BaseDir / kHiddenFile ) || Exists( Paths.BaseDir / kLaunchFile );

		if ( !Portable )
		{
			const std::string Xdg = Platform::Env( "XDG_CONFIG_HOME" );
			Paths.ConfigDir = ( Xdg.empty() ? Platform::HomeDir() / ".config" : fs::path( Xdg ) ) / "account-panel";
		}
#endif

		if ( !ConfigDir.empty() )
			Paths.ConfigDir = ConfigDir;

		return Paths;
	}

	auto FindDataDir() -> fs::path
	{
		const fs::path Exe = Platform::ExeDir();

		for ( const fs::path& Dir : { Exe , Exe / ".." / ".." / ".." / ".." / "x64" / "Release" , Exe / ".." / "x64" / "Release" } )
		{
			if ( Exists( Dir / "fonts" / "NunitoSans-Regular.ttf" ) || Exists( Dir / "fonts" / "IBMPlexSans-Regular.ttf" ) || Exists( Dir / "heroes" / "manifest.txt" ) )
			{
				std::error_code Ec;
				const fs::path Full = fs::weakly_canonical( Dir , Ec );
				return Ec ? Dir : Full;
			}
		}

		return Exe;
	}

	auto ReadHeroNames( const fs::path& DataDir ) -> std::map<uint32_t , std::string>
	{
		std::map<uint32_t , std::string> Names;
		std::ifstream File( DataDir / "heroes" / "abilities.txt" );
		std::string Line;

		while ( std::getline( File , Line ) )
		{
			if ( Line.rfind( "H|" , 0 ) != 0 )
				continue;

			std::vector<std::string> Parts;
			size_t Start = 0;

			for ( size_t End; ( End = Line.find( '|' , Start ) ) != std::string::npos; Start = End + 1 )
				Parts.push_back( Line.substr( Start , End - Start ) );

			Parts.push_back( Line.substr( Start ) );

			if ( Parts.size() >= 5 )
			{
				while ( !Parts[4].empty() && ( Parts[4].back() == '\r' || Parts[4].back() == ' ' ) )
					Parts[4].pop_back();

				Names[static_cast<uint32_t>( strtoul( Parts[1].c_str() , nullptr , 10 ) )] = Parts[4];
			}
		}

		return Names;
	}

	// ------------------------------------------------------------ the files of the panel
	auto ReadHidden( const Paths_t& Paths ) -> std::set<uint32_t>
	{
		std::set<uint32_t> Hidden;
		std::ifstream File( Paths.ConfigDir / kHiddenFile );
		unsigned long Id = 0;

		while ( File >> Id )
		{
			if ( Id )
				Hidden.insert( static_cast<uint32_t>( Id ) );
		}

		return Hidden;
	}

	auto SetHidden( const Paths_t& Paths , uint32_t AccountId , bool Hide ) -> bool
	{
		auto Hidden = ReadHidden( Paths );

		if ( Hide )
			Hidden.insert( AccountId );
		else
			Hidden.erase( AccountId );

		std::string Text;

		for ( const uint32_t Id : Hidden )
			Text += std::to_string( Id ) + "\n";

		return WriteText( Paths , kHiddenFile , Text );
	}

	auto CitadelOf( const fs::path& Dir ) -> fs::path
	{
		if ( Dir.empty() )
			return {};

		if ( Exists( Dir / "game" / "citadel" ) )
			return Dir / "game" / "citadel";

		std::error_code Ec;
		return fs::is_directory( Dir , Ec ) ? Dir : fs::path();
	}

	auto ReadGameDir( const Paths_t& Paths ) -> fs::path
	{
		std::string Line = ReadLine( Paths.ConfigDir / kGameDirFile );

		while ( !Line.empty() && ( Line.back() == ' ' || Line.back() == '\\' || Line.back() == '/' ) )
			Line.pop_back();

		return CitadelOf( Steam::PathFrom( Line ) );
	}

	auto WriteGameDir( const Paths_t& Paths , const fs::path& Dir ) -> bool
	{
		if ( Dir.empty() )
		{
			std::error_code Ec;
			fs::remove( Paths.ConfigDir / kGameDirFile , Ec );
			return !Ec;
		}

		return WriteText( Paths , kGameDirFile , Steam::PathText( Dir ) );
	}

	auto ReadLaunchLine( const Paths_t& Paths ) -> std::string
	{
		return ReadLine( Paths.ConfigDir / kLaunchFile );
	}

	auto WriteLaunchLine( const Paths_t& Paths , const std::string& Line ) -> bool
	{
		return WriteText( Paths , kLaunchFile , Line );
	}

	// ------------------------------------------------------------ the accounts
	auto Load( const Paths_t& Paths , bool ShowHidden ) -> Snapshot_t
	{
		Snapshot_t S;
		S.Info = Steam::ReadInfo();
		S.SteamRunning = Steam::IsSteamRunning();
		S.CitadelDir = ReadGameDir( Paths );
		S.CitadelManual = !S.CitadelDir.empty();

		if ( S.CitadelDir.empty() )
			S.CitadelDir = Steam::FindCitadelDir( S.Info.SteamPath );

		std::map<uint32_t , Row_t> Rows;

		for ( auto& Account : Steam::ReadAccounts( S.Info.SteamPath ) )
		{
			auto& Row = Rows[Account.AccountId];
			Row.AccountId = Account.AccountId;
			Row.HasSteam = true;
			Row.Steam = std::move( Account );
		}

		for ( auto& Data : SoCache::Scan( S.CitadelDir ) )
		{
			auto& Row = Rows[Data.AccountId];
			Row.AccountId = Data.AccountId;
			Row.Data = std::move( Data );

			if ( !Row.HasSteam )
				Row.Steam.SteamId = Steam::kSteamId64Base + Row.AccountId;
		}

		const auto Hidden = ReadHidden( Paths );

		for ( auto& [Id , Row] : Rows )
		{
			Row.Hidden = Hidden.count( Id ) > 0;
			S.HiddenCount += Row.Hidden ? 1 : 0;

			if ( !Row.Hidden || ShowHidden )
				S.Rows.push_back( std::move( Row ) );
		}

		const uint32_t Active = S.SteamRunning ? S.Info.ActiveUser : 0;

		std::sort( S.Rows.begin() , S.Rows.end() , [Active]( const Row_t& A , const Row_t& B )
		{
			const bool ActiveA = A.AccountId == Active , ActiveB = B.AccountId == Active;

			if ( ActiveA != ActiveB )
				return ActiveA;

			if ( A.Steam.Timestamp != B.Steam.Timestamp )
				return A.Steam.Timestamp > B.Steam.Timestamp;

			if ( A.Data.FileTime != B.Data.FileTime )
				return A.Data.FileTime > B.Data.FileTime;

			return A.AccountId < B.AccountId;
		} );

		return S;
	}

	auto Find( const Snapshot_t& Snapshot , const std::string& Query , std::string& Error ) -> const Row_t*
	{
		std::string Word = Query;

		while ( !Word.empty() && Word.front() == ' ' )
			Word.erase( 0 , 1 );

		while ( !Word.empty() && Word.back() == ' ' )
			Word.pop_back();

		if ( Word.empty() )
		{
			Error = "не указан аккаунт";
			return nullptr;
		}

		const auto& Rows = Snapshot.Rows;
		uint64_t Number = 0;

		// #N: the row of the list
		if ( Word[0] == '#' && ParseUInt( Word.substr( 1 ) , Number ) )
		{
			if ( Number >= 1 && Number <= Rows.size() )
				return &Rows[Number - 1];

			Error = "в списке нет строки " + Word + " (всего " + std::to_string( Rows.size() ) + ")";
			return nullptr;
		}

		// [U:1:id]
		if ( Word.size() > 7 && ( Word.rfind( "[U:1:" , 0 ) == 0 || Word.rfind( "[u:1:" , 0 ) == 0 ) && Word.back() == ']' )
			Word = Word.substr( 5 , Word.size() - 6 );

		if ( ParseUInt( Word , Number ) )
		{
			for ( const auto& Row : Rows )
			{
				if ( Row.AccountId == Number || Row.Steam.SteamId == Number )
					return &Row;
			}
		}

		const std::wstring Wanted = Fold( Word );
		std::vector<const Row_t*> Logins , Nicks , Starts;

		for ( const auto& Row : Rows )
		{
			const std::wstring Login = Fold( Row.Steam.AccountName ) , Nick = Fold( Row.Steam.PersonaName );

			if ( !Login.empty() && Login == Wanted )
				Logins.push_back( &Row );
			else if ( !Nick.empty() && Nick == Wanted )
				Nicks.push_back( &Row );
			else if ( Login.rfind( Wanted , 0 ) == 0 || Nick.rfind( Wanted , 0 ) == 0 )
				Starts.push_back( &Row );
		}

		for ( const auto* pFound : { &Logins , &Nicks , &Starts } )
		{
			if ( pFound->size() == 1 )
				return pFound->front();

			if ( pFound->size() > 1 )
			{
				Error = "«" + Query + "» подходит к нескольким: " + Names( *pFound ) + " — укажи логин, account id или #номер";
				return nullptr;
			}
		}

		Error = "аккаунт «" + Query + "» не найден" + ( Snapshot.HiddenCount ? " (убранные ищутся с --all)" : "" );
		return nullptr;
	}

	auto IsActive( const Snapshot_t& Snapshot , const Row_t& Row ) -> bool
	{
		return Snapshot.SteamRunning && Row.AccountId == Snapshot.Info.ActiveUser;
	}

	auto CanSwitch( const Row_t& Row , std::string* pWhy ) -> bool
	{
		const char* szWhy = !Row.HasSteam ? "Этого аккаунта нет в списке Steam (loginusers.vdf): войди в него один раз через Steam."
			: !Row.Steam.RememberPassword ? "Steam не помнит вход в этот аккаунт: войди в него один раз с галкой «Запомнить меня»."
			: nullptr;

		if ( szWhy && pWhy )
			*pWhy = szWhy;

		return !szWhy;
	}

	auto AccountStatus( const Row_t& Row , int64_t T ) -> std::vector<Status_t>
	{
		std::vector<Status_t> Pills;
		const auto& D = Row.Data;

		if ( !D.Valid )
		{
			Pills.push_back( { "нет данных" , SEVERITY_NONE } );
			return Pills;
		}

		if ( D.Flags & SoCache::kFlagAccountBanned )
			Pills.push_back( { "бан аккаунта" , SEVERITY_BAD } );

		if ( D.MmBanUntil > T )
			Pills.push_back( { "бан поиска " + FormatDuration( D.MmBanUntil - T ) , SEVERITY_BAD } );

		if ( D.LowPriorityGames > 0 )
			Pills.push_back( { "LP " + std::to_string( D.LowPriorityGames ) , SEVERITY_WARN } );

		if ( D.CommsBanUntil > T )
			Pills.push_back( { "бан чата" , SEVERITY_WARN } );

		if ( D.ReportBanUntil > T )
			Pills.push_back( { "бан репортов" , SEVERITY_WARN } );

		if ( Pills.empty() )
			Pills.push_back( { "чистый" , SEVERITY_OK } );

		return Pills;
	}

	// ------------------------------------------------------------ the actions
	auto Run( const Steam::Info_t& Info , const Steam::Account_t& Account , EAction Action , const Launch::Command_t& Command ,
			  const Steam::SwitchCallbacks_t& Callbacks , std::string& Message , int WaitMs , bool KeepSteam ) -> bool
	{
#ifdef _WIN32
		// ShellExecuteEx may go through COM
		const HRESULT Com = CoInitializeEx( nullptr , COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE );
#endif

		const auto Status = [&]( const std::string& Text )
		{
			if ( Callbacks.Status )
				Callbacks.Status( Text );
		};

		const bool Program = Action == ACTION_SWITCH_PROGRAM || Action == ACTION_PROGRAM;
		const bool Switch = Action != ACTION_PROGRAM || !Steam::IsSteamRunning();

		// for a program Steam is only the login: it starts without its window and closes right after the login (or stays in the tray
		// with KeepSteam), then the program starts
		const bool Background = Program || Action == ACTION_LOGIN;
		const bool CloseAfter = Background && !KeepSteam;
		const std::string Name = Launch::FileName( Command.Program );
		const auto Started = std::chrono::steady_clock::now();

		std::string Error;
		bool Ok = true;

		if ( Program && Command.Program.empty() )
		{
			Ok = false;
			Error = "команда программы не задана";
		}

		if ( Ok && Switch )
		{
			const Steam::EStart Mode = Action == ACTION_SWITCH_GAME ? Steam::START_GAME
				: !Background ? Steam::START_NORMAL
				: CloseAfter ? Steam::START_LOGIN
				: Steam::START_SILENT;

			Ok = Steam::SwitchAccount( Info , Account , Mode , Callbacks , Error );

			if ( Ok && Background )
			{
				Status( "Жду входа Steam в " + Account.AccountName + "…" );

				if ( !Steam::WaitForLogin( Info , Account.AccountId , WaitMs ) )
				{
					Ok = false;
					Error = "Steam не вошёл в " + Account.AccountName + " за " + FormatDuration( WaitMs / 1000 ) +
						" (просит пароль или Steam Guard?)" + ( Program ? ", " + Name + " не запущен" : std::string() );
				}
				else if ( Program && KeepSteam )
				{
					// the client of Steam is still loading a moment after the login
					Status( "Steam вошёл в " + Account.AccountName + ", запускаю " + Name + "…" );
					std::this_thread::sleep_for( std::chrono::milliseconds( 3000 ) );
				}
				else
					Status( "Steam вошёл в " + Account.AccountName );
			}
		}

		if ( Ok && CloseAfter && !Steam::CloseSteam( Info , Callbacks , Error ) )
		{
			Ok = false;
			Error = "вход в " + Account.AccountName + " выполнен, но " + Error + ( Program ? ", " + Name + " не запущен" : std::string() );
		}

		if ( Ok && Program )
		{
			Status( "Запускаю " + Name + "…" );
			Ok = Launch::Run( Command , Error );
		}

		char Seconds[32];
		snprintf( Seconds , sizeof( Seconds ) , "%.1f с" , std::chrono::duration<double>( std::chrono::steady_clock::now() - Started ).count() );

		if ( !Ok )
			Message = "Ошибка: " + Error;
		else if ( Program )
			Message = "Запущен " + Name + " (аккаунт " + Account.AccountName + ( CloseAfter ? ", Steam закрыт" : "" ) + ", " + Seconds + ")";
		else if ( Action == ACTION_LOGIN )
			Message = CloseAfter ? "Вход в " + Account.AccountName + " выполнен, Steam закрыт (" + Seconds + ")"
				: "Steam вошёл в " + Account.AccountName + " в фоне, без окна (" + Seconds + ")";
		else
			Message = "Steam запускается с аккаунтом " + Account.AccountName + ( Action == ACTION_SWITCH_GAME ? ", Deadlock запустится после входа" : "" );

#ifdef _WIN32
		if ( SUCCEEDED( Com ) )
			CoUninitialize();
#endif

		return Ok;
	}

	// ------------------------------------------------------------ the text
	auto Now() -> int64_t
	{
		return static_cast<int64_t>( std::time( nullptr ) );
	}

	auto FormatDate( int64_t Unix ) -> std::string
	{
		std::tm Local = {};

		if ( Unix <= 0 || !Platform::LocalTime( Unix , Local ) )
			return "—";

		char Text[64];
		std::strftime( Text , sizeof( Text ) , "%d.%m.%Y %H:%M" , &Local );
		return Text;
	}

	auto FormatDuration( int64_t Seconds ) -> std::string
	{
		if ( Seconds < 0 )
			Seconds = -Seconds;

		const long long Days = Seconds / 86400;
		const long long Hours = ( Seconds % 86400 ) / 3600;
		const long long Minutes = ( Seconds % 3600 ) / 60;

		char Text[64];

		if ( Days > 0 )
			snprintf( Text , sizeof( Text ) , "%lld дн. %lld ч" , Days , Hours );
		else if ( Hours > 0 )
			snprintf( Text , sizeof( Text ) , "%lld ч %lld мин" , Hours , Minutes );
		else if ( Minutes > 0 )
			snprintf( Text , sizeof( Text ) , "%lld мин" , Minutes );
		else
			snprintf( Text , sizeof( Text ) , "%lld с" , static_cast<long long>( Seconds ) );

		return Text;
	}

	auto DisplayName( const Row_t& Row ) -> std::string
	{
		if ( !Row.Steam.PersonaName.empty() )
			return Row.Steam.PersonaName;

		if ( !Row.Steam.AccountName.empty() )
			return Row.Steam.AccountName;

		return "ID " + std::to_string( Row.AccountId );
	}
}
