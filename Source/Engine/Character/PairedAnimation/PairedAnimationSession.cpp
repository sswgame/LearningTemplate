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
            const bool bActive = role._stage == PairedAnimationRoleStage::Waiting || role._stage == PairedAnimationRoleStage::Playing;
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
            if ( role._stage == PairedAnimationRoleStage::Playing )
                ++playingCount;
        }
        return playingCount;
    }

    bool PairedAnimationSession::isRolePlaying( const hashed_string& roleName ) const
    {
        const uint32 roleIndex = _def->findRoleIndex( roleName );
        return roleIndex < _listRole.size() && _listRole[roleIndex]._stage == PairedAnimationRoleStage::Playing;
    }

    bool PairedAnimationSession::tryComputeAlignTarget( GameObjectManager& manager, const PairedAnimationRoleDef& roleDef, float3& outPosition, float32& outYaw ) const
    {
        const SkeletalAnimatorComponent* pAnchor = resolveAnimator( manager, _listRole[_def->getAnchorRoleIndex()] );
        const SceneComponent*            pRoot   = pAnchor != nullptr ? PairedAnimationSessionInternal::findRoot( *pAnchor ) : nullptr;
        if ( pRoot == nullptr )
            return false;
        PairedAnimationDef::computeAlignTarget( pRoot->getWorldPosition(), YawUtil::fromWorld( pRoot->getWorldMatrix() ), roleDef, outPosition, outYaw );
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

    PairedAnimationStartResult PairedAnimationSession::bindRoles( const vector<PairedAnimationBinding>& listBinding )
    {
        const PairedAnimationDef& def = *_def;
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
        return PairedAnimationStartResult::Started;
    }

    PairedAnimationStartResult PairedAnimationSession::validateParticipant( GameObjectManager& manager, uint32 roleIndex )
    {
        const PairedAnimationDef&     def     = *_def;
        const PairedAnimationRoleDef& roleDef = def.getRoles()[roleIndex];
        RoleState&                    role    = _listRole[roleIndex];
        if ( role._animator.isValid() == false )
        {
            if ( roleDef._bRequired == false )
                return PairedAnimationStartResult::Started; // 선택 역할은 비어 있어도 된다
            SW_LOG_ERROR( "Paired animation needs required role '%#'", roleDef._name.c_str() );
            return PairedAnimationStartResult::MissingRequiredRole;
        }
        SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, role );
        if ( pAnimator == nullptr )
            return PairedAnimationStartResult::ParticipantMissing;
        // 시작 때 요청한 클립을 아직 읽는 중이면 재생 · 시각 맞추기가 미뤄져 세션의 시계와 어긋난다 — 시작하지 않는다.
        if ( pAnimator->isWaitingForAnimationAssets() )
            return PairedAnimationStartResult::ParticipantNotReady;
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
        role._stage = PairedAnimationRoleStage::Waiting;
        return PairedAnimationStartResult::Started;
    }

    PairedAnimationStartResult PairedAnimationSession::validateNotifyTargets( GameObjectManager& manager ) const
    {
        for ( const PairedAnimationNotifyDef& notify : _def->getNotifies() )
        {
            const RoleState& role = _listRole[_def->findRoleIndex( notify._role )];
            if ( role._stage == PairedAnimationRoleStage::Unbound )
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
        return PairedAnimationStartResult::Started;
    }

    PairedAnimationStartResult PairedAnimationSession::bindParticipants( GameObjectManager& manager, const vector<PairedAnimationBinding>& listBinding )
    {
        SW_ASSERT( _state == PairedAnimationSessionState::Idle );
        _listRole.assign( _def->getRoles().size(), RoleState{} );

        // 1) 바인딩 — 모르는 역할, 같은 역할 두 번, 같은 애니메이터 두 번은 부르는 쪽의 결함이다.
        const PairedAnimationStartResult bound = bindRoles( listBinding );
        if ( bound != PairedAnimationStartResult::Started )
            return bound;

        // 2) 참가자 — 해석, 스켈레톤, 클립과 길이, 워핑.
        for ( uint32 roleIndex = 0; roleIndex < static_cast<uint32>( _listRole.size() ); ++roleIndex )
        {
            const PairedAnimationStartResult checked = validateParticipant( manager, roleIndex );
            if ( checked != PairedAnimationStartResult::Started )
                return checked;
        }

        // 3) 공유 알림 — 받을 역할이 묶였으면 그 오브젝트의 표에 항목이 있어야 한다(조용히 건너뛰지 않는다).
        return validateNotifyTargets( manager );
    }

    PairedAnimationStartResult PairedAnimationSession::start( GameObjectManager& manager, const vector<PairedAnimationBinding>& listBinding )
    {
        // 1) ~ 3) 바인딩 · 참가자 · 공유 알림 대상 — 아무것도 바꾸지 않는 검사다.
        const PairedAnimationStartResult bound = bindParticipants( manager, listBinding );
        if ( bound != PairedAnimationStartResult::Started )
            return bound;
        const PairedAnimationDef&             def         = *_def;
        const vector<PairedAnimationRoleDef>& listRoleDef = def.getRoles();

        // 4) 진입 조건 — 처음부터 들어오는 정렬 역할이 목표 가까이 있어야 한다.
        for ( size_t roleIndex = 0; roleIndex < listRoleDef.size(); ++roleIndex )
        {
            const bool bEntersNow = _listRole[roleIndex]._stage == PairedAnimationRoleStage::Waiting && listRoleDef[roleIndex]._startSeconds <= 0.0f;
            if ( bEntersNow && isWithinAlignDistance( manager, static_cast<uint32>( roleIndex ) ) == false )
                return PairedAnimationStartResult::TooFarToAlign;
        }

        // 5) 바꾼다 — 앵커 먼저(정렬 목표가 앵커 루트를 읽는다), 그다음 0 초에 들어오는 역할.
        _state = PairedAnimationSessionState::Playing;
        enterRole( manager, def.getAnchorRoleIndex(), 0.0f );
        for ( size_t roleIndex = 0; roleIndex < listRoleDef.size(); ++roleIndex )
        {
            const bool bEntersNow = _listRole[roleIndex]._stage == PairedAnimationRoleStage::Waiting && listRoleDef[roleIndex]._startSeconds <= 0.0f;
            if ( bEntersNow )
                enterRole( manager, static_cast<uint32>( roleIndex ), 0.0f );
        }
        return PairedAnimationStartResult::Started;
    }

    bool PairedAnimationSession::captureSnapshot( PairedAnimationSessionSnapshot& outSnapshot ) const
    {
        if ( _state != PairedAnimationSessionState::Playing )
            return false;
        outSnapshot                  = PairedAnimationSessionSnapshot{};
        outSnapshot._sessionID       = _sessionID;
        outSnapshot._anchorTime      = _anchorTime;
        outSnapshot._nextNotifyIndex = _nextNotifyIndex;
        outSnapshot._listRole.resize( _listRole.size() );
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            outSnapshot._listRole[roleIndex]._stage           = _listRole[roleIndex]._stage;
            outSnapshot._listRole[roleIndex]._entryAnchorTime = _listRole[roleIndex]._entryAnchorTime;
        }
        return true;
    }

    PairedAnimationStartResult PairedAnimationSession::restore( GameObjectManager& manager, const vector<PairedAnimationBinding>& listBinding, const PairedAnimationSessionSnapshot& snapshot )
    {
        // 1) 바인딩 · 참가자 · 공유 알림 대상 — `start` 와 같은 검사다.
        const PairedAnimationStartResult bound = bindParticipants( manager, listBinding );
        if ( bound != PairedAnimationStartResult::Started )
            return bound;
        const PairedAnimationDef&             def         = *_def;
        const vector<PairedAnimationRoleDef>& listRoleDef = def.getRoles();
        const uint32                          anchorIndex = def.getAnchorRoleIndex();

        // 2) 스냅샷 — 에셋과 맞아야 한다. 맞지 않는 것은 복제 계층의 결함이라 조용히 고치지 않는다.
        const bool bShapeValid       = snapshot._listRole.size() == listRoleDef.size() && snapshot._nextNotifyIndex <= def.getNotifies().size();
        const bool bAnchorStageValid = bShapeValid && snapshot._listRole[anchorIndex]._stage == PairedAnimationRoleStage::Playing;
        const bool bAnchorTimeFinite = MathUtil::isFinite( snapshot._anchorTime );
        const bool bAnchorTimeInside = 0.0f <= snapshot._anchorTime && snapshot._anchorTime < def.getDuration();
        const bool bAnchorTimeValid  = bAnchorTimeFinite && bAnchorTimeInside;
        if ( bAnchorStageValid == false || bAnchorTimeValid == false )
        {
            SW_LOG_ERROR( "Paired animation %# snapshot does not fit the asset (roles %#/%#, notify cursor %#, anchor time %#)", snapshot._sessionID, static_cast<uint32>( snapshot._listRole.size() ),
                          static_cast<uint32>( listRoleDef.size() ), snapshot._nextNotifyIndex, snapshot._anchorTime );
            return PairedAnimationStartResult::InvalidSnapshot;
        }
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            if ( _listRole[roleIndex]._stage == PairedAnimationRoleStage::Unbound )
                continue; // 바인딩이 없는 선택 역할은 스냅샷이 무엇을 말하든 비워 둔다
            const PairedAnimationRoleSnapshot& saved = snapshot._listRole[roleIndex];
            // 들어와 있던 역할의 진입 시각은 지금 앵커 시각 이전이다(앵커는 진입 시각 0 으로 보고 읽지 않는다).
            const bool bEntryFinite = MathUtil::isFinite( saved._entryAnchorTime );
            const bool bEntryInside = 0.0f <= saved._entryAnchorTime && saved._entryAnchorTime <= snapshot._anchorTime + kTimeToleranceSeconds;
            const bool bEntryValid  = roleIndex == anchorIndex || ( bEntryFinite && bEntryInside );
            const bool bStageValid  = saved._stage != PairedAnimationRoleStage::Unbound;
            const bool bPlayingBad  = saved._stage == PairedAnimationRoleStage::Playing && bEntryValid == false;
            if ( bStageValid == false || bPlayingBad )
            {
                SW_LOG_ERROR( "Paired animation %# snapshot has an invalid state for role '%#' (entry anchor time %#)", snapshot._sessionID, listRoleDef[roleIndex]._name.c_str(), saved._entryAnchorTime );
                return PairedAnimationStartResult::InvalidSnapshot;
            }
        }

        // 3) 바꾼다 — 서버의 앵커 시각과 알림 커서를 이어 받고, 역할마다 단계에 맞게 재생을 맞춘다. 트랜스폼은 서버 권위라 건드리지 않는다.
        _state               = PairedAnimationSessionState::Playing;
        _anchorTime          = snapshot._anchorTime;
        _predictedAnchorTime = snapshot._anchorTime;
        _nextNotifyIndex     = snapshot._nextNotifyIndex;
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            RoleState& role = _listRole[roleIndex];
            if ( role._stage == PairedAnimationRoleStage::Unbound || snapshot._listRole[roleIndex]._stage == PairedAnimationRoleStage::Waiting )
                continue; // 기다리는 역할은 다음 갱신이 `start` 를 보고 들여보낸다
            SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, role );
            SW_ASSERT( pAnimator != nullptr ); // `bindParticipants` 가 해석했다
            const bool    bAnchor   = roleIndex == anchorIndex;
            const float32 entryTime = bAnchor ? 0.0f : snapshot._listRole[roleIndex]._entryAnchorTime;
            const float32 roleTime  = MathUtil::max( snapshot._anchorTime - entryTime, 0.0f );
            // 앵커는 끝나지 않았다(앵커 시각 < 타임라인 길이). 다른 역할은 클립이 이미 끝났거나 서버가 내보낸 것이다.
            const bool bClipDone = snapshot._listRole[roleIndex]._stage == PairedAnimationRoleStage::Exited || roleTime + kTimeToleranceSeconds >= role._clipSeconds;
            if ( bAnchor == false && bClipDone )
            {
                role._stage = PairedAnimationRoleStage::Exited;
                (void)pAnimator->play( listRoleDef[roleIndex]._exitState, true, 0.0f ); // 못 찾은 종료 상태는 `play` 가 오류로 남긴다
                continue;
            }
            startRolePlayback( *pAnimator, static_cast<uint32>( roleIndex ), 0.0f );
            role._entryAnchorTime = entryTime;
            // 앵커 클립 길이는 타임라인 길이와 허용치 안에서 같다. 그 안에서 클립 끝을 넘는 시각만 끝으로 맞춘다.
            const bool bSought = pAnimator->seekCurrentTime( MathUtil::min( roleTime, role._clipSeconds ) );
            SW_ASSERT( bSought ); // 시각은 0 이상이고 클립 길이를 넘지 않는다
            (void)bSought;
        }
        return PairedAnimationStartResult::Started;
    }

    void PairedAnimationSession::startRolePlayback( SkeletalAnimatorComponent& animator, uint32 roleIndex, float32 blendSeconds )
    {
        // 모든 역할이 같은 속도로 흘러야 앵커 시각에서 역할 시각을 정확히 안다. 끝나면 되돌린다. 속도를 매 프레임 쓰는 보폭 보정은 그동안 쥐어 둔다.
        LocomotionWarpingComponent* pLocomotion = PairedAnimationSessionInternal::findSibling<LocomotionWarpingComponent>( animator );
        if ( pLocomotion != nullptr )
            pLocomotion->setPlayRateHeld( true );
        RoleState& role        = _listRole[roleIndex];
        role._previousPlayRate = animator.getPlayRate();
        animator.setPlayRate( 1.0f );
        // 클립은 시작 검사에서 읽어 두었다 — 여기서 못 찾으면 `play` 가 오류를 남긴다.
        (void)animator.play( _def->getRoles()[roleIndex]._clip, false, blendSeconds ); // 시작 검사에서 클립을 읽었고, 실패는 `play` 가 로그로 남긴다
        role._stage = PairedAnimationRoleStage::Playing;
    }

    void PairedAnimationSession::enterRole( GameObjectManager& manager, uint32 roleIndex, float32 anchorTime )
    {
        const PairedAnimationRoleDef& roleDef   = _def->getRoles()[roleIndex];
        RoleState&                    role      = _listRole[roleIndex];
        SkeletalAnimatorComponent*    pAnimator = resolveAnimator( manager, role );
        SW_ASSERT( pAnimator != nullptr );
        startRolePlayback( *pAnimator, roleIndex, roleDef._enterBlendSeconds );
        role._entryAnchorTime = anchorTime;

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
        if ( role._stage != PairedAnimationRoleStage::Playing )
        {
            if ( role._stage == PairedAnimationRoleStage::Waiting )
                role._stage = PairedAnimationRoleStage::Exited;
            return;
        }
        closeWarpWindow( manager, roleIndex, false );
        role._stage                          = PairedAnimationRoleStage::Exited;
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
        const bool                       bBound    = role._stage == PairedAnimationRoleStage::Waiting || role._stage == PairedAnimationRoleStage::Playing;
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

    bool PairedAnimationSession::loseVanishedRoles( GameObjectManager& manager )
    {
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            RoleState& role = _listRole[roleIndex];
            if ( role._stage != PairedAnimationRoleStage::Waiting && role._stage != PairedAnimationRoleStage::Playing )
                continue;
            const SkeletalAnimatorComponent* pAnimator = resolveAnimator( manager, role );
            float32                          clipTime  = 0.0f;
            const bool                       bMissing  = pAnimator == nullptr;
            const bool                       bHijacked = bMissing == false && role._stage == PairedAnimationRoleStage::Playing && tryGetClipTime( *pAnimator, role, clipTime ) == false;
            if ( bMissing == false && bHijacked == false )
                continue;
            if ( loseRole( manager, static_cast<uint32>( roleIndex ), bMissing ? "is gone" : "stopped playing its clip" ) )
                return true;
        }
        return false;
    }

    bool PairedAnimationSession::abortOnRoleClockDrift( GameObjectManager& manager, float32 anchorTime )
    {
        const uint32 anchorIndex = _def->getAnchorRoleIndex();
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            const RoleState& role = _listRole[roleIndex];
            if ( role._stage != PairedAnimationRoleStage::Playing || roleIndex == anchorIndex )
                continue;
            float32 clipTime = 0.0f;
            (void)tryGetClipTime( *resolveAnimator( manager, role ), role, clipTime ); // loseVanishedRoles 에서 재생 중임을 확인했다
            const float32 expected = MathUtil::min( anchorTime - role._entryAnchorTime, role._clipSeconds );
            if ( MathUtil::abs( clipTime - expected ) > kTimeToleranceSeconds )
            {
                SW_LOG_ERROR( "Paired animation %# aborts: role '%#' time %# differs from the expected %#", _sessionID, _def->getRoles()[roleIndex]._name.c_str(), clipTime, expected );
                end( manager, PairedAnimationSessionState::Aborted );
                return true;
            }
        }
        return false;
    }

    bool PairedAnimationSession::advanceRoles( GameObjectManager& manager, float32 anchorTime )
    {
        const vector<PairedAnimationRoleDef>& listRoleDef = _def->getRoles();
        const uint32                          anchorIndex = _def->getAnchorRoleIndex();
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            const PairedAnimationRoleDef& roleDef = listRoleDef[roleIndex];
            RoleState&                    role    = _listRole[roleIndex];
            const uint32                  index   = static_cast<uint32>( roleIndex );
            if ( role._stage == PairedAnimationRoleStage::Waiting && roleDef._startSeconds <= anchorTime + kTimeToleranceSeconds )
            {
                if ( isWithinAlignDistance( manager, index ) == false )
                {
                    if ( loseRole( manager, index, "is too far from its alignment target" ) )
                        return true;
                    continue;
                }
                enterRole( manager, index, anchorTime );
                continue;
            }
            if ( role._stage != PairedAnimationRoleStage::Playing || roleIndex == anchorIndex )
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
        return false;
    }

    void PairedAnimationSession::update( GameObjectManager& manager, float32 deltaSeconds )
    {
        if ( _state != PairedAnimationSessionState::Playing )
            return;
        const PairedAnimationDef& def         = *_def;
        const uint32              anchorIndex = def.getAnchorRoleIndex();

        // 1) 잃은 역할 — 애니메이터가 없거나 세션 클립을 재생하지 않는다(게임플레이가 다른 것을 재생했다).
        if ( loseVanishedRoles( manager ) )
            return;

        // 2) 시계 — 앵커 시각이 기준이다. 지난 갱신의 예측과 다르면 누가 속도 · 시각을 바꾼 것이다.
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
        if ( abortOnRoleClockDrift( manager, anchorTime ) )
            return;

        // 4) 이번 프레임 평가가 갈 시각까지의 공유 알림 — 클립 알림(평가 끝)과 같은 프레임에 보인다.
        const float32 predicted = MathUtil::min( anchorTime + deltaSeconds * pAnchor->getPlayRate(), def.getDuration() );
        while ( _nextNotifyIndex < def.getNotifies().size() && def.getNotifies()[_nextNotifyIndex]._time <= predicted )
        {
            fireNotify( manager, _nextNotifyIndex );
            ++_nextNotifyIndex;
        }

        // 5) 늦은 역할 진입, 워프 목표 · 창, 클립이 끝난 역할 나감.
        if ( advanceRoles( manager, anchorTime ) )
            return;

        // 6) 앵커가 끝에 닿았다 — 남은 역할을 모두 내보낸다.
        if ( anchorTime + kTimeToleranceSeconds >= def.getDuration() )
        {
            end( manager, PairedAnimationSessionState::Finished );
            return;
        }
        _predictedAnchorTime = predicted;
    }
} // namespace sw
