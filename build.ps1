# Builds steam_loader on Windows (MSVC): the accounts of Steam, the bans of Deadlock on them (game\citadel\cache_<id>.soc), the
# switch of Steam with the login in the background.
#   AccountPanelCore.lib  the library without the window (core\*.hpp): the accounts, the switch, the command line
#   account-panel.exe     the command line (account-panel --help)
#   AccountPanel.exe      the window (ImGui + D3D11); it takes the same arguments
# The library and the command line are cross-platform; Linux: build.sh or CMakeLists.txt.
# ImGui, stb and freetype are vendored in third_party\ (the window needs them). Usage: powershell -File build.ps1 [-Out <dir>] [-NoGui]
param([string]$Out = "$PSScriptRoot\out", [switch]$NoGui)

$vc = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vc)) {
    Write-Error "vcvars64.bat not found: $vc. Fix the path to your Visual Studio."
    exit 1
}

$inc = "$PSScriptRoot\third_party\include"
$freetype = "$PSScriptRoot\third_party\freetype.lib"

New-Item -ItemType Directory -Force $Out | Out-Null
New-Item -ItemType Directory -Force "$Out\core" | Out-Null

$flags = "/nologo /MP /std:c++20 /EHsc /O2 /MT /utf-8 /W4 /DNDEBUG /DUNICODE /D_UNICODE"
$core = @("Platform", "Steam", "SoCache", "Launch", "Accounts", "Cli") | ForEach-Object { "`"$PSScriptRoot\core\$_.cpp`"" }
$coreLibs = "advapi32.lib shell32.lib ole32.lib user32.lib"

$steps = @(
    # the library
    "cd /d `"$Out\core`" && cl /c $flags $($core -join ' ') && lib /nologo /OUT:`"$Out\AccountPanelCore.lib`" *.obj",
    # the command line
    "cd /d `"$Out`" && cl $flags `"$PSScriptRoot\cli\main.cpp`" /Fo:cli_main.obj AccountPanelCore.lib $coreLibs /Fe:account-panel.exe /link /SUBSYSTEM:CONSOLE"
)

if (-not $NoGui) {
    $gui = @(
        "$PSScriptRoot\main.cpp",
        "$inc\ImGui\imgui.cpp",
        "$inc\ImGui\imgui_draw.cpp",
        "$inc\ImGui\imgui_tables.cpp",
        "$inc\ImGui\imgui_widgets.cpp",
        "$inc\ImGui\imgui_impl_win32.cpp",
        "$inc\ImGui\imgui_impl_dx11.cpp",
        "$inc\ImGui\Misc\freetype\imgui_freetype.cpp"
    ) | ForEach-Object { "`"$_`"" }

    # ImGui is built with the warnings of its own level
    $guiFlags = $flags -replace '/W4', '/W3'
    $libs = "AccountPanelCore.lib `"$freetype`" d3d11.lib d3dcompiler.lib dxgi.lib dwmapi.lib gdi32.lib uuid.lib $coreLibs"
    $steps += "cd /d `"$Out`" && cl $guiFlags /I`"$PSScriptRoot`" /I`"$inc`" $($gui -join ' ') $libs /Fe:AccountPanel.exe /link /SUBSYSTEM:WINDOWS"
}

foreach ($step in $steps) {
    cmd /c "`"$vc`" >nul 2>&1 && $step"

    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "Done: $Out\account-panel.exe$(if (-not $NoGui) { ', ' + $Out + '\AccountPanel.exe' })"
