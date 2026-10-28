#include "pch.h"

#include "Editor/Panels/Scene/HierarchyPanel.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/unordered_set.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/TagID.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneObjectCommands.h"
#include "Editor/Common/Commands/Scene/EditorSiblingOrder.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorComponentIcon.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorComponentMenu.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    /** @brief 프로브 `Editor.HierarchyRowIndex` 가 볼 오브젝트 이름입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchHierarchyObject, "", "감시 값 Editor.HierarchyRowIndex 가 볼 오브젝트 이름 (시나리오용)" );

    namespace
    {
        struct HierarchyPanelInternal
        {
            static constexpr Color kPrefabInstanceText{ 0.45f, 0.70f, 1.0f, 1.0f };

            // 이 창만의 액션(행 목록을 아는 것). 키는 `editor.input.xml` 의 `Editor.Panel.hierarchy` 레이어가 정한다.
            static constexpr string_view kActionSelectAll      = "hierarchy.selectAll";
            static constexpr string_view kActionSelectPrevious = "hierarchy.selectPrevious";
            static constexpr string_view kActionSelectNext     = "hierarchy.selectNext";

            static bool typeMatchesFilter( GameObject* pObj, string_view typeFilter )
            {
                if ( pObj == nullptr || typeFilter.empty() )
                    return false;

                for ( Component* pComp : pObj->getComponents() )
                {
                    if ( pComp == nullptr )
                        continue;

                    if ( StringUtil::startsWith( pComp->getTypeName().view(), typeFilter, true ) )
                        return true;
                }
                return false;
            }

            static bool nameMatchesFilter( GameObject* pObj, const utf8* pFilter )
            {
                if ( StringUtil::isNullOrEmpty( pFilter ) )
                    return true;
                if ( pObj == nullptr )
                    return false;

                // 1) 타입 문법 "t:ComponentName"
                if ( pFilter[0] == 't' && pFilter[1] == ':' )
                {
                    return typeMatchesFilter( pObj, string_view{ pFilter + 2 } );
                }

                // 2) 태그 문법 "tag:TagName"
                if ( StringUtil::startsWith( pFilter, "tag:", true ) )
                {
                    // ID 의 원본은 `TagID::computeID` 다. `computeStringHash64` 를 기본 인자로 호출하면 대소문자를 무시해서
                    // 대소문자를 구별하는 태그 쪽과 어긋나고, 대문자로 시작하는 태그를 하나도 찾지 못한다.
                    //
                    // 문자열도 함께 넘긴다. `isSubtagOf` 가 문자열을 보므로 `tag:Faction` 이 `Faction.Player` 까지
                    // 찾는다. 리터럴 태그는 역조회 테이블에 등록되지 않아, ID 만 가진 TagID 로는 계층 비교를 할 수
                    // 없다. 필터 버퍼는 이 호출 동안 유효하다.
                    const string_view tagFilter{ pFilter + 4 };
                    return pObj->hasTag( TagID{ TagID::computeID( tagFilter.data(), tagFilter.size() ), tagFilter.data() } );
                }

                // 3) 일반 이름 매칭
                return EditorListFilter{ pFilter }.matches( pObj->getName().view() );
            }

            /**
             * @brief 계층 창의 행 모델입니다. 루트 순서와 필터에 걸린 오브젝트를 담고, 구조 세대 · 태그 세대 · 필터 글이 바뀔 때만 다시 짓습니다(`refreshRowModel`).
             * @details 프레임마다 전 오브젝트를 훑어 루트를 모으고 정렬하면 큐브 수천 개 씬에서 그것만으로 프레임을 먹는다. 필터를 켜면 노드마다 서브트리를
             *          재귀로 다시 물었다. 언리얼 아웃라이너도 트리를 이벤트로 고치고 프레임마다 다시 모으지 않는다.
             *          포인터를 들어도 된다. 오브젝트는 해체 전에 늘 구조 세대를 올리고(`GameObjectStore::destroyObject`), 그리기 전에 세대를 먼저 본다.
             */
            struct RowModel
            {
                vector<GameObject*>                   _listRoot{};            ///< 형제 순서의 루트(삭제 대기 제외)
                unordered_set<uint64>                 _uniqueFilterMatchID{}; ///< 자기나 후손이 필터에 걸린 오브젝트 id. 필터가 꺼져 있으면 비어 있다
                fixed_string<constant::kMaxBuffer128> _filterText{};          ///< 지을 때의 필터 글
                const GameObjectManager*              _pManager{ nullptr };   ///< 지을 때의 매니저
                uint64                                _structureGeneration{ 0 };
                uint64                                _tagGeneration{ 0 };
                uint32                                _rebuildCount{ 0 }; ///< 다시 지은 횟수(감시 값 `Editor.HierarchyRowRebuildCount`)
                bool                                  _bFilterActive{ false };
            };

            /** @brief 이번 프레임에 그린 오브젝트 행을 위에서 아래 순서로 담습니다. Shift 범위 선택, Ctrl+A, 화살표가 지난 프레임의 것을 봅니다. */
            struct RowState
            {
                RowModel                             _rowModel{};
                vector<uint64>                       _listDrawnRow;
                vector<uint64>                       _listPreviousRow;
                uint64                               _anchorObjectID{ 0 };  ///< 범위 선택의 시작 행. 마지막으로 그냥 클릭하거나 Ctrl 을 누르고 클릭한 행이다
                uint64                               _revealObjectID{ 0 };  ///< 펼쳐서 보여 줄 오브젝트. 뷰포트에서 선택했다
                uint64                               _lastPrimaryID{ 0 };   ///< 지난 프레임의 주 선택. 외부에서 바뀌었는지 구분하는 데 쓴다
                uint64                               _pendingRenameID{ 0 }; ///< 이름 바꾸기 명령이 고른 오브젝트. 다음 그리기에서 입력 칸을 연다
                fixed_string<constant::kMaxBuffer64> _addComponentSearch{}; ///< "Add Component" 메뉴의 검색 칸 글(메뉴를 닫았다 열어도 남는다)
                bool                                 _bClickedThisFrame{ false };
            };

            /** @brief 행 상태입니다. 에디터 범위의 상태 저장소가 소유한다. */
            static RowState& getRowState() { return getEditorState<RowState>(); }

            /** @brief 프레임 시작에 행 순서를 다시 모읍니다(지난 프레임의 것은 `_listPreviousRow` 로 넘긴다). 주 선택이 외부(뷰포트, 다른 패널)에서 바뀌었으면 그 행을 펼쳐 보여 줍니다. */
            static void beginRowFrame()
            {
                RowState& rowState = getRowState();
                rowState._listPreviousRow.swap( rowState._listDrawnRow );
                rowState._listDrawnRow.clear();

                const GameObject* pPrimary             = editor::getMainSceneSelection().getPrimaryObject();
                const uint64      primaryID            = pPrimary != nullptr ? pPrimary->getObjectID() : 0;
                const bool        bRevealPrimaryObject = primaryID != rowState._lastPrimaryID && rowState._bClickedThisFrame == false && primaryID != 0;
                if ( bRevealPrimaryObject )
                    rowState._revealObjectID = primaryID;
                rowState._lastPrimaryID     = primaryID;
                rowState._bClickedThisFrame = false;
            }

            /** @brief `gv_editorWatchHierarchyObject` 오브젝트가 마지막 프레임에 그린 행에서 몇 번째인지입니다(0 부터). 그리지 않았거나 없으면 -1 입니다. */
            [[nodiscard]] static bool readProbedRowIndex( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue                    = -1.0;
                GameObjectManager* pManager = editor::getActiveObjectManager();
                const GameObject*  pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( gv_editorWatchHierarchyObject.c_str() ) ) : nullptr;
                if ( pObject == nullptr )
                    return true;
                const vector<uint64>& listRow = getRowState()._listDrawnRow;
                for ( size_t rowIndex = 0; rowIndex < listRow.size(); ++rowIndex )
                {
                    if ( listRow[rowIndex] == pObject->getObjectID() )
                    {
                        outValue = static_cast<float64>( rowIndex );
                        break;
                    }
                }
                return true;
            }

            /** @brief 단일 선택(주 선택)된 오브젝트를 형제 사이에서 한 단계 옮깁니다. 커맨드(`scene.moveSiblingUp` · `Down`)가 씁니다. */
            static void moveSelectionStep( bool bDown )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = editor::getMainSceneSelection().getPrimaryObject();
                if ( pManager != nullptr && pObject != nullptr )
                    (void)EditorSiblingOrder::moveStep( *pManager, pObject, bDown, findEditorCameraObject() ); // 끝이면 아무것도 하지 않는다
            }

            static void runMoveUp()
            {
                moveSelectionStep( false );
            }

            static void runMoveDown()
            {
                moveSelectionStep( true );
            }

            /** @brief 주 선택이 폴더면 그 안에, 아니면 루트에 정리용 폴더를 만듭니다(언리얼 아웃라이너의 새 폴더). */
            static void runCreateFolder()
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;
                GameObject* pSelected = editor::getMainSceneSelection().getPrimaryObject();
                GameObject* pParent   = pSelected != nullptr && EditorSiblingOrder::isFolder( *pSelected ) ? pSelected : nullptr;
                (void)EditorSiblingOrder::createFolder( *pManager, pParent ); // 편집이 막혔으면 nullptr 이고 아무것도 바뀌지 않는다
            }

            /** @brief @p pObj 가 @p pTarget 의 조상이면 true 입니다. */
            static bool isAncestorOf( const GameObject* pObj, const GameObject* pTarget )
            {
                for ( const GameObject* pParent = pTarget != nullptr ? pTarget->getParent() : nullptr; pParent != nullptr; pParent = pParent->getParent() )
                {
                    if ( pParent == pObj )
                        return true;
                }
                return false;
            }

            /**
             * @brief 이 루트 행을 그리지 않고 공간만 둘 수 있는지 확인합니다. 접혀 있고, 이름을 바꾸는 중이 아니고, 행이 창의 보이는 범위 밖일 때 가능합니다.
             * @details 트리 노드의 열림 상태는 drawGameObjectNode 와 같은 ID 로 읽는다(PushID( 오브젝트 id ) 안의 "###go<id>"). @p cursorY 는 이 행이 놓일 화면 y 다.
             */
            static bool canSkipRootRow( const GameObject* pObj, uint64 renamingObjectID, float32 cursorY, float32 rowHeight )
            {
                const uint64 objectID = pObj->getObjectID();
                if ( objectID == renamingObjectID || getRowState()._revealObjectID != 0 )
                    return false;
                const ImVec2 rowMin{ ImGui::GetCursorScreenPos().x, cursorY };
                if ( ImGui::IsRectVisible( rowMin, ImVec2( rowMin.x + 1.0f, cursorY + rowHeight ) ) )
                    return false;
                fixed_string<constant::kMaxBuffer64> nodeIDLabel;
                formatstring( nodeIDLabel.data(), nodeIDLabel.capacity(), "###go%#", objectID );
                ImGui::PushID( static_cast<int32>( objectID ) );
                const ImGuiID nodeID = ImGui::GetID( nodeIDLabel.c_str() );
                ImGui::PopID();
                return ImGui::TreeNodeGetOpen( nodeID ) == false;
            }

            /**
             * @brief 행 모델이 지금 매니저 · 구조 세대 · 태그 세대 · 필터 글과 맞지 않으면 다시 짓습니다. 그리기 전에 한 번 부릅니다.
             * @details 필터는 걸린 오브젝트마다 조상을 거슬러 올라가며 표시한다(이미 표시된 조상에서 멈춘다). 노드마다 서브트리를 다시 묻지 않는다.
             */
            static void refreshRowModel( GameObjectManager& manager, string_view filterText )
            {
                RowModel&    model               = getRowState()._rowModel;
                const uint64 structureGeneration = manager.getStructureGeneration();
                const uint64 tagGeneration       = manager.getTagIndex().getGeneration();
                const bool   bUpToDate           = model._pManager == &manager && model._structureGeneration == structureGeneration && model._tagGeneration == tagGeneration &&
                                       model._filterText.view() == filterText;
                if ( bUpToDate )
                    return;

                model._pManager            = &manager;
                model._structureGeneration = structureGeneration;
                model._tagGeneration       = tagGeneration;
                model._filterText          = filterText;
                ++model._rebuildCount;
                EditorSceneCommands::collectRootsInOrder( manager, model._listRoot ); // 루트만 형제 순서로 모은다. 지우고 되돌려도 위치가 그대로다
                model._uniqueFilterMatchID.clear();
                model._bFilterActive = EditorListFilter{ model._filterText.c_str() }.isActive();
                if ( model._bFilterActive == false )
                    return;
                const utf8*            pFilter     = model._filterText.c_str();
                unordered_set<uint64>& uniqueMatch = model._uniqueFilterMatchID;
                manager.forEachGameObject( [pFilter, &uniqueMatch]( GameObject* pObj )
                {
                    if ( nameMatchesFilter( pObj, pFilter ) == false )
                        return;
                    for ( const GameObject* pCursor = pObj; pCursor != nullptr; pCursor = pCursor->getParent() )
                    {
                        if ( uniqueMatch.insert( pCursor->getObjectID() ).second == false )
                            break;
                    }
                } );
            }

            /** @brief 필터가 꺼져 있거나, 이 오브젝트나 후손이 필터에 걸렸으면 true 입니다(행 모델로 답한다). */
            static bool isRowVisible( const GameObject* pObj )
            {
                const RowModel& model = getRowState()._rowModel;
                return model._bFilterActive == false || model._uniqueFilterMatchID.contains( pObj->getObjectID() );
            }

            /** @brief 행 모델을 다시 지은 횟수입니다(감시 값 `Editor.HierarchyRowRebuildCount`). 구조가 그대로인 프레임에는 오르지 않아야 한다. */
            [[nodiscard]] static bool readRowRebuildCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getRowState()._rowModel._rebuildCount );
                return true;
            }

            /** @brief 계층 창이 보이지 않는 에디터 카메라 오브젝트입니다. 형제 번호를 다시 매길 때 빼야 한다. 없으면 nullptr 입니다. */
            static const GameObject* findEditorCameraObject()
            {
                const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
                return pEditorCamera != nullptr ? pEditorCamera->getOwner() : nullptr;
            }

            /** @brief 끌어 놓을 위치를 행 위에 그립니다 — 앞 · 뒤는 선, 자식으로 넣기는 행 테두리입니다. */
            static void drawDropIndicator( SiblingDropZone zone, const ImVec2& rowMin, const ImVec2& rowMax )
            {
                ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
                const ImU32   color     = EditorColorUtil::toImU32( EditorThemeUtil::getAccentColor() );
                const float32 thickness = 2.0f;
                if ( zone == SiblingDropZone::Before )
                    pDrawList->AddLine( ImVec2{ rowMin.x, rowMin.y }, ImVec2{ rowMax.x, rowMin.y }, color, thickness );
                else if ( zone == SiblingDropZone::After )
                    pDrawList->AddLine( ImVec2{ rowMin.x, rowMax.y }, ImVec2{ rowMax.x, rowMax.y }, color, thickness );
                else
                    pDrawList->AddRect( rowMin, rowMax, color, 0.0f, 0, thickness );
            }

            /**
             * @brief 오브젝트 행에 끌어 놓은 오브젝트를 처리합니다. 행의 위 · 아래 가장자리는 그 오브젝트 앞 · 뒤의 형제로(형제 순서를 저장한다),
             *        가운데는 자식으로 넣습니다(유니티 하이어라키의 끌기와 같다). 놓기 전에는 위치 표시만 그립니다.
             */
            static void handleHierarchyDrop( GameObject* pTarget, uint64 draggedID, bool bDelivered, GameObjectManager* pManager )
            {
                const ImVec2          rowMin = ImGui::GetItemRectMin();
                const ImVec2          rowMax = ImGui::GetItemRectMax();
                const SiblingDropZone zone   = EditorSiblingOrder::computeDropZone( EditorInputGUI::readPointer()._position._y, rowMin.y, rowMax.y );
                drawDropIndicator( zone, rowMin, rowMax );
                if ( bDelivered == false )
                    return;

                GameObject* const pDragged = pManager->findGameObjectByID( draggedID );
                if ( zone == SiblingDropZone::Onto )
                    EditorSceneCommands::reparent( pDragged, pTarget );
                else
                    (void)EditorSiblingOrder::moveBeside( *pManager, pDragged, pTarget, zone == SiblingDropZone::After, findEditorCameraObject() ); // 실패는 아무것도 바꾸지 않는다
            }

            static void drawGameObjectDragDrop( GameObject* pObj, GameObjectManager* pManager )
            {
                if ( pObj == nullptr || pManager == nullptr )
                    return;

                // 끌기는 `{GameObject, id}` 한 형식이다 — 씬 뷰 · 인스펙터 칸도 같은 종류로 받는다.
                (void)EditorInputGUI::beginDragSource( EditorDragPayload{ EditorPayloadKind::GameObject, pObj->getObjectID() }, pObj->getName().c_str() ); // 시작했는지는 쓰지 않는다

                EditorDragPayload payload{};
                bool              bDelivered = false;
                if ( EditorInputGUI::previewDrop( EditorPayloadKindSet{ EditorPayloadKind::GameObject }, payload, bDelivered ) )
                    handleHierarchyDrop( pObj, payload._id, bDelivered, pManager );

                string droppedAssetPath;
                if ( EditorWidgets::acceptAssetDrop( droppedAssetPath ) )
                    EditorAssetCommands::spawnPrefab( pManager, droppedAssetPath.c_str(), pObj );
            }

            /** @brief 컴포넌트 행의 맥락 메뉴입니다(명령 등록부의 컴포넌트 메뉴 — `EditorSceneObjectCommands.cpp`). */
            static void drawComponentContextMenu( Component* pComp )
            {
                EditorContextMenuSubject subject{};
                subject._pPopupID                = "CompCtx";
                subject._kind                    = EditorCommandTargetKind::Component;
                subject._hoveredID               = pComp->getComponentID();
                const uint64 selectedComponentID = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getSelectedComponentID();
                if ( selectedComponentID != 0 )
                    subject._listSelectedID.push_back( selectedComponentID );
                (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
            }

            /** @brief "Add Component" 하위 메뉴의 안입니다(맥락 메뉴 명령 `scene.object.addComponent` 가 부른다). */
            static void drawAddComponentSubmenu( const EditorCommandTarget& target )
            {
                GameObject* pObj = EditorSceneObjectCommands::findTargetObject( target );
                if ( pObj == nullptr )
                    return;
                bool bFailed{ false };
                (void)EditorComponentMenu::drawAddComponentList( pObj, getRowState()._addComponentSearch, "hierarchy.addComponent", false, bFailed ); // 추가했는지는 되돌리기가 기록하고 실패는 목록이 알린다
            }

            static bool hasSingleTargetObject( const EditorCommandTarget& target ) { return EditorSceneObjectCommands::findTargetObject( target ) != nullptr; }

            /** @brief 이름 바꾸기를 요청합니다(맥락 메뉴 · F2). 계층 창이 다음 프레임에 그 행의 입력 칸을 엽니다. */
            static void requestRename( const EditorCommandTarget& target )
            {
                const GameObject* pObj = EditorSceneObjectCommands::findTargetObject( target );
                if ( pObj != nullptr )
                    getRowState()._pendingRenameID = pObj->getObjectID();
            }

            /** @brief 맥락 메뉴의 대상과 같은 선택을 만듭니다(R2 — 선택 밖 행에서 열면 그 행만 고른다). */
            static void selectOnlyObject( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObj     = pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
                if ( pObj != nullptr )
                    editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().selectGameObject( pObj, SelectionMode::Replace );
            }

            /** @brief 메인 선택의 오브젝트 id 들입니다(맥락 메뉴의 대상 스냅숏). */
            static vector<uint64> collectSelectedObjectIDs()
            {
                vector<GameObject*> listSelected;
                editor::getMainSceneSelection().getSelectedObjects( listSelected );
                vector<uint64> listID;
                listID.reserve( listSelected.size() );
                for ( const GameObject* pSelected : listSelected )
                {
                    listID.push_back( pSelected->getObjectID() );
                }
                return listID;
            }

            /** @brief 이 오브젝트의 컴포넌트 종류와 태그마다 "그것을 가진 오브젝트 모두 선택" 을 고르는 하위 메뉴의 안입니다(맥락 메뉴 명령 `scene.object.selectAllWith`). */
            static void drawSelectSameSubmenu( const EditorCommandTarget& target )
            {
                GameObject*        pObj     = EditorSceneObjectCommands::findTargetObject( target );
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pObj == nullptr || pManager == nullptr )
                    return;
                vector<GameObject*> listMatch;
                for ( const Component* pComponent : pObj->getComponents() )
                {
                    const TypeInfo* pType = pComponent != nullptr ? pComponent->getTypeInfo() : nullptr;
                    if ( pType == nullptr || ImGui::MenuItem( pType->_name.c_str() ) == false )
                        continue;
                    EditorSceneCommands::collectObjectsWithComponent( *pManager, pType, listMatch );
                    (void)EditorSceneCommands::selectObjects( listMatch ); // 이 오브젝트가 늘 들어 있어 0 이 아니다
                }
                const vector<TagID>& listTag = pObj->getTags().getTags();
                if ( listTag.empty() == false )
                    ImGui::Separator();
                for ( const TagID tag : listTag )
                {
                    fixed_string<constant::kMaxBuffer128> label;
                    formatstring( label.data(), label.capacity(), "Tag %s", tag.getString() );
                    if ( ImGui::MenuItem( label.c_str() ) == false )
                        continue;
                    EditorSceneCommands::collectObjectsWithTag( *pManager, tag, listMatch );
                    (void)EditorSceneCommands::selectObjects( listMatch ); // 이 오브젝트가 늘 들어 있어 0 이 아니다
                }
            }

            /**
             * @brief 오브젝트 행의 맥락 메뉴입니다. 항목은 명령 등록부의 오브젝트 메뉴(`EditorSceneObjectCommands.cpp` 와 아래 계층 창 항목)이고, 대상은 선택 전체다(R3).
             * @details 선택 밖 행에서 열면 그 행만 고른다(R2). 열기 · 대상 · 빈 팝업 규칙은 `EditorInputGUI::drawContextMenuFor` 한 곳이 정한다.
             */
            static void drawGameObjectContextMenu( GameObject* pObj )
            {
                EditorContextMenuSubject subject{};
                subject._pPopupID       = "GOCtx";
                subject._kind           = EditorCommandTargetKind::GameObject;
                subject._hoveredID      = pObj->getObjectID();
                subject._listSelectedID = collectSelectedObjectIDs();
                subject._selectOnly     = &selectOnlyObject;
                (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
            }

            static void drawSceneComponentNode( GameObject* pObj, SceneComponent* pSceneComp, GameObjectManager* pManager )
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();

                if ( pObj == nullptr || pSceneComp == nullptr )
                    return;

                ImGui::PushID( static_cast<int32>( pSceneComp->getComponentID() ) );

                EditorWorkspace& ws        = editorContext.getSubsystem<EditorWorkspace>();
                const bool       bSelected = ( ws.getSelectedObjectID() == pObj->getObjectID() &&
                                         ws.getSelectedComponentID() == pSceneComp->getComponentID() );

                const utf8* pCompName = pSceneComp->getComponentName().empty() == false
                                          ? pSceneComp->getComponentName().c_str()
                                          : "SceneComponent";

                // 행 앞에 컴포넌트 아이콘을 그린다. 언리얼의 Outliner 나 Godot 의 씬 트리처럼 종류를 한눈에 알 수 있다
                const utf8*                           pIcon = EditorComponentIcon::findRow( pSceneComp->getTypeInfo() )._pGlyph;
                fixed_string<constant::kMaxBuffer256> arrLabel;
                formatstring( arrLabel.data(), arrLabel.capacity(), "%# %###sc%#", pIcon, pCompName, pSceneComp->getComponentID() );

                bool                           hasChildOnOwner{ false };
                const vector<SceneComponent*>& listChild = pSceneComp->getChildren();
                for ( SceneComponent* pChild : listChild )
                {
                    if ( pChild != nullptr && pChild->getOwner() == pObj )
                    {
                        hasChildOnOwner = true;
                        break;
                    }
                }

                const ImGuiTreeNodeFlags flags =
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                    ( bSelected ? ImGuiTreeNodeFlags_Selected : 0 ) | ( hasChildOnOwner ? 0 : ImGuiTreeNodeFlags_Leaf );

                const bool bOpen = ImGui::TreeNodeEx( arrLabel.c_str(), flags );
                if ( EditorInputGUI::wasItemClicked() )
                    ws.selectComponent( pObj, pSceneComp );
                drawComponentContextMenu( pSceneComp );

                if ( bOpen )
                {
                    for ( SceneComponent* pChild : listChild )
                    {
                        if ( pChild != nullptr && pChild->getOwner() == pObj )
                            drawSceneComponentNode( pObj, pChild, pManager );
                    }
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            /** @brief 행 앞의 눈(에디터에서만 숨김)과 자물쇠(뷰포트 선택 잠금) 토글을 그립니다. */
            static void drawRowToggles( EditorContext* pContext, GameObject* pObj, uint64 objectID )
            {
                // 1) 눈 아이콘은 에디터에서만 숨긴다. 저장, 되돌리기, 게임 동작에 남지 않는다(언리얼과 유니티). 활성 비트는 인스펙터의 Active 체크박스다.
                const bool bVisible = pObj->isHiddenInEditor() == false;
                if ( EditorWidgets::drawToggleIconButton( "##visible", bVisible, editoricon::kEye, editoricon::kEyeSlash, SW_LOCKEY( "EditorScene", "VisibleInEditor" ),
                                                          SW_LOCKEY( "EditorScene", "HiddenInEditor" ) ) )
                    EditorSceneCommands::setHiddenInEditor( pObj, bVisible );
                EditorSelfTestMarks::note( "hierarchy.activeToggle" );
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::note( ( string( "hierarchy.toggle." ) + pObj->getName().c_str() ).c_str() );
                ImGui::SameLine();
                // 2) 자물쇠 아이콘은 뷰포트에서 선택하지 못하게 한다. Hierarchy 에서는 선택할 수 있다. 저장하지 않는다.
                const bool bLocked = pContext->getSubsystem<EditorWorkspace>().isObjectLocked( objectID );
                if ( EditorWidgets::drawToggleIconButton( "##lock", bLocked, editoricon::kLock, editoricon::kUnlock, SW_LOCKEY( "EditorScene", "LockedInViewport" ),
                                                          SW_LOCKEY( "EditorScene", "PickableInViewport" ) ) )
                    pContext->getSubsystem<EditorWorkspace>().setObjectLocked( objectID, bLocked == false );
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::note( ( string( "hierarchy.lock." ) + pObj->getName().c_str() ).c_str() );
                ImGui::SameLine();
            }

            /** @brief 행 라벨(아이콘, 이름, 카테고리 배지, 오브젝트 id 꼬리)을 만듭니다. */
            static void buildObjectLabel( const GameObject* pObj, uint64 objectID, fixed_string<constant::kMaxBuffer256>& outLabel )
            {
                // 배지는 리플렉션 Category 에서 가져온다. 위의 컴포넌트 추가 메뉴가 이미 쓰는 데이터다.
                // 타입 이름을 비교하면 게임이 추가한 컴포넌트는 배지가 없다.
                string badgeStr;
                for ( const Component* pComp : pObj->getComponents() )
                {
                    if ( pComp == nullptr )
                        continue;
                    const TypeInfo* pTypeInfo = pComp->getTypeInfo();
                    if ( pTypeInfo == nullptr )
                        continue;
                    EditorUtil::appendCategoryBadge( pTypeInfo->getCategory(), badgeStr );
                }

                // 오브젝트 아이콘은 빌보드 종류 컴포넌트(라이트, 카메라, 오디오)가 있으면 그 아이콘이고, 없으면 기본 오브젝트 아이콘이다.
                const utf8* pIcon = EditorSiblingOrder::isFolder( *pObj ) ? editoricon::kFolder : EditorComponentIcon::findObjectRow( *pObj )._pGlyph;
                if ( badgeStr.empty() == false )
                    formatstring( outLabel.data(), outLabel.capacity(), "%# %# %###go%#", pIcon, pObj->getName().c_str(), badgeStr.c_str(), objectID );
                else
                    formatstring( outLabel.data(), outLabel.capacity(), "%# %###go%#", pIcon, pObj->getName().c_str(), objectID );
            }

            /** @brief 방금 그린 행의 클릭을 선택으로 옮깁니다. Shift 는 지난 프레임에 그린 순서로 범위, Ctrl 은 토글입니다. */
            static void handleRowClick( EditorContext* pContext, GameObject* pObj, GameObjectManager* pManager, RowState& rowState, uint64 objectID )
            {
                if ( EditorInputGUI::wasItemClicked() == false )
                    return;

                const uint8 modifierMask    = EditorInputGUI::getModifierMask();
                const bool  bCtrl           = ( modifierMask & ModifierKey::Ctrl ) != 0;
                const bool  bShift          = ( modifierMask & ModifierKey::Shift ) != 0;
                rowState._bClickedThisFrame = true;
                if ( bShift == false || rowState._anchorObjectID == 0 )
                {
                    pContext->getSubsystem<EditorWorkspace>().selectGameObject( pObj, bCtrl ? SelectionMode::Toggle : SelectionMode::Replace );
                    rowState._anchorObjectID = objectID;
                    return;
                }

                // Shift 는 시작 행부터 이 행까지(지난 프레임에 그린 순서) 모두 선택한다. 유니티, 언리얼, 윈도 탐색기와 같다.
                const vector<uint64>& listRow     = rowState._listPreviousRow;
                size_t                anchorIndex = listRow.size();
                size_t                clickIndex  = listRow.size();
                for ( size_t rowIndex = 0; rowIndex < listRow.size(); ++rowIndex )
                {
                    if ( listRow[rowIndex] == rowState._anchorObjectID )
                        anchorIndex = rowIndex;
                    if ( listRow[rowIndex] == objectID )
                        clickIndex = rowIndex;
                }
                if ( anchorIndex >= listRow.size() || clickIndex >= listRow.size() )
                {
                    pContext->getSubsystem<EditorWorkspace>().selectGameObject( pObj, SelectionMode::Add );
                    return;
                }

                const size_t first = MathUtil::min( anchorIndex, clickIndex );
                const size_t last  = MathUtil::max( anchorIndex, clickIndex );
                if ( bCtrl == false )
                    editor::getMainSceneSelection().clearObjectSelection();
                for ( size_t rowIndex = first; rowIndex <= last; ++rowIndex )
                {
                    GameObject* pRow = pManager->findGameObjectByID( listRow[rowIndex] );
                    if ( pRow != nullptr )
                        pContext->getSubsystem<EditorWorkspace>().selectGameObject( pRow, SelectionMode::Add );
                }
                pContext->getSubsystem<EditorWorkspace>().selectGameObject( pObj, SelectionMode::Add ); // 누른 행이 주 선택이다
            }

            /** @brief 그 행에서 직접 이름을 바꾸는 입력입니다. 엔터나 포커스를 잃으면 확정하고 Esc 는 취소합니다. */
            static void drawInlineRename( GameObject* pObj, uint64 objectID, uint64& renamingObjectID, fixed_string<constant::kMaxBuffer256>& renameBuffer,
                                          bool& bFocusRenameInput )
            {
                if ( renamingObjectID != objectID || EditorPlaySession::areSceneEditsAllowed() == false )
                    return;

                ImGui::SameLine();
                EditorChrome::setNextItemWidthColumns( 21.0f );
                if ( bFocusRenameInput )
                {
                    ImGui::SetKeyboardFocusHere();
                    bFocusRenameInput = false;
                }
                if ( ImGui::InputText( "##InlineRename", renameBuffer.data(), renameBuffer.capacity(),
                                       ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll ) )
                {
                    (void)EditorSceneCommands::rename( pObj, renameBuffer.c_str() ); // 빈 이름은 거절하므로 이름이 그대로 남는다
                    renamingObjectID = 0;
                }
                const bool bCanceled = EditorInputGUI::wasCancelPressed();
                if ( ImGui::IsItemDeactivated() && bCanceled == false )
                {
                    (void)EditorSceneCommands::rename( pObj, renameBuffer.c_str() ); // 빈 이름은 거절하므로 이름이 그대로 남는다
                    renamingObjectID = 0;
                }
                if ( bCanceled )
                    renamingObjectID = 0;
            }

            /** @brief 씬 컴포넌트가 아닌 컴포넌트 한 줄을 그립니다. */
            static void drawPlainComponentRow( EditorContext* pContext, GameObject* pObj, Component* pComp, GameObjectManager* /*pManager*/ )
            {
                ImGui::PushID( static_cast<int32>( pComp->getComponentID() ) );

                EditorWorkspace& ws            = pContext->getSubsystem<EditorWorkspace>();
                const bool       bCompSelected = ( ws.getSelectedObjectID() == pObj->getObjectID() &&
                                             ws.getSelectedComponentID() == pComp->getComponentID() );

                const utf8* pCompName = pComp->getComponentName().empty() == false
                                          ? pComp->getComponentName().c_str()
                                          : "Component";

                fixed_string<constant::kMaxBuffer256> arrCompLabel;
                formatstring( arrCompLabel.data(), arrCompLabel.capacity(), "%###c%#", pCompName, pComp->getComponentID() );

                if ( ImGui::Selectable( arrCompLabel.c_str(), bCompSelected ) )
                    ws.selectComponent( pObj, pComp );
                drawComponentContextMenu( pComp );

                ImGui::PopID();
            }

            /** @brief 펼친 오브젝트의 자식 오브젝트(형제 순서)와 컴포넌트 줄을 그립니다. */
            static void drawOpenNodeChildren( EditorContext* pContext, GameObject* pObj, GameObjectManager* pManager,
                                              uint64& renamingObjectID, fixed_string<constant::kMaxBuffer256>& renameBuffer, bool& bFocusRenameInput )
            {
                vector<GameObject*> listChild;
                EditorSiblingOrder::collectChildrenInOrder( *pObj, listChild );
                for ( GameObject* pChild : listChild )
                {
                    drawGameObjectNode( pChild, pManager, renamingObjectID, renameBuffer,
                                        bFocusRenameInput );
                }

                const ComponentList& listComponent = pObj->getComponents();
                for ( Component* pComp : listComponent )
                {
                    if ( pComp == nullptr )
                        continue;

                    // 노드마다 프레임마다 확인하는 곳이다. 그래서 리플렉션 캐스트 대신 생성자에서 설정한 비트를 본다.
                    if ( pComp->isSceneComponent() )
                    {
                        SceneComponent*       pSceneComp    = static_cast<SceneComponent*>( pComp );
                        const SceneComponent* pParent       = pSceneComp->getParent();
                        const bool            bRootOnThisGo = pParent == nullptr || pParent->getOwner() != pObj;
                        if ( bRootOnThisGo )
                            drawSceneComponentNode( pObj, pSceneComp, pManager );
                        continue;
                    }

                    drawPlainComponentRow( pContext, pObj, pComp, pManager );
                }
            }

            static void drawGameObjectNode( GameObject* pObj, GameObjectManager* pManager,
                                            uint64& renamingObjectID, fixed_string<constant::kMaxBuffer256>& renameBuffer,
                                            bool& bFocusRenameInput )
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();

                const bool bSkipRow = pObj == nullptr || pManager == nullptr || isRowVisible( pObj ) == false;
                if ( bSkipRow )
                    return;

                const uint64 objectID  = pObj->getObjectID();
                const bool   bSelected = editor::getMainSceneSelection().hasObject( pObj );

                ImGui::PushID( static_cast<int32>( objectID ) );

                drawRowToggles( &editorContext, pObj, objectID );

                fixed_string<constant::kMaxBuffer256> arrLabel;
                buildObjectLabel( pObj, objectID, arrLabel );

                const bool bHasChildGos   = pObj->hasChildren();
                const bool bHasComponents = pObj->getComponentCount() > 0;
                const bool bLeaf          = ( bHasChildGos == false && bHasComponents == false );

                // 뷰포트에서 선택한 오브젝트는 조상을 펼쳐 그 행까지 보여 준다. 언리얼 Outliner 의 선택 따라가기에 해당한다.
                RowState&         rowState      = getRowState();
                const GameObject* pRevealTarget = rowState._revealObjectID != 0 ? pManager->findGameObjectByID( rowState._revealObjectID ) : nullptr;
                if ( pRevealTarget != nullptr && isAncestorOf( pObj, pRevealTarget ) )
                    ImGui::SetNextItemOpen( true );
                // 프리팹 인스턴스는 이름을 파랗게 표시한다(유니티 Hierarchy).
                const bool bPrefabInstance = editorContext.getSubsystem<EditorWorkspace>().findPrefabPath( pObj ).empty() == false;
                if ( bPrefabInstance )
                    ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( HierarchyPanelInternal::kPrefabInstanceText ) );
                // SpanAvailWidth 를 쓰면 선택 배경과 클릭 영역이 앞의 가시성 토글 오른쪽부터 시작한다. SpanFullWidth 는 창 왼쪽부터 칠해서 토글을 덮는다.
                const bool bOpen = ImGui::TreeNodeEx(
                    arrLabel.c_str(),
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                        ( bSelected ? ImGuiTreeNodeFlags_Selected : 0 ) | ( bLeaf ? ImGuiTreeNodeFlags_Leaf : 0 ) );
                if ( bPrefabInstance )
                    ImGui::PopStyleColor();
                rowState._listDrawnRow.push_back( objectID );
                if ( pRevealTarget == pObj )
                {
                    ImGui::SetScrollHereY( 0.5f );
                    rowState._revealObjectID = 0;
                }
                if ( bSelected )
                    EditorSelfTestMarks::note( "hierarchy.selectedRow" ); // 시나리오가 오른쪽 클릭으로 오브젝트 메뉴를 연다
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::note( ( string( "hierarchy.row." ) + pObj->getName().c_str() ).c_str() );

                handleRowClick( &editorContext, pObj, pManager, rowState, objectID );
                drawInlineRename( pObj, objectID, renamingObjectID, renameBuffer, bFocusRenameInput );

                drawGameObjectContextMenu( pObj );
                drawGameObjectDragDrop( pObj, pManager );

                if ( bOpen )
                {
                    drawOpenNodeChildren( &editorContext, pObj, pManager, renamingObjectID, renameBuffer, bFocusRenameInput );
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( HierarchyPanel, HierarchyPanel::kPanelID, EditorPanelCategory::Core, 100 );
    SW_EDITOR_COMMAND( SceneMoveSiblingUp, "scene.moveSiblingUp", 2420, "Move Up in Hierarchy", editoricon::kGameObject, "Scene", "고른 오브젝트를 형제 사이에서 한 칸 위로 옮깁니다",
                       "Move the selected object up among its siblings", nullptr, &HierarchyPanelInternal::runMoveUp, nullptr, Scene, "MainMenu/Edit", None, 0 );
    SW_EDITOR_COMMAND( SceneMoveSiblingDown, "scene.moveSiblingDown", 2421, "Move Down in Hierarchy", editoricon::kGameObject, "Scene",
                       "고른 오브젝트를 형제 사이에서 한 칸 아래로 옮깁니다", "Move the selected object down among its siblings", nullptr, &HierarchyPanelInternal::runMoveDown, nullptr, Scene,
                       "MainMenu/Edit", None, 0 );
    SW_EDITOR_COMMAND( SceneCreateFolder, "scene.createFolder", 2422, "Create Folder", editoricon::kFolder, "Scene",
                       "고른 오브젝트 아래(없으면 루트)에 정리용 폴더를 만듭니다", "Create an organizing folder under the selection", nullptr, &HierarchyPanelInternal::runCreateFolder, nullptr, Scene,
                       "MainMenu/Edit", None, 0 );
    // 계층 창이 그리는 오브젝트 메뉴 항목(하위 메뉴 · 이름 바꾸기) — 같은 종류(GameObject)의 메뉴라 씬 뷰에서 열어도 나온다(R5).
    SW_EDITOR_CONTEXT_COMMAND( ObjectRename, "scene.object.rename", "Rename", editoricon::kGameObject, "Scene", GameObject, 330, "scene.object.rename",
                               &HierarchyPanelInternal::requestRename, &HierarchyPanelInternal::hasSingleTargetObject, Scene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ObjectAddComponent, "scene.object.addComponent", "Add Component", editoricon::kPlus, "Scene", GameObject, 400, nullptr, nullptr,
                               &HierarchyPanelInternal::hasSingleTargetObject, Scene, &HierarchyPanelInternal::drawAddComponentSubmenu );
    SW_EDITOR_CONTEXT_COMMAND( ObjectSelectAllWith, "scene.object.selectAllWith", "Select All With", editoricon::kGameObject, "Scene", GameObject, 410, nullptr, nullptr,
                               &HierarchyPanelInternal::hasSingleTargetObject, NoScene, &HierarchyPanelInternal::drawSelectSameSubmenu );
    SW_AUTOMATION_WATCH( editorHierarchyRowIndex, "Editor.HierarchyRowIndex", "Row index (from 0) of the object named by gv_editorWatchHierarchyObject in the last Hierarchy frame (-1 when not drawn)",
                         &HierarchyPanelInternal::readProbedRowIndex );
    SW_AUTOMATION_WATCH( editorHierarchyRowRebuildCount, "Editor.HierarchyRowRebuildCount",
                         "Times the Hierarchy row model was rebuilt (roots and filter matches); rises only when the scene structure, tags or filter text change",
                         &HierarchyPanelInternal::readRowRebuildCount );

    HierarchyPanel::HierarchyPanel()
        : _renamingObjectID{ 0 }
        , _lastDrawnRootID{ 0 }
        , _filterBuffer{}
        , _renameBuffer{}
        , _visibleRootCount{ 0 }
        , _drawnRootCount{ 0 }
        , _bFocusRenameInput{ false }
        , _bSkipOffscreenRows{ true }
    {
    }

    void HierarchyPanel::drawContent()
    {
        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorScene", "HierarchyNoActiveScene" ) );
            return;
        }

        GameObjectManager* pManager = pScene->getObjectManager();

        HierarchyPanelInternal::beginRowFrame();
        // 이름 바꾸기 명령(맥락 메뉴 · F2)이 남긴 요청을 그 행의 입력 칸으로 연다.
        uint64& pendingRenameID = HierarchyPanelInternal::getRowState()._pendingRenameID;
        if ( pendingRenameID != 0 )
        {
            const GameObject* pRenamed = pManager->findGameObjectByID( pendingRenameID );
            if ( pRenamed != nullptr )
            {
                _renamingObjectID = pendingRenameID;
                formatstring( _renameBuffer.data(), _renameBuffer.capacity(), "%#", pRenamed->getName().c_str() );
                _bFocusRenameInput = true;
            }
            pendingRenameID = 0;
        }
        drawToolbar( pManager );
        // 행 모델은 툴바 뒤에 맞춘다 — 같은 프레임에 검색창에서 바뀐 글 · 만들기 단추가 바꾼 구조가 이 프레임 트리에 바로 닿는다(앞에서 맞추면 한 프레임 늦다).
        HierarchyPanelInternal::refreshRowModel( *pManager, _filterBuffer.view() );

        ImGui::Separator();

        editor::EditorSectionDesc treeDesc{};
        treeDesc._pID  = "##HierarchyTree";
        treeDesc._kind = editor::EditorSectionKind::Child;
        if ( EditorChrome::beginSection( treeDesc ) )
        {
            drawRootRows( pScene, pManager );
            drawTreeEmptyArea( pManager );
        }
        EditorChrome::endSection();
    }

    void HierarchyPanel::drawToolbar( GameObjectManager* pManager )
    {
        // 상단 툴바에는 생성 버튼과 검색창이 있다
        if ( EditorChrome::beginToolbar( "##HierarchyToolbar" ) )
        {
            const bool bEditsAllowed = EditorPlaySession::areSceneEditsAllowed();
            if ( bEditsAllowed == false )
            {
                EditorWidgets::drawChip( "Play Mode", EditorThemeUtil::getWarningColor() );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "PlayModeRunning" ) );
                ImGui::SameLine();
            }
            if ( bEditsAllowed == false )
                ImGui::BeginDisabled();
            if ( ImGui::Button( "+ Create" ) )
                EditorSceneCommands::create( pManager, nullptr );
            EditorSelfTestMarks::note( "hierarchy.create" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "CreateGameObject" ) );
            if ( bEditsAllowed == false )
                ImGui::EndDisabled();

            ImGui::SameLine();
            EditorWidgets::drawSearchField( "##HierarchyFilter", _filterBuffer,
                                            SW_LOCKEY( "EditorScene", "HierarchySearchHint" ), 0.0f, false );
            EditorSelfTestMarks::note( "hierarchy.filter" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "HierarchySearch" ) );
        }
        EditorChrome::endToolbar();
    }

    void HierarchyPanel::drawRootRows( Scene* pScene, GameObjectManager* pManager )
    {
        // `drawGameObjectNode` 는 서브트리가 필터에 걸리지 않으면 조용히 빠진다. 그래서 같은 술어(행 모델)로
        // 미리 세어 두고, 전부 빠질 때는 빈 상자 대신 이유를 보여 준다.
        const EditorListFilter treeFilter{ _filterBuffer.c_str() };
        uint32                 visibleRootCount{ 0 };
        uint32                 drawnRootCount{ 0 };
        uint64                 lastDrawnRootID{ 0 };

        // 화면 밖의 접힌 루트는 그리지 않고 같은 높이의 빈 공간만 둔다. 스크롤 막대와 다음 행의 위치는 그대로다. 모두 그리면 큐브 8000개 씬에서 노드 8000개
        // (버튼, 배지 문자열, 트리 노드, 드래그 앤 드롭)를 프레임마다 그린다. 이어진 빈 행은 빈 공간 하나로 합친다.
        // 접힌 루트 행의 높이는 프레임 높이(버튼, 트리 노드)에 행 간격을 더한 값이다.
        const float32 rowStride        = ImGui::GetFrameHeightWithSpacing();
        const float32 rowSpacing       = rowStride - ImGui::GetFrameHeight();
        uint32        pendingSkipRow   = 0;
        float32       nextRowY         = ImGui::GetCursorScreenPos().y;
        auto          flushSkippedRows = [&pendingSkipRow, rowStride, rowSpacing]()
        {
            if ( pendingSkipRow == 0 )
                return;
            ImGui::Dummy( ImVec2( 0.0f, static_cast<float32>( pendingSkipRow ) * rowStride - rowSpacing ) );
            pendingSkipRow = 0;
        };
        // 화면을 그리는 에디터 카메라는 씬 오브젝트지만 편집 대상이 아니다. 저장에서도 빠진다. 그래서 목록에 표시하지 않는다.
        const CameraComponent* pEditorCamera       = EditorCamera::find( pScene );
        const GameObject*      pEditorCameraObject = pEditorCamera != nullptr ? pEditorCamera->getOwner() : nullptr;
        for ( GameObject* pObj : HierarchyPanelInternal::getRowState()._rowModel._listRoot )
        {
            if ( pObj == pEditorCameraObject || HierarchyPanelInternal::isRowVisible( pObj ) == false )
                continue;
            ++visibleRootCount;
            const bool bSkip = _bSkipOffscreenRows && HierarchyPanelInternal::canSkipRootRow( pObj, _renamingObjectID, nextRowY, ImGui::GetFrameHeight() );
            if ( bSkip )
            {
                ++pendingSkipRow;
                nextRowY += rowStride;
                continue;
            }
            flushSkippedRows();
            HierarchyPanelInternal::drawGameObjectNode( pObj, pManager, _renamingObjectID, _renameBuffer, _bFocusRenameInput );
            nextRowY = ImGui::GetCursorScreenPos().y;
            ++drawnRootCount;
            lastDrawnRootID = pObj->getObjectID();
        }
        flushSkippedRows();
        _drawnRootCount  = drawnRootCount;
        _lastDrawnRootID = lastDrawnRootID;

        _visibleRootCount = visibleRootCount;
        if ( visibleRootCount == 0 && treeFilter.isActive() )
            EditorWidgets::drawNoSearchResultHint( treeFilter.getText() );
    }

    void HierarchyPanel::drawTreeEmptyArea( GameObjectManager* pManager )
    {
        // 빈 영역에 오브젝트를 놓으면 루트로 떼고, 에셋을 놓으면 루트에 스폰한다(끌기는 `{종류, id}` 한 형식).
        EditorDragPayload objectPayload{};
        if ( EditorInputGUI::acceptDrop( EditorPayloadKindSet{ EditorPayloadKind::GameObject }, objectPayload ) )
        {
            GameObject* const pDragged = pManager->findGameObjectByID( objectPayload._id );
            if ( pDragged != nullptr && pDragged->getParent() != nullptr )
                EditorSceneCommands::unparent( pDragged, "Detach GameObject to Root" );
        }
        string droppedAssetPath;
        if ( EditorWidgets::acceptAssetDrop( droppedAssetPath ) )
            EditorAssetCommands::spawnPrefab( pManager, droppedAssetPath.c_str(), nullptr );

        handleHierarchyShortcuts( pManager );
        // 빈 곳 메뉴 — 같은 오브젝트 메뉴를 선택 그대로 연다(R2: 빈 곳은 선택을 바꾸지 않는다, R3: 대상은 선택 전체). 만들기 · 붙여 넣기는 선택이 없어도 켜진다.
        EditorContextMenuSubject subject{};
        subject._pPopupID       = "HierarchyEmptyCtx";
        subject._kind           = EditorCommandTargetKind::GameObject;
        subject._listSelectedID = HierarchyPanelInternal::collectSelectedObjectIDs();
        (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
    }

    void HierarchyPanel::handleHierarchyShortcuts( GameObjectManager* pManager )
    {
        // 복제 · 복사 · 붙여 넣기 · 지우기 · 이름 바꾸기는 명령이다(`editor.input.xml` 의 `Editor.Panel.hierarchy` 레이어 — 이 창이 포커스일 때만 켜진다).
        // 여기는 행 목록을 아는 이 창만의 액션(모두 선택 · 위아래 이동)을 읽는다.
        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_ChildWindows ) == false )
            return;
        // 메인 월드를 보여 주는 창이 포커스다 — 인스펙터 · 전역 명령이 다시 메인 선택을 겨눈다(미리보기 창이 등록해 둔 것을 해제한다).
        editor::setFocusedSelection( nullptr );
        EditorSelection&                  editorSelection = editor::getMainSceneSelection();
        HierarchyPanelInternal::RowState& rowState        = HierarchyPanelInternal::getRowState();
        const vector<uint64>&             listRow         = rowState._listPreviousRow;

        if ( EditorInputGUI::wasActionTriggered( HierarchyPanelInternal::kActionSelectAll ) )
        {
            // 지난 프레임에 보인 행을 모두 선택한다. 접힌 자식은 빠진다. 유니티와 같다.
            editorSelection.clearObjectSelection();
            for ( const uint64 rowID : listRow )
            {
                GameObject* pRow = pManager->findGameObjectByID( rowID );
                if ( pRow != nullptr )
                    editorSelection.selectObject( pRow, SelectionMode::Add );
            }
            return;
        }
        const bool bUp   = EditorInputGUI::wasActionTriggered( HierarchyPanelInternal::kActionSelectPrevious );
        const bool bDown = EditorInputGUI::wasActionTriggered( HierarchyPanelInternal::kActionSelectNext );
        if ( ( bUp || bDown ) == false || listRow.empty() )
            return;
        const GameObject* pPrimary = editorSelection.getPrimaryObject();
        size_t            current  = listRow.size();
        for ( size_t rowIndex = 0; pPrimary != nullptr && rowIndex < listRow.size(); ++rowIndex )
        {
            if ( listRow[rowIndex] == pPrimary->getObjectID() )
                current = rowIndex;
        }
        const size_t next  = current >= listRow.size() ? 0 : ( bUp ? ( current > 0 ? current - 1 : 0 ) : MathUtil::min( current + 1, listRow.size() - 1 ) );
        GameObject*  pNext = pManager->findGameObjectByID( listRow[next] );
        if ( pNext == nullptr )
            return;
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().selectGameObject( pNext, SelectionMode::Replace );
        rowState._anchorObjectID    = pNext->getObjectID();
        rowState._bClickedThisFrame = true; // 키로 선택한 것이므로 펼쳐 보이기 대상이 아니다
    }
} // namespace sw::editor
