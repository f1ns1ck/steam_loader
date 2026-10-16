#include "Steam.hpp"

#include "Platform.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string_view>
#include <thread>

namespace Steam
{
	namespace
	{
		auto Lower( std::string_view Text ) -> std::string
		{
			std::string Out( Text );
			std::transform( Out.begin() , Out.end() , Out.begin() , []( unsigned char C ) { return static_cast<char>( tolower( C ) ); } );
			return Out;
		}

		// ------------------------------------------------------------ KeyValues (the text .vdf of Steam)
		// The order and the case of the keys are kept: registry.vdf of Linux is written back.
		struct Kv_t
		{
			std::string Key;
			std::string Value;
			bool Block = false;
			std::vector<Kv_t> Children;

			auto Find( std::string_view Name ) const -> const Kv_t*
			{
				const std::string Wanted = Lower( Name );

				for ( const auto& Child : Children )
				{
					if ( Lower( Child.Key ) == Wanted )
						return &Child;
				}

				return nullptr;
			}

			auto Find( std::string_view Name ) -> Kv_t*
			{
				return const_cast<Kv_t*>( static_cast<const Kv_t*>( this )->Find( Name ) );
			}

			// the value of a key, empty - none or a block
			auto Get( std::string_view Name ) const -> std::string
			{
				const Kv_t* pChild = Find( Name );
				return pChild && !pChild->Block ? pChild->Value : std::string();
			}

			auto BlockOf( std::string_view Name ) -> Kv_t&
			{
				Kv_t* pChild = Find( Name );

				if ( !pChild || !pChild->Block )
				{
					Children.push_back( { std::string( Name ) , {} , true , {} } );
					pChild = &Children.back();
				}

				return *pChild;
			}

			auto Set( std::string_view Name , const std::string& Text ) -> void
			{
				Kv_t* pChild = Find( Name );

				if ( !pChild || pChild->Block )
				{
					Children.push_back( { std::string( Name ) , Text , false , {} } );
					return;
				}

				pChild->Value = Text;
			}
		};

		class VdfParser_t
		{
		public:
			explicit VdfParser_t( const std::string& Text ) : m_Text( Text ) {}

			// false - the text is broken (then it is not written back)
			auto Parse( Kv_t& Root ) -> bool
			{
				Root.Block = true;
				return ParseBody( Root , 0 ) && m_End;
			}

		private:
			enum EToken { TOKEN_END , TOKEN_STRING , TOKEN_OPEN , TOKEN_CLOSE };

			auto NextToken( std::string& Out ) -> EToken
			{
				for ( ;; )
				{
					while ( m_Pos < m_Text.size() && static_cast<unsigned char>( m_Text[m_Pos] ) <= ' ' )
						m_Pos++;

					// a comment
					if ( m_Pos + 1 < m_Text.size() && m_Text[m_Pos] == '/' && m_Text[m_Pos + 1] == '/' )
					{
						while ( m_Pos < m_Text.size() && m_Text[m_Pos] != '\n' )
							m_Pos++;

						continue;
					}

					break;
				}

				if ( m_Pos >= m_Text.size() )
					return TOKEN_END;

				const char C = m_Text[m_Pos];

				if ( C == '{' )
				{
					m_Pos++;
					return TOKEN_OPEN;
				}

				if ( C == '}' )
				{
					m_Pos++;
					return TOKEN_CLOSE;
				}

				Out.clear();

				if ( C == '"' )
				{
					m_Pos++;

					while ( m_Pos < m_Text.size() && m_Text[m_Pos] != '"' )
					{
						if ( m_Text[m_Pos] == '\\' && m_Pos + 1 < m_Text.size() )
						{
							const char E = m_Text[m_Pos + 1];
							Out += E == 'n' ? '\n' : E == 't' ? '\t' : E;
							m_Pos += 2;
							continue;
						}

						Out += m_Text[m_Pos++];
					}

					m_Pos++;
					return TOKEN_STRING;
				}

				while ( m_Pos < m_Text.size() && static_cast<unsigned char>( m_Text[m_Pos] ) > ' ' && m_Text[m_Pos] != '{' && m_Text[m_Pos] != '}' )
					Out += m_Text[m_Pos++];

				return TOKEN_STRING;
			}

			auto ParseBody( Kv_t& Node , int Depth ) -> bool
			{
				std::string Key , Value;

				for ( ;; )
				{
					const EToken Token = NextToken( Key );

					if ( Token == TOKEN_END )
					{
						m_End = true;
						return Depth == 0;
					}

					if ( Token == TOKEN_CLOSE )
						return Depth > 0;

					if ( Token != TOKEN_STRING )
						return false;

					const EToken Next = NextToken( Value );

					if ( Next == TOKEN_STRING )
						Node.Children.push_back( { Key , Value , false , {} } );
					else if ( Next == TOKEN_OPEN && Depth < 16 )
					{
						Node.Children.push_back( { Key , {} , true , {} } );

						if ( !ParseBody( Node.Children.back() , Depth + 1 ) )
							return false;
					}
					else
						return false;
				}
			}

			const std::string& m_Text;
			size_t m_Pos = 0;
			bool m_End = false;
		};

		auto ReadTextFile( const fs::path& Path , std::string& Out ) -> bool
		{
			std::ifstream File( Path , std::ios::binary );

			if ( !File )
				return false;

			Out.assign( std::istreambuf_iterator<char>( File ) , std::istreambuf_iterator<char>() );
			return Out.size() < 8 * 1024 * 1024;
		}

		auto ParseVdfFile( const fs::path& Path , Kv_t& Root ) -> bool
		{
			std::string Text;

			if ( !ReadTextFile( Path , Text ) )
				return false;

			return VdfParser_t( Text ).Parse( Root );
		}

		// a node by the keys from the root, nullptr - none
		auto Walk( const Kv_t& Root , std::initializer_list<const char*> Keys ) -> const Kv_t*
		{
			const Kv_t* pNode = &Root;

			for ( const char* szKey : Keys )
			{
				pNode = pNode->Find( szKey );

				if ( !pNode || !pNode->Block )
					return nullptr;
			}

			return pNode;
		}

#ifndef _WIN32
		auto Escape( const std::string& Text ) -> std::string
		{
			std::string Out;

			for ( const char C : Text )
			{
				switch ( C )
				{
					case '"': Out += "\\\""; break;
					case '\\': Out += "\\\\"; break;
					case '\n': Out += "\\n"; break;
					case '\t': Out += "\\t"; break;
					default: Out += C; break;
				}
			}

			return Out;
		}

		// the way Steam writes it: tabs, the key and the value separated by two tabs
		auto Serialize( const Kv_t& Node , int Depth , std::string& Out ) -> void
		{
			const std::string Indent( Depth , '\t' );

			for ( const auto& Child : Node.Children )
			{
				if ( Child.Block )
				{
					Out += Indent + "\"" + Escape( Child.Key ) + "\"\n" + Indent + "{\n";
					Serialize( Child , Depth + 1 , Out );
					Out += Indent + "}\n";
				}
				else
					Out += Indent + "\"" + Escape( Child.Key ) + "\"\t\t\"" + Escape( Child.Value ) + "\"\n";
			}
		}
#endif

		auto DirectoryExists( const fs::path& Path ) -> bool
		{
			std::error_code Ec;
			return fs::is_directory( Path , Ec );
		}

		auto FileExists( const fs::path& Path ) -> bool
		{
			std::error_code Ec;
			return fs::exists( Path , Ec );
		}

		// Steam writes the paths with "/" (and on Windows in the lower case); no closing separator
		auto CleanPath( const fs::path& Path ) -> fs::path
		{
			fs::path Out = Path;
			Out.make_preferred();

			auto Text = Out.native();

			while ( Text.size() > 1 && ( Text.back() == '/' || Text.back() == '\\' ) )
				Text.pop_back();

			return fs::path( Text );
		}

		auto ParseUInt64( const std::string& Text ) -> uint64_t
		{
			return Text.empty() ? 0 : std::strtoull( Text.c_str() , nullptr , 10 );
		}

		auto WaitForExit( const char* szExe , int Milliseconds ) -> bool
		{
			for ( int Waited = 0; Waited < Milliseconds; Waited += 100 )
			{
				if ( Platform::ProcessIds( szExe ).empty() )
					return true;

				std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
			}

			return Platform::ProcessIds( szExe ).empty();
		}

		auto Start( const Info_t& Info , const std::vector<std::string>& Arguments , std::string& Error ) -> bool
		{
			std::vector<std::string> All = Info.SteamArgs;
			All.insert( All.end() , Arguments.begin() , Arguments.end() );
			return Platform::Spawn( Info.SteamExe , All , {} , Error );
		}

#ifdef _WIN32
		// ------------------------------------------------------------ the registry
		auto RegString( HKEY hRoot , const wchar_t* szKey , const wchar_t* szValue ) -> std::wstring
		{
			wchar_t Buffer[1024] = {};
			DWORD Size = sizeof( Buffer );

			if ( RegGetValueW( hRoot , szKey , szValue , RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ , nullptr , Buffer , &Size ) != ERROR_SUCCESS )
				return {};

			return Buffer;
		}

		auto RegDword( HKEY hRoot , const wchar_t* szKey , const wchar_t* szValue ) -> uint32_t
		{
			DWORD Data = 0;
			DWORD Size = sizeof( Data );

			if ( RegGetValueW( hRoot , szKey , szValue , RRF_RT_REG_DWORD , nullptr , &Data , &Size ) != ERROR_SUCCESS )
				return 0;

			return Data;
		}

		auto ReadActiveUser( const Info_t& ) -> uint32_t
		{
			return RegDword( HKEY_CURRENT_USER , L"Software\\Valve\\Steam\\ActiveProcess" , L"ActiveUser" );
		}

		auto WriteAutoLogin( const Info_t& , const std::string& AccountName , std::string& Error ) -> bool
		{
			HKEY hKey = nullptr;

			if ( RegOpenKeyExW( HKEY_CURRENT_USER , L"Software\\Valve\\Steam" , 0 , KEY_SET_VALUE , &hKey ) != ERROR_SUCCESS )
			{
				Error = "нет доступа к HKCU\\Software\\Valve\\Steam";
				return false;
			}

			const std::wstring Name = Wide( AccountName );
			const DWORD Remember = 1;

			const bool Written = RegSetValueExW( hKey , L"AutoLoginUser" , 0 , REG_SZ , reinterpret_cast<const BYTE*>( Name.c_str() ) ,
												 static_cast<DWORD>( ( Name.size() + 1 ) * sizeof( wchar_t ) ) ) == ERROR_SUCCESS &&
				RegSetValueExW( hKey , L"RememberPassword" , 0 , REG_DWORD , reinterpret_cast<const BYTE*>( &Remember ) , sizeof( Remember ) ) == ERROR_SUCCESS;

			RegCloseKey( hKey );

			if ( !Written )
			{
				Error = "не удалось записать AutoLoginUser";
				return false;
			}

			// Steam is closed: ActiveUser of the last session (a crash leaves it) is not taken for the new login
			if ( RegOpenKeyExW( HKEY_CURRENT_USER , L"Software\\Valve\\Steam\\ActiveProcess" , 0 , KEY_SET_VALUE , &hKey ) == ERROR_SUCCESS )
			{
				const DWORD None = 0;
				RegSetValueExW( hKey , L"ActiveUser" , 0 , REG_DWORD , reinterpret_cast<const BYTE*>( &None ) , sizeof( None ) );
				RegCloseKey( hKey );
			}

			return true;
		}
#else
		// ------------------------------------------------------------ registry.vdf
		constexpr const char* kFlatpakId = "com.valvesoftware.Steam";

		auto IsFlatpak( const fs::path& SteamPath ) -> bool
		{
			return SteamPath.native().find( "/.var/app/com.valvesoftware.Steam/" ) != std::string::npos;
		}

		auto IsSnap( const fs::path& SteamPath ) -> bool
		{
			return SteamPath.native().find( "/snap/steam/" ) != std::string::npos;
		}

		// the folder of Steam: the one that has loginusers.vdf, else the one that has steam.sh
		auto FindSteamPath() -> fs::path
		{
			const fs::path Home = Platform::HomeDir();
			const fs::path Dirs[] = {
				Home / ".steam" / "steam" ,
				Home / ".local" / "share" / "Steam" ,
				Home / ".steam" / "root" ,
				Home / ".var" / "app" / kFlatpakId / ".local" / "share" / "Steam" ,
				Home / ".var" / "app" / kFlatpakId / ".steam" / "steam" ,
				Home / "snap" / "steam" / "common" / ".local" / "share" / "Steam" ,
			};

			const auto Resolve = []( const fs::path& Dir )
			{
				std::error_code Ec;
				const fs::path Real = fs::canonical( Dir , Ec );
				return Ec ? Dir : Real;
			};

			for ( const auto& Dir : Dirs )
			{
				if ( FileExists( Dir / "config" / "loginusers.vdf" ) )
					return Resolve( Dir );
			}

			for ( const auto& Dir : Dirs )
			{
				if ( FileExists( Dir / "steam.sh" ) )
					return Resolve( Dir );
			}

			return {};
		}

		auto RegistryFileOf( const fs::path& SteamPath ) -> fs::path
		{
			const fs::path Home = Platform::HomeDir();

			if ( IsFlatpak( SteamPath ) )
				return Home / ".var" / "app" / kFlatpakId / ".steam" / "registry.vdf";

			if ( IsSnap( SteamPath ) )
				return Home / "snap" / "steam" / "common" / ".steam" / "registry.vdf";

			return Home / ".steam" / "registry.vdf";
		}

		// HKCU/Software/Valve/Steam of registry.vdf
		auto ReadSteamKey( const fs::path& RegistryFile , Kv_t& Root ) -> const Kv_t*
		{
			if ( !ParseVdfFile( RegistryFile , Root ) )
				return nullptr;

			return Walk( Root , { "Registry" , "HKCU" , "Software" , "Valve" , "Steam" } );
		}

		auto ReadActiveUser( const Info_t& Info ) -> uint32_t
		{
			Kv_t Root;
			const Kv_t* pSteam = ReadSteamKey( Info.RegistryFile , Root );

			if ( !pSteam )
				return 0;

			const Kv_t* pProcess = pSteam->Find( "ActiveProcess" );
			return pProcess && pProcess->Block ? static_cast<uint32_t>( ParseUInt64( pProcess->Get( "ActiveUser" ) ) ) : 0;
		}

		// Steam is closed: it writes the file on its exit and would put its own values back
		auto WriteAutoLogin( const Info_t& Info , const std::string& AccountName , std::string& Error ) -> bool
		{
			const fs::path& File = Info.RegistryFile;
			Kv_t Root;
			Root.Block = true;

			if ( FileExists( File ) && !ParseVdfFile( File , Root ) )
			{
				Error = "не удалось разобрать " + PathText( File ) + ", файл не тронут";
				return false;
			}

			Kv_t* pNode = &Root;

			for ( const char* szKey : { "Registry" , "HKCU" , "Software" , "Valve" , "Steam" } )
				pNode = &pNode->BlockOf( szKey );

			pNode->Set( "AutoLoginUser" , AccountName );
			pNode->Set( "RememberPassword" , "1" );

			// the file keeps ActiveUser of the last session: it is not taken for the new login
			if ( Kv_t* pProcess = pNode->Find( "ActiveProcess" ); pProcess && pProcess->Block )
				pProcess->Set( "ActiveUser" , "0" );

			std::string Text;
			Serialize( Root , 0 , Text );

			std::error_code Ec;
			fs::create_directories( File.parent_path() , Ec );

			// the copy of the last one before the panel wrote it
			if ( FileExists( File ) )
				fs::copy_file( File , fs::path( File ).concat( ".bak" ) , fs::copy_options::overwrite_existing , Ec );

			const fs::path Temp = fs::path( File ).concat( ".tmp" );

			{
				std::ofstream Out( Temp , std::ios::binary | std::ios::trunc );
				Out << Text;

				if ( !Out )
				{
					Error = "не удалось записать " + PathText( Temp );
					return false;
				}
			}

			fs::rename( Temp , File , Ec );

			if ( Ec )
			{
				Error = "не удалось записать " + PathText( File ) + ": " + Ec.message();
				return false;
			}

			return true;
		}
#endif

		// Steam has written loginusers.vdf after the login: the account is the most recent one and its time is after the start
		auto LoggedInByFile( const fs::path& SteamPath , uint32_t AccountId , int64_t Since ) -> bool
		{
			for ( const auto& Account : ReadAccounts( SteamPath ) )
			{
				if ( Account.AccountId == AccountId )
					return Account.MostRecent && Account.Timestamp >= Since;
			}

			return false;
		}
	}

#ifdef _WIN32
	auto Utf8( const std::wstring& Text ) -> std::string
	{
		if ( Text.empty() )
			return {};

		const int Size = WideCharToMultiByte( CP_UTF8 , 0 , Text.c_str() , static_cast<int>( Text.size() ) , nullptr , 0 , nullptr , nullptr );
		std::string Out( Size , '\0' );
		WideCharToMultiByte( CP_UTF8 , 0 , Text.c_str() , static_cast<int>( Text.size() ) , Out.data() , Size , nullptr , nullptr );
		return Out;
	}

	auto Wide( const std::string& Text ) -> std::wstring
	{
		if ( Text.empty() )
			return {};

		const int Size = MultiByteToWideChar( CP_UTF8 , 0 , Text.c_str() , static_cast<int>( Text.size() ) , nullptr , 0 );
		std::wstring Out( Size , L'\0' );
		MultiByteToWideChar( CP_UTF8 , 0 , Text.c_str() , static_cast<int>( Text.size() ) , Out.data() , Size );
		return Out;
	}

	auto PathText( const fs::path& Path ) -> std::string
	{
		return Utf8( Path.native() );
	}

	auto PathFrom( const std::string& Text ) -> fs::path
	{
		return fs::path( Wide( Text ) );
	}
#else
	// wchar_t is UTF-32 here
	auto Utf8( const std::wstring& Text ) -> std::string
	{
		std::string Out;

		for ( const wchar_t W : Text )
		{
			const auto C = static_cast<uint32_t>( W );

			if ( C < 0x80 )
				Out += static_cast<char>( C );
			else if ( C < 0x800 )
			{
				Out += static_cast<char>( 0xC0 | ( C >> 6 ) );
				Out += static_cast<char>( 0x80 | ( C & 0x3F ) );
			}
			else if ( C < 0x10000 )
			{
				Out += static_cast<char>( 0xE0 | ( C >> 12 ) );
				Out += static_cast<char>( 0x80 | ( ( C >> 6 ) & 0x3F ) );
				Out += static_cast<char>( 0x80 | ( C & 0x3F ) );
			}
			else
			{
				Out += static_cast<char>( 0xF0 | ( C >> 18 ) );
				Out += static_cast<char>( 0x80 | ( ( C >> 12 ) & 0x3F ) );
				Out += static_cast<char>( 0x80 | ( ( C >> 6 ) & 0x3F ) );
				Out += static_cast<char>( 0x80 | ( C & 0x3F ) );
			}
		}

		return Out;
	}

	auto Wide( const std::string& Text ) -> std::wstring
	{
		std::wstring Out;

		for ( size_t i = 0; i < Text.size(); )
		{
			const auto B = static_cast<unsigned char>( Text[i] );
			const int Extra = B >= 0xF0 ? 3 : B >= 0xE0 ? 2 : B >= 0xC0 ? 1 : 0;
			uint32_t C = Extra == 3 ? B & 0x07 : Extra == 2 ? B & 0x0F : Extra == 1 ? B & 0x1F : B;

			if ( i + Extra >= Text.size() )
				break;

			for ( int k = 1; k <= Extra; k++ )
				C = ( C << 6 ) | ( static_cast<unsigned char>( Text[i + k] ) & 0x3F );

			Out += static_cast<wchar_t>( C );
			i += 1 + Extra;
		}

		return Out;
	}

	auto PathText( const fs::path& Path ) -> std::string
	{
		return Path.native();
	}

	auto PathFrom( const std::string& Text ) -> fs::path
	{
		return fs::path( Text );
	}
#endif

	auto ReadInfo() -> Info_t
	{
		Info_t Info;

#ifdef _WIN32
		Info.SteamPath = CleanPath( RegString( HKEY_CURRENT_USER , L"Software\\Valve\\Steam" , L"SteamPath" ) );
		Info.SteamExe = CleanPath( RegString( HKEY_CURRENT_USER , L"Software\\Valve\\Steam" , L"SteamExe" ) );
		Info.AutoLoginUser = Utf8( RegString( HKEY_CURRENT_USER , L"Software\\Valve\\Steam" , L"AutoLoginUser" ) );
		Info.ActiveUser = ReadActiveUser( Info );

		if ( Info.SteamExe.empty() && !Info.SteamPath.empty() )
			Info.SteamExe = Info.SteamPath / "steam.exe";
#else
		Info.SteamPath = FindSteamPath();
		Info.RegistryFile = RegistryFileOf( Info.SteamPath );

		if ( IsFlatpak( Info.SteamPath ) )
		{
			Info.SteamExe = Platform::FindInPath( "flatpak" );
			Info.SteamArgs = { "run" , kFlatpakId };
		}
		else if ( IsSnap( Info.SteamPath ) )
		{
			Info.SteamExe = Platform::FindInPath( "snap" );
			Info.SteamArgs = { "run" , "steam" };
		}
		else
		{
			Info.SteamExe = Platform::FindInPath( "steam" );

			if ( Info.SteamExe.empty() && FileExists( Info.SteamPath / "steam.sh" ) )
				Info.SteamExe = Info.SteamPath / "steam.sh";
		}

		Kv_t Root;

		if ( const Kv_t* pSteam = ReadSteamKey( Info.RegistryFile , Root ) )
			Info.AutoLoginUser = pSteam->Get( "AutoLoginUser" );

		// the file keeps the value of the last session after the exit of Steam
		Info.ActiveUser = IsSteamRunning() ? ReadActiveUser( Info ) : 0;
#endif

		return Info;
	}

	auto ReadAccounts( const fs::path& SteamPath ) -> std::vector<Account_t>
	{
		std::vector<Account_t> Accounts;
		Kv_t Root;

		if ( SteamPath.empty() )
			return Accounts;

		// a broken end of the file still gives the accounts before it
		ParseVdfFile( SteamPath / "config" / "loginusers.vdf" , Root );

		const Kv_t* pUsers = Walk( Root , { "users" } );

		if ( !pUsers )
			return Accounts;

		for ( const auto& User : pUsers->Children )
		{
			if ( !User.Block )
				continue;

			Account_t Account;
			Account.SteamId = ParseUInt64( User.Key );

			if ( Account.SteamId <= kSteamId64Base )
				continue;

			Account.AccountId = static_cast<uint32_t>( Account.SteamId - kSteamId64Base );
			Account.AccountName = User.Get( "AccountName" );
			Account.PersonaName = User.Get( "PersonaName" );
			Account.RememberPassword = User.Get( "RememberPassword" ) == "1";
			Account.MostRecent = User.Get( "MostRecent" ) == "1";
			Account.Timestamp = static_cast<int64_t>( ParseUInt64( User.Get( "Timestamp" ) ) );

			if ( !Account.AccountName.empty() )
				Accounts.push_back( std::move( Account ) );
		}

		return Accounts;
	}

	auto FindCitadelDir( const fs::path& SteamPath ) -> fs::path
	{
		const auto FromGame = []( const fs::path& GameDir ) -> fs::path
		{
			const fs::path Citadel = CleanPath( GameDir ) / "game" / "citadel";
			return DirectoryExists( Citadel ) ? Citadel : fs::path();
		};

#ifdef _WIN32
		// the uninstall entry Steam makes for the app
		const std::wstring Install = RegString( HKEY_LOCAL_MACHINE , L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 1422450" ,
												L"InstallLocation" );

		if ( !Install.empty() )
		{
			if ( auto Dir = FromGame( Install ); !Dir.empty() )
				return Dir;
		}
#endif

		// the libraries of Steam: the one that has the app
		Kv_t Root;

		if ( !SteamPath.empty() && ParseVdfFile( SteamPath / "steamapps" / "libraryfolders.vdf" , Root ) )
		{
			if ( const Kv_t* pFolders = Walk( Root , { "libraryfolders" } ) )
			{
				for ( const auto& Folder : pFolders->Children )
				{
					const Kv_t* pApps = Folder.Find( "apps" );

					if ( !Folder.Block || !pApps || !pApps->Find( std::to_string( kDeadlockAppId ) ) )
						continue;

					if ( auto Dir = FromGame( PathFrom( Folder.Get( "path" ) ) / "steamapps" / "common" / "Deadlock" ); !Dir.empty() )
						return Dir;
				}
			}
		}

		if ( !SteamPath.empty() )
		{
			if ( auto Dir = FromGame( SteamPath / "steamapps" / "common" / "Deadlock" ); !Dir.empty() )
				return Dir;
		}

		return {};
	}

	auto IsSteamRunning() -> bool
	{
		return !Platform::ProcessIds( kSteamProcess ).empty();
	}

	auto IsGameRunning() -> bool
	{
		return !Platform::ProcessIds( kGameProcess ).empty();
	}

	auto AvatarPath( const fs::path& SteamPath , uint64_t SteamId ) -> fs::path
	{
		if ( SteamPath.empty() || !SteamId )
			return {};

		const fs::path Path = SteamPath / "config" / "avatarcache" / ( std::to_string( SteamId ) + ".png" );
		return FileExists( Path ) ? Path : fs::path();
	}

	auto CloseSteam( const Info_t& Info , const SwitchCallbacks_t& Callbacks , std::string& Error ) -> bool
	{
		if ( !IsSteamRunning() )
			return true;

		if ( Callbacks.Status )
			Callbacks.Status( "Закрываю Steam…" );

		std::string Ignored;
		Start( Info , { "-shutdown" } , Ignored );

		if ( !WaitForExit( kSteamProcess , 15000 ) )
		{
			if ( !Callbacks.AskKill || !Callbacks.AskKill() )
			{
				Error = "Steam не закрылся";
				return false;
			}

			if ( Callbacks.Status )
				Callbacks.Status( "Завершаю процесс Steam…" );

			for ( const auto Id : Platform::ProcessIds( kSteamProcess ) )
				Platform::Terminate( Id , false );

			if ( !WaitForExit( kSteamProcess , 3000 ) )
			{
				for ( const auto Id : Platform::ProcessIds( kSteamProcess ) )
					Platform::Terminate( Id , true );
			}

			if ( !WaitForExit( kSteamProcess , 5000 ) )
			{
				Error = std::string( "не удалось завершить " ) + kSteamProcess;
				return false;
			}
		}

		// the helper processes go away a moment later
		std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
		return true;
	}

	auto SwitchAccount( const Info_t& Info , const Account_t& Account , EStart Mode , const SwitchCallbacks_t& Callbacks , std::string& Error ) -> bool
	{
		const auto Status = [&]( const char* szText )
		{
			if ( Callbacks.Status )
				Callbacks.Status( szText );
		};

		if ( Info.SteamExe.empty() || !FileExists( Info.SteamExe ) )
		{
#ifdef _WIN32
			Error = "steam.exe не найден (HKCU\\Software\\Valve\\Steam\\SteamExe)";
#else
			Error = "Steam не найден: нет steam в PATH и steam.sh в папке Steam";
#endif
			return false;
		}

		// 1. Steam closes by its own command, then it is waited for
		if ( !CloseSteam( Info , Callbacks , Error ) )
		{
			Error += ", переключение отменено";
			return false;
		}

		// 2. the account Steam logs into on the start
		Status( "Записываю аккаунт для автологина…" );

		if ( !WriteAutoLogin( Info , Account.AccountName , Error ) )
			return false;

		// 3. Steam again; with the game it launches Deadlock after the login
		std::vector<std::string> Arguments;

		switch ( Mode )
		{
			case START_GAME:
				Status( "Запускаю Steam и Deadlock…" );
				Arguments = { "-applaunch" , std::to_string( kDeadlockAppId ) };
				break;
			case START_SILENT:
				Status( "Запускаю Steam в фоне (без окна)…" );
				Arguments = { "-silent" };
				break;
			case START_LOGIN:
				Status( "Вхожу в аккаунт (Steam без окна)…" );
				Arguments = { "-silent" , "-nofriendsui" , "-nochatui" , "-noverifyfiles" };
				break;
			default:
				Status( "Запускаю Steam…" );
				break;
		}

		if ( std::string StartError; !Start( Info , Arguments , StartError ) )
		{
			Error = "не удалось запустить Steam: " + StartError;
			return false;
		}

		return true;
	}

	auto WaitForLogin( const Info_t& Info , uint32_t AccountId , int Milliseconds ) -> bool
	{
		const int64_t Since = static_cast<int64_t>( std::time( nullptr ) ) - 2;

		for ( int Waited = 0; Waited < Milliseconds; Waited += 250 )
		{
			if ( IsSteamRunning() && ( ReadActiveUser( Info ) == AccountId || LoggedInByFile( Info.SteamPath , AccountId , Since ) ) )
				return true;

			std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
		}

		return false;
	}

	auto LaunchGame( const Info_t& Info ) -> bool
	{
		const std::string Url = "steam://rungameid/" + std::to_string( kDeadlockAppId );

#ifdef _WIN32
		( void )Info;
		const auto Result = reinterpret_cast<INT_PTR>( ShellExecuteW( nullptr , L"open" , Wide( Url ).c_str() , nullptr , nullptr , SW_SHOWNORMAL ) );
		return Result > 32;
#else
		// a running Steam takes the link from the new one
		std::string Error;
		return !Info.SteamExe.empty() && Start( Info , { Url } , Error );
#endif
	}
}
