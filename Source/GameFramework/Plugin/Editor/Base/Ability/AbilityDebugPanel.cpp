#include "pch.h"

#include "GameFramework/Plugin/Editor/Base/Ability/AbilityDebugPanel.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"

#include <imgui.h>

namespace sw::editor
{
    AbilitySystemComponent* AbilityDebugPanel::findTarget()
    {
        // 선택한 오브젝트에 컴포넌트가 있으면 그것을 본다 — 도는 월드(메인 씬)를 디버그하는 창이라 메인 씬의 선택이다
        vector<GameObject*> listSelected;
        editor::getMainSceneSelection().getSelectedObjects( listSelected );
        for ( GameObject* pObject : listSelected )
        {
            AbilitySystemComponent* pAbilitySystem = pObject != nullptr ? pObject->getComponent<AbilitySystemComponent>() : nullptr;
            if ( pAbilitySystem != nullptr )
            {
                _pickedHandle = pObject->getHandle();
                return pAbilitySystem;
            }
        }
        // 없으면 목록에서 고른 것을 이어서 본다
        GameObjectManager* pObjects = getActiveObjectManager();
        if ( pObjects == nullptr || _pickedHandle.isValid() == false )
            return nullptr;
        GameObject* pOwner = pObjects->resolveGameObject( _pickedHandle );
        return pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
    }

    void AbilityDebugPanel::drawTargetPicker( const AbilitySystemComponent* pCurrent )
    {
        GameObjectManager* pObjects = getActiveObjectManager();
        if ( pObjects == nullptr )
            return;

        const GameObject* pCurrentOwner = pCurrent != nullptr ? pCurrent->getOwner() : nullptr;
        const utf8*       pPreview      = pCurrentOwner != nullptr ? pCurrentOwner->getName().c_str() : SW_LOCKEY( "EdAbilityDebug", "PickTarget" );
        if ( ImGui::BeginCombo( "##AbilityTarget", pPreview ) == false )
            return;

        // 펼칠 때만 씬을 훑는다
        _listCandidate.clear();
        pObjects->forEachGameObject( [this]( GameObject* pObject )
        {
            if ( pObject != nullptr && pObject->getComponent<AbilitySystemComponent>() != nullptr )
                _listCandidate.push_back( pObject->getHandle() );
        } );
        if ( _listCandidate.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdAbilityDebug", "NoComponentsInScene" ) );
        for ( const GameObjectHandle& handle : _listCandidate )
        {
            GameObject* pObject = pObjects->resolveGameObject( handle );
            if ( pObject == nullptr )
                continue;
            ImGui::PushID( static_cast<int32>( pObject->getObjectID() ) );
            if ( ImGui::Selectable( pObject->getName().c_str(), pObject == pCurrentOwner ) )
                _pickedHandle = handle;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    void AbilityDebugPanel::drawContent()
    {
        AbilitySystemComponent* pTarget = findTarget();
        drawTargetPicker( pTarget );

        if ( EditorPlaySession::isStopped() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdAbilityDebug", "HintPlayOnly" ) );
            return;
        }
        if ( pTarget == nullptr )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdAbilityDebug", "HintNoTarget" ) );
            return;
        }

        AbilityDebugModel::collect( *pTarget, _snapshot );
        ImGui::TextDisabled( "t = %.2f", static_cast<float64>( _snapshot._time ) );
        drawSnapshot();
    }

    void AbilityDebugPanel::drawSnapshot()
    {
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionAttributes" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "##Attributes", 3, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnName" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnBase" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnCurrent" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugAttributeRow& row : _snapshot._listAttribute )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._name.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.2f", static_cast<float64>( row._baseValue ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.2f", static_cast<float64>( row._currentValue ) );
                }
                ImGui::EndTable();
            }
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionEffects" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "##Effects", 3, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnName" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnRemaining" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnStacks" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugEffectRow& row : _snapshot._listEffect )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._name.c_str() );
                    ImGui::TableNextColumn();
                    if ( row._bInfinite )
                        ImGui::TextUnformatted( SW_LOCKEY( "EdAbilityDebug", "Infinite" ) );
                    else
                        ImGui::Text( "%.2f", static_cast<float64>( row._remainingTime ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%d", row._stackCount );
                }
                ImGui::EndTable();
            }
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionTags" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "##Tags", 2, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnTag" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnCount" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugTagRow& row : _snapshot._listTag )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._tag.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%d", row._count );
                }
                ImGui::EndTable();
            }
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdAbilityDebug", "SectionAbilities" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "##Abilities", 3, kTableFlags ) )
            {
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnName" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnActive" ) );
                ImGui::TableSetupColumn( SW_LOCKEY( "EdAbilityDebug", "ColumnCooldown" ) );
                ImGui::TableHeadersRow();
                for ( const AbilityDebugAbilityRow& row : _snapshot._listAbility )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._name.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._bActive ? "*" : "" );
                    ImGui::TableNextColumn();
                    if ( row._cooldownRemaining > 0.0f )
                        ImGui::Text( "%.2f", static_cast<float64>( row._cooldownRemaining ) );
                    else
                        ImGui::TextDisabled( "-" );
                }
                ImGui::EndTable();
            }
        }
    }

    SW_EDITOR_PANEL( AbilityDebugPanel, AbilityDebugPanel::kPanelID, EditorPanelCategory::Custom, 9310 );
} // namespace sw::editor
