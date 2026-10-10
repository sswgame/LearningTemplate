#include "pch.h"

#include "Editor/Panels/HierarchyPanel.h"

#include "Core/Common/StdHeaders.h"
#include "Core/String/TagID.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorCamera.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct HierarchyPanelInternal
        {
            static constexpr const utf8* kHierarchyGoPayload = "SW_HIERARCHY_GO";

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
                    // ID 의 정본은 `TagID::computeId` 다. 주의: `computeHash64` 를 기본 인자로 부르면 대소문자를 무시해
                    // 태그 쪽(구별한다)과 어긋나고, 대문자로 시작하는 태그를 **하나도 찾지 못한다.**
                    //
                    // 문자열도 함께 넘긴다. `isSubtagOf` 가 그것을 보므로 `tag:Faction` 이 `Faction.Player` 까지
                    // 잡는다. 리터럴 태그는 역조회 표에 등록되지 않아, ID 만 든 TagID 로는 계층 비교를 할 수
                    // 없다. 필터 버퍼는 이 호출 동안 살아 있다.
                    const string_view tagFilter{ pFilter + 4 };
                    return pObj->hasTag( TagID{ TagID::computeId( tagFilter.data(), tagFilter.size() ), tagFilter.data() } );
                }

                // 3) 일반 이름 매칭
                return EditorListFilter{ pFilter }.matches( pObj->getName().view() );
            }

            /**
             * @brief 이 루트 줄을 그리지 않고 자리만 둘 수 있는지 봅니다 — 접혀 있고, 이름을 바꾸는 중이 아니고, 줄이 창의 보이는 범위 밖일 때.
             * @details 트리 노드의 열림 상태는 drawGameObjectNode 와 같은 ID 로 읽는다(PushID( 오브젝트 id ) 안의 "###go<id>"). @p cursorY 는 이 줄이 놓일 화면 y 다.
             */
            static bool canSkipRootRow( const GameObject* pObj, uint64 renamingObjectId, float32 cursorY, float32 rowHeight )
            {
                const uint64 objectId = pObj->getObjectId();
                if ( objectId == renamingObjectId )
                    return false;
                const ImVec2 rowMin{ ImGui::GetCursorScreenPos().x, cursorY };
                if ( ImGui::IsRectVisible( rowMin, ImVec2( rowMin.x + 1.0f, cursorY + rowHeight ) ) )
                    return false;
                fixed_string<constant::kMaxBuffer64> nodeIdLabel;
                formatstring( nodeIdLabel.data(), nodeIdLabel.capacity(), "###go%#", objectId );
                ImGui::PushID( static_cast<int32>( objectId ) );
                const ImGuiID nodeId = ImGui::GetID( nodeIdLabel.c_str() );
                ImGui::PopID();
                return ImGui::TreeNodeGetOpen( nodeId ) == false;
            }

            static bool subtreeMatchesFilter( GameObject* pObj, const utf8* pFilter )
            {
                if ( pObj == nullptr )
                    return false;
                if ( nameMatchesFilter( pObj, pFilter ) )
                    return true;
                vector<GameObject*> listChild;
                pObj->getChildren( listChild );
                for ( GameObject* pChild : listChild )
                {
                    if ( subtreeMatchesFilter( pChild, pFilter ) )
                        return true;
                }
                return false;
            }

            static void handleHierarchyReparentDrop( GameObject* pTargetParent, const ImGuiPayload* pPayload,
                                                     GameObjectManager* pManager )
            {
                if ( pPayload == nullptr || pManager == nullptr || pTargetParent == nullptr )
                    return;
                if ( pPayload->DataSize != static_cast<int32>( sizeof( uint64 ) ) )
                    return;

                const uint64      draggedId = *static_cast<const uint64*>( pPayload->Data );
                GameObject* const pDragged  = pManager->findGameObjectById( draggedId );
                EditorSceneCommands::reparent( pDragged, pTargetParent );
            }

            static void drawGameObjectDragDrop( GameObject* pObj, GameObjectManager* pManager )
            {
                if ( pObj == nullptr || pManager == nullptr )
                    return;

                if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
                {
                    const uint64 id = pObj->getObjectId();
                    ImGui::SetDragDropPayload( kHierarchyGoPayload, &id, sizeof( id ) );
                    ImGui::TextUnformatted( pObj->getName().c_str() );
                    ImGui::EndDragDropSource();
                }

                if ( ImGui::BeginDragDropTarget() )
                {
                    const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( kHierarchyGoPayload );
                    if ( pPayload != nullptr )
                        handleHierarchyReparentDrop( pObj, pPayload, pManager );

                    string droppedAssetPath;
                    if ( EditorWidgets::tryAcceptAssetPayload( droppedAssetPath ) )
                        EditorAssetCommands::spawnPrefab( pManager, droppedAssetPath.c_str(), pObj );
                    ImGui::EndDragDropTarget();
                }
            }

            static void drawComponentContextMenu( GameObject* pObj, Component* pComp, GameObjectManager* pManager )
            {
                if ( ImGui::BeginPopupContextItem( "CompCtx" ) == false )
                    return;

                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                {
                    ImGui::EndPopup();
                    return;
                }

                if ( ImGui::MenuItem( "Select Owner GameObject" ) )
                    pContext->getWorkspace().selectGameObject( pObj );

                const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
                if ( bEditsAllowed == false )
                    ImGui::BeginDisabled();
                if ( ImGui::MenuItem( "Remove Component" ) && pComp != nullptr && pManager != nullptr )
                    EditorSceneCommands::destroyComponent( pManager, pObj, pComp );
                if ( bEditsAllowed == false )
                    ImGui::EndDisabled();

                ImGui::EndPopup();
            }

            static void drawAddComponentMenu( GameObject* pObj )
            {
                const bool bMenuOpen = ImGui::BeginMenu( "Add Component" );
                EditorSelfTestMarks::note( "hierarchy.addComponent" );
                if ( bMenuOpen == false )
                    return;

                vector<hashed_string> listType;
                if ( pObj != nullptr && pObj->getManager() != nullptr )
                    listType = pObj->getManager()->getRegisteredComponentTypeNames();
                if ( listType.empty() )
                {
                    ImGui::TextDisabled( "No registered component types." );
                    ImGui::EndMenu();
                    return;
                }

                static fixed_string<constant::kMaxBuffer64> s_searchBuf;
                ImGui::SetNextItemWidth( 180.0f * EditorThemeUtil::getDpiScale() );
                ImGui::InputTextWithHint( "##compSearch", "Search...", s_searchBuf.data(),
                                          s_searchBuf.capacity() );
                EditorSelfTestMarks::note( "hierarchy.addComponent.search" );
                const EditorListFilter compFilter{ s_searchBuf.c_str() };
                const bool             bHasFilter = compFilter.isActive();

                auto* pRegistry = editor::getService<TypeRegistry>();

                auto drawItem = [&]( const hashed_string& typeName, const TypeInfo* pTypeInfo )
                {
                    const utf8* pDisplayName = ( pTypeInfo != nullptr ) ? pTypeInfo->getDisplayName() : typeName.c_str();
                    if ( ImGui::MenuItem( pDisplayName ) )
                    {
                        if ( EditorSceneCommands::addComponent( pObj, typeName ) == nullptr )
                            ImGui::OpenPopup( "AddCompFailed" );
                    }
                    // 자동화 시나리오가 타입 이름으로 누른다(EditorClick mark="hierarchy.addComponent.<타입>")
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::note( ( string( "hierarchy.addComponent." ) + typeName.c_str() ).c_str() );
                    if ( pTypeInfo != nullptr )
                        EditorWidgets::drawTooltip( pTypeInfo->getTooltip().c_str() );
                };

                if ( bHasFilter )
                {
                    ImGui::Separator();
                    uint32 matchCount{ 0 };
                    for ( const hashed_string& typeName : listType )
                    {
                        const TypeInfo* pTypeInfo = ( pRegistry != nullptr ) ? pRegistry->findType( typeName ) : nullptr;
                        if ( pTypeInfo != nullptr && pTypeInfo->isHiddenInMenu() )
                            continue;

                        const utf8* pDisplayName = ( pTypeInfo != nullptr ) ? pTypeInfo->getDisplayName() : typeName.c_str();
                        if ( compFilter.matchesAny( { string_view{ pDisplayName }, typeName.view() } ) )
                        {
                            drawItem( typeName, pTypeInfo );
                            ++matchCount;
                        }
                    }
                    if ( matchCount == 0 )
                        ImGui::TextDisabled( "No matching components." );
                }
                else
                {
                    map<string, vector<pair<hashed_string, const TypeInfo*>>> mapCategorized;
                    for ( const hashed_string& typeName : listType )
                    {
                        const TypeInfo* pTypeInfo = ( pRegistry != nullptr ) ? pRegistry->findType( typeName ) : nullptr;
                        if ( pTypeInfo != nullptr && pTypeInfo->isHiddenInMenu() )
                            continue;

                        string category = ( pTypeInfo != nullptr && pTypeInfo->getCategory().empty() == false )
                                            ? pTypeInfo->getCategory()
                                            : "General";
                        mapCategorized[category].emplace_back( typeName, pTypeInfo );
                    }

                    for ( const auto& [category, items] : mapCategorized )
                    {
                        if ( category == "General" )
                        {
                            for ( const auto& [typeName, pTypeInfo] : items )
                            {
                                drawItem( typeName, pTypeInfo );
                            }
                        }
                        else
                        {
                            if ( ImGui::BeginMenu( category.c_str() ) )
                            {
                                for ( const auto& [typeName, pTypeInfo] : items )
                                {
                                    drawItem( typeName, pTypeInfo );
                                }
                                ImGui::EndMenu();
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }

            /** @brief 이 오브젝트의 컴포넌트 종류 · 태그마다 "그것을 가진 오브젝트 모두 선택" 을 고르는 메뉴입니다. */
            static void drawSelectSameMenu( GameObject* pObj, GameObjectManager* pManager )
            {
                if ( pObj == nullptr || pManager == nullptr || ImGui::BeginMenu( "Select All With" ) == false )
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
                ImGui::EndMenu();
            }

            static void drawGameObjectContextMenu( GameObject* pObj, GameObjectManager* pManager )
            {
                if ( ImGui::BeginPopupContextItem( "GOCtx" ) == false )
                    return;

                const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
                if ( bEditsAllowed == false )
                    ImGui::BeginDisabled();

                if ( ImGui::MenuItem( "Create GameObject" ) )
                    EditorSceneCommands::create( pManager, nullptr );

                if ( ImGui::MenuItem( "Create Child GameObject" ) )
                    EditorSceneCommands::create( pManager, pObj );

                if ( ImGui::MenuItem( "Duplicate GameObject", "Ctrl+D" ) )
                    EditorSceneCommands::duplicate( pManager, pObj );

                drawAddComponentMenu( pObj );
                drawSelectSameMenu( pObj, pManager );

                ImGui::Separator();

                if ( pObj->getParent() != nullptr )
                {
                    if ( ImGui::MenuItem( "Unparent" ) )
                        EditorSceneCommands::unparent( pObj );
                }

                EditorContext* pContext             = EditorContext::get();
                GameObject*    pSelected            = ( pContext != nullptr )
                                                        ? pManager->findGameObjectById( pContext->getWorkspace().getSelectedObjectId() )
                                                        : nullptr;
                const bool     bCanParentToSelected = pContext != nullptr && pSelected != nullptr && pSelected != pObj &&
                                                  pContext->getWorkspace().getSelectedComponentId() == 0;
                if ( bCanParentToSelected )
                {
                    if ( EditorSceneCommands::wouldCreateParentCycle( pObj, pSelected ) == false )
                    {
                        if ( ImGui::MenuItem( "Parent to Selected" ) )
                            EditorSceneCommands::reparent( pObj, pSelected, "Parent to Selected" );
                    }
                    else
                    {
                        ImGui::BeginDisabled();
                        ImGui::MenuItem( "Parent to Selected" );
                        ImGui::EndDisabled();
                    }
                }

                ImGui::Separator();
                if ( ImGui::MenuItem( "Destroy GameObject", "Delete" ) )
                    EditorSceneCommands::destroy( pManager, pObj );

                if ( bEditsAllowed == false )
                    ImGui::EndDisabled();
                ImGui::EndPopup();
            }

            static void drawSceneComponentNode( GameObject* pObj, SceneComponent* pSceneComp, GameObjectManager* pManager )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;

                if ( pObj == nullptr || pSceneComp == nullptr )
                    return;

                ImGui::PushID( static_cast<int32>( pSceneComp->getComponentId() ) );

                EditorWorkspace& ws        = pContext->getWorkspace();
                const bool       bSelected = ( ws.getSelectedObjectId() == pObj->getObjectId() &&
                                         ws.getSelectedComponentId() == pSceneComp->getComponentId() );

                const utf8* pCompName = pSceneComp->getComponentName().empty() == false
                                          ? pSceneComp->getComponentName().c_str()
                                          : "SceneComponent";

                fixed_string<constant::kMaxBuffer256> arrLabel;
                formatstring( arrLabel.data(), arrLabel.capacity(), "%###sc%#", pCompName, pSceneComp->getComponentId() );

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
                if ( ImGui::IsItemClicked() )
                    ws.selectComponent( pObj, pSceneComp );
                drawComponentContextMenu( pObj, pSceneComp, pManager );

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

            static void drawGameObjectNode( GameObject* pObj, GameObjectManager* pManager, const utf8* pFilter,
                                            uint64& renamingObjectId, fixed_string<constant::kMaxBuffer256>& renameBuffer,
                                            bool& bFocusRenameInput )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;

                if ( pObj == nullptr || pManager == nullptr )
                    return;

                if ( StringUtil::isNullOrEmpty( pFilter ) == false )
                {
                    if ( subtreeMatchesFilter( pObj, pFilter ) == false )
                        return;
                }

                const uint64 objectId  = pObj->getObjectId();
                const bool   bSelected = pContext->getEditorSelection().hasObject( pObj );

                ImGui::PushID( static_cast<int32>( objectId ) );

                // 1) 가시성 토글(눈) — 정사각 아이콘 단추라 DPI 배율을 받는다
                const bool bActive = pObj->isActiveInHierarchy();
                if ( EditorWidgets::drawToggleIconButton( "##active", bActive, ICON_FA_EYE, ICON_FA_EYE_SLASH, "Visible - click to deactivate",
                                                          "Inactive - click to activate" ) )
                    EditorSceneCommands::setActive( pObj, bActive == false ); // 되돌리기 · 씬 dirty 에 남는다
                EditorSelfTestMarks::note( "hierarchy.activeToggle" );
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::note( ( string( "hierarchy.toggle." ) + pObj->getName().c_str() ).c_str() );
                ImGui::SameLine();

                // 뱃지는 리플렉션 Category 에서 가져온다. 위의 컴포넌트 추가 메뉴가 이미 쓰는 데이터다.
                // 타입 이름을 비교하면 게임이 넣은 컴포넌트는 뱃지가 없다.
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

                fixed_string<constant::kMaxBuffer256> arrLabel;
                if ( badgeStr.empty() == false )
                    formatstring( arrLabel.data(), arrLabel.capacity(), "%# %###go%#", pObj->getName().c_str(), badgeStr.c_str(), objectId );
                else
                    formatstring( arrLabel.data(), arrLabel.capacity(), "%###go%#", pObj->getName().c_str(), objectId );

                const bool bHasChildGos   = pObj->hasChildren();
                const bool bHasComponents = pObj->getComponentCount() > 0;
                const bool bLeaf          = ( bHasChildGos == false && bHasComponents == false );

                // SpanAvailWidth — 선택 배경 · 클릭 영역이 앞의 가시성 토글 오른쪽부터다(SpanFullWidth 면 창 왼쪽부터 칠해 토글을 덮는다).
                const bool bOpen = ImGui::TreeNodeEx(
                    arrLabel.c_str(),
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                        ( bSelected ? ImGuiTreeNodeFlags_Selected : 0 ) | ( bLeaf ? ImGuiTreeNodeFlags_Leaf : 0 ) );
                if ( bSelected )
                    EditorSelfTestMarks::note( "hierarchy.selectedRow" ); // 시나리오가 오른쪽 클릭으로 오브젝트 메뉴를 연다
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::note( ( string( "hierarchy.row." ) + pObj->getName().c_str() ).c_str() );

                if ( ImGui::IsItemClicked() )
                {
                    ImGuiIO&      io   = ImGui::GetIO();
                    SelectionMode mode = SelectionMode::Replace;
                    if ( io.KeyCtrl )
                        mode = SelectionMode::Toggle;
                    else if ( io.KeyShift )
                        mode = SelectionMode::Add;

                    pContext->getWorkspace().selectGameObject( pObj, mode );
                }

                // 제자리 이름 바꾸기 입력
                if ( renamingObjectId == objectId && EditorUtil::areSceneEditsAllowed() )
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth( 160.0f * EditorThemeUtil::getDpiScale() );
                    if ( bFocusRenameInput )
                    {
                        ImGui::SetKeyboardFocusHere();
                        bFocusRenameInput = false;
                    }
                    if ( ImGui::InputText( "##InlineRename", renameBuffer.data(), renameBuffer.capacity(),
                                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll ) )
                    {
                        (void)EditorSceneCommands::rename( pObj, renameBuffer.c_str() ); // 빈 이름은 거절 — 이름이 그대로 남는다
                        renamingObjectId = 0;
                    }
                    if ( ImGui::IsItemDeactivated() && ImGui::IsKeyPressed( ImGuiKey_Escape ) == false )
                    {
                        (void)EditorSceneCommands::rename( pObj, renameBuffer.c_str() ); // 빈 이름은 거절 — 이름이 그대로 남는다
                        renamingObjectId = 0;
                    }
                    if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
                        renamingObjectId = 0;
                }

                drawGameObjectContextMenu( pObj, pManager );
                drawGameObjectDragDrop( pObj, pManager );

                if ( bOpen )
                {
                    vector<GameObject*> listChild;
                    pObj->getChildren( listChild );
                    for ( GameObject* pChild : listChild )
                    {
                        drawGameObjectNode( pChild, pManager, pFilter, renamingObjectId, renameBuffer,
                                            bFocusRenameInput );
                    }

                    const ComponentList& listComponent = pObj->getComponents();
                    for ( Component* pComp : listComponent )
                    {
                        if ( pComp == nullptr )
                            continue;

                        // 노드마다, 프레임마다 묻는 곳이다. 그래서 리플렉션 캐스트 대신 생성자에서 세운 비트를 본다.
                        if ( pComp->isSceneComponent() )
                        {
                            SceneComponent*       pSceneComp    = static_cast<SceneComponent*>( pComp );
                            const SceneComponent* pParent       = pSceneComp->getParent();
                            const bool            bRootOnThisGo = pParent == nullptr || pParent->getOwner() != pObj;
                            if ( bRootOnThisGo )
                                drawSceneComponentNode( pObj, pSceneComp, pManager );
                            continue;
                        }

                        ImGui::PushID( static_cast<int32>( pComp->getComponentId() ) );

                        EditorWorkspace& ws            = pContext->getWorkspace();
                        const bool       bCompSelected = ( ws.getSelectedObjectId() == pObj->getObjectId() &&
                                                     ws.getSelectedComponentId() == pComp->getComponentId() );

                        const utf8* pCompName = pComp->getComponentName().empty() == false
                                                  ? pComp->getComponentName().c_str()
                                                  : "Component";

                        fixed_string<constant::kMaxBuffer256> arrCompLabel;
                        formatstring( arrCompLabel.data(), arrCompLabel.capacity(), "%###c%#", pCompName, pComp->getComponentId() );

                        if ( ImGui::Selectable( arrCompLabel.c_str(), bCompSelected ) )
                            ws.selectComponent( pObj, pComp );
                        drawComponentContextMenu( pObj, pComp, pManager );

                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( HierarchyPanel, "hierarchy", EditorPanelCategory::Core, 100 );

    HierarchyPanel::HierarchyPanel()
        : _renamingObjectId{ 0 }
        , _lastDrawnRootId{ 0 }
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
            EditorWidgets::drawEmptyHint( "No active scene." );
            return;
        }

        GameObjectManager* pManager = pScene->getObjectManager();
        EditorSceneCommands::collectRootsInOrder( *pManager, _listSceneObject ); // 루트만, id 순 — 지우고 되돌려도 자리가 그대로다

        // 상단 툴바: 생성 버튼 + 검색창
        if ( EditorChrome::beginToolbar( "##HierarchyToolbar" ) )
        {
            const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
            if ( bEditsAllowed == false )
            {
                EditorWidgets::drawChip( "Play Mode", editor::style::kWarn );
                EditorWidgets::drawTooltip( "플레이 모드 실행 중 (씬 편집 제한)" );
                ImGui::SameLine();
            }
            if ( bEditsAllowed == false )
                ImGui::BeginDisabled();
            if ( ImGui::Button( "+ Create" ) )
                EditorSceneCommands::create( pManager, nullptr );
            EditorSelfTestMarks::note( "hierarchy.create" );
            EditorWidgets::drawTooltip( "현재 씬의 루트에 새 게임오브젝트를 생성합니다" );
            if ( bEditsAllowed == false )
                ImGui::EndDisabled();

            ImGui::SameLine();
            EditorWidgets::drawSearchField( "##HierarchyFilter", _filterBuffer,
                                            "Search (t:Mesh, tag:Player)...", 0.0f, false );
            EditorSelfTestMarks::note( "hierarchy.filter" );
            EditorWidgets::drawTooltip( "오브젝트 이름 검색, 컴포넌트 타입(t:Mesh), 태그(tag:Player) 필터를 지원합니다" );
        }
        EditorChrome::endToolbar();

        ImGui::Separator();

        editor::EditorSectionDesc treeDesc{};
        treeDesc._pId  = "##HierarchyTree";
        treeDesc._kind = editor::EditorSectionKind::Child;
        if ( EditorChrome::beginSection( treeDesc ) )
        {
            // `drawGameObjectNode` 는 서브트리가 필터에 안 걸리면 조용히 빠진다. 그래서 같은 술어로
            // 미리 세어, 전부 빠질 때는 빈 상자 대신 이유를 보여 준다.
            const EditorListFilter treeFilter{ _filterBuffer.c_str() };
            const bool             bFilterActive = treeFilter.isActive();
            uint32                 visibleRootCount{ 0 };
            uint32                 drawnRootCount{ 0 };
            uint64                 lastDrawnRootId{ 0 };

            // 화면 밖의 접힌 루트는 그리지 않고 같은 높이의 빈자리만 둔다 — 스크롤 막대와 다음 줄 위치는 그대로다. 큐브 8000 이면 노드 8000 개
            // (버튼 · 뱃지 문자열 · 트리 노드 · 드래그 드롭)를 프레임마다 다 그리던 자리다. 이어진 빈 줄은 빈자리 하나로 모은다.
            // 접힌 루트 줄의 높이는 프레임 높이(버튼 · 트리 노드) + 줄 간격이다.
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
            // 화면을 그리는 에디터 카메라는 씬 오브젝트지만 편집 대상이 아니다(저장에서도 빠진다) — 목록에 내지 않는다.
            const CameraComponent* pEditorCamera       = EditorCamera::find( pScene );
            const GameObject*      pEditorCameraObject = pEditorCamera != nullptr ? pEditorCamera->getOwner() : nullptr;
            for ( GameObject* pObj : _listSceneObject )
            {
                if ( pObj == pEditorCameraObject )
                    continue;
                const bool bMatches = bFilterActive == false || HierarchyPanelInternal::subtreeMatchesFilter( pObj, _filterBuffer.c_str() );
                if ( bMatches == false )
                    continue;
                ++visibleRootCount;
                const bool bSkip = _bSkipOffscreenRows && HierarchyPanelInternal::canSkipRootRow( pObj, _renamingObjectId, nextRowY, ImGui::GetFrameHeight() );
                if ( bSkip )
                {
                    ++pendingSkipRow;
                    nextRowY += rowStride;
                    continue;
                }
                flushSkippedRows();
                HierarchyPanelInternal::drawGameObjectNode( pObj, pManager, _filterBuffer.c_str(), _renamingObjectId, _renameBuffer, _bFocusRenameInput );
                nextRowY = ImGui::GetCursorScreenPos().y;
                ++drawnRootCount;
                lastDrawnRootId = pObj->getObjectId();
            }
            flushSkippedRows();
            _drawnRootCount  = drawnRootCount;
            _lastDrawnRootId = lastDrawnRootId;

            _visibleRootCount = visibleRootCount;
            if ( visibleRootCount == 0 && treeFilter.isActive() )
                EditorWidgets::drawNoSearchResultHint( treeFilter.getText() );

            // 빈 영역을 SW_ASSET_PATH 드래그 앤 드롭 대상으로 만든다
            if ( ImGui::BeginDragDropTarget() )
            {
                const ImGuiPayload* pGoPayload = ImGui::AcceptDragDropPayload( HierarchyPanelInternal::kHierarchyGoPayload );
                if ( pGoPayload != nullptr && pGoPayload->DataSize == static_cast<int32>( sizeof( uint64 ) ) )
                {
                    const uint64      draggedId = *static_cast<const uint64*>( pGoPayload->Data );
                    GameObject* const pDragged  = pManager->findGameObjectById( draggedId );
                    if ( pDragged != nullptr && pDragged->getParent() != nullptr )
                        EditorSceneCommands::unparent( pDragged, "Detach GameObject to Root" );
                }

                string droppedAssetPath;
                if ( EditorWidgets::tryAcceptAssetPayload( droppedAssetPath ) )
                    EditorAssetCommands::spawnPrefab( pManager, droppedAssetPath.c_str(), nullptr );
                ImGui::EndDragDropTarget();
            }

            handleHierarchyShortcuts( pManager );
            // 빈 공간 우클릭 메뉴
            if ( ImGui::BeginPopupContextWindow( "HierarchyEmptyCtx",
                                                 ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems ) )
            {
                if ( ImGui::MenuItem( "Create Empty GameObject" ) )
                    EditorSceneCommands::create( pManager, nullptr );
                ImGui::EndPopup();
            }
        }
        EditorChrome::endSection();
    }

    void HierarchyPanel::handleHierarchyShortcuts( GameObjectManager* pManager )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        // 단축키(Ctrl+D 복제, F2 이름 바꾸기, Delete 삭제)
        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_ChildWindows ) && ImGui::GetIO().WantTextInput == false )
        {
            const ImGuiIO&   io              = ImGui::GetIO();
            EditorSelection& editorSelection = pContext->getEditorSelection();
            // 사본으로 받는다. 아래 삭제가 순회 도중 선택 목록에서 항목을 뺀다.
            vector<GameObject*> listSel;
            editorSelection.getSelectedObjects( listSel );

            if ( listSel.empty() == false )
            {
                if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_D, false ) )
                {
                    vector<GameObject*> listNewCreated;
                    EditorSceneCommands::duplicateObjects( pManager, listSel, listNewCreated ); // 되돌리기 한 단계
                    if ( listNewCreated.empty() == false )
                    {
                        editorSelection.clearObjectSelection();
                        for ( GameObject* pNewGo : listNewCreated )
                        {
                            editorSelection.selectObject( pNewGo, SelectionMode::Add );
                        }
                    }
                }
                else if ( ImGui::IsKeyPressed( ImGuiKey_F2, false ) )
                {
                    GameObject* pSelected = listSel.back();
                    if ( pSelected != nullptr )
                    {
                        _renamingObjectId = pSelected->getObjectId();
                        formatstring( _renameBuffer.data(), _renameBuffer.capacity(), "%#", pSelected->getName().c_str() );
                        _bFocusRenameInput = true;
                    }
                }
                else if ( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) )
                {
                    (void)EditorSceneCommands::destroyObjects( pManager, listSel ); // 되돌리기 한 단계
                    editorSelection.clearObjectSelection();
                }
            }
        }
    }
} // namespace sw::editor
