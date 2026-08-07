/**
 * @file GameViewPanel.cpp
 */
#include "Panels/GameViewPanel.h"
#include "Runtime/EditorUIContext.h"
#include <imgui.h>

namespace sw
{
	void GameViewPanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
		{
			ImGui::End();
			return;
		}

		if ( ctx.gameTextureID != nullptr )
		{
			const ImVec2 size = ImGui::GetContentRegionAvail();
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
