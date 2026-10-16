#include "Launch.hpp"

#include "Platform.hpp"
#include "Steam.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Launch
{
	namespace fs = std::filesystem;

	namespace
	{
#ifdef _WIN32
		constexpr char kSep = '\\';
#else
		constexpr char kSep = '/';
#endif

		auto IsSpace( char C ) -> bool
		{
			return C == ' ' || C == '\t' || C == '\r' || C == '\n';
		}

		auto IsQuote( char C ) -> bool
		{
#ifdef _WIN32
			return C == '"';
#else
			return C == '"' || C == '\'';
#endif
		}

		auto Trim( const std::string& Text ) -> std::string
		{
			size_t Start = 0 , End = Text.size();

			while ( Start < End && IsSpace( Text[Start] ) )
				Start++;

			while ( End > Start && IsSpace( Text[End - 1] ) )
				End--;

			return Text.substr( Start , End - Start );
		}

		auto Expand( const std::string& Text ) -> std::string
		{
#ifdef _WIN32
			if ( Text.find( '%' ) == std::string::npos )
				return Text;

			const std::wstring Wide = Steam::Wide( Text );
			const DWORD Size = ExpandEnvironmentStringsW( Wide.c_str() , nullptr , 0 );

			if ( !Size )
				return Text;

			std::wstring Out( Size , L'\0' );
			const DWORD Written = ExpandEnvironmentStringsW( Wide.c_str() , Out.data() , Size );

			if ( !Written || Written > Size )
				return Text;

			Out.resize( Written - 1 );
			return Steam::Utf8( Out );
#else
			// $NAME and ${NAME}, as sh: not inside '...' and not after a backslash; an unknown one is empty
			std::string Out;
			bool Single = false;

			for ( size_t i = 0; i < Text.size(); i++ )
			{
				const char C = Text[i];

				if ( C == '\'' )
					Single = !Single;

				if ( C == '\\' && !Single && i + 1 < Text.size() )
				{
					Out += C;
					Out += Text[++i];
					continue;
				}

				if ( C != '$' || Single || i + 1 >= Text.size() )
				{
					Out += C;
					continue;
				}

				std::string Name;

				if ( Text[i + 1] == '{' )
				{
					const size_t End = Text.find( '}' , i + 2 );

					if ( End == std::string::npos )
					{
						Out += C;
						continue;
					}

					Name = Text.substr( i + 2 , End - i - 2 );
					i = End;
				}
				else
				{
					size_t End = i + 1;

					while ( End < Text.size() && ( isalnum( static_cast<unsigned char>( Text[End] ) ) || Text[End] == '_' ) )
						End++;

					if ( End == i + 1 )
					{
						Out += C;
						continue;
					}

					Name = Text.substr( i + 1 , End - i - 1 );
					i = End - 1;
				}

				Out += Platform::Env( Name.c_str() );
			}

			return Out;
#endif
		}

		auto IsFile( const std::string& Path ) -> bool
		{
			std::error_code Ec;
			return !Path.empty() && fs::is_regular_file( Steam::PathFrom( Path ) , Ec );
		}

		auto FullPath( const std::string& Path ) -> std::string
		{
			std::error_code Ec;
			const fs::path Full = fs::absolute( Steam::PathFrom( Path ) , Ec );
			return Ec ? Path : Steam::PathText( Full.lexically_normal() );
		}

		auto Separators( std::string Path ) -> std::string
		{
#ifdef _WIN32
			std::replace( Path.begin() , Path.end() , '/' , '\\' );
#endif
			return Path;
		}

		// "./x", "../x" (after Separators)
		auto IsDotRelative( const std::string& Path ) -> bool
		{
			return Path.rfind( std::string( "." ) + kSep , 0 ) == 0 || Path.rfind( std::string( ".." ) + kSep , 0 ) == 0;
		}

		auto IsAbsolute( const std::string& Path ) -> bool
		{
#ifdef _WIN32
			return ( Path.size() >= 2 && Path[1] == ':' ) || Path.rfind( "\\" , 0 ) == 0;
#else
			return Path.rfind( "/" , 0 ) == 0;
#endif
		}

		// Linux: "~/x" -> the home folder; empty - not such a path
		auto HomePath( const std::string& Path ) -> std::string
		{
#ifdef _WIN32
			( void )Path;
			return {};
#else
			if ( Path != "~" && Path.rfind( "~/" , 0 ) != 0 )
				return {};

			return Steam::PathText( Platform::HomeDir() ) + Path.substr( 1 );
#endif
		}

		// Windows: "loader" -> "loader.exe" when only that one is there
		auto WithExe( const std::string& Path ) -> std::string
		{
#ifdef _WIN32
			return !IsFile( Path ) && IsFile( Path + ".exe" ) ? Path + ".exe" : Path;
#else
			return Path;
#endif
		}

		// the path of the program: the dot ones and the relative ones with a folder are from Base, a bare name too when it is there
		auto ResolvePath( const std::string& Written , const std::string& Base ) -> std::string
		{
			std::string Path = Separators( Written );

			if ( const std::string Home = HomePath( Path ); !Home.empty() )
				Path = Home;

			if ( Path.empty() || IsAbsolute( Path ) )
				return WithExe( Path );

			const std::string Joined = WithExe( FullPath( Base + kSep + Path ) );

			if ( IsDotRelative( Path ) || Path.find( kSep ) != std::string::npos || IsFile( Joined ) )
				return Joined;

			return Path;
		}

		auto IsProgram( const std::string& Written , const std::string& Base ) -> bool
		{
			return IsFile( ResolvePath( Expand( Written ) , Base ) );
		}

		// the program and the rest of the line, as they are written
		auto Split( const std::string& Line , const std::string& Base , std::string& Program , std::string& Arguments ) -> void
		{
			Program.clear();
			Arguments.clear();

			if ( Line.empty() )
				return;

			if ( IsQuote( Line[0] ) )
			{
				const size_t End = Line.find( Line[0] , 1 );
				Program = Line.substr( 1 , End == std::string::npos ? std::string::npos : End - 1 );
				Arguments = End == std::string::npos ? std::string() : Trim( Line.substr( End + 1 ) );
				return;
			}

			// without the quotes: the shortest start of the line up to a space that is a file
			for ( size_t Space = Line.find( ' ' ); Space != std::string::npos; Space = Line.find( ' ' , Space + 1 ) )
			{
				if ( IsProgram( Line.substr( 0 , Space ) , Base ) )
				{
					Program = Line.substr( 0 , Space );
					Arguments = Trim( Line.substr( Space + 1 ) );
					return;
				}
			}

			if ( IsProgram( Line , Base ) )
			{
				Program = Line;
				return;
			}

			// nothing is there: the first word (a name from the PATH, or a file that is missing)
			const size_t Space = Line.find_first_of( " \t" );
			Program = Line.substr( 0 , Space );
			Arguments = Space == std::string::npos ? std::string() : Trim( Line.substr( Space + 1 ) );
		}

		// an argument: a "./" path (the whole of it or after "=") becomes full, in quotes when it has spaces
		auto ResolveArgument( const std::string& Token , const std::string& Base ) -> std::string
		{
			const size_t Equals = Token.find( '=' );
			const std::string Head = Equals != std::string::npos ? Token.substr( 0 , Equals + 1 ) : std::string();
			const std::string Value = Equals != std::string::npos ? Token.substr( Equals + 1 ) : Token;

			const bool Quoted = Value.size() >= 2 && IsQuote( Value.front() ) && Value.back() == Value.front();
			const std::string Path = Separators( Quoted ? Value.substr( 1 , Value.size() - 2 ) : Value );
			std::string Full = HomePath( Path );

			if ( Full.empty() )
			{
				if ( !IsDotRelative( Path ) )
					return Token;

				Full = FullPath( Base + kSep + Path );
			}

			// "./folder/" stays a folder
			if ( Path.back() == kSep && Full.back() != kSep )
				Full += kSep;

			if ( Quoted || Full.find( ' ' ) != std::string::npos )
			{
#ifdef _WIN32
				// a closing backslash would escape the quote for the parser of the arguments
				if ( Full.back() == '\\' )
					Full += '\\';

				Full = "\"" + Full + "\"";
#else
				std::string Escaped;

				for ( const char C : Full )
				{
					if ( C == '"' || C == '\\' || C == '$' || C == '`' )
						Escaped += '\\';

					Escaped += C;
				}

				Full = "\"" + Escaped + "\"";
#endif
			}

			return Head + Full;
		}

		// the words of the arguments: the spaces inside the quotes stay
		auto ResolveArguments( const std::string& Text , const std::string& Base ) -> std::string
		{
			std::string Out;
			size_t I = 0;

			while ( I < Text.size() )
			{
				while ( I < Text.size() && IsSpace( Text[I] ) )
					I++;

				if ( I >= Text.size() )
					break;

				const size_t Start = I;
				char Quote = 0;

				while ( I < Text.size() && ( Quote || !IsSpace( Text[I] ) ) )
				{
					if ( Quote ? Text[I] == Quote : IsQuote( Text[I] ) )
						Quote = Quote ? 0 : Text[I];

					I++;
				}

				if ( !Out.empty() )
					Out += ' ';

				Out += ResolveArgument( Text.substr( Start , I - Start ) , Base );
			}

			return Out;
		}

		auto SameStart( const std::string& Text , const std::string& Prefix ) -> bool
		{
			if ( Text.size() < Prefix.size() )
				return false;

#ifdef _WIN32
			return std::equal( Prefix.begin() , Prefix.end() , Text.begin() , []( char A , char B )
			{
				return tolower( static_cast<unsigned char>( A ) ) == tolower( static_cast<unsigned char>( B ) );
			} );
#else
			return Text.compare( 0 , Prefix.size() , Prefix ) == 0;
#endif
		}
	}

	auto FileName( const fs::path& Path ) -> std::string
	{
		return Steam::PathText( Path.filename() );
	}

	auto Parse( const std::string& Line , const fs::path& BaseDir ) -> Command_t
	{
		Command_t Command;
		std::string Program , Arguments;
		const std::string Base = Steam::PathText( BaseDir );

		Split( Trim( Line ) , Base , Program , Arguments );

		if ( Program.empty() )
			return Command;

		const std::string Path = ResolvePath( Expand( Program ) , Base );
		Command.Program = Steam::PathFrom( Path );
		Command.Arguments = ResolveArguments( Expand( Arguments ) , Base );
		Command.Exists = IsFile( Path );
		Command.Directory = Command.Exists ? Command.Program.parent_path() : BaseDir;
		return Command;
	}

	auto ReplaceProgram( const std::string& Line , const fs::path& Program , const fs::path& BaseDir ) -> std::string
	{
		const std::string Base = Steam::PathText( BaseDir );
		std::string Old , Arguments;
		Split( Trim( Line ) , Base , Old , Arguments );

		std::string Path = Steam::PathText( Program );
		const std::string Prefix = Base + kSep;

		if ( Path.size() > Prefix.size() && SameStart( Path , Prefix ) )
		{
			Path = "./" + Path.substr( Prefix.size() );
			std::replace( Path.begin() , Path.end() , '\\' , '/' );
		}

		if ( Path.find( ' ' ) != std::string::npos )
			Path = "\"" + Path + "\"";

		return Arguments.empty() ? Path : Path + " " + Arguments;
	}

	auto SplitArguments( const std::string& Text ) -> std::vector<std::string>
	{
		std::vector<std::string> Words;
		std::string Word;
		bool InWord = false;
		char Quote = 0;

		for ( size_t i = 0; i < Text.size(); i++ )
		{
			const char C = Text[i];

			if ( Quote == '\'' )
			{
				if ( C == '\'' )
					Quote = 0;
				else
					Word += C;

				continue;
			}

			if ( Quote == '"' )
			{
				if ( C == '"' )
					Quote = 0;
				else if ( C == '\\' && i + 1 < Text.size() && strchr( "\"\\$`" , Text[i + 1] ) )
					Word += Text[++i];
				else
					Word += C;

				continue;
			}

			if ( IsSpace( C ) )
			{
				if ( InWord )
					Words.push_back( std::move( Word ) );

				Word.clear();
				InWord = false;
				continue;
			}

			InWord = true;

			if ( C == '\'' || C == '"' )
				Quote = C;
			else if ( C == '\\' && i + 1 < Text.size() )
				Word += Text[++i];
			else
				Word += C;
		}

		if ( InWord )
			Words.push_back( std::move( Word ) );

		return Words;
	}

	auto Run( const Command_t& Command , std::string& Error ) -> bool
	{
		if ( Command.Program.empty() )
		{
			Error = "команда запуска не задана";
			return false;
		}

#ifdef _WIN32
		const std::wstring Arguments = Steam::Wide( Command.Arguments );

		SHELLEXECUTEINFOW Info = { sizeof( Info ) };
		Info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
		Info.lpVerb = L"open";
		Info.lpFile = Command.Program.c_str();
		Info.lpParameters = Arguments.empty() ? nullptr : Arguments.c_str();
		Info.lpDirectory = Command.Directory.empty() ? nullptr : Command.Directory.c_str();
		Info.nShow = SW_SHOWNORMAL;

		if ( ShellExecuteExW( &Info ) )
			return true;

		if ( GetLastError() == ERROR_CANCELLED )
		{
			Error = "запуск отменён (окно UAC)";
			return false;
		}

		Error = FileName( Command.Program ) + ": " + Platform::LastErrorText();
		return false;
#else
		std::string SpawnError;

		if ( Platform::Spawn( Command.Program , SplitArguments( Command.Arguments ) , Command.Directory , SpawnError ) )
			return true;

		Error = FileName( Command.Program ) + ": " + SpawnError;
		return false;
#endif
	}
}
