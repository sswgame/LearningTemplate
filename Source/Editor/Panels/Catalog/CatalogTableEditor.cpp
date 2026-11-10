#include "pch.h"

#include "Editor/Panels/Catalog/CatalogTableEditor.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Catalog/Model/CatalogDocument.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CatalogTableEditorInternal
        {
            static constexpr float32 kColumnWidth            = 130.0f; ///< 96 DPI 기준입니다
            static constexpr float32 kCurveKeyFieldWidth     = 110.0f; ///< 곡선 키 줄의 칸 폭(96 DPI)
            static constexpr float32 kChildColumnWidth       = 120.0f; ///< 수와 Children · Curves 단추
            static constexpr float32 kChildActionColumnWidth = 210.0f; ///< 자식 창의 Up · Down · Reset · Remove 단추
            static constexpr int32   kExtraColumnCount       = 2;      ///< 자식 수 · 행 동작
            static constexpr int32   kMaxVisibleRows         = 12;     ///< 넘으면 테이블 안에서 세로로 스크롤한다
            static constexpr float32 kCurveGraphLines        = 12.0f;  ///< 곡선 창 그래프의 높이(글자 줄 수 — 시퀀서 곡선 편집기와 같다)

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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    CatalogTableEditor::CatalogTableEditor()
        : _listCurve{}
        , _listGradient{}
        , _listProbeCurve{}
        , _listSample{}
        , _listGradientColor{}
        , _listVisibleRow{}
        , _listChildSection{}
        , _curveOwner{}
        , _childOwner{}
        , _graphCurve{}
        , _bOpenCurvePopup{ SW_FALSE }
        , _bOpenChildPopup{ SW_FALSE }
        , _bEditFailed{ SW_FALSE }
        , _editText{}
        , _lastActionText{}
        , _editID{ 0 }
        , _pending{}
        , _graphState{}
        , _rowFilter{}
    {
    }

    void CatalogTableEditor::reset()
    {
        _editText.clear();
        _editID  = 0;
        _pending = CatalogTableEdit{};
        _curveOwner.reset();
        _childOwner.reset();
        _graphCurve.reset();
        _bOpenCurvePopup = SW_FALSE;
        _bOpenChildPopup = SW_FALSE;
        _bEditFailed     = SW_FALSE;
    }

    bool CatalogTableEditor::takeEditFailed()
    {
        const bool bFailed = _bEditFailed == SW_TRUE;
        _bEditFailed       = SW_FALSE;
        return bFailed;
    }

    bool CatalogTableEditor::draw( CatalogDocument& inoutDocument )
    {
        if ( inoutDocument.isParsed() == false )
        {
            EditorWidgets::drawEmptyHint( inoutDocument.getError().empty() ? "Nothing to show" : inoutDocument.getError().c_str() );
            return false;
        }

        if ( inoutDocument.getInheritanceError().empty() == false )
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", inoutDocument.getInheritanceError().c_str() );
        // 언리얼 Data Table 의 행 검색처럼 칸 값으로 거른다.
        EditorWidgets::drawSearchField( "##catalogRowFilter", _rowFilter, SW_LOCKEY( "EdCommon", "SearchHint" ), -1.0f, false );

        bool bFirstCell        = true;
        bool bFirstCurveButton = true;
        bool bFirstVariantRow  = true;
        for ( const CatalogDocument::Group& group : inoutDocument.getGroups() )
        {
            drawGroup( inoutDocument, group, &group == &inoutDocument.getGroups().front(), bFirstCell, bFirstCurveButton, bFirstVariantRow );
        }
        drawCurvePopup( inoutDocument );
        drawChildPopup( inoutDocument );
        return applyPending( inoutDocument );
    }

    void CatalogTableEditor::drawGroup( const CatalogDocument& document, const CatalogDocument::Group& group, bool bFirstGroup, bool& inoutbFirstCell, bool& inoutbFirstCurveButton, bool& inoutbFirstVariantButton )
    {
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::PushID( group._name.c_str() );
        document.collectVisibleRows( group, _rowFilter.c_str(), _listVisibleRow );
        StringBuilder<constant::buffer::kMax64> subtitle;
        if ( _listVisibleRow.size() == group._listRow.size() )
            subtitle.appendFormat( "%# rows", group._listRow.size() );
        else
            subtitle.appendFormat( "%# / %# rows", _listVisibleRow.size(), group._listRow.size() );
        EditorWidgets::drawSectionHeader( group._name.c_str(), subtitle.c_str() );
        if ( group._duplicateID.empty() == false )
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "Two rows share the id '%s' - the loader refuses this", group._duplicateID.c_str() );

        const int32               columnCount = static_cast<int32>( group._listColumn.size() ) + CatalogTableEditorInternal::kExtraColumnCount;
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
        const ImVec2              outerSize{ 0.0f, CatalogTableEditorInternal::computeOuterHeight( _listVisibleRow.size() ) };
        if ( ImGui::BeginTable( "##CatalogRows", columnCount, kTableFlags, outerSize ) )
        {
            // 동작 칸은 맨 앞에 고정한다 - 맨 뒤면 칸이 많은 표에서 가로 스크롤 밖으로 밀려 행을 복사 · 삭제하지 못한다.
            // 폭은 줄 동작 단추를 모두 한 줄에 담는 값이다 - 고정 폭이면 글꼴 · 번역에 따라 뒤 단추가 칸 밖으로 잘린다.
            const float32 spacing     = ImGui::GetStyle().ItemSpacing.x;
            const float32 actionWidth = EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "Duplicate" ) ) + EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) +
                                        EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "CreateVariant" ) ) + EditorChrome::measureSmallButton( SW_LOCKEY( "EditorCatalog", "Children" ) ) + spacing * 3.0f;
            ImGui::TableSetupColumn( "##actions", ImGuiTableColumnFlags_WidthFixed, actionWidth );
            for ( const string& column : group._listColumn )
            {
                ImGui::TableSetupColumn( column.c_str(), ImGuiTableColumnFlags_WidthFixed, CatalogTableEditorInternal::kColumnWidth * dpiScale );
            }
            ImGui::TableSetupColumn( "children", ImGuiTableColumnFlags_WidthFixed, CatalogTableEditorInternal::kChildColumnWidth * dpiScale );
            ImGui::TableSetupScrollFreeze( 1, 1 );
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin( static_cast<int32>( _listVisibleRow.size() ) );
            while ( clipper.Step() )
            {
                for ( int32 rowPosition = clipper.DisplayStart; rowPosition < clipper.DisplayEnd; ++rowPosition )
                {
                    drawRow( document, group, _listVisibleRow[static_cast<size_t>( rowPosition )], rowPosition, bFirstGroup, inoutbFirstCell, inoutbFirstCurveButton, inoutbFirstVariantButton );
                }
            }
            ImGui::EndTable();
        }

        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "AddRow" ) ) )
        {
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::AddRow, nullopt, string(), group._name };
        }
        if ( bFirstGroup )
            EditorSelfTestMarks::record( "catalog.addrow.first" );
        ImGui::PopID();
    }

    void CatalogTableEditor::drawRow( const CatalogDocument& document, const CatalogDocument::Group& group, int32 elementIndex, int32 rowPosition, bool bFirstGroup, bool& inoutbFirstCell,
                                      bool& inoutbFirstCurveButton, bool& inoutbFirstVariantButton )
    {
        ImGui::PushID( elementIndex );
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex( 0 );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdLabel", "Duplicate" ) ) )
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::DuplicateRow, elementIndex, string(), string() };
        if ( rowPosition == 0 && bFirstGroup )
            EditorSelfTestMarks::record( "catalog.row.duplicate.first" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) )
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::RemoveRow, elementIndex, string(), string() };
        if ( rowPosition == 0 && bFirstGroup )
            EditorSelfTestMarks::record( "catalog.row.remove.first" );
        if ( document.canCreateVariant( elementIndex ) )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "CreateVariant" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdLabel", "CreateVariant" ) ) )
                _pending = CatalogTableEdit{ CatalogTableEdit::Kind::CreateVariant, elementIndex, string(), string() };
            // 자식 창 버튼은 줄 동작과 함께 고정 열에 둔다 - 끝 열이면 칸이 많은 표에서 가로로 스크롤해야 보인다.
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorCatalog", "Children" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "Children" ) ) )
            {
                _childOwner      = elementIndex;
                _bOpenChildPopup = SW_TRUE;
            }
            if ( rowPosition == 0 && bFirstGroup )
                EditorSelfTestMarks::record( "catalog.children.open" );
            // 시나리오가 "물려받는 첫 행" 을 고를 수 있게 한다.
            if ( inoutbFirstVariantButton && document.getParsed().getElement( elementIndex ).findAttribute( DataInheritance::kBaseAttribute ).has_value() )
            {
                EditorSelfTestMarks::record( "catalog.children.open.variant" );
                inoutbFirstVariantButton = false;
            }
        }

        for ( size_t columnIndex = 0; columnIndex < group._listColumn.size(); ++columnIndex )
        {
            ImGui::TableSetColumnIndex( static_cast<int32>( columnIndex ) + 1 );
            drawCell( document, elementIndex, group._listColumn[columnIndex], inoutbFirstCell, -1.0f );
            inoutbFirstCell = false;
        }

        const CatalogXMLElement& element = document.getParsed().getElement( elementIndex );
        ImGui::TableSetColumnIndex( static_cast<int32>( group._listColumn.size() ) + 1 );
        if ( element._listChild.empty() )
            ImGui::TextDisabled( "-" );
        else
            ImGui::Text( "%d", static_cast<int32>( element._listChild.size() ) );
        const bool bHovered = document.canCreateVariant( elementIndex ) == false && element._listChild.empty() == false && ImGui::IsItemHovered();
        if ( element._listChild.empty() == false )
        {
            CatalogCurve::collectCurves( document.getParsed(), elementIndex, _listProbeCurve );
            if ( _listProbeCurve.empty() == false )
            {
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Curves" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Curves" ) ) )
                {
                    _curveOwner      = elementIndex;
                    _bOpenCurvePopup = SW_TRUE;
                }
                if ( inoutbFirstCurveButton )
                    EditorSelfTestMarks::record( "catalog.curves.open" );
                inoutbFirstCurveButton = false;
            }
        }
        if ( bHovered )
        {
            ImGui::BeginTooltip();
            for ( const int32 childIndex : element._listChild )
            {
                ImGui::TextUnformatted( document.getParsed().getElement( childIndex )._name.c_str() );
            }
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "EditNestedElementsInTheRawTextView" ) );
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    void CatalogTableEditor::drawCell( const CatalogDocument& document, int32 elementIndex, const string& column, bool bFirstCell, float32 width )
    {
        ImGui::PushID( column.c_str() );
        const string*     pValue     = document.findCellValue( elementIndex, column );
        const utf8*       pInherited = pValue == nullptr ? document.findInheritedValue( elementIndex, column ) : nullptr;
        const string_view shown      = pValue != nullptr ? string_view( *pValue ) : ( pInherited != nullptr ? string_view( pInherited ) : string_view{} );
        string            committed;
        if ( drawValueField( shown, pValue == nullptr, pInherited != nullptr, width, false, committed ) )
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::SetCell, elementIndex, column, committed };
        if ( bFirstCell )
            EditorSelfTestMarks::record( "catalog.cell.first" );

        if ( pValue != nullptr && EditorInputGUI::beginItemContextMenu( "##cellMenu" ) )
        {
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "UnsetAttribute" ) ) )
                _pending = CatalogTableEdit{ CatalogTableEdit::Kind::UnsetCell, elementIndex, column, string() };
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    bool CatalogTableEditor::drawValueField( string_view shown, bool bAbsent, bool bDim, float32 width, bool bMarkFirst, string& outCommitted )
    {
        const ImGuiID cellID   = ImGui::GetID( "##cell" );
        const bool    bEditing = _editID == cellID;
        string        display  = bEditing ? _editText : string( shown );
        if ( bAbsent && bEditing == false )
            ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.12f } ); // 속성이 없는 칸은 어둡게 — 비어 있는 값과 다르다
        if ( bDim && bEditing == false )
            ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) ); // 물려받은 값은 흐리게 — 이 행이 적은 값이 아니다
        const bool bChanged = EditorWidgets::drawTextField( "##cell", display, width );
        if ( bDim && bEditing == false )
            ImGui::PopStyleColor();
        if ( bAbsent && bEditing == false )
            ImGui::PopStyleColor();
        if ( bMarkFirst )
            EditorSelfTestMarks::record( "catalog.children.cell" );
        if ( bChanged )
        {
            _editID   = cellID;
            _editText = display;
        }
        bool bCommitted = false;
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            if ( shown != string_view( _editText ) )
            {
                outCommitted = _editText;
                bCommitted   = true;
            }
            _editID = 0;
        }
        else if ( ImGui::IsItemDeactivated() && bEditing )
        {
            _editID = 0;
        }
        return bCommitted;
    }

    void CatalogTableEditor::drawChildPopup( const CatalogDocument& document )
    {
        constexpr const utf8* kPopupName = "Children##catalogChildren";
        if ( _bOpenChildPopup )
        {
            EditorInputGUI::openPopup( kPopupName );
            _bOpenChildPopup = SW_FALSE;
        }
        EditorChrome::setNextWindowExtent( EditorTextExtent::ofText( 100.0f, 30.0f ), false );
        bool bOpen = true;
        if ( ImGui::BeginPopupModal( kPopupName, &bOpen ) == false )
            return;
        // 창이 열린 채 편집기가 비워지면(`reset`) 그릴 표가 없다 — 아래 편집은 표가 있을 때만 `_childOwner` 를 읽는다.
        if ( _childOwner.has_value() )
            document.collectChildSections( *_childOwner, _listChildSection );
        else
            _listChildSection.clear();
        if ( _listChildSection.empty() )
        {
            EditorWidgets::drawEmptyHint( document.getInheritanceError().empty() ? SW_LOCKEY( "EditorCatalog", "NoChildRules" ) : document.getInheritanceError().c_str() );
        }
        else
        {
            ImGui::BeginChild( "##childList", ImVec2{ 0.0f, -ImGui::GetFrameHeightWithSpacing() } );
            bool bFirstCell   = true;
            bool bFirstAdd    = true;
            bool bFirstRemove = true;
            for ( const CatalogChildSection& section : _listChildSection )
            {
                drawChildSection( section, bFirstCell, bFirstAdd, bFirstRemove );
            }
            ImGui::EndChild();
        }
        const bool bCloseClicked = ImGui::Button( SW_LOCKEY( "EdText", "Close" ) );
        EditorSelfTestMarks::record( "catalog.children.close" );
        if ( bCloseClicked || bOpen == false )
        {
            _childOwner.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void CatalogTableEditor::drawChildSectionActions( const CatalogChildSection& section, bool& inoutbFirstAdd )
    {
        // 구조체 칸은 하나뿐이라 더할 것이 없다.
        if ( section._kind == CatalogChildSectionKind::Field )
            return;
        const auto groupEdit = [this, &section]( CatalogTableEdit::Kind kind )
        { _pending = CatalogTableEdit{ kind, _childOwner.value(), section._name, string(), section._ownerPath }; };
        if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "AddChild" ) ) )
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::AddChildNode, _childOwner.value(), section._name, section._keyAttribute, section._ownerPath };
        if ( inoutbFirstAdd )
            EditorSelfTestMarks::record( "catalog.children.add" );
        inoutbFirstAdd = false;
        if ( section._kind != CatalogChildSectionKind::Whole || section._bHasParent == false )
            return;
        // 규칙에 없는 칸: 물려받는다 · 적어서 바꾼다 · 비운다 가운데 하나다.
        if ( section._bOverridden || section._bCleared )
        {
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "UseInherited" ) ) )
                groupEdit( CatalogTableEdit::Kind::InheritChildGroup );
        }
        if ( section._bOverridden == false && section._bCleared == false && section._listNode.empty() == false )
        {
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "OverrideInherited" ) ) )
                groupEdit( CatalogTableEdit::Kind::OverrideChildGroup );
        }
        if ( section._bCleared == false )
        {
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "ClearInherited" ) ) )
                groupEdit( CatalogTableEdit::Kind::ClearChildGroup );
            EditorSelfTestMarks::record( "catalog.children.clear" );
        }
    }

    void CatalogTableEditor::drawChildSection( const CatalogChildSection& section, bool& inoutbFirstCell, bool& inoutbFirstAdd, bool& inoutbFirstRemove )
    {
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::PushID( section._title.c_str() );
        const utf8* pState = nullptr;
        if ( section._bCleared )
            pState = SW_LOCKEY( "EditorCatalog", "ChildCleared" );
        else if ( section._bOverridden && section._bHasParent )
            pState = SW_LOCKEY( "EditorCatalog", "ChildOverridden" );
        StringBuilder<constant::buffer::kMax64> subtitle;
        subtitle.appendFormat( "%# rows", section._listNode.size() );
        EditorWidgets::drawSectionHeader( section._title.c_str(), pState != nullptr ? pState : subtitle.c_str() );
        drawChildSectionActions( section, inoutbFirstAdd );
        if ( section._listNode.empty() == false && section._listColumn.empty() == false )
        {
            const bool                bActions    = section._kind != CatalogChildSectionKind::Field;
            const int32               columnCount = static_cast<int32>( section._listColumn.size() ) + ( bActions ? 1 : 0 );
            constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
            if ( ImGui::BeginTable( "##ChildRows", columnCount, kTableFlags, ImVec2{ 0.0f, CatalogTableEditorInternal::computeOuterHeight( section._listNode.size() ) } ) )
            {
                if ( bActions )
                    ImGui::TableSetupColumn( "##actions", ImGuiTableColumnFlags_WidthFixed, CatalogTableEditorInternal::kChildActionColumnWidth * dpiScale );
                for ( const string& column : section._listColumn )
                {
                    ImGui::TableSetupColumn( column.c_str(), ImGuiTableColumnFlags_WidthFixed, CatalogTableEditorInternal::kColumnWidth * dpiScale );
                }
                ImGui::TableSetupScrollFreeze( bActions ? 1 : 0, 1 );
                ImGui::TableHeadersRow();
                for ( size_t nodeIndex = 0; nodeIndex < section._listNode.size(); ++nodeIndex )
                {
                    drawChildNode( section, section._listNode[nodeIndex], static_cast<int32>( nodeIndex ), inoutbFirstCell, inoutbFirstRemove );
                }
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }

    void CatalogTableEditor::drawChildNode( const CatalogChildSection& section, const CatalogChildNode& node, int32 nodePosition, bool& inoutbFirstCell, bool& inoutbFirstRemove )
    {
        ImGui::PushID( nodePosition );
        ImGui::TableNextRow();
        const bool bActions = section._kind != CatalogChildSectionKind::Field;
        const bool bLocked  = node._bRemoved || ( section._kind == CatalogChildSectionKind::Whole && section._bOverridden == false );
        if ( bActions )
        {
            ImGui::TableSetColumnIndex( 0 );
            const auto nodeEdit = [this, &node]( CatalogTableEdit::Kind kind )
            { _pending = CatalogTableEdit{ kind, _childOwner.value(), string(), string(), node._path }; };
            if ( node._bRemoved )
            {
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorCatalog", "ChildRemoved" ) );
                ImGui::SameLine();
                if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "RestoreChild" ) ) )
                    nodeEdit( CatalogTableEdit::Kind::RestoreChildNode );
                EditorSelfTestMarks::record( "catalog.children.restore" );
            }
            else if ( bLocked == false )
            {
                if ( section._kind == CatalogChildSectionKind::List && section._bHasParent )
                {
                    if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "MoveEarlier" ) ) )
                        nodeEdit( CatalogTableEdit::Kind::MoveChildEarlier );
                    ImGui::SameLine();
                    if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "MoveLater" ) ) )
                        nodeEdit( CatalogTableEdit::Kind::MoveChildLater );
                    if ( node._bMoved )
                    {
                        ImGui::SameLine();
                        if ( ImGui::SmallButton( SW_LOCKEY( "EditorCatalog", "ResetOrder" ) ) )
                            nodeEdit( CatalogTableEdit::Kind::ResetChildOrder );
                    }
                    ImGui::SameLine();
                }
                if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) )
                    nodeEdit( CatalogTableEdit::Kind::RemoveChildNode );
                if ( inoutbFirstRemove )
                    EditorSelfTestMarks::record( "catalog.children.remove" );
                inoutbFirstRemove = false;
            }
        }
        const int32 firstColumn = bActions ? 1 : 0;
        for ( size_t cellIndex = 0; cellIndex < node._listCell.size(); ++cellIndex )
        {
            const CatalogChildCell& cell = node._listCell[cellIndex];
            ImGui::TableSetColumnIndex( firstColumn + static_cast<int32>( cellIndex ) );
            ImGui::PushID( cell._column.c_str() );
            const bool bReadOnly = bLocked || ( cell._bKey && node._bInherited );
            if ( bReadOnly )
            {
                // 물려받은 원소의 열쇠와 뺀 원소는 고칠 수 없다 - 글로만 보인다.
                ImGui::TextDisabled( "%s", cell._value.c_str() );
            }
            else
            {
                string committed;
                if ( drawValueField( cell._value, cell._bWritten == false, cell._bInherited, -1.0f, inoutbFirstCell, committed ) )
                    _pending = CatalogTableEdit{ CatalogTableEdit::Kind::SetChildCell, _childOwner.value(), cell._column, committed, node._path };
                inoutbFirstCell = false;
                if ( cell._bWritten && cell._bKey == false && EditorInputGUI::beginItemContextMenu( "##cellMenu" ) )
                {
                    if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "UnsetAttribute" ) ) )
                        _pending = CatalogTableEdit{ CatalogTableEdit::Kind::UnsetChildCell, _childOwner.value(), cell._column, string(), node._path };
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    bool CatalogTableEditor::applyPending( CatalogDocument& inoutDocument )
    {
        if ( _pending._kind == CatalogTableEdit::Kind::None )
            return false;
        const CatalogTableEdit edit = std::move( _pending );
        _pending                    = CatalogTableEdit{};
        const bool bChanged         = edit.apply( inoutDocument, _lastActionText );
        _bEditFailed                = bChanged ? SW_FALSE : SW_TRUE;
        if ( bChanged )
            SW_LOG_INFO_ID( "Catalog.TableEdited", "Catalog %#", _lastActionText.c_str() );
        else
            SW_LOG_WARNING_ID( "Catalog.TableEditRefused", "Catalog edit refused: %#", _lastActionText.c_str() );
        return bChanged;
    }

    void CatalogTableEditor::drawGradientBar( const CatalogGradient& gradient )
    {
        constexpr uint32 kSegmentCount = 32;
        const float32    width         = ImGui::GetContentRegionAvail().x;
        const float32    height        = ImGui::GetFrameHeight();
        const ImVec2     origin        = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2{ width, height } );
        ImDrawList* pDrawList  = ImGui::GetWindowDrawList();
        const ImU32 frameColor = ImGui::ColorConvertFloat4ToU32( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "Border" ) ) );
        if ( CatalogCurve::sampleGradient( _listCurve, gradient, kSegmentCount + 1, _listGradientColor ) == false )
        {
            pDrawList->AddRect( origin, ImVec2{ origin.x + width, origin.y + height }, frameColor ); // 네 채널 중 키가 없는 것이 있다 - 띠 대신 빈 틀만 보인다
            return;
        }
        const auto toColor = [this]( uint32 index )
        {
            const float4& color = _listGradientColor[index];
            return ImGui::ColorConvertFloat4ToU32( ImVec4{ color._x, color._y, color._z, color._w } );
        };
        for ( uint32 segment = 0; segment < kSegmentCount; ++segment )
        {
            const float32 left  = origin.x + width * static_cast<float32>( segment ) / static_cast<float32>( kSegmentCount );
            const float32 right = origin.x + width * static_cast<float32>( segment + 1 ) / static_cast<float32>( kSegmentCount );
            const ImU32   from  = toColor( segment );
            const ImU32   to    = toColor( segment + 1 );
            pDrawList->AddRectFilledMultiColor( ImVec2{ left, origin.y }, ImVec2{ right, origin.y + height }, from, to, to, from );
        }
        pDrawList->AddRect( origin, ImVec2{ origin.x + width, origin.y + height }, frameColor );
    }

    void CatalogTableEditor::drawCurveBlock( const CatalogDocument& document, const CatalogCurve& curve )
    {
        ImGui::PushID( curve._elementIndex );
        ImGui::TextUnformatted( curve._label.c_str() );
        // 미리보기를 누르면 위 그래프가 이 곡선을 연다 — 키는 그래프에서 끌어 고친다(아래 칸은 숫자로 고치는 길).
        const FloatCurve preview = CatalogCurve::makeFloatCurve( curve );
        if ( preview._listKey.empty() == false )
        {
            if ( EditorCurveEditorState::drawPreview( "##plot", preview, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ).y ) )
            {
                _graphCurve              = curve._elementIndex; // 보기 상태 — 문서는 그래프를 놓을 때 바뀐다
                _graphState              = EditorCurveEditorState{};
                _graphState._working     = preview;
                _graphState._bFitPending = true;
            }
            EditorSelfTestMarks::record( "catalog.curves.preview" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdText", "CurvePreviewOpensGraphTip" ) );
        }
        else
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoKeysYet" ) );
        }
        for ( const CatalogCurveKey& key : curve._listKey )
        {
            ImGui::PushID( key._elementIndex );
            // 폭을 칸마다 넘긴다 — 남은 폭 전부를 쓰면 첫 칸이 줄을 다 차지해 값 · 보간 칸이 창 밖으로 밀렸다.
            const float32 fieldWidth = CatalogTableEditorInternal::kCurveKeyFieldWidth * EditorThemeUtil::getDPIScale();
            drawCell( document, key._elementIndex, "_time", false, fieldWidth );
            ImGui::SameLine();
            drawCell( document, key._elementIndex, "_value", false, fieldWidth );
            ImGui::SameLine();
            drawCell( document, key._elementIndex, "_interpolation", false, fieldWidth );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) )
                _pending = CatalogTableEdit{ CatalogTableEdit::Kind::RemoveRow, key._elementIndex, string(), string() };
            if ( key._bValid == false )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "(time or value is not a number)" );
            }
            ImGui::PopID();
        }
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "AddKey" ) ) )
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::AddCurveKey, curve._elementIndex, string(), string() };
        EditorSelfTestMarks::record( "catalog.curves.addkey" );
        ImGui::Separator();
        ImGui::PopID();
    }

    void CatalogTableEditor::drawCurveGraph()
    {
        // 그래프는 곡선 하나를 크게 연다(언리얼 Curve Editor · 유니티 Curve 창처럼 키를 끌어 고친다). 놓을 때 한 번 문서에 적는다 — 끌기 하나가 되돌리기 한 단계다.
        const CatalogCurve* pCurve = nullptr;
        for ( const CatalogCurve& curve : _listCurve )
        {
            if ( _graphCurve == curve._elementIndex )
                pCurve = &curve;
        }
        if ( pCurve == nullptr )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "PickACurvePreviewToEditItInTheGraph" ) );
            return;
        }
        ImGui::TextUnformatted( pCurve->_label.c_str() );
        // 끄는 중 · 칸 입력 중이 아니면 문서에서 다시 뜬다(되돌리기 · 아래 칸의 숫자 편집이 그래프에 보인다) — 시퀀서 곡선 편집기와 같은 규칙이다.
        if ( _graphState._drag == EditorCurveDrag::None && ImGui::IsAnyItemActive() == false )
            _graphState._working = CatalogCurve::makeFloatCurve( *pCurve );
        if ( ImGui::BeginChild( "##curveGraph", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, CatalogTableEditorInternal::kCurveGraphLines ) ), ImGuiChildFlags_Borders ) )
        {
            if ( EditorCurveEditorState::drawEditor( _graphState, "catalog.curves.graph" ) )
                _pending = CatalogTableEdit{ CatalogTableEdit::Kind::SetCurveKeys, pCurve->_elementIndex, string(), string(), CatalogChildPath{}, _graphState._working };
        }
        ImGui::EndChild();
    }

    void CatalogTableEditor::drawCurvePopup( const CatalogDocument& document )
    {
        constexpr const utf8* kPopupName = "Curves##catalogCurves";
        if ( _bOpenCurvePopup )
        {
            EditorInputGUI::openPopup( kPopupName );
            _bOpenCurvePopup = SW_FALSE;
        }
        EditorChrome::setNextWindowExtent( EditorTextExtent::ofText( 75.0f, 40.0f ), false ); // 그래프(12 줄) 아래에 곡선 목록이 보일 높이
        bool bOpen = true;
        if ( ImGui::BeginPopupModal( kPopupName, &bOpen ) == false )
            return;
        if ( _curveOwner.has_value() )
            CatalogCurve::collectCurves( document.getParsed(), *_curveOwner, _listCurve );
        else
            _listCurve.clear();
        CatalogCurve::collectGradients( _listCurve, _listGradient );
        if ( _listCurve.empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoCurvesUnderThisRow" ) );
        }
        else
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "EditAKeySTimeValueAndInterpolation" ) );
            for ( const CatalogGradient& gradient : _listGradient )
            {
                ImGui::TextUnformatted( gradient._label.c_str() );
                drawGradientBar( gradient );
            }
            drawCurveGraph();
            ImGui::Separator();
            ImGui::BeginChild( "##curveList", ImVec2{ 0.0f, -ImGui::GetFrameHeightWithSpacing() } );
            for ( const CatalogCurve& curve : _listCurve )
            {
                drawCurveBlock( document, curve );
            }
            ImGui::EndChild();
        }
        const bool bClose = ImGui::Button( SW_LOCKEY( "EdText", "Close" ) );
        EditorSelfTestMarks::record( "catalog.curves.close" );
        if ( bClose || bOpen == false )
        {
            _curveOwner.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
} // namespace sw::editor
