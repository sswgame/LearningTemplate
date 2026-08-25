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

		// 1) Grid Snap
		{
			const bool bActive = settings._bGridSnap;
			if ( bActive )
				ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.25f, 0.45f, 0.75f, 1.0f } );

			if ( ImGui::Button( "Grid Snap" ) )
				settings._bGridSnap = !settings._bGridSnap;

			if ( bActive )
				ImGui::PopStyleColor();

			ImGui::SameLine();
			ImGui::SetNextItemWidth( 65.0f );
			const float32 arrSnapValues[] = { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
			const char*	  arrSnapLabels[] = { "0.1", "0.5", "1.0", "5.0", "10.0" };
			int32		  currentSnapIndex = 2;
			for ( int32 snapIndex = 0; snapIndex < 5; ++snapIndex )
			{
				if ( MathUtil::abs( settings._gridSnapValue - arrSnapValues[snapIndex] ) < 1e-4f )
					currentSnapIndex = snapIndex;
			}
			if ( ImGui::Combo( "##GridSnapVal", &currentSnapIndex, arrSnapLabels, 5 ) )
				settings._gridSnapValue = arrSnapValues[currentSnapIndex];
		}

		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();

		// 2) Rotation Snap
		{
			const bool bActive = settings._bRotationSnap;
			if ( bActive )
				ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.25f, 0.45f, 0.75f, 1.0f } );

			if ( ImGui::Button( "Rot Snap" ) )
				settings._bRotationSnap = !settings._bRotationSnap;

			if ( bActive )
				ImGui::PopStyleColor();

			ImGui::SameLine();
			ImGui::SetNextItemWidth( 60.0f );
			const float32 arrRotValues[] = { 5.0f, 15.0f, 45.0f, 90.0f };
			const char*	  arrRotLabels[] = { "5 deg", "15 deg", "45 deg", "90 deg" };
			int32		  currentRotIndex = 1;
			for ( int32 rotIndex = 0; rotIndex < 4; ++rotIndex )
			{
				if ( MathUtil::abs( settings._rotationSnapValue - arrRotValues[rotIndex] ) < 1e-4f )
					currentRotIndex = rotIndex;
			}
			if ( ImGui::Combo( "##RotSnapVal", &currentRotIndex, arrRotLabels, 4 ) )
				settings._rotationSnapValue = arrRotValues[currentRotIndex];
		}

		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();

		// 3) View Mode
		{
			ImGui::SetNextItemWidth( 85.0f );
			const char* arrModeLabels[] = { "Lit", "Unlit", "Wireframe" };
			int32		modeIndex		= static_cast<int32>( settings._renderMode );
			if ( ImGui::Combo( "##ViewMode", &modeIndex, arrModeLabels, 3 ) )
				settings._renderMode = static_cast<ViewportRenderMode>( modeIndex );
		}

		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();

		// 4) Camera Speed
		{
			ImGui::TextDisabled( "Cam:" );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 70.0f );
			ImGui::SliderFloat( "##CamSpeed", &settings._cameraSpeed, 0.5f, 20.0f, "%.1f" );
		}

		// 5) Stats Overlay Toggle
		if ( viewportWidth > 600.0f )
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
