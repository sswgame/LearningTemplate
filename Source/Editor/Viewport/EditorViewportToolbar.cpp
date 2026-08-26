#include "pch.h"

#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		void drawSnapToggleCombo( const utf8* pButtonLabel, const utf8* pComboId, bool& bEnabled, float32& value,
								  const float32* arrValues, const utf8* const* arrLabels, int32 valueCount,
								  float32 comboWidth, int32 fallbackIndex )
		{
			const bool bActive = bEnabled;
			if ( bActive )
				ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.25f, 0.45f, 0.75f, 1.0f } );

			if ( ImGui::Button( pButtonLabel ) )
				bEnabled = ( bEnabled == false );

			if ( bActive )
				ImGui::PopStyleColor();

			ImGui::SameLine();
			ImGui::SetNextItemWidth( comboWidth );
			int32 currentIndex = fallbackIndex;
			for ( int32 valueIndex = 0; valueIndex < valueCount; ++valueIndex )
			{
				if ( MathUtil::abs( value - arrValues[valueIndex] ) < 1e-4f )
					currentIndex = valueIndex;
			}
			if ( ImGui::Combo( pComboId, &currentIndex, arrLabels, valueCount ) )
				value = arrValues[currentIndex];
		}
	} // namespace

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

		editor::drawToolbarSeparator();

		{
			ImGui::TextDisabled( "Cam:" );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 70.0f );
			ImGui::SliderFloat( "##CamSpeed", &settings._cameraSpeed, 0.5f, 20.0f, "%.1f" );
		}

		if ( viewportWidth > 280.0f )
		{
			editor::drawToolbarSeparator();
			ImGui::Checkbox( "Stats", &settings._bShowStats );
		}

		ImGui::PopStyleColor( 2 );
		ImGui::PopStyleVar( 2 );
	}

	void EditorViewportToolbar::drawTransformBar( ViewportToolbarSettings& settings, const float2& anchorPos,
												  bool bEnabled )
	{
		editor::EditorFloatingBarDesc barDesc{};
		barDesc._pId	   = "##EditorTransformBar";
		barDesc._anchorPos = anchorPos;
		barDesc._pivot	   = float2{ 0.5f, 0.0f };
		barDesc._bEnabled  = bEnabled;

		if ( editor::beginFloatingBar( barDesc ) == false )
		{
			editor::endFloatingBar();
			return;
		}

		int32& op = EditorWorkspace::gizmoOperation();
		ImGui::RadioButton( "Translate", &op, 0 );
		ImGui::SameLine();
		ImGui::RadioButton( "Rotate", &op, 1 );
		ImGui::SameLine();
		ImGui::RadioButton( "Scale", &op, 2 );
		ImGui::SameLine();
		bool& bLocal = EditorWorkspace::gizmoLocalSpace();
		ImGui::Checkbox( "Local", &bLocal );

		editor::drawToolbarSeparator();

		const float32 arrSnapValues[] = { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
		const utf8*	  arrSnapLabels[] = { "0.1", "0.5", "1.0", "5.0", "10.0" };
		drawSnapToggleCombo( "Grid Snap", "##GridSnapVal", settings._bGridSnap, settings._gridSnapValue, arrSnapValues,
							 arrSnapLabels, 5, 65.0f, 2 );

		editor::drawToolbarSeparator();

		const float32 arrRotValues[] = { 5.0f, 15.0f, 45.0f, 90.0f };
		const utf8*	  arrRotLabels[] = { "5 deg", "15 deg", "45 deg", "90 deg" };
		drawSnapToggleCombo( "Rot Snap", "##RotSnapVal", settings._bRotationSnap, settings._rotationSnapValue,
							 arrRotValues, arrRotLabels, 4, 60.0f, 1 );

		editor::endFloatingBar();
	}
} // namespace sw::editor
