#include "pch.h"

#include "GameFramework/AI/SpawnDirector.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

#include <algorithm>

namespace sw
{
    bool SpawnEntryDef::hasAnyTag( const vector<hashed_string>& listTag ) const
    {
        for ( const hashed_string& tag : _listTag )
        {
            for ( const hashed_string& other : listTag )
            {
                if ( tag == other )
                    return true;
            }
        }
        return false;
    }

    SpawnTable::SpawnTable()
        : _catalog{}
        , _listCurvePoint{}
        , _budgetPerMinute{ 1.0f }
        , _maxBudget{ 10.0f }
        , _startBudget{ 0.0f }
        , _bRefundOnDespawn{ SW_FALSE }
    {
    }

    bool SpawnTable::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "SpawnTable", root, sourceName ) && loadRoot( root, sourceName ) > 0;
    }

    bool SpawnTable::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "SpawnTable", root ) && loadRoot( root, sourceName ) > 0;
    }

    uint32 SpawnTable::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _catalog.clear();
        _listCurvePoint.clear();
        _budgetPerMinute  = MathUtil::max( 0.0f, root.getAttributeFloat( "budgetPerMinute", _budgetPerMinute ) );
        _maxBudget        = MathUtil::max( 0.0f, root.getAttributeFloat( "maxBudget", _maxBudget ) );
        _startBudget      = MathUtil::clamp( root.getAttributeFloat( "startBudget", _startBudget ), 0.0f, _maxBudget );
        _bRefundOnDespawn = root.getAttributeBool( "refund", false ) ? SW_TRUE : SW_FALSE;
        for ( XmlNode node = root.findChild( "Entry" ); node; node = node.findNextSibling( "Entry" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            SpawnEntryDef entry;
            entry._id      = hashed_string( pId );
            entry._cost    = MathUtil::max( 0.0f, node.getAttributeFloat( "cost", entry._cost ) );
            entry._weight  = MathUtil::max( 0.0f, node.getAttributeFloat( "weight", entry._weight ) );
            entry._minTime = MathUtil::max( 0.0f, node.getAttributeFloat( "minTime", entry._minTime ) );
            entry._max     = node.getAttributeInt( "max", entry._max );
            GameDataXml::forEachToken( node.getAttributeText( "tags" ), ",; ", [&]( string_view token )
            { entry._listTag.push_back( hashed_string( token ) ); } );
            (void)_catalog.add( entry );
        }
        for ( XmlNode node = root.findChild( "Curve" ); node; node = node.findNextSibling( "Curve" ) )
        {
            SpawnCurvePoint point;
            point._time  = node.getAttributeFloat( "time", point._time );
            point._scale = MathUtil::max( 0.0f, node.getAttributeFloat( "scale", point._scale ) );
            _listCurvePoint.push_back( point );
        }
        std::stable_sort( _listCurvePoint.begin(), _listCurvePoint.end(), []( const SpawnCurvePoint& lhs, const SpawnCurvePoint& rhs )
        { return lhs._time < rhs._time; } );
        return static_cast<uint32>( _catalog.getCount() );
    }

    float32 SpawnTable::computeScale( float32 time ) const
    {
        if ( _listCurvePoint.empty() )
            return 1.0f;
        if ( time <= _listCurvePoint.front()._time )
            return _listCurvePoint.front()._scale;
        for ( size_t index = 1; index < _listCurvePoint.size(); ++index )
        {
            const SpawnCurvePoint& next = _listCurvePoint[index];
            if ( time > next._time )
                continue;
            const SpawnCurvePoint& previous = _listCurvePoint[index - 1];
            const float32          span     = next._time - previous._time;
            const float32          alpha    = span > 0.0f ? ( time - previous._time ) / span : 1.0f;
            return previous._scale + ( next._scale - previous._scale ) * alpha;
        }
        return _listCurvePoint.back()._scale;
    }

    SpawnDirector::SpawnDirector()
        : _listAlive{}
        , _listAliveCount{}
        , _listAllowedTag{}
        , _listEvent{}
        , _pTable{ nullptr }
        , _random{}
        , _budget{ 0.0f }
        , _time{ 0.0f }
        , _pendingIndex{ -1 }
        , _nextSpawnId{ 1 }
        , _bRefundOnDespawn{ SW_FALSE }
    {
    }

    void SpawnDirector::initialize( const SpawnTable* pTable, uint32 seed )
    {
        _pTable = pTable;
        _random.setSeed( seed );
        _listAlive.clear();
        _listEvent.clear();
        _listAliveCount.assign( pTable != nullptr ? pTable->getEntries().size() : 0, 0 );
        _budget           = pTable != nullptr ? pTable->getStartBudget() : 0.0f;
        _time             = 0.0f;
        _pendingIndex     = -1;
        _nextSpawnId      = 1;
        _bRefundOnDespawn = pTable != nullptr && pTable->isRefundOnDespawn() ? SW_TRUE : SW_FALSE;
    }

    void SpawnDirector::setAllowedTags( const vector<hashed_string>& listTag )
    {
        _listAllowedTag = listTag;
        if ( _pendingIndex >= 0 && isEligible( _pendingIndex ) == false )
            _pendingIndex = -1;
    }

    bool SpawnDirector::isEligible( int32 entryIndex ) const
    {
        const SpawnEntryDef& entry       = _pTable->getEntries()[static_cast<size_t>( entryIndex )];
        const bool           bTimeOk     = entry._minTime <= _time;
        const bool           bCountOk    = entry._max < 0 || _listAliveCount[static_cast<size_t>( entryIndex )] < entry._max;
        const bool           bTagOk      = _listAllowedTag.empty() || entry._listTag.empty() || entry.hasAnyTag( _listAllowedTag );
        const bool           bAffordable = entry._cost <= _pTable->getMaxBudget();
        return entry._weight > 0.0f && bTimeOk && bCountOk && bTagOk && bAffordable;
    }

    void SpawnDirector::pickPending()
    {
        _pendingIndex                          = -1;
        const vector<SpawnEntryDef>& listEntry = _pTable->getEntries();
        float32                      total     = 0.0f;
        for ( size_t index = 0; index < listEntry.size(); ++index )
            total += isEligible( static_cast<int32>( index ) ) ? listEntry[index]._weight : 0.0f;
        if ( total <= 0.0f )
            return;
        float32 pick = _random.nextFloat() * total;
        for ( size_t index = 0; index < listEntry.size(); ++index )
        {
            if ( isEligible( static_cast<int32>( index ) ) == false )
                continue;
            _pendingIndex = static_cast<int32>( index ); // 부동소수 끝자락이면 마지막 후보
            if ( pick < listEntry[index]._weight )
                return;
            pick -= listEntry[index]._weight;
        }
    }

    int32 SpawnDirector::update( float32 deltaTime )
    {
        if ( _pTable == nullptr || deltaTime < 0.0f )
            return 0;
        // 구간 가운데의 배율로 쌓는다 — 프레임 길이가 달라도 같은 시간이면 거의 같은 예산.
        const float32 midScale = _pTable->computeScale( _time + deltaTime * 0.5f );
        _time += deltaTime;
        _budget = MathUtil::min( _pTable->getMaxBudget(), _budget + _pTable->getBudgetPerMinute() / 60.0f * midScale * deltaTime );

        int32 spawnCount = 0;
        while ( spawnCount < kMaxSpawnsPerUpdate )
        {
            if ( _pendingIndex >= 0 && isEligible( _pendingIndex ) == false )
                _pendingIndex = -1;
            if ( _pendingIndex < 0 )
                pickPending();
            if ( _pendingIndex < 0 )
                break;
            const SpawnEntryDef& entry = _pTable->getEntries()[static_cast<size_t>( _pendingIndex )];
            if ( _budget < entry._cost )
                break; // 모은다
            _budget -= entry._cost;
            ++_listAliveCount[static_cast<size_t>( _pendingIndex )];
            SpawnAlive alive;
            alive._spawnId    = _nextSpawnId++;
            alive._entryIndex = _pendingIndex;
            _listAlive.push_back( alive );

            SpawnEvent event;
            event._kind    = SpawnEvent::Kind::Spawned;
            event._entryId = entry._id;
            event._time    = _time;
            event._cost    = entry._cost;
            event._spawnId = alive._spawnId;
            _listEvent.push_back( event );
            _pendingIndex = -1;
            ++spawnCount;
        }
        return spawnCount;
    }

    bool SpawnDirector::notifyDespawned( uint32 spawnId )
    {
        for ( size_t index = 0; index < _listAlive.size(); ++index )
        {
            if ( _listAlive[index]._spawnId != spawnId )
                continue;
            const int32          entryIndex = _listAlive[index]._entryIndex;
            const SpawnEntryDef& entry      = _pTable->getEntries()[static_cast<size_t>( entryIndex )];
            _listAlive.erase( _listAlive.begin() + static_cast<ptrdiff_t>( index ) );
            --_listAliveCount[static_cast<size_t>( entryIndex )];
            const float32 refund = _bRefundOnDespawn == SW_TRUE ? entry._cost : 0.0f;
            _budget              = MathUtil::min( _pTable->getMaxBudget(), _budget + refund );

            SpawnEvent event;
            event._kind    = SpawnEvent::Kind::Despawned;
            event._entryId = entry._id;
            event._time    = _time;
            event._cost    = refund;
            event._spawnId = spawnId;
            _listEvent.push_back( event );
            return true;
        }
        return false;
    }

    void SpawnDirector::drainEvents( vector<SpawnEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    int32 SpawnDirector::getAliveCount( const hashed_string& entryId ) const
    {
        const int32 entryIndex = _pTable != nullptr ? _pTable->findEntryIndex( entryId ) : -1;
        return entryIndex >= 0 ? _listAliveCount[static_cast<size_t>( entryIndex )] : 0;
    }

    hashed_string SpawnDirector::getPendingEntry() const
    {
        return _pendingIndex >= 0 ? _pTable->getEntries()[static_cast<size_t>( _pendingIndex )]._id : hashed_string{};
    }
} // namespace sw
