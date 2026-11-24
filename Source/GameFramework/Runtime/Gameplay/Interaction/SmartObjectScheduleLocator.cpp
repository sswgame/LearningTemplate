#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectScheduleLocator.h"

#include "Core/Algorithm/CompareUtil.h"
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneObjectReference.h"

#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectComponent.h"

namespace sw
{
    SmartObjectScheduleLocator::SmartObjectScheduleLocator()
        : _pScene{ nullptr }
        , _pManager{ nullptr }
        , _defaultArea{}
        , _listReservation{}
        , _nextID{ 1 }
    {
    }

    void SmartObjectScheduleLocator::initialize( Scene* pScene )
    {
        _pScene   = pScene;
        _pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        clear();
    }

    void SmartObjectScheduleLocator::clear()
    {
        _listReservation.clear();
        _nextID = 1;
    }

    void SmartObjectScheduleLocator::collectCandidates( const hashed_string& kind, vector<Candidate>& outListCandidate ) const
    {
        outListCandidate.clear();
        if ( _pManager == nullptr )
            return;
        // 씬을 훑지 않는다 — 스마트 오브젝트는 붙을 때 컴포넌트 레지스트리에 등록된다.
        for ( SmartObjectComponent* pSmart : _pManager->getComponentRegistry().getAll<SmartObjectComponent>() )
        {
            const GameObject* pOwner           = pSmart->getOwner();
            const bool        bSkipSmartObject = pOwner == nullptr || pSmart->isPendingDestroy() || pSmart->getDefinitionID() != kind;
            if ( bSkipSmartObject )
                continue;
            const int32 slotCount = pSmart->getSlotCount();
            for ( int32 slot = 0; slot < slotCount; ++slot )
            {
                Candidate candidate;
                float32   yaw{ 0.0f };
                if ( pSmart->computeSlotTransform( slot, candidate._position, yaw ) == false )
                    continue;
                candidate._position = _pManager->getWorldOrigin().toAbsolute( candidate._position ); // 일정은 절대 자리로 산다
                candidate._objectID = pOwner->getObjectID();
                candidate._slot     = slot;
                outListCandidate.push_back( candidate );
            }
        }
        // 순회 순서에 기대지 않는다 — (오브젝트 id, 슬롯) 순이라 같은 씬이면 늘 같은 후보 순서다.
        VectorUtil::sort( outListCandidate, Ascending<&Candidate::_objectID, &Candidate::_slot>{} );
    }

    bool SmartObjectScheduleLocator::computeReservationPosition( const Reservation& reservation, float3& outPosition ) const
    {
        if ( _pManager == nullptr )
            return false;
        const GameObject* pObject = _pManager->findGameObjectByID( reservation._objectID );
        if ( pObject == nullptr )
            return false;
        const SmartObjectComponent* pSmart = pObject->getComponent<SmartObjectComponent>();
        float32                     yaw{ 0.0f };
        if ( pSmart == nullptr || pSmart->computeSlotTransform( reservation._slot, outPosition, yaw ) == false )
            return false;
        outPosition = _pManager->getWorldOrigin().toAbsolute( outPosition );
        return true;
    }

    bool SmartObjectScheduleLocator::isTakenByOther( const hashed_string& npc, uint64 objectID, int32 slot, int32 day, int32 startMinute, int32 endMinute ) const
    {
        for ( const Reservation& reservation : _listReservation )
        {
            const bool bOverlaps        = reservation._startMinute < endMinute && startMinute < reservation._endMinute;
            const bool bSameSlotOverlap = reservation._npc != npc && reservation._objectID == objectID && reservation._slot == slot && reservation._day == day && bOverlaps;
            if ( bSameSlotOverlap )
                return true;
        }
        return false;
    }

    bool SmartObjectScheduleLocator::reserve( const ScheduleReserveRequest& request, ScheduleLocation& outLocation, uint32& outReservationID )
    {
        if ( _pManager == nullptr )
            return false;
        const hashed_string area = request._area.empty() ? _defaultArea : request._area;

        // 멱등 — 같은 NPC · 종류 · 날의 겹치는 창이면 이미 가진 예약이다. 오브젝트가 사라졌으면 그 예약은 버리고 새로 고른다.
        // 창은 요청까지 넓힌다(옛 창만 들면 나머지 시간을 남이 겹쳐 잡는다).
        for ( size_t reservationIndex = 0; reservationIndex < _listReservation.size(); ++reservationIndex )
        {
            Reservation& reservation = _listReservation[reservationIndex];
            const bool   bSameOwner  = reservation._npc == request._npc && reservation._kind == request._objectKind && reservation._day == request._day;
            const bool   bOverlaps   = reservation._startMinute < request._endMinute && request._startMinute < reservation._endMinute;
            if ( bSameOwner == false || bOverlaps == false )
                continue;
            float3 position{};
            if ( computeReservationPosition( reservation, position ) )
            {
                const int32 startMinute = MathUtil::min( reservation._startMinute, request._startMinute );
                const int32 endMinute   = MathUtil::max( reservation._endMinute, request._endMinute );
                if ( isTakenByOther( request._npc, reservation._objectID, reservation._slot, request._day, startMinute, endMinute ) )
                    continue; // 나머지 시간은 남의 것이다 — 새로 고른다
                reservation._startMinute = startMinute;
                reservation._endMinute   = endMinute;
                outLocation              = ScheduleLocation{ position, area };
                outReservationID         = reservation._id;
                return true;
            }
            _listReservation.erase( _listReservation.begin() + static_cast<ptrdiff_t>( reservationIndex ) );
            break;
        }

        vector<Candidate> listCandidate;
        collectCandidates( request._objectKind, listCandidate );
        int32   bestIndex    = constant::invalid_index::kInt32;
        float32 bestDistance = 0.0f;
        for ( int32 candidateIndex = 0; candidateIndex < static_cast<int32>( listCandidate.size() ); ++candidateIndex )
        {
            const Candidate& candidate = listCandidate[static_cast<size_t>( candidateIndex )];
            if ( isTakenByOther( request._npc, candidate._objectID, candidate._slot, request._day, request._startMinute, request._endMinute ) )
                continue;
            const float32 distance = float3::getDistance( candidate._position, request._near );
            if ( bestIndex < 0 || distance < bestDistance )
            {
                bestIndex    = candidateIndex;
                bestDistance = distance;
            }
        }
        if ( bestIndex < 0 )
            return false;

        const Candidate& chosen = listCandidate[static_cast<size_t>( bestIndex )];
        Reservation      reservation;
        reservation._npc         = request._npc;
        reservation._kind        = request._objectKind;
        reservation._id          = _nextID++;
        reservation._objectID    = chosen._objectID;
        reservation._slot        = chosen._slot;
        reservation._day         = request._day;
        reservation._startMinute = request._startMinute;
        reservation._endMinute   = request._endMinute;
        _listReservation.push_back( reservation );
        outLocation      = ScheduleLocation{ chosen._position, area };
        outReservationID = reservation._id;
        return true;
    }

    void SmartObjectScheduleLocator::release( uint32 reservationID )
    {
        for ( size_t reservationIndex = 0; reservationIndex < _listReservation.size(); ++reservationIndex )
        {
            if ( _listReservation[reservationIndex]._id == reservationID )
            {
                _listReservation.erase( _listReservation.begin() + static_cast<ptrdiff_t>( reservationIndex ) );
                return;
            }
        }
    }

    bool SmartObjectScheduleLocator::claimReserved( uint32 reservationID, const GameObject& npc )
    {
        if ( _pManager == nullptr )
            return false;
        for ( const Reservation& reservation : _listReservation )
        {
            if ( reservation._id != reservationID )
                continue;
            GameObject*           pObject = _pManager->findGameObjectByID( reservation._objectID );
            SmartObjectComponent* pSmart  = pObject != nullptr ? pObject->getComponent<SmartObjectComponent>() : nullptr;
            return pSmart != nullptr && pSmart->claimSlot( npc, reservation._slot );
        }
        return false;
    }

    bool SmartObjectScheduleLocator::releaseClaim( uint32 reservationID, const GameObject& npc )
    {
        if ( _pManager == nullptr )
            return false;
        for ( const Reservation& reservation : _listReservation )
        {
            if ( reservation._id != reservationID )
                continue;
            GameObject*           pObject = _pManager->findGameObjectByID( reservation._objectID );
            SmartObjectComponent* pSmart  = pObject != nullptr ? pObject->getComponent<SmartObjectComponent>() : nullptr;
            return pSmart != nullptr && pSmart->releaseSlot( npc );
        }
        return false;
    }

    void SmartObjectScheduleLocator::fillState( vector<ScheduleReservationSaveState>& outListReservation ) const
    {
        outListReservation.clear();
        if ( _pScene == nullptr )
            return;
        for ( const Reservation& reservation : _listReservation )
        {
            float3 position{};
            if ( computeReservationPosition( reservation, position ) == false )
                continue; // 사라진 오브젝트 — 복원도 버린다
            ScheduleReservationSaveState state;
            if ( SceneObjectReference::makeReference( *_pScene, reservation._objectID, state._object ) == false )
            {
                SW_LOG_ERROR_ID( "SmartObject.ReservationHasNoIdentity", "The smart object reserved by '%#' has no saved identity in scene '%#' - the reservations are not saved",
                                 reservation._npc.c_str(), _pScene->getSourcePath() );
                outListReservation.clear();
                return;
            }
            state._npc         = reservation._npc;
            state._kind        = reservation._kind;
            state._id          = reservation._id;
            state._slot        = reservation._slot;
            state._day         = reservation._day;
            state._startMinute = reservation._startMinute;
            state._endMinute   = reservation._endMinute;
            outListReservation.push_back( std::move( state ) );
        }
    }

    void SmartObjectScheduleLocator::restoreState( const vector<ScheduleReservationSaveState>& listReservation )
    {
        clear();
        if ( _pScene == nullptr )
            return;
        for ( const ScheduleReservationSaveState& state : listReservation )
        {
            Reservation reservation;
            if ( SceneObjectReference::resolveInScene( *_pScene, state._object, reservation._objectID ) != SceneObjectResolveResult::Resolved )
                continue; // 지운 오브젝트 — 다시 세운 계획이 새 자리를 고른다
            reservation._npc         = state._npc;
            reservation._kind        = state._kind;
            reservation._id          = state._id;
            reservation._slot        = state._slot;
            reservation._day         = state._day;
            reservation._startMinute = state._startMinute;
            reservation._endMinute   = state._endMinute;
            float3 position{};
            if ( computeReservationPosition( reservation, position ) == false )
                continue;
            _listReservation.push_back( reservation );
            _nextID = MathUtil::max( _nextID, state._id + 1 );
        }
    }
} // namespace sw
