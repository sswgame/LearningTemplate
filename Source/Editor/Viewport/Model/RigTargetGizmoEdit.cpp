// 리그 대상 오프셋 기즈모의 Model 쪽(모으기 · 적용, ImGui 없음)입니다. 그리기와 조작은 `Visualizers/RigTargetGizmoVisualizer.cpp`(MVVM View)다.
#include "pch.h"

#include "Editor/Viewport/Model/RigTargetGizmoEdit.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/Asset/RigSaveParams.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/Model/EditorViewportMouseClaim.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw::editor
{
    RigTargetEdit& RigTargetEdit::get()
    {
        return getEditorState<RigTargetEdit>();
    }

    namespace
    {
        struct RigTargetGizmoEditInternal
        {
            /** @brief 놓은 값이 파일에서 다시 읽힌 값과 같다고 보는 위치 차이(월드 단위)입니다. */
            static constexpr float32 kReloadDistance = 1.0e-4f;
            /** @brief 놓은 값이 파일에서 다시 읽힌 값과 같다고 보는 회전 내적의 여유입니다. */
            static constexpr float32 kReloadDotSlack = 1.0e-5f;
            /** @brief 워크스페이스 기즈모 조작의 회전 값입니다(`EditorWorkspace::getGizmoOperation`). */
            static constexpr int32 kRotateOperation = 1;

            /** @brief 고른 대상이 풀린 한 프레임의 값입니다. */
            struct Target
            {
                PoseModifierComponent* _pModifier{ nullptr };
                const RigTargetDef*    _pDef{ nullptr };
                float4x4               _parentWorld{};
            };

            /** @brief 오브젝트 id 의 리그 컴포넌트입니다(없으면 nullptr). */
            static PoseModifierComponent* findModifier( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = ( pManager != nullptr && objectID != 0 ) ? pManager->findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<PoseModifierComponent>() : nullptr;
            }

            /**
             * @brief 자기 유닛 대상의 부모 공간(데이터 오프셋을 곱하기 전, 소켓 로컬 × 본 × 유닛)의 월드 행렬입니다. 바깥 대상이면 false 입니다.
             * @details 대상 월드 = 데이터 오프셋 × 소켓 로컬 × 본 모델 × 유닛 월드(행 벡터). 인스턴스는 앞의 둘을 합친 값만 가지므로 데이터 오프셋의 역을 앞에 곱해 되짚는다.
             */
            static bool computeTargetParentWorld( const PoseModifierComponent& modifier, const SkeletalMeshComponent& unit, uint32 targetIndex, float4x4& outParentWorld )
            {
                const RigInstance& instance = modifier.getRigInstance();
                optional<uint32>   bone;
                BoneTransform      combined{};
                if ( instance.tryGetOwnTargetBinding( targetIndex, bone, combined ) == false )
                    return false;
                const vector<float4x4>& listModel = unit.getModelSpaceTransforms();
                const bool              bOnBone   = bone.has_value() && *bone < listModel.size();
                const float4x4          boneWorld = bOnBone ? listModel[*bone] * unit.getWorldMatrix() : unit.getWorldMatrix();
                const RigTargetDef&     def       = instance.getTargetDef( targetIndex );
                outParentWorld                    = def._offset.toMatrix().invert() * combined.toMatrix() * boneWorld;
                return true;
            }

            /** @brief 창이 고른 자기 유닛 대상을 풉니다. 대상이 없거나 부모 공간을 풀 수 없으면 false 입니다. */
            static bool resolveTarget( const RigTargetEdit& edit, Target& outTarget )
            {
                PoseModifierComponent* pModifier = edit._targetIndex.has_value() ? findModifier( edit._objectID ) : nullptr;
                GameObject*            pObject   = pModifier != nullptr ? pModifier->getOwner() : nullptr;
                SkeletalMeshComponent* pUnit     = pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
                const bool             bReady    = pUnit != nullptr && pModifier->getRigInstance().isInitialized() && *edit._targetIndex < pModifier->getRigInstance().getTargetCount();
                if ( bReady == false || computeTargetParentWorld( *pModifier, *pUnit, *edit._targetIndex, outTarget._parentWorld ) == false )
                    return false;
                outTarget._pModifier = pModifier;
                outTarget._pDef      = &pModifier->getRigInstance().getTargetDef( *edit._targetIndex );
                return true;
            }
        };
    } // namespace

    void RigTargetGizmoCommand::step( bool bDragging, const float4x4* pMovedWorld, bool bUsing, vector<RigTargetGizmoCommand>& outListCommand )
    {
        if ( pMovedWorld != nullptr )
            outListCommand.push_back( RigTargetGizmoCommand{ RigTargetGizmoCommandKind::Drag, *pMovedWorld } );
        if ( ( bDragging || pMovedWorld != nullptr ) && bUsing == false )
            outListCommand.push_back( RigTargetGizmoCommand{ RigTargetGizmoCommandKind::Release, float4x4{} } );
    }

    void RigTargetEdit::gather( const CameraComponent* pActiveCamera, float32 canvasAspect, RigTargetGizmoViewState& outState )
    {
        using Internal        = RigTargetGizmoEditInternal;
        outState              = RigTargetGizmoViewState{};
        RigTargetEdit&   edit = RigTargetEdit::get();
        Internal::Target target{};
        if ( pActiveCamera == nullptr || Internal::resolveTarget( edit, target ) == false )
        {
            EditorViewportMouseClaim::release( &edit );
            return;
        }
        const RigTargetDef& def = *target._pDef;
        // 놓은 뒤 파일이 다시 읽혀 인스턴스가 같은 값이 되면 남겨 둔 값을 버린다.
        const bool bReloaded = edit._bDragging == SW_FALSE && edit._bPending == SW_TRUE &&
                               float3::getDistance( edit._pending._translation, def._offset._translation ) < Internal::kReloadDistance &&
                               MathUtil::abs( edit._pending._rotation.dot( def._offset._rotation ) ) > 1.0f - Internal::kReloadDotSlack;
        if ( bReloaded )
            edit._bPending = SW_FALSE;
        const BoneTransform offset = edit._bPending == SW_TRUE ? edit._pending : def._offset;

        EditorViewportMouseClaim::acquire( &edit );
        outState._view       = pActiveCamera->getViewMatrix();
        outState._projection = pActiveCamera->getProjectionMatrix( canvasAspect );
        outState._world      = offset.toMatrix() * target._parentWorld;
        outState._bRotate    = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getGizmoOperation() == Internal::kRotateOperation;
        outState._bDragging  = edit._bDragging == SW_TRUE;
        outState._bAvailable = true;
    }

    void RigTargetEdit::apply( const vector<RigTargetGizmoCommand>& listCommand )
    {
        using Internal      = RigTargetGizmoEditInternal;
        RigTargetEdit& edit = *this;
        for ( const RigTargetGizmoCommand& command : listCommand )
        {
            Internal::Target target{};
            if ( Internal::resolveTarget( edit, target ) == false )
            {
                edit._bDragging = SW_FALSE; // 대상이 사라졌다 — 저장할 곳이 없다
                return;
            }
            if ( command._kind == RigTargetGizmoCommandKind::Drag )
            {
                BoneTransform moved = BoneTransform::makeFromMatrix( command._world * target._parentWorld.invert() );
                moved._scale        = float3{ 1.0f, 1.0f, 1.0f };
                edit._pending       = moved;
                edit._bPending      = SW_TRUE;
                edit._bDragging     = SW_TRUE;
                continue;
            }
            edit._bDragging = SW_FALSE;
            RigSaveParams params;
            params._listTarget.push_back( RigTargetSaveParams{ target._pDef->_name, edit._pending._translation, edit._pending._rotation } );
            edit._status = RigSaveParams::writeToRigFile( *target._pModifier, params, "Move Rig Target" );
            // 저장하지 못했으면 파일이 다시 읽히지 않는다 — 남겨 둔 값을 버려 기즈모가 파일 값으로 돌아오게 한다.
            if ( edit._status != RigSaveParams::kSavedStatus )
                edit._bPending = SW_FALSE;
        }
    }
} // namespace sw::editor
