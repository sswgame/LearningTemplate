#include "pch.h"

#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringUtil.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
	namespace
	{
		struct EditorWidgetsInternal
		{
			static ImVec4 toIm( const Color4& c )
			{
				return ImVec4( c._r, c._g, c._b, c._a );
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	bool EditorWidgets::drawVec3Control( const utf8* pLabel, float3& values, float32 resetValue, float32 columnWidth, float32 speed )
	{
		ImGui::PushID( pLabel );

		ImGui::Columns( 2 );
		ImGui::SetColumnWidth( 0, columnWidth );
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( pLabel );
		ImGui::NextColumn();

		ImGui::PushMultiItemsWidths( 3, ImGui::CalcItemWidth() );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f } );

		const float32 lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
		const ImVec2  buttonSize{ lineHeight + 3.0f, lineHeight };
		bool		  bChanged{ false };

		auto axis = [&]( const utf8* pAxisLabel, float32& axisValue, const Color4& col )
		{
			ImGui::PushStyleColor( ImGuiCol_Button, EditorWidgetsInternal::toIm( col ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( col._r + 0.1f, col._g + 0.1f, col._b + 0.1f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonActive, EditorWidgetsInternal::toIm( col ) );
			if ( ImGui::Button( pAxisLabel, buttonSize ) )
			{
				axisValue = resetValue;
				bChanged  = true;
			}
			ImGui::PopStyleColor( 3 );
			ImGui::SameLine();
			ImGui::PushID( pAxisLabel );
			if ( ImGui::DragFloat( "##v", &axisValue, speed, 0.0f, 0.0f, "%.2f" ) )
				bChanged = true;
			ImGui::PopID();
			ImGui::PopItemWidth();
			ImGui::SameLine();
		};

		axis( "X", values._x, style::kAxisX );
		axis( "Y", values._y, style::kAxisY );
		axis( "Z", values._z, style::kAxisZ );

		ImGui::PopStyleVar();
		ImGui::Columns( 1 );
		ImGui::PopID();
		return bChanged;
	}

	bool EditorWidgets::beginComponentCard( const utf8* pName, uint64 id, bool* pBActive, bool* pBRemoveRequested, bool bAccent )
	{
		constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
											 ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
											 ImGuiTreeNodeFlags_FramePadding;

		ImGui::PushID( static_cast<int32>( id ) );
		if ( bAccent )
		{
			ImGui::PushStyleColor( ImGuiCol_Header, EditorWidgetsInternal::toIm( style::kAccent ) );
			ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.85f, 0.28f, 0.22f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( 0.70f, 0.18f, 0.14f, 1.0f ) );
		}
		else
		{
			ImGui::PushStyleColor( ImGuiCol_Header, EditorWidgetsInternal::toIm( style::kHeader ) );
			ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.28f, 0.42f, 0.55f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( 0.18f, 0.30f, 0.42f, 1.0f ) );
		}

		ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2{ 4.0f, 4.0f } );
		const float32 lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
		const float32 availX	 = ImGui::GetContentRegionAvail().x;
		const bool	  open		 = ImGui::TreeNodeEx( "hdr", flags, "%s", pName );
		ImGui::PopStyleVar();
		ImGui::PopStyleColor( 3 );

		if ( pBActive != nullptr )
		{
			ImGui::SameLine( availX - lineHeight * 2.2f );
			ImGui::Checkbox( "##active", pBActive );
		}

		ImGui::SameLine( availX - lineHeight * 0.5f );
		if ( ImGui::Button( "+", ImVec2{ lineHeight, lineHeight } ) )
			ImGui::OpenPopup( "ComponentSettings" );

		if ( ImGui::BeginPopup( "ComponentSettings" ) )
		{
			if ( ImGui::MenuItem( "Remove component" ) && pBRemoveRequested != nullptr )
				*pBRemoveRequested = true;
			ImGui::EndPopup();
		}

		if ( open == false )
			ImGui::PopID();
		return open;
	}

	void EditorWidgets::endComponentCard()
	{
		ImGui::TreePop();
		ImGui::PopID();
	}

	void EditorWidgets::drawSectionHeader( const utf8* pTitle, const utf8* pSubtitle )
	{
		ImGui::TextUnformatted( pTitle );
		if ( pSubtitle != nullptr && pSubtitle[0] != '\0' )
			ImGui::TextDisabled( "%s", pSubtitle );
		ImGui::Separator();
	}

	void EditorWidgets::drawToolbarSeparator()
	{
		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();
	}

	bool EditorWidgets::drawToggleButton( const utf8* pLabel, bool bActive, const Color4& activeColor )
	{
		const Color4& color = bActive ? activeColor : style::kToggleInactive;
		ImGui::PushStyleColor( ImGuiCol_Button, EditorWidgetsInternal::toIm( color ) );
		const bool bClicked = ImGui::Button( pLabel );
		ImGui::PopStyleColor();
		return bClicked;
	}

	void EditorWidgets::drawEmptyHint( const utf8* pText )
	{
		if ( pText == nullptr )
			return;
		ImGui::TextDisabled( "%s", pText );
	}

	void EditorWidgets::drawCountLabel( uint32 visible, uint32 total, const utf8* pUnit )
	{
		const bool bHasUnit = ( pUnit != nullptr && pUnit[0] != '\0' );
		if ( total == 0 )
		{
			if ( bHasUnit )
				ImGui::TextDisabled( "%u %s", visible, pUnit );
			else
				ImGui::TextDisabled( "%u", visible );
			return;
		}

		if ( bHasUnit )
			ImGui::TextDisabled( "%u / %u %s", visible, total, pUnit );
		else
			ImGui::TextDisabled( "%u / %u", visible, total );
	}

	void EditorWidgets::drawPanelStatus( const utf8* pText )
	{
		if ( pText == nullptr || pText[0] == '\0' )
			return;
		ImGui::Separator();
		ImGui::TextDisabled( "%s", pText );
	}

	bool EditorWidgets::drawSearchField( const utf8* pId, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, float32 width,
										 bool bShowClear )
	{
		if ( pBuffer == nullptr || bufferBytes == 0 )
			return false;

		ImGui::PushID( pId != nullptr ? pId : "##search" );
		if ( width < 0.0f )
			ImGui::SetNextItemWidth( -1.0f );
		else if ( width > 0.0f )
			ImGui::SetNextItemWidth( width );
		else
		{
			const float32 avail		 = ImGui::GetContentRegionAvail().x;
			const float32 clearWidth = bShowClear ? 28.0f : 0.0f;
			ImGui::SetNextItemWidth( ( avail > clearWidth + 4.0f ) ? ( avail - clearWidth ) : avail );
		}

		const utf8* pHintText = ( pHint != nullptr ) ? pHint : "Search...";
		bool		bChanged  = ImGui::InputTextWithHint( "##search", pHintText, pBuffer, bufferBytes );
		if ( bShowClear )
		{
			ImGui::SameLine();
			if ( ImGui::Button( "X", ImVec2{ 22.0f, 0.0f } ) && pBuffer[0] != '\0' )
			{
				pBuffer[0] = '\0';
				bChanged   = true;
			}
		}
		ImGui::PopID();
		return bChanged;
	}

	void EditorWidgets::drawChip( const utf8* pLabel, const Color4& color )
	{
		ImGui::PushStyleColor( ImGuiCol_Button, EditorWidgetsInternal::toIm( color ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorWidgetsInternal::toIm( color ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonActive, EditorWidgetsInternal::toIm( color ) );
		ImGui::SmallButton( pLabel );
		ImGui::PopStyleColor( 3 );
	}

	bool EditorWidgets::drawPropertyRowBegin( const utf8* pLabel, float32 labelWidth )
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled( "%s", pLabel );
		ImGui::SameLine( labelWidth );
		ImGui::SetNextItemWidth( -1.0f );
		return true;
	}

	void EditorWidgets::drawPropertyRowEnd()
	{
	}

	bool EditorWidgets::drawSearchFilter( const utf8* pId, string& filterText, float32 width )
	{
		utf8 arrBuffer[256]{};
		StringUtil::strncpy( arrBuffer, filterText.c_str(), sizeof( arrBuffer ) - 1 );
		const bool bChanged = drawSearchField( pId, arrBuffer, sizeof( arrBuffer ), "Search...", width, true );
		if ( bChanged )
			filterText = arrBuffer;
		return bChanged;
	}

	bool EditorWidgets::drawAssetSlot( const utf8* pLabel, string& assetPath, const utf8* pExpectedExt, float32 labelWidth )
	{
		ImGui::PushID( pLabel );
		bool bChanged{ false };

		if ( pLabel != nullptr && pLabel[0] != '\0' )
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled( "%s", pLabel );
			ImGui::SameLine( labelWidth );
		}

		const float32 availWidth	= ImGui::GetContentRegionAvail().x;
		const float32 clearBtnWidth = 24.0f;
		const float32 inputWidth	= ( assetPath.empty() == false ) ? ( availWidth - clearBtnWidth - 4.0f ) : availWidth;

		const utf8* pDisplayPath = assetPath.empty() ? "(None / Drop Asset)" : assetPath.c_str();
		ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f } );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.25f, 0.25f, 0.25f, 1.0f } );
		ImGui::Button( pDisplayPath, ImVec2{ inputWidth, 0.0f } );
		ImGui::PopStyleColor( 2 );

		if ( ImGui::BeginDragDropTarget() )
		{
			string droppedPath;
			if ( tryAcceptAssetPayload( droppedPath ) )
			{
				bool bAccept{ true };
				if ( pExpectedExt != nullptr && pExpectedExt[0] != '\0' )
					bAccept = FileUtil::endsWithIgnoreCase( droppedPath.c_str(), pExpectedExt );
				if ( bAccept )
				{
					assetPath = droppedPath;
					bChanged  = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		if ( assetPath.empty() == false )
		{
			ImGui::SameLine();
			if ( ImGui::Button( "x", ImVec2{ clearBtnWidth, 0.0f } ) )
			{
				assetPath.clear();
				bChanged = true;
			}
		}

		ImGui::PopID();
		return bChanged;
	}

	bool EditorWidgets::drawSplitter( const utf8* pId, bool bVertical, float32 thickness, float32* pSize1, float32* pSize2,
									  float32 minSize1, float32 minSize2 )
	{
		ImGuiContext& g		  = *GImGui;
		ImGuiWindow*  pWindow = g.CurrentWindow;
		ImGuiID		  id	  = pWindow->GetID( pId );
		ImRect		  bb;
		bb.Min = ImVec2{ pWindow->DC.CursorPos.x + ( bVertical ? *pSize1 : 0.0f ),
						 pWindow->DC.CursorPos.y + ( bVertical ? 0.0f : *pSize1 ) };
		const ImVec2 size =
			ImGui::CalcItemSize( bVertical ? ImVec2{ thickness, -1.0f } : ImVec2{ -1.0f, thickness }, 0.0f, 0.0f );
		bb.Max = ImVec2{ bb.Min.x + size.x, bb.Min.y + size.y };
		return ImGui::SplitterBehavior( bb, id, bVertical ? ImGuiAxis_X : ImGuiAxis_Y, pSize1, pSize2, minSize1, minSize2,
										0.0f );
	}

	bool EditorWidgets::drawColorEdit( const utf8* pLabel, Color4& color, float32 labelWidth )
	{
		ImGui::PushID( pLabel );
		if ( pLabel != nullptr && pLabel[0] != '\0' )
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled( "%s", pLabel );
			ImGui::SameLine( labelWidth );
		}
		ImGui::SetNextItemWidth( -1.0f );
		float32 arrCol[4]{ color._r, color._g, color._b, color._a };
		bool	bChanged{ false };
		if ( ImGui::ColorEdit4( "##color", arrCol, ImGuiColorEditFlags_AlphaBar ) )
		{
			color._r = arrCol[0];
			color._g = arrCol[1];
			color._b = arrCol[2];
			color._a = arrCol[3];
			bChanged = true;
		}
		ImGui::PopID();
		return bChanged;
	}

	void EditorWidgets::pushInspectorStyle()
	{
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 1.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2.0f, 2.0f ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 5.0f );
	}

	void EditorWidgets::popInspectorStyle()
	{
		ImGui::PopStyleVar( 3 );
	}

	void EditorWidgets::drawAssetDragSource( const utf8* pRelativePath, bool bAllowNullId )
	{
		if ( pRelativePath == nullptr || pRelativePath[0] == '\0' )
			return;

		ImGuiDragDropFlags flags = 0;
		if ( bAllowNullId )
			flags |= ImGuiDragDropFlags_SourceAllowNullID;

		if ( ImGui::BeginDragDropSource( flags ) == false )
			return;

		const uint32 pathBytes = StringUtil::strlen( pRelativePath ) + 1;
		ImGui::SetDragDropPayload( kAssetPathPayload, pRelativePath, pathBytes );
		ImGui::TextUnformatted( pRelativePath );
		ImGui::EndDragDropSource();
	}

	bool EditorWidgets::tryAcceptAssetPayload( string& outPath )
	{
		const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( kAssetPathPayload );
		if ( pPayload == nullptr || pPayload->Data == nullptr )
			return false;

		outPath = static_cast<const utf8*>( pPayload->Data );
		return true;
	}

	bool EditorWidgets::acceptAssetDrop( string& outPath )
	{
		if ( ImGui::BeginDragDropTarget() == false )
			return false;

		const bool bAccepted = tryAcceptAssetPayload( outPath );
		ImGui::EndDragDropTarget();
		return bAccepted;
	}

	bool EditorWidgets::updateListSelection( int32& selectedIndex, int32 itemCount, bool bRepeat )
	{
		if ( itemCount <= 0 )
		{
			selectedIndex = 0;
			return false;
		}

		if ( selectedIndex >= itemCount )
			selectedIndex = itemCount - 1;
		if ( selectedIndex < 0 )
			selectedIndex = 0;

		if ( ImGui::IsKeyPressed( ImGuiKey_DownArrow, bRepeat ) )
		{
			++selectedIndex;
			if ( selectedIndex >= itemCount )
				selectedIndex = itemCount - 1;
		}
		if ( ImGui::IsKeyPressed( ImGuiKey_UpArrow, bRepeat ) )
		{
			--selectedIndex;
			if ( selectedIndex < 0 )
				selectedIndex = 0;
		}

		const bool bValidSelection = ( 0 <= selectedIndex && selectedIndex < itemCount );
		if ( bValidSelection == false )
			return false;
		return ImGui::IsKeyPressed( ImGuiKey_Enter, bRepeat );
	}

	EditorUnsavedChoice EditorWidgets::drawUnsavedChangesModal( const utf8* pPopupId, const utf8* pMessage )
	{
		if ( pPopupId == nullptr || pPopupId[0] == '\0' )
			return EditorUnsavedChoice::None;
		if ( ImGui::BeginPopupModal( pPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize ) == false )
			return EditorUnsavedChoice::None;

		ImGui::TextUnformatted( pMessage != nullptr ? pMessage : "You have unsaved changes." );
		EditorUnsavedChoice choice = EditorUnsavedChoice::None;
		if ( ImGui::Button( "Save" ) )
			choice = EditorUnsavedChoice::Save;
		ImGui::SameLine();
		if ( ImGui::Button( "Don't Save" ) )
			choice = EditorUnsavedChoice::Discard;
		ImGui::SameLine();
		if ( ImGui::Button( "Cancel" ) )
			choice = EditorUnsavedChoice::Cancel;
		if ( choice != EditorUnsavedChoice::None )
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return choice;
	}
} // namespace sw::editor
