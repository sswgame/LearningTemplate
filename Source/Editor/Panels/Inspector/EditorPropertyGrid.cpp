#include "pch.h"

#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorCompactControls.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionMetadata.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorPropertyGridInternal
        {
            static constexpr Color kNestedHeader{ 0.15f, 0.15f, 0.15f, 1.0f };

            /** @brief 자체 테스트 표식 하나(`<머리>.<꼬리>`)를 방금 그린 항목으로 남깁니다. 테스트가 꺼져 있으면 글을 짓지 않는다. */
            static void recordMark( const utf8* pHead, const string& tail )
            {
                if ( EditorSelfTestMarks::isEnabled() == false || tail.empty() )
                    return;
                fixed_string<constant::buffer::kMax128> mark;
                formatstring( mark.data(), mark.capacity(), "%#.%#", pHead, tail.c_str() );
                EditorSelfTestMarks::record( mark.c_str() );
            }

            /** @brief 화면 밖 행의 자체 테스트 표식을 그 자리 사각형으로 남깁니다 — 시나리오가 요청하면 그 창이 스크롤해 행이 보이게 되고, 다음 프레임에 값이 채워진다. */
            static void recordSkippedMark( const utf8* pHead, const string& tail, float32 minX, float32 minY, float32 maxX, float32 maxY )
            {
                fixed_string<constant::buffer::kMax128> mark;
                formatstring( mark.data(), mark.capacity(), "%#.%#", pHead, tail.c_str() );
                EditorSelfTestMarks::recordRect( mark.c_str(), float2{ minX, minY }, float2{ maxX, maxY } );
            }

            /** @brief 비트 플래그 열거형을 콤보로 그립니다. 전체 선택 · 해제 · 항목 체크는 @p inoutEditedValue 만 바꿉니다. */
            static void drawBitFlagEnum( const PropertyValueRow& row, int64& inoutEditedValue )
            {
                const utf8* pLabel = "##value";
                if ( row._bReadOnly != SW_FALSE )
                {
                    ImGui::TextDisabled( "%s", pLabel );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( row._valueText.c_str() );
                    return;
                }

                if ( ImGui::BeginCombo( pLabel, row._valueText.c_str() ) == false )
                    return;
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "SelectAll" ) ) )
                {
                    for ( const PropertyEnumChoice& choice : row._listChoice )
                    {
                        inoutEditedValue |= choice._value;
                    }
                }
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ClearAll" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "ClearAll" ) ) )
                    inoutEditedValue = 0;
                ImGui::Separator();

                for ( const PropertyEnumChoice& choice : row._listChoice )
                {
                    if ( choice._value == 0 )
                        continue;
                    bool bChecked = ( ( inoutEditedValue & choice._value ) == choice._value );
                    if ( ImGui::Checkbox( choice._name.c_str(), &bChecked ) )
                    {
                        if ( bChecked )
                            inoutEditedValue |= choice._value;
                        else
                            inoutEditedValue &= ~choice._value;
                    }
                }
                ImGui::EndCombo();
            }

            /** @brief 하나만 고르는 열거형을 콤보로 그립니다. 고른 값은 @p outEditedValue 에 둡니다. */
            static void drawPlainEnum( const PropertyValueRow& row, int64& outEditedValue )
            {
                const utf8* pLabel = "##value";
                if ( row._bReadOnly != SW_FALSE )
                {
                    ImGui::TextDisabled( "%s", pLabel );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( row._valueText.c_str() );
                    return;
                }

                if ( ImGui::BeginCombo( pLabel, row._valueText.c_str() ) == false )
                    return;
                for ( const PropertyEnumChoice& choice : row._listChoice )
                {
                    const bool bSelected = ( choice._value == row._enumValue );
                    if ( ImGui::Selectable( choice._name.c_str(), bSelected ) )
                        outEditedValue = choice._value;
                    if ( EditorSelfTestMarks::isEnabled() )
                    {
                        // 시나리오가 콤보 항목을 누를 수 있게 한다(`inspector.enum.<프로퍼티>.<열거자>`) — 콤보를 연 프레임에만 그려진다.
                        fixed_string<constant::buffer::kMax128> itemMark;
                        formatstring( itemMark.data(), itemMark.capacity(), "inspector.enum.%#.%#", row._propName.c_str(), choice._name.c_str() );
                        EditorSelfTestMarks::record( itemMark.c_str() );
                    }
                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorPropertyGrid::EditorPropertyGrid()
        : _propertyFilter{}
        , _mapMethodArgSlot{}
        , _mapContainerAddText{}
        , _listMethodArgChoice{}
        , _model{}
        , _viewState{}
        , _listOpenScope{}
        , _pendingEditText{}
        , _pendingEdit{ PendingRowEdit::None }
        , _pendingEditRowID{ 0 }
        , _pendingEditIndex{ 0 }
        , _measureRowID{ constant::invalid_index::kUint32 }
        , _measureStartY{ 0.0f }
        , _openAllRequest{ OpenAllRequest::None }
        , _bFavoritesOnly{ SW_FALSE }
        , _bShowAllMethods{ SW_FALSE }
        , _bTableOpen{ SW_FALSE }
        , _bShowModifiedOnly{ false }
        , _countFrame{ -1 }
        , _methodRowCount{ 0 }
        , _hiddenMethodCount{ 0 }
        , _filledRowCount{ 0 }
        , _skippedRowCount{ 0 }
    {
    }

    void EditorPropertyGrid::drawSearchBarWithOptions( const utf8* pFieldTooltip )
    {
        const float32 buttonWidth = ImGui::GetFrameHeight();
        const float32 spacing     = ImGui::GetStyle().ItemSpacing.x;
        // drawSearchField 는 width 뒤에 지우기 단추(22 * dpi)를 더 그린다 — 둘을 빼야 보기 옵션 단추가 패널 오른쪽 밖으로 밀려나지 않는다.
        const float32 clearWidth = 22.0f * EditorThemeUtil::getDPIScale();
        const float32 fieldWidth = MathUtil::max( ImGui::GetContentRegionAvail().x - buttonWidth - clearWidth - spacing * 2.0f, 1.0f );
        EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ), fieldWidth );
        if ( pFieldTooltip != nullptr )
            EditorWidgets::drawTooltip( pFieldTooltip );
        ImGui::SameLine();
        // 거르는 중이면 단추가 켜진 바탕으로 그려진다 — 꺼진 줄 모르고 프로퍼티가 사라졌다고 여기지 않게 한다(예전 아이콘 토글은 켜도 모양이 같았다).
        if ( EditorCompactControls::beginViewOptions( "##InspectorViewOptions", _bShowModifiedOnly, SW_LOCKEY( "EditorInspector", "ViewOptions" ), "inspector.viewOptions" ) )
        {
            ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ShowOnlyModifiedProperties" ), nullptr, &_bShowModifiedOnly );
            EditorSelfTestMarks::record( "inspector.viewOptions.modifiedOnly" );
            ImGui::Separator();
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ExpandAll" ) ) )
                _openAllRequest = OpenAllRequest::ExpandAll;
            EditorSelfTestMarks::record( "inspector.viewOptions.expandAll" );
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CollapseAll" ) ) )
                _openAllRequest = OpenAllRequest::CollapseAll;
            EditorSelfTestMarks::record( "inspector.viewOptions.collapseAll" );
            ImGui::Separator();
            // 진단 보기 — CallInEditor 가 없는 FUNCTION 도 호출할 수 있게 한다. 실행 안전성을 선언하지 않은 함수라 기본 목록에서 뺀다.
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ShowAllFunctions" ), nullptr, _bShowAllMethods == SW_TRUE ) )
                _bShowAllMethods = _bShowAllMethods == SW_TRUE ? SW_FALSE : SW_TRUE;
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ShowAllFunctionsTooltip" ) );
            EditorSelfTestMarks::record( "inspector.viewOptions.allFunctions" );
            EditorCompactControls::endViewOptions();
        }
    }

    void EditorPropertyGrid::drawSearchBar( const utf8* pMarkKey )
    {
        if ( pMarkKey != nullptr )
        {
            EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ), 0.0f, false, pMarkKey );
            return;
        }
        EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ) );
    }

    void EditorPropertyGrid::drawProperties( const EditorPropertyGridTarget& target, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName )
    {
        // 씬 대상은 이 프레임에 id 로 다시 찾는다 — 사라졌으면 그리지 않는다.
        if ( _model.beginTarget( target ) )
        {
            drawTargetProperties( pSectionTitle, listDrawnName );
            _model.endTarget();
        }
        _bFavoritesOnly = SW_FALSE;
        // 편집 알림(다중 선택의 나머지에 값 적용)이 끝난 뒤에 되돌리기를 남긴다.
        InspectorPropertyUndo::commitFinishedEdits();
    }

    void EditorPropertyGrid::drawFavoriteProperties( const EditorPropertyGridTarget& target )
    {
        // 즐겨찾기가 없는 타입은 모델이 행을 내지 않아 아무것도 그리지 않는다.
        if ( _model.hasFavorites() == false )
            return;
        // 본문 그리드와 같은 카테고리 · 프로퍼티 id 를 쓰므로 id 공간을 나눈다.
        ImGui::PushID( "##favorites" );
        _bFavoritesOnly = SW_TRUE;
        drawProperties( target, nullptr, {} );
        ImGui::PopID();
    }

    void EditorPropertyGrid::drawMethodsAndEvents( const EditorPropertyGridTarget& target, bool bEvents )
    {
        if ( _model.beginTarget( target ) == false )
            return;
        drawTypeMethods( _model.getType() );
        if ( bEvents )
            drawTypeEvents( _model.getType() );
        _model.endTarget();
    }

    void EditorPropertyGrid::resetFrameCounts()
    {
        const int32 frame = ImGui::GetFrameCount();
        if ( frame == _countFrame )
            return;
        _countFrame        = frame;
        _methodRowCount    = 0;
        _hiddenMethodCount = 0;
        _filledRowCount    = 0;
        _skippedRowCount   = 0;
    }

    PropertyGridRowWindow EditorPropertyGrid::makeRowWindow()
    {
        // 보이는 영역은 그리는 창의 자르기 사각형이다(스크롤 · 도킹 · 팝업 모두 같다). 잰 적 없는 행은 한 줄 위젯 높이로 셈한다.
        const ImGuiWindow*    pWindow = ImGui::GetCurrentWindowRead();
        const ImGuiStyle&     style   = ImGui::GetStyle();
        PropertyGridRowWindow window{};
        window._originY          = ImGui::GetCursorScreenPos().y;
        window._clipMinY         = pWindow->ClipRect.Min.y;
        window._clipMaxY         = pWindow->ClipRect.Max.y;
        window._rowHeight        = ImGui::GetFrameHeight() + style.CellPadding.y * 2.0f;
        window._headerHeight     = ImGui::GetFrameHeight() + style.ItemSpacing.y;
        window._bSkipOffscreen   = SW_TRUE;
        window._bNameSkippedRows = EditorSelfTestMarks::isEnabled() ? SW_TRUE : SW_FALSE;
        return window;
    }

    void EditorPropertyGrid::drawTargetProperties( const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName )
    {
        resetFrameCounts();
        PropertyGridGatherParams params{};
        params._window         = makeRowWindow();
        params._filterText     = _propertyFilter.c_str();
        params._pListDrawnName = &listDrawnName;
        params._openAllRequest = _openAllRequest;
        params._bFavoritesOnly = _bFavoritesOnly;
        params._bModifiedOnly  = _bShowModifiedOnly ? SW_TRUE : SW_FALSE;
        _bFavoritesOnly        = SW_FALSE; // 이번 한 번만이다
        _model.gather( params, _viewState );
        _filledRowCount += _viewState._filledCount;

        if ( _viewState._bHasRows == SW_FALSE )
        {
            // 검색어가 아무 프로퍼티도 맞히지 못하면 그렇다고 알려 준다.
            if ( _viewState._bNoMatch != SW_FALSE )
                EditorWidgets::drawNoSearchResultHint( string_view{ _propertyFilter.c_str() } );
            return;
        }
        if ( pSectionTitle != nullptr )
            ImGui::SeparatorText( pSectionTitle );
        drawRows();
        applyPendingRowEdit();
    }

    void EditorPropertyGrid::drawRows()
    {
        _listOpenScope.clear();
        _bTableOpen        = SW_FALSE;
        _measureRowID      = constant::invalid_index::kUint32;
        bool   bInCategory = false; // 펴진 카테고리의 표가 열려 있다
        uint32 index       = 0;
        while ( index < _viewState._rowCount )
        {
            const PropertyValueRow& row = _viewState._listRow[index];
            if ( row._kind == PropertyRowKind::Category )
            {
                while ( _listOpenScope.empty() == false )
                    closeRowScope();
                if ( _bTableOpen )
                    endTable();
                bInCategory = drawCategoryRow( row );
                ++index;
                continue;
            }
            // 이번 프레임에 접은 카테고리 · 노드의 행은 모델이 아직 펴진 것으로 냈다 — 다음 gather 부터 빠진다.
            if ( bInCategory == false )
            {
                ++index;
                continue;
            }
            while ( _listOpenScope.empty() == false && _listOpenScope.back()._depth >= row._depth )
                closeRowScope();
            if ( row._depth > 0 && ( _listOpenScope.empty() || _listOpenScope.back()._bTreeOpen == 0 ) )
            {
                ++index;
                continue;
            }
            if ( row._kind == PropertyRowKind::Skipped )
            {
                index = drawSkippedRows( index );
                continue;
            }
            if ( row._depth == 0 )
            {
                beginTableRow( 0.0f );
                _measureRowID  = row._rowID;
                _measureStartY = ImGui::GetCursorScreenPos().y;
                drawRowNameColumn( row );
                ImGui::TableNextColumn();
            }
            openRowScope( row );
            ++index;
        }
        while ( _listOpenScope.empty() == false )
            closeRowScope();
        if ( _bTableOpen )
            endTable();
    }

    bool EditorPropertyGrid::drawCategoryRow( const PropertyValueRow& row )
    {
        // 펼침 상태는 모델이 든다(창이 `SetNextItemOpen` 으로 넘기고 머리의 결과를 돌려준다 — Unreal Outliner `ExpansionState`).
        const float32 startY    = ImGui::GetCursorScreenPos().y;
        const bool    bExpanded = row._bExpanded != SW_FALSE;
        ImGui::SetNextItemOpen( bExpanded );
        const bool bOpen = ImGui::CollapsingHeader( row._label.c_str(), ImGuiTreeNodeFlags_DefaultOpen );
        if ( bOpen != bExpanded )
            _model.setRowExpanded( row._rowID, bOpen );
        if ( bOpen == false || ImGui::BeginTable( row._label.c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable ) == false ) // 줄무늬 바탕은 두지 않는다(앱 화면 규칙 — 행은 간격으로 가른다)
        {
            _model.setRowHeight( row._rowID, ImGui::GetCursorScreenPos().y - startY );
            return false;
        }
        ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 150.0f * EditorThemeUtil::getDPIScale() );
        ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
        _bTableOpen = SW_TRUE;
        _model.setRowHeight( row._rowID, ImGui::GetCursorScreenPos().y - startY );
        return true;
    }

    void EditorPropertyGrid::beginTableRow( float32 minHeight )
    {
        ImGui::TableNextRow( ImGuiTableRowFlags_None, minHeight );
        ImGui::TableNextColumn();
        // 앞 깊이 0 행의 높이는 행 시작에서 다음 행 시작까지다(자식 행 · 칸 여백 포함). 모델이 다음 gather 에 화면 밖 판정으로 쓴다.
        if ( _measureRowID != constant::invalid_index::kUint32 )
            _model.setRowHeight( _measureRowID, ImGui::GetCursorScreenPos().y - _measureStartY );
        _measureRowID = constant::invalid_index::kUint32;
    }

    void EditorPropertyGrid::endTable()
    {
        ImGui::EndTable();
        if ( _measureRowID != constant::invalid_index::kUint32 )
            _model.setRowHeight( _measureRowID, ImGui::GetItemRectMax().y - _measureStartY + ImGui::GetStyle().CellPadding.y );
        _measureRowID = constant::invalid_index::kUint32;
        _bTableOpen   = SW_FALSE;
    }

    uint32 EditorPropertyGrid::drawSkippedRows( uint32 firstIndex )
    {
        // 화면 밖 행은 높이만 차지한다 — 이어진 묶음을 표 행 하나로 두어 스크롤 길이와 다음 행 자리를 지킨다(`HierarchyPanel` 의 묶음과 같다).
        float32 totalHeight = 0.0f;
        uint32  endIndex    = firstIndex;
        while ( endIndex < _viewState._rowCount && _viewState._listRow[endIndex]._kind == PropertyRowKind::Skipped )
        {
            totalHeight += _viewState._listRow[endIndex]._height;
            ++endIndex;
        }
        beginTableRow( totalHeight );
        _skippedRowCount += endIndex - firstIndex;
        if ( EditorSelfTestMarks::isEnabled() )
        {
            // 시나리오가 화면 밖 행의 표식을 찾으면 그 자리로 스크롤해 온다 — 이름 칸 · 값 칸 자리를 행마다 남긴다.
            const float32 nameMinX = ImGui::GetCursorScreenPos().x;
            const float32 nameMaxX = nameMinX + ImGui::GetContentRegionAvail().x;
            float32       rowY     = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().CellPadding.y;
            ImGui::TableSetColumnIndex( 1 );
            const float32 valueMinX = ImGui::GetCursorScreenPos().x;
            const float32 valueMaxX = valueMinX + ImGui::GetContentRegionAvail().x;
            for ( uint32 index = firstIndex; index < endIndex; ++index )
            {
                const PropertyValueRow& skipped = _viewState._listRow[index];
                if ( skipped._markName.empty() == false )
                {
                    EditorPropertyGridInternal::recordSkippedMark( "inspector.label", skipped._markName, nameMinX, rowY, nameMaxX, rowY + skipped._height );
                    EditorPropertyGridInternal::recordSkippedMark( "inspector.property", skipped._markName, valueMinX, rowY, valueMaxX, rowY + skipped._height );
                }
                rowY += skipped._height;
            }
        }
        return endIndex;
    }

    void EditorPropertyGrid::drawRowNameColumn( const PropertyValueRow& row )
    {
        ImGui::AlignTextToFramePadding();
        if ( row._bMixed != SW_FALSE )
        {
            ImGui::TextDisabled( "\xe2\x80\x94" ); // — (혼합)
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "MultipleValues" ) );
            ImGui::SameLine();
        }
        if ( row._bFavorite != SW_FALSE )
        {
            ImGui::TextUnformatted( SW_EDITOR_ICON( "Star" ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "FavoriteProperty" ) );
            ImGui::SameLine();
        }
        ImGui::TextUnformatted( row._label.c_str() );
        EditorWidgets::drawTooltip( row._tooltip.c_str() );
        // 시나리오가 행 메뉴를 오른쪽 클릭으로 열 수 있게 이름 글에 레이블을 남긴다(`inspector.label.<타입>.<프로퍼티>`).
        EditorPropertyGridInternal::recordMark( "inspector.label", row._markName );

        drawPropertyRowMenu( row );

        // 기본값과 다르면 이름 영역 끝에 되돌리기 버튼(언리얼의 노란 화살표)을 둔다. 기본값은 컴포넌트의 기본 인스턴스(CDO)이고, 없으면 메타 텍스트다.
        if ( row._bModified != SW_FALSE )
        {
            if ( EditorCompactControls::drawResetButton( "##reset", SW_LOCKEY( "EditorInspector", "ResetToDefaultTooltip" ) ) )
            {
                // 값을 통째로 바꾼다(컨테이너면 원소 자리도) — 이 행의 자식을 다 그린 뒤에 부른다.
                _pendingEdit      = PendingRowEdit::ResetToDefault;
                _pendingEditRowID = row._rowID;
            }
            EditorPropertyGridInternal::recordMark( "inspector.reset", row._markName );
        }

        if ( row._bTransient != SW_FALSE )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(T)" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "TransientProperty" ) );
        }
    }

    void EditorPropertyGrid::drawPropertyRowMenu( const PropertyValueRow& row )
    {
        if ( EditorInputGUI::beginItemContextMenu( "PropCtx" ) == false )
            return;

        // 언리얼 Details 의 행 메뉴 Add to Favorites · Remove from Favorites 에 해당한다. 바꾸면 바로 저장한다.
        if ( row._bFavoriteTarget != SW_FALSE )
        {
            if ( ImGui::MenuItem( row._bFavorite != SW_FALSE ? SW_LOCKEY( "EditorInspector", "RemoveFromFavorites" ) : SW_LOCKEY( "EditorInspector", "AddToFavorites" ) ) )
                _model.toggleFavorite( row._favoriteKey );
            ImGui::Separator();
        }
        // 기본값 · 붙여넣기는 값을 통째로 바꾼다(컨테이너면 원소 자리도) — 이 행의 자식을 다 그린 뒤에 부른다.
        if ( row._bCanReset != SW_FALSE && ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ResetToDefault" ) ) )
        {
            _pendingEdit      = PendingRowEdit::ResetToDefault;
            _pendingEditRowID = row._rowID;
        }
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CopyValue" ) ) )
            ImGui::SetClipboardText( _model.formatPropertyText( row._rowID ).c_str() );
        // 타입 핸들은 선택기만이 입력 길이다. 글로 붙이면 대상 타입을 확인하지 않은 값이 들어간다.
        const utf8* pClipboard    = ImGui::GetClipboardText();
        const bool  bPasteAllowed = pClipboard != nullptr && row._bPasteAllowed != SW_FALSE;
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "PasteValue" ), nullptr, false, bPasteAllowed ) )
        {
            _pendingEdit      = PendingRowEdit::PasteText;
            _pendingEditRowID = row._rowID;
            _pendingEditText  = pClipboard;
        }
        EditorSelfTestMarks::record( "inspector.menu.pasteValue" );
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CopyPropertyName" ) ) )
            ImGui::SetClipboardText( row._propName.c_str() );
        ImGui::EndPopup();
    }

    void EditorPropertyGrid::openRowScope( const PropertyValueRow& row )
    {
        // id 는 예전 재귀 그리기와 같다 — 깊이 0 · 구조체 필드는 프로퍼티 이름, 원소 · 맵 항목은 자리(위젯 id 가 되돌리기 키다).
        if ( row._child == PropertyRowChild::None || row._child == PropertyRowChild::Field )
            ImGui::PushID( row._propName.c_str() );
        else
            ImGui::PushID( static_cast<int32>( row._ordinal ) );

        if ( row._kind == PropertyRowKind::KeyedElement )
        {
            drawKeyedElementRow( row );
            ImGui::BeginDisabled( false ); // 닫기가 막기 끝을 짝으로 부른다
            _listOpenScope.push_back( OpenRowScope{ row._rowID, row._depth, 0 } );
            return;
        }
        switch ( row._child )
        {
            case PropertyRowChild::Field:
            {
                ImGui::AlignTextToFramePadding();
                ImGui::BulletText( "%s", row._label.c_str() );
                ImGui::SameLine();
                break;
            }
            case PropertyRowChild::Element:
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted( row._label.c_str() );
                ImGui::SameLine();
                break;
            }
            case PropertyRowChild::MapEntry:
            {
                // 지우기는 행을 다 그린 뒤에 한다(뒤의 항목 행이 옛 자리를 그리지 않게).
                if ( row._bRemovable != SW_FALSE )
                {
                    if ( ImGui::SmallButton( "x" ) )
                    {
                        _pendingEdit      = PendingRowEdit::RemoveMapEntry;
                        _pendingEditRowID = row._parentRowID;
                        _pendingEditIndex = row._ordinal;
                    }
                    ImGui::SameLine();
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted( row._label.c_str() );
                ImGui::SameLine();
                break;
            }
            case PropertyRowChild::None:
                break;
        }
        ImGui::SetNextItemWidth( -FLT_MIN );
        ImGui::BeginDisabled( row._bDisabled != SW_FALSE );
        const bool bTreeOpen = drawRowWidget( row );
        _listOpenScope.push_back( OpenRowScope{ row._rowID, row._depth, static_cast<uint8>( bTreeOpen ? 1 : 0 ) } );
    }

    void EditorPropertyGrid::closeRowScope()
    {
        const OpenRowScope scope = _listOpenScope.back();
        _listOpenScope.pop_back();
        const PropertyValueRow& row = _viewState._listRow[scope._rowID];
        if ( scope._bTreeOpen != 0 )
        {
            // 맵 · `set` 의 끝에 "추가" 입력란을 둔다. Enter 로 받은 글은 행을 다 그린 뒤에 넣는다(같은 키가 있으면 모델이 넣지 않는다).
            const bool bAddRow = row._bCanAddRemove != SW_FALSE && ( row._kind == PropertyRowKind::Map || row._kind == PropertyRowKind::KeyedSequence );
            string     addText;
            if ( bAddRow && drawContainerAddRow( row._kind == PropertyRowKind::Map ? SW_LOCKEY( "EditorInspector", "NewKeyHint" ) : SW_LOCKEY( "EditorInspector", "NewElementHint" ), addText ) )
            {
                _pendingEdit      = row._kind == PropertyRowKind::Map ? PendingRowEdit::AddMapEntry : PendingRowEdit::AddKeyedElement;
                _pendingEditRowID = row._rowID;
                _pendingEditText  = std::move( addText );
            }
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
        // 시나리오가 누를 수 있게 값 입력란에 이름을 남긴다(`inspector.property.<타입>.<프로퍼티>`) — 펴진 노드는 예전처럼 마지막으로 그린 자식 위젯이다.
        if ( row._depth == 0 )
            EditorPropertyGridInternal::recordMark( "inspector.property", row._markName );
        ImGui::PopID();
    }

    bool EditorPropertyGrid::drawRowWidget( const PropertyValueRow& row )
    {
        // 그리기 확장(내장 값 위젯 포함)이 먼저 그린다. 맡지 않으면 행 종류대로 그린다.
        IInspectorProperty* pDrawer = PropertyGridModel::getPropertyManager().find( row._typeName.c_str() );
        if ( pDrawer != nullptr && pDrawer->draw( row, _model ) )
            return false;
        switch ( row._kind )
        {
            case PropertyRowKind::Enum:
            case PropertyRowKind::BitFlagEnum:
            {
                drawEnumRow( row );
                return false;
            }
            case PropertyRowKind::Sequence:
            case PropertyRowKind::KeyedSequence:
            case PropertyRowKind::Map:
            case PropertyRowKind::Struct:
                return drawNodeHeader( row );
            default:
            {
                // 그리지 못하는 값도 글을 남긴다 — 빈 칸이면 값이 비었는지, 그리지 못하는 것인지 화면만 보고는 구분할 수 없다.
                ImGui::TextDisabled( "%s", row._valueText.c_str() );
                return false;
            }
        }
    }

    bool EditorPropertyGrid::drawNodeHeader( const PropertyValueRow& row )
    {
        const bool bStruct   = row._kind == PropertyRowKind::Struct;
        const bool bExpanded = row._bExpanded != SW_FALSE;
        if ( bStruct )
            ImGui::PushStyleColor( ImGuiCol_Header, EditorColorUtil::toImVec4( EditorPropertyGridInternal::kNestedHeader ) );
        ImGui::SetNextItemOpen( bExpanded );
        const bool bOpen = ImGui::TreeNodeEx( "##value", ImGuiTreeNodeFlags_SpanFullWidth, "%s", row._valueText.c_str() );
        if ( bStruct )
            ImGui::PopStyleColor();
        // 펼침은 모델이 든다 — 펼친 다음 gather 가 자식 행을 낸다.
        if ( bOpen != bExpanded )
            _model.setRowExpanded( row._rowID, bOpen );
        // 고정 배열은 원소 수가 정해져 있어 모델이 단추를 두지 않게 한다(`_bCanAddRemove`). 누름은 행을 다 그린 뒤에 부른다.
        if ( bOpen && row._kind == PropertyRowKind::Sequence && row._bCanAddRemove != SW_FALSE )
        {
            if ( ImGui::SmallButton( "+ Add" ) )
            {
                _pendingEdit      = PendingRowEdit::AddElement;
                _pendingEditRowID = row._rowID;
            }
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) )
            {
                _pendingEdit      = PendingRowEdit::ClearElements;
                _pendingEditRowID = row._rowID;
            }
            ImGui::Separator();
        }
        return bOpen;
    }

    void EditorPropertyGrid::drawEnumRow( const PropertyValueRow& row )
    {
        int64 editedValue = row._enumValue;
        if ( row._kind == PropertyRowKind::BitFlagEnum )
            EditorPropertyGridInternal::drawBitFlagEnum( row, editedValue );
        else
            EditorPropertyGridInternal::drawPlainEnum( row, editedValue );
        if ( row._bReadOnly == SW_FALSE && editedValue != row._enumValue )
            _model.setEnumValue( row._rowID, editedValue );
    }

    void EditorPropertyGrid::drawKeyedElementRow( const PropertyValueRow& row )
    {
        if ( row._bRemovable != SW_FALSE )
        {
            if ( ImGui::SmallButton( "x" ) )
            {
                _pendingEdit      = PendingRowEdit::RemoveKeyedElement;
                _pendingEditRowID = row._parentRowID;
                _pendingEditIndex = row._ordinal;
            }
            ImGui::SameLine();
        }
        // 원소는 텍스트 입력란이다. Enter 로 확정한 텍스트를 원소 타입으로 읽어 지우고 다시 넣는다. 원소가 곧 정렬 키이므로 제자리에 쓰면 트리가 깨진다.
        fixed_string<constant::buffer::kMax256> elementText{ row._valueText.c_str() };
        const bool                              bEditable = row._bReadOnly == SW_FALSE;
        ImGui::SetNextItemWidth( -FLT_MIN );
        if ( ImGui::InputText( "##element", elementText.data(), elementText.capacity(), bEditable ? ImGuiInputTextFlags_EnterReturnsTrue : ImGuiInputTextFlags_ReadOnly ) && bEditable )
        {
            _pendingEdit      = PendingRowEdit::ReplaceKeyedElement;
            _pendingEditRowID = row._parentRowID;
            _pendingEditIndex = row._ordinal;
            _pendingEditText  = elementText.c_str();
        }
    }

    bool EditorPropertyGrid::drawContainerAddRow( const utf8* pHint, string& outText )
    {
        const ImGuiID                            rowID = ImGui::GetID( "##add" );
        fixed_string<constant::buffer::kMax256>& text  = _mapContainerAddText[static_cast<uint32>( rowID )];
        ImGui::SetNextItemWidth( -FLT_MIN );
        const bool bEntered = ImGui::InputTextWithHint( "##add", pHint, text.data(), text.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        if ( bEntered == false || text.empty() )
            return false;
        outText = text.c_str();
        text.clear();
        return true;
    }

    void EditorPropertyGrid::applyPendingRowEdit()
    {
        const PendingRowEdit edit = _pendingEdit;
        _pendingEdit              = PendingRowEdit::None;
        switch ( edit )
        {
            case PendingRowEdit::None:
                break;
            case PendingRowEdit::PasteText:
                _model.setPropertyText( _pendingEditRowID, _pendingEditText, "Paste Value" );
                break;
            case PendingRowEdit::ResetToDefault:
                _model.resetToDefault( _pendingEditRowID );
                break;
            case PendingRowEdit::AddElement:
                _model.addElement( _pendingEditRowID );
                break;
            case PendingRowEdit::ClearElements:
                _model.clearElements( _pendingEditRowID );
                break;
            case PendingRowEdit::RemoveMapEntry:
                _model.removeMapEntry( _pendingEditRowID, _pendingEditIndex );
                break;
            case PendingRowEdit::AddMapEntry:
                _model.addMapEntry( _pendingEditRowID, _pendingEditText ); // 같은 키(텍스트가 같다)가 이미 있으면 값을 덮어쓰지 않는다
                break;
            case PendingRowEdit::ReplaceKeyedElement:
                _model.replaceKeyedElement( _pendingEditRowID, _pendingEditIndex, _pendingEditText );
                break;
            case PendingRowEdit::RemoveKeyedElement:
                _model.removeKeyedElement( _pendingEditRowID, _pendingEditIndex );
                break;
            case PendingRowEdit::AddKeyedElement:
                _model.addKeyedElement( _pendingEditRowID, _pendingEditText );
                break;
        }
        _pendingEditText.clear();
    }

    void EditorPropertyGrid::drawTypeMethods( const TypeInfo* pTypeInfo )
    {
        resetFrameCounts();

        // 그릴 메서드: 생성자는 빼고, 진단 보기가 아니면 CallInEditor 만. 숨긴 것이 있으면 빈 칸 글이 그렇다고 말한다("메서드 없음" 으로 읽히지 않게).
        uint32 drawableCount = 0;
        uint32 hiddenCount   = 0;
        if ( pTypeInfo != nullptr )
        {
            for ( const FunctionInfo& method : pTypeInfo->_listMethod )
            {
                if ( method._metadata->_bConstructor != SW_FALSE )
                    continue;
                if ( isMethodListed( method ) )
                    ++drawableCount;
                else
                    ++hiddenCount;
            }
        }
        _hiddenMethodCount += hiddenCount;

        if ( drawableCount == 0 )
        {
            EditorWidgets::drawEmptyHint( hiddenCount == 0 ? SW_LOCKEY( "EditorInspector", "NoFunctionMethods" ) : SW_LOCKEY( "EditorInspector", "NoCallInEditorMethods" ) );
            return;
        }

        const utf8* pLastInvokeResult = _model.getLastInvokeResult();
        if ( StringUtil::isNullOrEmpty( pLastInvokeResult ) == false )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "LastResult", TextArgumentList().addText( "lastInvokeResult", pLastInvokeResult ) ).c_str() );

        for ( const FunctionInfo& method : pTypeInfo->_listMethod )
        {
            if ( method._metadata->_bConstructor != SW_FALSE || isMethodListed( method ) == false )
                continue;
            ++_methodRowCount;
            drawTypeMethod( pTypeInfo, method );
        }
    }

    bool EditorPropertyGrid::isMethodListed( const FunctionInfo& method ) const
    {
        return method._metadata->getEditor()._bCallInEditor != SW_FALSE || _bShowAllMethods == SW_TRUE;
    }

    void EditorPropertyGrid::drawTypeMethod( const TypeInfo* pTypeInfo, const FunctionInfo& method )
    {
        // 생성자 호출기는 초기화되지 않은 저장 공간(placement new)을 기대한다. 살아 있는 인스펙터 인스턴스에 호출하면 안전하지 않다.
        if ( method._metadata->_bConstructor != SW_FALSE )
            return;

        const utf8* pLabelName = method._name.c_str();
        if ( method._metadata->getEditor()._displayName.empty() == false )
            pLabelName = method._metadata->getEditor()._displayName.c_str();

        const uint32 paramCount = method.getParameterCount();
        // 네트워크 함수(Server · Client · Multicast)를 여기서 부르면 RPC 경로(`RemoteCall`)를 지나지 않고 이 프로세스에서 바로 돈다 — 툴팁이 그렇게 알린다.
        const bool bNetRole = method._metadata->_netRole != FunctionNetRole::Local;

        if ( method._metadata->getEditor()._bCallInEditor != SW_FALSE && paramCount == 0 )
        {
            // 강조색을 쓰지 않는다 — 앱 화면 규칙상 강조는 데이터 · 선택에만 둔다(전에는 패널 전용 파란 단추였다).
            ImGui::PushID( method._hashName.c_str() );
            fixed_string<constant::buffer::kMax128> buttonLabel;
            formatstring( buttonLabel.data(), buttonLabel.capacity(), "Run %#", pLabelName );
            if ( ImGui::Button( buttonLabel.c_str(), ImVec2{ -FLT_MIN, 0.0f } ) )
            {
                _model.invokeMethod( method, TaskArgs{} );
            }
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( string( "inspector.method.run." ) + method._name.c_str() ).c_str() );
            EditorWidgets::drawTooltip( bNetRole ? SW_LOCKEY( "EditorInspector", "NetRoleMethodRunsLocally" ) : method._metadata->getEditor()._tooltip.c_str() );
            ImGui::PopID();
            return;
        }

        ImGui::PushID( method._hashName.c_str() );
        ImGui::Text( "%s (%s)", pLabelName,
                     method._returnTypeName.empty() ? "?" : method._returnTypeName.c_str() );
        if ( method._metadata->getEditor()._bCallInEditor == SW_FALSE )
        {
            // 진단 보기에서만 보이는 줄이다 — 실행 안전성을 선언하지 않은 함수라는 표시를 색이 아니라 글로 단다.
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInspector", "DiagnosticMethodTag" ) );
        }
        if ( bNetRole )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInspector", "NetRoleNotRoutedTag" ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "NetRoleMethodRunsLocally" ) );
        }
        EditorWidgets::drawTooltip( method._metadata->getEditor()._tooltip.c_str() );
        // 키는 타입 · 메서드 이름 원문이다(해시 조합은 부딪히면 다른 메서드의 인자 슬롯을 쓴다).
        const string                    slotKey  = string( pTypeInfo->_fullyQualifiedName.c_str() ) + '\x1F' + method._hashName.c_str();
        vector<InspectorMethodArgSlot>& listSlot = _mapMethodArgSlot[slotKey];
        const bool                      bArgsOk  = InspectorBuiltinValue::prepareMethodArgs( method, listSlot );
        drawMethodArgInputs( method, listSlot );

        if ( paramCount > InspectorBuiltinValue::kMaxMethodArgCount )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "TooManyArgumentsMax", TextArgumentList().addInteger( "maxMethodArgCount", static_cast<int64>( InspectorBuiltinValue::kMaxMethodArgCount ) ) ).c_str() );

        if ( bArgsOk == false )
        {
            ImGui::BeginDisabled();
            ImGui::Button( SW_LOCKEY( "EdText", "Invoke" ) );
            ImGui::EndDisabled();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "UnsupportedFUNCTIONArgsInvokeSkipped" ) );
        }
        else
        {
            const bool bInvoke = ImGui::Button( SW_LOCKEY( "EdText", "Invoke" ) );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                const string invokeMark = string( "inspector.method.invoke." ) + method._name.c_str();
                EditorSelfTestMarks::record( invokeMark.c_str() );
            }
            if ( bInvoke )
                _model.invokeMethod( method, InspectorBuiltinValue::makeMethodArgs( listSlot ) );
        }

        ImGui::PopID();
    }

    void EditorPropertyGrid::drawMethodArgInputs( const FunctionInfo& method, vector<InspectorMethodArgSlot>& inoutListSlot )
    {
        for ( uint32 paramIndex = 0; paramIndex < static_cast<uint32>( inoutListSlot.size() ); ++paramIndex )
        {
            ImGui::PushID( static_cast<int32>( paramIndex ) );
            // 선언에 인자 이름이 있으면 그것을 쓰고, 없으면 순번을 쓴다.
            const FunctionParameterInfo&           parameter = method._listParameter[paramIndex];
            fixed_string<constant::buffer::kMax64> label;
            if ( parameter._name.empty() )
                formatstring( label.data(), label.capacity(), "arg%# (%#)", paramIndex, parameter._typeName.c_str() );
            else
                formatstring( label.data(), label.capacity(), "%# (%#)", parameter._name.c_str(), parameter._typeName.c_str() );
            InspectorPropertyManager::drawMethodArg( label.c_str(), inoutListSlot[paramIndex], parameter, _model, _listMethodArgChoice );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                // 시나리오가 인자 입력란을 누른다(핸들 선택기의 콤보가 마지막 항목이다). 인자 레이블 글("mesh (ComponentHandle)")이 오른쪽에 붙어
                // 좁은 창에서 위젯 사각형의 가운데가 레이블 글 위에 놓이므로 입력 칸만 표식으로 남긴다.
                StringBuilder<constant::buffer::kMax256> argMarkBuilder;
                argMarkBuilder.appendFormat( "%#%#.%#", string( "inspector.method.arg." ), method._name.c_str(), paramIndex );
                const string argMark{ argMarkBuilder.view() };
                EditorSelfTestMarks::recordInputFrame( argMark.c_str() );
            }
            ImGui::PopID();
        }
    }

    void EditorPropertyGrid::drawTypeEvents( const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo == nullptr )
            return;
        bool bHasEvent = false;
        pTypeInfo->forEachEventWithBase( [&bHasEvent]( const EventInfo& )
        { bHasEvent = true; } );
        if ( bHasEvent == false )
            return;

        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Events" ) );
        pTypeInfo->forEachEventWithBase( [this]( const EventInfo& event )
        {
            ImGui::PushID( event._name.c_str() );
            const string signature = InspectorPropertyLayout::formatParameterList( event._listParameter );
            ImGui::BulletText( "%s(%s)", event._name.c_str(), signature.c_str() );
            EditorWidgets::drawTooltip( event._metadata->getEditor()._tooltip.c_str() );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", _model.isEventBound( event ) ? "(bound)" : "(unbound)" );
            if ( event.getParameterCount() == 0 )
            {
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Broadcast" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Broadcast" ) ) )
                    _model.broadcastEvent( event );
            }
            ImGui::PopID();
        } );
    }
} // namespace sw::editor
