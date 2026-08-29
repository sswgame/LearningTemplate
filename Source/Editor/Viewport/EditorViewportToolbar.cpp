#include "pch.h"

#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		struct EditorViewportToolbarInternal
		{
			static void drawSnapToggleCombo( const utf8* pButtonLabel, const utf8* pComboId, bool& bEnabled, float32& value,
											 const float32* arrValues, const utf8* const* arrLabels, int32 valueCount,
											 float32 comboWidth, int32 fallbackIndex )
			{
				if ( EditorWidgets::drawToggleButton( pButtonLabel, bEnabled ) )
					bEnabled = ( bEnabled == false );

				ImGui::SameLine();
				ImGui::SetNextItemWidth( comboWidth );

				int32 currentIdx = fallbackIndex;
				for ( int32 idx = 0; idx < valueCount; ++idx )
				{
					if ( MathUtil::abs( value - arrValues[idx] ) < 0.001f )
					{
						currentIdx = idx;
						break;
					}
				}

				if ( ImGui::Combo( pComboId, &currentIdx, arrLabels, valueCount ) )
				{
					if ( 0 <= currentIdx && currentIdx < valueCount )
						value = arrValues[currentIdx];
				}
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	void EditorViewportToolbar::draw( ViewportToolbarSettings& settings, float32 viewportWidth )
	{
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 6.0f, 2.0f } );
		ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.18f, 0.18f, 0.22f, 0.85f } );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.28f, 0.28f, 0.32f, 1.0f } );

		{
			ImGui::SetNextItemWidth( 85.0f );
			const utf8* arrModeLabels[] = { "Lit", "Unlit", "Wireframe" };
			int32		modeIndex		= static_cast<int32>( settings._renderMode );
			if ( ImGui::Combo( "##ViewMode", &modeIndex, arrModeLabels, 3 ) )
				settings._renderMode = static_cast<ViewportRenderMode>( modeIndex );
		}

		EditorWidgets::drawToolbarSeparator();

		{
			const bool b2D = settings._bIs2DMode;
			if ( EditorWidgets::drawToggleButton( b2D ? "2D Mode" : "3D Mode", b2D ) )
				settings._bIs2DMode = ( settings._bIs2DMode == false );

			if ( ImGui::IsItemHovered() )
				ImGui::SetTooltip( "Toggle 2D (XY Plane Grid) / 3D (XZ Plane Grid) View Mode" );
		}

		EditorWidgets::drawToolbarSeparator();

		{
			ImGui::TextDisabled( "Cam:" );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 70.0f );
			ImGui::SliderFloat( "##CamSpeed", &settings._cameraSpeed, 0.5f, 20.0f, "%.1f" );
		}

		if ( viewportWidth > 320.0f )
		{
			EditorWidgets::drawToolbarSeparator();
			ImGui::Checkbox( "Stats", &settings._bShowStats );
			ImGui::SameLine();
			ImGui::Checkbox( "Grid", &settings._bShowGrid );
			ImGui::SameLine();
			ImGui::Checkbox( "Cube", &settings._bShowOrientationCube );
			ImGui::SameLine();
			ImGui::Checkbox( "Col", &settings._bShowColliders );
			ImGui::SameLine();
			ImGui::Checkbox( "Cam", &settings._bShowCameras );
			ImGui::SameLine();
			ImGui::Checkbox( "Surf", &settings._bSurfaceSnap );
		}

		if ( viewportWidth > 420.0f )
		{
			EditorWidgets::drawToolbarSeparator();
			if ( ImGui::Button( "Bookmarks" ) )
				ImGui::OpenPopup( "##ViewportBookmarksPopup" );

			if ( ImGui::BeginPopup( "##ViewportBookmarksPopup" ) )
			{
				ImGui::Text( "Camera Bookmarks (Ctrl+1~9)" );
				ImGui::Separator();
				EditorContext* pContext = EditorContext::get();
				if ( pContext != nullptr )
				{
					EditorWorkspace& ws = pContext->getWorkspace();
					for ( uint32 slot = 0; slot < 9; ++slot )
					{
						const bool bHas = ws.hasCameraBookmark( slot );
						utf8	   arrLabel[64];
						formatstring( arrLabel, sizeof( arrLabel ), "Slot %u: %s", slot + 1,
									  bHas ? ws.getCameraBookmark( slot )->_name.c_str() : "<Empty>" );
						if ( ImGui::Selectable( arrLabel, false ) && bHas )
						{
							settings._requestedBookmarkSlot = static_cast<int32>( slot );
						}
						if ( ImGui::IsItemHovered() && bHas )
						{
							const CameraBookmark* pBm = ws.getCameraBookmark( slot );
							if ( pBm != nullptr )
								ImGui::SetTooltip( "Pos: (%.1f, %.1f, %.1f)",
												   static_cast<float64>( pBm->_position._x ),
												   static_cast<float64>( pBm->_position._y ),
												   static_cast<float64>( pBm->_position._z ) );
						}
					}
				}
				ImGui::EndPopup();
			}
		}

		if ( viewportWidth > 520.0f )
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr && pContext->getSelectionManager().getSelectedObjectCount() >= 2 )
			{
				EditorWidgets::drawToolbarSeparator();
				if ( ImGui::Button( "Align..." ) )
					ImGui::OpenPopup( "##ViewportAlignPopup" );

				if ( ImGui::BeginPopup( "##ViewportAlignPopup" ) )
				{
					ImGui::Text( "Multi-Object Alignment" );
					ImGui::Separator();
					EditorWorkspace& ws = pContext->getWorkspace();
					if ( ImGui::MenuItem( "Snap to Ground (Y=0)" ) )
						ws.snapSelectedToGround();
					ImGui::Separator();
					if ( ImGui::MenuItem( "Align X (Center)" ) )
						ws.alignSelectedObjects( AlignAxis::X, AlignType::Center );
					if ( ImGui::MenuItem( "Align Y (Center)" ) )
						ws.alignSelectedObjects( AlignAxis::Y, AlignType::Center );
					if ( ImGui::MenuItem( "Align Z (Center)" ) )
						ws.alignSelectedObjects( AlignAxis::Z, AlignType::Center );
					ImGui::Separator();
					if ( ImGui::MenuItem( "Distribute X Evenly" ) )
						ws.distributeSelectedObjects( AlignAxis::X );
					if ( ImGui::MenuItem( "Distribute Y Evenly" ) )
						ws.distributeSelectedObjects( AlignAxis::Y );
					if ( ImGui::MenuItem( "Distribute Z Evenly" ) )
						ws.distributeSelectedObjects( AlignAxis::Z );
					ImGui::EndPopup();
				}
			}
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

		if ( EditorChrome::beginFloatingBar( barDesc ) == false )
		{
			EditorChrome::endFloatingBar();
			return;
		}

		int32& op = EditorContext::get()->getWorkspace().getGizmoOperationRef();
		ImGui::RadioButton( "Translate", &op, 0 );
		ImGui::SameLine();
		ImGui::RadioButton( "Rotate", &op, 1 );
		ImGui::SameLine();
		ImGui::RadioButton( "Scale", &op, 2 );
		ImGui::SameLine();
		bool& bLocal = EditorContext::get()->getWorkspace().getGizmoLocalSpaceRef();
		ImGui::Checkbox( "Local", &bLocal );

		EditorWidgets::drawToolbarSeparator();

		const float32 arrSnapValues[] = { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
		const utf8*	  arrSnapLabels[] = { "0.1", "0.5", "1.0", "5.0", "10.0" };
		EditorViewportToolbarInternal::drawSnapToggleCombo( "Grid Snap", "##GridSnapVal", settings._bGridSnap, settings._gridSnapValue, arrSnapValues,
															arrSnapLabels, 5, 65.0f, 2 );

		EditorWidgets::drawToolbarSeparator();

		const float32 arrRotValues[] = { 5.0f, 15.0f, 45.0f, 90.0f };
		const utf8*	  arrRotLabels[] = { "5 deg", "15 deg", "45 deg", "90 deg" };
		EditorViewportToolbarInternal::drawSnapToggleCombo( "Rot Snap", "##RotSnapVal", settings._bRotationSnap, settings._rotationSnapValue,
															arrRotValues, arrRotLabels, 4, 60.0f, 1 );

		EditorChrome::endFloatingBar();
	}
} // namespace sw::editor
