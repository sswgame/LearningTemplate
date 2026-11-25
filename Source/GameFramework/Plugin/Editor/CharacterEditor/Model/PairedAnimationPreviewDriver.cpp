#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/PairedAnimationPreviewDriver.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Scene/Scene.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/PairedAnimationEditModel.h"

namespace sw::editor
{
    namespace
    {
        struct PairedAnimationPreviewDriverInternal
        {
            /// @brief 기즈모가 옮겼다고 보는 자리 차이(m)와 요 차이(라디안)입니다. 창이 놓은 값을 다시 읽는 반올림은 이보다 작다.
            static constexpr float32 kMovedToleranceMeters  = 1.0e-4f;
            static constexpr float32 kMovedToleranceRadians = 1.0e-4f;

            /** @brief 자리 · 요(라디안)의 월드 변환입니다. */
            static float4x4 makeWorld( const float3& position, float32 yaw )
            {
                return float4x4::makeTrs( position, quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, yaw ), float3{ 1.0f, 1.0f, 1.0f } );
            }
        };
    } // namespace

    void PairedAnimationPreviewDriver::clearSpawned()
    {
        for ( PairedAnimationRoleSlot& slot : _listSlot )
        {
            slot._object = GameObjectHandle{};
            slot._spawnedPath.clear();
        }
    }

    void PairedAnimationPreviewDriver::destroySpawned( Scene* pScene )
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        for ( PairedAnimationRoleSlot& slot : _listSlot )
        {
            GameObject* pObject = pManager != nullptr ? pManager->resolveGameObject( slot._object ) : nullptr;
            if ( pObject != nullptr )
                pManager->destroyGameObject( pObject );
        }
        clearSpawned();
    }

    void PairedAnimationPreviewDriver::sync( Scene* pScene, PairedAnimationEditModel& model, bool bGizmoHasMouse, string& inoutStatus, bool& outEdited, expected<void>& outEdit )
    {
        outEdited                   = false;
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        const vector<PairedAnimationRoleDef>& listRole = model.getContent()._listRole;
        _listSlot.resize( listRole.size() );
        const float3  anchorPosition{};
        const float32 anchorYaw = 0.0f;
        for ( uint32 roleIndex = 0; roleIndex < static_cast<uint32>( listRole.size() ); ++roleIndex )
        {
            ensureRoleObject( *pManager, model, roleIndex, inoutStatus );
            PairedAnimationRoleSlot& slot    = _listSlot[roleIndex];
            GameObject*              pObject = pManager->resolveGameObject( slot._object );
            SceneComponent*          pRoot   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pRoot == nullptr )
                continue;

            // 기즈모가 정렬 역할을 옮겼으면 그 자리를 오프셋으로 넣는다.
            const PairedAnimationRoleDef& role     = listRole[roleIndex];
            const bool                    bAligned = role._bAnchor == false && role._align != PairedAnimationAlign::None;
            const float3                  current  = pRoot->getWorldPosition();
            const float32                 yaw      = OrientationUtil::computeYawFromWorld( pRoot->getWorldMatrix() );
            const bool                    bMoved   = ( current - slot._placedPosition ).getLength() > PairedAnimationPreviewDriverInternal::kMovedToleranceMeters ||
                                MathUtil::abs( MathUtil::wrapAngle( yaw - slot._placedYaw ) ) > PairedAnimationPreviewDriverInternal::kMovedToleranceRadians;
            if ( bAligned && bMoved )
            {
                outEdit   = model.setRoleOffsetFromWorld( roleIndex, anchorPosition, anchorYaw, current, yaw, true );
                outEdited = true;
            }
            else if ( bMoved && bGizmoHasMouse == false )
                model.endContinuousEdit();

            float3  target{};
            float32 targetYaw = 0.0f;
            model.computeRoleTarget( roleIndex, anchorPosition, anchorYaw, target, targetYaw );
            pRoot->setWorldTransform( PairedAnimationPreviewDriverInternal::makeWorld( target, targetYaw ) );
            slot._placedPosition = pRoot->getWorldPosition();
            slot._placedYaw      = OrientationUtil::computeYawFromWorld( pRoot->getWorldMatrix() );

            SkeletalAnimatorComponent* pAnimator = pObject->getComponent<SkeletalAnimatorComponent>();
            if ( pAnimator != nullptr )
                poseRoleObject( *pAnimator, model, roleIndex );
        }
    }

    void PairedAnimationPreviewDriver::ensureRoleObject( GameObjectManager& manager, const PairedAnimationEditModel& model, uint32 roleIndex, string& inoutStatus )
    {
        PairedAnimationRoleSlot& slot       = _listSlot[roleIndex];
        const string             prefabPath = string( slot._prefabPath.c_str() );
        GameObject*              pExisting  = manager.resolveGameObject( slot._object );
        if ( pExisting != nullptr && slot._spawnedPath == prefabPath )
            return;
        if ( pExisting != nullptr )
            manager.destroyGameObject( pExisting );
        slot._object = GameObjectHandle{};
        slot._spawnedPath.clear();
        slot._clipSeconds = 0.0f;
        if ( prefabPath.empty() )
            return;

        AssetManager* pAssets  = manager.findAssetManager();
        GameObject*   pSpawned = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, prefabPath, SpawnLifetime::Persistent ) : nullptr;
        slot._spawnedPath      = prefabPath; // 실패해도 같은 경로로 매 프레임 다시 놓지 않는다(경로를 고치면 다시 한다)
        if ( pSpawned == nullptr )
        {
            StringBuilder<constant::buffer::kMax512> status;
            status.appendFormat( "Prefab '%#' could not be spawned - see the log", prefabPath );
            inoutStatus = string( status.view() );
            return;
        }
        slot._object                            = pSpawned->getHandle();
        slot._placedPosition                    = float3{};
        slot._placedYaw                         = 0.0f;
        SkeletalAnimatorComponent*    pAnimator = pSpawned->getComponent<SkeletalAnimatorComponent>();
        const PairedAnimationRoleDef& role      = model.getContent()._listRole[roleIndex];
        if ( pAnimator == nullptr )
        {
            StringBuilder<constant::buffer::kMax512> status;
            status.appendFormat( "Prefab '%#' has no skeletal animator on its root - the role cannot play", prefabPath );
            inoutStatus = string( status.view() );
            return;
        }
        pAnimator->setPlayRate( 0.0f ); // 창이 시각을 정한다
        const AnimationClip* pClip = pAnimator->preloadClip( role._clip ) ? pAnimator->findClip( role._clip ) : nullptr;
        if ( pClip == nullptr )
        {
            StringBuilder<constant::buffer::kMax256> status;
            status.appendFormat( "Role clip '%#' is not in the prefab's clip folder", role._clip.c_str() );
            inoutStatus = string( status.view() );
            return;
        }
        slot._clipSeconds = pClip->getDuration();
    }

    void PairedAnimationPreviewDriver::poseRoleObject( SkeletalAnimatorComponent& animator, const PairedAnimationEditModel& model, uint32 roleIndex ) const
    {
        const PairedAnimationRoleDef&    role    = model.getContent()._listRole[roleIndex];
        const PairedAnimationRolePreview preview = model.computeRolePreview( roleIndex, _listSlot[roleIndex]._clipSeconds );
        if ( preview._stage == PairedAnimationPreviewStage::Exited )
        {
            if ( animator.getCurrentStateName() != role._exitState )
                (void)animator.play( role._exitState, true, 0.0f ); // 못 찾은 종료 상태는 `play` 가 오류로 남긴다
            return;
        }
        if ( _listSlot[roleIndex]._clipSeconds <= 0.0f )
            return; // 클립을 읽지 못했다 — 상태 줄이 알렸다
        if ( animator.getCurrentStateName() != role._clip )
            (void)animator.play( role._clip, false, 0.0f ); // 클립은 ensureRoleObject 에서 읽었다
        // 기다리는 역할은 클립 첫 자세를 보인다(세션에서는 아직 다른 것을 재생하지만, 저작에는 진입 자세가 쓸모 있다).
        const float32 clipTime = preview._stage == PairedAnimationPreviewStage::Playing ? preview._clipSeconds : 0.0f;
        (void)animator.seekCurrentTime( clipTime ); // 범위 밖은 모델이 자른다 — 실패는 seek 가 오류로 남긴다
    }
} // namespace sw::editor
