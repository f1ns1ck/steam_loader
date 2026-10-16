#pragma once

// The palette and the sizes of the panel: the graphite blue window, lighter cards, the blue accent (2026-10-16: the black of the
// screenshots was too heavy, the surfaces were lifted). The font is Nunito Sans (x64\Release\fonts\NunitoSans-*.ttf, OFL).

#include <ImGui/imgui.h>

#include <cstdint>

namespace Style
{
	constexpr auto Hex( uint32_t Rgb , float Alpha = 1.f ) -> ImU32
	{
		return IM_COL32( ( Rgb >> 16 ) & 0xFF , ( Rgb >> 8 ) & 0xFF , Rgb & 0xFF , static_cast<int>( Alpha * 255.f + 0.5f ) );
	}

	inline auto Vec( ImU32 Color ) -> ImVec4
	{
		return ImGui::ColorConvertU32ToFloat4( Color );
	}

	constexpr auto Alpha( ImU32 Color , float Value ) -> ImU32
	{
		return ( Color & 0x00FFFFFF ) | ( static_cast<ImU32>( Value * 255.f + 0.5f ) << 24 );
	}

	// the surfaces
	constexpr ImU32 kBg = Hex( 0x151A26 );			// the window and the side bar
	constexpr ImU32 kCard = Hex( 0x1C2232 );		// the cards, the rows of the list and the dialogs
	constexpr ImU32 kCardHover = Hex( 0x222939 );
	constexpr ImU32 kCardBorder = Hex( 0x2A3247 );
	constexpr ImU32 kCardBorderHover = Hex( 0x38425C );
	constexpr ImU32 kCardSelected = Hex( 0x212B45 );
	constexpr ImU32 kCardBorderSelected = Hex( 0x4A6FD8 );
	constexpr ImU32 kDialogBorder = Hex( 0x2E364C );
	constexpr ImU32 kLine = Hex( 0x262D40 );		// the lines inside a dialog and under the header
	constexpr ImU32 kGhost = Hex( 0x2A3247 );		// the frame of an icon button

	// the text
	constexpr ImU32 kText = Hex( 0xF2F2F3 );		// the titles
	constexpr ImU32 kLabel = Hex( 0xD5D7DB );		// the names of the values in a dialog
	constexpr ImU32 kSoft = Hex( 0xA3B3C2 );		// the second lines, the side bar
	constexpr ImU32 kMuted = Hex( 0x7F8C9A );		// the dimmed second lines
	constexpr ImU32 kDim = Hex( 0x858892 );			// the dimmed titles

	// the accent and the states
	constexpr ImU32 kAccent = Hex( 0x4F7DFF );		// the main button
	constexpr ImU32 kAccentHover = Hex( 0x6A91FF );
	constexpr ImU32 kAccentText = Hex( 0xE6EDFF );	// the text on the accent
	constexpr ImU32 kLink = Hex( 0x5B86F7 );		// "fine", "logged in": the screenshots have no green
	constexpr ImU32 kBad = Hex( 0xF0616D );
	constexpr ImU32 kWarn = Hex( 0xF2B45A );

	// the sizes (multiplied by the scale of the screen)
	constexpr float kHeaderH = 60.f;
	constexpr float kFooterH = 58.f;
	constexpr float kRowH = 66.f;
	constexpr float kCardRounding = 8.f;
	constexpr float kDialogRounding = 10.f;
	constexpr float kButtonRounding = 5.f;
}
