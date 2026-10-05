#include "pch.h"

#include "GameFramework/Base/Combat/TurnOrder.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    TurnOrder::TurnOrder()
        : _listActor{}
        , _listRoundQueue{}
        , _random{}
        , _round{ 0 }
        , _mode{ TurnOrderMode::Rounds }
    {
    }

    void TurnOrder::initialize( TurnOrderMode mode, uint32 seed )
    {
        _mode = mode;
        _random.setSeed( seed );
        _listActor.clear();
        _listRoundQueue.clear();
        _round = 0;
    }

    TurnOrder::Actor* TurnOrder::findActor( int32 actorId )
    {
        for ( Actor& actor : _listActor )
        {
            if ( actor._actorId == actorId )
                return &actor;
        }
        return nullptr;
    }

    void TurnOrder::addActor( int32 actorId, float32 speed )
    {
        if ( findActor( actorId ) != nullptr )
            return;
        Actor actor;
        actor._actorId = actorId;
        actor._speed   = MathUtil::max( 0.01f, speed );
        actor._gauge   = kTimelineThreshold;
        _listActor.push_back( actor );
    }

    void TurnOrder::removeActor( int32 actorId )
    {
        _listActor.erase( std::remove_if( _listActor.begin(), _listActor.end(), [actorId]( const Actor& actor )
        { return actor._actorId == actorId; } ),
                          _listActor.end() );
        _listRoundQueue.erase( std::remove( _listRoundQueue.begin(), _listRoundQueue.end(), actorId ), _listRoundQueue.end() );
    }

    void TurnOrder::setSpeed( int32 actorId, float32 speed )
    {
        Actor* pActor = findActor( actorId );
        if ( pActor != nullptr )
            pActor->_speed = MathUtil::max( 0.01f, speed ); // 게이지는 "남은 양" 이라 속도가 바뀌면 남은 시간이 저절로 바뀐다
    }

    void TurnOrder::setPriority( int32 actorId, int32 priority )
    {
        Actor* pActor = findActor( actorId );
        if ( pActor != nullptr )
            pActor->_priority = priority;
    }

    void TurnOrder::delayActor( int32 actorId, float32 amount )
    {
        Actor* pActor = findActor( actorId );
        if ( pActor != nullptr )
            pActor->_gauge = MathUtil::max( 0.0f, pActor->_gauge + amount * kTimelineThreshold );
    }

    void TurnOrder::restartRound() { _listRoundQueue.clear(); }

    void TurnOrder::makeRoundQueue( vector<int32>& outListQueue, const vector<Actor>& listActor, GameRandom& random ) const
    {
        vector<Actor> listSorted = listActor;
        for ( Actor& actor : listSorted )
            actor._tieBreak = random.nextUint();
        std::sort( listSorted.begin(), listSorted.end(), []( const Actor& lhs, const Actor& rhs )
        {
            if ( lhs._priority != rhs._priority )
                return lhs._priority > rhs._priority;
            if ( lhs._speed != rhs._speed )
                return lhs._speed > rhs._speed;
            return lhs._tieBreak < rhs._tieBreak;
        } );
        outListQueue.clear();
        for ( const Actor& actor : listSorted )
            outListQueue.push_back( actor._actorId );
    }

    int32 TurnOrder::popTimeline( vector<Actor>& listActor )
    {
        // 남은 시간 = 게이지 / 속도. 가장 이른 쪽이 차례, 그만큼 모두 흐른다. 같으면 먼저 넣은 쪽.
        size_t  bestIndex = 0;
        float32 bestTime  = MathUtil::MaxFloat;
        for ( size_t index = 0; index < listActor.size(); ++index )
        {
            const float32 time = listActor[index]._gauge / listActor[index]._speed;
            if ( time < bestTime - 1.0e-5f )
            {
                bestTime  = time;
                bestIndex = index;
            }
        }
        for ( Actor& actor : listActor )
            actor._gauge = MathUtil::max( 0.0f, actor._gauge - bestTime * actor._speed );
        listActor[bestIndex]._gauge = kTimelineThreshold;
        return listActor[bestIndex]._actorId;
    }

    int32 TurnOrder::next()
    {
        if ( _listActor.empty() )
            return -1;
        if ( _mode == TurnOrderMode::Timeline )
            return popTimeline( _listActor );
        if ( _listRoundQueue.empty() )
        {
            makeRoundQueue( _listRoundQueue, _listActor, _random );
            ++_round;
        }
        const int32 actorId = _listRoundQueue.front();
        _listRoundQueue.erase( _listRoundQueue.begin() );
        return actorId;
    }

    void TurnOrder::previewOrder( int32 count, vector<int32>& outListActor ) const
    {
        outListActor.clear();
        if ( _listActor.empty() )
            return;
        if ( _mode == TurnOrderMode::Timeline )
        {
            vector<Actor> listCopy = _listActor;
            for ( int32 index = 0; index < count; ++index )
                outListActor.push_back( popTimeline( listCopy ) );
            return;
        }
        vector<int32> listQueue = _listRoundQueue;
        GameRandom    random    = _random;
        while ( static_cast<int32>( outListActor.size() ) < count )
        {
            if ( listQueue.empty() )
                makeRoundQueue( listQueue, _listActor, random );
            outListActor.push_back( listQueue.front() );
            listQueue.erase( listQueue.begin() );
        }
    }

    void TurnOrder::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listActor.size() );
        for ( const Actor& actor : _listActor )
        {
            outArchive << actor._speed;
            outArchive << actor._gauge;
            outArchive << actor._actorId;
            outArchive << actor._priority;
            outArchive << actor._tieBreak;
        }
        outArchive << static_cast<uint32>( _listRoundQueue.size() );
        for ( const int32 actorId : _listRoundQueue )
        {
            outArchive << actorId;
        }
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _round;
        outArchive << static_cast<uint8>( _mode );
    }

    bool TurnOrder::readState( Archive& archive )
    {
        TurnOrder restored = *this;
        uint32    count    = 0;
        // 배우마다 속도 · 게이지 · id · 우선 · 동률깨기(20)
        if ( StateArchiveUtil::readCount( archive, 20, count ) == false )
            return false;
        restored._listActor.resize( count );
        for ( Actor& actor : restored._listActor )
        {
            archive >> actor._speed;
            archive >> actor._gauge;
            archive >> actor._actorId;
            archive >> actor._priority;
            archive >> actor._tieBreak;
        }
        if ( archive.isError() || StateArchiveUtil::readCount( archive, 4, count ) == false )
            return false;
        restored._listRoundQueue.resize( count );
        for ( int32& actorId : restored._listRoundQueue )
        {
            archive >> actorId;
        }
        uint8      mode        = 0;
        const bool bRandomRead = StateArchiveUtil::readRandom( archive, restored._random );
        archive >> restored._round;
        archive >> mode;
        const bool bValid = bRandomRead && archive.isOk() && mode <= static_cast<uint8>( TurnOrderMode::Timeline );
        if ( bValid == false )
            return false;
        restored._mode = static_cast<TurnOrderMode>( mode );
        *this          = std::move( restored );
        return true;
    }
} // namespace sw
