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

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CatalogTableEditorInternal
        {
            static constexpr float32 kColumnWidth        = 130.0f; ///< 96 DPI 기준입니다
            static constexpr float32 kActionColumnWidth  = 150.0f; ///< Duplicate · Remove 두 단추
            static constexpr float32 kCurveKeyFieldWidth = 110.0f; ///< 곡선 키 줄의 칸 폭(96 DPI)
            static constexpr float32 kChildColumnWidth   = 64.0f;
            static constexpr int32   kExtraColumnCount   = 2;  ///< 자식 수 · 행 동작
            static constexpr int32   kMaxVisibleRows     = 12; ///< 넘으면 테이블 안에서 세로로 스크롤한다

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
        , _curveOwner{ -1 }
        , _bOpenCurvePopup{ SW_FALSE }
        , _bEditFailed{ SW_FALSE }
        , _editText{}
        , _lastActionText{}
        , _editID{ 0 }
        , _pending{}
        , _rowFilter{}
    {
    }

    void CatalogTableEditor::reset()
    {
        _editText.clear();
        _editID          = 0;
        _pending         = CatalogTableEdit{};
        _curveOwner      = -1;
        _bOpenCurvePopup = SW_FALSE;
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

        // 언리얼 Data Table 의 행 검색처럼 칸 값으로 거른다.
        EditorWidgets::drawSearchField( "##catalogRowFilter", _rowFilter, SW_LOCKEY( "EdCommon", "SearchHint" ), -1.0f, false );

        bool bFirstCell        = true;
        bool bFirstCurveButton = true;
        for ( const CatalogDocument::Group& group : inoutDocument.getGroups() )
        {
            drawGroup( inoutDocument, group, &group == &inoutDocument.getGroups().front(), bFirstCell, bFirstCurveButton );
        }
        drawCurvePopup( inoutDocument );
        return applyPending( inoutDocument );
    }

    void CatalogTableEditor::drawGroup( const CatalogDocument& document, const CatalogDocument::Group& group, bool bFirstGroup, bool& inoutbFirstCell, bool& inoutbFirstCurveButton )
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
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEMECOLOR( "Signal", "Warning" ) ), "Two rows share the id '%s' - the loader refuses this", group._duplicateID.c_str() );

        const int32               columnCount = static_cast<int32>( group._listColumn.size() ) + CatalogTableEditorInternal::kExtraColumnCount;
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
        const ImVec2              outerSize{ 0.0f, CatalogTableEditorInternal::computeOuterHeight( _listVisibleRow.size() ) };
        if ( ImGui::BeginTable( "##CatalogRows", columnCount, kTableFlags, outerSize ) )
        {
            // 동작 칸은 맨 앞에 고정한다 - 맨 뒤면 칸이 많은 표에서 가로 스크롤 밖으로 밀려 행을 복사 · 삭제하지 못한다.
            ImGui::TableSetupColumn( "##actions", ImGuiTableColumnFlags_WidthFixed, CatalogTableEditorInternal::kActionColumnWidth * dpiScale );
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
                    drawRow( document, group, _listVisibleRow[static_cast<size_t>( rowPosition )], rowPosition, bFirstGroup, inoutbFirstCell, inoutbFirstCurveButton );
                }
            }
            ImGui::EndTable();
        }

        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "AddRow" ) ) )
        {
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::AddRow, -1, string(), group._name };
        }
        if ( bFirstGroup )
            EditorSelfTestMarks::note( "catalog.addrow.first" );
        ImGui::PopID();
    }

    void CatalogTableEditor::drawRow( const CatalogDocument& document, const CatalogDocument::Group& group, int32 elementIndex, int32 rowPosition, bool bFirstGroup, bool& inoutbFirstCell,
                                      bool& inoutbFirstCurveButton )
    {
        ImGui::PushID( elementIndex );
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex( 0 );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdLabel", "Duplicate" ) ) )
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::DuplicateRow, elementIndex, string(), string() };
        if ( rowPosition == 0 && bFirstGroup )
            EditorSelfTestMarks::note( "catalog.row.duplicate.first" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) )
            _pending = CatalogTableEdit{ CatalogTableEdit::Kind::RemoveRow, elementIndex, string(), string() };
        if ( rowPosition == 0 && bFirstGroup )
            EditorSelfTestMarks::note( "catalog.row.remove.first" );

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
        const bool bHovered = element._listChild.empty() == false && ImGui::IsItemHovered();
        if ( element._listChild.empty() == false )
        {
            CatalogCurveModel::collectCurves( document.getParsed(), elementIndex, _listProbeCurve );
            if ( _listProbeCurve.empty() == false )
            {
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Curves" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Curves" ) ) )
                {
                    _curveOwner      = elementIndex;
                    _bOpenCurvePopup = SW_TRUE;
                }
                if ( inoutbFirstCurveButton )
                    EditorSelfTestMarks::note( "catalog.curves.open" );
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
        const string* pValue   = document.findCellValue( elementIndex, column );
        const ImGuiID cellID   = ImGui::GetID( "##cell" );
        const bool    bEditing = _editID == cellID;
        string        display  = bEditing ? _editText : ( pValue != nullptr ? *pValue : string() );
        if ( pValue == nullptr && bEditing == false )
            ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.12f } ); // 속성이 없는 칸은 어둡게 — 비어 있는 값과 다르다
        const bool bChanged = EditorWidgets::drawTextField( "##cell", display, width );
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
                _pending = CatalogTableEdit{ CatalogTableEdit::Kind::SetCell, elementIndex, column, _editText };
            _editID = 0;
        }
        else if ( ImGui::IsItemDeactivated() && bEditing )
        {
            _editID = 0;
        }

        if ( pValue != nullptr && EditorInputGUI::beginItemContextMenu( "##cellMenu" ) )
        {
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "UnsetAttribute" ) ) )
                _pending = CatalogTableEdit{ CatalogTableEdit::Kind::UnsetCell, elementIndex, column, string() };
            ImGui::EndPopup();
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
        const ImU32 frameColor = ImGui::ColorConvertFloat4ToU32( EditorColorUtil::toImVec4( SW_THEMECOLOR( "Base", "Border" ) ) );
        if ( CatalogCurveModel::sampleGradient( _listCurve, gradient, kSegmentCount + 1, _listGradientColor ) == false )
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
        if ( CatalogCurveModel::sample( curve, CatalogCurveModel::computeEndTime( curve ), 48, _listSample ) )
            ImGui::PlotLines( "##plot", _listSample.data(), static_cast<int32>( _listSample.size() ), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2{ ImGui::GetContentRegionAvail().x, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ).y } );
        else
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoKeysYet" ) );
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
        EditorSelfTestMarks::note( "catalog.curves.addkey" );
        ImGui::Separator();
        ImGui::PopID();
    }

    void CatalogTableEditor::drawCurvePopup( const CatalogDocument& document )
    {
        constexpr const utf8* kPopupName = "Curves##catalogCurves";
        if ( _bOpenCurvePopup )
        {
            EditorInputGUI::openPopup( kPopupName );
            _bOpenCurvePopup = SW_FALSE;
        }
        EditorChrome::setNextWindowExtent( EditorTextExtent::ofText( 75.0f, 27.0f ), false );
        bool bOpen = true;
        if ( ImGui::BeginPopupModal( kPopupName, &bOpen ) == false )
            return;
        CatalogCurveModel::collectCurves( document.getParsed(), _curveOwner, _listCurve );
        CatalogCurveModel::collectGradients( _listCurve, _listGradient );
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
            ImGui::Separator();
            ImGui::BeginChild( "##curveList", ImVec2{ 0.0f, -ImGui::GetFrameHeightWithSpacing() } );
            for ( const CatalogCurve& curve : _listCurve )
            {
                drawCurveBlock( document, curve );
            }
            ImGui::EndChild();
        }
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Close" ) ) || bOpen == false )
        {
            _curveOwner = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
} // namespace sw::editor
