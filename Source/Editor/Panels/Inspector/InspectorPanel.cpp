#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/Dev/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Commands/Scene/EditorMultiEdit.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorComponentIcon.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorComponentMenu.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorObjectEdit.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/PropertyEditCondition.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct InspectorPanelInternal
        {
            static constexpr Color kMultiSelectionText{ 0.4f, 0.7f, 1.0f, 1.0f };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( InspectorPanel, InspectorPanel::kPanelID, EditorPanelCategory::Core, 200 );

    InspectorPanel::InspectorPanel()
        : _propertyGrid{}
        , _favorites{}
        , _listMultiEditComponent{}
        , _pMultiEditCurrent{ nullptr }
        , _nameEditBuffer{}
        , _componentPresetJob{}
        , _listComponentPresetFile{}
        , _nameEditObjectID{ 0 }
        , _lockedObjectID{ 0 }
        , _pPendingMoveComponent{ nullptr }
        , _pendingMoveDirection{ 0 }
        , _addComponentSearch{}
        , _presetNameBuffer{}
        , _bComponentPresetDirty{ SW_TRUE }
        , _bFavoritesLoaded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void InspectorPanel::drawContent()
    {
        // 저장 파일은 처음 그릴 때 읽는다. 패널을 만들 때는 에디터 상태 폴더가 아직 정해지지 않았을 수 있다.
        if ( _bFavoritesLoaded == SW_FALSE )
        {
            (void)_favorites.loadFile( InspectorFavorites::getDefaultFilePath() ); // 없으면 빈 집합이다(처음 실행)
            _bFavoritesLoaded = SW_TRUE;
        }
        _propertyGrid.setFavorites( &_favorites );
        EditorWidgets::pushInspectorStyle();
        drawSelectionSection();
        _propertyGrid.endFrame(); // 모두 펼치기 · 접기 요청은 이번 프레임의 머리에만 적용한다
        InspectorPropertyUndo::commitFinishedEdits();

        // Undo 와 Redo 단축키는 여기서 처리하지 않는다. edit.undo 와 edit.redo 커맨드가 유일한 처리자다.
        // ImGui 의 IsKeyPressed 는 소비되지 않으므로 여기서도 받으면 전역 처리기(EditorCommandGUI)와 함께
        // 두 번 되돌리게 된다.

        EditorWidgets::popInspectorStyle();
    }

    void InspectorPanel::drawSelectionSection()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        EditorWidgets::drawSectionHeader( "Selection" );
        // 잠그면 선택이 바뀌어도 현재 오브젝트를 계속 보여 준다. 유니티 Inspector 의 자물쇠에 해당하며, 언리얼에서는 Details 창을 여러 개 여는 것과 같은 용도다.
        ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() );
        const bool bLocked = _lockedObjectID != 0;
        if ( EditorWidgets::drawToggleIconButton( "##inspectorLock", bLocked, editoricon::kLock, editoricon::kUnlock, SW_LOCKEY( "EditorInspector", "LockedToObject" ),
                                                  SW_LOCKEY( "EditorInspector", "FollowingSelection" ) ) )
            _lockedObjectID = bLocked ? 0 : editorContext.getSubsystem<EditorWorkspace>().getSelectedObjectID();
        EditorSelfTestMarks::note( "inspector.lock" );

        // 미리보기 창의 선택을 겨누면 그 창 씬의 오브젝트를 보인다. 잠금은 메인 선택의 오브젝트에만 쓴다.
        const EditorSelection* pFocusedSelection = editor::getFocusedSelection();
        const bool             bShowPreview      = _lockedObjectID == 0 && pFocusedSelection != nullptr && pFocusedSelection->isPreviewScope();
        if ( bShowPreview )
        {
            drawPreviewSelection( *pFocusedSelection );
            return;
        }

        EditorWorkspace&    ws = editorContext.getSubsystem<EditorWorkspace>();
        vector<GameObject*> listSelected;
        editor::getMainSceneSelection().getSelectedObjects( listSelected );
        if ( listSelected.size() > 1 && _lockedObjectID == 0 )
        {
            drawMultiSelection( listSelected );
            return;
        }

        const uint64 shownObjectID = _lockedObjectID != 0 ? _lockedObjectID : ws.getSelectedObjectID();
        if ( shownObjectID == 0 )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NothingSelected" ) );
            return;
        }

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NoActiveScene" ) );
            return;
        }

        GameObject* pObj = pScene->getObjectManager()->findGameObjectByID( shownObjectID );
        if ( pObj == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "SelectionGone" ) );
            if ( _lockedObjectID != 0 )
                _lockedObjectID = 0; // 잠근 오브젝트가 사라졌으므로 선택을 따라가는 상태로 돌아간다
            else
                ws.clearSelection();
            return;
        }

        // 플레이 중에도 값은 고칠 수 있다(유니티의 플레이 모드 편집). Stop 이 스냅샷으로 되돌리며, 남기려면 Keep Simulation Changes 를 쓴다.
        // 구조 편집(컴포넌트 추가, 프리팹)은 씬 명령이 플레이 중에 스스로 거절한다.
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
        {
            EditorWidgets::drawChip( "Play Mode", EditorThemeUtil::getWarningColor() );
            ImGui::SameLine();
            ImGui::TextDisabled( "Edits revert on Stop (Play > Keep Simulation Changes keeps the selection)." );
        }

        drawGameObjectHeader( pObj );

        ImGui::Spacing();
        ImGui::Separator();

        const string_view pfbPath = ws.findPrefabPath( pObj );
        if ( pfbPath.empty() == false )
            drawPrefabLinkSection( pObj, pfbPath );

        _propertyGrid.drawSearchBarWithOptions( SW_LOCKEY( "EditorInspector", "SearchTooltip" ) );
        ImGui::Spacing();
        drawFavoritesSection( pObj );

        const TypeInfo* pTypeInfo = pObj->getTypeInfo();
        if ( pTypeInfo != nullptr )
        {
            const EditorPropertyGridTarget objectTarget{ pObj, pTypeInfo, nullptr, pObj, {}, {} };
            _propertyGrid.drawProperties( objectTarget, "Reflected Properties", {} );
            ImGui::SeparatorText( "Methods" );
            _propertyGrid.drawMethodsAndEvents( objectTarget, false );
        }

        drawComponentList( pObj, ws );
    }

    void InspectorPanel::drawMultiSelection( const vector<GameObject*>& listObject )
    {
        GameObject* pPrimary = listObject[0];
        ImGui::TextColored( EditorColorUtil::toImVec4( InspectorPanelInternal::kMultiSelectionText ), "%u objects selected", static_cast<uint32>( listObject.size() ) );
        ImGui::TextDisabled( "Primary: %s", pPrimary->getName().c_str() );

        const bool bEditsAllowed = EditorPlaySession::areSceneEditsAllowed();
        if ( bEditsAllowed == false )
            ImGui::BeginDisabled();

        // 활성은 모든 선택에 적용한다. 값이 섞여 있으면 혼합으로 표시하고, 체크는 주 선택의 값을 따른다.
        bool bMixedActive = false;
        for ( const GameObject* pObject : listObject )
        {
            bMixedActive = bMixedActive || pObject->isActive() != pPrimary->isActive();
        }
        bool bActive = pPrimary->isActive();
        if ( bMixedActive )
        {
            ImGui::TextDisabled( "\xe2\x80\x94" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "MultipleValues" ) );
            ImGui::SameLine();
        }
        if ( ImGui::Checkbox( "Active", &bActive ) )
        {
            EditorTransaction::beginTransaction( "Set Active (multiple)" );
            for ( GameObject* pObject : listObject )
            {
                EditorSceneCommands::setActive( pObject, bActive );
            }
            EditorTransaction::endTransaction();
        }

        _propertyGrid.drawSearchBarWithOptions( SW_LOCKEY( "EditorInspector", "SearchTooltip" ) );
        drawFavoritesSection( pPrimary ); // 다중 선택의 즐겨찾기는 주 선택 오브젝트 기준이다
        ImGui::SeparatorText( "Components" );
        EditorMultiEditUtil::collectCommonComponents( listObject, _listMultiEditComponent );
        for ( EditorMultiEditComponent& common : _listMultiEditComponent )
        {
            Component* pComponent = common._listComponent[0];
            ImGui::PushID( pComponent->getTypeInfo()->_name.c_str() );
            if ( _propertyGrid.getOpenAllRequest() != 0 )
                ImGui::SetNextItemOpen( _propertyGrid.getOpenAllRequest() > 0 );
            if ( ImGui::CollapsingHeader( pComponent->getTypeInfo()->getDisplayName(), ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                _pMultiEditCurrent = &common;
                EditorPropertyGridTarget target{};
                target._pInstance  = pComponent;
                target._pType      = pComponent->getTypeInfo();
                target._pComponent = pComponent;
                target._pObject    = pPrimary;
                target._onEdited   = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &InspectorPanel::copyEditToOthers, this );
                target._isMixed    = SW_DELEGATE_METHOD( Delegate<bool( const PropertyInfo& )>, &InspectorPanel::isMultiEditMixed, this );
                _propertyGrid.drawProperties( target, nullptr, {} );
                _pMultiEditCurrent = nullptr;
            }
            ImGui::PopID();
        }
        size_t componentCount{ 0 };
        for ( const Component* pComponent : pPrimary->getComponents() )
        {
            componentCount += pComponent != nullptr ? 1u : 0u;
        }
        if ( componentCount > _listMultiEditComponent.size() )
            ImGui::TextDisabled( "%u component(s) are not on every selected object", static_cast<uint32>( componentCount - _listMultiEditComponent.size() ) );

        if ( bEditsAllowed == false )
            ImGui::EndDisabled();
    }

    void InspectorPanel::copyEditToOthers( const PropertyInfo& prop )
    {
        if ( _pMultiEditCurrent != nullptr )
            (void)EditorMultiEditUtil::copyPropertyToOthers( prop, _pMultiEditCurrent->_listComponent ); // 수는 쓰지 않는다 — 되돌리기는 위젯 추적이 모두를 담는다
    }

    bool InspectorPanel::isMultiEditMixed( const PropertyInfo& prop )
    {
        if ( _pMultiEditCurrent == nullptr )
            return false;
        vector<ObjectAddress> listInstance;
        for ( const Component* pComponent : _pMultiEditCurrent->_listComponent )
        {
            listInstance.push_back( ObjectAddress{ pComponent } );
        }
        return EditorMultiEditUtil::hasMixedValues( prop, listInstance );
    }

    void InspectorPanel::drawFavoritesSection( GameObject* pObj )
    {
        if ( pObj == nullptr || _favorites.isEmpty() )
            return;
        // 그릴 것이 있는 대상만 모은다. 하나도 없으면 구역 머리도 그리지 않는다.
        vector<EditorPropertyGridTarget> listTarget;
        const TypeInfo*                  pObjectType = pObj->getTypeInfo();
        if ( pObjectType != nullptr && _favorites.hasAnyForTypeOrBase( *pObjectType ) )
            listTarget.push_back( EditorPropertyGridTarget{ pObj, pObjectType, nullptr, pObj, {}, {} } );
        for ( Component* pComp : pObj->getComponents() )
        {
            const bool bNotFavoriteComponent = pComp == nullptr || pComp->getTypeInfo() == nullptr || _favorites.hasAnyForTypeOrBase( *pComp->getTypeInfo() ) == false;
            if ( bNotFavoriteComponent )
                continue;
            listTarget.push_back( EditorPropertyGridTarget{ pComp, pComp->getTypeInfo(), pComp, pComp->getOwner(), {}, {} } );
        }
        if ( listTarget.empty() )
            return;

        ImGui::SeparatorText( SW_LOCKEY( "EditorInspector", "FavoritesSection" ) );
        EditorSelfTestMarks::note( "inspector.favorites" );
        for ( const EditorPropertyGridTarget& target : listTarget )
        {
            // 같은 타입의 컴포넌트가 둘이면 id 가 겹치므로 인스턴스 주소로 나눈다.
            ImGui::PushID( target._pInstance );
            _propertyGrid.drawFavoriteProperties( target );
            ImGui::PopID();
        }
        ImGui::Spacing();
    }

    void InspectorPanel::drawPreviewSelection( const EditorSelection& selection )
    {
        const Scene* pScene = selection.getScene();
        EditorWidgets::drawChip( "Preview", EditorThemeUtil::getAccentColor() );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", pScene != nullptr ? pScene->getName().c_str() : "?" );
        GameObject* pObj = selection.getPrimaryObject();
        if ( pObj == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NothingSelected" ) );
            return;
        }
        ImGui::TextUnformatted( pObj->getName().c_str() );
        ImGui::Separator();
        _propertyGrid.drawSearchBarWithOptions( SW_LOCKEY( "EditorInspector", "SearchTooltip" ) );
        // 구조 편집(추가 · 제거)도 메인과 같은 명령(`EditorSceneCommands`)이다 — 기록은 오브젝트가 사는 씬이 고른 창의 스택에 간다(`EditorTransaction`).
        // 복사한다. 아래에서 컴포넌트를 제거할 수 있어 원본을 순회할 수 없다.
        const vector<Component*> listComponent( pObj->getComponents().begin(), pObj->getComponents().end() );
        for ( Component* pComp : listComponent )
        {
            if ( pComp == nullptr || pComp->getTypeInfo() == nullptr )
                continue;
            ImGui::PushID( pComp );
            bool       bRemove{ false };
            const bool bOpen = ImGui::CollapsingHeader( pComp->getTypeInfo()->getDisplayName(), ImGuiTreeNodeFlags_DefaultOpen );
            if ( ImGui::BeginPopupContextItem( "PreviewCompCtx" ) )
            {
                bRemove = ImGui::MenuItem( "Remove Component" );
                ImGui::EndPopup();
            }
            if ( bOpen && bRemove == false )
            {
                const EditorPropertyGridTarget target{ pComp, pComp->getTypeInfo(), pComp, pObj, {}, {} };
                _propertyGrid.drawProperties( target, nullptr, {} );
            }
            ImGui::PopID();
            if ( bRemove )
            {
                (void)EditorSceneCommands::destroyComponent( pObj->getManager(), pObj, pComp ); // 막혔으면(플레이 중) 그대로 남고 카드가 보인다
                break;
            }
        }

        ImGui::Spacing();
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlus, "Add Component" ) ) )
        {
            _addComponentSearch.clear();
            ImGui::OpenPopup( "PreviewAddComponent" );
        }
        if ( ImGui::BeginPopup( "PreviewAddComponent" ) )
        {
            bool bFailed{ false };
            if ( EditorComponentMenu::drawAddComponentList( pObj, _addComponentSearch, "inspector.previewAddComponent", ImGui::IsWindowAppearing(), bFailed ) )
                ImGui::CloseCurrentPopup();
            if ( bFailed )
                SW_LOG_WARNING( "Could not add the component to '%#'", pObj->getName().c_str() );
            ImGui::EndPopup();
        }
    }

    void InspectorPanel::drawPrefabLinkSection( GameObject* pObj, string_view prefabPath )
    {
        EditorWidgets::drawChip( "Prefab", EditorThemeUtil::getAccentColor() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "PrefabInstance" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%.*s", static_cast<int32>( prefabPath.size() ), prefabPath.data() );

        if ( ImGui::Button( "Apply to Prefab" ) )
            (void)EditorInspectorCommands::applyToPrefab( pObj, prefabPath ); // 실패는 프리팹 저장이 알린다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ApplyToPrefab" ) );

        ImGui::SameLine();
        if ( ImGui::Button( "Revert to Prefab" ) && EditorInspectorCommands::revertToPrefab( pObj, prefabPath ) == false )
            SW_LOG_WARNING( "Revert to Prefab failed for '%#' (%#)", pObj->getName().c_str(), prefabPath );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "RevertToPrefab" ) );

        ImGui::SameLine();
        if ( ImGui::Button( "Unlink" ) )
            EditorInspectorCommands::unlinkPrefab( pObj ); // 부품이면 맨 바깥 인스턴스를 한 겹 푼다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "UnlinkPrefab" ) );

        // 중첩 부품이면 바깥 인스턴스의 프리팹에도 적용할 수 있다 — 부품의 수정이 바깥 프리팹의 노드 오버라이드(`<Part>` 포함)가 된다.
        const GameObject* pOuter = EditorInspectorCommands::findOuterInstance( pObj );
        EditorContext*    pCtx   = EditorContext::get();
        if ( pOuter != nullptr && pCtx != nullptr )
        {
            const string_view outerPath = pCtx->getSubsystem<EditorWorkspace>().findPrefabPath( pOuter );
            if ( ImGui::Button( "Apply to Outer Prefab" ) )
                (void)EditorInspectorCommands::applyToOuterPrefab( pObj ); // 실패는 프리팹 저장이 알린다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ApplyToPrefab" ) );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s: %.*s", pOuter->getName().c_str(), static_cast<int32>( outerPath.size() ), outerPath.data() );
        }

        ImGui::Separator();
    }

    void InspectorPanel::drawComponentList( GameObject* pObj, EditorWorkspace& workspace )
    {
        ImGui::SeparatorText( "Components" );
        EditorContext* pSelEditorContext = EditorContext::get();
        IRHIDevice*    pRHIDevice        = ( pSelEditorContext != nullptr ) ? pSelEditorContext->getSubsystem<EditorRenderViews>().getRHIDevice() : nullptr;
        // 복사한다. 아래 루프가 컴포넌트를 제거할 수 있어 원본을 순회할 수 없다.
        const vector<Component*> listComponent( pObj->getComponents().begin(), pObj->getComponents().end() );
        for ( Component* pComp : listComponent )
        {
            if ( pComp == nullptr )
                continue;

            const utf8* pComponentName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str() : "Component";
            // 카드 헤더 앞에 컴포넌트 아이콘을 둔다. 유니티 Inspector 와 언리얼 Details 의 컴포넌트 헤더에 해당한다
            fixed_string<constant::kMaxBuffer128> cardTitle;
            formatstring( cardTitle.data(), cardTitle.capacity(), "%#  %#", EditorComponentIcon::findRow( pComp->getTypeInfo() )._pGlyph, pComponentName );
            const utf8* pName = cardTitle.c_str();
            // 체크박스는 컴포넌트 자기 비트다. 실효값(isActive, 소유 오브젝트의 계층 활성까지 포함)을 읽어 그대로 다시 쓰면
            // 꺼진 부모 아래의 컴포넌트는 인스펙터에 보이기만 해도 자기 비트가 꺼지므로 주의한다. 쓰는 것도 값이 바뀐 때 한 번이다(아래).
            const bool bWasActive = pComp->isSelfActive();
            bool       bActive    = bWasActive;
            bool       bRemove{ false };
            const bool bAccent   = isA<SceneComponent>( pComp );
            const bool bScrollTo = ( workspace.getScrollToComponentID() != 0 &&
                                     workspace.getScrollToComponentID() == pComp->getComponentID() );

            if ( bScrollTo )
            {
                ImGui::SetNextItemOpen( true );
                workspace.setScrollToComponentID( 0 );
            }
            else if ( _propertyGrid.getOpenAllRequest() != 0 )
            {
                ImGui::SetNextItemOpen( _propertyGrid.getOpenAllRequest() > 0 ); // 보기 옵션의 모두 펼치기 · 접기는 컴포넌트 카드에도 적용한다
            }

            if ( EditorWidgets::beginComponentCard( pName, pComp->getComponentID(), &bActive, &bRemove, bAccent ) )
            {
                if ( bScrollTo )
                    ImGui::SetScrollHereY( 0.25f );

                drawComponentContextMenu( pObj, pComp, workspace, bRemove );

                drawComponentSection( pComp, pRHIDevice );
                EditorWidgets::endComponentCard();
            }
            if ( bActive != bWasActive )
                InspectorObjectEdit::applyObjectEdit( pObj, "Toggle Component Active", [pComp, bActive]()
                { pComp->setActive( bActive ); } );

            if ( _pPendingMoveComponent == pComp && _pendingMoveDirection != 0 )
            {
                const int32 direction = _pendingMoveDirection;
                InspectorObjectEdit::applyObjectEdit( pObj, direction < 0 ? "Move Component Up" : "Move Component Down", [pObj, pComp, direction]()
                { (void)pObj->moveComponent( pComp, direction ); } ); // 끝 · 주 씬 컴포넌트면 그대로 — 메뉴가 이미 막는다
                _pPendingMoveComponent = nullptr;
                _pendingMoveDirection  = 0;
                break;
            }

            if ( bRemove )
            {
                GameObjectManager* pGameObjectManager = pObj->getManager();
                if ( pGameObjectManager == nullptr )
                    (void)pObj->removeComponent( pComp ); // 실패는 removeComponent 가 알리고 카드가 남는다
                else if ( EditorPlaySession::areSceneEditsAllowed() )
                    EditorSceneCommands::destroyComponent( pGameObjectManager, pObj, pComp ); // 기록과 dirty 를 남긴다
                else
                    pGameObjectManager->destroyComponent( pComp ); // 플레이 사본이므로 Stop 이 되돌린다
                break;
            }
        }

        // 컴포넌트 목록 끝의 Add Component 다. 유니티 Inspector 맨 아래, 언리얼 Details 의 Add 에 해당하며 Hierarchy 메뉴와 같은 목록이다.
        ImGui::Spacing();
        const float32 buttonWidth = MathUtil::min( ImGui::GetContentRegionAvail().x, 220.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( ImGui::GetContentRegionAvail().x - buttonWidth ) * 0.5f );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlus, "Add Component" ), ImVec2{ buttonWidth, 0.0f } ) )
        {
            _addComponentSearch.clear();
            ImGui::OpenPopup( "InspectorAddComponent" );
        }
        EditorSelfTestMarks::note( "inspector.addComponent" );
        if ( ImGui::BeginPopup( "InspectorAddComponent" ) )
        {
            bool bFailed{ false };
            if ( EditorComponentMenu::drawAddComponentList( pObj, _addComponentSearch, "inspector.addComponent", ImGui::IsWindowAppearing(), bFailed ) )
                ImGui::CloseCurrentPopup();
            if ( bFailed )
                SW_LOG_WARNING( "Could not add the component to '%#'", pObj->getName().c_str() );
            ImGui::EndPopup();
        }
    }

    void InspectorPanel::drawComponentContextMenu( GameObject* pObj, Component* pComp, EditorWorkspace& workspace, bool& bOutRemove )
    {
        if ( ImGui::BeginPopupContextItem( "CompCardCtx" ) )
        {
            const TypeInfo* pTInfo = pComp->getTypeInfo();
            if ( pTInfo != nullptr )
            {
                drawComponentCopyPasteItems( pObj, pComp, workspace );
                drawComponentMoveItems( pObj, pComp );

                ImGui::Separator();
                drawComponentPresetsMenu( pComp );
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( "Remove Component" ) )
                bOutRemove = true;
            ImGui::EndPopup();
        }
    }

    void InspectorPanel::drawComponentCopyPasteItems( GameObject* pObj, Component* pComp, EditorWorkspace& workspace )
    {
        if ( ImGui::MenuItem( "Copy Component" ) )
            workspace.copyComponent( pComp );
        const string compTypeName = pComp->getTypeName().c_str();
        const bool   bCanPaste    = ( workspace.hasCopiedComponent() &&
                                 workspace.getCopiedComponentTypeName() == compTypeName );
        if ( bCanPaste )
        {
            if ( ImGui::MenuItem( "Paste Component Values" ) )
                (void)EditorTransformCommands::pasteCopiedComponentValues( workspace, pComp ); // 실패는 붙여넣기가 로그로 알린다
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::MenuItem( "Paste Component Values" );
            ImGui::EndDisabled();
        }

        if ( workspace.hasCopiedComponent() )
        {
            if ( ImGui::MenuItem( "Paste as New Component" ) )
                (void)EditorTransformCommands::pasteCopiedComponentAsNew( workspace, pObj ); // 실패는 붙여넣기가 로그로 알린다
        }
        ImGui::Separator();
    }

    void InspectorPanel::drawComponentMoveItems( GameObject* pObj, Component* pComp )
    {
        // 주 씬 컴포넌트(루트 트랜스폼)는 옮기지 않고, 그 위치로 옮겨 들어가지도 않는다(`GameObject::moveComponent`).
        const auto& listComponent = pObj->getComponents();
        size_t      index         = 0;
        while ( index < listComponent.size() && listComponent[index] != pComp )
        {
            ++index;
        }
        const Component* pPrimary = pObj->getPrimarySceneComponent();
        const bool       bCanUp   = index > 0 && index < listComponent.size() && pComp != pPrimary && listComponent[index - 1] != pPrimary;
        const bool       bCanDown = index + 1 < listComponent.size() && pComp != pPrimary && listComponent[index + 1] != pPrimary;
        if ( ImGui::MenuItem( "Move Up", nullptr, false, bCanUp ) )
        {
            _pPendingMoveComponent = pComp;
            _pendingMoveDirection  = -1;
        }
        if ( ImGui::MenuItem( "Move Down", nullptr, false, bCanDown ) )
        {
            _pPendingMoveComponent = pComp;
            _pendingMoveDirection  = 1;
        }
    }

    void InspectorPanel::drawComponentPresetsMenu( Component* pComp )
    {
        if ( ImGui::BeginMenu( "Presets" ) )
        {
            ImGui::InputTextWithHint( "##presetName", SW_LOCKEY( "EditorInspector", "PresetNameHint" ), _presetNameBuffer.data(),
                                      _presetNameBuffer.capacity() );
            ImGui::SameLine();
            if ( ImGui::Button( "Save" ) && _presetNameBuffer.empty() == false )
            {
                if ( EditorTransformCommands::saveComponentPreset( pComp, _presetNameBuffer.c_str() ) )
                {
                    _presetNameBuffer.clear();
                    _bComponentPresetDirty = SW_TRUE;
                }
                else
                {
                    SW_LOG_ERROR( "Could not save component preset '%#'", _presetNameBuffer.c_str() );
                }
            }
            ImGui::Separator();

            vector<string>       listNewPresetFile;
            const EditorTaskPoll presetPoll = _componentPresetJob.take( listNewPresetFile );
            if ( presetPoll == EditorTaskPoll::Ready )
            {
                _listComponentPresetFile = std::move( listNewPresetFile );
                _bComponentPresetDirty   = SW_FALSE;
            }
            else if ( _bComponentPresetDirty == SW_TRUE && presetPoll == EditorTaskPoll::Idle )
            {
                _componentPresetJob.request(
                    EditorGlobalVariableCommands::getComponentPresetFolderPath(), ".preset.xml", false );
            }

            bool bFoundPresets{ false };
            for ( const string& presetFile : _listComponentPresetFile )
            {
                const string displayPreset = EditorTransformCommands::getComponentPresetName( pComp, presetFile );
                if ( displayPreset.empty() == false )
                {
                    bFoundPresets = true;
                    if ( ImGui::MenuItem( displayPreset.c_str() ) )
                        if ( EditorTransformCommands::loadComponentPreset( pComp, presetFile ) == false )
                            SW_LOG_ERROR( "Could not apply component preset '%#'", presetFile.c_str() );
                }
            }
            if ( bFoundPresets == false )
                ImGui::TextDisabled( "No saved presets." );

            ImGui::EndMenu();
        }
    }

    void InspectorPanel::drawGameObjectHeader( GameObject* pObj )
    {
        ImGui::Text( "GameObject  ID: %u", static_cast<uint32>( pObj->getObjectID() ) );

        // 편집 중이 아니면 오브젝트 이름으로 채운다. 편집 중인 텍스트를 멤버로 보관해야 입력란을 떠난 프레임(ImGui 가 버퍼에 쓰지 않는다)에도 적용할 수 있다.
        // Enter 를 누르거나 다른 곳을 눌러 떠나도 적용한다(유니티와 언리얼). Esc 는 ImGui 가 텍스트를 되돌려 편집 없음으로 끝난다.
        const bool bEditingName = ImGui::GetActiveID() == ImGui::GetID( "Name" ) && _nameEditObjectID == pObj->getObjectID();
        if ( bEditingName == false )
        {
            _nameEditBuffer   = pObj->getName().c_str();
            _nameEditObjectID = pObj->getObjectID();
        }
        const bool bEnter         = ImGui::InputText( "Name", _nameEditBuffer.data(), _nameEditBuffer.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        const bool bLeftAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        const bool bNameChanged   = _nameEditBuffer.empty() == false && pObj->getName().isEqual( hashed_string( _nameEditBuffer.c_str() ), NameCase::CaseSensitive ) == false;
        if ( ( bEnter || bLeftAfterEdit ) && bNameChanged )
            InspectorObjectEdit::applyObjectEdit( pObj, "Rename GameObject", [pObj, this]()
            { pObj->setName( hashed_string( _nameEditBuffer.c_str() ) ); } );
        EditorSelfTestMarks::note( "inspector.name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ObjectName" ) );

        bool bActive = pObj->isActive();
        if ( ImGui::Checkbox( "Active", &bActive ) )
            EditorSceneCommands::setActive( pObj, bActive );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ObjectActive" ) );

        GameObject* pParent = pObj->getParent();
        if ( pParent != nullptr )
        {
            ImGui::Text( "Parent: %s", pParent->getName().c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Unparent" ) )
                InspectorObjectEdit::applyObjectEdit( pObj, "Unparent GameObject", [pObj]()
                { pObj->detachFromParent(); } );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "Unparent" ) );
        }
        else
            ImGui::TextDisabled( "Parent: (root)" );

        const vector<TagID>& listTag = pObj->getTags().getTags();
        if ( listTag.empty() == false )
        {
            ImGui::TextUnformatted( "Tags:" );
            for ( const TagID& tag : listTag )
            {
                if ( StringUtil::isNullOrEmpty( tag._pString ) == false )
                {
                    ImGui::SameLine();
                    EditorWidgets::drawChip( tag._pString, EditorThemeUtil::getSuccessColor() );
                }
            }
        }
    }

    void InspectorPanel::drawComponentSection( Component* pComp, IRHIDevice* pRHIDevice )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        ImGui::TextDisabled( "ID: %u", static_cast<uint32>( pComp->getComponentID() ) );

        const TypeInfo* pTypeInfo = pComp->getTypeInfo();
        if ( pTypeInfo == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NoTypeInfo" ) );
            return;
        }

        // 확장은 이 타입과 그 기반 타입들에 등록된 것 전부다(기반에서 파생 순서). 각자 자기 영역을 그리고 직접 그린 리플렉션 프로퍼티만 감춘다. 나머지 리플렉션
        // 프로퍼티는 상속한 것까지 아래에서 그린다(언리얼 Details 의 `IDetailCustomization` 과 `HideProperty`).
        vector<IInspectorComponent*> listInspector;
        editorContext.getSubsystem<InspectorComponentManager>().collectForType( *pTypeInfo, listInspector );
        vector<hashed_string> listDrawnName;
        for ( IInspectorComponent* pInspector : listInspector )
        {
            pInspector->drawHeader( pComp );
        }
        for ( IInspectorComponent* pInspector : listInspector )
        {
            // 확장 영역도 프로퍼티 위젯과 같은 규칙으로 되돌리기와 dirty 에 남긴다. 영역을 하나로 묶어 위젯 하나처럼 추적하므로
            // 영역이 위젯마다 `trackPod` 를 호출하지 않아도 된다.
            ImGui::BeginGroup();
            pInspector->drawSection( pComp, pRHIDevice );
            ImGui::EndGroup();
            InspectorPropertyUndo::trackLastItem( pTypeInfo->getDisplayName() );
            pInspector->collectDrawnProperties( listDrawnName );
        }

        const EditorPropertyGridTarget componentTarget{ pComp, pTypeInfo, pComp, pComp->getOwner(), {}, {} };
        _propertyGrid.drawProperties( componentTarget, "Properties", listDrawnName );
        ImGui::SeparatorText( "Methods" );
        _propertyGrid.drawMethodsAndEvents( componentTarget, true );

        for ( IInspectorComponent* pInspector : listInspector )
        {
            pInspector->drawFooter( pComp, pRHIDevice );
        }
    }
} // namespace sw::editor
