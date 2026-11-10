#include "pch.h"

#include "Editor/Panels/Animation/Model/RigDebugViewModel.h"

#include "Core/Delegate/Delegate.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/RigSaveParams.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/Visualizers/RigTargetGizmoVisualizer.h"

#include "Engine/Animation/Component/PoseModifierComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        struct RigDebugViewModelInternal
        {
            static constexpr float32 kMarkerRadius = 0.03f;
            /** @brief 디버그 드로우 카테고리입니다(`debugdraw.category RigDebug off` 로 끈다). */
            static constexpr const utf8* kDrawCategory = "RigDebug";

            /** @brief 고른 오브젝트 id 입니다(에디터 컨텍스트가 없으면 0). */
            static uint64 getSelectedObjectID()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? pContext->getSubsystem<EditorWorkspace>().getSelectedObjectID() : 0;
            }

            /** @brief 오브젝트 id 의 리그 컴포넌트입니다(없으면 nullptr). */
            static PoseModifierComponent* findModifier( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = ( pManager != nullptr && objectID != 0 ) ? pManager->findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<PoseModifierComponent>() : nullptr;
            }

            /** @brief 리그 컴포넌트와 같은 오브젝트의 스켈레탈 유닛입니다(없으면 nullptr). */
            static const SkeletalMeshComponent* findUnit( const PoseModifierComponent& modifier )
            {
                const GameObject* pObject = modifier.getOwner();
                return pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
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

            /** @brief 노드가 쓰는 본 이름을 "첫 본 (+n)" 꼴로 줄입니다. */
            static string describeBones( const Skeleton& skeleton, const vector<uint32>& listBone )
            {
                if ( listBone.empty() )
                    return string{ "-" };
                string text = listBone[0] < skeleton.getBoneCount() ? string{ skeleton.getBone( listBone[0] )._name.c_str() } : string{ "?" };
                if ( listBone.size() > 1 )
                {
                    StringBuilder<constant::buffer::kMax256> textPart;
                    textPart.appendFormat( " (+%#)", listBone.size() - 1 );
                    text += textPart.c_str();
                }
                return text;
            }

            /** @brief 고른 오브젝트의 묶인 리그 인스턴스입니다. 고른 것이 없거나 리그가 아직 묶이지 않았으면 nullptr 입니다(감시 값이 읽는다). */
            [[nodiscard]] static const RigInstance* findSelectedInstance()
            {
                const PoseModifierComponent* pModifier = findModifier( getSelectedObjectID() );
                return pModifier != nullptr && pModifier->getRigInstance().isInitialized() ? &pModifier->getRigInstance() : nullptr;
            }

            [[nodiscard]] static bool readNodeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RigInstance* pInstance = findSelectedInstance();
                if ( pInstance == nullptr )
                    return false;
                outValue = static_cast<float64>( pInstance->getNodeCount() );
                return true;
            }

            [[nodiscard]] static bool readEnabledNodeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RigInstance* pInstance = findSelectedInstance();
                if ( pInstance == nullptr )
                    return false;
                uint32 enabledCount{ 0 };
                for ( uint32 nodeIndex = 0; nodeIndex < pInstance->getNodeCount(); ++nodeIndex )
                {
                    if ( pInstance->getNode( nodeIndex ).isEnabled() )
                        ++enabledCount;
                }
                outValue = static_cast<float64>( enabledCount );
                return true;
            }

            [[nodiscard]] static bool readWeightSum( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RigInstance* pInstance = findSelectedInstance();
                if ( pInstance == nullptr )
                    return false;
                float64 weightSum{ 0.0 };
                for ( uint32 nodeIndex = 0; nodeIndex < pInstance->getNodeCount(); ++nodeIndex )
                {
                    weightSum += static_cast<float64>( pInstance->getNode( nodeIndex ).getWeight() );
                }
                outValue = weightSum;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorRigDebugNodeCount, "Editor.RigDebugNodeCount", "Rig nodes of the selected object's bound rig (fails while nothing with a bound rig is selected)",
                         &RigDebugViewModelInternal::readNodeCount );
    SW_AUTOMATION_WATCH( editorRigDebugEnabledNodeCount, "Editor.RigDebugEnabledNodeCount", "Enabled rig nodes of the selected object's bound rig",
                         &RigDebugViewModelInternal::readEnabledNodeCount );
    SW_AUTOMATION_WATCH( editorRigDebugWeightSum, "Editor.RigDebugWeightSum", "Sum of the node weights of the selected object's bound rig",
                         &RigDebugViewModelInternal::readWeightSum );

    RigDebugViewModel::RigDebugViewModel()
        : _status{}
        , _selectedObjectID{ 0 }
        , _selectedNode{ constant::invalid_index::kInt32 }
    {
    }

    void RigDebugViewModel::gather( RigDebugViewState& outState )
    {
        using Internal = RigDebugViewModelInternal;
        outState._listNode.clear();
        outState._listTarget.clear();
        outState._content = RigDebugContent::NoRig;

        const uint64   objectID   = Internal::getSelectedObjectID();
        RigTargetEdit& targetEdit = RigTargetEdit::get();
        if ( objectID != _selectedObjectID )
        {
            _selectedObjectID = objectID;
            _selectedNode     = constant::invalid_index::kInt32;
            targetEdit        = RigTargetEdit{};
        }
        outState._status       = _status;
        outState._targetStatus = targetEdit._status;
        outState._selectedNode = _selectedNode;

        const PoseModifierComponent* pModifier = Internal::findModifier( objectID );
        if ( pModifier == nullptr )
            return;
        const RigInstance& instance = pModifier->getRigInstance();
        if ( instance.isInitialized() == false )
        {
            outState._content = RigDebugContent::RigNotReady;
            return;
        }
        outState._content  = RigDebugContent::Rig;
        outState._bCanSave = pModifier->getRigPath().empty() ? SW_FALSE : SW_TRUE;

        const SkeletalMeshComponent* pUnit = Internal::findUnit( *pModifier );
        outState._listNode.resize( instance.getNodeCount() );
        for ( uint32 nodeIndex = 0; nodeIndex < instance.getNodeCount(); ++nodeIndex )
        {
            const RigNode&   node = instance.getNode( nodeIndex );
            RigDebugNodeRow& row  = outState._listNode[nodeIndex];
            row._listBone.clear();
            node.collectWrittenBones( row._listBone );
            row._name       = node.getName().empty() ? "(unnamed)" : node.getName().c_str();
            row._typeName   = node.getTypeName();
            row._bones      = pUnit != nullptr ? Internal::describeBones( pUnit->getSkeleton(), row._listBone ) : string{ "?" };
            row._weightSlot = node.getWeightSlot().empty() ? "" : node.getWeightSlot().c_str();
            row._weight     = node.getWeight();
            // 커브 · 슬롯 · LOD 를 모두 곱한 이번 프레임 값 — 시퀀서 리그 슬롯 트랙이 움직이는 것이 여기 보인다.
            row._effective = instance.computeNodeWeight( nodeIndex );
            row._bEnabled  = node.isEnabled() ? SW_TRUE : SW_FALSE;
        }

        // 대상 — 자기 유닛 대상(본 · 소켓)만 기즈모로 옮긴다. 바깥 대상(다른 유닛 · 오브젝트)은 부모 공간이 호스트 쪽에 있다.
        outState._listTarget.resize( instance.getTargetCount() );
        for ( uint32 targetIndex = 0; targetIndex < instance.getTargetCount(); ++targetIndex )
        {
            const RigTargetDef& def = instance.getTargetDef( targetIndex );
            optional<uint32>    bone;
            BoneTransform       combined{};
            const bool          bOwnUnit     = instance.tryGetOwnTargetBinding( targetIndex, bone, combined );
            const bool          bEditingThis = targetEdit._objectID == objectID && targetEdit._targetIndex == static_cast<int32>( targetIndex );
            RigDebugTargetRow&  row          = outState._listTarget[targetIndex];
            row._name                        = def._name.c_str();
            row._translation                 = bEditingThis && targetEdit._bPending == SW_TRUE ? targetEdit._pending._translation : def._offset._translation;
            row._bOwnUnit                    = bOwnUnit ? SW_TRUE : SW_FALSE;
            row._bGizmoOn                    = bEditingThis ? SW_TRUE : SW_FALSE;
        }
    }

    void RigDebugViewModel::toggleNodeSelection( uint32 nodeIndex )
    {
        _selectedNode = _selectedNode == static_cast<int32>( nodeIndex ) ? constant::invalid_index::kInt32 : static_cast<int32>( nodeIndex );
    }

    void RigDebugViewModel::setNodeWeight( uint32 nodeIndex, float32 weight )
    {
        RigDebugViewModelInternal::applyWeight( _selectedObjectID, nodeIndex, weight );
    }

    void RigDebugViewModel::commitNodeWeight( uint32 nodeIndex, float32 weightBefore )
    {
        using Internal       = RigDebugViewModelInternal;
        const RigNode* pNode = Internal::findNode( _selectedObjectID, nodeIndex );
        if ( pNode == nullptr )
            return;
        EditorTransaction::push( SW_UNDO_STEP( RigDebugViewModelInternal::applyWeight, _selectedObjectID, nodeIndex, weightBefore ),
                                 SW_UNDO_STEP( RigDebugViewModelInternal::applyWeight, _selectedObjectID, nodeIndex, pNode->getWeight() ), "Edit Rig Node Weight" );
    }

    void RigDebugViewModel::setNodeEnabled( uint32 nodeIndex, bool bEnabled )
    {
        using Internal = RigDebugViewModelInternal;
        if ( Internal::findNode( _selectedObjectID, nodeIndex ) == nullptr )
            return;
        Internal::applyEnabled( _selectedObjectID, nodeIndex, bEnabled );
        EditorTransaction::push( SW_UNDO_STEP( RigDebugViewModelInternal::applyEnabled, _selectedObjectID, nodeIndex, bEnabled == false ),
                                 SW_UNDO_STEP( RigDebugViewModelInternal::applyEnabled, _selectedObjectID, nodeIndex, bEnabled ), "Toggle Rig Node" );
    }

    void RigDebugViewModel::saveToRig()
    {
        const PoseModifierComponent* pModifier = RigDebugViewModelInternal::findModifier( _selectedObjectID );
        if ( pModifier == nullptr || pModifier->getRigPath().empty() )
            return;
        _status = RigSaveParams::writeToRigFile( *pModifier, RigSaveParams::makeFromNodes( pModifier->getRigInstance() ), nullptr );
    }

    void RigDebugViewModel::setTargetGizmo( uint32 targetIndex, bool bOn )
    {
        RigTargetEdit& targetEdit = RigTargetEdit::get();
        targetEdit                = RigTargetEdit{};
        targetEdit._objectID      = bOn ? _selectedObjectID : 0;
        targetEdit._targetIndex   = bOn ? static_cast<int32>( targetIndex ) : constant::invalid_index::kInt32;
    }

    void RigDebugViewModel::submitBoneMarkers( const float4& color )
    {
        using Internal                         = RigDebugViewModelInternal;
        const PoseModifierComponent* pModifier = Internal::findModifier( _selectedObjectID );
        const SkeletalMeshComponent* pUnit     = pModifier != nullptr ? Internal::findUnit( *pModifier ) : nullptr;
        const bool                   bNoTarget = pUnit == nullptr || _selectedNode < 0 || static_cast<uint32>( _selectedNode ) >= pModifier->getRigInstance().getNodeCount();
        if ( bNoTarget )
            return;
        vector<uint32> listBone;
        pModifier->getRigInstance().getNode( static_cast<uint32>( _selectedNode ) ).collectWrittenBones( listBone );
        const vector<float4x4>& listModel = pUnit->getModelSpaceTransforms();
        const Skeleton&         skeleton  = pUnit->getSkeleton();
        const float4x4          world     = pUnit->getWorldMatrix();
        const hashed_string     category( Internal::kDrawCategory );
        DebugDrawQueue&         queue = editor::getSubsystem<DebugDrawQueue>();
        for ( const uint32 boneIndex : listBone )
        {
            if ( boneIndex >= listModel.size() || boneIndex >= skeleton.getBoneCount() )
                continue;
            const float3 position = float3::transform( listModel[boneIndex].getTranslation(), world );
            queue.drawSphere( position, Internal::kMarkerRadius, color, 0.0f, category );
            queue.drawText( position, skeleton.getBone( boneIndex )._name.c_str(), color, 0.0f, category );
        }
    }
} // namespace sw::editor
