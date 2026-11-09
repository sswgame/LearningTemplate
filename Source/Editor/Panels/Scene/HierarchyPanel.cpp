#include "pch.h"

#include "Editor/Panels/Scene/HierarchyPanel.h"

#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorCompactControls.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorComponentMenu.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct HierarchyPanelInternal
        {
            static constexpr Color kPrefabInstanceText{ 0.45f, 0.70f, 1.0f, 1.0f };

            // 이 창만의 액션(행 목록을 아는 것). 키는 `editor.input.xml` 의 `Editor.Panel.hierarchy` 레이어가 정한다.
            static constexpr string_view kActionSelectAll      = "hierarchy.selectAll";
            static constexpr string_view kActionSelectPrevious = "hierarchy.selectPrevious";
            static constexpr string_view kActionSelectNext     = "hierarchy.selectNext";

            static void runMoveUp()
            {
                HierarchyCommands::moveSelectionStep( false );
            }

            static void runMoveDown()
            {
                HierarchyCommands::moveSelectionStep( true );
            }

            /** @brief 끌어 놓을 위치를 행 위에 그립니다 — 앞 · 뒤는 선, 자식으로 넣기는 행 테두리입니다. */
            static void drawDropIndicator( HierarchyDropZone zone, const ImVec2& rowMin, const ImVec2& rowMax )
            {
                ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
                const ImU32   color     = EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Accent" ) );
                const float32 thickness = 2.0f;
                if ( zone == HierarchyDropZone::Before )
                    pDrawList->AddLine( ImVec2{ rowMin.x, rowMin.y }, ImVec2{ rowMax.x, rowMin.y }, color, thickness );
                else if ( zone == HierarchyDropZone::After )
                    pDrawList->AddLine( ImVec2{ rowMin.x, rowMax.y }, ImVec2{ rowMax.x, rowMax.y }, color, thickness );
                else
                    pDrawList->AddRect( rowMin, rowMax, color, 0.0f, 0, thickness );
            }

            /**
             * @brief 방금 그린 오브젝트 행의 끌기 · 놓기입니다. 행의 위 · 아래 가장자리는 그 오브젝트 앞 · 뒤의 형제로, 가운데는 자식으로 넣습니다(`HierarchyViewModel::dropObject`).
             *        놓기 전에는 위치 표시만 그립니다. 에셋을 놓으면 그 아래에 스폰합니다.
             */
            static void drawRowDragDrop( const HierarchyRowEntry& entry )
            {
                HierarchyViewModel& viewModel = HierarchyViewModel::get();
                // 끌기는 `{GameObject, id}` 한 형식이다 — 씬 뷰 · 인스펙터 칸도 같은 종류로 받는다.
                (void)EditorInputGUI::beginDragSource( EditorDragPayload{ EditorPayloadKind::GameObject, entry._id }, entry._name.c_str() ); // 시작했는지는 쓰지 않는다

                EditorDragPayload payload{};
                bool              bDelivered = false;
                if ( EditorInputGUI::previewDrop( EditorPayloadKindSet{ EditorPayloadKind::GameObject }, payload, bDelivered ) )
                {
                    const ImVec2            rowMin = ImGui::GetItemRectMin();
                    const ImVec2            rowMax = ImGui::GetItemRectMax();
                    const HierarchyDropZone zone   = HierarchyViewModel::computeDropZone( EditorInputGUI::readPointer()._position._y, rowMin.y, rowMax.y );
                    drawDropIndicator( zone, rowMin, rowMax );
                    if ( bDelivered )
                        viewModel.dropObject( payload._id, entry._id, zone );
                }

                string droppedAssetPath;
                if ( EditorWidgets::acceptAssetDrop( droppedAssetPath ) )
                    viewModel.spawnPrefab( droppedAssetPath.c_str(), entry._id );
            }

            /** @brief 컴포넌트 행의 맥락 메뉴입니다(명령 등록부의 컴포넌트 메뉴 — `EditorSceneObjectCommands.cpp`). */
            static void drawComponentContextMenu( uint64 componentID, uint64 selectedComponentID )
            {
                EditorContextMenuSubject subject{};
                subject._pPopupID  = "CompCtx";
                subject._kind      = EditorCommandTargetKind::Component;
                subject._hoveredID = componentID;
                if ( selectedComponentID != 0 )
                    subject._listSelectedID.push_back( selectedComponentID );
                (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
            }

            /**
             * @brief 오브젝트 행의 맥락 메뉴입니다. 항목은 명령 등록부의 오브젝트 메뉴(`EditorSceneObjectCommands.cpp` 와 아래 계층 창 항목)이고, 대상은 선택 전체다(R3).
             * @details 선택 밖 행에서 열면 그 행만 고른다(R2). 열기 · 대상 · 빈 팝업 규칙은 `EditorInputGUI::drawContextMenuFor` 한 곳이 정한다.
             */
            static void drawGameObjectContextMenu( uint64 objectID, const vector<uint64>& listSelectedObjectID )
            {
                EditorContextMenuSubject subject{};
                subject._pPopupID       = "GOCtx";
                subject._kind           = EditorCommandTargetKind::GameObject;
                subject._hoveredID      = objectID;
                subject._listSelectedID = listSelectedObjectID;
                subject._selectOnly     = &HierarchyCommands::selectOnlyObject;
                (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
            }

            /** @brief "Add Component" 하위 메뉴의 안입니다(맥락 메뉴 명령 `scene.object.addComponent` 가 부른다). */
            static void drawAddComponentSubmenu( const EditorCommandTarget& target )
            {
                const uint64 objectID = HierarchyCommands::findTargetObjectID( target );
                if ( objectID == 0 )
                    return;
                HierarchyViewModel& viewModel = HierarchyViewModel::get();
                const hashed_string typeName  = EditorComponentMenu::drawComponentTypeList( viewModel.editAddComponentSearch(), "hierarchy.addComponent", false );
                if ( typeName.empty() == false )
                    viewModel.addComponent( objectID, typeName );
            }

            /** @brief 이 오브젝트의 컴포넌트 종류와 태그마다 "그것을 가진 오브젝트 모두 선택" 을 고르는 하위 메뉴의 안입니다(맥락 메뉴 명령 `scene.object.selectAllWith`). */
            static void drawSelectSameSubmenu( const EditorCommandTarget& target )
            {
                const uint64 objectID = HierarchyCommands::findTargetObjectID( target );
                if ( objectID == 0 )
                    return;
                HierarchyViewModel&     viewModel = HierarchyViewModel::get();
                HierarchySelectSameMenu menu{};
                viewModel.collectSelectSameMenu( objectID, menu );
                for ( uint32 itemIndex = 0; itemIndex < menu._listLabel.size(); ++itemIndex )
                {
                    if ( itemIndex == menu._typeCount )
                        ImGui::Separator(); // 종류와 태그 사이
                    if ( ImGui::MenuItem( menu._listLabel[itemIndex].c_str() ) )
                        viewModel.selectAllWith( objectID, itemIndex );
                }
            }

            /** @brief 방금 그린 행(트리 노드) 이름 뒤에 배지를 회색으로 얹습니다. 배치는 바꾸지 않는다 — 창이 좁으면 배지만 잘린다. */
            static void drawRowBadge( const fixed_string<constant::buffer::kMax256>& label, const fixed_string<constant::buffer::kMax256>& badge )
            {
                if ( badge.empty() )
                    return;
                const utf8* const pText    = label.c_str();
                const utf8*       pTextEnd = strstr( pText, "##" );
                const ImVec2      itemMin  = ImGui::GetItemRectMin();
                const float32     textY    = itemMin.y + ( ImGui::GetItemRectSize().y - ImGui::GetFontSize() ) * 0.5f;
                const float32     textX    = itemMin.x + ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize( pText, pTextEnd ).x + ImGui::GetStyle().ItemSpacing.x;
                ImGui::GetWindowDrawList()->PushClipRect( itemMin, ImGui::GetItemRectMax(), true );
                ImGui::GetWindowDrawList()->AddText( ImVec2{ textX, textY }, ImGui::GetColorU32( ImGuiCol_TextDisabled ), badge.c_str() );
                ImGui::GetWindowDrawList()->PopClipRect();
            }

            /** @brief 펼칠 수 있는 행의 트리 노드 깃발입니다. 잎은 깊이를 넣지 않는다(`NoTreePushOnOpen` — 닫을 짝이 없다). */
            static ImGuiTreeNodeFlags makeTreeNodeFlags( const HierarchyRowEntry& entry, ImGuiTreeNodeFlags spanFlag )
            {
                return ImGuiTreeNodeFlags_OpenOnArrow | spanFlag | ( entry._bSelected != 0 ? ImGuiTreeNodeFlags_Selected : 0 ) |
                       ( entry._bLeaf != 0 ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0 );
            }

            /**
             * @brief 펼침을 ViewModel 이 든 값으로 넘기고 트리 노드를 그린 뒤, 사용자가 화살표로 바꾼 값을 돌려줍니다(언리얼 아웃라이너의 ExpansionState — ImGui 저장소를 쓰지 않는다).
             * @return 노드가 깊이를 넣었으면 true 입니다(잎이 아니고 열렸다).
             */
            static bool drawTreeNode( const HierarchyRowEntry& entry, ImGuiTreeNodeFlags spanFlag )
            {
                if ( entry._bLeaf == 0 )
                    ImGui::SetNextItemOpen( entry._bExpanded != 0, ImGuiCond_Always );
                const bool bOpen = ImGui::TreeNodeEx( entry._label.c_str(), makeTreeNodeFlags( entry, spanFlag ) );
                if ( entry._bLeaf != 0 )
                    return false;
                if ( bOpen != ( entry._bExpanded != 0 ) )
                    HierarchyViewModel::get().setExpanded( entry._kind, entry._id, bOpen );
                return bOpen;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( HierarchyPanel, HierarchyPanel::kPanelID, EditorPanelCategory::Core, 100 );
    SW_EDITOR_COMMAND( SceneMoveSiblingUp, "scene.moveSiblingUp", &HierarchyPanelInternal::runMoveUp, nullptr, Scene );
    SW_EDITOR_COMMAND( SceneMoveSiblingDown, "scene.moveSiblingDown", &HierarchyPanelInternal::runMoveDown, nullptr, Scene );
    SW_EDITOR_COMMAND( SceneCreateFolder, "scene.createFolder", &HierarchyCommands::createFolder, nullptr, Scene );
    // 계층 창이 그리는 오브젝트 메뉴 항목(하위 메뉴 · 이름 바꾸기) — 같은 종류(GameObject)의 메뉴라 씬 뷰에서 열어도 나온다(R5).
    SW_EDITOR_CONTEXT_COMMAND( ObjectRename, "scene.object.rename", &HierarchyCommands::requestRename, &HierarchyCommands::hasSingleTargetObject, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectAddComponent, "scene.object.addComponent", nullptr, &HierarchyCommands::hasSingleTargetObject, Scene, &HierarchyPanelInternal::drawAddComponentSubmenu );
    SW_EDITOR_CONTEXT_COMMAND( ObjectSelectAllWith, "scene.object.selectAllWith", nullptr, &HierarchyCommands::hasSingleTargetObject, NoScene, &HierarchyPanelInternal::drawSelectSameSubmenu );

    HierarchyPanel::HierarchyPanel()
        : _state{}
        , _lastDrawnRootID{ 0 }
        , _filterBuffer{}
        , _renameBuffer{}
        , _drawnRootCount{ 0 }
        , _bFocusRenameInput{ SW_FALSE }
        , _bSkipOffscreenRows{ SW_TRUE }
    {
    }

    void HierarchyPanel::drawContent()
    {
        if ( HierarchyViewModel::hasActiveScene() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorScene", "HierarchyNoActiveScene" ) );
            return;
        }

        drawToolbar();
        ImGui::Separator();

        editor::EditorSectionDesc treeDesc{};
        treeDesc._pID  = "##HierarchyTree";
        treeDesc._kind = editor::EditorSectionKind::Child;
        if ( EditorChrome::beginSection( treeDesc ) )
        {
            // gather 는 트리 영역을 연 뒤 한 번 — 이번 프레임에 적용된 스크롤(휠 · 스크롤 막대 · SetScrollY)로 세부를 채울 행을 고른다. 툴바 뒤라 같은 프레임에
            // 검색창에 친 글 · 만들기 단추가 바꾼 구조가 이 프레임 트리에 바로 닿는다. 툴바는 ViewModel 값을 읽지 않는다(명령과 편집 잠금만).
            // 행 높이: 오브젝트 행은 토글 단추가 있어 프레임 높이, 컴포넌트 행(트리 노드 · Selectable 만 있는 줄)은 글 높이다.
            HierarchyRowWindow window{};
            window._stride._object     = ImGui::GetFrameHeightWithSpacing();
            window._stride._component  = ImGui::GetTextLineHeightWithSpacing();
            window._scrollY            = ImGui::GetScrollY();
            window._viewHeight         = ImGui::GetWindowHeight();
            window._bSkipOffscreenRows = _bSkipOffscreenRows;
            HierarchyViewModel::get().gather( _filterBuffer.view(), window, _state );
            if ( _state._bRenameStarted != 0 )
            {
                _renameBuffer      = _state._renameStartText.view();
                _bFocusRenameInput = SW_TRUE;
            }
            drawRows( window._stride );
            drawTreeEmptyArea();
        }
        EditorChrome::endSection();
    }

    void HierarchyPanel::drawToolbar()
    {
        HierarchyViewModel& viewModel = HierarchyViewModel::get();
        // 상단 툴바에는 생성 버튼과 검색창이 있다
        if ( EditorChrome::beginToolbar( "##HierarchyToolbar" ) )
        {
            const bool bEditsAllowed = areSceneEditsAllowed();
            if ( bEditsAllowed == false )
            {
                EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "PlayMode" ), SW_THEME_COLOR( "Signal", "Warning" ) );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "PlayModeRunning" ) );
                ImGui::SameLine();
            }
            if ( bEditsAllowed == false )
                ImGui::BeginDisabled();
            if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdButton", "PlusCreate" ), "+ Create" ) ) )
                viewModel.createObject();
            EditorSelfTestMarks::record( "hierarchy.create" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "CreateGameObject" ) );
            if ( bEditsAllowed == false )
                ImGui::EndDisabled();

            // 형식 필터 — 검색 문법 `t:<종류>` 를 몰라도 고를 수 있게 씬에 있는 컴포넌트 종류를 늘어놓는다(언리얼 아웃라이너의 필터 단추 · 유니티 계층 창의 형식 검색).
            // 검색 칸이 남은 너비를 다 쓰므로 단추는 그 앞에 둔다.
            ImGui::SameLine();
            const string_view activeType = HierarchyFilter::findTypeFilter( _filterBuffer.view() );
            if ( EditorCompactControls::beginViewOptions( "##HierarchyTypeFilter", activeType.empty() == false, SW_LOCKEY( "EditorScene", "HierarchyTypeFilter" ), "hierarchy.typeFilter" ) )
            {
                if ( ImGui::MenuItem( SW_LOCKEY( "EditorScene", "HierarchyAllTypes" ), nullptr, activeType.empty() ) )
                    _filterBuffer.clear();
                EditorSelfTestMarks::record( "hierarchy.typeFilter.all" );
                ImGui::Separator();
                vector<string> listTypeName; // 메뉴가 열렸을 때만 씬을 훑는다
                viewModel.collectTypeFilterNames( listTypeName );
                for ( const string& typeName : listTypeName )
                {
                    if ( ImGui::MenuItem( typeName.c_str(), nullptr, StringUtil::equals( activeType, typeName ) ) )
                        _filterBuffer = string_view{ "t:" + typeName };
                    const string mark = "hierarchy.typeFilter." + typeName;
                    EditorSelfTestMarks::record( mark.c_str() );
                }
                EditorCompactControls::endViewOptions();
            }

            ImGui::SameLine();
            EditorWidgets::drawSearchField( "##HierarchyFilter", _filterBuffer, SW_LOCKEY( "EditorScene", "HierarchySearchHint" ), 0.0f, false, "hierarchy.filter" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "HierarchySearch" ) );
        }
        EditorChrome::endToolbar();
    }

    void HierarchyPanel::drawRows( const HierarchyRowMetrics& stride )
    {
        // 화면 밖의 행은 그리지 않고 같은 높이의 빈 공간만 둔다. 스크롤 막대와 다음 행의 위치는 그대로다. 모두 그리면 큐브 8000개 씬에서 노드 8000개
        // (버튼, 배지 문자열, 트리 노드, 드래그 앤 드롭)를 프레임마다 그린다. ViewModel 이 화면 밖 행을 묶음으로 넘기고, 이어진 빈 행은 빈 공간 하나로 합친다.
        const float32 rowSpacing      = ImGui::GetStyle().ItemSpacing.y;
        float32       pendingHeight   = 0.0f;
        uint32        treeDepth       = 0; ///< 지금 연 트리 깊이(넣은 TreePush 수)
        uint32        drawnRootCount  = 0;
        uint64        lastDrawnRootID = 0;
        auto          flushPending    = [&pendingHeight, rowSpacing]()
        {
            if ( pendingHeight <= 0.0f )
                return;
            ImGui::Dummy( ImVec2( 0.0f, pendingHeight - rowSpacing ) ); // Dummy 도 줄 간격을 더한다
            pendingHeight = 0.0f;
        };
        for ( const HierarchyRowEntry& entry : _state._listEntry )
        {
            if ( entry.isSkippedRun() )
            {
                pendingHeight += static_cast<float32>( entry._skippedObjectRows ) * stride._object + static_cast<float32>( entry._skippedComponentRows ) * stride._component;
                continue;
            }
            // 이번 프레임에 접은 행의 자식은 아직 목록에 있지만 그 깊이가 열려 있지 않다 — 다음 gather 에서 빠진다.
            if ( entry._depth > treeDepth )
                continue;
            while ( treeDepth > entry._depth )
            {
                ImGui::TreePop();
                --treeDepth;
            }

            const float32 rowHeight = entry._kind == HierarchyRowKind::Object ? stride._object : stride._component;
            const ImVec2  cursor    = ImGui::GetCursorScreenPos();
            const ImVec2  rowMin{ cursor.x, cursor.y + pendingHeight };
            const bool    bOnScreen  = ImGui::IsRectVisible( rowMin, ImVec2( rowMin.x + 1.0f, rowMin.y + rowHeight - rowSpacing ) );
            const bool    bMustDraw  = bOnScreen || entry._bRenaming != 0 || entry._bScrollTo != 0 || _bSkipOffscreenRows == 0;
            const bool    bOpenEntry = entry._bExpanded != 0 && entry._bLeaf == 0;
            if ( bMustDraw == false )
            {
                pendingHeight += rowHeight;
                // 화면 밖의 펼친 행(보이는 행의 조상) — 그리지 않고 깊이만 연다. 트리 노드가 열릴 때 넣는 id 와 같다(라벨의 id).
                if ( bOpenEntry )
                {
                    ImGui::TreePushOverrideID( ImGui::GetID( entry._label.c_str() ) );
                    ++treeDepth;
                }
                continue;
            }

            flushPending();
            bool bPushed = false;
            if ( entry._kind == HierarchyRowKind::Object )
            {
                bPushed = drawObjectRow( entry );
                if ( entry._depth == 0 )
                {
                    ++drawnRootCount;
                    lastDrawnRootID = entry._id;
                }
            }
            else if ( entry._kind == HierarchyRowKind::SceneComponent )
            {
                bPushed = drawSceneComponentRow( entry );
            }
            else
            {
                drawComponentRow( entry );
            }
            if ( bPushed )
                ++treeDepth;
        }
        flushPending();
        while ( treeDepth > 0 )
        {
            ImGui::TreePop();
            --treeDepth;
        }
        _drawnRootCount  = drawnRootCount;
        _lastDrawnRootID = lastDrawnRootID;

        // 행이 모두 필터에 걸러지면 빈 상자 대신 이유를 보여 준다.
        if ( _state._visibleRootCount == 0 && _state._bFilterActive != 0 )
            EditorWidgets::drawNoSearchResultHint( EditorListFilter{ _filterBuffer.c_str() }.getText() );
    }

    bool HierarchyPanel::drawObjectRow( const HierarchyRowEntry& entry )
    {
        HierarchyViewModel& viewModel = HierarchyViewModel::get();

        // 토글은 행 id 아래 둔다(행마다 같은 "##visible" 이름이다). 트리 노드는 그 밖이다 — 열리면 깊이를 넣어 자식 행이 그 아래 놓이므로 행 id 를 먼저 닫는다.
        ImGui::PushID( static_cast<int32>( entry._id ) );
        // 1) 눈 아이콘은 에디터에서만 숨긴다. 저장, 되돌리기, 게임 동작에 남지 않는다(언리얼과 유니티). 활성 비트는 인스펙터의 Active 체크박스다.
        const bool bVisible = entry._bHiddenInEditor == 0;
        if ( EditorWidgets::drawRowIconToggle( "##visible", bVisible, true, SW_EDITOR_ICON( "Eye" ), SW_EDITOR_ICON( "EyeSlash" ), SW_LOCKEY( "EditorScene", "VisibleInEditor" ),
                                               SW_LOCKEY( "EditorScene", "HiddenInEditor" ) ) )
            viewModel.setHiddenInEditor( entry._id, bVisible );
        EditorSelfTestMarks::record( "hierarchy.activeToggle" );
        if ( EditorSelfTestMarks::isEnabled() )
            EditorSelfTestMarks::record( ( string( "hierarchy.toggle." ) + entry._name.c_str() ).c_str() );
        ImGui::SameLine();
        // 2) 자물쇠 아이콘은 뷰포트에서 선택하지 못하게 한다. Hierarchy 에서는 선택할 수 있다. 저장하지 않는다.
        const bool bLocked = entry._bLocked != 0;
        if ( EditorWidgets::drawRowIconToggle( "##lock", bLocked, false, SW_EDITOR_ICON( "Lock" ), SW_EDITOR_ICON( "Unlock" ), SW_LOCKEY( "EditorScene", "LockedInViewport" ),
                                               SW_LOCKEY( "EditorScene", "PickableInViewport" ) ) )
            viewModel.setObjectLocked( entry._id, bLocked == false );
        if ( EditorSelfTestMarks::isEnabled() )
            EditorSelfTestMarks::record( ( string( "hierarchy.lock." ) + entry._name.c_str() ).c_str() );
        ImGui::SameLine();
        ImGui::PopID();

        // 프리팹 인스턴스는 이름을 파랗게 표시한다(유니티 Hierarchy).
        if ( entry._bPrefabInstance != 0 )
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( HierarchyPanelInternal::kPrefabInstanceText ) );
        // SpanAvailWidth 를 쓰면 선택 배경과 클릭 영역이 앞의 가시성 토글 오른쪽부터 시작한다. SpanFullWidth 는 창 왼쪽부터 칠해서 토글을 덮는다.
        const bool bPushed = HierarchyPanelInternal::drawTreeNode( entry, ImGuiTreeNodeFlags_SpanAvailWidth );
        if ( entry._bPrefabInstance != 0 )
            ImGui::PopStyleColor();
        HierarchyPanelInternal::drawRowBadge( entry._label, entry._badge );
        // 뷰포트에서 선택한 오브젝트는 조상을 펼쳐(ViewModel) 그 행까지 보여 준다. 언리얼 Outliner 의 선택 따라가기에 해당한다.
        if ( entry._bScrollTo != 0 )
            ImGui::SetScrollHereY( 0.5f );
        if ( entry._bSelected != 0 )
            EditorSelfTestMarks::record( "hierarchy.selectedRow" ); // 시나리오가 오른쪽 클릭으로 오브젝트 메뉴를 연다
        if ( EditorSelfTestMarks::isEnabled() )
            EditorSelfTestMarks::record( ( string( "hierarchy.row." ) + entry._name.c_str() ).c_str() );

        // 행 뒤의 것(클릭 · 이름 칸 · 맥락 메뉴 · 끌기)은 방금 그린 트리 노드를 본다. 열린 노드가 넣은 깊이 위에 행 id 를 얹고 바로 닫는다.
        ImGui::PushID( static_cast<int32>( entry._id ) );
        if ( EditorInputGUI::wasItemClicked() )
        {
            const uint8 modifierMask = EditorInputGUI::getModifierMask();
            viewModel.clickRow( entry._id, ( modifierMask & ModifierKey::Ctrl ) != 0, ( modifierMask & ModifierKey::Shift ) != 0 );
        }
        drawInlineRename( entry );
        HierarchyPanelInternal::drawGameObjectContextMenu( entry._id, _state._listSelectedObjectID );
        HierarchyPanelInternal::drawRowDragDrop( entry );
        ImGui::PopID();
        return bPushed;
    }

    bool HierarchyPanel::drawSceneComponentRow( const HierarchyRowEntry& entry )
    {
        const bool bPushed = HierarchyPanelInternal::drawTreeNode( entry, ImGuiTreeNodeFlags_SpanFullWidth );
        ImGui::PushID( static_cast<int32>( entry._id ) );
        if ( EditorInputGUI::wasItemClicked() )
            HierarchyViewModel::get().selectComponent( entry._objectID, entry._id );
        HierarchyPanelInternal::drawComponentContextMenu( entry._id, _state._selectedComponentID );
        ImGui::PopID();
        return bPushed;
    }

    void HierarchyPanel::drawComponentRow( const HierarchyRowEntry& entry )
    {
        ImGui::PushID( static_cast<int32>( entry._id ) );
        if ( ImGui::Selectable( entry._label.c_str(), entry._bSelected != 0 ) )
            HierarchyViewModel::get().selectComponent( entry._objectID, entry._id );
        HierarchyPanelInternal::drawComponentContextMenu( entry._id, _state._selectedComponentID );
        ImGui::PopID();
    }

    void HierarchyPanel::drawInlineRename( const HierarchyRowEntry& entry )
    {
        if ( entry._bRenaming == 0 || areSceneEditsAllowed() == false )
            return;

        HierarchyViewModel& viewModel = HierarchyViewModel::get();
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 21.0f );
        if ( _bFocusRenameInput != 0 )
        {
            ImGui::SetKeyboardFocusHere();
            _bFocusRenameInput = SW_FALSE;
        }
        const bool bSubmitted = ImGui::InputText( "##InlineRename", _renameBuffer.data(), _renameBuffer.capacity(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll );
        const bool bCanceled  = EditorInputGUI::wasCancelPressed();
        // 엔터도 칸을 놓으므로 확정은 한 번만 한다.
        if ( bSubmitted || ( ImGui::IsItemDeactivated() && bCanceled == false ) )
        {
            viewModel.renameObject( entry._id, _renameBuffer.c_str() );
            viewModel.endRename();
        }
        else if ( bCanceled )
        {
            viewModel.endRename();
        }
    }

    void HierarchyPanel::drawTreeEmptyArea()
    {
        HierarchyViewModel& viewModel = HierarchyViewModel::get();
        // 빈 영역에 오브젝트를 놓으면 루트로 떼고, 에셋을 놓으면 루트에 스폰한다(끌기는 `{종류, id}` 한 형식).
        EditorDragPayload objectPayload{};
        if ( EditorInputGUI::acceptDrop( EditorPayloadKindSet{ EditorPayloadKind::GameObject }, objectPayload ) )
            viewModel.detachToRoot( objectPayload._id );
        string droppedAssetPath;
        if ( EditorWidgets::acceptAssetDrop( droppedAssetPath ) )
            viewModel.spawnPrefab( droppedAssetPath.c_str(), 0 );

        handleHierarchyShortcuts();
        // 빈 곳 메뉴 — 같은 오브젝트 메뉴를 선택 그대로 연다(R2: 빈 곳은 선택을 바꾸지 않는다, R3: 대상은 선택 전체). 만들기 · 붙여 넣기는 선택이 없어도 켜진다.
        EditorContextMenuSubject subject{};
        subject._pPopupID       = "HierarchyEmptyCtx";
        subject._kind           = EditorCommandTargetKind::GameObject;
        subject._listSelectedID = _state._listSelectedObjectID;
        (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
    }

    void HierarchyPanel::handleHierarchyShortcuts()
    {
        // 복제 · 복사 · 붙여 넣기 · 지우기 · 이름 바꾸기는 명령이다(`editor.input.xml` 의 `Editor.Panel.hierarchy` 레이어 — 이 창이 포커스일 때만 켜진다).
        // 여기는 행 목록을 아는 이 창만의 액션(모두 선택 · 위아래 이동)을 읽는다.
        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_ChildWindows ) == false )
            return;
        HierarchyViewModel& viewModel = HierarchyViewModel::get();
        viewModel.focusMainSelection();
        if ( EditorInputGUI::wasActionTriggered( HierarchyPanelInternal::kActionSelectAll ) )
        {
            viewModel.selectAllRows();
            return;
        }
        const bool bUp   = EditorInputGUI::wasActionTriggered( HierarchyPanelInternal::kActionSelectPrevious );
        const bool bDown = EditorInputGUI::wasActionTriggered( HierarchyPanelInternal::kActionSelectNext );
        if ( bUp || bDown )
            viewModel.selectStep( bUp );
    }
} // namespace sw::editor
