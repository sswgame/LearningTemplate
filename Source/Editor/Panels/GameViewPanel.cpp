/**
 * @file GameViewPanel.cpp
 */
#include "Panels/GameViewPanel.h"
#include "Runtime/EditorUIContext.h"
#include <imgui.h>
#include <cmath>

namespace sw
{
	void GameViewPanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		const ImVec2 size = ImGui::GetContentRegionAvail();
		if ( size.x > 1.0f && size.y > 1.0f &&
			 ctx.requestGameViewportWidth != nullptr &&
			 ctx.requestGameViewportHeight != nullptr )
		{
			const uint32 wantW = static_cast<uint32>( std::lround( size.x ) );
			const uint32 wantH = static_cast<uint32>( std::lround( size.y ) );
			const int32	 dW	   = static_cast<int32>( wantW ) - static_cast<int32>( ctx.gameViewportWidth );
			const int32	 dH	   = static_cast<int32>( wantH ) - static_cast<int32>( ctx.gameViewportHeight );
			// Ignore 1px layout jitter; request recreate when content region meaningfully differs.
			if ( ( dW > 1 || dW < -1 || dH > 1 || dH < -1 ) && wantW > 0 && wantH > 0 )
			{
				*ctx.requestGameViewportWidth  = wantW;
				*ctx.requestGameViewportHeight = wantH;
			}
		}

		if ( ctx.gameTextureID != nullptr )
		{
			if ( size.x > 0.0f && size.y > 0.0f )
				ImGui::Image( reinterpret_cast<ImTextureID>( ctx.gameTextureID ), size );
		}
		else
		{
			ImGui::TextUnformatted( "Game RenderTarget is not available." );
		}

		ImGui::End();
	}
} // namespace sw
