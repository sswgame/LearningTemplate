/**
 * @file EditorWidgets.cpp
 */
#include "Widgets/EditorWidgets.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
	namespace
	{
		ImVec4 toIm( const Color4& c )
		{
			return ImVec4( c.r, c.g, c.b, c.a );
		}
	} // namespace

	bool drawVec3Control( const char* label, float3& values, float32 resetValue, float32 columnWidth, float32 speed )
	{
		ImGui::PushID( label );

		ImGui::Columns( 2 );
		ImGui::SetColumnWidth( 0, columnWidth );
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( label );
		ImGui::NextColumn();

		ImGui::PushMultiItemsWidths( 3, ImGui::CalcItemWidth() );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f } );

		const float32 lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
		const ImVec2  buttonSize{ lineHeight + 3.0f, lineHeight };
		bool		  bChanged = false;

		auto axis = [&]( const char* axisLabel, float32& v, const Color4& col ) {
			ImGui::PushStyleColor( ImGuiCol_Button, toIm( col ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( col.r + 0.1f, col.g + 0.1f, col.b + 0.1f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonActive, toIm( col ) );
			if ( ImGui::Button( axisLabel, buttonSize ) )
			{
				v		 = resetValue;
				bChanged = true;
			}
			ImGui::PopStyleColor( 3 );
			ImGui::SameLine();
			ImGui::PushID( axisLabel );
			if ( ImGui::DragFloat( "##v", &v, speed, 0.0f, 0.0f, "%.2f" ) )
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

	bool beginComponentCard( const char* name, uint64 id, bool* bActive, bool* bRemoveRequested, bool bAccent )
	{
		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
										 ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
										 ImGuiTreeNodeFlags_FramePadding;

		ImGui::PushID( static_cast<int>( id ) );
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
		const bool	  open		 = ImGui::TreeNodeEx( "hdr", flags, "%s", name );
		ImGui::PopStyleVar();
		ImGui::PopStyleColor( 3 );

		if ( bActive != nullptr )
		{
			ImGui::SameLine( availX - lineHeight * 2.2f );
			ImGui::Checkbox( "##active", bActive );
		}

		ImGui::SameLine( availX - lineHeight * 0.5f );
		if ( ImGui::Button( "+", ImVec2{ lineHeight, lineHeight } ) )
			ImGui::OpenPopup( "ComponentSettings" );

		if ( ImGui::BeginPopup( "ComponentSettings" ) )
		{
			if ( ImGui::MenuItem( "Remove component" ) && bRemoveRequested != nullptr )
				*bRemoveRequested = true;
			ImGui::EndPopup();
		}

		if ( open == false )
			ImGui::PopID();
		return open;
	}

	void endComponentCard()
	{
		ImGui::TreePop();
		ImGui::PopID();
	}

	void drawPanelHeader( const char* title, const char* subtitle )
	{
		ImGui::TextUnformatted( title );
		if ( subtitle != nullptr && subtitle[0] != '\0' )
			ImGui::TextDisabled( "%s", subtitle );
		ImGui::Separator();
	}

	void drawChip( const char* label, const Color4& color )
	{
		ImGui::PushStyleColor( ImGuiCol_Button, toIm( color ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, toIm( color ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonActive, toIm( color ) );
		ImGui::SmallButton( label );
		ImGui::PopStyleColor( 3 );
	}

	bool drawPropertyRowBegin( const char* label, float32 labelWidth )
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled( "%s", label );
		ImGui::SameLine( labelWidth );
		ImGui::SetNextItemWidth( -1.0f );
		return true;
	}

	void drawPropertyRowEnd() {}

	void pushInspectorStyle()
	{
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 1.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2.0f, 2.0f ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 5.0f );
	}

	void popInspectorStyle()
	{
		ImGui::PopStyleVar( 3 );
	}
} // namespace sw::editor
