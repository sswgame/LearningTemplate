#include "pch.h"

#include "GameFramework/AI/Schedule/ScheduleLocator.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/AI/Schedule/ScheduleSaveState.h"

namespace sw
{
    ScheduleSpotLocator::ScheduleSpotLocator()
        : _pCatalog{ nullptr }
        , _listReservation{}
        , _nextId{ 1 }
    {
    }

    void ScheduleSpotLocator::initialize( const ScheduleCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        clear();
    }

    void ScheduleSpotLocator::clear()
    {
        _listReservation.clear();
        _nextId = 1;
    }

    bool ScheduleSpotLocator::reserve( const ScheduleReserveRequest& request, ScheduleLocation& outLocation, uint32& outReservationId )
    {
        if ( _pCatalog == nullptr )
            return false;
        const vector<ScheduleSpotDef>& listSpot = _pCatalog->getSpots();
        // 멱등 — 같은 NPC · 종류 · 날의 겹치는 창이면 이미 가진 예약이다.
        for ( const Reservation& reservation : _listReservation )
        {
            const bool bSameOwner = reservation._npc == request._npc && reservation._kind == request._objectKind && reservation._day == request._day;
            const bool bOverlaps  = reservation._startMinute < request._endMinute && request._startMinute < reservation._endMinute;
            if ( bSameOwner && bOverlaps )
            {
                const ScheduleSpotDef& spot = listSpot[static_cast<size_t>( reservation._spotIndex )];
                outLocation                 = ScheduleLocation{ spot._position, spot._area };
                outReservationId            = reservation._id;
                return true;
            }
        }

        int32   bestIndex    = -1;
        bool    bBestInArea  = false;
        float32 bestDistance = 0.0f;
        for ( int32 spotIndex = 0; spotIndex < static_cast<int32>( listSpot.size() ); ++spotIndex )
        {
            const ScheduleSpotDef& spot = listSpot[static_cast<size_t>( spotIndex )];
            if ( spot._kind != request._objectKind || countOverlapping( spotIndex, request._day, request._startMinute, request._endMinute ) >= spot._capacity )
                continue;
            const bool    bInArea  = request._area.empty() == false && spot._area == request._area;
            const float32 distance = float3::getDistance( spot._position, request._near );
            const bool    bBetter  = bestIndex < 0 || ( bInArea && bBestInArea == false ) || ( bInArea == bBestInArea && distance < bestDistance );
            if ( bBetter )
            {
                bestIndex    = spotIndex;
                bBestInArea  = bInArea;
                bestDistance = distance;
            }
        }
        if ( bestIndex < 0 )
            return false;

        Reservation reservation;
        reservation._npc         = request._npc;
        reservation._kind        = request._objectKind;
        reservation._id          = _nextId++;
        reservation._spotIndex   = bestIndex;
        reservation._day         = request._day;
        reservation._startMinute = request._startMinute;
        reservation._endMinute   = request._endMinute;
        _listReservation.push_back( reservation );
        const ScheduleSpotDef& spot = listSpot[static_cast<size_t>( bestIndex )];
        outLocation                 = ScheduleLocation{ spot._position, spot._area };
        outReservationId            = reservation._id;
        return true;
    }

    void ScheduleSpotLocator::release( uint32 reservationId )
    {
        for ( size_t reservationIndex = 0; reservationIndex < _listReservation.size(); ++reservationIndex )
        {
            if ( _listReservation[reservationIndex]._id == reservationId )
            {
                _listReservation.erase( _listReservation.begin() + static_cast<ptrdiff_t>( reservationIndex ) );
                return;
            }
        }
    }

    void ScheduleSpotLocator::fillState( vector<ScheduleReservationSaveState>& outListReservation ) const
    {
        outListReservation.clear();
        if ( _pCatalog == nullptr )
            return;
        for ( const Reservation& reservation : _listReservation )
        {
            ScheduleReservationSaveState state;
            state._spot        = _pCatalog->getSpots()[static_cast<size_t>( reservation._spotIndex )]._id;
            state._npc         = reservation._npc;
            state._kind        = reservation._kind;
            state._id          = reservation._id;
            state._day         = reservation._day;
            state._startMinute = reservation._startMinute;
            state._endMinute   = reservation._endMinute;
            outListReservation.push_back( state );
        }
    }

    void ScheduleSpotLocator::restoreState( const vector<ScheduleReservationSaveState>& listReservation )
    {
        clear();
        if ( _pCatalog == nullptr )
            return;
        const vector<ScheduleSpotDef>& listSpot = _pCatalog->getSpots();
        for ( const ScheduleReservationSaveState& state : listReservation )
        {
            int32 spotIndex = -1;
            for ( int32 candidateIndex = 0; candidateIndex < static_cast<int32>( listSpot.size() ) && spotIndex < 0; ++candidateIndex )
            {
                if ( listSpot[static_cast<size_t>( candidateIndex )]._id == state._spot )
                    spotIndex = candidateIndex;
            }
            if ( spotIndex < 0 )
                continue; // 데이터에서 지운 자리 — 다시 세운 계획이 새 자리를 고른다
            Reservation reservation;
            reservation._npc         = state._npc;
            reservation._kind        = state._kind;
            reservation._id          = state._id;
            reservation._spotIndex   = spotIndex;
            reservation._day         = state._day;
            reservation._startMinute = state._startMinute;
            reservation._endMinute   = state._endMinute;
            _listReservation.push_back( reservation );
            _nextId = MathUtil::max( _nextId, state._id + 1 );
        }
    }

    int32 ScheduleSpotLocator::countOverlapping( int32 spotIndex, int32 day, int32 startMinute, int32 endMinute ) const
    {
        int32 count = 0;
        for ( const Reservation& reservation : _listReservation )
        {
            const bool bOverlaps = reservation._startMinute < endMinute && startMinute < reservation._endMinute;
            if ( reservation._spotIndex == spotIndex && reservation._day == day && bOverlaps )
                ++count;
        }
        return count;
    }
} // namespace sw
