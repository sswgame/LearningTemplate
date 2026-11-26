#include "pch.h"

#include "Games/Playground/FeatureDemo/GuardDemoComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"

#include <utility>

namespace sw
{
    namespace
    {
        struct GuardDemoComponentInternal
        {
            static constexpr FormatDesc kStateFormat{ "FDGU" };
        };
    } // namespace

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
        , _seenTarget{}
        , _heardTarget{}
        , _lockedTarget{}
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
        const GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( GameObjectHandle::fromValue( objectID ) ) : nullptr;
        return pObject != nullptr ? pObject->getName() : hashed_string{};
    }

    void GuardDemoComponent::updateGame( GameSeconds frameTime )
    {
        const float32            deltaTime = frameTime.count();
        const GameObjectManager* pManager  = getObjectManager();
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
        (void)_lockOn.update( eye, listCandidate, deltaTime ); // 풀렸으면 아래가 빈 이름으로 적는다
        refreshLockedTarget();
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
        io.header( GuardDemoComponentInternal::kStateFormat );
        io.end();
    }
} // namespace sw
