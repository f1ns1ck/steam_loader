// account-panel: the command line of Account Panel, without the window (Windows and Linux). account-panel --help has the commands.

#include "../core/Cli.hpp"
#include "../core/Steam.hpp"

#ifdef _WIN32
int wmain( int Argc , wchar_t** Argv )
{
	std::vector<std::string> Args;

	for ( int i = 1; i < Argc; i++ )
		Args.push_back( Steam::Utf8( Argv[i] ) );

	return Cli::Run( Args );
}
#else
int main( int Argc , char** Argv )
{
	return Cli::Run( std::vector<std::string>( Argv + 1 , Argv + Argc ) );
}
#endif
