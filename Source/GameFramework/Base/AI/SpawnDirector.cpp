#include "pch.h"

#include "GameFramework/Base/AI/SpawnDirector.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

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
        , _curve{}
        , _budgetPerMinute{ 1.0f }
        , _maxBudget{ 10.0f }
        , _startBudget{ 0.0f }
        , _bRefundOnDespawn{ SW_FALSE }
    {
    }

    uint32 SpawnTable::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _catalog.clear();
        _curve.clear();
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
        (void)_curve.readPoints( root, "Curve", "scale", 0.0f ); // 점 수만 돌려준다 — 점이 없으면 곡선이 대체값을 쓴다
        return static_cast<uint32>( _catalog.getCount() );
    }

    SpawnDirector::SpawnDirector()
        : _listAlive{}
        , _listAliveCount{}
        , _listAllowedTag{}
        , _eventBuffer{}
        , _pTable{ nullptr }
        , _random{}
        , _budget{ 0.0f }
        , _budgetScale{ 1.0f }
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
        _eventBuffer.clear();
        _listAliveCount.assign( pTable != nullptr ? pTable->getEntries().size() : 0, 0 );
        _budget           = pTable != nullptr ? pTable->getStartBudget() : 0.0f;
        _budgetScale      = 1.0f;
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
        const float32 midScale = _pTable->computeScale( _time + deltaTime * 0.5f ) * _budgetScale;
        _time += deltaTime;
        if ( _budgetScale <= 0.0f )
            return 0; // 쉬는 중 — 쌓지도 쓰지도 않는다
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
            _eventBuffer.push( event );
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
            _eventBuffer.push( event );
            return true;
        }
        return false;
    }

    void SpawnDirector::drainEvents( vector<SpawnEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
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

    void SpawnDirector::collectAliveSpawnIds( vector<uint32>& outListSpawnId ) const
    {
        outListSpawnId.clear();
        outListSpawnId.reserve( _listAlive.size() );
        for ( const SpawnAlive& alive : _listAlive )
            outListSpawnId.push_back( alive._spawnId );
    }

    void SpawnDirector::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listAlive.size() );
        for ( const SpawnAlive& alive : _listAlive )
        {
            outArchive << alive._spawnId;
            outArchive << alive._entryIndex;
        }
        outArchive << static_cast<uint32>( _listAliveCount.size() );
        for ( const int32 count : _listAliveCount )
        {
            outArchive << count;
        }
        outArchive << static_cast<uint32>( _listAllowedTag.size() );
        for ( const hashed_string& tag : _listAllowedTag )
        {
            StateArchiveUtil::writeName( outArchive, tag );
        }
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _budget;
        outArchive << _budgetScale;
        outArchive << _time;
        outArchive << _pendingIndex;
        outArchive << _nextSpawnId;
        outArchive << _bRefundOnDespawn;
    }

    bool SpawnDirector::readState( Archive& archive )
    {
        uint32 count = 0;
        // 살아 있는 것마다 id(4) + 항목(4)
        if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        const int32        entryCount = static_cast<int32>( _listAliveCount.size() );
        vector<SpawnAlive> listAlive( count );
        for ( SpawnAlive& alive : listAlive )
        {
            archive >> alive._spawnId;
            archive >> alive._entryIndex;
            if ( alive._entryIndex < 0 || alive._entryIndex >= entryCount )
                archive.setError();
        }
        if ( archive.isError() || StateArchiveUtil::readCount( archive, 4, count ) == false || count != _listAliveCount.size() )
            return false;
        vector<int32> listAliveCount( count, 0 );
        for ( int32& aliveCount : listAliveCount )
        {
            archive >> aliveCount;
        }
        if ( StateArchiveUtil::readCount( archive, 4, count ) == false )
            return false;
        vector<hashed_string> listAllowedTag( count );
        for ( hashed_string& tag : listAllowedTag )
        {
            if ( StateArchiveUtil::readName( archive, tag ) == false )
                return false;
        }
        GameRandom random       = _random;
        float32    budget       = 0.0f;
        float32    budgetScale  = 1.0f;
        float32    time         = 0.0f;
        int32      pendingIndex = -1;
        uint32     nextSpawnId  = 1;
        uint8      bRefund      = SW_FALSE;
        const bool bRandomRead  = StateArchiveUtil::readRandom( archive, random );
        archive >> budget;
        archive >> budgetScale;
        archive >> time;
        archive >> pendingIndex;
        archive >> nextSpawnId;
        archive >> bRefund;
        const bool bValid = bRandomRead && archive.isOk() && -1 <= pendingIndex && pendingIndex < entryCount && bRefund <= SW_TRUE;
        if ( bValid == false )
            return false;
        _listAlive        = std::move( listAlive );
        _listAliveCount   = std::move( listAliveCount );
        _listAllowedTag   = std::move( listAllowedTag );
        _random           = random;
        _budget           = budget;
        _budgetScale      = budgetScale;
        _time             = time;
        _pendingIndex     = pendingIndex;
        _nextSpawnId      = nextSpawnId;
        _bRefundOnDespawn = bRefund;
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
