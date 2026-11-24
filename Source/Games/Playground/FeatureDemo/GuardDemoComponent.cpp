#include "pch.h"

#include "Games/Playground/FeatureDemo/GuardDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"

#include "Games/Playground/FeatureDemo/FeatureDemoCommandUtil.h"

#include <utility>

namespace sw
{
    namespace
    {
        struct GuardDemoComponentInternal
        {
            static constexpr uint32      kStateTag     = FourCCUtil::make( "FDGU" );
            static constexpr uint32      kStateVersion = 1;
            static constexpr const utf8* kNone         = "none";

            static bool runLock( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                GuardDemoComponent* pGuard = FeatureDemoCommandUtil::findStarted<GuardDemoComponent>( listArgument[0], outReply );
                if ( pGuard == nullptr )
                    return false;
                (void)pGuard->lockBest(); // 잡을 것이 없으면 풀린 채다 — 답과 상태 칸이 알린다
                FeatureDemoCommandUtil::reply( outReply, "locked %#", pGuard->getLockedTarget().c_str() );
                return true;
            }

            static bool runCycle( const vector<string>& listArgument, string& outReply )
            {
                const bool bRight = listArgument.size() == 2 && StringUtil::equals( listArgument[1], "right", true );
                const bool bLeft  = listArgument.size() == 2 && StringUtil::equals( listArgument[1], "left", true );
                if ( bRight == false && bLeft == false )
                    return false;
                GuardDemoComponent* pGuard = FeatureDemoCommandUtil::findStarted<GuardDemoComponent>( listArgument[0], outReply );
                if ( pGuard == nullptr )
                    return false;
                pGuard->cycleLock( bRight ? 1 : -1 );
                FeatureDemoCommandUtil::reply( outReply, "locked %#", pGuard->getLockedTarget().c_str() );
                return true;
            }

            static bool runRelease( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                GuardDemoComponent* pGuard = FeatureDemoCommandUtil::findStarted<GuardDemoComponent>( listArgument[0], outReply );
                if ( pGuard == nullptr )
                    return false;
                pGuard->releaseLock();
                outReply = "released";
                return true;
            }

            /** @brief `noise.report <object> <radius>` — 자극 원천이 낸 소리 반경(m)을 겁니다. 0 이면 거둡니다. */
            static bool runNoise( const vector<string>& listArgument, string& outReply )
            {
                float32 radius = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[1], radius ) == false )
                    return false;
                GameObject*                pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                AIStimulusSourceComponent* pSource = pObject != nullptr ? pObject->getComponent<AIStimulusSourceComponent>() : nullptr;
                if ( pSource == nullptr )
                {
                    FeatureDemoCommandUtil::reply( outReply, "no object named '%#' with an AIStimulusSourceComponent", listArgument[0] );
                    return false;
                }
                pSource->reportNoise( radius );
                FeatureDemoCommandUtil::reply( outReply, "%# makes noise of radius %#", listArgument[0], radius );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( GuardLock, "guard.lock", "guard.lock <guard object>", "Lock on to the best target the guard sees", &GuardDemoComponentInternal::runLock );
    SW_DEV_COMMAND( GuardCycle, "guard.cycle", "guard.cycle <guard object> <left|right>", "Move the guard's lock to the next target on that side", &GuardDemoComponentInternal::runCycle );
    SW_DEV_COMMAND( GuardRelease, "guard.release", "guard.release <guard object>", "Release the guard's lock", &GuardDemoComponentInternal::runRelease );
    SW_DEV_COMMAND( NoiseReport, "noise.report", "noise.report <object> <radius>", "Set the noise radius of a stimulus source (0 clears it)", &GuardDemoComponentInternal::runNoise );

    GuardDemoComponent::GuardDemoComponent()
        : _team{ 1 }
        , _sightRange{ 12.0f }
        , _loseSightRange{ 14.0f }
        , _sightHalfAngle{ Radians::fromDegrees( 60.0f ) }
        , _memoryDuration{ 2.0f }
        , _wallName{}
        , _gridOrigin{ -16.0f, 0.0f, -16.0f }
        , _gridSize{ 32 }
        , _gridCellSize{ 1.0f }
        , _lockMaxDistance{ 9.0f }
        , _lockBreakDistance{ 10.0f }
        , _lockLostSightGrace{ 0.5f }
        , _seenTarget{ GuardDemoComponentInternal::kNone }
        , _heardTarget{ GuardDemoComponentInternal::kNone }
        , _lockedTarget{ GuardDemoComponentInternal::kNone }
        , _rememberedCount{ 0 }
        , _perception{}
        , _lockOn{}
        , _sightGrid{}
    {
    }

    GuardDemoComponent::~GuardDemoComponent() = default;

    bool GuardDemoComponent::startGame()
    {
        if ( _sightGrid.initialize( _gridSize, _gridSize, _gridCellSize, _gridOrigin ) == false )
            return false; // 격자가 까닭을 남겼다
        vector<const GameObject*> listWall;
        std::as_const( *getObjectManager() ).findGameObjectsByBaseName( _wallName.view(), listWall );
        for ( const GameObject* pWall : listWall )
        {
            const SceneComponent* pScene = pWall->getPrimarySceneComponent();
            if ( pScene == nullptr )
                continue;
            const int2 cell = _sightGrid.computeCell( getObjectManager()->getWorldOrigin().toAbsolute( pScene->getWorldPosition() ) ); // 격자는 절대 자리다
            if ( _sightGrid.isInside( cell ) )
                _sightGrid.setBlocked( cell._x, cell._y, true );
        }

        AIPerceptionSettings sense;
        sense._sightRange     = _sightRange;
        sense._loseSightRange = _loseSightRange;
        sense._sightHalfAngle = _sightHalfAngle.radians();
        sense._memoryDuration = _memoryDuration;
        _perception.setSettings( sense );
        _perception.clearAll();

        LockOnSettings lockOn;
        lockOn._maxDistance    = _lockMaxDistance;
        lockOn._breakDistance  = _lockBreakDistance;
        lockOn._lostSightGrace = _lockLostSightGrace;
        _lockOn.setSettings( lockOn );
        _lockOn.release();
        return true;
    }

    bool GuardDemoComponent::readEye( float3& outEye, float3& outForward ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return false;
        outEye     = pScene->getWorldPosition();
        outForward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pScene->getWorldMatrix() );
        return true;
    }

    void GuardDemoComponent::gatherLockCandidates( vector<LockOnCandidate>& outListCandidate ) const
    {
        outListCandidate.clear();
        for ( const AIPerceivedTarget& target : _perception.getTargets() )
        {
            LockOnCandidate candidate;
            candidate._position = target._lastKnownPosition;
            candidate._id       = target._id;
            candidate._bVisible = target._bSeen;
            outListCandidate.push_back( candidate );
        }
    }

    hashed_string GuardDemoComponent::findName( uint64 objectID ) const
    {
        const GameObjectManager* pManager = getObjectManager();
        const GameObject*        pObject  = objectID != 0 && pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
        return pObject != nullptr ? pObject->getName() : hashed_string( GuardDemoComponentInternal::kNone );
    }

    void GuardDemoComponent::updateGame( float32 deltaTime )
    {
        const GameObjectManager* pManager = getObjectManager();
        float3                   eye{};
        float3                   forward{};
        if ( pManager == nullptr || readEye( eye, forward ) == false )
            return;
        _perception.sense( *pManager, static_cast<uint8>( _team ), eye, forward, &_sightGrid, deltaTime );

        const AIPerceivedTarget* pSeen = _perception.findNearestSeen( eye );
        _seenTarget                    = findName( pSeen != nullptr ? pSeen->_id : 0 );
        _heardTarget                   = findName( 0 );
        for ( const AIPerceivedTarget& target : _perception.getTargets() )
        {
            if ( target._bHeard == SW_TRUE )
                _heardTarget = findName( target._id );
        }
        _rememberedCount = static_cast<int32>( _perception.getTargets().size() );

        vector<LockOnCandidate> listCandidate;
        gatherLockCandidates( listCandidate );
        (void)_lockOn.update( eye, listCandidate, deltaTime ); // 풀렸으면 아래가 none 으로 적는다
        refreshLockedTarget();
    }

    void GuardDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)manager;
        (void)bRespawnViews;
    }

    bool GuardDemoComponent::lockBest()
    {
        float3 eye{};
        float3 forward{};
        if ( readEye( eye, forward ) == false )
            return false;
        vector<LockOnCandidate> listCandidate;
        gatherLockCandidates( listCandidate );
        const bool bLocked = _lockOn.pickBest( eye, forward, listCandidate ) != 0;
        refreshLockedTarget();
        return bLocked;
    }

    void GuardDemoComponent::cycleLock( int32 direction )
    {
        float3 eye{};
        float3 forward{};
        if ( readEye( eye, forward ) == false )
            return;
        vector<LockOnCandidate> listCandidate;
        gatherLockCandidates( listCandidate );
        (void)_lockOn.cycle( eye, forward, listCandidate, direction ); // 그쪽에 없으면 그대로다
        refreshLockedTarget();
    }

    void GuardDemoComponent::releaseLock()
    {
        _lockOn.release();
        refreshLockedTarget();
    }

    void GuardDemoComponent::serializeGameState( StateIO& io )
    {
        // 감각의 기억 · 록온 대상은 런타임 id 라 저장하지 않는다 — 불러온 판이 다음 프레임에 다시 본다.
        io.header( GuardDemoComponentInternal::kStateTag, GuardDemoComponentInternal::kStateVersion );
        io.end();
    }
} // namespace sw
