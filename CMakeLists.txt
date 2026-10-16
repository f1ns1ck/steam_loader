# Account Panel without the window: the library AccountPanelCore and the command line account-panel (Linux and Windows).
# The window (ImGui + D3D11) is Windows only: build.ps1.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   cmake --install build --prefix ~/.local        # bin/account-panel, lib/, include/account_panel/
#
# -DBUILD_SHARED_LIBS=ON - libAccountPanelCore.so (AccountPanelCore.dll) in place of the static one.

cmake_minimum_required( VERSION 3.16 )
project( AccountPanel VERSION 1.1 LANGUAGES CXX )

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

if ( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
	set( CMAKE_BUILD_TYPE Release )
endif()

set( CORE_SOURCES
	core/Platform.cpp
	core/Steam.cpp
	core/SoCache.cpp
	core/Launch.cpp
	core/Accounts.cpp
	core/Cli.cpp
)

set( CORE_HEADERS
	core/Platform.hpp
	core/Steam.hpp
	core/SoCache.hpp
	core/Launch.hpp
	core/Accounts.hpp
	core/Cli.hpp
)

add_library( AccountPanelCore ${CORE_SOURCES} )
target_include_directories( AccountPanelCore PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core> $<INSTALL_INTERFACE:include/account_panel> )
set_target_properties( AccountPanelCore PROPERTIES POSITION_INDEPENDENT_CODE ON WINDOWS_EXPORT_ALL_SYMBOLS ON PUBLIC_HEADER "${CORE_HEADERS}" )

if ( MSVC )
	target_compile_options( AccountPanelCore PRIVATE /W4 /utf-8 )
	target_compile_definitions( AccountPanelCore PUBLIC UNICODE _UNICODE )
	target_link_libraries( AccountPanelCore PUBLIC advapi32 shell32 ole32 user32 )
else()
	target_compile_options( AccountPanelCore PRIVATE -Wall -Wextra )

	# std::filesystem of GCC 8
	if ( CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9 )
		target_link_libraries( AccountPanelCore PUBLIC stdc++fs )
	endif()
endif()

add_executable( account-panel cli/main.cpp )
target_link_libraries( account-panel PRIVATE AccountPanelCore )

if ( MSVC )
	target_compile_options( account-panel PRIVATE /utf-8 )
endif()

include( GNUInstallDirs )
install( TARGETS AccountPanelCore account-panel
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/account_panel )
