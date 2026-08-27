#include "pch.h"

#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringUtil.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
	namespace
	{
		ImVec4 toIm( const Color4& c )
		{
			return ImVec4( c._r, c._g, c._b, c._a );
		}
	} // namespace

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
			ImGui::PushStyleColor( ImGuiCol_Button, toIm( col ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( col._r + 0.1f, col._g + 0.1f, col._b + 0.1f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonActive, toIm( col ) );
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
			ImGui::PushStyleColor( ImGuiCol_Header, toIm( style::kAccent ) );
			ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.85f, 0.28f, 0.22f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( 0.70f, 0.18f, 0.14f, 1.0f ) );
		}
		else
		{
			ImGui::PushStyleColor( ImGuiCol_Header, toIm( style::kHeader ) );
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

	void EditorWidgets::drawEmptyHint( const utf8* pText )
	{
		if ( pText == nullptr )
			return;
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
		ImGui::PushStyleColor( ImGuiCol_Button, toIm( color ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, toIm( color ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonActive, toIm( color ) );
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
		utf8 buffer[256]{};
		StringUtil::strncpy( buffer, filterText.c_str(), sizeof( buffer ) - 1 );
		const bool bChanged = drawSearchField( pId, buffer, sizeof( buffer ), "Search...", width, true );
		if ( bChanged )
			filterText = buffer;
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
			if ( const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( "SW_ASSET_PATH" ) )
			{
				const utf8* pDropped = static_cast<const utf8*>( pPayload->Data );
				if ( pDropped != nullptr )
				{
					bool bAccept{ true };
					if ( pExpectedExt != nullptr && pExpectedExt[0] != '\0' )
					{
						bAccept = FileUtil::endsWithIgnoreCase( pDropped, pExpectedExt );
					}
					if ( bAccept )
					{
						assetPath = pDropped;
						bChanged  = true;
					}
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
} // namespace sw::editor
