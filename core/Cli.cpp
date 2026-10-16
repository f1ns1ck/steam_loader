#include "Cli.hpp"

#include "Accounts.hpp"
#include "Platform.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace Cli
{
	namespace
	{
		namespace fs = std::filesystem;

		using Accounts::Row_t;
		using Accounts::Snapshot_t;
		using Accounts::Paths_t;

		enum ECommand
		{
			CMD_NONE ,
			CMD_HELP ,
			CMD_VERSION ,
			CMD_LIST ,
			CMD_INFO ,
			CMD_STATUS ,
			CMD_SWITCH ,
			CMD_PLAY ,
			CMD_RUN ,
			CMD_LOGIN ,
			CMD_HIDE ,
			CMD_UNHIDE ,
			CMD_GAME_DIR ,
			CMD_COMMAND ,
			CMD_DUMP ,
			CMD_DUMP_SOC ,
			CMD_RESOLVE ,
		};

		struct Spec_t
		{
			const char* szLong;
			const char* szShort;
			ECommand Id;
			int Min;
			int Max;
		};

		constexpr Spec_t kCommands[] = {
			{ "--help" , "-h" , CMD_HELP , 0 , 0 } ,
			{ "--version" , "-v" , CMD_VERSION , 0 , 0 } ,
			{ "--list" , "-l" , CMD_LIST , 0 , 0 } ,
			{ "--info" , "-i" , CMD_INFO , 1 , 1 } ,
			{ "--status" , nullptr , CMD_STATUS , 0 , 0 } ,
			{ "--switch" , "-s" , CMD_SWITCH , 1 , 1 } ,
			{ "--play" , "-p" , CMD_PLAY , 0 , 1 } ,
			{ "--run" , "-r" , CMD_RUN , 0 , 1 } ,
			{ "--login" , nullptr , CMD_LOGIN , 1 , 1 } ,
			{ "--hide" , nullptr , CMD_HIDE , 1 , 1 } ,
			{ "--unhide" , nullptr , CMD_UNHIDE , 1 , 1 } ,
			{ "--game-dir" , nullptr , CMD_GAME_DIR , 0 , 1 } ,
			{ "--command" , nullptr , CMD_COMMAND , 0 , 1 } ,
			{ "--dump" , nullptr , CMD_DUMP , 0 , 1 } ,
			{ "--dump-soc" , nullptr , CMD_DUMP_SOC , 1 , 2 } ,
			{ "--resolve" , nullptr , CMD_RESOLVE , 1 , 2 } ,
		};

		struct Options_t
		{
			ECommand Command = CMD_NONE;
			std::string Name;
			std::vector<std::string> Values;
			bool All = false;
			bool Json = false;
			bool Yes = false;
			bool KeepSteam = false;
			int WaitSeconds = 120;
			std::string ConfigDir;
		};

		// ------------------------------------------------------------ the output
		bool g_Color = false;

		auto SetupConsole() -> void
		{
#ifdef _WIN32
			SetConsoleOutputCP( CP_UTF8 );
			SetConsoleCP( CP_UTF8 );

			HANDLE hOut = GetStdHandle( STD_OUTPUT_HANDLE );
			DWORD Mode = 0;

			if ( hOut != INVALID_HANDLE_VALUE && GetConsoleMode( hOut , &Mode ) )
				g_Color = SetConsoleMode( hOut , Mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING ) != FALSE;
#else
			g_Color = isatty( 1 ) != 0;
#endif

			if ( !Platform::Env( "NO_COLOR" ).empty() )
				g_Color = false;
		}

		auto Out( const std::string& Text ) -> void
		{
			std::fputs( Text.c_str() , stdout );
		}

		auto Err( const std::string& Text ) -> void
		{
			std::fflush( stdout );
			std::fputs( Text.c_str() , stderr );
		}

		// the codes of SGR: 1 bold, 2 dim, 31 red, 32 green, 33 yellow, 36 cyan
		auto Paint( const char* szCode , const std::string& Text ) -> std::string
		{
			return g_Color && !Text.empty() ? "\x1b[" + std::string( szCode ) + "m" + Text + "\x1b[0m" : Text;
		}

		auto SeverityColor( Accounts::ESeverity Severity ) -> const char*
		{
			switch ( Severity )
			{
				case Accounts::SEVERITY_OK: return "32";
				case Accounts::SEVERITY_WARN: return "33";
				case Accounts::SEVERITY_BAD: return "31";
				default: return "2";
			}
		}

		// the width in the characters of the terminal (the code points of UTF-8)
		auto Width( const std::string& Text ) -> size_t
		{
			return static_cast<size_t>( std::count_if( Text.begin() , Text.end() , []( char C ) { return ( static_cast<unsigned char>( C ) & 0xC0 ) != 0x80; } ) );
		}

		auto Cut( const std::string& Text , size_t Max ) -> std::string
		{
			if ( Width( Text ) <= Max )
				return Text;

			std::string Out;
			size_t Count = 0;

			for ( const char C : Text )
			{
				if ( ( static_cast<unsigned char>( C ) & 0xC0 ) != 0x80 && ++Count > Max - 1 )
					break;

				Out += C;
			}

			return Out + "…";
		}

		auto Pad( const std::string& Text , size_t Columns ) -> std::string
		{
			const size_t W = Width( Text );
			return W >= Columns ? Text : Text + std::string( Columns - W , ' ' );
		}

		auto Ask( const std::string& Question ) -> bool
		{
			if ( !Platform::IsTerminal() )
				return false;

			Out( Question + " [y/N] " );
			std::fflush( stdout );

			std::string Line;

			if ( !std::getline( std::cin , Line ) )
				return false;

			Line.erase( std::remove_if( Line.begin() , Line.end() , []( char C ) { return C == ' ' || C == '\r'; } ) , Line.end() );
			return Line == "y" || Line == "Y" || Line == "yes" || Line == "д" || Line == "Д" || Line == "да";
		}

		// ------------------------------------------------------------ JSON
		auto Json( const std::string& Text ) -> std::string
		{
			std::string Out = "\"";

			for ( const char C : Text )
			{
				switch ( C )
				{
					case '"': Out += "\\\""; break;
					case '\\': Out += "\\\\"; break;
					case '\n': Out += "\\n"; break;
					case '\r': Out += "\\r"; break;
					case '\t': Out += "\\t"; break;
					default:
						if ( static_cast<unsigned char>( C ) < 0x20 )
						{
							char Code[8];
							snprintf( Code , sizeof( Code ) , "\\u%04x" , C );
							Out += Code;
						}
						else
							Out += C;
				}
			}

			return Out + "\"";
		}

		auto Json( const std::filesystem::path& Path ) -> std::string
		{
			return Path.empty() ? "null" : Json( Steam::PathText( Path ) );
		}

		auto Json( bool Value ) -> std::string
		{
			return Value ? "true" : "false";
		}

		auto SeverityName( Accounts::ESeverity Severity ) -> const char*
		{
			switch ( Severity )
			{
				case Accounts::SEVERITY_OK: return "ok";
				case Accounts::SEVERITY_WARN: return "warn";
				case Accounts::SEVERITY_BAD: return "bad";
				default: return "none";
			}
		}

		auto RowJson( const Snapshot_t& S , const Row_t& Row , const std::map<uint32_t , std::string>& Heroes ) -> std::string
		{
			const auto& D = Row.Data;
			std::string Out = "{";

			Out += "\"account_id\":" + std::to_string( Row.AccountId );
			Out += ",\"steam_id\":" + Json( std::to_string( Row.Steam.SteamId ) );
			Out += ",\"login\":" + Json( Row.Steam.AccountName );
			Out += ",\"nick\":" + Json( Row.Steam.PersonaName );
			Out += ",\"in_steam\":" + Json( Row.HasSteam );
			Out += ",\"remember_password\":" + Json( Row.Steam.RememberPassword );
			Out += ",\"can_switch\":" + Json( Accounts::CanSwitch( Row ) );
			Out += ",\"active\":" + Json( Accounts::IsActive( S , Row ) );
			Out += ",\"hidden\":" + Json( Row.Hidden );
			Out += ",\"last_login\":" + std::to_string( Row.Steam.Timestamp );
			Out += ",\"status\":[";

			bool First = true;

			for ( const auto& Status : Accounts::AccountStatus( Row , Accounts::Now() ) )
			{
				Out += ( First ? "" : "," ) + std::string( "{\"text\":" ) + Json( Status.Text ) + ",\"severity\":\"" + SeverityName( Status.Severity ) + "\"}";
				First = false;
			}

			Out += "],\"game\":";

			if ( !D.Valid )
				return Out + "null}";

			const auto Hero = Heroes.find( D.MostPlayedHero );

			Out += "{\"cache\":" + Json( D.Path ) + ",\"cache_time\":" + std::to_string( D.FileTime );
			Out += ",\"flags\":" + std::to_string( D.Flags ) + ",\"account_banned\":" + Json( ( D.Flags & SoCache::kFlagAccountBanned ) != 0 );
			Out += ",\"wins\":" + std::to_string( D.Wins ) + ",\"losses\":" + std::to_string( D.Losses ) + ",\"kills\":" + std::to_string( D.Kills );
			Out += ",\"most_played_hero\":" + std::to_string( D.MostPlayedHero ) + ",\"most_played_hero_name\":" + ( Hero != Heroes.end() ? Json( Hero->second ) : "null" );
			Out += ",\"mm_ban_until\":" + std::to_string( D.MmBanUntil ) + ",\"comms_ban_until\":" + std::to_string( D.CommsBanUntil );
			Out += ",\"report_ban_until\":" + std::to_string( D.ReportBanUntil ) + ",\"low_priority_games\":" + std::to_string( D.LowPriorityGames );
			Out += ",\"priority_tokens\":" + std::to_string( D.PriorityTokens ) + ",\"priority_token_progress\":" + std::to_string( D.PriorityTokenProgress );
			Out += ",\"priority_tokens_earned\":" + std::to_string( D.PriorityTokensEarned );
			Out += ",\"brawl_wins\":" + std::to_string( D.BrawlWins ) + ",\"brawl_losses\":" + std::to_string( D.BrawlLosses ) + ",\"brawl_kills\":" + std::to_string( D.BrawlKills );
			Out += ",\"last_match\":" + std::to_string( D.LastMatchTime ) + ",\"privacy\":" + std::to_string( D.Privacy ) + "}}";
			return Out;
		}

		// ------------------------------------------------------------ the arguments
		auto FindSpec( const std::string& Arg ) -> const Spec_t*
		{
			for ( const auto& Spec : kCommands )
			{
				if ( Arg == Spec.szLong || ( Spec.szShort && Arg == Spec.szShort ) )
					return &Spec;
			}

			return nullptr;
		}

		auto Parse( const std::vector<std::string>& Args , Options_t& O , std::string& Error ) -> bool
		{
			for ( size_t i = 0; i < Args.size(); i++ )
			{
				std::string Arg = Args[i];
				std::string Inline;
				bool HasInline = false;

				// --switch=name
				if ( Arg.rfind( "--" , 0 ) == 0 )
				{
					if ( const size_t Equals = Arg.find( '=' ); Equals != std::string::npos )
					{
						Inline = Arg.substr( Equals + 1 );
						Arg = Arg.substr( 0 , Equals );
						HasInline = true;
					}
				}

				const auto Value = [&]( std::string& Out ) -> bool
				{
					if ( HasInline )
						Out = Inline;
					else if ( i + 1 < Args.size() )
						Out = Args[++i];
					else
					{
						Error = Arg + ": нужно значение";
						return false;
					}

					return true;
				};

				if ( Arg == "-a" || Arg == "--all" )
					O.All = true;
				else if ( Arg == "--json" )
					O.Json = true;
				else if ( Arg == "-y" || Arg == "--yes" )
					O.Yes = true;
				else if ( Arg == "--keep-steam" )
					O.KeepSteam = true;
				else if ( Arg == "--config" )
				{
					if ( !Value( O.ConfigDir ) )
						return false;
				}
				else if ( Arg == "--wait" )
				{
					std::string Text;

					if ( !Value( Text ) )
						return false;

					O.WaitSeconds = atoi( Text.c_str() );

					if ( O.WaitSeconds <= 0 )
					{
						Error = "--wait: нужно число секунд";
						return false;
					}
				}
				else if ( const Spec_t* pSpec = FindSpec( Arg ) )
				{
					if ( O.Command != CMD_NONE )
					{
						Error = "две команды сразу: " + O.Name + " и " + Arg;
						return false;
					}

					O.Command = pSpec->Id;
					O.Name = pSpec->szLong;

					if ( HasInline )
						O.Values.push_back( Inline );

					// the values of the command: an option ends them once the needed ones are taken
					while ( static_cast<int>( O.Values.size() ) < pSpec->Max && i + 1 < Args.size() )
					{
						const std::string& Next = Args[i + 1];

						if ( Next.size() > 1 && Next[0] == '-' && static_cast<int>( O.Values.size() ) >= pSpec->Min )
							break;

						O.Values.push_back( Next );
						i++;
					}

					if ( static_cast<int>( O.Values.size() ) < pSpec->Min )
					{
						Error = O.Name + ": не хватает аргумента";
						return false;
					}
				}
				else
				{
					Error = "неизвестный аргумент «" + Arg + "»";
					return false;
				}
			}

			return true;
		}

		// ------------------------------------------------------------ the commands
		auto PrintHelp( const Paths_t& Paths ) -> int
		{
			Out( Paint( "1" , "Account Panel" ) + " " + kVersion + " — аккаунты Steam на этом компьютере, блокировки Deadlock на них\n"
				 "(кеш координатора game/citadel/cache_<id>.soc) и переключение Steam на другой аккаунт.\n"
				 "\n"
				 "Использование: account-panel <команда> [опции]\n"
				 "\n"
				 "Команды:\n"
				 "  -l, --list                  список аккаунтов (* — в сети)\n"
				 "  -i, --info <аккаунт>        всё об аккаунте: Steam, блокировки, статистика\n"
				 "      --status                Steam, папка Deadlock, файлы панели, команда программы\n"
				 "  -s, --switch <аккаунт>      закрыть Steam и войти в аккаунт\n"
				 "  -p, --play [аккаунт]        войти и запустить Deadlock (без аккаунта — на текущем)\n"
				 "  -r, --run [аккаунт]         войти в аккаунт (Steam без окна), закрыть Steam и запустить программу\n"
				 "                              из --command; без аккаунта — на текущем\n"
				 "      --login <аккаунт>       только войти в аккаунт (Steam без окна) и закрыть Steam\n"
				 "      --hide <аккаунт>        убрать из списка панели (Steam и игра не меняются)\n"
				 "      --unhide <аккаунт>      вернуть в список\n"
				 "      --game-dir [папка]      показать или задать папку Deadlock; \"\" — искать самой\n"
				 "      --command [строка]      показать или задать команду программы после входа; \"\" — убрать\n"
				 "      --dump [файл]           всё прочитанное текстом (отладка)\n"
				 "      --dump-soc <папка> [файл]  разбор cache_*.soc папки game/citadel (Steam не читается)\n"
				 "      --resolve <строка> [файл]  как будет запущена команда\n"
				 "  -h, --help                  эта справка\n"
				 "  -v, --version               версия\n"
				 "\n"
				 "Опции:\n"
				 "  -a, --all                   с убранными аккаунтами\n"
				 "      --json                  вывод в JSON (--list, --info, --status)\n"
				 "  -y, --yes                   не спрашивать: переключать при запущенном Deadlock (это лив из матча),\n"
				 "                              завершать Steam, если он не закрылся сам\n"
				 "      --wait <сек>            сколько ждать входа Steam (--run, --login), по умолчанию 120\n"
				 "      --keep-steam            --run, --login: не закрывать Steam после входа (остаётся в трее)\n"
				 "      --config <папка>        папка файлов панели, сейчас " + Steam::PathText( Paths.ConfigDir ) + "\n"
				 "\n"
				 "Аккаунт — логин, ник, account id, SteamID64, [U:1:id], #номер из --list или начало логина/ника\n"
				 "(регистр не важен). Переключение работает для аккаунтов, вход в которые Steam запомнил.\n"
				 "\n"
				 "Примеры:\n"
				 "  account-panel --list\n"
				 "  account-panel --switch my_login\n"
				 "  account-panel --play \"#2\" -y\n"
				 "  account-panel --command \"./loader.exe --silent\"\n"
				 "  account-panel --run smurf --wait 60\n"
				 "  account-panel --login smurf && ./loader.exe\n"
				 "\n"
				 "Код выхода: 0 — готово, 1 — ошибка, 2 — неверные аргументы.\n" );
			return 0;
		}

		auto SteamLine( const Snapshot_t& S ) -> std::string
		{
			if ( S.Info.SteamPath.empty() )
				return Paint( "31" , "Steam не найден" );

			if ( !S.SteamRunning )
				return "Steam закрыт";

			for ( const auto& Row : S.Rows )
			{
				if ( Accounts::IsActive( S , Row ) )
					return "Steam в сети: " + Paint( "32" , Row.Steam.AccountName ) + ( Row.Steam.PersonaName.empty() ? "" : " (" + Row.Steam.PersonaName + ")" );
			}

			return S.Info.ActiveUser ? "Steam в сети: account id " + std::to_string( S.Info.ActiveUser ) : "Steam запущен, вход не выполнен";
		}

		auto List( const Options_t& O , const Paths_t& Paths ) -> int
		{
			const auto S = Accounts::Load( Paths , O.All );

			if ( O.Json )
			{
				const auto Heroes = Accounts::ReadHeroNames( Accounts::FindDataDir() );
				std::string Text = "[";

				for ( size_t i = 0; i < S.Rows.size(); i++ )
					Text += ( i ? "," : "" ) + RowJson( S , S.Rows[i] , Heroes );

				Out( Text + "]\n" );
				return 0;
			}

			Out( SteamLine( S ) + " · " + ( S.CitadelDir.empty() ? Paint( "33" , "папка Deadlock не найдена (--game-dir)" ) : "Deadlock: " + Steam::PathText( S.CitadelDir ) ) + "\n" );

			if ( S.Rows.empty() )
			{
				Out( S.HiddenCount ? "Все аккаунты убраны из списка: --list --all\n" : "Аккаунтов нет: войди в Steam хотя бы раз с галкой «Запомнить меня».\n" );
				return 0;
			}

			const int64_t T = Accounts::Now();
			size_t LoginW = 5 , NickW = 3 , IdW = 10 , StatusW = 9;

			struct Line_t
			{
				std::string Login , Nick , Id , Status , Login2;
				Accounts::ESeverity Severity;
			};

			std::vector<Line_t> Lines;

			for ( const auto& Row : S.Rows )
			{
				const auto Pills = Accounts::AccountStatus( Row , T );

				Line_t Line;
				Line.Login = Cut( Row.Steam.AccountName.empty() ? "—" : Row.Steam.AccountName , 24 );
				Line.Nick = Cut( Row.Steam.PersonaName.empty() ? "—" : Row.Steam.PersonaName , 24 );
				Line.Id = std::to_string( Row.AccountId );
				Line.Status = Pills.front().Text + ( Pills.size() > 1 ? " +" + std::to_string( Pills.size() - 1 ) : "" );
				Line.Severity = Pills.front().Severity;
				Line.Login2 = !Row.HasSteam ? "нет в Steam" : Row.Steam.RememberPassword ? "запомнен" : "нужен пароль";

				if ( Row.Hidden )
					Line.Login2 += ", убран";

				LoginW = std::max( LoginW , Width( Line.Login ) );
				NickW = std::max( NickW , Width( Line.Nick ) );
				IdW = std::max( IdW , Width( Line.Id ) );
				StatusW = std::max( StatusW , Width( Line.Status ) );
				Lines.push_back( std::move( Line ) );
			}

			const size_t NumW = std::to_string( Lines.size() ).size() + 1;

			Out( Paint( "2" , "  " + Pad( "#" , NumW ) + "  " + Pad( "ЛОГИН" , LoginW ) + "  " + Pad( "НИК" , NickW ) + "  " + Pad( "ACCOUNT ID" , IdW ) + "  " +
							  Pad( "СОСТОЯНИЕ" , StatusW ) + "  ВХОД" ) + "\n" );

			for ( size_t i = 0; i < Lines.size(); i++ )
			{
				const auto& L = Lines[i];
				const bool Active = Accounts::IsActive( S , S.Rows[i] );

				Out( ( Active ? Paint( "32" , "*" ) : " " ) + " " + Pad( "#" + std::to_string( i + 1 ) , NumW ) + "  " +
					 ( Active ? Paint( "1" , Pad( L.Login , LoginW ) ) : Pad( L.Login , LoginW ) ) + "  " + Pad( L.Nick , NickW ) + "  " + Pad( L.Id , IdW ) + "  " +
					 Paint( SeverityColor( L.Severity ) , Pad( L.Status , StatusW ) ) + "  " +
					 ( S.Rows[i].Hidden || !Accounts::CanSwitch( S.Rows[i] ) ? Paint( "2" , L.Login2 ) : L.Login2 ) + "\n" );
			}

			if ( S.HiddenCount && !O.All )
				Out( Paint( "2" , "Убранных из списка: " + std::to_string( S.HiddenCount ) + " (показать: --all)" ) + "\n" );

			return 0;
		}

		// the account of the command: among the shown ones, then (by the name, not by #N) among the hidden ones too
		auto FindRow( const Options_t& O , const Paths_t& Paths , const std::string& Query , Snapshot_t& S , std::string& Error ) -> const Row_t*
		{
			S = Accounts::Load( Paths , O.All );

			if ( const Row_t* pRow = Accounts::Find( S , Query , Error ) )
				return pRow;

			if ( O.All || !S.HiddenCount || Query.rfind( "#" , 0 ) == 0 )
				return nullptr;

			S = Accounts::Load( Paths , true );
			std::string Ignored;
			return Accounts::Find( S , Query , Ignored );
		}

		auto Field( const std::string& Label , const std::string& Value , const char* szColor = nullptr ) -> void
		{
			Out( "  " + Paint( "2" , Pad( Label , 18 ) ) + ( szColor ? Paint( szColor , Value ) : Value ) + "\n" );
		}

		auto BanField( const std::string& Label , uint32_t Until , int64_t T , const char* szColor ) -> void
		{
			if ( Until > T )
				Field( Label , "до " + Accounts::FormatDate( Until ) + " (осталось " + Accounts::FormatDuration( Until - T ) + ")" , szColor );
			else if ( Until )
				Field( Label , "нет (последний истёк " + Accounts::FormatDuration( T - Until ) + " назад)" , "32" );
			else
				Field( Label , "нет" , "32" );
		}

		auto Info( const Options_t& O , const Paths_t& Paths ) -> int
		{
			Snapshot_t S;
			std::string Error;
			const Row_t* pRow = FindRow( O , Paths , O.Values[0] , S , Error );

			if ( !pRow )
			{
				Err( "account-panel: " + Error + "\n" );
				return 1;
			}

			const auto& Row = *pRow;
			const auto& D = Row.Data;
			const auto Heroes = Accounts::ReadHeroNames( Accounts::FindDataDir() );
			const int64_t T = Accounts::Now();

			if ( O.Json )
			{
				Out( RowJson( S , Row , Heroes ) + "\n" );
				return 0;
			}

			Out( Paint( "1" , Accounts::DisplayName( Row ) ) + ( Row.Steam.AccountName.empty() ? "" : "  " + Paint( "2" , Row.Steam.AccountName ) ) + "\n" );
			Field( "SteamID64" , std::to_string( Row.Steam.SteamId ) );
			Field( "Account ID" , std::to_string( Row.AccountId ) + "  [U:1:" + std::to_string( Row.AccountId ) + "]" );
			Field( "Профиль" , "https://steamcommunity.com/profiles/" + std::to_string( Row.Steam.SteamId ) );

			if ( Accounts::IsActive( S , Row ) )
				Field( "Steam" , "в сети" , "32" );
			else if ( !Row.HasSteam )
				Field( "Steam" , "нет в loginusers.vdf — переключиться нельзя" , "33" );
			else
				Field( "Steam" , std::string( Row.Steam.RememberPassword ? "вход запомнен" : "вход не запомнен — переключиться нельзя" ) +
					   ", последний вход " + Accounts::FormatDate( Row.Steam.Timestamp ) , Row.Steam.RememberPassword ? nullptr : "33" );

			if ( Row.Hidden )
				Field( "Список" , "убран (--unhide)" , "2" );

			std::string States;

			for ( const auto& Status : Accounts::AccountStatus( Row , T ) )
				States += ( States.empty() ? "" : ", " ) + Paint( SeverityColor( Status.Severity ) , Status.Text );

			Field( "Состояние" , States );

			if ( !D.Valid )
			{
				Out( "\n" + Paint( "2" , S.CitadelDir.empty() ? "Папка Deadlock не найдена (--game-dir)." : "Аккаунт не заходил в Deadlock на этом компьютере." ) + "\n" );
				return 0;
			}

			Out( "\n" + Paint( "1" , "Блокировки" ) + "\n" );
			Field( "Аккаунт" , D.Flags & SoCache::kFlagAccountBanned ? "заблокирован" : "не заблокирован" , D.Flags & SoCache::kFlagAccountBanned ? "31" : "32" );
			BanField( "Поиск матчей" , D.MmBanUntil , T , "31" );
			BanField( "Чат" , D.CommsBanUntil , T , "33" );
			BanField( "Репорты" , D.ReportBanUntil , T , "33" );
			Field( "Low Priority" , D.LowPriorityGames ? "осталось побед: " + std::to_string( D.LowPriorityGames ) : "нет" , D.LowPriorityGames ? "33" : "32" );

			const uint32_t Games = D.Wins + D.Losses;
			const auto Hero = Heroes.find( D.MostPlayedHero );

			Out( "\n" + Paint( "1" , "Статистика" ) + "\n" );
			Field( "Победы / поражения" , std::to_string( D.Wins ) + " / " + std::to_string( D.Losses ) +
				   ( Games ? " (" + std::to_string( ( D.Wins * 100 + Games / 2 ) / Games ) + "%)" : "" ) );
			Field( "Убийства" , std::to_string( D.Kills ) );
			Field( "Любимый герой" , Hero != Heroes.end() ? Hero->second : D.MostPlayedHero ? "#" + std::to_string( D.MostPlayedHero ) : "—" );
			Field( "Brawl" , std::to_string( D.BrawlWins ) + " / " + std::to_string( D.BrawlLosses ) + ", убийств " + std::to_string( D.BrawlKills ) );
			Field( "Токены приоритета" , std::to_string( D.PriorityTokens ) + " (прогресс " + std::to_string( D.PriorityTokenProgress ) + ", всего " +
				   std::to_string( D.PriorityTokensEarned ) + ")" );
			Field( "Последний матч" , Accounts::FormatDate( D.LastMatchTime ) + ( D.LastMatchTime ? " (" + Accounts::FormatDuration( T - D.LastMatchTime ) + " назад)" : "" ) );

			Out( "\n" + Paint( "1" , "Данные" ) + "\n" );
			Field( "Кеш координатора" , Steam::PathText( D.Path ) );
			Field( "Обновлён" , Accounts::FormatDate( D.FileTime ) + " (" + Accounts::FormatDuration( T - D.FileTime ) + " назад, при входе аккаунта в Deadlock)" );
			return 0;
		}

		auto Status( const Options_t& O , const Paths_t& Paths ) -> int
		{
			const auto S = Accounts::Load( Paths , true );
			const std::string Line = Accounts::ReadLaunchLine( Paths );
			const auto Command = Launch::Parse( Line , Paths.BaseDir );
			const bool Game = Steam::IsGameRunning();

			std::string SteamCommand = Steam::PathText( S.Info.SteamExe );

			for ( const auto& Argument : S.Info.SteamArgs )
				SteamCommand += " " + Argument;

			if ( O.Json )
			{
				std::string Text = "{";
				Text += "\"steam_path\":" + Json( S.Info.SteamPath ) + ",\"steam_command\":" + ( S.Info.SteamExe.empty() ? "null" : Json( SteamCommand ) );
				Text += ",\"registry_file\":" + Json( S.Info.RegistryFile ) + ",\"steam_running\":" + Json( S.SteamRunning );
				Text += ",\"active_user\":" + std::to_string( S.Info.ActiveUser ) + ",\"auto_login_user\":" + Json( S.Info.AutoLoginUser );
				Text += ",\"citadel_dir\":" + Json( S.CitadelDir ) + ",\"citadel_manual\":" + Json( S.CitadelManual ) + ",\"game_running\":" + Json( Game );
				Text += ",\"config_dir\":" + Json( Paths.ConfigDir ) + ",\"base_dir\":" + Json( Paths.BaseDir );
				Text += ",\"accounts\":" + std::to_string( S.Rows.size() ) + ",\"hidden\":" + std::to_string( S.HiddenCount );
				Text += ",\"command\":{\"line\":" + Json( Line ) + ",\"program\":" + Json( Command.Program ) + ",\"arguments\":" + Json( Command.Arguments );
				Text += ",\"directory\":" + Json( Command.Directory ) + ",\"exists\":" + Json( Command.Exists ) + "}}";
				Out( Text + "\n" );
				return 0;
			}

			Out( SteamLine( S ) + "\n" );
			Field( "Папка Steam" , S.Info.SteamPath.empty() ? "не найдена" : Steam::PathText( S.Info.SteamPath ) , S.Info.SteamPath.empty() ? "31" : nullptr );
			Field( "Запуск Steam" , S.Info.SteamExe.empty() ? "не найден" : SteamCommand , S.Info.SteamExe.empty() ? "31" : nullptr );

			if ( !S.Info.RegistryFile.empty() )
				Field( "registry.vdf" , Steam::PathText( S.Info.RegistryFile ) );

			Field( "AutoLoginUser" , S.Info.AutoLoginUser.empty() ? "—" : S.Info.AutoLoginUser );
			Field( "Deadlock" , S.CitadelDir.empty() ? "папка не найдена (--game-dir)" : Steam::PathText( S.CitadelDir ) + ( S.CitadelManual ? " (задана вручную)" : "" ) ,
				   S.CitadelDir.empty() ? "33" : nullptr );
			Field( "Игра" , Game ? "запущена" : "не запущена" , Game ? "33" : nullptr );
			Field( "Аккаунты" , std::to_string( S.Rows.size() ) + ( S.HiddenCount ? ", из них убрано " + std::to_string( S.HiddenCount ) : "" ) );
			Field( "Файлы панели" , Steam::PathText( Paths.ConfigDir ) );

			if ( Line.empty() )
				Field( "Команда" , "не задана (--command)" , "2" );
			else
			{
				Field( "Команда" , Line );
				Field( "  запускает" , Steam::PathText( Command.Program ) + ( Command.Arguments.empty() ? "" : " " + Command.Arguments ) ,
					   Command.Exists || !Command.Program.has_parent_path() ? nullptr : "31" );

				if ( !Command.Exists )
					Field( "" , Command.Program.has_parent_path() ? "файл не найден" : "из PATH" , Command.Program.has_parent_path() ? "31" : "2" );
			}

			return 0;
		}

		auto Action( const Options_t& O , const Paths_t& Paths ) -> int
		{
			Snapshot_t S;
			const Row_t* pRow = nullptr;
			std::string Error;

			if ( !O.Values.empty() )
				pRow = FindRow( O , Paths , O.Values[0] , S , Error );
			else
			{
				// --play / --run without the account: the logged in one
				S = Accounts::Load( Paths , true );

				for ( const auto& Row : S.Rows )
				{
					if ( Accounts::IsActive( S , Row ) )
						pRow = &Row;
				}

				if ( !pRow && O.Command == CMD_PLAY )
				{
					if ( !Steam::LaunchGame( S.Info ) )
					{
						Err( "account-panel: не удалось запустить Deadlock через Steam\n" );
						return 1;
					}

					Out( "Deadlock запускается через Steam\n" );
					return 0;
				}

				if ( !pRow )
					Error = "Steam не в сети: укажи аккаунт";
			}

			if ( !pRow )
			{
				Err( "account-panel: " + Error + "\n" );
				return 1;
			}

			const auto& Row = *pRow;
			const bool Active = Accounts::IsActive( S , Row );
			std::string Why;

			if ( !Active && !Accounts::CanSwitch( Row , &Why ) )
			{
				Err( "account-panel: " + Accounts::DisplayName( Row ) + ": " + Why + "\n" );
				return 1;
			}

			Accounts::EAction Kind = Accounts::ACTION_SWITCH;

			if ( O.Command == CMD_SWITCH || O.Command == CMD_LOGIN )
			{
				Kind = O.Command == CMD_LOGIN ? Accounts::ACTION_LOGIN : Accounts::ACTION_SWITCH;

				if ( Active && ( O.Command == CMD_SWITCH || O.KeepSteam ) )
				{
					Out( "Steam уже в аккаунте " + Row.Steam.AccountName + "\n" );
					return 0;
				}

				// --login into the logged in account: the login is there, only Steam is closed
				if ( Active )
				{
					if ( Steam::IsGameRunning() && !O.Yes && !Ask( "Deadlock запущен и закроется вместе со Steam. Закрыть?" ) )
					{
						Err( "Отменено.\n" );
						return 1;
					}

					Steam::SwitchCallbacks_t Callbacks;
					Callbacks.AskKill = [&O]() { return O.Yes || Ask( "Steam не закрылся за 15 секунд. Завершить процесс принудительно?" ); };

					if ( std::string CloseError; !Steam::CloseSteam( S.Info , Callbacks , CloseError ) )
					{
						Err( "account-panel: " + CloseError + "\n" );
						return 1;
					}

					Out( Paint( "32" , "Аккаунт " + Row.Steam.AccountName + " уже вошёл, Steam закрыт" ) + "\n" );
					return 0;
				}
			}
			else if ( O.Command == CMD_PLAY )
			{
				if ( Active )
				{
					if ( !Steam::LaunchGame( S.Info ) )
					{
						Err( "account-panel: не удалось запустить Deadlock через Steam\n" );
						return 1;
					}

					Out( "Deadlock запускается (аккаунт " + Row.Steam.AccountName + ")\n" );
					return 0;
				}

				Kind = Accounts::ACTION_SWITCH_GAME;
			}
			else
				Kind = Active ? Accounts::ACTION_PROGRAM : Accounts::ACTION_SWITCH_PROGRAM;

			Launch::Command_t Command;

			if ( O.Command == CMD_RUN )
			{
				Command = Launch::Parse( Accounts::ReadLaunchLine( Paths ) , Paths.BaseDir );

				if ( Command.Program.empty() )
				{
					Err( "account-panel: команда программы не задана: account-panel --command \"<программа и аргументы>\"\n" );
					return 1;
				}

				if ( !Command.Exists && Command.Program.has_parent_path() )
				{
					Err( "account-panel: файл команды не найден: " + Steam::PathText( Command.Program ) + "\n" );
					return 1;
				}
			}

			// the switch closes Steam and the game with it
			if ( ( Kind != Accounts::ACTION_PROGRAM || !O.KeepSteam ) && Steam::IsGameRunning() && !O.Yes )
			{
				Err( Paint( "33" , "Deadlock запущен." ) + " Переключение закроет Steam, а вместе с ним и игру. Если идёт матч, это засчитается\n"
					 "как дисконнект или лив: бан поиска или Low Priority.\n" );

				if ( !Ask( "Всё равно переключить?" ) )
				{
					Err( Platform::IsTerminal() ? "Отменено.\n" : "Отменено (без терминала подтверждение — флаг -y).\n" );
					return 1;
				}
			}

			Steam::SwitchCallbacks_t Callbacks;
			Callbacks.Status = []( const std::string& Text ) { Out( Paint( "2" , "  " + Text ) + "\n" ); std::fflush( stdout ); };
			Callbacks.AskKill = [&O]()
			{
				return O.Yes || Ask( "Steam не закрылся за 15 секунд. Завершить процесс принудительно?" );
			};

			if ( Kind == Accounts::ACTION_LOGIN || Kind == Accounts::ACTION_SWITCH_PROGRAM )
				Out( "Вхожу в " + Paint( "1" , Row.Steam.AccountName ) + ( Row.Steam.PersonaName.empty() ? "" : " (" + Row.Steam.PersonaName + ")" ) +
					 ( O.KeepSteam ? ", Steam без окна\n" : ", Steam без окна и закроется после входа\n" ) );
			else if ( Kind != Accounts::ACTION_PROGRAM )
				Out( "Переключаю Steam на " + Paint( "1" , Row.Steam.AccountName ) + ( Row.Steam.PersonaName.empty() ? "" : " (" + Row.Steam.PersonaName + ")" ) + "\n" );

			std::fflush( stdout );

			std::string Message;
			const bool Ok = Accounts::Run( S.Info , Row.Steam , Kind , Command , Callbacks , Message , O.WaitSeconds * 1000 , O.KeepSteam );

			if ( Ok )
				Out( Paint( "32" , Message ) + "\n" );
			else
				Err( Paint( "31" , Message ) + "\n" );

			return Ok ? 0 : 1;
		}

		auto Hide( const Options_t& O , const Paths_t& Paths , bool Take ) -> int
		{
			Snapshot_t S;
			std::string Error;
			Options_t All = O;
			All.All = true;

			const Row_t* pRow = FindRow( All , Paths , O.Values[0] , S , Error );

			if ( !pRow )
			{
				Err( "account-panel: " + Error + "\n" );
				return 1;
			}

			if ( !Accounts::SetHidden( Paths , pRow->AccountId , Take ) )
			{
				Err( "account-panel: не удалось записать " + Steam::PathText( Paths.ConfigDir / "account_panel_hidden.txt" ) + "\n" );
				return 1;
			}

			Out( Accounts::DisplayName( *pRow ) + ( Take ? " убран из списка панели (Steam и файлы игры не тронуты)\n" : " возвращён в список\n" ) );
			return 0;
		}

		auto GameDir( const Options_t& O , const Paths_t& Paths ) -> int
		{
			if ( O.Values.empty() )
			{
				const auto S = Accounts::Load( Paths , true );

				if ( S.CitadelDir.empty() )
				{
					Err( "account-panel: папка Deadlock не найдена, задай её: account-panel --game-dir <папка Deadlock>\n" );
					return 1;
				}

				Out( Steam::PathText( S.CitadelDir ) + ( S.CitadelManual ? "  (задана вручную)" : "" ) + "\n" );
				return 0;
			}

			if ( O.Values[0].empty() )
			{
				Accounts::WriteGameDir( Paths , {} );
				const auto S = Accounts::Load( Paths , true );
				Out( "Папка Deadlock ищется сама: " + ( S.CitadelDir.empty() ? std::string( "не найдена" ) : Steam::PathText( S.CitadelDir ) ) + "\n" );
				return 0;
			}

			std::error_code Ec;
			fs::path Dir = fs::absolute( Steam::PathFrom( O.Values[0] ) , Ec );

			if ( Ec || Accounts::CitadelOf( Dir ).empty() )
			{
				Err( "account-panel: папки нет: " + O.Values[0] + "\n" );
				return 1;
			}

			Dir = Dir.lexically_normal();

			if ( !Accounts::WriteGameDir( Paths , Dir ) )
			{
				Err( "account-panel: не удалось записать файл в " + Steam::PathText( Paths.ConfigDir ) + "\n" );
				return 1;
			}

			const fs::path Citadel = Accounts::CitadelOf( Dir );
			Out( "Папка Deadlock: " + Steam::PathText( Citadel ) + "\n" );

			if ( SoCache::Scan( Citadel ).empty() )
				Out( Paint( "33" , "В ней нет cache_*.soc: это точно папка Deadlock (или game/citadel)?" ) + "\n" );

			return 0;
		}

		auto CommandLine( const Options_t& O , const Paths_t& Paths ) -> int
		{
			if ( !O.Values.empty() && !Accounts::WriteLaunchLine( Paths , O.Values[0] ) )
			{
				Err( "account-panel: не удалось записать файл в " + Steam::PathText( Paths.ConfigDir ) + "\n" );
				return 1;
			}

			const std::string Line = Accounts::ReadLaunchLine( Paths );

			if ( Line.empty() )
			{
				Out( "Команда не задана\n" );
				return 0;
			}

			const auto Command = Launch::Parse( Line , Paths.BaseDir );
			Out( Line + "\n" );
			Field( "программа" , Steam::PathText( Command.Program ) , Command.Exists || !Command.Program.has_parent_path() ? nullptr : "31" );

			if ( !Command.Arguments.empty() )
				Field( "аргументы" , Command.Arguments );

			Field( "папка" , Steam::PathText( Command.Directory ) );

			if ( !Command.Exists )
				Field( "" , Command.Program.has_parent_path() ? "файл не найден" : "ищется в PATH" , Command.Program.has_parent_path() ? "31" : "2" );

			return 0;
		}

		// ------------------------------------------------------------ the dumps (the text for the checks)
		auto WriteOut( const std::vector<std::string>& Values , size_t FileIndex , const std::string& Text ) -> int
		{
			if ( Values.size() <= FileIndex )
			{
				Out( Text );
				return 0;
			}

			std::ofstream File( Steam::PathFrom( Values[FileIndex] ) , std::ios::binary | std::ios::trunc );
			File << Text;
			return File ? 0 : 1;
		}

		auto DumpAccount( std::string& Out , const SoCache::Account_t& D , const std::map<uint32_t , std::string>& Heroes ) -> void
		{
			char Line[1024];

			snprintf( Line , sizeof( Line ) , "  cache %s: valid %d, file time %s\n" , Steam::PathText( D.Path ).c_str() , D.Valid ? 1 : 0 , Accounts::FormatDate( D.FileTime ).c_str() );
			Out += Line;

			if ( !D.Valid )
				return;

			const auto Hero = Heroes.find( D.MostPlayedHero );

			snprintf( Line , sizeof( Line ) ,
					  "    flags %llu (banned %d), wins %u, losses %u, kills %u, most played hero %u (%s)\n"
					  "    mm ban until %s, comms ban until %s, report ban until %s, low priority games %u\n"
					  "    priority tokens %u (progress %u, earned %u), brawl %u/%u/%u, last match %s, privacy %u\n" ,
					  static_cast<unsigned long long>( D.Flags ) , ( D.Flags & SoCache::kFlagAccountBanned ) ? 1 : 0 , D.Wins , D.Losses , D.Kills , D.MostPlayedHero ,
					  Hero != Heroes.end() ? Hero->second.c_str() : "—" , Accounts::FormatDate( D.MmBanUntil ).c_str() , Accounts::FormatDate( D.CommsBanUntil ).c_str() ,
					  Accounts::FormatDate( D.ReportBanUntil ).c_str() , D.LowPriorityGames , D.PriorityTokens , D.PriorityTokenProgress , D.PriorityTokensEarned ,
					  D.BrawlWins , D.BrawlLosses , D.BrawlKills , Accounts::FormatDate( D.LastMatchTime ).c_str() , D.Privacy );
			Out += Line;
		}

		auto Dump( const Options_t& O , const Paths_t& Paths ) -> int
		{
			const auto S = Accounts::Load( Paths , true );
			const auto Heroes = Accounts::ReadHeroNames( Accounts::FindDataDir() );

			std::string Text = "steam " + Steam::PathText( S.Info.SteamPath ) + ", exe " + Steam::PathText( S.Info.SteamExe ) + ", registry " +
				Steam::PathText( S.Info.RegistryFile ) + ", running " + ( S.SteamRunning ? "1" : "0" ) + ", active user " + std::to_string( S.Info.ActiveUser ) +
				", auto login " + S.Info.AutoLoginUser + "\ncitadel " + Steam::PathText( S.CitadelDir ) + ( S.CitadelManual ? " (manual)" : "" ) +
				"\nconfig " + Steam::PathText( Paths.ConfigDir ) + ", base " + Steam::PathText( Paths.BaseDir ) + ", data " + Steam::PathText( Accounts::FindDataDir() ) + "\n";

			for ( const auto& Row : S.Rows )
			{
				Text += "account " + std::to_string( Row.AccountId ) + ": steam " + ( Row.HasSteam ? "yes" : "no" ) + ", name " + Row.Steam.AccountName +
					", nick " + Row.Steam.PersonaName + ", remember " + ( Row.Steam.RememberPassword ? "1" : "0" ) + ", hidden " + ( Row.Hidden ? "1" : "0" ) +
					", last login " + Accounts::FormatDate( Row.Steam.Timestamp ) +
					", avatar " + ( Steam::AvatarPath( S.Info.SteamPath , Row.Steam.SteamId ).empty() ? "no" : "yes" ) + "\n";
				DumpAccount( Text , Row.Data , Heroes );
			}

			return WriteOut( O.Values , 0 , Text );
		}

		auto DumpSoc( const Options_t& O ) -> int
		{
			const auto Heroes = Accounts::ReadHeroNames( Accounts::FindDataDir() );
			std::string Text = "caches in " + O.Values[0] + "\n";

			for ( const auto& D : SoCache::Scan( Steam::PathFrom( O.Values[0] ) ) )
			{
				Text += "account " + std::to_string( D.AccountId ) + "\n";
				DumpAccount( Text , D , Heroes );
			}

			return WriteOut( O.Values , 1 , Text );
		}

		auto Resolve( const Options_t& O , const Paths_t& Paths ) -> int
		{
			const std::string& Line = O.Values[0];
			const auto Command = Launch::Parse( Line , Paths.BaseDir );

			std::string Text = "line " + Line + "\n";
			Text += "base " + Steam::PathText( Paths.BaseDir ) + "\n";
			Text += "program " + Steam::PathText( Command.Program ) + "\n";
			Text += "arguments " + Command.Arguments + "\n";

#ifndef _WIN32
			for ( const auto& Word : Launch::SplitArguments( Command.Arguments ) )
				Text += "  word " + Word + "\n";
#endif

			Text += "directory " + Steam::PathText( Command.Directory ) + "\n";
			Text += std::string( "exists " ) + ( Command.Exists ? "1" : "0" ) + "\n";
			Text += "replaced " + Launch::ReplaceProgram( Line , Paths.BaseDir / "sub dir" / "x.exe" , Paths.BaseDir ) + "\n";
			return WriteOut( O.Values , 1 , Text );
		}
	}

	auto Run( const std::vector<std::string>& Args ) -> int
	{
		SetupConsole();

		Options_t O;
		std::string Error;

		if ( !Parse( Args , O , Error ) )
		{
			Err( "account-panel: " + Error + "\nСправка: account-panel --help\n" );
			return 2;
		}

		const Paths_t Paths = Accounts::DefaultPaths( Steam::PathFrom( O.ConfigDir ) );
		int Code = 0;

		switch ( O.Command )
		{
			case CMD_NONE:
			case CMD_HELP: Code = PrintHelp( Paths ); break;
			case CMD_VERSION: Out( std::string( "account-panel " ) + kVersion + "\n" ); break;
			case CMD_LIST: Code = List( O , Paths ); break;
			case CMD_INFO: Code = Info( O , Paths ); break;
			case CMD_STATUS: Code = Status( O , Paths ); break;
			case CMD_SWITCH:
			case CMD_PLAY:
			case CMD_RUN:
			case CMD_LOGIN: Code = Action( O , Paths ); break;
			case CMD_HIDE: Code = Hide( O , Paths , true ); break;
			case CMD_UNHIDE: Code = Hide( O , Paths , false ); break;
			case CMD_GAME_DIR: Code = GameDir( O , Paths ); break;
			case CMD_COMMAND: Code = CommandLine( O , Paths ); break;
			case CMD_DUMP: Code = Dump( O , Paths ); break;
			case CMD_DUMP_SOC: Code = DumpSoc( O ); break;
			case CMD_RESOLVE: Code = Resolve( O , Paths ); break;
		}

		std::fflush( stdout );
		return Code;
	}
}
