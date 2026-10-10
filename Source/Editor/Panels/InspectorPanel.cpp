#include "pch.h"

#include "Editor/Panels/InspectorPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/EditorInspectorCommands.h"
#include "Editor/Common/Commands/EditorMultiEdit.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Commands/EditorTransformCommands.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorComponentIcon.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorComponentMenu.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectValidation.h"
#include "Engine/Reflection/PropertyEditCondition.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Utility/CommandStack.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct InspectorPanelInternal
        {
            /**
             * @brief 오브젝트 하나를 바꾸는 인스펙터 편집 — 멈춰 있으면 되돌리기 기록과 씬 dirty 를 남기고, 플레이 중이면 바로 바꿉니다.
             * @details 이름 · 활성 · 부모 해제 · 컴포넌트 활성 · 컴포넌트 제거가 이것을 지난다 — 기록도 dirty 도 없이 바꾸면 그 편집만
             *          하고 다른 씬을 열거나 끌 때 묻지도 않고 사라지고 Ctrl+Z 로도 돌릴 수 없다. 플레이 중에는 씬 명령이 막히므로
             *          (플레이 사본은 Stop 이 되돌린다) 바로 바꾼다.
             */
            template <typename EditFunc>
            static void applyObjectEdit( GameObject* pObj, string_view undoLabel, EditFunc&& edit )
            {
                if ( pObj == nullptr )
                    return;
                if ( EditorUtil::areSceneEditsAllowed() == false )
                {
                    edit();
                    return;
                }
                const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
                edit();
                EditorSceneCommands::commitModify( pObj, beforeSnapshot, undoLabel );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( InspectorPanel, "inspector", EditorPanelCategory::Core, 200 );

    InspectorPanel::InspectorPanel()
        : _propertyGrid{}
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
        , _bComponentPresetDirty{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void InspectorPanel::drawContent()
    {
        EditorWidgets::pushInspectorStyle();
        drawSelectionSection();
        InspectorPropertyUndo::commitFinishedEdits();

        // Undo/Redo 단축키는 여기서 처리하지 않는다 — edit.undo / edit.redo 커맨드가 유일한 처리자다.
        // ImGui 의 IsKeyPressed 는 소비되지 않으므로 여기서도 받으면 전역 처리기(EditorCommandGUI)와 함께
        // **두 번 되돌린다.**

        EditorWidgets::popInspectorStyle();
    }

    void InspectorPanel::drawSelectionSection()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        EditorWidgets::drawSectionHeader( "Selection" );
        // 잠그면 선택이 바뀌어도 지금 오브젝트를 계속 보인다(유니티 Inspector 자물쇠 · 언리얼의 여러 Details 창 대신).
        ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() );
        const bool bLocked = _lockedObjectID != 0;
        if ( EditorWidgets::drawToggleIconButton( "##inspectorLock", bLocked, editoricon::kLock, editoricon::kUnlock, "Locked to this object - click to follow the selection",
                                                  "Following the selection - click to lock to this object" ) )
            _lockedObjectID = bLocked ? 0 : pContext->getWorkspace().getSelectedObjectID();
        EditorSelfTestMarks::note( "inspector.lock" );

        EditorWorkspace&    ws = pContext->getWorkspace();
        vector<GameObject*> listSelected;
        pContext->getEditorSelection().getSelectedObjects( listSelected );
        if ( listSelected.size() > 1 && _lockedObjectID == 0 )
        {
            drawMultiSelection( listSelected );
            return;
        }

        const uint64 shownObjectID = _lockedObjectID != 0 ? _lockedObjectID : ws.getSelectedObjectID();
        if ( shownObjectID == 0 )
        {
            EditorWidgets::drawEmptyHint( "Nothing selected. Pick in the Scene view or use Hierarchy." );
            return;
        }

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            EditorWidgets::drawEmptyHint( "No active scene." );
            return;
        }

        GameObject* pObj = pScene->getObjectManager()->findGameObjectByID( shownObjectID );
        if ( pObj == nullptr )
        {
            EditorWidgets::drawEmptyHint( "Selected object no longer exists." );
            if ( _lockedObjectID != 0 )
                _lockedObjectID = 0; // 잠근 오브젝트가 사라졌다 — 선택을 따라가는 데로 돌아간다
            else
                ws.clearSelection();
            return;
        }

        // 플레이 중에도 값은 고친다(유니티 플레이 모드 편집) — Stop 이 스냅샷으로 되돌리고, 남기려면 Keep Simulation Changes.
        // 구조 편집(컴포넌트 더하기 · 프리팹)은 씬 명령이 플레이 중에 스스로 거절한다.
        if ( EditorUtil::areSceneEditsAllowed() == false )
        {
            EditorWidgets::drawChip( "Play Mode", editor::style::kWarn );
            ImGui::SameLine();
            ImGui::TextDisabled( "Edits revert on Stop (Play > Keep Simulation Changes keeps the selection)." );
        }

        drawGameObjectHeader( pObj );

        ImGui::Spacing();
        ImGui::Separator();

        const string& pfbPath = ws.getGameObjectPrefabPath( pObj->getObjectID() );
        if ( pfbPath.empty() == false )
            drawPrefabLinkSection( pObj, pfbPath );

        _propertyGrid.drawSearchBar();
        EditorWidgets::drawTooltip( "프로퍼티 및 컴포넌트 이름을 검색하여 필터링합니다" );
        ImGui::Spacing();

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
        ImGui::TextColored( ImVec4{ 0.4f, 0.7f, 1.0f, 1.0f }, "%u objects selected", static_cast<uint32>( listObject.size() ) );
        ImGui::TextDisabled( "Primary: %s", pPrimary->getName().c_str() );

        const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
        if ( bEditsAllowed == false )
            ImGui::BeginDisabled();

        // 활성은 모두에 — 섞여 있으면 혼합 표시(체크는 주 선택 값).
        bool bMixedActive = false;
        for ( const GameObject* pObject : listObject )
        {
            bMixedActive = bMixedActive || pObject->isActive() != pPrimary->isActive();
        }
        bool bActive = pPrimary->isActive();
        if ( bMixedActive )
        {
            ImGui::TextDisabled( "\xe2\x80\x94" );
            EditorWidgets::drawTooltip( "Multiple values" );
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

        _propertyGrid.drawSearchBar();
        ImGui::SeparatorText( "Components" );
        EditorMultiEditUtil::collectCommonComponents( listObject, _listMultiEditComponent );
        for ( EditorMultiEditComponent& common : _listMultiEditComponent )
        {
            Component* pComponent = common._listComponent[0];
            ImGui::PushID( pComponent->getTypeInfo()->_name.c_str() );
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
        vector<const void*> listInstance;
        for ( const Component* pComponent : _pMultiEditCurrent->_listComponent )
        {
            listInstance.push_back( pComponent );
        }
        return EditorMultiEditUtil::hasMixedValues( prop, listInstance );
    }

    void InspectorPanel::drawPrefabLinkSection( GameObject* pObj, const string& prefabPath )
    {
        EditorWidgets::drawChip( "Prefab", editor::style::kAccent );
        EditorWidgets::drawTooltip( "프리팹 인스턴스입니다" );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", prefabPath.c_str() );

        if ( ImGui::Button( "Apply to Prefab" ) )
            (void)EditorInspectorCommands::applyToPrefab( pObj, prefabPath ); // 실패는 프리팹 저장이 알린다
        EditorWidgets::drawTooltip( "현재 오브젝트의 변경사항을 프리팹 원본 파일에 저장합니다" );

        ImGui::SameLine();
        if ( ImGui::Button( "Revert to Prefab" ) && EditorInspectorCommands::revertToPrefab( pObj, prefabPath ) == false )
            SW_LOG_WARNING( "Revert to Prefab failed for '%#' (%#)", pObj->getName().c_str(), prefabPath.c_str() );
        EditorWidgets::drawTooltip( "프리팹 원본 파일의 내용으로 현재 오브젝트를 되돌립니다" );

        ImGui::SameLine();
        if ( ImGui::Button( "Unlink" ) )
            EditorInspectorCommands::unlinkPrefab( pObj );
        EditorWidgets::drawTooltip( "프리팹과의 연결을 끊고 독립된 일반 오브젝트로 변환합니다" );

        ImGui::Separator();
    }

    void InspectorPanel::drawComponentList( GameObject* pObj, EditorWorkspace& workspace )
    {
        ImGui::SeparatorText( "Components" );
        EditorContext* pSelEditorContext = EditorContext::get();
        IRHIDevice*    pRHIDevice        = ( pSelEditorContext != nullptr ) ? pSelEditorContext->getRHIDevice() : nullptr;
        // 복사한다. 아래 루프가 컴포넌트를 뗄 수 있어 원본을 돌 수 없다.
        const vector<Component*> listComponent( pObj->getComponents().begin(), pObj->getComponents().end() );
        for ( Component* pComp : listComponent )
        {
            if ( pComp == nullptr )
                continue;

            const utf8* pComponentName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str() : "Component";
            // 카드 머리 앞에 컴포넌트 아이콘(유니티 Inspector · 언리얼 Details 의 컴포넌트 머리)
            fixed_string<constant::kMaxBuffer128> cardTitle;
            formatstring( cardTitle.data(), cardTitle.capacity(), "%#  %#", EditorComponentIcon::findRow( pComp->getTypeInfo() )._pGlyph, pComponentName );
            const utf8* pName = cardTitle.c_str();
            // 체크박스는 컴포넌트 **자기** 비트다. 주의: 실효값(isActive — 소유 오브젝트의 계층 활성까지)을 읽어 그대로 다시 쓰면
            // 꺼진 부모 아래의 컴포넌트는 인스펙터에 보이기만 해도 자기 비트가 꺼진다. 쓰는 것도 바뀐 때 한 번이다(아래).
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

            if ( EditorWidgets::beginComponentCard( pName, pComp->getComponentID(), &bActive, &bRemove, bAccent ) )
            {
                if ( bScrollTo )
                    ImGui::SetScrollHereY( 0.25f );

                drawComponentContextMenu( pObj, pComp, workspace, bRemove );

                drawComponentSection( pComp, pRHIDevice );
                EditorWidgets::endComponentCard();
            }
            if ( bActive != bWasActive )
                InspectorPanelInternal::applyObjectEdit( pObj, "Toggle Component Active", [pComp, bActive]()
                { pComp->setActive( bActive ); } );

            if ( _pPendingMoveComponent == pComp && _pendingMoveDirection != 0 )
            {
                const int32 direction = _pendingMoveDirection;
                InspectorPanelInternal::applyObjectEdit( pObj, direction < 0 ? "Move Component Up" : "Move Component Down", [pObj, pComp, direction]()
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
                else if ( EditorUtil::areSceneEditsAllowed() )
                    EditorSceneCommands::destroyComponent( pGameObjectManager, pObj, pComp ); // 기록 · dirty
                else
                    pGameObjectManager->destroyComponent( pComp ); // 플레이 사본 — Stop 이 되돌린다
                break;
            }
        }

        // 컴포넌트 목록 끝의 Add Component(유니티 Inspector 맨 아래 · 언리얼 Details 의 Add) — Hierarchy 메뉴와 같은 목록이다.
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
                if ( ImGui::MenuItem( "Copy Component" ) )
                    workspace.copyComponent( pComp );
                const string compTypeName = pComp->getTypeName().c_str();
                const bool   bCanPaste    = ( workspace.hasCopiedComponent() &&
                                         workspace.getCopiedComponentTypeName() == compTypeName );
                if ( bCanPaste )
                {
                    if ( ImGui::MenuItem( "Paste Component Values" ) )
                        workspace.pasteComponentValues( pComp );
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
                        workspace.pasteComponentAsNew( pObj );
                }
                ImGui::Separator();
                // 주 씬 컴포넌트(뿌리 트랜스폼)는 옮기지 않고, 그 자리로 옮겨 들어가지도 않는다(`GameObject::moveComponent`).
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

                ImGui::Separator();
                if ( ImGui::BeginMenu( "Presets" ) )
                {
                    static fixed_string<constant::kMaxBuffer64> s_presetNameBuf;
                    ImGui::InputTextWithHint( "##presetName", "Preset Name...", s_presetNameBuf.data(),
                                              s_presetNameBuf.capacity() );
                    ImGui::SameLine();
                    if ( ImGui::Button( "Save" ) && s_presetNameBuf.empty() == false )
                    {
                        if ( workspace.saveComponentPreset( pComp, s_presetNameBuf.c_str() ) )
                        {
                            s_presetNameBuf.clear();
                            _bComponentPresetDirty = SW_TRUE;
                        }
                        else
                        {
                            SW_LOG_ERROR( "Could not save component preset '%#'", s_presetNameBuf.c_str() );
                        }
                    }
                    ImGui::Separator();

                    if ( _bComponentPresetDirty == SW_TRUE && _componentPresetJob.isPending() == false )
                    {
                        _componentPresetJob.request(
                            EditorGlobalVariableCommands::getComponentPresetFolderPath(), ".preset.xml", false );
                    }

                    vector<string> listNewPresetFile;
                    if ( _componentPresetJob.take( listNewPresetFile ) )
                    {
                        _listComponentPresetFile = std::move( listNewPresetFile );
                        _bComponentPresetDirty   = SW_FALSE;
                    }

                    bool bFoundPresets{ false };
                    for ( const string& presetFile : _listComponentPresetFile )
                    {
                        const string displayPreset = EditorTransformCommands::getComponentPresetName( pComp, presetFile );
                        if ( displayPreset.empty() == false )
                        {
                            bFoundPresets = true;
                            if ( ImGui::MenuItem( displayPreset.c_str() ) )
                                if ( workspace.loadComponentPreset( pComp, presetFile ) == false )
                                    SW_LOG_ERROR( "Could not apply component preset '%#'", presetFile.c_str() );
                        }
                    }
                    if ( bFoundPresets == false )
                        ImGui::TextDisabled( "No saved presets." );

                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( "Remove Component" ) )
                bOutRemove = true;
            ImGui::EndPopup();
        }
    }

    void InspectorPanel::drawGameObjectHeader( GameObject* pObj )
    {
        ImGui::Text( "GameObject  ID: %u", static_cast<uint32>( pObj->getObjectID() ) );

        // 편집 중이 아니면 오브젝트 이름으로 채운다. 편집 중인 글을 멤버로 들어야 칸을 떠난 프레임(ImGui 가 버퍼에 쓰지 않는다)에도 적용할 수 있다 —
        // Enter 로도, 다른 곳을 눌러 떠나도 적용한다(유니티 · 언리얼). Esc 는 ImGui 가 글을 되돌려 편집 없음으로 끝난다.
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
            InspectorPanelInternal::applyObjectEdit( pObj, "Rename GameObject", [pObj, this]()
            { pObj->setName( hashed_string( _nameEditBuffer.c_str() ) ); } );
        EditorSelfTestMarks::note( "inspector.name" );
        EditorWidgets::drawTooltip( "게임오브젝트의 고유 이름 (Enter 키로 적용)" );

        bool bActive = pObj->isActive();
        if ( ImGui::Checkbox( "Active", &bActive ) )
            EditorSceneCommands::setActive( pObj, bActive );
        EditorWidgets::drawTooltip( "게임오브젝트의 활성화 상태를 토글합니다" );

        GameObject* pParent = pObj->getParent();
        if ( pParent != nullptr )
        {
            ImGui::Text( "Parent: %s", pParent->getName().c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Unparent" ) )
                InspectorPanelInternal::applyObjectEdit( pObj, "Unparent GameObject", [pObj]()
                { pObj->detachFromParent(); } );
            EditorWidgets::drawTooltip( "부모 오브젝트와의 연결을 해제하고 씬 루트로 이동합니다" );
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
                    EditorWidgets::drawChip( tag._pString, editor::style::kOk );
                }
            }
        }
    }

    void InspectorPanel::drawComponentSection( Component* pComp, IRHIDevice* pRHIDevice )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        ImGui::TextDisabled( "ID: %u", static_cast<uint32>( pComp->getComponentID() ) );

        const TypeInfo* pTypeInfo = pComp->getTypeInfo();
        if ( pTypeInfo == nullptr )
        {
            EditorWidgets::drawEmptyHint( "No TypeInfo registered for this component." );
            return;
        }

        // 확장은 이 타입과 그 기반들에 등록된 것 전부다(기반 → 파생). 각자 자기 구역을 그리고 직접 그린 반사 프로퍼티만 감춘다 — 나머지 반사
        // 프로퍼티는 상속분까지 아래에서 그린다(언리얼 Details 의 `IDetailCustomization` · `HideProperty`).
        vector<IInspectorComponent*> listInspector;
        pContext->getInspectorComponentManager().collectForType( *pTypeInfo, listInspector );
        vector<hashed_string> listDrawnName;
        for ( IInspectorComponent* pInspector : listInspector )
        {
            pInspector->drawHeader( pComp );
        }
        for ( IInspectorComponent* pInspector : listInspector )
        {
            // 확장 구역도 프로퍼티 위젯과 같은 규칙으로 되돌리기 · dirty 에 남긴다 — 구역을 한 묶음으로 닫고 그 묶음을 위젯 하나처럼 추적한다
            // (구역이 위젯마다 `trackPod` 를 부르지 않아도 된다).
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
