#include "Platform.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <io.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace Platform
{
	namespace
	{
		auto SameText( const std::string& A , const std::string& B ) -> bool
		{
			if ( A.size() != B.size() )
				return false;

			for ( size_t i = 0; i < A.size(); i++ )
			{
				if ( tolower( static_cast<unsigned char>( A[i] ) ) != tolower( static_cast<unsigned char>( B[i] ) ) )
					return false;
			}

			return true;
		}

#ifdef _WIN32
		auto Wide( const std::string& Text ) -> std::wstring
		{
			if ( Text.empty() )
				return {};

			const int Size = MultiByteToWideChar( CP_UTF8 , 0 , Text.c_str() , static_cast<int>( Text.size() ) , nullptr , 0 );
			std::wstring Out( Size , L'\0' );
			MultiByteToWideChar( CP_UTF8 , 0 , Text.c_str() , static_cast<int>( Text.size() ) , Out.data() , Size );
			return Out;
		}

		auto Utf8( const std::wstring& Text ) -> std::string
		{
			if ( Text.empty() )
				return {};

			const int Size = WideCharToMultiByte( CP_UTF8 , 0 , Text.c_str() , static_cast<int>( Text.size() ) , nullptr , 0 , nullptr , nullptr );
			std::string Out( Size , '\0' );
			WideCharToMultiByte( CP_UTF8 , 0 , Text.c_str() , static_cast<int>( Text.size() ) , Out.data() , Size , nullptr , nullptr );
			return Out;
		}

		// a word of the command line, the way CommandLineToArgvW reads it back
		auto Quote( const std::wstring& Argument ) -> std::wstring
		{
			if ( !Argument.empty() && Argument.find_first_of( L" \t\"" ) == std::wstring::npos )
				return Argument;

			std::wstring Out = L"\"";
			size_t Slashes = 0;

			for ( const wchar_t C : Argument )
			{
				if ( C == L'\\' )
				{
					Slashes++;
					continue;
				}

				Out.append( C == L'"' ? Slashes * 2 + 1 : Slashes , L'\\' );
				Slashes = 0;
				Out += C;
			}

			Out.append( Slashes * 2 , L'\\' );
			Out += L'"';
			return Out;
		}

		auto ErrorText( DWORD Code ) -> std::string
		{
			wchar_t* szText = nullptr;
			FormatMessageW( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS , nullptr , Code , 0 ,
							reinterpret_cast<LPWSTR>( &szText ) , 0 , nullptr );

			std::string Text = szText ? Utf8( szText ) : std::string();

			if ( szText )
				LocalFree( szText );

			while ( !Text.empty() && ( Text.back() == '\r' || Text.back() == '\n' || Text.back() == ' ' || Text.back() == '.' ) )
				Text.pop_back();

			return Text.empty() ? "код " + std::to_string( Code ) : Text;
		}
#else
		// the start of a file up to the delimiter
		auto ReadFirst( const std::string& Path , char Delimiter ) -> std::string
		{
			std::ifstream File( Path , std::ios::binary );
			std::string Text;

			if ( File )
				std::getline( File , Text , Delimiter );

			return Text;
		}

		auto BaseName( const std::string& Path ) -> std::string
		{
			const size_t Slash = Path.find_last_of( "/\\" );
			return Slash == std::string::npos ? Path : Path.substr( Slash + 1 );
		}

		auto WriteAll( int Fd , const void* pData , size_t Size ) -> void
		{
			const auto* p = static_cast<const char*>( pData );

			while ( Size > 0 )
			{
				const ssize_t Written = write( Fd , p , Size );

				if ( Written < 0 && errno == EINTR )
					continue;

				if ( Written <= 0 )
					return;

				p += Written;
				Size -= static_cast<size_t>( Written );
			}
		}
#endif
	}

#ifdef _WIN32
	auto ProcessIds( const std::string& Name ) -> std::vector<Pid_t>
	{
		std::vector<Pid_t> Ids;
		HANDLE hSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS , 0 );

		if ( hSnapshot == INVALID_HANDLE_VALUE )
			return Ids;

		const std::wstring WideName = Wide( Name );
		PROCESSENTRY32W Entry = { sizeof( Entry ) };

		for ( BOOL Ok = Process32FirstW( hSnapshot , &Entry ); Ok; Ok = Process32NextW( hSnapshot , &Entry ) )
		{
			if ( _wcsicmp( Entry.szExeFile , WideName.c_str() ) == 0 )
				Ids.push_back( Entry.th32ProcessID );
		}

		CloseHandle( hSnapshot );
		return Ids;
	}

	auto Terminate( Pid_t Id , bool ) -> bool
	{
		HANDLE hProcess = OpenProcess( PROCESS_TERMINATE , FALSE , Id );

		if ( !hProcess )
			return false;

		const bool Ok = TerminateProcess( hProcess , 0 ) != FALSE;
		CloseHandle( hProcess );
		return Ok;
	}

	auto Spawn( const fs::path& Program , const std::vector<std::string>& Arguments , const fs::path& Directory , std::string& Error ) -> bool
	{
		std::wstring Line;

		for ( const auto& Argument : Arguments )
		{
			if ( !Line.empty() )
				Line += L' ';

			Line += Quote( Wide( Argument ) );
		}

		SHELLEXECUTEINFOW Info = { sizeof( Info ) };
		Info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
		Info.lpVerb = L"open";
		Info.lpFile = Program.c_str();
		Info.lpParameters = Line.empty() ? nullptr : Line.c_str();
		Info.lpDirectory = Directory.empty() ? nullptr : Directory.c_str();
		Info.nShow = SW_SHOWNORMAL;

		if ( ShellExecuteExW( &Info ) )
			return true;

		Error = LastErrorText();
		return false;
	}

	auto FindInPath( const std::string& Name ) -> fs::path
	{
		wchar_t Buffer[MAX_PATH] = {};
		const DWORD Size = SearchPathW( nullptr , Wide( Name ).c_str() , L".exe" , MAX_PATH , Buffer , nullptr );
		return Size && Size < MAX_PATH ? fs::path( Buffer ) : fs::path();
	}

	auto ExeDir() -> fs::path
	{
		std::wstring Path( 32768 , L'\0' );
		const DWORD Size = GetModuleFileNameW( nullptr , Path.data() , static_cast<DWORD>( Path.size() ) );
		Path.resize( Size );
		return fs::path( Path ).parent_path();
	}

	auto HomeDir() -> fs::path
	{
		return fs::path( Wide( Env( "USERPROFILE" ) ) );
	}

	auto Env( const char* szName ) -> std::string
	{
		const std::wstring Name = Wide( szName );
		const DWORD Size = GetEnvironmentVariableW( Name.c_str() , nullptr , 0 );

		if ( !Size )
			return {};

		std::wstring Value( Size , L'\0' );
		const DWORD Written = GetEnvironmentVariableW( Name.c_str() , Value.data() , Size );
		Value.resize( Written < Size ? Written : 0 );
		return Utf8( Value );
	}

	auto IsTerminal() -> bool
	{
		return _isatty( _fileno( stdin ) ) != 0;
	}

	auto LocalTime( int64_t Unix , std::tm& Out ) -> bool
	{
		const std::time_t Time = static_cast<std::time_t>( Unix );
		return localtime_s( &Out , &Time ) == 0;
	}

	auto LastErrorText() -> std::string
	{
		const DWORD Code = GetLastError();
		return Code == ERROR_CANCELLED ? "отменено пользователем" : ErrorText( Code );
	}
#else
	auto ProcessIds( const std::string& Name ) -> std::vector<Pid_t>
	{
		std::vector<Pid_t> Ids;
		DIR* pDir = opendir( "/proc" );

		if ( !pDir )
			return Ids;

		const uid_t Uid = getuid();
		const pid_t Self = getpid();

		// the comm is cut to 15 characters
		const std::string Comm = Name.substr( 0 , 15 );

		while ( const dirent* pEntry = readdir( pDir ) )
		{
			char* pEnd = nullptr;
			const long Id = strtol( pEntry->d_name , &pEnd , 10 );

			if ( Id <= 0 || *pEnd || Id == Self )
				continue;

			const std::string Dir = std::string( "/proc/" ) + pEntry->d_name;
			struct stat Info = {};

			if ( stat( Dir.c_str() , &Info ) != 0 || Info.st_uid != Uid )
				continue;

			// Wine sets the comm to the name of the exe; argv[0] of it may be a path of Windows
			if ( SameText( ReadFirst( Dir + "/comm" , '\n' ) , Comm ) || SameText( BaseName( ReadFirst( Dir + "/cmdline" , '\0' ) ) , Name ) )
				Ids.push_back( static_cast<Pid_t>( Id ) );
		}

		closedir( pDir );
		return Ids;
	}

	auto Terminate( Pid_t Id , bool Force ) -> bool
	{
		return kill( static_cast<pid_t>( Id ) , Force ? SIGKILL : SIGTERM ) == 0;
	}

	auto Spawn( const fs::path& Program , const std::vector<std::string>& Arguments , const fs::path& Directory , std::string& Error ) -> bool
	{
		std::vector<std::string> Argv;
		Argv.push_back( Program.string() );
		Argv.insert( Argv.end() , Arguments.begin() , Arguments.end() );

		std::vector<char*> Pointers;

		for ( auto& Argument : Argv )
			Pointers.push_back( Argument.data() );

		Pointers.push_back( nullptr );

		// the errno of a failed exec comes back through the pipe; the pipe closes by itself on a successful one
		int Pipe[2];

		if ( pipe2( Pipe , O_CLOEXEC ) != 0 )
		{
			Error = LastErrorText();
			return false;
		}

		const pid_t Child = fork();

		if ( Child < 0 )
		{
			Error = LastErrorText();
			close( Pipe[0] );
			close( Pipe[1] );
			return false;
		}

		if ( Child == 0 )
		{
			// the child: a session of its own; the grandchild is the program (it is not a child of the panel, nothing waits for it)
			close( Pipe[0] );
			setsid();

			const pid_t Grandchild = fork();

			if ( Grandchild != 0 )
			{
				if ( Grandchild < 0 )
				{
					const int Code = errno;
					WriteAll( Pipe[1] , &Code , sizeof( Code ) );
				}

				_exit( 0 );
			}

			if ( !Directory.empty() && chdir( Directory.c_str() ) != 0 )
			{
				const int Code = errno;
				WriteAll( Pipe[1] , &Code , sizeof( Code ) );
				_exit( 127 );
			}

			const int Null = open( "/dev/null" , O_RDWR );

			if ( Null >= 0 )
			{
				dup2( Null , 0 );
				dup2( Null , 1 );
				dup2( Null , 2 );

				if ( Null > 2 )
					close( Null );
			}

			signal( SIGPIPE , SIG_DFL );
			execvp( Pointers[0] , Pointers.data() );

			const int Code = errno;
			WriteAll( Pipe[1] , &Code , sizeof( Code ) );
			_exit( 127 );
		}

		close( Pipe[1] );

		int Status = 0;

		while ( waitpid( Child , &Status , 0 ) < 0 && errno == EINTR )
		{
		}

		int Code = 0;
		ssize_t Read = 0;

		do
			Read = read( Pipe[0] , &Code , sizeof( Code ) );
		while ( Read < 0 && errno == EINTR );

		close( Pipe[0] );

		if ( Read == sizeof( Code ) )
		{
			Error = strerror( Code );
			return false;
		}

		return true;
	}

	auto FindInPath( const std::string& Name ) -> fs::path
	{
		const std::string Path = Env( "PATH" );
		size_t Start = 0;

		while ( Start <= Path.size() )
		{
			size_t End = Path.find( ':' , Start );

			if ( End == std::string::npos )
				End = Path.size();

			const std::string Dir = End > Start ? Path.substr( Start , End - Start ) : std::string( "." );
			const std::string File = Dir + "/" + Name;

			if ( access( File.c_str() , X_OK ) == 0 )
			{
				std::error_code Ec;

				if ( fs::is_regular_file( File , Ec ) )
					return fs::path( File );
			}

			Start = End + 1;
		}

		return {};
	}

	auto ExeDir() -> fs::path
	{
		std::error_code Ec;
		const fs::path Exe = fs::read_symlink( "/proc/self/exe" , Ec );
		return Ec ? fs::current_path( Ec ) : Exe.parent_path();
	}

	auto HomeDir() -> fs::path
	{
		const std::string Home = Env( "HOME" );

		if ( !Home.empty() )
			return fs::path( Home );

		const passwd* pUser = getpwuid( getuid() );
		return pUser && pUser->pw_dir ? fs::path( pUser->pw_dir ) : fs::path();
	}

	auto Env( const char* szName ) -> std::string
	{
		const char* szValue = std::getenv( szName );
		return szValue ? szValue : "";
	}

	auto IsTerminal() -> bool
	{
		return isatty( 0 ) != 0;
	}

	auto LocalTime( int64_t Unix , std::tm& Out ) -> bool
	{
		const std::time_t Time = static_cast<std::time_t>( Unix );
		return localtime_r( &Time , &Out ) != nullptr;
	}

	auto LastErrorText() -> std::string
	{
		return strerror( errno );
	}
#endif
}
