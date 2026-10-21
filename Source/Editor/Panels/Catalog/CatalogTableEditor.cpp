#include "pch.h"

#include "Editor/Panels/Catalog/CatalogTableEditor.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Catalog/CatalogDocument.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CatalogTableEditorInternal
        {
            static constexpr float32 kColumnWidth       = 130.0f; ///< 96 DPI 기준입니다
            static constexpr float32 kActionColumnWidth = 70.0f;
            static constexpr float32 kChildColumnWidth  = 64.0f;
            static constexpr int32   kExtraColumnCount  = 2;  ///< 자식 수 · 행 동작
            static constexpr int32   kMaxVisibleRows    = 12; ///< 넘으면 테이블 안에서 세로로 스크롤한다

            /**
             * @brief 그룹 테이블의 바깥 높이입니다 — 행 수만큼(최대 `kMaxVisibleRows`)만 차지합니다.
             * @details 스크롤 표(ScrollX · ScrollY)에 바깥 높이 0 을 주면 ImGui 는 남은 높이를 모두 준다. 그러면 첫 그룹이 패널을 다 차지해
             *          그 아래의 "Add row" 단추와 다음 그룹이 창 밖으로 밀린다.
             */
            static float32 computeOuterHeight( size_t rowCount )
            {
                const ImGuiStyle& style        = ImGui::GetStyle();
                const float32     headerHeight = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
                const float32     rowHeight    = ImGui::GetFrameHeight() + style.CellPadding.y * 2.0f;
                const int32       shownRows    = rowCount < static_cast<size_t>( kMaxVisibleRows ) ? static_cast<int32>( rowCount ) : kMaxVisibleRows;
                const int32       paddedRows   = shownRows > 0 ? shownRows : 1;
                return headerHeight + rowHeight * static_cast<float32>( paddedRows ) + style.ScrollbarSize + style.FrameBorderSize * 2.0f + 2.0f;
            }

            static string describe( string_view verb, string_view groupName, string_view column )
            {
                StringBuilder<constant::kMaxBuffer256> text;
                if ( column.empty() )
                    text.appendFormat( "%# %#", verb, groupName );
                else
                    text.appendFormat( "%# %#.%#", verb, groupName, column );
                return string( text.view() );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    CatalogTableEditor::CatalogTableEditor()
        : _editText{}
        , _pendingColumn{}
        , _pendingText{}
        , _lastActionText{}
        , _editID{ 0 }
        , _pendingElement{ -1 }
        , _pendingKind{ PendingKind::None }
    {
    }

    void CatalogTableEditor::reset()
    {
        _editText.clear();
        _pendingColumn.clear();
        _pendingText.clear();
        _editID         = 0;
        _pendingElement = -1;
        _pendingKind    = PendingKind::None;
    }

    bool CatalogTableEditor::draw( CatalogDocument& inoutDocument )
    {
        if ( inoutDocument.isParsed() == false )
        {
            EditorWidgets::drawEmptyHint( inoutDocument.getError().empty() ? "Nothing to show" : inoutDocument.getError().c_str() );
            return false;
        }

        const float32 dpiScale   = EditorThemeUtil::getDpiScale();
        bool          bFirstCell = true;
        for ( const CatalogDocument::Group& group : inoutDocument.getGroups() )
        {
            ImGui::PushID( group._name.c_str() );
            StringBuilder<constant::kMaxBuffer64> subtitle;
            subtitle.appendFormat( "%# rows", group._listRow.size() );
            EditorWidgets::drawSectionHeader( group._name.c_str(), subtitle.c_str() );

            const int32               columnCount = static_cast<int32>( group._listColumn.size() ) + CatalogTableEditorInternal::kExtraColumnCount;
            constexpr ImGuiTableFlags kTableFlags =
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
            const ImVec2 outerSize{ 0.0f, CatalogTableEditorInternal::computeOuterHeight( group._listRow.size() ) };
            if ( ImGui::BeginTable( "##CatalogRows", columnCount, kTableFlags, outerSize ) )
            {
                for ( const string& column : group._listColumn )
                {
                    ImGui::TableSetupColumn( column.c_str(), ImGuiTableColumnFlags_WidthFixed, CatalogTableEditorInternal::kColumnWidth * dpiScale );
                }
                ImGui::TableSetupColumn( "children", ImGuiTableColumnFlags_WidthFixed, CatalogTableEditorInternal::kChildColumnWidth * dpiScale );
                ImGui::TableSetupColumn( "##actions", ImGuiTableColumnFlags_WidthFixed, CatalogTableEditorInternal::kActionColumnWidth * dpiScale );
                ImGui::TableSetupScrollFreeze( 0, 1 );
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin( static_cast<int32>( group._listRow.size() ) );
                while ( clipper.Step() )
                {
                    for ( int32 rowPosition = clipper.DisplayStart; rowPosition < clipper.DisplayEnd; ++rowPosition )
                    {
                        const int32 elementIndex = group._listRow[static_cast<size_t>( rowPosition )];
                        ImGui::PushID( elementIndex );
                        ImGui::TableNextRow();
                        for ( size_t columnIndex = 0; columnIndex < group._listColumn.size(); ++columnIndex )
                        {
                            ImGui::TableSetColumnIndex( static_cast<int32>( columnIndex ) );
                            drawCell( inoutDocument, elementIndex, group._listColumn[columnIndex], bFirstCell );
                            bFirstCell = false;
                        }

                        const CatalogXMLElement& element = inoutDocument.getParsed().getElement( elementIndex );
                        ImGui::TableSetColumnIndex( static_cast<int32>( group._listColumn.size() ) );
                        if ( element._listChild.empty() )
                            ImGui::TextDisabled( "-" );
                        else
                            ImGui::Text( "%d", static_cast<int32>( element._listChild.size() ) );
                        if ( element._listChild.empty() == false && ImGui::IsItemHovered() )
                        {
                            ImGui::BeginTooltip();
                            for ( const int32 childIndex : element._listChild )
                            {
                                ImGui::TextUnformatted( inoutDocument.getParsed().getElement( childIndex )._name.c_str() );
                            }
                            ImGui::TextDisabled( "Edit nested elements in the raw text view" );
                            ImGui::EndTooltip();
                        }

                        ImGui::TableSetColumnIndex( static_cast<int32>( group._listColumn.size() ) + 1 );
                        if ( ImGui::SmallButton( "Remove" ) )
                        {
                            _pendingKind    = PendingKind::RemoveRow;
                            _pendingElement = elementIndex;
                            _pendingColumn.clear();
                            _pendingText = group._name;
                        }
                        if ( rowPosition == 0 && &group == &inoutDocument.getGroups().front() )
                            EditorSelfTestMarks::note( "catalog.row.remove.first" );
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }

            if ( ImGui::SmallButton( "Add row" ) )
            {
                _pendingKind    = PendingKind::AddRow;
                _pendingElement = -1;
                _pendingColumn.clear();
                _pendingText = group._name;
            }
            if ( &group == &inoutDocument.getGroups().front() )
                EditorSelfTestMarks::note( "catalog.addrow.first" );
            ImGui::PopID();
        }
        return applyPending( inoutDocument );
    }

    void CatalogTableEditor::drawCell( const CatalogDocument& document, int32 elementIndex, const string& column, bool bFirstCell )
    {
        ImGui::PushID( column.c_str() );
        const string* pValue   = document.findCellValue( elementIndex, column );
        const ImGuiID cellID   = ImGui::GetID( "##cell" );
        const bool    bEditing = _editID == cellID;
        string        display  = bEditing ? _editText : ( pValue != nullptr ? *pValue : string() );
        if ( pValue == nullptr && bEditing == false )
            ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.12f } ); // 속성이 없는 칸은 어둡게 — 비어 있는 값과 다르다
        const bool bChanged = EditorWidgets::drawTextField( "##cell", display, -1.0f );
        if ( pValue == nullptr && bEditing == false )
            ImGui::PopStyleColor();
        if ( bFirstCell )
            EditorSelfTestMarks::note( "catalog.cell.first" );
        if ( bChanged )
        {
            _editID   = cellID;
            _editText = display;
        }
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            const bool bUnchanged = ( pValue == nullptr && _editText.empty() ) || ( pValue != nullptr && *pValue == _editText );
            if ( bUnchanged == false )
            {
                _pendingKind    = PendingKind::SetCell;
                _pendingElement = elementIndex;
                _pendingColumn  = column;
                _pendingText    = _editText;
            }
            _editID = 0;
        }
        else if ( ImGui::IsItemDeactivated() && bEditing )
        {
            _editID = 0;
        }

        if ( pValue != nullptr && ImGui::BeginPopupContextItem( "##cellMenu" ) )
        {
            if ( ImGui::MenuItem( "Unset attribute" ) )
            {
                _pendingKind    = PendingKind::UnsetCell;
                _pendingElement = elementIndex;
                _pendingColumn  = column;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    bool CatalogTableEditor::applyPending( CatalogDocument& inoutDocument )
    {
        const PendingKind kind = _pendingKind;
        if ( kind == PendingKind::None )
            return false;
        _pendingKind = PendingKind::None;

        string     groupName;
        const bool bRowScopedEdit = kind == PendingKind::SetCell || kind == PendingKind::UnsetCell || kind == PendingKind::RemoveRow;
        if ( bRowScopedEdit )
        {
            if ( _pendingElement >= 0 && static_cast<size_t>( _pendingElement ) < inoutDocument.getParsed().getElements().size() )
                groupName = inoutDocument.getParsed().getElement( _pendingElement )._name;
        }

        bool bChanged = false;
        switch ( kind )
        {
            case PendingKind::None:
            {
                break;
            }
            case PendingKind::SetCell:
            {
                bChanged        = inoutDocument.setCell( _pendingElement, _pendingColumn, _pendingText );
                _lastActionText = CatalogTableEditorInternal::describe( "Edited", groupName, _pendingColumn );
                break;
            }
            case PendingKind::UnsetCell:
            {
                bChanged        = inoutDocument.unsetCell( _pendingElement, _pendingColumn );
                _lastActionText = CatalogTableEditorInternal::describe( "Unset", groupName, _pendingColumn );
                break;
            }
            case PendingKind::AddRow:
            {
                bChanged        = inoutDocument.addRow( _pendingText );
                _lastActionText = CatalogTableEditorInternal::describe( "Added row to", _pendingText, string_view() );
                break;
            }
            case PendingKind::RemoveRow:
            {
                bChanged        = inoutDocument.removeRow( _pendingElement );
                _lastActionText = CatalogTableEditorInternal::describe( "Removed row from", groupName, string_view() );
                break;
            }
        }
        if ( bChanged == false )
            _lastActionText = inoutDocument.getError();
        else
            SW_LOG_INFO( "Catalog %#", _lastActionText.c_str() );
        return bChanged;
    }
} // namespace sw::editor
