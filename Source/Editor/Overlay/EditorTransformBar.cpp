#include "pch.h"

#include "Editor/Overlay/EditorTransformBar.h"

#include "Editor/Viewport/EditorViewportToolbar.h"
#include "Editor/Workspace/EditorWorkspace.h"
#include "Editor/Workspace/SelectionManager.h"

#include "Core/Math/MathUtil.h"

#include <imgui.h>

namespace sw
{
	void drawEditorTransformBar( ViewportToolbarSettings& settings, const float2& anchorPos )
	{
		const bool bHasSelection = SelectionManager::getSelectedObjectCount() > 0;

		ImGui::SetNextWindowPos( ImVec2{ anchorPos._x, anchorPos._y }, ImGuiCond_Always, ImVec2{ 0.5f, 0.0f } );

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
								 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
								 ImGuiWindowFlags_AlwaysAutoResize;
		if ( bHasSelection == false )
			flags |= ImGuiWindowFlags_NoInputs;

		if ( ImGui::Begin( "##EditorTransformBar", nullptr, flags ) == false )
		{
			ImGui::End();
			return;
		}

		if ( bHasSelection == false )
			ImGui::BeginDisabled();

		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 3.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 4.0f, 2.0f } );

		int32& op = EditorWorkspace::gizmoOperation();
		ImGui::RadioButton( "Translate", &op, 0 );
		ImGui::SameLine();
		ImGui::RadioButton( "Rotate", &op, 1 );
		ImGui::SameLine();
		ImGui::RadioButton( "Scale", &op, 2 );
		ImGui::SameLine();
		bool& bLocal = EditorWorkspace::gizmoLocalSpace();
		ImGui::Checkbox( "Local", &bLocal );

		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();

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
			const float32 arrSnapValues[]  = { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
			const utf8*	  arrSnapLabels[]  = { "0.1", "0.5", "1.0", "5.0", "10.0" };
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
			const float32 arrRotValues[]  = { 5.0f, 15.0f, 45.0f, 90.0f };
			const utf8*	  arrRotLabels[]  = { "5 deg", "15 deg", "45 deg", "90 deg" };
			int32		  currentRotIndex = 1;
			for ( int32 rotIndex = 0; rotIndex < 4; ++rotIndex )
			{
				if ( MathUtil::abs( settings._rotationSnapValue - arrRotValues[rotIndex] ) < 1e-4f )
					currentRotIndex = rotIndex;
			}
			if ( ImGui::Combo( "##RotSnapVal", &currentRotIndex, arrRotLabels, 4 ) )
				settings._rotationSnapValue = arrRotValues[currentRotIndex];
		}

		ImGui::PopStyleVar( 2 );

		if ( bHasSelection == false )
			ImGui::EndDisabled();

		ImGui::End();
	}
} // namespace sw
