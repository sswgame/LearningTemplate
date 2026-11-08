#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/Diagnostics/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Commands/Scene/EditorMultiEdit.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneObjectCommands.h"
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorComponentMenu.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorObjectEdit.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/Model/InspectorMultiSelection.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"
#include "Editor/Panels/Inspector/Model/PropertyEditConditionExpr.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct InspectorPanelInternal
        {
            static constexpr Color kMultiSelectionText{ 0.4f, 0.7f, 1.0f, 1.0f };

            static InspectorPanel* findPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                return pPanels != nullptr ? pPanels->findPanel<InspectorPanel>() : nullptr;
            }

            /** @brief 프리셋 하위 메뉴는 인스펙터가 프리셋 목록 · 이름 칸을 들고 있어 그 창이 있을 때 켜진다. */
            static bool canShowPresets( const EditorCommandTarget& target )
            {
                GameObject* pOwner = nullptr;
                return findPanel() != nullptr && EditorSceneObjectCommands::findTargetComponent( target, pOwner ) != nullptr;
            }

            static void drawPresetsSubmenu( const EditorCommandTarget& target )
            {
                GameObject*     pOwner     = nullptr;
                Component*      pComponent = EditorSceneObjectCommands::findTargetComponent( target, pOwner );
                InspectorPanel* pPanel     = findPanel();
                if ( pComponent != nullptr && pPanel != nullptr )
                    pPanel->drawComponentPresetItems( pComponent );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( InspectorPanel, InspectorPanel::kPanelID, EditorPanelCategory::Core, 200 );
    // 컴포넌트 메뉴의 Presets 하위 메뉴(나머지 항목은 `EditorSceneObjectCommands.cpp`).
    SW_EDITOR_CONTEXT_COMMAND( ComponentPresets, "inspector.component.presets", nullptr, &InspectorPanelInternal::canShowPresets, NoScene, &InspectorPanelInternal::drawPresetsSubmenu );

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
            (void)_favorites.loadFromFile( InspectorFavorites::getDefaultFilePath() ); // 없으면 빈 집합이다(처음 실행)
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

        // 머리 글과 자물쇠를 한 줄에 두고 그 아래에 구분선 — drawSectionHeader 는 구분선까지 그려 자물쇠가 다음 줄로 밀렸다(uiaudit 00).
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Selection" ) );
        // 잠그면 선택이 바뀌어도 현재 오브젝트를 계속 보여 준다. 유니티 Inspector 의 자물쇠에 해당하며, 언리얼에서는 Details 창을 여러 개 여는 것과 같은 용도다.
        ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() );
        const bool bLocked = _lockedObjectID != 0;
        if ( EditorWidgets::drawToggleIconButton( "##inspectorLock", bLocked, SW_EDITOR_ICON( "Lock" ), SW_EDITOR_ICON( "Unlock" ), SW_LOCKEY( "EditorInspector", "LockedToObject" ),
                                                  SW_LOCKEY( "EditorInspector", "FollowingSelection" ) ) )
            _lockedObjectID = bLocked ? 0 : editorContext.getSubsystem<EditorWorkspace>().getSelectedObjectID();
        EditorSelfTestMarks::record( "inspector.lock" );
        ImGui::Separator();

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

        drawPlayModeNotice();
        drawGameObjectHeader( pObj );

        ImGui::Spacing();
        ImGui::Separator();

        const string_view pfbPath = ws.findPrefabPath( pObj );
        if ( pfbPath.empty() == false )
            drawPrefabLinkSection( pObj, AssetID{ pfbPath } );
        drawSceneAssemblySection( pObj );

        _propertyGrid.drawSearchBarWithOptions( SW_LOCKEY( "EditorInspector", "SearchTooltip" ) );
        ImGui::Spacing();
        drawFavoritesSection( pObj );

        const TypeInfo* pTypeInfo = pObj->getTypeInfo();
        if ( pTypeInfo != nullptr )
        {
            const EditorPropertyGridTarget objectTarget{ pObj, pTypeInfo, nullptr, pObj, {}, {} };
            _propertyGrid.drawProperties( objectTarget, "Reflected Properties", {} );
            ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Methods" ) );
            _propertyGrid.drawMethodsAndEvents( objectTarget, false );
        }

        drawComponentList( pObj, ws );
    }

    void InspectorPanel::drawPlayModeNotice()
    {
        // 플레이 중에도 값은 고칠 수 있다(유니티의 플레이 모드 편집). Stop 이 스냅샷으로 되돌리며, 남기려면 Keep Simulation Changes 를 쓴다.
        // 구조 편집(컴포넌트 추가, 프리팹)은 씬 명령이 플레이 중에 스스로 거절한다.
        if ( EditorPlaySession::areSceneEditsAllowed() )
            return;
        EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "PlayMode" ), SW_THEME_COLOR( "Signal", "Warning" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "EditsRevertOnStopPlayKeepSimulationChanges" ) );
    }

    void InspectorPanel::drawMultiSelection( const vector<GameObject*>& listObject )
    {
        GameObject* pPrimary = listObject[0];
        ImGui::TextColored( EditorColorUtil::toImVec4( InspectorPanelInternal::kMultiSelectionText ), "%s", SW_LOCKEYFORMAT( "EdFormat", "ObjectsSelected", TextArgumentList().addInteger( "objectCount", static_cast<int64>( listObject.size() ) ) ).c_str() );
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Primary", TextArgumentList().addText( "name", pPrimary->getName().c_str() ) ).c_str() );

        drawPlayModeNotice();

        // 활성은 모든 선택에 적용한다. 값이 섞여 있으면 혼합으로 표시하고, 체크는 주 선택의 값을 따른다.
        const InspectorMultiSelection::ActiveState activeState = InspectorMultiSelection::readActiveState( listObject );
        bool                                       bActive     = activeState._bActive;
        if ( activeState._bMixed )
        {
            ImGui::TextDisabled( "\xe2\x80\x94" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "MultipleValues" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Active" ) ) );
        }
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Active" ), &bActive ) )
            InspectorMultiSelection::applyActive( listObject, bActive );

        _propertyGrid.drawSearchBarWithOptions( SW_LOCKEY( "EditorInspector", "SearchTooltip" ) );
        drawFavoritesSection( pPrimary ); // 다중 선택의 즐겨찾기는 주 선택 오브젝트 기준이다
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Components" ) );
        EditorMultiEditComponent::collectCommonComponents( listObject, _listMultiEditComponent );
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
        const size_t unsharedCount = InspectorMultiSelection::countUnsharedComponents( *pPrimary, _listMultiEditComponent );
        if ( unsharedCount > 0 )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "ComponentSAreNot", TextArgumentList().addInteger( "componentCount", static_cast<int64>( unsharedCount ) ) ).c_str() );
    }

    void InspectorPanel::copyEditToOthers( const PropertyInfo& prop )
    {
        if ( _pMultiEditCurrent != nullptr )
            (void)EditorMultiEditComponent::copyPropertyToOthers( prop, _pMultiEditCurrent->_listComponent ); // 수는 쓰지 않는다 — 되돌리기는 위젯 추적이 모두를 담는다
    }

    bool InspectorPanel::isMultiEditMixed( const PropertyInfo& prop )
    {
        return _pMultiEditCurrent != nullptr && InspectorMultiSelection::hasMixedValues( prop, _pMultiEditCurrent->_listComponent );
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
        EditorSelfTestMarks::record( "inspector.favorites" );
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
        EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "Preview" ), SW_THEME_COLOR( "Base", "Accent" ) );
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
            const bool bOpen = ImGui::CollapsingHeader( pComp->getTypeInfo()->getDisplayName(), ImGuiTreeNodeFlags_DefaultOpen );
            drawComponentContextMenu( pComp ); // 메인 카드와 같은 컴포넌트 메뉴 — 고른 명령은 다음 프레임 첫머리에 돈다
            if ( bOpen )
            {
                const EditorPropertyGridTarget target{ pComp, pComp->getTypeInfo(), pComp, pObj, {}, {} };
                _propertyGrid.drawProperties( target, nullptr, {} );
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Plus" ), SW_LOCKEY( "EdButton", "AddComponent" ), "Add Component" ) ) )
        {
            _addComponentSearch.clear();
            EditorInputGUI::openPopup( "PreviewAddComponent" );
        }
        if ( ImGui::BeginPopup( "PreviewAddComponent" ) )
        {
            bool bFailed{ false };
            if ( EditorComponentMenu::drawAddComponentList( pObj, _addComponentSearch, "inspector.previewAddComponent", ImGui::IsWindowAppearing(), bFailed ) )
                ImGui::CloseCurrentPopup();
            if ( bFailed )
                SW_LOG_WARNING_ID( "InspectorPanel.CouldNotAddComponent", "Could not add the component to '%#'", pObj->getName().c_str() );
            ImGui::EndPopup();
        }
    }

    void InspectorPanel::drawSceneAssemblySection( GameObject* pObj )
    {
        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr )
            return;
        const uint64                     objectID         = pObj->getObjectID();
        const string_view                instancePath     = pScene->getSceneInstancePath( objectID );
        const uint64                     anchorObjectID   = pScene->findInstanceAnchorObjectID( objectID );
        const bool                       bInstanceRelated = instancePath.empty() == false || anchorObjectID != 0;
        const vector<SceneDataLayerDef>& listLayer        = pScene->getSceneSettings()._listDataLayer;
        if ( bInstanceRelated == false && listLayer.empty() )
            return;

        if ( bInstanceRelated )
        {
            EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "SceneInstance" ), SW_THEME_COLOR( "Base", "Accent" ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "SceneInstance" ) );
            ImGui::SameLine();
            const GameObject* pAnchor = anchorObjectID != 0 && pObj->getManager() != nullptr ? pObj->getManager()->findGameObjectByID( anchorObjectID ) : nullptr;
            if ( pAnchor != nullptr )
                ImGui::TextDisabled( "%s", pAnchor->getName().c_str() );
            else
                ImGui::TextDisabled( "%.*s", static_cast<int32>( instancePath.size() ), instancePath.data() );
            // 인스턴스의 차이는 앵커의 `<Part>` 로 저장된다 — 내부 씬 파일은 그대로다.
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ApplyInstanceOverrides" ) ) )
                (void)editor::getSubsystem<SceneManager>().saveActiveScene(); // 실패는 저장이 알린다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ApplyInstanceOverrides" ) );
        }

        // 멤버의 레이어는 내부 씬의 엔티티와 앵커가 정한다 — 여기서 고치지 않는다.
        if ( listLayer.empty() == false && anchorObjectID == 0 )
        {
            const vector<string>* pEntityLayer = pScene->findEntityDataLayers( objectID );
            vector<string>        listEntityLayer;
            if ( pEntityLayer != nullptr )
                listEntityLayer = *pEntityLayer;
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "DataLayers" ) );
            for ( const SceneDataLayerDef& layer : listLayer )
            {
                ImGui::PushID( layer._name.c_str() );
                vector<string> listOtherLayer;
                for ( const string& entityLayer : listEntityLayer )
                {
                    if ( entityLayer != layer._name )
                        listOtherLayer.push_back( entityLayer );
                }
                bool bOnLayer = listOtherLayer.size() != listEntityLayer.size();
                if ( ImGui::Checkbox( layer._name.c_str(), &bOnLayer ) )
                {
                    if ( bOnLayer )
                        listOtherLayer.push_back( layer._name );
                    listEntityLayer = listOtherLayer;
                    (void)pScene->setEntityDataLayers( objectID, listEntityLayer ); // 멤버는 위에서 걸렀다
                }
                ImGui::PopID();
            }
        }
        ImGui::Separator();
    }

    void InspectorPanel::drawPrefabLinkSection( GameObject* pObj, const AssetID& assetID )
    {
        const string_view prefabPath = assetID.view();
        EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "Prefab" ), SW_THEME_COLOR( "Base", "Accent" ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "PrefabInstance" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%.*s", static_cast<int32>( prefabPath.size() ), prefabPath.data() );

        if ( ImGui::Button( SW_LOCKEY( "EdText", "ApplyToPrefab" ) ) )
            (void)EditorInspectorCommands::applyToPrefab( pObj, assetID ); // 실패는 프리팹 저장이 알린다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ApplyToPrefab" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RevertToPrefab" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RevertToPrefab" ) ) && EditorInspectorCommands::revertToPrefab( pObj, prefabPath ) == false )
            SW_LOG_WARNING_ID( "InspectorPanel.RevertPrefabFailed", "Revert to Prefab failed for '%#' (%#)", pObj->getName().c_str(), prefabPath );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "RevertToPrefab" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Unlink" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Unlink" ) ) )
            (void)EditorInspectorCommands::unlinkPrefab( pObj ); // 거절 이유는 명령이 경고 로그로 남긴다. 부품이면 맨 바깥 인스턴스를 한 겹 푼다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "UnlinkPrefab" ) );

        // 중첩 부품이면 바깥 인스턴스의 프리팹에도 적용할 수 있다 — 부품의 수정이 바깥 프리팹의 노드 오버라이드(`<Part>` 포함)가 된다.
        const GameObject* pOuter = EditorInspectorCommands::findOuterInstance( pObj );
        if ( pOuter != nullptr )
        {
            const string_view outerPath = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().findPrefabPath( pOuter );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ApplyToOuterPrefab" ) ) )
                (void)EditorInspectorCommands::applyToOuterPrefab( pObj ); // 실패는 프리팹 저장이 알린다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ApplyToPrefab" ) );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s: %.*s", pOuter->getName().c_str(), static_cast<int32>( outerPath.size() ), outerPath.data() );
        }

        ImGui::Separator();
    }

    void InspectorPanel::drawComponentList( GameObject* pObj, EditorWorkspace& workspace )
    {
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Components" ) );
        // 복사한다. 아래 루프가 컴포넌트를 제거할 수 있어 원본을 순회할 수 없다.
        const vector<Component*> listComponent( pObj->getComponents().begin(), pObj->getComponents().end() );
        for ( Component* pComp : listComponent )
        {
            if ( pComp == nullptr )
                continue;

            const utf8* pComponentName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str() : "Component";
            // 카드 헤더 앞에 컴포넌트 아이콘을 둔다. 유니티 Inspector 와 언리얼 Details 의 컴포넌트 헤더에 해당한다
            fixed_string<constant::buffer::kMax128> cardTitle;
            formatstring( cardTitle.data(), cardTitle.capacity(), "%#  %#", EditorComponentIconRow::findRow( pComp->getTypeInfo() )._pGlyph, pComponentName );
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

                drawComponentContextMenu( pComp );

                drawComponentSection( pComp );
                EditorWidgets::endComponentCard();
            }
            if ( bActive != bWasActive )
                InspectorObjectEdit::applyObjectEdit( pObj, "Toggle Component Active", [pComp, bActive]()
                { pComp->setActive( bActive ); } );

            if ( bRemove )
            {
                EditorSceneObjectCommands::removeComponent( pObj, pComp ); // 메뉴의 Remove Component 와 같은 한 곳
                break;
            }
        }

        // 컴포넌트 목록 끝의 Add Component 다. 유니티 Inspector 맨 아래, 언리얼 Details 의 Add 에 해당하며 Hierarchy 메뉴와 같은 목록이다.
        ImGui::Spacing();
        const float32 buttonWidth = MathUtil::min( ImGui::GetContentRegionAvail().x, 220.0f * EditorThemeUtil::getDPIScale() );
        ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( ImGui::GetContentRegionAvail().x - buttonWidth ) * 0.5f );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Plus" ), SW_LOCKEY( "EdButton", "AddComponent" ), "Add Component" ), ImVec2{ buttonWidth, 0.0f } ) )
        {
            _addComponentSearch.clear();
            EditorInputGUI::openPopup( "InspectorAddComponent" );
        }
        EditorSelfTestMarks::record( "inspector.addComponent" );
        if ( ImGui::BeginPopup( "InspectorAddComponent" ) )
        {
            bool bFailed{ false };
            if ( EditorComponentMenu::drawAddComponentList( pObj, _addComponentSearch, "inspector.addComponent", ImGui::IsWindowAppearing(), bFailed ) )
                ImGui::CloseCurrentPopup();
            if ( bFailed )
                SW_LOG_WARNING_ID( "InspectorPanel.CouldNotAddComponent", "Could not add the component to '%#'", pObj->getName().c_str() );
            ImGui::EndPopup();
        }
    }

    void InspectorPanel::drawComponentContextMenu( const Component* pComp )
    {
        // 계층 창의 컴포넌트 행 · 미리보기 카드와 같은 메뉴다(`scene.component.*` 와 프리셋 하위 메뉴). 선택은 오브젝트 단위라 컴포넌트 선택을 바꾸지 않는다.
        EditorContextMenuSubject subject{};
        subject._pPopupID  = "CompCardCtx";
        subject._kind      = EditorCommandTargetKind::Component;
        subject._hoveredID = pComp->getComponentID();
        (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
    }

    void InspectorPanel::drawComponentPresetItems( Component* pComp )
    {
        ImGui::InputTextWithHint( "##presetName", SW_LOCKEY( "EditorInspector", "PresetNameHint" ), _presetNameBuffer.data(), _presetNameBuffer.capacity() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) && _presetNameBuffer.empty() == false )
        {
            if ( EditorTransformCommands::saveComponentPreset( pComp, _presetNameBuffer.c_str() ) )
            {
                _presetNameBuffer.clear();
                _bComponentPresetDirty = SW_TRUE;
            }
            else
            {
                SW_LOG_ERROR_ID( "InspectorPanel.CouldNotSaveComponent", "Could not save component preset '%#'", _presetNameBuffer.c_str() );
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
            _componentPresetJob.request( EditorGlobalVariableCommands::getComponentPresetFolderPath(), ".preset.xml", false );
        }

        bool bFoundPresets{ false };
        for ( const string& presetFile : _listComponentPresetFile )
        {
            const string displayPreset = EditorTransformCommands::getComponentPresetName( pComp, presetFile );
            if ( displayPreset.empty() )
                continue;
            bFoundPresets = true;
            if ( ImGui::MenuItem( displayPreset.c_str() ) && EditorTransformCommands::loadComponentPreset( pComp, presetFile ) == false )
                SW_LOG_ERROR_ID( "InspectorPanel.CouldNotApplyComponent", "Could not apply component preset '%#'", presetFile.c_str() );
        }
        if ( bFoundPresets == false )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoSavedPresets" ) );
    }

    void InspectorPanel::drawGameObjectHeader( GameObject* pObj )
    {
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "GameObjectID", TextArgumentList().addInteger( "objectID", static_cast<int64>( pObj->getObjectID() ) ) ).c_str() );

        // 편집 중이 아니면 오브젝트 이름으로 채운다. 편집 중인 텍스트를 멤버로 보관해야 입력란을 떠난 프레임(ImGui 가 버퍼에 쓰지 않는다)에도 적용할 수 있다.
        // Enter 를 누르거나 다른 곳을 눌러 떠나도 적용한다(유니티와 언리얼). Esc 는 ImGui 가 텍스트를 되돌려 편집 없음으로 끝난다.
        const bool bEditingName = ImGui::GetActiveID() == ImGui::GetID( "##Name" ) && _nameEditObjectID == pObj->getObjectID();
        if ( bEditingName == false )
        {
            _nameEditBuffer   = pObj->getName().c_str();
            _nameEditObjectID = pObj->getObjectID();
        }
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "NameAlt" ) );
        const bool bEnter         = ImGui::InputText( "##Name", _nameEditBuffer.data(), _nameEditBuffer.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        const bool bLeftAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        const bool bNameChanged   = _nameEditBuffer.empty() == false && pObj->getName().isEqual( hashed_string( _nameEditBuffer.c_str() ), NameCase::CaseSensitive ) == false;
        if ( ( bEnter || bLeftAfterEdit ) && bNameChanged )
            InspectorObjectEdit::applyObjectEdit( pObj, "Rename GameObject", [pObj, this]()
            { pObj->setName( hashed_string( _nameEditBuffer.c_str() ) ); } );
        EditorSelfTestMarks::record( "inspector.name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ObjectName" ) );

        bool bActive = pObj->isActive();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Active" ), &bActive ) )
            EditorSceneCommands::setActive( pObj, bActive );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ObjectActive" ) );

        GameObject* pParent = pObj->getParent();
        if ( pParent != nullptr )
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Parent", TextArgumentList().addText( "parent", pParent->getName().c_str() ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Unparent" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Unparent" ) ) )
                InspectorObjectEdit::applyObjectEdit( pObj, "Unparent GameObject", [pObj]()
                { pObj->detachFromParent(); } );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "Unparent" ) );
        }
        else
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "ParentRoot" ) );

        const vector<TagID>& listTag = pObj->getTags().getTags();
        if ( listTag.empty() == false )
        {
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Tags" ) );
            for ( const TagID& tag : listTag )
            {
                if ( StringUtil::isNullOrEmpty( tag._pString ) == false )
                {
                    ImGui::SameLine();
                    EditorWidgets::drawChip( tag._pString, SW_THEME_COLOR( "Signal", "Success" ) );
                }
            }
        }
    }

    void InspectorPanel::drawComponentSection( Component* pComp )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "ID", TextArgumentList().addInteger( "componentID", static_cast<int64>( pComp->getComponentID() ) ) ).c_str() );

        const TypeInfo* pTypeInfo = pComp->getTypeInfo();
        if ( pTypeInfo == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NoTypeInfo" ) );
            return;
        }

        // 확장은 이 타입과 그 기반 타입들에 등록된 것 전부다(기반에서 파생 순서). 각자 자기 영역을 그리고 직접 그린 리플렉션 프로퍼티만 감춘다. 나머지 리플렉션
        // 프로퍼티는 상속한 것까지 아래에서 그린다(언리얼 Details 의 `IDetailCustomization` 과 `HideProperty`).
        const vector<IInspectorComponent*>& listInspector = editorContext.getSubsystem<InspectorComponentManager>().getInspectorsForType( *pTypeInfo );
        vector<hashed_string>               listDrawnName;
        for ( IInspectorComponent* pInspector : listInspector )
        {
            pInspector->drawHeader( pComp );
        }
        for ( IInspectorComponent* pInspector : listInspector )
        {
            // 확장 영역도 프로퍼티 위젯과 같은 규칙으로 되돌리기와 dirty 에 남긴다. 영역을 하나로 묶어 위젯 하나처럼 추적하므로
            // 영역이 위젯마다 `trackPOD` 를 호출하지 않아도 된다.
            ImGui::BeginGroup();
            pInspector->drawSection( pComp );
            ImGui::EndGroup();
            InspectorPropertyUndo::trackLastItem( pTypeInfo->getDisplayName() );
            pInspector->collectDrawnProperties( listDrawnName );
        }

        const EditorPropertyGridTarget componentTarget{ pComp, pTypeInfo, pComp, pComp->getOwner(), {}, {} };
        _propertyGrid.drawProperties( componentTarget, "Properties", listDrawnName );
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Methods" ) );
        _propertyGrid.drawMethodsAndEvents( componentTarget, true );

        for ( IInspectorComponent* pInspector : listInspector )
        {
            pInspector->drawFooter( pComp );
        }
    }
} // namespace sw::editor
