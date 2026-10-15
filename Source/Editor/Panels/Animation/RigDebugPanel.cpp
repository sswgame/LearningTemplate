#include "pch.h"

#include "Editor/Panels/Animation/RigDebugPanel.h"

#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( RigDebugPanel, RigDebugPanel::kPanelID, EditorPanelCategory::Tool, 1660 );

    namespace
    {
        struct RigDebugPanelInternal
        {
            static constexpr float4  kMarkerColor{ 1.0f, 0.45f, 0.2f, 1.0f };
            static constexpr float32 kMarkerRadius = 0.03f;
            /** @brief 디버그 드로우 카테고리입니다(`debugdraw.category RigDebug off` 로 끈다). */
            static constexpr const utf8* kDrawCategory = "RigDebug";

            /** @brief 오브젝트 id 의 리그 컴포넌트입니다(없으면 nullptr). */
            static PoseModifierComponent* findModifier( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = ( pManager != nullptr && objectID != 0 ) ? pManager->findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<PoseModifierComponent>() : nullptr;
            }

            /** @brief 오브젝트 id 와 노드 번호의 노드입니다(되돌리기 때 다시 찾는다, 없으면 nullptr). */
            static RigNode* findNode( uint64 objectID, uint32 nodeIndex )
            {
                PoseModifierComponent* pModifier = findModifier( objectID );
                if ( pModifier == nullptr || nodeIndex >= pModifier->getRigInstance().getNodeCount() )
                    return nullptr;
                return &pModifier->getRigInstance().getNode( nodeIndex );
            }

            static void applyWeight( uint64 objectID, uint32 nodeIndex, float32 weight )
            {
                if ( RigNode* pNode = findNode( objectID, nodeIndex ) )
                    pNode->setWeight( weight );
            }

            static void applyEnabled( uint64 objectID, uint32 nodeIndex, bool bEnabled )
            {
                if ( RigNode* pNode = findNode( objectID, nodeIndex ) )
                    pNode->setEnabled( bEnabled );
            }

            static void recordWeight( uint64 objectID, uint32 nodeIndex, float32 before, float32 after )
            {
                EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [objectID, nodeIndex, before]()
                { applyWeight( objectID, nodeIndex, before ); } ),
                                         SW_DELEGATE_LAMBDA( Delegate<void()>, [objectID, nodeIndex, after]()
                { applyWeight( objectID, nodeIndex, after ); } ),
                                         "Edit Rig Node Weight" );
            }

            static void recordEnabled( uint64 objectID, uint32 nodeIndex, bool bAfter )
            {
                const bool bBefore = bAfter == false;
                EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [objectID, nodeIndex, bBefore]()
                { applyEnabled( objectID, nodeIndex, bBefore ); } ),
                                         SW_DELEGATE_LAMBDA( Delegate<void()>, [objectID, nodeIndex, bAfter]()
                { applyEnabled( objectID, nodeIndex, bAfter ); } ),
                                         "Toggle Rig Node" );
            }

            /** @brief 노드가 쓰는 본 이름을 "첫 본 (+n)" 꼴로 줄입니다. */
            static string describeBones( const Skeleton& skeleton, const vector<uint32>& listBone )
            {
                if ( listBone.empty() )
                    return string{ "-" };
                string text = listBone[0] < skeleton.getBoneCount() ? string{ skeleton.getBone( listBone[0] )._name.c_str() } : string{ "?" };
                if ( listBone.size() > 1 )
                    text += " (+" + to_string( listBone.size() - 1 ) + ")";
                return text;
            }

            /** @brief 고른 노드가 쓰는 본마다 구와 이름을 그립니다(이번 프레임만). */
            static void drawBoneMarkers( DebugDrawQueue& queue, const SkeletalMeshComponent& unit, const vector<uint32>& listBone )
            {
                const vector<float4x4>& listModel = unit.getModelSpaceTransforms();
                const Skeleton&         skeleton  = unit.getSkeleton();
                const float4x4          world     = unit.getWorldMatrix();
                const hashed_string     category( kDrawCategory );
                for ( const uint32 boneIndex : listBone )
                {
                    if ( boneIndex >= listModel.size() || boneIndex >= skeleton.getBoneCount() )
                        continue;
                    const float3 position = float3::transform( listModel[boneIndex].getTranslation(), world );
                    queue.drawSphere( position, kMarkerRadius, kMarkerColor, 0.0f, category );
                    queue.drawText( position, skeleton.getBone( boneIndex )._name.c_str(), kMarkerColor, 0.0f, category );
                }
            }
        };
    } // namespace

    RigDebugPanel::RigDebugPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _selectedObjectID{ 0 }
        , _selectedNode{ -1 }
        , _weightBeforeEdit{ 0.0f }
        , _bDrawMarkers{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void RigDebugPanel::drawContent()
    {
        EditorContext* pContext = EditorContext::get();
        const uint64   objectID = pContext != nullptr ? pContext->getSubsystem<EditorWorkspace>().getSelectedObjectID() : 0;
        if ( objectID != _selectedObjectID )
        {
            _selectedObjectID = objectID;
            _selectedNode     = -1;
        }
        PoseModifierComponent* pModifier = RigDebugPanelInternal::findModifier( objectID );
        if ( pModifier == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRigDebug", "SelectRig" ) );
            return;
        }
        RigInstance& instance = pModifier->getRigInstance();
        if ( instance.isInitialized() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRigDebug", "RigNotReady" ) );
            return;
        }

        bool bDrawMarkers = _bDrawMarkers == SW_TRUE;
        if ( ImGui::Checkbox( "Bone Markers", &bDrawMarkers ) )
            _bDrawMarkers = bDrawMarkers ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRigDebug", "BoneMarkersTip" ) );

        GameObject*                  pObject = pModifier->getOwner();
        const SkeletalMeshComponent* pUnit   = pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
        const uint32                 count   = instance.getNodeCount();
        vector<uint32>               listBone;
        vector<uint32>               listSelectedBone;
        const ImGuiTableFlags        flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##rig_nodes", 6, flags ) )
        {
            ImGui::TableSetupColumn( "Node" );
            ImGui::TableSetupColumn( "Type" );
            ImGui::TableSetupColumn( "Bones" );
            ImGui::TableSetupColumn( "Weight" );
            ImGui::TableSetupColumn( "Effective" );
            ImGui::TableSetupColumn( "On" );
            ImGui::TableHeadersRow();
            for ( uint32 nodeIndex = 0; nodeIndex < count; ++nodeIndex )
            {
                RigNode& node = instance.getNode( nodeIndex );
                listBone.clear();
                node.collectWrittenBones( listBone );
                ImGui::PushID( static_cast<int32>( nodeIndex ) );
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                const bool bSelected = _selectedNode == static_cast<int32>( nodeIndex );
                if ( ImGui::Selectable( node.getName().empty() ? "(unnamed)" : node.getName().c_str(), bSelected ) )
                    _selectedNode = bSelected ? -1 : static_cast<int32>( nodeIndex );
                if ( bSelected )
                    listSelectedBone = listBone;

                ImGui::TableNextColumn();
                ImGui::TextUnformatted( node.getTypeName() );

                ImGui::TableNextColumn();
                const string bones = pUnit != nullptr ? RigDebugPanelInternal::describeBones( pUnit->getSkeleton(), listBone ) : string{ "?" };
                ImGui::TextUnformatted( bones.c_str() );

                ImGui::TableNextColumn();
                float32 weight = node.getWeight();
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::SliderFloat( "##weight", &weight, 0.0f, 1.0f, "%.2f" ) )
                    node.setWeight( weight );
                if ( ImGui::IsItemActivated() )
                    _weightBeforeEdit = node.getWeight();
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    RigDebugPanelInternal::recordWeight( objectID, nodeIndex, _weightBeforeEdit, node.getWeight() );

                ImGui::TableNextColumn();
                // 커브 · 슬롯 · LOD 를 모두 곱한 이번 프레임 값 — 시퀀서 리그 슬롯 트랙이 움직이는 것이 여기 보인다.
                const float32 effective = instance.isNodeLODOff( nodeIndex ) ? 0.0f : instance.computeNodeWeight( nodeIndex );
                ImGui::Text( "%.2f", static_cast<float64>( effective ) );
                if ( node.getWeightSlot().empty() == false )
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled( "[%s]", node.getWeightSlot().c_str() );
                }

                ImGui::TableNextColumn();
                bool bEnabled = node.isEnabled();
                if ( ImGui::Checkbox( "##on", &bEnabled ) )
                {
                    node.setEnabled( bEnabled );
                    RigDebugPanelInternal::recordEnabled( objectID, nodeIndex, bEnabled );
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        DebugDrawQueue* pQueue              = findSubsystem<DebugDrawQueue>();
        const bool      bCanDrawBoneMarkers = _bDrawMarkers == SW_TRUE && pQueue != nullptr && pUnit != nullptr && listSelectedBone.empty() == false;
        if ( bCanDrawBoneMarkers )
            RigDebugPanelInternal::drawBoneMarkers( *pQueue, *pUnit, listSelectedBone );
    }
} // namespace sw::editor
