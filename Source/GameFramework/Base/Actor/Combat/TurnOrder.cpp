#include "pch.h"

#include "GameFramework/Base/Actor/Combat/TurnOrder.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    TurnOrder::TurnOrder()
        : _listCombatant{}
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
        _listCombatant.clear();
        _listRoundQueue.clear();
        _round = 0;
    }

    TurnOrder::Combatant* TurnOrder::findCombatant( int32 actorID )
    {
        for ( Combatant& actor : _listCombatant )
        {
            if ( actor._actorID == actorID )
                return &actor;
        }
        return nullptr;
    }

    void TurnOrder::addActor( int32 actorID, float32 speed )
    {
        if ( findCombatant( actorID ) != nullptr )
            return;
        Combatant actor;
        actor._actorID = actorID;
        actor._speed   = MathUtil::max( 0.01f, speed );
        actor._gauge   = kTimelineThreshold;
        _listCombatant.push_back( actor );
    }

    void TurnOrder::removeActor( int32 actorID )
    {
        _listCombatant.erase( std::remove_if( _listCombatant.begin(), _listCombatant.end(), [actorID]( const Combatant& actor )
        { return actor._actorID == actorID; } ),
                              _listCombatant.end() );
        _listRoundQueue.erase( std::remove( _listRoundQueue.begin(), _listRoundQueue.end(), actorID ), _listRoundQueue.end() );
    }

    void TurnOrder::setSpeed( int32 actorID, float32 speed )
    {
        Combatant* pActor = findCombatant( actorID );
        if ( pActor != nullptr )
            pActor->_speed = MathUtil::max( 0.01f, speed ); // 게이지는 "남은 양" 이라 속도가 바뀌면 남은 시간이 저절로 바뀐다
    }

    void TurnOrder::setPriority( int32 actorID, int32 priority )
    {
        Combatant* pActor = findCombatant( actorID );
        if ( pActor != nullptr )
            pActor->_priority = priority;
    }

    void TurnOrder::delayActor( int32 actorID, float32 amount )
    {
        Combatant* pActor = findCombatant( actorID );
        if ( pActor != nullptr )
            pActor->_gauge = MathUtil::max( 0.0f, pActor->_gauge + amount * kTimelineThreshold );
    }

    void TurnOrder::restartRound() { _listRoundQueue.clear(); }

    void TurnOrder::makeRoundQueue( vector<int32>& outListQueue, const vector<Combatant>& listActor, GameRandom& random ) const
    {
        vector<Combatant> listSorted = listActor;
        for ( Combatant& actor : listSorted )
        {
            actor._tieBreak = random.nextUint();
        }
        std::sort( listSorted.begin(), listSorted.end(), []( const Combatant& lhs, const Combatant& rhs )
        {
            if ( lhs._priority != rhs._priority )
                return lhs._priority > rhs._priority;
            if ( lhs._speed != rhs._speed )
                return lhs._speed > rhs._speed;
            return lhs._tieBreak < rhs._tieBreak;
        } );
        outListQueue.clear();
        for ( const Combatant& actor : listSorted )
        {
            outListQueue.push_back( actor._actorID );
        }
    }

    int32 TurnOrder::popTimeline( vector<Combatant>& listActor )
    {
        // 남은 시간 = 게이지 / 속도. 가장 이른 쪽이 차례, 그만큼 모두 흐른다. 같으면 먼저 넣은 쪽.
        size_t  bestIndex = 0;
        float32 bestTime  = MathUtil::kMaxFloat;
        for ( size_t index = 0; index < listActor.size(); ++index )
        {
            const float32 time = listActor[index]._gauge / listActor[index]._speed;
            if ( time < bestTime - 1.0e-5f )
            {
                bestTime  = time;
                bestIndex = index;
            }
        }
        for ( Combatant& actor : listActor )
        {
            actor._gauge = MathUtil::max( 0.0f, actor._gauge - bestTime * actor._speed );
        }
        listActor[bestIndex]._gauge = kTimelineThreshold;
        return listActor[bestIndex]._actorID;
    }

    int32 TurnOrder::next()
    {
        if ( _listCombatant.empty() )
            return -1;
        if ( _mode == TurnOrderMode::Timeline )
            return popTimeline( _listCombatant );
        if ( _listRoundQueue.empty() )
        {
            makeRoundQueue( _listRoundQueue, _listCombatant, _random );
            ++_round;
        }
        const int32 actorID = _listRoundQueue.front();
        _listRoundQueue.erase( _listRoundQueue.begin() );
        return actorID;
    }

    void TurnOrder::previewOrder( int32 count, vector<int32>& outListActor ) const
    {
        outListActor.clear();
        if ( _listCombatant.empty() )
            return;
        if ( _mode == TurnOrderMode::Timeline )
        {
            vector<Combatant> listCopy = _listCombatant;
            for ( int32 index = 0; index < count; ++index )
            {
                outListActor.push_back( popTimeline( listCopy ) );
            }
            return;
        }
        vector<int32> listQueue = _listRoundQueue;
        GameRandom    random    = _random;
        while ( static_cast<int32>( outListActor.size() ) < count )
        {
            if ( listQueue.empty() )
                makeRoundQueue( listQueue, _listCombatant, random );
            outListActor.push_back( listQueue.front() );
            listQueue.erase( listQueue.begin() );
        }
    }

    void TurnOrder::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listCombatant.size() );
        for ( const Combatant& actor : _listCombatant )
        {
            outArchive << actor._speed;
            outArchive << actor._gauge;
            outArchive << actor._actorID;
            outArchive << actor._priority;
            outArchive << actor._tieBreak;
        }
        outArchive << static_cast<uint32>( _listRoundQueue.size() );
        for ( const int32 actorID : _listRoundQueue )
        {
            outArchive << actorID;
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
        restored._listCombatant.resize( count );
        for ( Combatant& actor : restored._listCombatant )
        {
            archive >> actor._speed;
            archive >> actor._gauge;
            archive >> actor._actorID;
            archive >> actor._priority;
            archive >> actor._tieBreak;
        }
        if ( archive.isError() || StateArchiveUtil::readCount( archive, 4, count ) == false )
            return false;
        restored._listRoundQueue.resize( count );
        for ( int32& actorID : restored._listRoundQueue )
        {
            archive >> actorID;
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
