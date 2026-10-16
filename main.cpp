// Account Panel: the accounts of Steam on this computer, the bans of Deadlock on them (the cache of the game coordinator,
// game\citadel\cache_<id>.soc) and the switch of Steam to one of them. The window over the library core/ (Accounts.hpp).
//
//   AccountPanel.exe                          the window
//   AccountPanel.exe --help, --list, --switch <account> ...  the command line (core/Cli.cpp; the console of the parent)
//   AccountPanel.exe --preview <dir> <file.rgba> [w h [dialog]]  the window into a picture (made up accounts if <dir> has no caches;
//                                             dialog 2 - with the question "Deadlock is running", 3 - the command of the program)

#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <ImGui/imgui.h>
#include <ImGui/imgui_impl_dx11.h>
#include <ImGui/imgui_impl_win32.h>
#include <ImGui/imgui_internal.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#include <stb/stb_image.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "Style.hpp"
#include "core/Accounts.hpp"
#include "core/Cli.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd , UINT Msg , WPARAM wParam , LPARAM lParam );

namespace
{
	// ---------------------------------------------------------------- D3D11
	ID3D11Device* g_pDevice = nullptr;
	ID3D11DeviceContext* g_pContext = nullptr;
	IDXGISwapChain* g_pSwapChain = nullptr;
	ID3D11RenderTargetView* g_pTarget = nullptr;
	UINT g_ResizeWidth = 0 , g_ResizeHeight = 0;
	float g_Scale = 1.f;
	bool g_OverItem = false;	// the mouse is over a button of the window (the top of the window drags it only elsewhere)

	auto S( float Value ) -> float
	{
		return Value * g_Scale;
	}

	auto CreateTarget() -> void
	{
		ID3D11Texture2D* pBack = nullptr;

		if ( SUCCEEDED( g_pSwapChain->GetBuffer( 0 , IID_PPV_ARGS( &pBack ) ) ) && pBack )
		{
			g_pDevice->CreateRenderTargetView( pBack , nullptr , &g_pTarget );
			pBack->Release();
		}
	}

	auto ReleaseTarget() -> void
	{
		if ( g_pTarget )
		{
			g_pTarget->Release();
			g_pTarget = nullptr;
		}
	}

	auto CreateDevice( HWND hWnd ) -> bool
	{
		DXGI_SWAP_CHAIN_DESC Desc = {};
		Desc.BufferCount = 2;
		Desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.BufferDesc.RefreshRate.Numerator = 60;
		Desc.BufferDesc.RefreshRate.Denominator = 1;
		Desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		Desc.OutputWindow = hWnd;
		Desc.SampleDesc.Count = 1;
		Desc.Windowed = TRUE;
		Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0 , D3D_FEATURE_LEVEL_10_0 };
		D3D_FEATURE_LEVEL Level;

		HRESULT Result = D3D11CreateDeviceAndSwapChain( nullptr , D3D_DRIVER_TYPE_HARDWARE , nullptr , 0 , Levels , 2 , D3D11_SDK_VERSION , &Desc , &g_pSwapChain ,
														&g_pDevice , &Level , &g_pContext );

		if ( Result == DXGI_ERROR_UNSUPPORTED )
			Result = D3D11CreateDeviceAndSwapChain( nullptr , D3D_DRIVER_TYPE_WARP , nullptr , 0 , Levels , 2 , D3D11_SDK_VERSION , &Desc , &g_pSwapChain ,
													&g_pDevice , &Level , &g_pContext );

		if ( FAILED( Result ) )
			return false;

		CreateTarget();
		return true;
	}

	auto ReleaseDevice() -> void
	{
		ReleaseTarget();

		if ( g_pSwapChain ) g_pSwapChain->Release();
		if ( g_pContext ) g_pContext->Release();
		if ( g_pDevice ) g_pDevice->Release();
	}

	LRESULT WINAPI WndProc( HWND hWnd , UINT Msg , WPARAM wParam , LPARAM lParam )
	{
		if ( ImGui_ImplWin32_WndProcHandler( hWnd , Msg , wParam , lParam ) )
			return true;

		switch ( Msg )
		{
			case WM_SIZE:
				if ( wParam != SIZE_MINIMIZED )
				{
					g_ResizeWidth = LOWORD( lParam );
					g_ResizeHeight = HIWORD( lParam );
				}
				return 0;
			case WM_GETMINMAXINFO:
			{
				auto* pInfo = reinterpret_cast<MINMAXINFO*>( lParam );
				pInfo->ptMinTrackSize.x = static_cast<LONG>( S( 820.f ) );
				pInfo->ptMinTrackSize.y = static_cast<LONG>( S( 520.f ) );
				return 0;
			}
			// no frame and no title of Windows: the whole window is ours (the shadow and the rounded corners stay from DWM)
			case WM_NCCALCSIZE:
				if ( wParam == TRUE )
					return 0;
				break;
			case WM_NCHITTEST:
			{
				POINT Point = { GET_X_LPARAM( lParam ) , GET_Y_LPARAM( lParam ) };
				ScreenToClient( hWnd , &Point );

				RECT Client = {};
				GetClientRect( hWnd , &Client );

				const LONG Border = static_cast<LONG>( S( 6.f ) );
				const bool Left = Point.x < Border , Right = Point.x >= Client.right - Border;
				const bool Top = Point.y < Border , Bottom = Point.y >= Client.bottom - Border;

				if ( Top && Left ) return HTTOPLEFT;
				if ( Top && Right ) return HTTOPRIGHT;
				if ( Bottom && Left ) return HTBOTTOMLEFT;
				if ( Bottom && Right ) return HTBOTTOMRIGHT;
				if ( Left ) return HTLEFT;
				if ( Right ) return HTRIGHT;
				if ( Top ) return HTTOP;
				if ( Bottom ) return HTBOTTOM;

				if ( Point.y < static_cast<LONG>( S( Style::kHeaderH ) ) && !g_OverItem )
					return HTCAPTION;

				return HTCLIENT;
			}
			case WM_SYSCOMMAND:
				if ( ( wParam & 0xFFF0 ) == SC_KEYMENU )
					return 0;
				break;
			case WM_DESTROY:
				PostQuitMessage( 0 );
				return 0;
		}

		return DefWindowProcW( hWnd , Msg , wParam , lParam );
	}

	// ---------------------------------------------------------------- the files of the panel (core/Accounts.hpp)
	Accounts::Paths_t g_Paths;

	auto FileExists( const std::wstring& Path ) -> bool
	{
		return GetFileAttributesW( Path.c_str() ) != INVALID_FILE_ATTRIBUTES;
	}

	auto PickFolder( HWND hWnd ) -> std::wstring
	{
		std::wstring Result;
		IFileOpenDialog* pDialog = nullptr;

		if ( FAILED( CoCreateInstance( CLSID_FileOpenDialog , nullptr , CLSCTX_INPROC_SERVER , IID_PPV_ARGS( &pDialog ) ) ) )
			return Result;

		DWORD Options = 0;
		pDialog->GetOptions( &Options );
		pDialog->SetOptions( Options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM );
		pDialog->SetTitle( L"Папка Deadlock (или game\\citadel)" );

		if ( SUCCEEDED( pDialog->Show( hWnd ) ) )
		{
			IShellItem* pItem = nullptr;

			if ( SUCCEEDED( pDialog->GetResult( &pItem ) ) )
			{
				PWSTR szPath = nullptr;

				if ( SUCCEEDED( pItem->GetDisplayName( SIGDN_FILESYSPATH , &szPath ) ) )
				{
					Result = szPath;
					CoTaskMemFree( szPath );
				}

				pItem->Release();
			}
		}

		pDialog->Release();
		return Result;
	}

	// the program to start after the switch: the dialog opens in the folder of the current one
	auto PickExe( HWND hWnd , const std::wstring& Folder ) -> std::wstring
	{
		std::wstring Result;
		IFileOpenDialog* pDialog = nullptr;

		if ( FAILED( CoCreateInstance( CLSID_FileOpenDialog , nullptr , CLSCTX_INPROC_SERVER , IID_PPV_ARGS( &pDialog ) ) ) )
			return Result;

		const COMDLG_FILTERSPEC Filters[] = { { L"Программы (*.exe, *.bat, *.cmd)" , L"*.exe;*.bat;*.cmd" } , { L"Все файлы" , L"*.*" } };

		DWORD Options = 0;
		pDialog->GetOptions( &Options );
		pDialog->SetOptions( Options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM );
		pDialog->SetFileTypes( 2 , Filters );
		pDialog->SetTitle( L"Программа после входа в аккаунт" );

		IShellItem* pFolder = nullptr;

		if ( !Folder.empty() && SUCCEEDED( SHCreateItemFromParsingName( Folder.c_str() , nullptr , IID_PPV_ARGS( &pFolder ) ) ) )
		{
			pDialog->SetFolder( pFolder );
			pFolder->Release();
		}

		if ( SUCCEEDED( pDialog->Show( hWnd ) ) )
		{
			IShellItem* pItem = nullptr;

			if ( SUCCEEDED( pDialog->GetResult( &pItem ) ) )
			{
				PWSTR szPath = nullptr;

				if ( SUCCEEDED( pItem->GetDisplayName( SIGDN_FILESYSPATH , &szPath ) ) )
				{
					Result = szPath;
					CoTaskMemFree( szPath );
				}

				pItem->Release();
			}
		}

		pDialog->Release();
		return Result;
	}

	// the command of the program (account_panel_launch.txt: one line, UTF-8, as it is written)
	std::string g_LaunchLine;

	auto LoadLaunch() -> void
	{
		g_LaunchLine = Accounts::ReadLaunchLine( g_Paths );
	}

	auto SaveLaunch( const std::string& Line ) -> void
	{
		g_LaunchLine = Line;
		Accounts::WriteLaunchLine( g_Paths , Line );
	}

	// parsed again when the line changes and every 2 s (the files of the command may come and go)
	auto LaunchCommand() -> const Launch::Command_t&
	{
		static Launch::Command_t s_Command;
		static std::string s_Line;
		static double s_Next = -1.0;

		const double Time = ImGui::GetTime();

		if ( s_Line != g_LaunchLine || Time >= s_Next )
		{
			s_Line = g_LaunchLine;
			s_Command = Launch::Parse( s_Line , g_Paths.BaseDir );
			s_Next = Time + 2.0;
		}

		return s_Command;
	}

	// where the command points: the line under it on the card and in the dialog
	auto CommandTarget( const Launch::Command_t& Command , ImU32& Color ) -> std::string
	{
		Color = Style::kMuted;

		if ( Command.Program.empty() )
			return "Выбери exe: панель войдёт в аккаунт (Steam без окна), закроет Steam и запустит её";

		// (Nunito has no arrows)
		const std::string Arguments = Command.Arguments.empty() ? std::string() : " " + Command.Arguments;

		if ( Command.Exists )
			return Steam::PathText( Command.Program ) + Arguments;

		if ( !Command.Program.has_parent_path() )
			return Steam::PathText( Command.Program ) + " (из PATH)" + Arguments;

		Color = Style::kBad;
		return "файл не найден: " + Steam::PathText( Command.Program );
	}

	// ---------------------------------------------------------------- the time
	using Accounts::Now;
	using Accounts::FormatDate;
	using Accounts::FormatDuration;

	// ---------------------------------------------------------------- the textures (the avatars, the portraits)
	auto LoadTexture( const std::wstring& Path ) -> ID3D11ShaderResourceView*
	{
		FILE* pFile = nullptr;

		if ( Path.empty() || _wfopen_s( &pFile , Path.c_str() , L"rb" ) != 0 || !pFile )
			return nullptr;

		int Width = 0 , Height = 0 , Channels = 0;
		unsigned char* pPixels = stbi_load_from_file( pFile , &Width , &Height , &Channels , 4 );
		fclose( pFile );

		if ( !pPixels )
			return nullptr;

		D3D11_TEXTURE2D_DESC Desc = {};
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA Data = {};
		Data.pSysMem = pPixels;
		Data.SysMemPitch = Width * 4;

		ID3D11Texture2D* pTexture = nullptr;
		ID3D11ShaderResourceView* pView = nullptr;

		if ( SUCCEEDED( g_pDevice->CreateTexture2D( &Desc , &Data , &pTexture ) ) && pTexture )
		{
			g_pDevice->CreateShaderResourceView( pTexture , nullptr , &pView );
			pTexture->Release();
		}

		stbi_image_free( pPixels );
		return pView;
	}

	// ---------------------------------------------------------------- the heroes (x64\Release\heroes)
	struct Heroes_t
	{
		std::map<uint32_t , std::string> Names;		// the id of the hero -> the name (abilities.txt, H|id|...|name)
		std::map<std::string , std::string> Icons;	// the name -> the file of the portrait (manifest.txt, model|icon|name)
		std::map<std::string , ID3D11ShaderResourceView*> Textures;
		std::wstring Dir;
	};

	Heroes_t g_Heroes;

	auto LoadHeroes( const std::wstring& DataDir ) -> void
	{
		g_Heroes.Dir = DataDir + L"\\heroes";

		g_Heroes.Names = Accounts::ReadHeroNames( DataDir );

		std::string Line;
		std::ifstream Manifest( g_Heroes.Dir + L"\\manifest.txt" );

		while ( std::getline( Manifest , Line ) )
		{
			const size_t A = Line.find( '|' );
			const size_t B = A == std::string::npos ? std::string::npos : Line.find( '|' , A + 1 );

			if ( B == std::string::npos )
				continue;

			std::string Name = Line.substr( B + 1 );

			while ( !Name.empty() && ( Name.back() == '\r' || Name.back() == ' ' ) )
				Name.pop_back();

			if ( !g_Heroes.Icons.count( Name ) )
				g_Heroes.Icons[Name] = Line.substr( A + 1 , B - A - 1 );
		}
	}

	auto HeroName( uint32_t Id ) -> std::string
	{
		const auto Found = g_Heroes.Names.find( Id );
		return Found != g_Heroes.Names.end() ? Found->second : ( Id ? "#" + std::to_string( Id ) : std::string( "—" ) );
	}

	auto HeroTexture( uint32_t Id ) -> ID3D11ShaderResourceView*
	{
		const auto Name = g_Heroes.Names.find( Id );

		if ( Name == g_Heroes.Names.end() )
			return nullptr;

		const auto Icon = g_Heroes.Icons.find( Name->second );

		if ( Icon == g_Heroes.Icons.end() )
			return nullptr;

		auto Found = g_Heroes.Textures.find( Icon->second );

		if ( Found == g_Heroes.Textures.end() )
			Found = g_Heroes.Textures.emplace( Icon->second , LoadTexture( g_Heroes.Dir + L"\\" + Steam::Wide( Icon->second ) + L".png" ) ).first;

		return Found->second;
	}

	// ---------------------------------------------------------------- the accounts
	using Accounts::Row_t;

	struct State_t
	{
		Steam::Info_t Info;
		std::filesystem::path CitadelDir;
		std::filesystem::path DataDir;
		std::vector<Row_t> Rows;
		bool SteamRunning = false;
		uint32_t Selected = 0;
		bool ShowHidden = false;
		int HiddenCount = 0;		// of the accounts found now
		double NextRefresh = 0.0;
		std::map<uint64_t , ID3D11ShaderResourceView*> Avatars;
	};

	State_t g_State;

	// the accounts taken off the list (account_panel_hidden.txt): only the list of the panel, Steam and the files of the game are not touched
	auto SetHidden( uint32_t AccountId , bool Hide ) -> void
	{
		Accounts::SetHidden( g_Paths , AccountId , Hide );
		g_State.NextRefresh = 0.0;
	}

	auto Refresh() -> void
	{
		auto& St = g_State;
		auto Snapshot = Accounts::Load( g_Paths , St.ShowHidden );

		St.Info = std::move( Snapshot.Info );
		St.SteamRunning = Snapshot.SteamRunning;
		St.CitadelDir = std::move( Snapshot.CitadelDir );
		St.Rows = std::move( Snapshot.Rows );
		St.HiddenCount = Snapshot.HiddenCount;

		if ( !St.HiddenCount )
			St.ShowHidden = false;

		const bool SelectedExists = std::any_of( St.Rows.begin() , St.Rows.end() , [&]( const Row_t& Row ) { return Row.AccountId == St.Selected; } );

		if ( !SelectedExists )
			St.Selected = St.Rows.empty() ? 0 : St.Rows.front().AccountId;
	}

	auto Avatar( uint64_t SteamId ) -> ID3D11ShaderResourceView*
	{
		auto Found = g_State.Avatars.find( SteamId );

		if ( Found == g_State.Avatars.end() )
			Found = g_State.Avatars.emplace( SteamId , LoadTexture( Steam::AvatarPath( g_State.Info.SteamPath , SteamId ) ) ).first;

		return Found->second;
	}

	// ---------------------------------------------------------------- the switch (its own thread)
	struct Switch_t
	{
		std::atomic<bool> Running = false;
		std::atomic<int> Ask = 0;	// 1 - the question "end the process of Steam?" is up, 2 - yes, 3 - no
		std::mutex Lock;
		std::string Status;
		bool Failed = false;
	};

	Switch_t g_Switch;

	auto SetStatus( const std::string& Text , bool Failed = false ) -> void
	{
		std::scoped_lock Lock( g_Switch.Lock );
		g_Switch.Status = Text;
		g_Switch.Failed = Failed;
	}

	using Accounts::EAction;
	using enum Accounts::EAction;

	auto StartAction( const Row_t& Row , EAction Action ) -> void
	{
		if ( g_Switch.Running.exchange( true ) )
			return;

		const Steam::Info_t Info = g_State.Info;
		const Steam::Account_t Account = Row.Steam;
		const Launch::Command_t Command = LaunchCommand();

		std::thread( [Info , Account , Action , Command]()
		{
			Steam::SwitchCallbacks_t Callbacks;
			Callbacks.Status = []( const std::string& Text ) { SetStatus( Text ); };
			Callbacks.AskKill = []()
			{
				g_Switch.Ask = 1;

				while ( g_Switch.Ask == 1 )
					std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

				return g_Switch.Ask.exchange( 0 ) == 2;
			};

			std::string Message;
			const bool Ok = Accounts::Run( Info , Account , Action , Command , Callbacks , Message );
			SetStatus( Message , !Ok );

			g_State.NextRefresh = 0.0;
			g_Switch.Running = false;
		} ).detach();
	}

	// ---------------------------------------------------------------- the fonts
	enum EFont { FONT_REG , FONT_MED , FONT_SEMI , FONT_BOLD , FONT_KINDS };

	std::map<int , ImFont*> g_Fonts[FONT_KINDS];

	auto Font( EFont Kind , int Size ) -> ImFont*
	{
		auto& Map = g_Fonts[Kind];

		if ( Map.empty() )
			return ImGui::GetFont();

		// the nearest loaded size: a letter is drawn only at the size it was loaded at (FreeType)
		auto Best = Map.begin();

		for ( auto It = Map.begin(); It != Map.end(); ++It )
		{
			if ( std::abs( It->first - Size ) < std::abs( Best->first - Size ) )
				Best = It;
		}

		return Best->second;
	}

	auto LoadFonts( const std::wstring& DataDir ) -> void
	{
		static const ImWchar kRanges[] = { 0x0020 , 0x00FF , 0x0400 , 0x052F , 0x2000 , 0x206F , 0x2190 , 0x21FF , 0x2500 , 0x25FF , 0 };

		auto& Io = ImGui::GetIO();

		const std::wstring Fonts = DataDir + L"\\fonts\\";
		wchar_t Windows[MAX_PATH] = {};
		GetWindowsDirectoryW( Windows , MAX_PATH );
		const std::wstring System = std::wstring( Windows ) + L"\\Fonts\\";

		const auto Pick = [&]( const wchar_t* szNunito , const wchar_t* szSystem ) -> std::string
		{
			if ( FileExists( Fonts + szNunito ) )
				return Steam::Utf8( Fonts + szNunito );

			return Steam::Utf8( System + szSystem );
		};

		const std::string Files[FONT_KINDS] = {
			Pick( L"NunitoSans-Regular.ttf" , L"segoeui.ttf" ) ,
			Pick( L"NunitoSans-Medium.ttf" , L"segoeui.ttf" ) ,
			Pick( L"NunitoSans-SemiBold.ttf" , L"seguisb.ttf" ) ,
			Pick( L"NunitoSans-ExtraBold.ttf" , L"segoeuib.ttf" ) ,
		};

		const std::vector<int> Sizes[FONT_KINDS] = { { 15 , 16 } , { 15 , 16 , 18 } , { 18 , 22 , 28 } , { 22 } };

		ImFontConfig Config;
		Config.OversampleH = 1;
		Config.OversampleV = 1;

		for ( int Kind = 0; Kind < FONT_KINDS; Kind++ )
		{
			for ( const int Size : Sizes[Kind] )
			{
				if ( ImFont* pFont = Io.Fonts->AddFontFromFileTTF( Files[Kind].c_str() , std::round( S( static_cast<float>( Size ) ) ) , &Config , kRanges ) )
					g_Fonts[Kind][Size] = pFont;
			}
		}

		if ( g_Fonts[FONT_MED].count( 16 ) )
			Io.FontDefault = g_Fonts[FONT_MED][16];
	}

	// ---------------------------------------------------------------- the drawing helpers
	auto TextSize( ImFont* pFont , const std::string& Text ) -> ImVec2
	{
		return pFont->CalcTextSizeA( pFont->FontSize , FLT_MAX , 0.f , Text.c_str() );
	}

	auto PutText( ImDrawList* pDraw , ImFont* pFont , ImVec2 Pos , ImU32 Color , const std::string& Text ) -> void
	{
		pDraw->AddText( pFont , pFont->FontSize , ImVec2( std::floor( Pos.x ) , std::floor( Pos.y ) ) , Color , Text.c_str() );
	}

	// the text cut to the width with "…"
	auto Ellipsize( ImFont* pFont , const std::string& Text , float MaxWidth ) -> std::string
	{
		if ( TextSize( pFont , Text ).x <= MaxWidth )
			return Text;

		std::string Cut = Text;

		while ( !Cut.empty() )
		{
			size_t Size = Cut.size() - 1;

			while ( Size > 0 && ( static_cast<unsigned char>( Cut[Size] ) & 0xC0 ) == 0x80 )
				Size--;

			Cut.resize( Size );

			if ( TextSize( pFont , Cut + "…" ).x <= MaxWidth )
				return Cut + "…";
		}

		return "…";
	}

	auto DrawImage( ImDrawList* pDraw , ID3D11ShaderResourceView* pTexture , ImVec2 Min , ImVec2 Max , float Rounding ) -> void
	{
		pDraw->AddImageRounded( reinterpret_cast<ImTextureID>( pTexture ) , Min , Max , ImVec2( 0.f , 0.f ) , ImVec2( 1.f , 1.f ) , IM_COL32_WHITE , Rounding );
	}

	// the first letter of the nick when there is no avatar
	auto DrawInitials( ImDrawList* pDraw , ImVec2 Min , float Size , const std::string& Name , float Rounding ) -> void
	{
		pDraw->AddRectFilled( Min , ImVec2( Min.x + Size , Min.y + Size ) , Style::kCardBorderHover , Rounding );

		std::string Letter = "?";

		if ( !Name.empty() )
		{
			unsigned int Char = 0;
			const int Length = ImTextCharFromUtf8( &Char , Name.c_str() , nullptr );
			Letter = Length == 1 ? std::string( 1 , static_cast<char>( toupper( static_cast<unsigned char>( Name[0] ) ) ) ) : Name.substr( 0 , Length );
		}

		ImFont* pFont = Font( FONT_SEMI , Size > S( 30.f ) ? 22 : 19 );
		const ImVec2 TS = TextSize( pFont , Letter );
		PutText( pDraw , pFont , ImVec2( Min.x + ( Size - TS.x ) * 0.5f , Min.y + ( Size - TS.y ) * 0.5f ) , Style::kSoft , Letter );
	}

	auto DrawAvatar( ImDrawList* pDraw , const Row_t* pRow , ImVec2 Min , float Size , float Rounding ) -> void;

	enum EIcon { ICON_PLAY , ICON_CLOSE , ICON_MINUS };

	auto DrawIcon( ImDrawList* pDraw , EIcon Icon , ImVec2 C , ImU32 Color ) -> void
	{
		const float T = S( 1.4f );

		switch ( Icon )
		{
			case ICON_PLAY:
				pDraw->AddTriangle( ImVec2( C.x - S( 4.f ) , C.y - S( 5.5f ) ) , ImVec2( C.x - S( 4.f ) , C.y + S( 5.5f ) ) , ImVec2( C.x + S( 5.f ) , C.y ) , Color , T );
				break;
			case ICON_CLOSE:
				pDraw->AddLine( ImVec2( C.x - S( 5.f ) , C.y - S( 5.f ) ) , ImVec2( C.x + S( 5.f ) , C.y + S( 5.f ) ) , Color , T );
				pDraw->AddLine( ImVec2( C.x + S( 5.f ) , C.y - S( 5.f ) ) , ImVec2( C.x - S( 5.f ) , C.y + S( 5.f ) ) , Color , T );
				break;
			case ICON_MINUS:
				pDraw->AddLine( ImVec2( C.x - S( 6.f ) , C.y ) , ImVec2( C.x + S( 6.f ) , C.y ) , Color , T );
				break;
		}
	}

	auto Tooltip( bool Hovered , const char* szTooltip ) -> void
	{
		if ( Hovered && szTooltip && *szTooltip )
		{
			ImGui::PushStyleColor( ImGuiCol_Text , Style::Vec( Style::kLabel ) );
			ImGui::SetTooltip( "%s" , szTooltip );
			ImGui::PopStyleColor();
		}
	}

	// the main button: the accent, the light text, the icon on the left
	auto PrimaryButton( const char* szId , const std::string& Label , bool Play , bool Enabled , float Width , const char* szTooltip = nullptr ) -> bool
	{
		const float Height = S( 34.f );
		const ImVec2 Pos = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton( szId , ImVec2( Width , Height ) );

		const bool Hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled );
		const bool Clicked = Enabled && ImGui::IsItemClicked();

		if ( Hovered && Enabled )
			ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );

		auto* pDraw = ImGui::GetWindowDrawList();
		const ImU32 Back = !Enabled ? Style::Hex( 0x2A3350 ) : Hovered ? Style::kAccentHover : Style::kAccent;
		const ImU32 Text = Enabled ? Style::kAccentText : Style::Hex( 0x75819C );

		pDraw->AddRectFilled( Pos , ImVec2( Pos.x + Width , Pos.y + Height ) , Back , S( Style::kButtonRounding ) );

		ImFont* pFont = Font( FONT_MED , 16 );
		const float TextWidth = TextSize( pFont , Label ).x;
		const float IconWidth = Play ? S( 19.f ) : 0.f;
		const float X = Pos.x + ( Width - TextWidth - IconWidth ) * 0.5f;

		if ( Play )
			DrawIcon( pDraw , ICON_PLAY , ImVec2( X + S( 5.f ) , Pos.y + Height * 0.5f ) , Text );

		PutText( pDraw , pFont , ImVec2( X + IconWidth , Pos.y + ( Height - pFont->FontSize ) * 0.5f - S( 1.f ) ) , Text , Label );
		Tooltip( Hovered , szTooltip );
		return Clicked;
	}

	// the text button (the links at the bottom of a dialog, the items of the side bar)
	auto LinkButton( const char* szId , const std::string& Label , bool Enabled , float Height , ImU32 Color , const char* szTooltip = nullptr ,
					 bool Active = false ) -> bool
	{
		ImFont* pFont = Font( FONT_MED , 18 );
		const ImVec2 TS = TextSize( pFont , Label );
		const ImVec2 Pos = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton( szId , ImVec2( TS.x , Height ) );

		const bool Hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled );
		const bool Clicked = Enabled && ImGui::IsItemClicked();

		if ( Hovered && Enabled )
			ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );

		const ImU32 Text = !Enabled ? Style::Alpha( Color , 0.45f ) : ( Hovered || Active ) ? Style::kText : Color;
		PutText( ImGui::GetWindowDrawList() , pFont , ImVec2( Pos.x , Pos.y + ( Height - pFont->FontSize ) * 0.5f - S( 1.f ) ) , Text , Label );

		Tooltip( Hovered , szTooltip );
		return Clicked;
	}

	// the minimize and close of the window, the close of a dialog
	auto GlyphButton( const char* szId , EIcon Icon , ImVec2 Center , float Size ) -> bool
	{
		ImGui::SetCursorScreenPos( ImVec2( Center.x - Size * 0.5f , Center.y - Size * 0.5f ) );
		ImGui::InvisibleButton( szId , ImVec2( Size , Size ) );

		const bool Hovered = ImGui::IsItemHovered();

		if ( Hovered )
			ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );

		DrawIcon( ImGui::GetWindowDrawList() , Icon , Center , Hovered ? Style::kText : Style::Hex( 0xC9CCD2 ) );
		return ImGui::IsItemClicked();
	}

	// ---------------------------------------------------------------- the window
	using Accounts::DisplayName;

	auto DrawAvatar( ImDrawList* pDraw , const Row_t* pRow , ImVec2 Min , float Size , float Rounding ) -> void
	{
		if ( pRow )
		{
			if ( auto* pAvatar = Avatar( pRow->Steam.SteamId ) )
			{
				DrawImage( pDraw , pAvatar , Min , ImVec2( Min.x + Size , Min.y + Size ) , Rounding );
				return;
			}
		}

		DrawInitials( pDraw , Min , Size , pRow ? DisplayName( *pRow ) : std::string( "?" ) , Rounding );
	}

	auto FindRow( uint32_t AccountId ) -> const Row_t*
	{
		for ( const auto& Row : g_State.Rows )
		{
			if ( Row.AccountId == AccountId )
				return &Row;
		}

		return nullptr;
	}

	// the state of the account: the pills (the list shows the first one)
	struct Status_t
	{
		std::string Text;
		ImU32 Color = Style::kSoft;
	};

	auto AccountStatus( const Row_t& Row ) -> std::vector<Status_t>
	{
		std::vector<Status_t> Pills;

		for ( const auto& Status : Accounts::AccountStatus( Row , Now() ) )
		{
			const ImU32 Color = Status.Severity == Accounts::SEVERITY_BAD ? Style::kBad
				: Status.Severity == Accounts::SEVERITY_WARN ? Style::kWarn
				: Status.Severity == Accounts::SEVERITY_OK ? Style::kLink
				: Style::kMuted;

			Pills.push_back( { Status.Text , Color } );
		}

		return Pills;
	}

	auto PillWidth( const Status_t& Pill ) -> float
	{
		return TextSize( Font( FONT_MED , 15 ) , Pill.Text ).x + S( 18.f );
	}

	auto DrawPill( ImDrawList* pDraw , ImVec2 Pos , const Status_t& Pill ) -> float
	{
		ImFont* pFont = Font( FONT_MED , 15 );
		const float Width = PillWidth( Pill );
		const float Height = S( 24.f );

		pDraw->AddRectFilled( Pos , ImVec2( Pos.x + Width , Pos.y + Height ) , Style::Alpha( Pill.Color , 0.12f ) , Height * 0.5f );
		PutText( pDraw , pFont , ImVec2( Pos.x + S( 9.f ) , Pos.y + ( Height - pFont->FontSize ) * 0.5f - S( 1.f ) ) , Pill.Color , Pill.Text );
		return Width;
	}

	// the second button: the card with a frame
	auto GhostButton( const char* szId , const std::string& Label , bool Enabled , float Width , const char* szTooltip = nullptr ) -> bool
	{
		const float Height = S( 34.f );
		const ImVec2 Pos = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton( szId , ImVec2( Width , Height ) );

		const bool Hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled );
		const bool Clicked = Enabled && ImGui::IsItemClicked();

		if ( Hovered && Enabled )
			ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );

		auto* pDraw = ImGui::GetWindowDrawList();
		const ImVec2 Max( Pos.x + Width , Pos.y + Height );
		const bool Lit = Hovered && Enabled;

		pDraw->AddRectFilled( Pos , Max , Lit ? Style::kCardHover : Style::kCard , S( Style::kButtonRounding ) );
		pDraw->AddRect( Pos , Max , Lit ? Style::kCardBorderHover : Style::kCardBorder , S( Style::kButtonRounding ) , 0 , S( 1.f ) );

		ImFont* pFont = Font( FONT_MED , 16 );
		const float TextWidth = TextSize( pFont , Label ).x;
		PutText( pDraw , pFont , ImVec2( Pos.x + ( Width - TextWidth ) * 0.5f , Pos.y + ( Height - pFont->FontSize ) * 0.5f - S( 1.f ) ) ,
				 Enabled ? Style::kText : Style::kMuted , Label );

		Tooltip( Hovered , szTooltip );
		return Clicked;
	}

	auto ButtonWidth( const std::string& Label , bool Play = false ) -> float
	{
		return TextSize( Font( FONT_MED , 16 ) , Label ).x + S( 32.f ) + ( Play ? S( 19.f ) : 0.f );
	}

	int g_PreviewDialog = 0;	// --preview: 2 - the question "Deadlock is running" is open

	// ---------------------------------------------------------------- the dialogs
	auto BeginDialog( const char* szId , float Width , ImVec2 Padding ) -> bool
	{
		const ImVec2 Display = ImGui::GetIO().DisplaySize;

		ImGui::SetNextWindowPos( ImVec2( Display.x * 0.5f , Display.y * 0.5f ) , ImGuiCond_Always , ImVec2( 0.5f , 0.5f ) );
		ImGui::SetNextWindowSize( ImVec2( S( Width ) , 0.f ) );

		// a modal window takes the rounding and the border of a window, not of a popup
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , Padding );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding , S( Style::kDialogRounding ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize , 1.f );
		const bool Open = ImGui::BeginPopupModal( szId , nullptr , ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
												  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar );
		ImGui::PopStyleVar( 3 );
		return Open;
	}

	// a question: the title, the text, "Отмена" and the main button on the right. 0 - nothing, 1 - cancel, 2 - yes
	auto Question( const char* szId , const std::string& Title , const std::string& Text , const std::string& Yes ) -> int
	{
		int Answer = 0;

		if ( !BeginDialog( szId , 460.f , ImVec2( S( 24.f ) , S( 22.f ) ) ) )
			return Answer;

		ImGui::PushFont( Font( FONT_SEMI , 22 ) );
		ImGui::PushStyleColor( ImGuiCol_Text , Style::Vec( Style::kText ) );
		ImGui::TextUnformatted( Title.c_str() );
		ImGui::PopStyleColor();
		ImGui::PopFont();

		ImGui::Dummy( ImVec2( 0.f , S( 2.f ) ) );
		ImGui::PushFont( Font( FONT_REG , 16 ) );
		ImGui::PushStyleColor( ImGuiCol_Text , Style::Vec( Style::kSoft ) );
		ImGui::PushTextWrapPos( 0.f );
		ImGui::TextUnformatted( Text.c_str() );
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::PopFont();
		ImGui::Dummy( ImVec2( 0.f , S( 14.f ) ) );

		const float YesW = ButtonWidth( Yes );
		const float NoW = ButtonWidth( "Отмена" );
		ImGui::SetCursorPosX( ImGui::GetWindowWidth() - S( 24.f ) - YesW - S( 10.f ) - NoW );

		if ( GhostButton( "##no" , "Отмена" , true , NoW ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
		{
			Answer = 1;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine( 0.f , S( 10.f ) );

		if ( PrimaryButton( "##yes" , Yes , false , true , YesW ) )
		{
			Answer = 2;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
		return Answer;
	}

	// the command of the program by hand: the field, where it points, "Обзор…" for the exe
	char g_CommandBuffer[2048] = {};

	auto OpenCommandDialog() -> void
	{
		snprintf( g_CommandBuffer , sizeof( g_CommandBuffer ) , "%s" , g_LaunchLine.c_str() );
		ImGui::OpenPopup( "##command" );
	}

	auto RenderCommandDialog( HWND hWnd ) -> void
	{
		const float Pad = S( 24.f );

		if ( !BeginDialog( "##command" , 600.f , ImVec2( Pad , S( 22.f ) ) ) )
			return;

		ImGui::PushFont( Font( FONT_SEMI , 22 ) );
		ImGui::PushStyleColor( ImGuiCol_Text , Style::Vec( Style::kText ) );
		ImGui::TextUnformatted( "Команда запуска" );
		ImGui::PopStyleColor();
		ImGui::PopFont();

		ImGui::Dummy( ImVec2( 0.f , S( 2.f ) ) );
		ImGui::PushFont( Font( FONT_REG , 16 ) );
		ImGui::PushStyleColor( ImGuiCol_Text , Style::Vec( Style::kSoft ) );
		ImGui::PushTextWrapPos( 0.f );
		ImGui::TextUnformatted( "Exe и аргументы, как в cmd. ./ и ../ — от папки панели (и в аргументах), %ПЕРЕМЕННЫЕ% раскрываются, "
								"путь с пробелами — в кавычках. Программа запускается из своей папки." );
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::PopFont();
		ImGui::Dummy( ImVec2( 0.f , S( 8.f ) ) );

		// the field in the color of the window with the frame of the cards
		ImGui::PushStyleVar( ImGuiStyleVar_FramePadding , ImVec2( S( 10.f ) , S( 8.f ) ) );
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding , S( Style::kButtonRounding ) );
		ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize , S( 1.f ) );
		ImGui::PushStyleColor( ImGuiCol_FrameBg , Style::Vec( Style::kBg ) );
		ImGui::PushStyleColor( ImGuiCol_FrameBgHovered , Style::Vec( Style::kBg ) );
		ImGui::PushStyleColor( ImGuiCol_FrameBgActive , Style::Vec( Style::kBg ) );
		ImGui::PushStyleColor( ImGuiCol_Border , Style::Vec( Style::kCardBorderHover ) );
		ImGui::PushStyleColor( ImGuiCol_TextSelectedBg , Style::Vec( Style::Alpha( Style::kAccent , 0.35f ) ) );
		ImGui::PushStyleColor( ImGuiCol_NavHighlight , ImVec4( 0.f , 0.f , 0.f , 0.f ) );
		ImGui::PushFont( Font( FONT_REG , 16 ) );

		if ( ImGui::IsWindowAppearing() )
			ImGui::SetKeyboardFocusHere();

		ImGui::SetNextItemWidth( -FLT_MIN );
		const bool Enter = ImGui::InputText( "##line" , g_CommandBuffer , sizeof( g_CommandBuffer ) , ImGuiInputTextFlags_EnterReturnsTrue );

		ImGui::PopFont();
		ImGui::PopStyleColor( 6 );
		ImGui::PopStyleVar( 3 );

		// where it points (the files are checked on every frame only while the dialog is open)
		const Launch::Command_t Command = Launch::Parse( g_CommandBuffer , g_Paths.BaseDir );
		ImU32 Color = Style::kMuted;
		const std::string Target = CommandTarget( Command , Color );

		ImGui::Dummy( ImVec2( 0.f , S( 2.f ) ) );
		ImGui::PushFont( Font( FONT_REG , 15 ) );
		ImGui::PushStyleColor( ImGuiCol_Text , Style::Vec( Color ) );
		ImGui::PushTextWrapPos( 0.f );
		ImGui::TextUnformatted( Target.c_str() );
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::PopFont();
		ImGui::Dummy( ImVec2( 0.f , S( 14.f ) ) );

		// "Обзор…" on the left, "Отмена" and "Сохранить" on the right
		const std::string Browse = "Обзор…";

		if ( GhostButton( "##browse" , Browse , hWnd != nullptr , ButtonWidth( Browse ) , "Выбрать exe: из папки панели запишется как ./…" ) )
		{
			const std::wstring Path = PickExe( hWnd , ( Command.Exists ? Command.Directory : g_Paths.BaseDir ).wstring() );

			if ( !Path.empty() )
			{
				const std::string Line = Launch::ReplaceProgram( g_CommandBuffer , Path , g_Paths.BaseDir );
				snprintf( g_CommandBuffer , sizeof( g_CommandBuffer ) , "%s" , Line.c_str() );
			}
		}

		const float SaveW = ButtonWidth( "Сохранить" );
		const float NoW = ButtonWidth( "Отмена" );
		ImGui::SameLine( ImGui::GetWindowWidth() - Pad - SaveW - S( 10.f ) - NoW );

		if ( GhostButton( "##no" , "Отмена" , true , NoW ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
			ImGui::CloseCurrentPopup();

		ImGui::SameLine( 0.f , S( 10.f ) );

		if ( PrimaryButton( "##save" , "Сохранить" , false , true , SaveW ) || Enter )
		{
			std::string Line = g_CommandBuffer;
			Line.erase( 0 , Line.find_first_not_of( " \t" ) == std::string::npos ? Line.size() : Line.find_first_not_of( " \t" ) );
			Line.erase( Line.find_last_not_of( " \t" ) + 1 );

			SaveLaunch( Line );
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	// ---------------------------------------------------------------- the parts of the window
	auto RenderHeader( HWND hWnd , ImVec2 Size ) -> void
	{
		auto* pDraw = ImGui::GetWindowDrawList();
		const float Height = S( Style::kHeaderH );
		const float MidY = Height * 0.5f;

		pDraw->AddLine( ImVec2( 0.f , Height - 1.f ) , ImVec2( Size.x , Height - 1.f ) , Style::kLine );

		// the name of the tool
		ImFont* pName = Font( FONT_BOLD , 20 );
		PutText( pDraw , pName , ImVec2( S( 20.f ) , MidY - pName->FontSize * 0.5f - S( 1.f ) ) , Style::kText , "Аккаунты" );

		// the minimize and the close of the window
		if ( GlyphButton( "##close" , ICON_CLOSE , ImVec2( Size.x - S( 30.f ) , MidY ) , S( 28.f ) ) && hWnd )
			PostMessageW( hWnd , WM_CLOSE , 0 , 0 );

		if ( GlyphButton( "##minimize" , ICON_MINUS , ImVec2( Size.x - S( 64.f ) , MidY ) , S( 28.f ) ) && hWnd )
			ShowWindow( hWnd , SW_MINIMIZE );

		// Steam and the account it is logged in with: a pill before the buttons of the window
		const auto& Info = g_State.Info;
		std::string Text;

		if ( Info.SteamPath.empty() )
			Text = "Steam не найден";
		else if ( !g_State.SteamRunning )
			Text = "Steam закрыт";
		else if ( !Info.ActiveUser )
			Text = "Steam: вход не выполнен";
		else
		{
			const Row_t* pActive = FindRow( Info.ActiveUser );
			Text = "Steam: " + ( pActive ? DisplayName( *pActive ) : "ID " + std::to_string( Info.ActiveUser ) );
		}

		ImFont* pFont = Font( FONT_MED , 15 );
		const float PillH = S( 28.f );
		const float PillW = TextSize( pFont , Text ).x + S( 34.f );
		const ImVec2 PillMin( Size.x - S( 90.f ) - PillW , MidY - PillH * 0.5f );

		pDraw->AddRectFilled( PillMin , ImVec2( PillMin.x + PillW , PillMin.y + PillH ) , Style::kCard , PillH * 0.5f );
		pDraw->AddRect( PillMin , ImVec2( PillMin.x + PillW , PillMin.y + PillH ) , Style::kCardBorder , PillH * 0.5f , 0 , S( 1.f ) );
		pDraw->AddCircleFilled( ImVec2( PillMin.x + S( 14.f ) , MidY ) , S( 3.5f ) , Info.ActiveUser && g_State.SteamRunning ? Style::kLink : Style::kMuted );
		PutText( pDraw , pFont , ImVec2( PillMin.x + S( 24.f ) , MidY - pFont->FontSize * 0.5f - S( 1.f ) ) , Style::kLabel , Text );
	}

	auto RenderList( ImVec2 Min , ImVec2 Size ) -> void
	{
		ImGui::SetCursorScreenPos( Min );
		ImGui::BeginChild( "##list" , Size , ImGuiChildFlags_None , ImGuiWindowFlags_NoBackground );

		auto* pDraw = ImGui::GetWindowDrawList();
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		const float Width = Size.x - S( 10.f );
		const float RowH = S( Style::kRowH );
		const float Gap = S( 8.f );
		const float Rounding = S( Style::kCardRounding );

		ImFont* pCaption = Font( FONT_MED , 15 );
		PutText( pDraw , pCaption , Origin , Style::kMuted , "Аккаунты · " + std::to_string( g_State.Rows.size() ) );

		float Y = Origin.y + pCaption->FontSize + S( 12.f );

		for ( const auto& Row : g_State.Rows )
		{
			const ImVec2 RowMin( Origin.x , Y );
			const ImVec2 RowMax( Origin.x + Width , Y + RowH );
			Y += RowH + Gap;

			ImGui::SetCursorScreenPos( RowMin );
			ImGui::PushID( static_cast<int>( Row.AccountId ) );
			ImGui::InvisibleButton( "##row" , ImVec2( Width , RowH ) );
			ImGui::PopID();

			const bool Hovered = ImGui::IsItemHovered();

			if ( Hovered )
				ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );

			if ( ImGui::IsItemClicked() )
				g_State.Selected = Row.AccountId;

			const bool Selected = Row.AccountId == g_State.Selected;
			const bool Active = Row.AccountId == g_State.Info.ActiveUser;

			pDraw->AddRectFilled( RowMin , RowMax , Selected ? Style::kCardSelected : Hovered ? Style::kCardHover : Style::kCard , Rounding );
			pDraw->AddRect( RowMin , RowMax , Selected ? Style::kCardBorderSelected : Hovered ? Style::kCardBorderHover : Style::kCardBorder , Rounding , 0 , S( 1.f ) );

			// the avatar, the dot of "logged in" on its corner
			const float AvatarSize = S( 42.f );
			const ImVec2 AvatarMin( RowMin.x + S( 13.f ) , RowMin.y + ( RowH - AvatarSize ) * 0.5f );
			DrawAvatar( pDraw , &Row , AvatarMin , AvatarSize , S( 8.f ) );

			if ( Active )
			{
				const ImVec2 Dot( AvatarMin.x + AvatarSize - S( 1.f ) , AvatarMin.y + AvatarSize - S( 1.f ) );
				pDraw->AddCircleFilled( Dot , S( 6.5f ) , Selected ? Style::kCardSelected : Style::kCard );
				pDraw->AddCircleFilled( Dot , S( 4.5f ) , Style::kLink );
			}

			// the pill on the right, the nick and the login before it
			const auto Pills = AccountStatus( Row );
			const float PillW = PillWidth( Pills.front() );
			const float PillX = RowMax.x - S( 12.f ) - PillW;
			DrawPill( pDraw , ImVec2( PillX , RowMin.y + ( RowH - S( 24.f ) ) * 0.5f ) , Pills.front() );

			const float TextX = AvatarMin.x + AvatarSize + S( 13.f );
			const float TextMax = PillX - S( 10.f ) - TextX;
			ImFont* pNick = Font( FONT_SEMI , 18 );
			ImFont* pLogin = Font( FONT_REG , 15 );

			PutText( pDraw , pNick , ImVec2( TextX , RowMin.y + S( 12.f ) ) , Row.Data.Valid ? Style::kText : Style::kDim , Ellipsize( pNick , DisplayName( Row ) , TextMax ) );

			std::string Login = Row.HasSteam ? Row.Steam.AccountName : "нет в Steam";

			if ( Row.Hidden )
				Login += " · убран";

			PutText( pDraw , pLogin , ImVec2( TextX , RowMin.y + S( 37.f ) ) , Active ? Style::kLink : Style::kMuted ,
					 Ellipsize( pLogin , Active ? Login + " · в сети" : Login , TextMax ) );

			// a hidden account (shown on request) is dimmed
			if ( Row.Hidden )
				pDraw->AddRectFilled( RowMin , RowMax , Style::Alpha( Style::kBg , 0.5f ) , Rounding );
		}

		if ( g_State.Rows.empty() )
		{
			ImFont* pFont = Font( FONT_REG , 16 );
			pDraw->AddText( pFont , pFont->FontSize , ImVec2( Origin.x , Y ) , Style::kMuted ,
							"Аккаунты не найдены: нет loginusers.vdf Steam и файлов cache_*.soc Deadlock." , nullptr , Width );
			Y += S( 60.f );
		}

		ImGui::SetCursorScreenPos( Origin );
		ImGui::Dummy( ImVec2( Width , Y - Origin.y ) );
		ImGui::EndChild();
	}

	// a card of the details: the title and the rows "the name ... the value" with the lines between them
	struct Field_t
	{
		std::string Label;
		std::string Value;
		ImU32 Color = Style::kText;
		std::string Sub;
		ID3D11ShaderResourceView* pIcon = nullptr;
	};

	struct Section_t
	{
		std::string Title;
		std::vector<Field_t> Fields;
	};

	auto SectionHeight( const Section_t& Section ) -> float
	{
		float Height = S( 52.f ) + S( 8.f );

		for ( const auto& Field : Section.Fields )
			Height += S( Field.Sub.empty() ? 40.f : 58.f );

		return Height;
	}

	auto DrawSection( ImDrawList* pDraw , ImVec2 Min , float Width , const Section_t& Section , float Height ) -> void
	{
		const ImVec2 Max( Min.x + Width , Min.y + Height );
		const float Pad = S( 18.f );

		pDraw->AddRectFilled( Min , Max , Style::kCard , S( Style::kCardRounding ) );
		pDraw->AddRect( Min , Max , Style::kCardBorder , S( Style::kCardRounding ) , 0 , S( 1.f ) );

		PutText( pDraw , Font( FONT_SEMI , 18 ) , ImVec2( Min.x + Pad , Min.y + S( 16.f ) ) , Style::kText , Section.Title );

		ImFont* pLabel = Font( FONT_REG , 16 );
		ImFont* pValue = Font( FONT_MED , 16 );
		ImFont* pSub = Font( FONT_REG , 15 );
		float Y = Min.y + S( 52.f );

		for ( const auto& Field : Section.Fields )
		{
			const float RowH = S( Field.Sub.empty() ? 40.f : 58.f );
			pDraw->AddLine( ImVec2( Min.x + Pad , Y ) , ImVec2( Max.x - Pad , Y ) , Style::kLine , S( 1.f ) );

			const float TextY = Y + S( 10.f );
			const float LabelW = TextSize( pLabel , Field.Label ).x;
			PutText( pDraw , pLabel , ImVec2( Min.x + Pad , TextY ) , Style::kSoft , Field.Label );

			const float ValueMax = Width - Pad * 2.f - LabelW - S( 16.f );
			const std::string Value = Ellipsize( pValue , Field.Value , ValueMax - ( Field.pIcon ? S( 28.f ) : 0.f ) );
			const float ValueW = TextSize( pValue , Value ).x;
			const float ValueX = Max.x - Pad - ValueW;
			PutText( pDraw , pValue , ImVec2( ValueX , TextY ) , Field.Color , Value );

			if ( Field.pIcon )
			{
				const float Icon = S( 22.f );
				const ImVec2 IconMin( ValueX - Icon - S( 7.f ) , TextY + ( pValue->FontSize - Icon ) * 0.5f );
				DrawImage( pDraw , Field.pIcon , IconMin , ImVec2( IconMin.x + Icon , IconMin.y + Icon ) , S( 4.f ) );
			}

			if ( !Field.Sub.empty() )
			{
				const std::string Sub = Ellipsize( pSub , Field.Sub , Width - Pad * 2.f );
				PutText( pDraw , pSub , ImVec2( Max.x - Pad - TextSize( pSub , Sub ).x , TextY + S( 21.f ) ) , Style::kMuted , Sub );
			}

			Y += RowH;
		}
	}

	auto BanField( const char* szLabel , uint32_t Until , ImU32 Color ) -> Field_t
	{
		const int64_t T = Now();

		if ( Until > T )
			return { szLabel , "до " + FormatDate( Until ) , Color , "осталось " + FormatDuration( Until - T ) };

		if ( Until > 0 )
			return { szLabel , "нет" , Style::kLink , "последний истёк " + FormatDuration( T - Until ) + " назад" };

		return { szLabel , "нет" , Style::kLink };
	}

	auto RenderDetails( HWND hWnd , ImVec2 Min , ImVec2 Size ) -> void
	{
		ImGui::SetCursorScreenPos( Min );
		ImGui::BeginChild( "##details" , Size , ImGuiChildFlags_None , ImGuiWindowFlags_NoBackground );

		auto* pDraw = ImGui::GetWindowDrawList();
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		const float Width = Size.x - S( 10.f );
		const Row_t* pRow = FindRow( g_State.Selected );

		if ( !pRow )
		{
			PutText( pDraw , Font( FONT_REG , 16 ) , Origin , Style::kMuted , "Выбери аккаунт слева." );
			ImGui::EndChild();
			return;
		}

		const auto& Row = *pRow;
		const auto& D = Row.Data;
		const bool Active = Row.AccountId == g_State.Info.ActiveUser;
		const int64_t T = Now();
		const float Pad = S( 20.f );

		// the head: the avatar, the nick, the ids, the pills, the buttons
		const float HeadH = S( 168.f );
		const ImVec2 HeadMax( Origin.x + Width , Origin.y + HeadH );

		pDraw->AddRectFilled( Origin , HeadMax , Style::kCard , S( Style::kCardRounding ) );
		pDraw->AddRect( Origin , HeadMax , Style::kCardBorder , S( Style::kCardRounding ) , 0 , S( 1.f ) );

		const float AvatarSize = S( 64.f );
		const ImVec2 AvatarMin( Origin.x + Pad , Origin.y + Pad );
		DrawAvatar( pDraw , pRow , AvatarMin , AvatarSize , S( 10.f ) );

		const float X = AvatarMin.x + AvatarSize + S( 18.f );
		ImFont* pName = Font( FONT_SEMI , 28 );
		ImFont* pIds = Font( FONT_REG , 15 );

		PutText( pDraw , pName , ImVec2( X , AvatarMin.y - S( 2.f ) ) , Style::kText , Ellipsize( pName , DisplayName( Row ) , HeadMax.x - Pad - X ) );

		const std::string Ids = ( Row.HasSteam ? Row.Steam.AccountName + "  ·  " : std::string( "нет в Steam  ·  " ) ) + "SteamID " +
			std::to_string( Row.Steam.SteamId );
		PutText( pDraw , pIds , ImVec2( X , AvatarMin.y + S( 33.f ) ) , Style::kMuted , Ellipsize( pIds , Ids , HeadMax.x - Pad - X ) );

		float PillX = X;
		const float PillY = AvatarMin.y + S( 56.f );

		if ( Active )
			PillX += DrawPill( pDraw , ImVec2( PillX , PillY ) , { "в сети" , Style::kLink } ) + S( 6.f );

		if ( Row.Hidden )
			PillX += DrawPill( pDraw , ImVec2( PillX , PillY ) , { "убран из списка" , Style::kMuted } ) + S( 6.f );

		for ( const auto& Pill : AccountStatus( Row ) )
		{
			if ( PillX + PillWidth( Pill ) > HeadMax.x - Pad )
				break;

			PillX += DrawPill( pDraw , ImVec2( PillX , PillY ) , Pill ) + S( 6.f );
		}

		// the buttons at the bottom of the head
		const bool CanSwitch = Row.HasSteam && Row.Steam.RememberPassword && !g_Switch.Running;
		const char* szWhy = !Row.HasSteam ? "Этого аккаунта нет в списке Steam (loginusers.vdf): войди в него один раз через Steam."
			: !Row.Steam.RememberPassword ? "Steam не помнит вход в этот аккаунт: войди в него один раз с галкой «Запомнить меня»."
			: g_Switch.Running ? "Переключение уже идёт." : nullptr;

		static EAction s_PendingAction = ACTION_SWITCH;

		// the switch closes Steam and the game with it: asked first when Deadlock is running
		const auto Request = [&]( EAction Action )
		{
			s_PendingAction = Action;

			if ( Steam::IsGameRunning() )
				ImGui::OpenPopup( "##deadlock" );
			else
				StartAction( Row , Action );
		};

		const float ButtonsY = HeadMax.y - Pad - S( 34.f );
		ImGui::SetCursorScreenPos( ImVec2( Origin.x + Pad , ButtonsY ) );

		if ( Active )
		{
			const std::string Label = "Играть";

			if ( PrimaryButton( "##play" , Label , true , true , ButtonWidth( Label , true ) , "Запустить Deadlock через Steam" ) )
				Steam::LaunchGame( g_State.Info );
		}
		else
		{
			const std::string Label = "Переключить и играть";

			if ( PrimaryButton( "##switchplay" , Label , true , CanSwitch , ButtonWidth( Label , true ) , szWhy ? szWhy : "Войти в этот аккаунт и запустить Deadlock" ) )
				Request( ACTION_SWITCH_GAME );

			ImGui::SameLine( 0.f , S( 10.f ) );

			if ( GhostButton( "##switch" , "Переключить" , CanSwitch , ButtonWidth( "Переключить" ) , szWhy ? szWhy : "Закрыть Steam и войти в этот аккаунт" ) )
				Request( ACTION_SWITCH );
		}

		ImGui::SameLine( 0.f , S( 10.f ) );

		if ( GhostButton( "##folder" , "Папка игры" , !g_State.CitadelDir.empty() , ButtonWidth( "Папка игры" ) , "Открыть game\\citadel (там cache_*.soc)" ) )
			ShellExecuteW( nullptr , L"open" , g_State.CitadelDir.c_str() , nullptr , nullptr , SW_SHOWNORMAL );

		// take off the list / bring back: a link on the right
		{
			const std::string Label = Row.Hidden ? "Вернуть в список" : "Убрать из списка";
			const float LinkW = TextSize( Font( FONT_MED , 18 ) , Label ).x;
			ImGui::SetCursorScreenPos( ImVec2( HeadMax.x - Pad - LinkW , ButtonsY ) );

			if ( LinkButton( "##hide" , Label , true , S( 34.f ) , Style::kMuted ,
							 Row.Hidden ? "Вернуть аккаунт в список панели" : "Только из этой панели: Steam и файлы игры не меняются" ) )
			{
				if ( Row.Hidden )
					SetHidden( Row.AccountId , false );
				else
					ImGui::OpenPopup( "##hidepopup" );
			}
		}

		if ( g_PreviewDialog == 2 && !ImGui::IsPopupOpen( "##deadlock" ) )
			ImGui::OpenPopup( "##deadlock" );

		if ( Question( "##hidepopup" , "Убрать «" + DisplayName( Row ) + "» из списка?" ,
					   "Аккаунт пропадёт только из этой панели: Steam, его вход и кеш Deadlock не меняются. Вернуть — кнопкой «Убранные» внизу окна." ,
					   "Убрать" ) == 2 )
			SetHidden( Row.AccountId , true );

		if ( Question( "##deadlock" , "Deadlock запущен" ,
					   "Переключение закроет Steam, а вместе с ним и игру. Если идёт матч, это засчитается как дисконнект или лив: бан поиска или Low Priority." ,
					   "Всё равно" ) == 2 )
			StartAction( Row , s_PendingAction );

		// the program after the login: the command, where it points, the buttons
		const ImVec2 LaunchMin( Origin.x , HeadMax.y + S( 12.f ) );
		const ImVec2 LaunchMax( Origin.x + Width , LaunchMin.y + S( 158.f ) );
		{
			const float CardPad = S( 18.f );
			const float TextW = Width - CardPad * 2.f;

			pDraw->AddRectFilled( LaunchMin , LaunchMax , Style::kCard , S( Style::kCardRounding ) );
			pDraw->AddRect( LaunchMin , LaunchMax , Style::kCardBorder , S( Style::kCardRounding ) , 0 , S( 1.f ) );
			PutText( pDraw , Font( FONT_SEMI , 18 ) , ImVec2( LaunchMin.x + CardPad , LaunchMin.y + S( 16.f ) ) , Style::kText , "Программа после входа" );
			pDraw->AddLine( ImVec2( LaunchMin.x + CardPad , LaunchMin.y + S( 52.f ) ) , ImVec2( LaunchMax.x - CardPad , LaunchMin.y + S( 52.f ) ) , Style::kLine , S( 1.f ) );

			const auto& Command = LaunchCommand();
			const bool HasCommand = !Command.Program.empty();
			ImFont* pLine = Font( FONT_MED , 16 );
			ImFont* pSub = Font( FONT_REG , 15 );
			ImU32 TargetColor = Style::kMuted;
			const std::string Target = CommandTarget( Command , TargetColor );

			PutText( pDraw , pLine , ImVec2( LaunchMin.x + CardPad , LaunchMin.y + S( 62.f ) ) , HasCommand ? Style::kText : Style::kMuted ,
					 Ellipsize( pLine , HasCommand ? g_LaunchLine : std::string( "Не задана" ) , TextW ) );
			PutText( pDraw , pSub , ImVec2( LaunchMin.x + CardPad , LaunchMin.y + S( 85.f ) ) , TargetColor , Ellipsize( pSub , Target , TextW ) );

			ImGui::SetCursorScreenPos( ImVec2( LaunchMin.x + CardPad , LaunchMax.y - CardPad - S( 34.f ) ) );

			// a path to a file that is not there would fail only after the switch
			const bool Missing = HasCommand && !Command.Exists && Command.Program.has_parent_path();
			const bool CanRun = HasCommand && !Missing && !g_Switch.Running && ( Active || CanSwitch );
			const char* szRunWhy = !HasCommand ? "Сначала выбери exe или впиши команду" : Missing ? "Файл программы не найден" :
				g_Switch.Running ? "Переключение уже идёт." : Active ? nullptr : szWhy;
			const std::string RunLabel = "Запустить";

			if ( PrimaryButton( "##run" , RunLabel , true , CanRun , ButtonWidth( RunLabel , true ) ,
								szRunWhy ? szRunWhy : Active ? "Аккаунт уже в Steam: закрыть Steam и запустить программу" :
								"Войти в этот аккаунт (Steam без окна), закрыть Steam и запустить программу" ) )
				Request( Active ? ACTION_PROGRAM : ACTION_SWITCH_PROGRAM );

			ImGui::SameLine( 0.f , S( 10.f ) );

			if ( GhostButton( "##pickexe" , "Выбрать exe" , hWnd != nullptr , ButtonWidth( "Выбрать exe" ) , "Программа из папки панели запишется как ./…" ) )
			{
				const std::wstring Path = PickExe( hWnd , ( Command.Exists ? Command.Directory : g_Paths.BaseDir ).wstring() );

				if ( !Path.empty() )
					SaveLaunch( Launch::ReplaceProgram( g_LaunchLine , Path , g_Paths.BaseDir ) );
			}

			ImGui::SameLine( 0.f , S( 10.f ) );

			if ( GhostButton( "##editcmd" , "Команда" , true , ButtonWidth( "Команда" ) , "Путь и аргументы вручную: ./ — от папки панели" ) )
				OpenCommandDialog();

			if ( g_PreviewDialog == 3 && !ImGui::IsPopupOpen( "##command" ) )
				OpenCommandDialog();

			RenderCommandDialog( hWnd );
		}

		// the cards: the bans and the statistics side by side (one under another in a narrow window), the data of the file
		std::vector<Section_t> Sections;

		if ( !D.Valid )
		{
			Sections.push_back( { "Deadlock" , { { "Данные" , "нет" , Style::kMuted ,
				g_State.CitadelDir.empty() ? "папка Deadlock не найдена" : "аккаунт не заходил в Deadlock на этом компьютере" } } } );
		}
		else
		{
			Section_t Bans = { "Блокировки" };
			Bans.Fields.push_back( { "Аккаунт" , D.Flags & SoCache::kFlagAccountBanned ? "заблокирован" : "не заблокирован" ,
									 D.Flags & SoCache::kFlagAccountBanned ? Style::kBad : Style::kLink } );
			Bans.Fields.push_back( BanField( "Поиск матчей" , D.MmBanUntil , Style::kBad ) );
			Bans.Fields.push_back( D.LowPriorityGames > 0
								   ? Field_t { "Low Priority" , std::to_string( D.LowPriorityGames ) + " побед" , Style::kWarn , "снимают только победы в очереди LP" }
								   : Field_t { "Low Priority" , "нет" , Style::kLink } );
			Bans.Fields.push_back( BanField( "Чат и голос" , D.CommsBanUntil , Style::kWarn ) );
			Bans.Fields.push_back( BanField( "Репорты" , D.ReportBanUntil , Style::kWarn ) );

			const uint32_t Games = D.Wins + D.Losses;
			char Rate[32] = "—";

			if ( Games )
				snprintf( Rate , sizeof( Rate ) , "%.1f%%" , 100.0 * D.Wins / Games );

			Section_t Stats = { "Статистика" };
			Stats.Fields.push_back( { "Победы / поражения" , std::to_string( D.Wins ) + " / " + std::to_string( D.Losses ) , Style::kText ,
									  std::string( "винрейт " ) + Rate + " · " + std::to_string( Games ) + " игр" } );
			Stats.Fields.push_back( { "Убийства" , std::to_string( D.Kills ) } );
			Stats.Fields.push_back( { "Любимый герой" , HeroName( D.MostPlayedHero ) , Style::kText , {} , HeroTexture( D.MostPlayedHero ) } );
			Stats.Fields.push_back( { "Street Brawl" , std::to_string( D.BrawlWins ) + " / " + std::to_string( D.BrawlLosses ) , Style::kText ,
									  std::to_string( D.BrawlKills ) + " убийств" } );
			Stats.Fields.push_back( { "Последний матч" , FormatDate( D.LastMatchTime ) , Style::kText ,
									  D.LastMatchTime ? FormatDuration( T - D.LastMatchTime ) + " назад" : std::string() } );

			Section_t Source = { "Данные" };
			Source.Fields.push_back( { "Кеш координатора" , FormatDate( D.FileTime ) , Style::kLabel ,
									   "обновляется при входе аккаунта в Deadlock · " + FormatDuration( T - D.FileTime ) + " назад" } );
			Source.Fields.push_back( { "Priority tokens" , std::to_string( D.PriorityTokens ) , Style::kLabel ,
									   "заработано " + std::to_string( D.PriorityTokensEarned ) + " · прогресс " + std::to_string( D.PriorityTokenProgress ) } );
			Source.Fields.push_back( { "Файл" , Steam::PathText( D.Path.filename() ) , Style::kLabel } );

			Sections = { Bans , Stats , Source };
		}

		const float Gap = S( 12.f );
		float Y = LaunchMax.y + Gap;

		if ( Sections.size() >= 2 && Width >= S( 600.f ) )
		{
			const float Half = ( Width - Gap ) * 0.5f;
			const float Height = std::max( SectionHeight( Sections[0] ) , SectionHeight( Sections[1] ) );

			DrawSection( pDraw , ImVec2( Origin.x , Y ) , Half , Sections[0] , Height );
			DrawSection( pDraw , ImVec2( Origin.x + Half + Gap , Y ) , Half , Sections[1] , Height );
			Y += Height + Gap;
			Sections.erase( Sections.begin() , Sections.begin() + 2 );
		}

		for ( const auto& Section : Sections )
		{
			const float Height = SectionHeight( Section );
			DrawSection( pDraw , ImVec2( Origin.x , Y ) , Width , Section , Height );
			Y += Height + Gap;
		}

		ImGui::SetCursorScreenPos( Origin );
		ImGui::Dummy( ImVec2( Width , Y - Origin.y ) );
		ImGui::EndChild();
	}

	auto RenderFooter( HWND hWnd , ImVec2 Size ) -> void
	{
		auto* pDraw = ImGui::GetWindowDrawList();
		const float Height = S( Style::kFooterH );
		const float Top = Size.y - Height;

		pDraw->AddLine( ImVec2( 0.f , Top ) , ImVec2( Size.x , Top ) , Style::kLine );

		std::string Status;
		bool Failed = false;

		{
			std::scoped_lock Lock( g_Switch.Lock );
			Status = g_Switch.Status;
			Failed = g_Switch.Failed;
		}

		ImU32 Color = Failed ? Style::kBad : g_Switch.Running ? Style::kLink : Style::kMuted;

		if ( Status.empty() )
		{
			if ( g_State.CitadelDir.empty() )
			{
				Status = "Папка Deadlock не найдена — укажи её кнопкой справа";
				Color = Style::kWarn;
			}
			else
				Status = "Deadlock: " + Steam::PathText( g_State.CitadelDir );
		}

		// the buttons on the right
		const float Pad = S( 20.f );
		const float ButtonY = Top + ( Height - S( 34.f ) ) * 0.5f;
		const std::string Pick = "Указать папку";
		const std::string Hidden = ( g_State.ShowHidden ? "Скрыть убранные (" : "Убранные (" ) + std::to_string( g_State.HiddenCount ) + ")";
		const float PickW = ButtonWidth( Pick );
		const float HiddenW = g_State.HiddenCount > 0 ? ButtonWidth( Hidden ) + S( 10.f ) : 0.f;
		float X = Size.x - Pad - PickW - HiddenW;

		ImFont* pFont = Font( FONT_REG , 15 );
		PutText( pDraw , pFont , ImVec2( Pad , Top + ( Height - pFont->FontSize ) * 0.5f - S( 1.f ) ) , Color , Ellipsize( pFont , Status , X - Pad * 2.f ) );

		if ( g_State.HiddenCount > 0 )
		{
			ImGui::SetCursorScreenPos( ImVec2( X , ButtonY ) );

			if ( GhostButton( "##showhidden" , Hidden , true , HiddenW - S( 10.f ) , g_State.ShowHidden ? "Не показывать убранные аккаунты" : "Показать убранные аккаунты" ) )
			{
				g_State.ShowHidden = !g_State.ShowHidden;
				g_State.NextRefresh = 0.0;
			}

			X += HiddenW;
		}

		ImGui::SetCursorScreenPos( ImVec2( X , ButtonY ) );

		if ( GhostButton( "##pick" , Pick , hWnd != nullptr , PickW , "Папка Deadlock (или game\\citadel), если она не нашлась сама" ) )
		{
			const std::wstring Dir = PickFolder( hWnd );

			if ( !Dir.empty() )
			{
				Accounts::WriteGameDir( g_Paths , Dir );
				g_State.NextRefresh = 0.0;
			}
		}
	}

	// the question of the thread of the switch: Steam does not close
	auto RenderKillQuestion() -> void
	{
		if ( g_Switch.Ask == 1 && !ImGui::IsPopupOpen( "##kill" ) )
			ImGui::OpenPopup( "##kill" );

		const int Answer = Question( "##kill" , "Steam не закрылся за 15 секунд" , "Завершить процесс steam.exe принудительно?" , "Завершить" );

		if ( Answer == 1 )
			g_Switch.Ask = 3;
		else if ( Answer == 2 )
			g_Switch.Ask = 2;
	}

	auto RenderFrame( HWND hWnd ) -> void
	{
		const ImVec2 Size = ImGui::GetIO().DisplaySize;

		ImGui::SetNextWindowPos( ImVec2( 0.f , 0.f ) );
		ImGui::SetNextWindowSize( Size );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , ImVec2( 0.f , 0.f ) );
		ImGui::Begin( "##main" , nullptr , ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
					  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse );
		ImGui::PopStyleVar();

		const float HeaderH = S( Style::kHeaderH );
		const float FooterH = S( Style::kFooterH );
		const float Pad = S( 20.f );
		const float ListW = std::clamp( Size.x * 0.34f , S( 320.f ) , S( 400.f ) );
		const float BodyY = HeaderH + Pad;
		const float BodyH = Size.y - BodyY - FooterH - S( 12.f );

		RenderHeader( hWnd , Size );
		RenderList( ImVec2( Pad , BodyY ) , ImVec2( ListW , BodyH ) );
		RenderDetails( hWnd , ImVec2( Pad + ListW + S( 10.f ) , BodyY ) , ImVec2( Size.x - Pad * 2.f - ListW - S( 10.f ) + S( 10.f ) , BodyH ) );
		RenderFooter( hWnd , Size );
		RenderKillQuestion();

		// the top of the window drags it where nothing is under the mouse
		g_OverItem = ImGui::IsAnyItemHovered() || ImGui::IsPopupOpen( "" , ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel );

		ImGui::End();
	}

	auto ApplyStyle() -> void
	{
		auto& St = ImGui::GetStyle();
		St.WindowRounding = 0.f;
		St.ChildRounding = 0.f;
		St.PopupRounding = S( Style::kDialogRounding );
		St.FrameRounding = S( Style::kButtonRounding );
		St.ScrollbarRounding = S( 3.f );
		St.ScrollbarSize = S( 6.f );
		St.WindowBorderSize = 0.f;
		St.PopupBorderSize = 1.f;
		St.WindowPadding = ImVec2( S( 10.f ) , S( 8.f ) );
		St.ItemSpacing = ImVec2( S( 8.f ) , S( 6.f ) );

		auto* Colors = St.Colors;
		Colors[ImGuiCol_WindowBg] = Style::Vec( Style::kBg );
		Colors[ImGuiCol_ChildBg] = ImVec4( 0.f , 0.f , 0.f , 0.f );
		Colors[ImGuiCol_PopupBg] = Style::Vec( Style::kCard );
		Colors[ImGuiCol_Border] = Style::Vec( Style::kDialogBorder );
		Colors[ImGuiCol_Text] = Style::Vec( Style::kText );
		Colors[ImGuiCol_TextDisabled] = Style::Vec( Style::kMuted );
		Colors[ImGuiCol_ScrollbarBg] = ImVec4( 0.f , 0.f , 0.f , 0.f );
		Colors[ImGuiCol_ScrollbarGrab] = Style::Vec( Style::kCardBorder );
		Colors[ImGuiCol_ScrollbarGrabHovered] = Style::Vec( Style::kCardBorderHover );
		Colors[ImGuiCol_ScrollbarGrabActive] = Style::Vec( Style::kLink );
		Colors[ImGuiCol_ModalWindowDimBg] = ImVec4( 0.f , 0.f , 0.f , 0.6f );
	}

	// The window drawn into a picture (raw RGBA, Width x Height): the caches of a folder with made up names, Steam is not read.
	// The rows: the first one is "logged in", the second one has no remembered login.
	auto Preview( const std::wstring& CitadelDir , const std::wstring& File , int Width , int Height ) -> int
	{
		const D3D_FEATURE_LEVEL Level = D3D_FEATURE_LEVEL_11_0;

		if ( FAILED( D3D11CreateDevice( nullptr , D3D_DRIVER_TYPE_HARDWARE , nullptr , 0 , &Level , 1 , D3D11_SDK_VERSION , &g_pDevice , nullptr , &g_pContext ) ) )
			return 1;

		D3D11_TEXTURE2D_DESC Desc = {};
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.SampleDesc.Count = 1;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET;

		ID3D11Texture2D* pTarget = nullptr;
		g_pDevice->CreateTexture2D( &Desc , nullptr , &pTarget );
		g_pDevice->CreateRenderTargetView( pTarget , nullptr , &g_pTarget );

		g_State.CitadelDir = CitadelDir;

		int Index = 0;

		for ( auto& Data : SoCache::Scan( CitadelDir ) )
		{
			Row_t Row;
			Row.AccountId = Data.AccountId;
			Row.HasSteam = true;
			Row.Steam.SteamId = Steam::kSteamId64Base + Data.AccountId;
			Row.Steam.AccountName = "login_" + std::to_string( ++Index );
			Row.Steam.PersonaName = "Аккаунт " + std::to_string( Index );
			Row.Steam.RememberPassword = Index != 2;
			Row.Data = std::move( Data );
			g_State.Rows.push_back( std::move( Row ) );
		}

		// no caches in the folder: made up accounts (the look of the window without the game)
		if ( g_State.Rows.empty() )
		{
			const int64_t T = Now();

			const auto Demo = [&]( const char* szNick , uint32_t Wins , uint32_t Losses , uint32_t MmBan , uint32_t Low , uint32_t Comms , bool Remember )
			{
				Row_t Row;
				Row.AccountId = 1000 + ++Index;
				Row.HasSteam = true;
				Row.Steam.SteamId = Steam::kSteamId64Base + Row.AccountId;
				Row.Steam.AccountName = "login_" + std::to_string( Index );
				Row.Steam.PersonaName = szNick;
				Row.Steam.RememberPassword = Remember;
				Row.Data.Valid = true;
				Row.Data.AccountId = Row.AccountId;
				Row.Data.FileTime = T - 3 * 3600;
				Row.Data.Wins = Wins;
				Row.Data.Losses = Losses;
				Row.Data.Kills = Wins * 9 + Losses * 6;
				Row.Data.MostPlayedHero = g_Heroes.Names.empty() ? 0 : g_Heroes.Names.begin()->first;
				Row.Data.MmBanUntil = MmBan ? static_cast<uint32_t>( T + MmBan ) : 0;
				Row.Data.LowPriorityGames = Low;
				Row.Data.CommsBanUntil = Comms ? static_cast<uint32_t>( T + Comms ) : 0;
				Row.Data.LastMatchTime = static_cast<uint32_t>( T - 5 * 3600 );
				Row.Data.BrawlWins = 3;
				Row.Data.BrawlLosses = 2;
				Row.Data.BrawlKills = 41;
				Row.Data.PriorityTokens = 2;
				Row.Data.PriorityTokensEarned = 5;
				g_State.Rows.push_back( std::move( Row ) );
			};

			Demo( "connector" , 124 , 101 , 0 , 0 , 0 , true );
			Demo( "Haze main" , 61 , 70 , 0 , 0 , 3 * 86400 , true );
			Demo( "smurf_eu" , 12 , 9 , 2 * 86400 + 4 * 3600 , 0 , 0 , false );
			Demo( "low prio" , 40 , 52 , 0 , 3 , 0 , true );
		}

		// one without the data of the game
		Row_t Empty;
		Empty.AccountId = 12345;
		Empty.HasSteam = true;
		Empty.Steam.SteamId = Steam::kSteamId64Base + 12345;
		Empty.Steam.AccountName = "new_account";
		Empty.Steam.PersonaName = "Новый";
		Empty.Hidden = true;
		g_State.Rows.push_back( Empty );
		g_State.HiddenCount = 1;
		g_State.ShowHidden = true;

		if ( g_LaunchLine.empty() )
			g_LaunchLine = "./loader.exe --silent";

		if ( !g_State.Rows.empty() )
		{
			g_State.Info.ActiveUser = g_State.Rows.front().AccountId;
			g_State.Info.SteamPath = L"preview";
			g_State.SteamRunning = true;
			g_State.Selected = g_State.Rows.size() > 1 ? g_State.Rows[1].AccountId : g_State.Rows.front().AccountId;
		}

		ImGui::CreateContext();
		auto& Io = ImGui::GetIO();
		Io.IniFilename = nullptr;
		Io.DisplaySize = ImVec2( static_cast<float>( Width ) , static_cast<float>( Height ) );
		Io.DeltaTime = 1.f / 60.f;

		ApplyStyle();
		LoadFonts( g_State.DataDir );
		ImGui_ImplDX11_Init( g_pDevice , g_pContext );

		for ( int Frame = 0; Frame < 6; Frame++ )
		{
			ImGui_ImplDX11_NewFrame();
			ImGui::NewFrame();
			RenderFrame( nullptr );
			ImGui::Render();

			const ImVec4 Bg = Style::Vec( Style::kBg );
			const float Clear[4] = { Bg.x , Bg.y , Bg.z , 1.f };
			g_pContext->OMSetRenderTargets( 1 , &g_pTarget , nullptr );
			g_pContext->ClearRenderTargetView( g_pTarget , Clear );
			ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
		}

		Desc.BindFlags = 0;
		Desc.Usage = D3D11_USAGE_STAGING;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		ID3D11Texture2D* pStaging = nullptr;
		g_pDevice->CreateTexture2D( &Desc , nullptr , &pStaging );
		g_pContext->CopyResource( pStaging , pTarget );

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		int Result = 1;

		if ( SUCCEEDED( g_pContext->Map( pStaging , 0 , D3D11_MAP_READ , 0 , &Mapped ) ) )
		{
			std::ofstream Out( File , std::ios::binary | std::ios::trunc );

			for ( int Y = 0; Y < Height; Y++ )
				Out.write( static_cast<const char*>( Mapped.pData ) + Y * Mapped.RowPitch , Width * 4 );

			g_pContext->Unmap( pStaging , 0 );
			Result = Out ? 0 : 1;
		}

		ImGui_ImplDX11_Shutdown();
		ImGui::DestroyContext();
		return Result;
	}

}

int WINAPI wWinMain( HINSTANCE hInstance , HINSTANCE , PWSTR , int )
{
	int Argc = 0;
	wchar_t** Argv = CommandLineToArgvW( GetCommandLineW() , &Argc );

	g_Paths = Accounts::DefaultPaths();
	g_State.DataDir = Accounts::FindDataDir();
	LoadHeroes( g_State.DataDir );
	LoadLaunch();

	if ( Argc >= 4 && wcscmp( Argv[1] , L"--preview" ) == 0 )
	{
		g_PreviewDialog = Argc >= 7 ? _wtoi( Argv[6] ) : 0;
		return Preview( Argv[2] , Argv[3] , Argc >= 6 ? _wtoi( Argv[4] ) : 1040 , Argc >= 6 ? _wtoi( Argv[5] ) : 680 );
	}

	// the command line: the output into the console of the parent (cmd, PowerShell) unless it is redirected; the window is not opened.
	// (A program of the window does not hold the prompt of cmd: account-panel.exe is the one for the console.)
	if ( Argc >= 2 && Argv[1][0] == L'-' )
	{
		const auto Redirected = []( DWORD Handle ) { return GetFileType( GetStdHandle( Handle ) ) != FILE_TYPE_UNKNOWN; };
		const bool OutRedirected = Redirected( STD_OUTPUT_HANDLE ) , ErrRedirected = Redirected( STD_ERROR_HANDLE );

		if ( AttachConsole( ATTACH_PARENT_PROCESS ) )
		{
			FILE* pFile = nullptr;

			if ( !OutRedirected )
				freopen_s( &pFile , "CONOUT$" , "w" , stdout );

			if ( !ErrRedirected )
				freopen_s( &pFile , "CONOUT$" , "w" , stderr );

			freopen_s( &pFile , "CONIN$" , "r" , stdin );
		}

		std::vector<std::string> Args;

		for ( int i = 1; i < Argc; i++ )
			Args.push_back( Steam::Utf8( Argv[i] ) );

		const int Code = Cli::Run( Args );
		LocalFree( Argv );
		return Code;
	}

	CoInitializeEx( nullptr , COINIT_APARTMENTTHREADED );
	ImGui_ImplWin32_EnableDpiAwareness();

	WNDCLASSEXW Class = { sizeof( Class ) , CS_CLASSDC , WndProc , 0 , 0 , hInstance , LoadIconW( nullptr , IDI_APPLICATION ) , LoadCursorW( nullptr , IDC_ARROW ) ,
						  nullptr , nullptr , L"AccountPanel" , nullptr };
	RegisterClassExW( &Class );

	// the scale of the screen: the window, the fonts and every size
	{
		HDC hScreen = GetDC( nullptr );
		g_Scale = std::clamp( static_cast<float>( GetDeviceCaps( hScreen , LOGPIXELSX ) ) / 96.f , 1.f , 3.f );
		ReleaseDC( nullptr , hScreen );
	}

	// the window without the frame of Windows (WM_NCCALCSIZE): the caption keeps the snap and the animation of the minimize
	const int Width = static_cast<int>( S( 1040.f ) ) , Height = static_cast<int>( S( 680.f ) );
	HWND hWnd = CreateWindowW( Class.lpszClassName , L"Account Panel" , WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX , ( GetSystemMetrics( SM_CXSCREEN ) - Width ) / 2 ,
							   ( GetSystemMetrics( SM_CYSCREEN ) - Height ) / 2 , Width , Height , nullptr , nullptr , hInstance , nullptr );

	// the dark frame, the rounded corners of Windows 11 (DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND), the border in the color of the cards,
	// the shadow (the frame extended into the window by a pixel)
	const BOOL Dark = TRUE;
	DwmSetWindowAttribute( hWnd , 20 , &Dark , sizeof( Dark ) );

	const int Corners = 2;
	DwmSetWindowAttribute( hWnd , 33 , &Corners , sizeof( Corners ) );

	const COLORREF Border = RGB( 0x2A , 0x32 , 0x47 );
	DwmSetWindowAttribute( hWnd , 34 , &Border , sizeof( Border ) );

	const MARGINS Margins = { 0 , 0 , 0 , 1 };
	DwmExtendFrameIntoClientArea( hWnd , &Margins );
	SetWindowPos( hWnd , nullptr , 0 , 0 , 0 , 0 , SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );

	if ( !CreateDevice( hWnd ) )
	{
		ReleaseDevice();
		MessageBoxW( nullptr , L"Не удалось создать устройство D3D11" , L"Account Panel" , MB_ICONERROR );
		return 1;
	}

	ShowWindow( hWnd , SW_SHOWDEFAULT );
	UpdateWindow( hWnd );

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;

	ApplyStyle();
	LoadFonts( g_State.DataDir );

	ImGui_ImplWin32_Init( hWnd );
	ImGui_ImplDX11_Init( g_pDevice , g_pContext );

	bool Done = false;

	while ( !Done )
	{
		MSG Msg;

		while ( PeekMessageW( &Msg , nullptr , 0 , 0 , PM_REMOVE ) )
		{
			TranslateMessage( &Msg );
			DispatchMessageW( &Msg );

			if ( Msg.message == WM_QUIT )
				Done = true;
		}

		if ( Done )
			break;

		if ( IsIconic( hWnd ) )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
			continue;
		}

		if ( g_ResizeWidth && g_ResizeHeight )
		{
			ReleaseTarget();
			g_pSwapChain->ResizeBuffers( 0 , g_ResizeWidth , g_ResizeHeight , DXGI_FORMAT_UNKNOWN , 0 );
			g_ResizeWidth = g_ResizeHeight = 0;
			CreateTarget();
		}

		// the files change by themselves (Steam, the game): read again every 2 s
		const double Time = ImGui::GetTime();

		if ( Time >= g_State.NextRefresh )
		{
			Refresh();
			g_State.NextRefresh = Time + 2.0;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		RenderFrame( hWnd );

		ImGui::Render();

		const ImVec4 Bg = Style::Vec( Style::kBg );
		const float Clear[4] = { Bg.x , Bg.y , Bg.z , 1.f };
		g_pContext->OMSetRenderTargets( 1 , &g_pTarget , nullptr );
		g_pContext->ClearRenderTargetView( g_pTarget , Clear );
		ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );

		// a frame of the screen when the window is in front, fewer when not
		g_pSwapChain->Present( 1 , 0 );

		if ( GetForegroundWindow() != hWnd )
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	ReleaseDevice();
	DestroyWindow( hWnd );
	UnregisterClassW( Class.lpszClassName , hInstance );
	CoUninitialize();

	return 0;
}
