#include "pch.h"

#include "Editor/Panels/InspectorPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/EditorInspectorCommands.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Commands/EditorTransformCommands.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
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
#include "Engine/Serialization/Core/SerializerUtil.h"
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

            /** @brief 컨테이너 구조 편집(더하기 · 지우기 · 다시 넣기)입니다. 주인 오브젝트가 있으면 되돌리기에 남기고, 없으면 그냥 바꿉니다. */
            template <typename EditFunc>
            static void applyContainerEdit( GameObject* pOwner, string_view undoLabel, EditFunc&& edit )
            {
                if ( pOwner == nullptr )
                    edit();
                else
                    applyObjectEdit( pOwner, undoLabel, std::forward<EditFunc>( edit ) );
            }

            /** @brief 키 · 원소 하나를 값 자리에 둔 프로퍼티입니다(오프셋 0). 글 변환(`SerializerUtil::formatPropertyText` · `applyPropertyText`)에 씁니다. */
            static PropertyInfo makeValueProperty( const hashed_string& typeName )
            {
                PropertyInfo valueProp{};
                valueProp._typeName = typeName;
                return valueProp;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( InspectorPanel, "inspector", EditorPanelCategory::Core, 200 );

    InspectorPanel::InspectorPanel()
        : _propertyFilter{}
        , _mapMethodArgSlot{}
        , _lastInvokeResult{}
        , _nameEditBuffer{}
        , _componentPresetJob{}
        , _listComponentPresetFile{}
        , _pEditTargetComponent{ nullptr }
        , _pEditTargetObject{ nullptr }
        , _mapContainerAddText{}
        , _nameEditObjectId{ 0 }
        , _propertyDrawDepth{ 0 }
        , _bComponentPresetDirty{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void InspectorPanel::drawContent()
    {
        EditorWidgets::pushInspectorStyle();
        drawSelectionSection();

        // Undo/Redo 단축키는 여기서 처리하지 않는다 — edit.undo / edit.redo 커맨드가 유일한 처리자다.
        // ImGui 의 IsKeyPressed 는 소비되지 않으므로 여기서도 받으면 전역 처리기(EditorCommandGui)와 함께
        // **두 번 되돌린다.**

        EditorWidgets::popInspectorStyle();
    }

    void InspectorPanel::drawSelectionSection()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        EditorWidgets::drawSectionHeader( "Selection" );

        const size_t selCount = pContext->getEditorSelection().getSelectedObjectCount();
        if ( selCount > 1 )
        {
            ImGui::TextColored( ImVec4{ 0.4f, 0.7f, 1.0f, 1.0f }, "Multi-Selection (%u objects)",
                                static_cast<uint32>( selCount ) );
            ImGui::Separator();
        }

        EditorWorkspace& ws = pContext->getWorkspace();
        if ( ws.getSelectedObjectId() == 0 )
        {
            EditorWidgets::drawEmptyHint( "Nothing selected. Pick in Game View or use Hierarchy." );
            return;
        }

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            EditorWidgets::drawEmptyHint( "No active scene." );
            return;
        }

        GameObject* pObj = pScene->getObjectManager()->findGameObjectById( ws.getSelectedObjectId() );
        if ( pObj == nullptr )
        {
            EditorWidgets::drawEmptyHint( "Selected object no longer exists." );
            ws.clearSelection();
            return;
        }

        const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
        if ( bEditsAllowed == false )
        {
            EditorWidgets::drawChip( "Play Mode", editor::style::kWarn );
            ImGui::SameLine();
            ImGui::TextDisabled( "Scene edits locked until Stop." );
        }
        if ( bEditsAllowed == false )
            ImGui::BeginDisabled();

        drawGameObjectHeader( pObj );

        ImGui::Spacing();
        ImGui::Separator();

        const string& pfbPath = ws.getGameObjectPrefabPath( pObj->getObjectId() );
        if ( pfbPath.empty() == false )
            drawPrefabLinkSection( pObj, pfbPath );

        EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, "Search properties..." );
        EditorWidgets::drawTooltip( "프로퍼티 및 컴포넌트 이름을 검색하여 필터링합니다" );
        ImGui::Spacing();

        const TypeInfo* pTypeInfo = pObj->getTypeInfo();
        if ( pTypeInfo != nullptr )
        {
            _pEditTargetObject = pObj;
            drawTypeProperties( pObj, pTypeInfo, "Reflected Properties", {} );
            ImGui::SeparatorText( "Methods" );
            drawTypeMethods( pObj, pTypeInfo );
            _pEditTargetObject = nullptr;
        }

        drawComponentList( pObj, ws );

        if ( bEditsAllowed == false )
            ImGui::EndDisabled();
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
        IRHIDevice*    pRhiDevice        = ( pSelEditorContext != nullptr ) ? pSelEditorContext->getRhiDevice() : nullptr;
        // 복사한다. 아래 루프가 컴포넌트를 뗄 수 있어 원본을 돌 수 없다.
        const vector<Component*> listComponent( pObj->getComponents().begin(), pObj->getComponents().end() );
        for ( Component* pComp : listComponent )
        {
            if ( pComp == nullptr )
                continue;

            const utf8* pName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str() : "Component";
            // 체크박스는 컴포넌트 **자기** 비트다. 주의: 실효값(isActive — 소유 오브젝트의 계층 활성까지)을 읽어 그대로 다시 쓰면
            // 꺼진 부모 아래의 컴포넌트는 인스펙터에 보이기만 해도 자기 비트가 꺼진다. 쓰는 것도 바뀐 때 한 번이다(아래).
            const bool bWasActive = pComp->isSelfActive();
            bool       bActive    = bWasActive;
            bool       bRemove{ false };
            const bool bAccent   = isA<SceneComponent>( pComp );
            const bool bScrollTo = ( workspace.getScrollToComponentId() != 0 &&
                                     workspace.getScrollToComponentId() == pComp->getComponentId() );

            if ( bScrollTo )
            {
                ImGui::SetNextItemOpen( true );
                workspace.setScrollToComponentId( 0 );
            }

            if ( EditorWidgets::beginComponentCard( pName, pComp->getComponentId(), &bActive, &bRemove, bAccent ) )
            {
                if ( bScrollTo )
                    ImGui::SetScrollHereY( 0.25f );

                drawComponentContextMenu( pObj, pComp, workspace, bRemove );

                drawComponentSection( pComp, pRhiDevice );
                EditorWidgets::endComponentCard();
            }
            if ( bActive != bWasActive )
                InspectorPanelInternal::applyObjectEdit( pObj, "Toggle Component Active", [pComp, bActive]()
                { pComp->setActive( bActive ); } );

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
        ImGui::Text( "GameObject  ID: %u", static_cast<uint32>( pObj->getObjectId() ) );

        // 편집 중이 아니면 오브젝트 이름으로 채운다. 편집 중인 글을 멤버로 들어야 칸을 떠난 프레임(ImGui 가 버퍼에 쓰지 않는다)에도 적용할 수 있다 —
        // Enter 로도, 다른 곳을 눌러 떠나도 적용한다(유니티 · 언리얼). Esc 는 ImGui 가 글을 되돌려 편집 없음으로 끝난다.
        const bool bEditingName = ImGui::GetActiveID() == ImGui::GetID( "Name" ) && _nameEditObjectId == pObj->getObjectId();
        if ( bEditingName == false )
        {
            _nameEditBuffer   = pObj->getName().c_str();
            _nameEditObjectId = pObj->getObjectId();
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

    void InspectorPanel::drawComponentSection( Component* pComp, IRHIDevice* pRhiDevice )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        ImGui::TextDisabled( "ID: %u", static_cast<uint32>( pComp->getComponentId() ) );

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
            pInspector->drawSection( pComp, pRhiDevice );
            ImGui::EndGroup();
            InspectorPropertyUndo::trackLastItem( pTypeInfo->getDisplayName() );
            pInspector->collectDrawnProperties( listDrawnName );
        }

        _pEditTargetComponent = pComp;
        drawTypeProperties( pComp, pTypeInfo, "Properties", listDrawnName );
        ImGui::SeparatorText( "Methods" );
        drawTypeMethods( pComp, pTypeInfo );
        drawTypeEvents( pComp, pTypeInfo );
        _pEditTargetComponent = nullptr;

        for ( IInspectorComponent* pInspector : listInspector )
        {
            pInspector->drawFooter( pComp, pRhiDevice );
        }
    }

    void InspectorPanel::drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName )
    {
        if ( pInstance == nullptr || pTypeInfo == nullptr )
            return;

        // 무엇을 어떤 순서로 그릴지는 `InspectorPropertyLayout` 이 정한다 — 상속분까지, 카테고리는 기반부터 처음 나온 순서.
        const EditorListFilter         filter{ _propertyFilter.c_str() };
        vector<InspectorPropertyGroup> listGroup;
        InspectorPropertyLayout::collectPropertyGroups( *pTypeInfo, listDrawnName, filter, listGroup );

        // 검색어가 아무 프로퍼티도 맞히지 못하면 그렇다고 알려 준다.
        if ( listGroup.empty() )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            return;
        }

        if ( pSectionTitle != nullptr )
            ImGui::SeparatorText( pSectionTitle );
        for ( const InspectorPropertyGroup& group : listGroup )
        {
            const string& category = group._category;
            const auto&   props    = group._listProperty;
            if ( ImGui::CollapsingHeader( category.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) == false )
                continue;

            if ( ImGui::BeginTable( category.c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg ) )
            {
                ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 150.0f * EditorThemeUtil::getDpiScale() );
                ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );

                for ( const PropertyInfo* prop : props )
                {
                    // `EditCondition` 이 거짓이면 숨기거나(EditConditionHides) 막는다. 판정은 ImGui 를 모르는 `PropertyEditCondition` 이 한다.
                    const PropertyEditState editState = PropertyEditCondition::getEditState( *pTypeInfo, *prop, pInstance );
                    if ( editState == PropertyEditState::Hidden )
                        continue;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted( InspectorPropertyLayout::getPropertyLabel( *prop ) );
                    EditorWidgets::drawTooltip( prop->_metadata._tooltip.c_str() );

                    if ( ImGui::BeginPopupContextItem( "PropCtx" ) )
                    {
                        if ( prop->_metadata._defaultValue.empty() == false )
                        {
                            fixed_string<constant::kMaxBuffer128> resetLabel;
                            formatstring( resetLabel.data(), resetLabel.capacity(), "Reset to Default (%#)", prop->_metadata._defaultValue.c_str() );
                            if ( ImGui::MenuItem( resetLabel.c_str() ) )
                            {
                                // 그 프로퍼티 하나만 쓰고(비트필드는 그 비트만) 알린 뒤 되돌리기에 남긴다. 주의: `{"이름":기본값}` 을 JSON 으로
                                // 읽히면 읽기가 빠진 프로퍼티마다 기본값을 채우므로 **기본값이 있는 다른 프로퍼티까지** 되돌린다.
                                const PropertyInfo& resetProp = *prop;
                                GameObject*         pOwnerObj = _pEditTargetComponent != nullptr ? _pEditTargetComponent->getOwner() : _pEditTargetObject;
                                auto                reset     = [this, &resetProp, pInstance]()
                                {
                                    if ( SerializerUtil::applyPropertyText( resetProp, pInstance, resetProp._metadata._defaultValue, SerializeContext::getDefault() ) )
                                        notifyPropertyEdited( resetProp );
                                    else
                                        SW_LOG_WARNING( "Reset to Default could not apply '%#' to %#", resetProp._metadata._defaultValue.c_str(), resetProp._name.c_str() );
                                };
                                if ( pOwnerObj != nullptr )
                                    InspectorPanelInternal::applyObjectEdit( pOwnerObj, "Reset to Default", reset );
                                else
                                    reset();
                            }
                        }
                        if ( ImGui::MenuItem( "Copy Property Name" ) )
                            ImGui::SetClipboardText( prop->_name.c_str() );
                        ImGui::EndPopup();
                    }

                    if ( prop->_metadata._bTransient == SW_TRUE )
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled( "(T)" );
                        EditorWidgets::drawTooltip( "Transient property: not saved to disk" );
                    }

                    ImGui::TableNextColumn();
                    ImGui::PushID( prop->_name.c_str() );
                    ImGui::SetNextItemWidth( -FLT_MIN );
                    ImGui::BeginDisabled( editState == PropertyEditState::Disabled );
                    drawPropertyWidget( pInstance, *prop );
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }

    void InspectorPanel::drawPropertyWidget( void* pInstance, const PropertyInfo& prop )
    {
        // ImGui 는 이번 프레임에 활성 아이템이 편집됐는지를 컨텍스트에 전역으로 기록한다. 위젯을
        // 그리기 전후로 그 플래그의 **변화**를 보면 "그 사이에 그린 이 프로퍼티가 편집됐다" 를 정확히
        // 가려낼 수 있다. 위젯이 아이템 하나든 여럿이든, 값이 float 이든 string 이든 상관없다.
        // 위젯마다 판정을 심으면 새 위젯을 추가할 때 빠뜨리는데, 여기 한 곳이면 빠뜨릴 수 없다.
        ImGuiContext& g             = *ImGui::GetCurrentContext();
        const bool    bEditedBefore = g.ActiveIdHasBeenEditedThisFrame;

        ++_propertyDrawDepth;
        drawPropertyWidgetBody( pInstance, prop );
        --_propertyDrawDepth;

        // 컨테이너·중첩 구조체는 이 함수가 재귀한다. 통지는 가장 바깥에서 한 번만 한다.
        if ( _propertyDrawDepth == 0 && bEditedBefore == false && g.ActiveIdHasBeenEditedThisFrame )
            notifyPropertyEdited( prop );
    }

    void InspectorPanel::notifyPropertyEdited( const PropertyInfo& prop )
    {
        if ( _pEditTargetComponent != nullptr )
            _pEditTargetComponent->onPropertyChanged( prop._name );
        else if ( _pEditTargetObject != nullptr )
            _pEditTargetObject->onPropertyChanged( prop._name );

        // 고친 값을 검증해 맵 검사 결과를 바꾼다(로그는 남기지 않는다 — 끄는 동안 프레임마다 온다).
        const GameObject* pOwner = ( _pEditTargetComponent != nullptr ) ? _pEditTargetComponent->getOwner() : _pEditTargetObject;
        if ( pOwner != nullptr )
            (void)ObjectValidation::reportGameObject( *pOwner, false );
    }

    void InspectorPanel::drawPropertyWidgetBody( void* pInstance, const PropertyInfo& prop )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        IInspectorProperty* pProperty = pContext->getInspectorPropertyManager().find( prop._typeName.c_str() );
        if ( pProperty != nullptr )
        {
            if ( pProperty->draw( pInstance, prop ) )
                return;
        }

        const bool bReadOnly = prop._metadata._bReadOnly != SW_FALSE;

        auto* pRegistry = editor::getService<TypeRegistry>();
        if ( pRegistry == nullptr )
            return;

        const EnumInfo* pEnumInfo = pRegistry->findEnum( prop._typeName );
        if ( pEnumInfo != nullptr )
        {
            drawEnumProperty( pInstance, prop, *pEnumInfo, bReadOnly );
            return;
        }

        if ( prop._bIsContainer && prop._containerWrapper != nullptr )
        {
            drawContainerProperty( pInstance, prop, bReadOnly );
            return;
        }

        const TypeInfo* pFieldType = pRegistry->findType( prop._typeName );
        if ( pFieldType != nullptr || prop._typeName.isPredefinedType( PredefinedNameType::NameType_string ) )
        {
            drawStructOrStringProperty( pInstance, prop, pFieldType );
            return;
        }

        ImGui::TextDisabled( "No inspector for %s", prop._typeName.c_str() );
    }

    void InspectorPanel::drawEnumProperty( void* pInstance, const PropertyInfo& prop, const EnumInfo& enumInfo, bool bReadOnly )
    {
        const utf8* pLabel    = "##value";
        auto*       pRegistry = editor::getService<TypeRegistry>();
        if ( pRegistry == nullptr )
            return;

        // **enum 의 실제 크기로** 읽고 쓴다(`EnumInfo::_size` · 부호). 주의: 늘 int32 로 읽고 쓰면 — 엔진의 enum 은 대부분 uint8 ·
        // uint16 이라 — 하나를 고를 때마다 뒤의 필드를 덮고(`CameraComponent::_role` 을 고르면 바로 뒤의 `_bOrthographic` 이 꺼진다),
        // 읽을 때는 이웃 바이트가 섞여 멀쩡한 값이 "<Unknown>" 으로 뜬다.
        void* pEnumMemory = prop.getValuePtr<void>( pInstance );
        if ( pEnumMemory == nullptr )
            return;
        const int64 currentValue = enumInfo.readValueFromMemory( pEnumMemory );
        int64       editedValue  = currentValue;
        const auto  commitEdit   = [&enumInfo, pEnumMemory, currentValue, &editedValue, &prop]()
        {
            if ( editedValue != currentValue )
                enumInfo.writeValueToMemory( pEnumMemory, editedValue );
            InspectorPropertyUndo::trackPod( pEnumMemory, enumInfo._size, prop );
        };

        if ( enumInfo._bIsBitFlag )
        {
            string previewStr;
            for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
            {
                if ( val != 0 && ( currentValue & val ) == val )
                {
                    if ( previewStr.empty() == false )
                        previewStr += " | ";
                    previewStr += nameHashed.c_str();
                }
            }
            if ( previewStr.empty() )
                previewStr = ( currentValue == 0 ) ? "None" : "<Unknown>";

            if ( bReadOnly )
            {
                ImGui::TextDisabled( "%s", pLabel );
                ImGui::SameLine();
                ImGui::TextUnformatted( previewStr.c_str() );
                return;
            }

            if ( ImGui::BeginCombo( pLabel, previewStr.c_str() ) )
            {
                if ( ImGui::SmallButton( "Select All" ) )
                {
                    for ( const auto& [val, _] : enumInfo._mapValueToName )
                    {
                        editedValue |= val;
                    }
                }
                ImGui::SameLine();
                if ( ImGui::SmallButton( "Clear All" ) )
                    editedValue = 0;
                ImGui::Separator();

                for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
                {
                    if ( val == 0 )
                        continue;
                    bool bChecked = ( ( editedValue & val ) == val );
                    if ( ImGui::Checkbox( nameHashed.c_str(), &bChecked ) )
                    {
                        if ( bChecked )
                            editedValue |= val;
                        else
                            editedValue &= ~val;
                    }
                }
                ImGui::EndCombo();
            }
            commitEdit();
            return;
        }

        const utf8* pName = pRegistry->enumToString( prop._typeName, currentValue );
        if ( bReadOnly )
        {
            ImGui::TextDisabled( "%s", pLabel );
            ImGui::SameLine();
            ImGui::TextUnformatted( pName != nullptr ? pName : "<Unknown>" );
            return;
        }

        const utf8* pPreview = ( pName != nullptr ) ? pName : "<Unknown>";

        if ( ImGui::BeginCombo( pLabel, pPreview ) )
        {
            for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
            {
                const bool bSelected = ( val == currentValue );
                if ( ImGui::Selectable( nameHashed.c_str(), bSelected ) )
                    editedValue = val;
                if ( bSelected )
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        commitEdit();
        return;
    }

    void InspectorPanel::drawContainerProperty( void* pInstance, const PropertyInfo& prop, bool bReadOnly )
    {
        const utf8* pLabel = "##value";

        // 이 함수의 **모든 분기는 무언가를 그리고 끝난다.** 아무것도 그리지 않고 돌아가면 인스펙터에는 **빈 칸 하나**만 남아,
        // 값이 비었는지, 그리지 못하는 것인지, 버그인지 화면만 보고는 구분할 수 없다(모르는 타입도 "No inspector for ..." 라고
        // 알린다). 맵 프로퍼티도 실제로 있다(`GameSettings::_mapCustomProperty` 등).
        void* pContainer = prop.getRawPtr( pInstance );
        if ( pContainer == nullptr )
        {
            ImGui::TextDisabled( "컨테이너를 읽을 수 없습니다 (%s)", prop._typeName.c_str() );
            return;
        }

        ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
        if ( pSeq == nullptr )
        {
            IMapContainerWrapper* pMap = prop._containerWrapper->asMap();
            if ( pMap == nullptr )
            {
                ImGui::TextDisabled( "No inspector for %s", prop._typeName.c_str() );
                return;
            }

            drawMapContainer( pContainer, prop, *pMap, bReadOnly );
            return;
        }
        // 원소가 곧 정렬 · 해시 키인 컨테이너(`set`)는 제자리에서 고칠 수 없다 — 지우고 다시 넣는다.
        if ( pSeq->allowsInPlaceElementWrite() == false )
        {
            drawKeyedSequenceContainer( pContainer, prop, *pSeq, bReadOnly );
            return;
        }

        const size_t                          count = pSeq->getSize( pContainer );
        fixed_string<constant::kMaxBuffer128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%#] (%# elements)", prop._elementTypeName.c_str(), count );

        const bool bElementEditable = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );

        if ( ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) )
        {
            // 고정 배열은 칸 수가 정해져 있다 — 더하기 · 비우기는 아무 일도 하지 않으므로 그리지 않는다.
            if ( bReadOnly == false && pSeq->isFixedSize() == false )
            {
                if ( ImGui::SmallButton( "+ Add" ) )
                {
                    InspectorPanelInternal::applyContainerEdit( getEditOwner(), "Add Element", [pSeq, pContainer]()
                    { pSeq->addElementDefault( pContainer ); } );
                    notifyPropertyEdited( prop );
                }
                ImGui::SameLine();
                if ( ImGui::SmallButton( "Clear" ) )
                {
                    InspectorPanelInternal::applyContainerEdit( getEditOwner(), "Clear Elements", [pSeq, pContainer]()
                    { pSeq->clear( pContainer ); } );
                    notifyPropertyEdited( prop );
                }
                ImGui::Separator();
            }

            // 원소 위젯도 같은 규칙을 받아야 한다 — `bReadOnly` 가 위의 버튼만 가리면
            // **`ReadOnly` 컨테이너의 원소가 그대로 편집된다.**
            ImGui::BeginDisabled( bElementEditable == false );

            const size_t newCount = pSeq->getSize( pContainer );
            for ( size_t elemIndex = 0; elemIndex < newCount; ++elemIndex )
            {
                void* pElem = pSeq->getElement( pContainer, elemIndex );
                if ( pElem == nullptr )
                    continue;

                ImGui::PushID( static_cast<int32>( elemIndex ) );
                ImGui::AlignTextToFramePadding();
                ImGui::Text( "[%zu]", elemIndex );
                ImGui::SameLine();

                PropertyInfo elemProp{};
                elemProp._typeName = prop._elementTypeName;
                elemProp._name     = prop._name;
                elemProp._metadata = prop._metadata;

                ImGui::SetNextItemWidth( -FLT_MIN );
                drawPropertyWidget( pElem, elemProp );
                ImGui::PopID();
            }

            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }

    void InspectorPanel::drawMapContainer( void* pContainer, const PropertyInfo& prop, IMapContainerWrapper& mapWrapper, bool bReadOnly )
    {
        const SerializeContext&               ctx = SerializeContext::getDefault();
        fixed_string<constant::kMaxBuffer128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%# -> %#] (%# entries)", prop._keyTypeName.c_str(), prop._elementTypeName.c_str(),
                      mapWrapper.getSize( pContainer ) );
        if ( ImGui::TreeNodeEx( "##value", ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) == false )
            return;

        const PropertyInfo keyProp   = InspectorPanelInternal::makeValueProperty( prop._keyTypeName );
        PropertyInfo       valueProp = InspectorPanelInternal::makeValueProperty( prop._elementTypeName );
        valueProp._name              = prop._name;
        valueProp._metadata          = prop._metadata;
        const bool bElementEditable  = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );

        // 값은 맵 노드 안에서 제자리로 고친다(되돌리기는 위젯 하나 단위 — `drawPropertyWidget`). 지우기는 순회가 끝난 뒤에 한다.
        size_t eraseOrdinal = static_cast<size_t>( -1 );
        size_t ordinal      = 0;
        mapWrapper.forEachMutable( pContainer, SW_DELEGATE_LAMBDA( MapForEachMutableDelegate, [&]( const void* pKey, void* pValue )
        {
            ImGui::PushID( static_cast<int32>( ordinal ) );
            if ( bReadOnly == false && ImGui::SmallButton( "x" ) )
                eraseOrdinal = ordinal;
            if ( bReadOnly == false )
                ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( SerializerUtil::formatPropertyText( keyProp, pKey, ctx ).c_str() );
            ImGui::SameLine();
            ImGui::BeginDisabled( bElementEditable == false );
            ImGui::SetNextItemWidth( -FLT_MIN );
            drawPropertyWidget( pValue, valueProp );
            ImGui::EndDisabled();
            ImGui::PopID();
            ++ordinal;
        } ) );

        if ( eraseOrdinal != static_cast<size_t>( -1 ) )
        {
            InspectorPanelInternal::applyContainerEdit( getEditOwner(), "Remove Map Entry", [&mapWrapper, pContainer, eraseOrdinal]()
            { (void)mapWrapper.eraseAt( pContainer, eraseOrdinal ); } );
            notifyPropertyEdited( prop );
        }

        string keyText;
        if ( bReadOnly == false && drawContainerAddRow( "new key (Enter)", keyText ) )
        {
            // 키를 키 타입으로 읽어 본다. 같은 키(글이 같다)가 이미 있으면 값을 덮어쓰지 않는다.
            void* pKey   = Memory::allocateAligned( mapWrapper.getKeySize(), alignof( std::max_align_t ) );
            void* pValue = Memory::allocateAligned( mapWrapper.getValueSize(), alignof( std::max_align_t ) );
            mapWrapper.defaultConstructKey( pKey );
            mapWrapper.defaultConstructValue( pValue );
            if ( SerializerUtil::applyPropertyText( keyProp, pKey, keyText, ctx ) )
            {
                const string stagedText = SerializerUtil::formatPropertyText( keyProp, pKey, ctx );
                bool         bExists    = false;
                mapWrapper.forEach( pContainer, SW_DELEGATE_LAMBDA( MapForEachDelegate, [&]( const void* pExistingKey, const void* )
                {
                    if ( SerializerUtil::formatPropertyText( keyProp, pExistingKey, ctx ) == stagedText )
                        bExists = true;
                } ) );
                if ( bExists == false )
                {
                    InspectorPanelInternal::applyContainerEdit( getEditOwner(), "Add Map Entry", [&mapWrapper, pContainer, pKey, pValue]()
                    { mapWrapper.insertKeyValue( pContainer, pKey, pValue ); } );
                    notifyPropertyEdited( prop );
                }
            }
            mapWrapper.destroyKey( pKey );
            mapWrapper.destroyValue( pValue );
            Memory::freeAligned( pKey );
            Memory::freeAligned( pValue );
        }
        ImGui::TreePop();
    }

    void InspectorPanel::drawKeyedSequenceContainer( void* pContainer, const PropertyInfo& prop, ISequenceContainerWrapper& sequence, bool bReadOnly )
    {
        const SerializeContext&               ctx   = SerializeContext::getDefault();
        const size_t                          count = sequence.getSize( pContainer );
        fixed_string<constant::kMaxBuffer128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%#] (%# elements)", prop._elementTypeName.c_str(), count );
        if ( ImGui::TreeNodeEx( "##value", ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) == false )
            return;

        const PropertyInfo elementProp      = InspectorPanelInternal::makeValueProperty( prop._elementTypeName );
        const bool         bElementEditable = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );
        size_t             eraseIndex       = static_cast<size_t>( -1 );
        size_t             replaceIndex     = static_cast<size_t>( -1 );
        string             replaceText;
        for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
        {
            ImGui::PushID( static_cast<int32>( elemIndex ) );
            if ( bReadOnly == false && ImGui::SmallButton( "x" ) )
                eraseIndex = elemIndex;
            if ( bReadOnly == false )
                ImGui::SameLine();
            // 원소는 글 칸이다. Enter 로 낸 글을 원소 타입으로 읽어 지우고 다시 넣는다 — 원소가 곧 정렬 키라 제자리 쓰기는 트리를 망친다.
            fixed_string<constant::kMaxBuffer256> elementText{ SerializerUtil::formatPropertyText( elementProp, sequence.getElementConst( pContainer, elemIndex ), ctx ).c_str() };
            const ImGuiInputTextFlags             flags = bElementEditable ? ImGuiInputTextFlags_EnterReturnsTrue : ImGuiInputTextFlags_ReadOnly;
            ImGui::SetNextItemWidth( -FLT_MIN );
            if ( ImGui::InputText( "##element", elementText.data(), elementText.capacity(), flags ) && bElementEditable )
            {
                replaceIndex = elemIndex;
                replaceText  = elementText.c_str();
            }
            ImGui::PopID();
        }

        if ( replaceIndex != static_cast<size_t>( -1 ) )
        {
            InspectorPanelInternal::applyContainerEdit( getEditOwner(), "Edit Set Element", [&]()
            {
                (void)sequence.replaceElement( pContainer, replaceIndex, SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
                { return SerializerUtil::applyPropertyText( elementProp, pElement, replaceText, ctx ); } ) );
            } );
            notifyPropertyEdited( prop );
        }
        else if ( eraseIndex != static_cast<size_t>( -1 ) )
        {
            InspectorPanelInternal::applyContainerEdit( getEditOwner(), "Remove Set Element", [&sequence, pContainer, eraseIndex]()
            { (void)sequence.eraseAt( pContainer, eraseIndex ); } );
            notifyPropertyEdited( prop );
        }

        string addText;
        if ( bReadOnly == false && drawContainerAddRow( "new element (Enter)", addText ) )
        {
            InspectorPanelInternal::applyContainerEdit( getEditOwner(), "Add Set Element", [&]()
            {
                (void)sequence.appendElement( pContainer, sequence.getSize( pContainer ), SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
                { return SerializerUtil::applyPropertyText( elementProp, pElement, addText, ctx ); } ) );
            } );
            notifyPropertyEdited( prop );
        }
        ImGui::TreePop();
    }

    bool InspectorPanel::drawContainerAddRow( const utf8* pHint, string& outText )
    {
        const ImGuiID                          rowId = ImGui::GetID( "##add" );
        fixed_string<constant::kMaxBuffer256>& text  = _mapContainerAddText[static_cast<uint32>( rowId )];
        ImGui::SetNextItemWidth( -FLT_MIN );
        const bool bEntered = ImGui::InputTextWithHint( "##add", pHint, text.data(), text.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        if ( bEntered == false || text.empty() )
            return false;
        outText = text.c_str();
        text.clear();
        return true;
    }

    GameObject* InspectorPanel::getEditOwner() const
    {
        if ( _pEditTargetComponent != nullptr )
            return _pEditTargetComponent->getOwner();
        return _pEditTargetObject;
    }

    void InspectorPanel::drawStructOrStringProperty( void* pInstance, const PropertyInfo& prop, const TypeInfo* pFieldType )
    {
        const utf8* pLabel = "##value";

        void* pNestedPtr = prop.getRawPtr( pInstance );
        if ( pNestedPtr == nullptr )
            return;

        ImGui::PushStyleColor( ImGuiCol_Header, ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f } );
        bool bNodeOpen = ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "[%s]", prop._typeName.c_str() );
        ImGui::PopStyleColor();

        if ( bNodeOpen )
        {
            pFieldType->forEachProperty( [&]( const PropertyInfo& nestedProp )
            {
                const PropertyEditState editState = PropertyEditCondition::getEditState( *pFieldType, nestedProp, pNestedPtr );
                if ( editState == PropertyEditState::Hidden )
                    return;
                ImGui::PushID( nestedProp._name.c_str() );
                ImGui::AlignTextToFramePadding();
                ImGui::BulletText( "%s", InspectorPropertyLayout::getPropertyLabel( nestedProp ) );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( -FLT_MIN );
                ImGui::BeginDisabled( editState == PropertyEditState::Disabled );
                drawPropertyWidget( pNestedPtr, nestedProp );
                ImGui::EndDisabled();
                ImGui::PopID();
            }, true ); // 구조체의 기반 필드도 그린다(컴포넌트와 같은 규칙)
            ImGui::TreePop();
        }
        return;
    }

    void InspectorPanel::drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo )
    {
        if ( pInstance == nullptr || pTypeInfo == nullptr || pTypeInfo->_listMethod.empty() )
        {
            EditorWidgets::drawEmptyHint( "No FUNCTION() methods." );
            return;
        }

        if ( _lastInvokeResult.empty() == false )
            ImGui::TextDisabled( "Last result: %s", _lastInvokeResult.c_str() );

        for ( const FunctionInfo& method : pTypeInfo->_listMethod )
        {
            // 생성자 호출기는 초기화되지 않은 저장 공간(placement new)을 기대한다. 살아 있는 인스펙터 인스턴스에 부르면 안전하지 않다.
            if ( method._metadata._bConstructor != SW_FALSE )
                continue;

            const utf8* pLabelName = method._name.c_str();
            if ( method._metadata._displayName.empty() == false )
                pLabelName = method._metadata._displayName.c_str();

            const uint32 paramCount = method.getParameterCount();

            if ( method._metadata._bCallInEditor != SW_FALSE && paramCount == 0 )
            {
                ImGui::PushID( method._hashName.c_str() );
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.18f, 0.42f, 0.65f, 1.0f } );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.25f, 0.52f, 0.78f, 1.0f } );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4{ 0.12f, 0.35f, 0.55f, 1.0f } );

                fixed_string<constant::kMaxBuffer128> buttonLabel;
                formatstring( buttonLabel.data(), buttonLabel.capacity(), "Run %#", pLabelName );
                if ( ImGui::Button( buttonLabel.c_str(), ImVec2{ -FLT_MIN, 0.0f } ) )
                {
                    invokeTypeMethod( pInstance, pTypeInfo, method, TaskArgs{} );
                }
                ImGui::PopStyleColor( 3 );
                EditorWidgets::drawTooltip( method._metadata._tooltip.c_str() );
                ImGui::PopID();
                continue;
            }

            ImGui::PushID( method._hashName.c_str() );
            ImGui::Text( "%s (%s)", pLabelName,
                         method._returnTypeName.empty() ? "?" : method._returnTypeName.c_str() );
            if ( method._metadata._bCallInEditor != SW_FALSE )
            {
                ImGui::SameLine();
                ImGui::TextColored( ImVec4{ 0.3f, 0.8f, 1.0f, 1.0f }, "[Editor]" );
            }
            EditorWidgets::drawTooltip( method._metadata._tooltip.c_str() );
            const uint64                    slotKey  = static_cast<uint64>( pTypeInfo->_fullyQualifiedName.getHash() ) * 31u + static_cast<uint64>( method._hashName.getHash() );
            vector<InspectorMethodArgSlot>& listSlot = _mapMethodArgSlot[slotKey];
            const bool                      bArgsOk  = InspectorBuiltinValueUtil::prepareMethodArgs( method, listSlot );
            for ( uint32 paramIndex = 0; paramIndex < static_cast<uint32>( listSlot.size() ); ++paramIndex )
            {
                ImGui::PushID( static_cast<int32>( paramIndex ) );
                // 선언의 인자 이름이 있으면 그것, 없으면 순번이다.
                const FunctionParameterInfo&         parameter = method._listParameter[paramIndex];
                fixed_string<constant::kMaxBuffer64> label;
                if ( parameter._name.empty() )
                    formatstring( label.data(), label.capacity(), "arg%# (%#)", paramIndex, parameter._typeName.c_str() );
                else
                    formatstring( label.data(), label.capacity(), "%# (%#)", parameter._name.c_str(), parameter._typeName.c_str() );
                InspectorPropertyManager::drawMethodArg( label.c_str(), listSlot[paramIndex] );
                ImGui::PopID();
            }

            if ( paramCount > InspectorBuiltinValueUtil::kMaxMethodArgCount )
                ImGui::TextDisabled( "Too many arguments (max %u in UI).", InspectorBuiltinValueUtil::kMaxMethodArgCount );

            if ( bArgsOk == false )
            {
                ImGui::BeginDisabled();
                ImGui::Button( "Invoke" );
                ImGui::EndDisabled();
                ImGui::TextDisabled( "Unsupported FUNCTION args - invoke skipped." );
            }
            else if ( ImGui::Button( "Invoke" ) )
            {
                invokeTypeMethod( pInstance, pTypeInfo, method, InspectorBuiltinValueUtil::makeMethodArgs( listSlot ) );
            }

            ImGui::PopID();
        }
    }

    void InspectorPanel::drawTypeEvents( void* pInstance, const TypeInfo* pTypeInfo )
    {
        bool bHasEvent = false;
        pTypeInfo->forEachEventWithBase( [&bHasEvent]( const EventInfo& )
        { bHasEvent = true; } );
        if ( bHasEvent == false )
            return;

        ImGui::SeparatorText( "Events" );
        pTypeInfo->forEachEventWithBase( [pInstance]( const EventInfo& event )
        {
            ImGui::PushID( event._name.c_str() );
            const string signature = InspectorPropertyLayout::formatParameterList( event._listParameter );
            ImGui::BulletText( "%s(%s)", event._name.c_str(), signature.c_str() );
            EditorWidgets::drawTooltip( event._metadata._tooltip.c_str() );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", ReflectionInvoke::isEventBound( event, pInstance ) ? "(bound)" : "(unbound)" );
            if ( event.getParameterCount() == 0 )
            {
                ImGui::SameLine();
                if ( ImGui::SmallButton( "Broadcast" ) )
                    (void)ReflectionInvoke::broadcastEvent( event, pInstance, {} );
            }
            ImGui::PopID();
        } );
    }

    void InspectorPanel::invokeTypeMethod( void* pInstance, const TypeInfo* pTypeInfo, const FunctionInfo& method,
                                           const TaskArgs& args )
    {
        // `getService<T>()` 는 nullptr 을 반환할 수 있다. 역참조하기 전에 확인한다.
        TypeRegistry* pTypeRegistry = editor::getService<TypeRegistry>();
        if ( pTypeRegistry == nullptr )
            return;

        const TaskValue result = pTypeRegistry->invokeMethod( pInstance, pTypeInfo->_fullyQualifiedName, method._hashName, args );
        InspectorBuiltinValueUtil::formatMethodResult( result, method._returnTypeName, _lastInvokeResult.data(),
                                                       _lastInvokeResult.capacity() );
    }
} // namespace sw::editor
