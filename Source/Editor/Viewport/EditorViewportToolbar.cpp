#include "pch.h"

#include "Editor/Viewport/EditorViewportToolbar.h"

#include <imgui.h>

namespace sw
{
	void EditorViewportToolbar::draw( ViewportToolbarSettings& settings, float32 viewportWidth )
	{
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 3.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 4.0f, 2.0f } );
		ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.18f, 0.18f, 0.20f, 0.85f } );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.28f, 0.28f, 0.32f, 1.0f } );

		{
			ImGui::SetNextItemWidth( 85.0f );
			const utf8* arrModeLabels[] = { "Lit", "Unlit", "Wireframe" };
			int32		modeIndex		= static_cast<int32>( settings._renderMode );
			if ( ImGui::Combo( "##ViewMode", &modeIndex, arrModeLabels, 3 ) )
				settings._renderMode = static_cast<ViewportRenderMode>( modeIndex );
		}

		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();

		{
			ImGui::TextDisabled( "Cam:" );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 70.0f );
			ImGui::SliderFloat( "##CamSpeed", &settings._cameraSpeed, 0.5f, 20.0f, "%.1f" );
		}

		if ( viewportWidth > 280.0f )
		{
			ImGui::SameLine();
			ImGui::TextDisabled( "|" );
			ImGui::SameLine();
			ImGui::Checkbox( "Stats", &settings._bShowStats );
		}

		ImGui::PopStyleColor( 2 );
		ImGui::PopStyleVar( 2 );
	}
} // namespace sw
