#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/TurnOrder.h"

#include "Core/Container/CompareUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

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
        return VectorUtil::findBy<&Combatant::_actorID>( _listCombatant, actorID );
    }

    bool TurnOrder::hasActor( int32 actorID ) const
    {
        return VectorUtil::containsBy<&Combatant::_actorID>( _listCombatant, actorID );
    }

    void TurnOrder::addActor( int32 actorID, float32 speed )
    {
        if ( findCombatant( actorID ) != nullptr )
            return;
        Combatant actor;
        actor._actorID = actorID;
        actor._speed   = MathUtil::max( kMinSpeed, speed );
        actor._gauge   = kTimelineThreshold;
        _listCombatant.push_back( actor );
    }

    void TurnOrder::removeActor( int32 actorID )
    {
        VectorUtil::removeIf( _listCombatant, [actorID]( const Combatant& actor )
        { return actor._actorID == actorID; } );
        VectorUtil::removeIf( _listRoundQueue, [actorID]( int32 queuedID )
        { return queuedID == actorID; } );
    }

    void TurnOrder::setSpeed( int32 actorID, float32 speed )
    {
        Combatant* pActor = findCombatant( actorID );
        if ( pActor != nullptr )
            pActor->_speed = MathUtil::max( kMinSpeed, speed ); // 게이지는 "남은 양" 이라 속도가 바뀌면 남은 시간이 저절로 바뀐다
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

    void TurnOrder::restartRound()
    {
        _listRoundQueue.clear();
    }

    void TurnOrder::makeRoundQueue( vector<int32>& outListQueue, const vector<Combatant>& listActor, GameRandom& random ) const
    {
        vector<Combatant> listSorted = listActor;
        for ( Combatant& actor : listSorted )
        {
            actor._tieBreak = random.nextUint();
        }
        VectorUtil::sort( listSorted, Lexicographic<Descending<&Combatant::_priority, &Combatant::_speed>, Ascending<&Combatant::_tieBreak>>{} );
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
            return constant::invalid_index::kInt32;
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

    void TurnOrder::serializeState( StateIO& io )
    {
        // 배우마다 속도 · 게이지 · id · 우선 · 동률깨기(20)
        io.list( _listCombatant, 20, [&]( Combatant& actor )
        {
            io.value( actor._speed );
            io.value( actor._gauge );
            io.value( actor._actorID );
            io.value( actor._priority );
            io.value( actor._tieBreak );
            io.check( kMinSpeed <= actor._speed && 0.0f <= actor._gauge );
        } );
        if ( io.isReading() )
        {
            io.check( VectorUtil::findDuplicateBy<&Combatant::_actorID>( _listCombatant ) == nullptr );
        }
        io.list( _listRoundQueue, 4, [&]( int32& actorID )
        {
            io.value( actorID );
            if ( io.isReading() )
            {
                io.check( hasActor( actorID ) );
            }
        } );
        io.value( _random );
        io.value( _round );
        io.enumValue( _mode, TurnOrderMode::Timeline );
        io.check( 0 <= _round );
    }

} // namespace sw
