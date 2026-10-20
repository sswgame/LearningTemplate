#include "pch.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSession.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/YawUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyComponent.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/LocomotionWarpingComponent.h"
#include "Engine/Object/SceneAnimation/MotionWarpingComponent.h"

namespace sw
{
    SW_LOG_CALLER( "PairedAnimation" );

    namespace
    {
        struct PairedAnimationSessionInternal
        {
            /** @brief 세션이 참가자의 `MotionWarpingComponent` 에 넣는 목표 이름입니다. 참가자는 한 번에 한 세션에만 있으므로 하나면 된다. */
            static const hashed_string& getWarpTargetName()
            {
                static const hashed_string s_name( "PairedAnimation" );
                return s_name;
            }

            /** @brief 애니메이터 오브젝트의 이름입니다(로그용). */
            static hashed_string findOwnerName( const SkeletalAnimatorComponent& animator )
            {
                const GameObject* pOwner = animator.getOwner();
                return pOwner != nullptr ? pOwner->getName() : hashed_string( "(no owner)" );
            }

            /** @brief 애니메이터 오브젝트의 루트 씬 컴포넌트입니다. 없으면 nullptr 입니다. */
            static SceneComponent* findRoot( const SkeletalAnimatorComponent& animator )
            {
                GameObject* pOwner = animator.getOwner();
                return pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
            }

            /** @brief 애니메이터 오브젝트의 컴포넌트입니다. 없으면 nullptr 입니다. */
            template <typename TComponent>
            static TComponent* findSibling( const SkeletalAnimatorComponent& animator )
            {
                GameObject* pOwner = animator.getOwner();
                return pOwner != nullptr ? pOwner->getComponent<TComponent>() : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PairedAnimationSession::PairedAnimationSession( uint32 sessionID, shared_ptr<const PairedAnimationDef> def )
        : _def{ std::move( def ) }
        , _listRole{}
        , _sessionID{ sessionID }
        , _nextNotifyIndex{ 0 }
        , _anchorTime{ 0.0f }
        , _predictedAnchorTime{ 0.0f }
        , _state{ PairedAnimationSessionState::Idle }
        , _bClockStarted{ false }
    {
        SW_ASSERT( _def != nullptr );
    }

    SkeletalAnimatorComponent* PairedAnimationSession::resolveAnimator( GameObjectManager& manager, const RoleState& role ) const
    {
        return role._animator.isValid() ? manager.resolveComponent( role._animator ) : nullptr;
    }

    bool PairedAnimationSession::tryGetClipTime( const SkeletalAnimatorComponent& animator, const RoleState& role, float32& outTime )
    {
        const AnimationPlayer& player = animator.getGraphPlayer().getPlayer();
        if ( player.getCurrentPlayable() == role._pClip )
        {
            outTime = player.getCurrentTime();
            return true;
        }
        // 들어오는 크로스페이드 중에는 세션 클립이 다음 슬롯이다.
        if ( player.getNextPlayable() == role._pClip )
        {
            outTime = player.getNextTime();
            return true;
        }
        return false;
    }

    bool PairedAnimationSession::hasParticipant( const TComponentHandle<SkeletalAnimatorComponent>& animator ) const
    {
        for ( const RoleState& role : _listRole )
        {
            const bool bActive = role._stage == RoleStage::Waiting || role._stage == RoleStage::Playing;
            if ( bActive && role._animator == animator )
                return true;
        }
        return false;
    }

    uint32 PairedAnimationSession::getPlayingRoleCount() const
    {
        uint32 playingCount = 0;
        for ( const RoleState& role : _listRole )
        {
            if ( role._stage == RoleStage::Playing )
                ++playingCount;
        }
        return playingCount;
    }

    bool PairedAnimationSession::isRolePlaying( const hashed_string& roleName ) const
    {
        const uint32 roleIndex = _def->findRoleIndex( roleName );
        return roleIndex < _listRole.size() && _listRole[roleIndex]._stage == RoleStage::Playing;
    }

    bool PairedAnimationSession::tryComputeAlignTarget( GameObjectManager& manager, const PairedAnimationRoleDef& roleDef, float3& outPosition, float32& outYaw ) const
    {
        const SkeletalAnimatorComponent* pAnchor = resolveAnimator( manager, _listRole[_def->getAnchorRoleIndex()] );
        const SceneComponent*            pRoot   = pAnchor != nullptr ? PairedAnimationSessionInternal::findRoot( *pAnchor ) : nullptr;
        if ( pRoot == nullptr )
            return false;
        // 오프셋은 앵커 요로 돌린다(+Z 가 앞). 같은 축 · 각 규칙을 `MotionWarpingComponent` 가 목표 요로 쓴다.
        const float32    anchorYaw = YawUtil::fromWorld( pRoot->getWorldMatrix() );
        const quaternion turn      = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, anchorYaw );
        outPosition                = pRoot->getWorldPosition() + float3::transform( roleDef._offset, turn );
        outYaw                     = MathUtil::wrapAngle( anchorYaw + roleDef._yawRadians );
        return true;
    }

    bool PairedAnimationSession::isWithinAlignDistance( GameObjectManager& manager, uint32 roleIndex ) const
    {
        const PairedAnimationRoleDef& roleDef = _def->getRoles()[roleIndex];
        if ( roleDef._align == PairedAnimationAlign::None )
            return true;
        const SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, _listRole[roleIndex] );
        const SceneComponent*            pRoot     = pAnimator != nullptr ? PairedAnimationSessionInternal::findRoot( *pAnimator ) : nullptr;
        float3                           target{};
        float32                          targetYaw = 0.0f;
        if ( pRoot == nullptr || tryComputeAlignTarget( manager, roleDef, target, targetYaw ) == false )
            return false;
        return ( target - pRoot->getWorldPosition() ).getLength() <= roleDef._maxAlignDistance;
    }

    PairedAnimationStartResult PairedAnimationSession::start( GameObjectManager& manager, const vector<PairedAnimationBinding>& listBinding )
    {
        SW_ASSERT( _state == PairedAnimationSessionState::Idle );
        const PairedAnimationDef&             def         = *_def;
        const vector<PairedAnimationRoleDef>& listRoleDef = def.getRoles();
        _listRole.assign( listRoleDef.size(), RoleState{} );

        // 1) 바인딩 — 모르는 역할, 같은 역할 두 번, 같은 애니메이터 두 번은 부르는 쪽의 결함이다.
        for ( const PairedAnimationBinding& binding : listBinding )
        {
            const uint32 roleIndex = def.findRoleIndex( binding._role );
            if ( roleIndex == invalid_index::kUint32 )
            {
                SW_LOG_ERROR( "Paired animation binds unknown role '%#'", binding._role.c_str() );
                return PairedAnimationStartResult::InvalidBinding;
            }
            if ( _listRole[roleIndex]._animator.isValid() )
            {
                SW_LOG_ERROR( "Paired animation binds role '%#' twice", binding._role.c_str() );
                return PairedAnimationStartResult::InvalidBinding;
            }
            for ( const RoleState& other : _listRole )
            {
                if ( other._animator == binding._animator )
                {
                    SW_LOG_ERROR( "Paired animation binds one animator to two roles (second: '%#')", binding._role.c_str() );
                    return PairedAnimationStartResult::InvalidBinding;
                }
            }
            _listRole[roleIndex]._animator = binding._animator;
        }

        // 2) 참가자 — 해석, 스켈레톤, 클립과 길이, 워핑.
        for ( size_t roleIndex = 0; roleIndex < listRoleDef.size(); ++roleIndex )
        {
            const PairedAnimationRoleDef& roleDef = listRoleDef[roleIndex];
            RoleState&                    role    = _listRole[roleIndex];
            if ( role._animator.isValid() == false )
            {
                if ( roleDef._bRequired == false )
                    continue;
                SW_LOG_ERROR( "Paired animation needs required role '%#'", roleDef._name.c_str() );
                return PairedAnimationStartResult::MissingRequiredRole;
            }
            SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, role );
            if ( pAnimator == nullptr )
                return PairedAnimationStartResult::ParticipantMissing;
            const hashed_string ownerName = PairedAnimationSessionInternal::findOwnerName( *pAnimator );
            if ( roleDef._skeletonPath.empty() == false )
            {
                const SkeletalMeshComponent* pMesh         = PairedAnimationSessionInternal::findSibling<SkeletalMeshComponent>( *pAnimator );
                const bool                   bSameSkeleton = pMesh != nullptr && pMesh->getSkeletonPath() == roleDef._skeletonPath;
                if ( bSameSkeleton == false )
                {
                    SW_LOG_ERROR( "'%#': role '%#' needs skeleton '%#'", ownerName.c_str(), roleDef._name.c_str(), roleDef._skeletonPath.c_str() );
                    return PairedAnimationStartResult::SkeletonMismatch;
                }
            }
            const AnimationClip* pClip = pAnimator->preloadClip( roleDef._clip ) ? pAnimator->findClip( roleDef._clip ) : nullptr;
            if ( pClip == nullptr )
            {
                SW_LOG_ERROR( "'%#': role '%#' clip '%#' was not found in '%#'", ownerName.c_str(), roleDef._name.c_str(), roleDef._clip.c_str(), pAnimator->getClipFolder().c_str() );
                return PairedAnimationStartResult::ClipNotFound;
            }
            role._pClip       = pClip;
            role._clipSeconds = pClip->getDuration();
            const bool bClipLengthMismatch =
                roleDef._bAnchor ? MathUtil::abs( role._clipSeconds - def.getDuration() ) > kTimeToleranceSeconds : roleDef._startSeconds + role._clipSeconds > def.getDuration() + kTimeToleranceSeconds;
            if ( bClipLengthMismatch )
            {
                SW_LOG_ERROR( "'%#': role '%#' clip '%#' (%# s) does not fit the %# s timeline", ownerName.c_str(), roleDef._name.c_str(), roleDef._clip.c_str(), role._clipSeconds, def.getDuration() );
                return PairedAnimationStartResult::ClipLengthMismatch;
            }
            if ( roleDef._align == PairedAnimationAlign::Warp )
            {
                if ( PairedAnimationSessionInternal::findSibling<MotionWarpingComponent>( *pAnimator ) == nullptr )
                {
                    SW_LOG_ERROR( "'%#': role '%#' aligns by warp but has no MotionWarpingComponent", ownerName.c_str(), roleDef._name.c_str() );
                    return PairedAnimationStartResult::WarpingUnavailable;
                }
                if ( roleDef._warpEndSeconds > role._clipSeconds + kTimeToleranceSeconds )
                {
                    SW_LOG_ERROR( "'%#': role '%#' warp ends after its clip", ownerName.c_str(), roleDef._name.c_str() );
                    return PairedAnimationStartResult::ClipLengthMismatch;
                }
            }
            role._stage = RoleStage::Waiting;
        }

        // 3) 공유 알림 — 받을 역할이 묶였으면 그 오브젝트의 표에 항목이 있어야 한다(조용히 건너뛰지 않는다).
        for ( const PairedAnimationNotifyDef& notify : def.getNotifies() )
        {
            const RoleState& role = _listRole[def.findRoleIndex( notify._role )];
            if ( role._stage == RoleStage::Unbound )
                continue;
            const SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, role );
            const AnimationNotifyComponent*  pNotify   = pAnimator != nullptr ? PairedAnimationSessionInternal::findSibling<AnimationNotifyComponent>( *pAnimator ) : nullptr;
            const AnimationNotifyTable*      pTable    = pNotify != nullptr ? pNotify->getNotifyTable() : nullptr;
            if ( pTable == nullptr || pTable->findEntry( notify._name ) == nullptr )
            {
                SW_LOG_ERROR( "Paired animation notify '%#' has no entry in the notify table of role '%#'", notify._name.c_str(), notify._role.c_str() );
                return PairedAnimationStartResult::NotifyTargetMissing;
            }
        }

        // 4) 진입 조건 — 처음부터 들어오는 정렬 역할이 목표 가까이 있어야 한다.
        for ( size_t roleIndex = 0; roleIndex < listRoleDef.size(); ++roleIndex )
        {
            const bool bEntersNow = _listRole[roleIndex]._stage == RoleStage::Waiting && listRoleDef[roleIndex]._startSeconds <= 0.0f;
            if ( bEntersNow && isWithinAlignDistance( manager, static_cast<uint32>( roleIndex ) ) == false )
                return PairedAnimationStartResult::TooFarToAlign;
        }

        // 5) 바꾼다 — 앵커 먼저(정렬 목표가 앵커 루트를 읽는다), 그다음 0 초에 들어오는 역할.
        _state = PairedAnimationSessionState::Playing;
        enterRole( manager, def.getAnchorRoleIndex(), 0.0f );
        for ( size_t roleIndex = 0; roleIndex < listRoleDef.size(); ++roleIndex )
        {
            const bool bEntersNow = _listRole[roleIndex]._stage == RoleStage::Waiting && listRoleDef[roleIndex]._startSeconds <= 0.0f;
            if ( bEntersNow )
                enterRole( manager, static_cast<uint32>( roleIndex ), 0.0f );
        }
        return PairedAnimationStartResult::Started;
    }

    void PairedAnimationSession::enterRole( GameObjectManager& manager, uint32 roleIndex, float32 anchorTime )
    {
        const PairedAnimationRoleDef& roleDef   = _def->getRoles()[roleIndex];
        RoleState&                    role      = _listRole[roleIndex];
        SkeletalAnimatorComponent*    pAnimator = resolveAnimator( manager, role );
        SW_ASSERT( pAnimator != nullptr );
        // 모든 역할이 같은 속도로 흘러야 앵커 시각에서 역할 시각을 정확히 안다. 끝나면 되돌린다. 속도를 매 프레임 쓰는 보폭 보정은 그동안 쥐어 둔다.
        LocomotionWarpingComponent* pLocomotion = PairedAnimationSessionInternal::findSibling<LocomotionWarpingComponent>( *pAnimator );
        if ( pLocomotion != nullptr )
            pLocomotion->setPlayRateHeld( true );
        role._previousPlayRate = pAnimator->getPlayRate();
        pAnimator->setPlayRate( 1.0f );
        // 클립은 시작 검사에서 읽어 두었다 — 여기서 못 찾으면 `play` 가 오류를 남긴다.
        (void)pAnimator->play( roleDef._clip, false, roleDef._enterBlendSeconds ); // 시작 검사에서 클립을 읽었고, 실패는 `play` 가 로그로 남긴다
        role._entryAnchorTime = anchorTime;
        role._stage           = RoleStage::Playing;

        float3  target{};
        float32 targetYaw = 0.0f;
        if ( roleDef._align == PairedAnimationAlign::None || tryComputeAlignTarget( manager, roleDef, target, targetYaw ) == false )
            return;
        if ( roleDef._align == PairedAnimationAlign::Snap )
        {
            SceneComponent* pRoot = PairedAnimationSessionInternal::findRoot( *pAnimator );
            if ( pRoot != nullptr )
                pRoot->setWorldTransform( float4x4::makeTrs( target, quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, targetYaw ), pRoot->getLocalScale() ) );
            return;
        }
        // Warp — 창은 클립 시각 0 부터 `warpEnd` 까지다. 창 끝 프레임에 비율 1 이 되어 목표에 닿는다(`MotionWarpingComponent`).
        MotionWarpingComponent* pWarping = PairedAnimationSessionInternal::findSibling<MotionWarpingComponent>( *pAnimator );
        SW_ASSERT( pWarping != nullptr );
        pWarping->setWarpTarget( PairedAnimationSessionInternal::getWarpTargetName(), target, targetYaw );
        MotionWarpWindow window{};
        window._target       = PairedAnimationSessionInternal::getWarpTargetName();
        window._pClip        = role._pClip;
        window._endTime      = roleDef._warpEndSeconds;
        window._bTranslation = true;
        window._bRotation    = true;
        window._bVertical    = false;
        pWarping->beginWindow( window );
        role._bWarpOpen = true;
    }

    void PairedAnimationSession::refreshWarpTarget( GameObjectManager& manager, uint32 roleIndex )
    {
        const SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, _listRole[roleIndex] );
        MotionWarpingComponent*          pWarping  = pAnimator != nullptr ? PairedAnimationSessionInternal::findSibling<MotionWarpingComponent>( *pAnimator ) : nullptr;
        float3                           target{};
        float32                          targetYaw = 0.0f;
        if ( pWarping != nullptr && tryComputeAlignTarget( manager, _def->getRoles()[roleIndex], target, targetYaw ) )
            pWarping->setWarpTarget( PairedAnimationSessionInternal::getWarpTargetName(), target, targetYaw );
    }

    void PairedAnimationSession::closeWarpWindow( GameObjectManager& manager, uint32 roleIndex, bool bCheckReached )
    {
        RoleState& role = _listRole[roleIndex];
        if ( role._bWarpOpen == false )
            return;
        role._bWarpOpen                            = false;
        const SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, role );
        MotionWarpingComponent*          pWarping  = pAnimator != nullptr ? PairedAnimationSessionInternal::findSibling<MotionWarpingComponent>( *pAnimator ) : nullptr;
        if ( pWarping == nullptr )
            return;
        const MotionWarpTarget* pTarget = pWarping->findWarpTarget( PairedAnimationSessionInternal::getWarpTargetName() );
        const SceneComponent*   pRoot   = PairedAnimationSessionInternal::findRoot( *pAnimator );
        // 창이 끝까지 열려 있었는데 목표에 없다 — 루트 모션을 뽑지 않거나 워핑이 애니메이터에 붙지 않았다. 조용히 넘기지 않는다.
        const bool bMissed = bCheckReached && pTarget != nullptr && pRoot != nullptr && ( pTarget->_position - pRoot->getWorldPosition() ).getLength() > kWarpReachToleranceMeters;
        if ( bMissed )
            SW_LOG_ERROR( "'%#': paired role '%#' did not reach its warp target (is root motion extracted?)", PairedAnimationSessionInternal::findOwnerName( *pAnimator ).c_str(),
                          _def->getRoles()[roleIndex]._name.c_str() );
        pWarping->endWindow( PairedAnimationSessionInternal::getWarpTargetName(), role._pClip );
        pWarping->clearWarpTarget( PairedAnimationSessionInternal::getWarpTargetName() );
    }

    void PairedAnimationSession::exitRole( GameObjectManager& manager, uint32 roleIndex )
    {
        RoleState& role = _listRole[roleIndex];
        if ( role._stage != RoleStage::Playing )
        {
            if ( role._stage == RoleStage::Waiting )
                role._stage = RoleStage::Exited;
            return;
        }
        closeWarpWindow( manager, roleIndex, false );
        role._stage                          = RoleStage::Exited;
        SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, role );
        if ( pAnimator == nullptr )
            return;
        LocomotionWarpingComponent* pLocomotion = PairedAnimationSessionInternal::findSibling<LocomotionWarpingComponent>( *pAnimator );
        if ( pLocomotion != nullptr )
            pLocomotion->setPlayRateHeld( false );
        pAnimator->setPlayRate( role._previousPlayRate );
        // 게임플레이가 이미 다른 것을 재생하면(세션 클립이 빠졌으면) 그 결정을 덮지 않는다.
        float32 clipTime = 0.0f;
        if ( tryGetClipTime( *pAnimator, role, clipTime ) == false )
            return;
        const PairedAnimationRoleDef& roleDef = _def->getRoles()[roleIndex];
        (void)pAnimator->play( roleDef._exitState, true, roleDef._exitBlendSeconds ); // 못 찾은 종료 상태는 `play` 가 오류로 남긴다
    }

    void PairedAnimationSession::end( GameObjectManager& manager, PairedAnimationSessionState state )
    {
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            exitRole( manager, static_cast<uint32>( roleIndex ) );
        }
        _state = state;
    }

    void PairedAnimationSession::abort( GameObjectManager& manager )
    {
        if ( _state == PairedAnimationSessionState::Playing )
            end( manager, PairedAnimationSessionState::Aborted );
    }

    bool PairedAnimationSession::loseRole( GameObjectManager& manager, uint32 roleIndex, const utf8* pReason )
    {
        const PairedAnimationRoleDef& roleDef = _def->getRoles()[roleIndex];
        if ( roleDef._bRequired )
        {
            SW_LOG_WARNING( "Paired animation %# aborts: required role '%#' %#", _sessionID, roleDef._name.c_str(), pReason );
            end( manager, PairedAnimationSessionState::Aborted );
            return true;
        }
        exitRole( manager, roleIndex );
        return false;
    }

    void PairedAnimationSession::fireNotify( GameObjectManager& manager, uint32 notifyIndex )
    {
        const PairedAnimationNotifyDef&  notify    = _def->getNotifies()[notifyIndex];
        const RoleState&                 role      = _listRole[_def->findRoleIndex( notify._role )];
        const bool                       bBound    = role._stage == RoleStage::Waiting || role._stage == RoleStage::Playing;
        const SkeletalAnimatorComponent* pAnimator = bBound ? resolveAnimator( manager, role ) : nullptr;
        AnimationNotifyComponent*        pNotify   = pAnimator != nullptr ? PairedAnimationSessionInternal::findSibling<AnimationNotifyComponent>( *pAnimator ) : nullptr;
        if ( pNotify == nullptr )
            return;
        AnimationFiredNotify fired{};
        fired._name       = notify._name;
        fired._pSource    = role._pClip;
        fired._time       = notify._time;
        fired._weight     = 1.0f;
        fired._duration   = 0.0f;
        fired._eventIndex = notifyIndex;
        fired._phase      = AnimationNotifyPhase::Instant;
        if ( pNotify->processExternalNotify( fired ) == false )
            SW_LOG_ERROR( "Paired animation notify '%#' lost its entry in the notify table of role '%#'", notify._name.c_str(), notify._role.c_str() );
    }

    void PairedAnimationSession::update( GameObjectManager& manager, float32 deltaSeconds )
    {
        if ( _state != PairedAnimationSessionState::Playing )
            return;
        const PairedAnimationDef&             def         = *_def;
        const vector<PairedAnimationRoleDef>& listRoleDef = def.getRoles();
        const uint32                          anchorIndex = def.getAnchorRoleIndex();

        // 1) 잃은 역할 — 애니메이터가 없거나 세션 클립을 재생하지 않는다(게임플레이가 다른 것을 재생했다).
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            RoleState& role = _listRole[roleIndex];
            if ( role._stage != RoleStage::Waiting && role._stage != RoleStage::Playing )
                continue;
            const SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, role );
            float32                          clipTime  = 0.0f;
            const bool                       bMissing  = pAnimator == nullptr;
            const bool                       bHijacked = bMissing == false && role._stage == RoleStage::Playing && tryGetClipTime( *pAnimator, role, clipTime ) == false;
            if ( bMissing == false && bHijacked == false )
                continue;
            if ( loseRole( manager, static_cast<uint32>( roleIndex ), bMissing ? "is gone" : "stopped playing its clip" ) )
                return;
        }

        // 2) 시계 — 앵커 시각이 정본이다. 지난 갱신의 예측과 다르면 누가 속도 · 시각을 바꾼 것이다.
        const SkeletalAnimatorComponent* pAnchor    = resolveAnimator( manager, _listRole[anchorIndex] );
        float32                          anchorTime = 0.0f;
        if ( pAnchor == nullptr || tryGetClipTime( *pAnchor, _listRole[anchorIndex], anchorTime ) == false )
        {
            (void)loseRole( manager, anchorIndex, "lost its clock" ); // 앵커는 필수라 늘 중단한다
            return;
        }
        if ( _bClockStarted && MathUtil::abs( anchorTime - _predictedAnchorTime ) > kTimeToleranceSeconds )
        {
            SW_LOG_ERROR( "Paired animation %# aborts: anchor time %# differs from the expected %# (play rate or time changed outside the session)", _sessionID, anchorTime, _predictedAnchorTime );
            end( manager, PairedAnimationSessionState::Aborted );
            return;
        }
        _bClockStarted = true;
        _anchorTime    = anchorTime;

        // 3) 다른 역할의 시각 — 진입 때 앵커 시각만큼 뒤에서 같은 속도로 흐른다.
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            const RoleState& role = _listRole[roleIndex];
            if ( role._stage != RoleStage::Playing || roleIndex == anchorIndex )
                continue;
            float32 clipTime = 0.0f;
            (void)tryGetClipTime( *resolveAnimator( manager, role ), role, clipTime ); // 1) 에서 재생 중임을 확인했다
            const float32 expected = MathUtil::min( anchorTime - role._entryAnchorTime, role._clipSeconds );
            if ( MathUtil::abs( clipTime - expected ) > kTimeToleranceSeconds )
            {
                SW_LOG_ERROR( "Paired animation %# aborts: role '%#' time %# differs from the expected %#", _sessionID, listRoleDef[roleIndex]._name.c_str(), clipTime, expected );
                end( manager, PairedAnimationSessionState::Aborted );
                return;
            }
        }

        // 4) 이번 프레임 평가가 갈 시각까지의 공유 알림 — 클립 알림(평가 끝)과 같은 프레임에 보인다.
        const float32 predicted = MathUtil::min( anchorTime + deltaSeconds * pAnchor->getPlayRate(), def.getDuration() );
        while ( _nextNotifyIndex < def.getNotifies().size() && def.getNotifies()[_nextNotifyIndex]._time <= predicted )
        {
            fireNotify( manager, _nextNotifyIndex );
            ++_nextNotifyIndex;
        }

        // 5) 늦은 역할 진입, 워프 목표 · 창, 클립이 끝난 역할 나감.
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            const PairedAnimationRoleDef& roleDef = listRoleDef[roleIndex];
            RoleState&                    role    = _listRole[roleIndex];
            const uint32                  index   = static_cast<uint32>( roleIndex );
            if ( role._stage == RoleStage::Waiting && roleDef._startSeconds <= anchorTime + kTimeToleranceSeconds )
            {
                if ( isWithinAlignDistance( manager, index ) == false )
                {
                    if ( loseRole( manager, index, "is too far from its alignment target" ) )
                        return;
                    continue;
                }
                enterRole( manager, index, anchorTime );
                continue;
            }
            if ( role._stage != RoleStage::Playing || roleIndex == anchorIndex )
                continue;
            const float32 roleTime = anchorTime - role._entryAnchorTime;
            if ( role._bWarpOpen )
            {
                if ( roleTime + kTimeToleranceSeconds >= roleDef._warpEndSeconds )
                    closeWarpWindow( manager, index, true );
                else
                    refreshWarpTarget( manager, index );
            }
            if ( roleTime + kTimeToleranceSeconds >= role._clipSeconds )
                exitRole( manager, index );
        }

        // 6) 앵커가 끝에 닿았다 — 남은 역할을 모두 내보낸다.
        if ( anchorTime + kTimeToleranceSeconds >= def.getDuration() )
        {
            end( manager, PairedAnimationSessionState::Finished );
            return;
        }
        _predictedAnchorTime = predicted;
    }
} // namespace sw
