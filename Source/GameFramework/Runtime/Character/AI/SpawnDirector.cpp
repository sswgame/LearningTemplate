#include "pch.h"

#include "GameFramework/Runtime/Character/AI/SpawnDirector.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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

    uint32 SpawnTable::loadRoot( const XMLNode& root, string_view sourceName )
    {
        _catalog.clear();
        _curve._listKey.clear();
        // 적지 않은 칸은 생성자의 초기값이다. 깨진 값 · 범위 밖 값은 표 전체를 읽지 않는 오류다(자르지 않는다).
        SpawnTableRootParams rootParams{ _budgetPerMinute, _maxBudget, _startBudget, _bRefundOnDespawn == SW_TRUE };
        bool                 bValid = XMLFields::readProperties( root, rootParams, sourceName );
        _budgetPerMinute            = rootParams._budgetPerMinute;
        _maxBudget                  = rootParams._maxBudget;
        _startBudget                = rootParams._startBudget;
        _bRefundOnDespawn           = rootParams._bRefund ? SW_TRUE : SW_FALSE;
        // 키가 없으면 곡선이 대체값을 쓴다(곡선의 계약). 모르는 이름 · 깨진 키는 오류다.
        using Element                          = XMLCatalogElement<SpawnTable>;
        static constexpr Element kArrElement[] = {
            Element::of<&SpawnTable::loadEntry>( "Entry" ).uncounted(),
            Element::of<&SpawnTable::loadCurve>( "Curve" ).uncounted(),
        };
        bValid = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement ).isClean() && bValid;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "SpawnDirector.SpawnTableInvalidParams", "%#: spawn table has invalid fields - not loaded", sourceName );
            _catalog.clear();
            return 0;
        }
        return static_cast<uint32>( _catalog.getCount() );
    }

    bool SpawnTable::loadEntry( const XMLNode& node, string_view sourceName )
    {
        SpawnEntryDef entry;
        if ( XMLFields::readProperties( node, entry, sourceName ) == false )
            return false;
        (void)_catalog.add( entry );
        return true;
    }

    bool SpawnTable::loadCurve( const XMLNode& node, string_view sourceName )
    {
        return GameDataXML::readFloatCurveElement( node, 0.0f, _curve, sourceName );
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
        , _nextSpawnID{ 1 }
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
        _nextSpawnID      = 1;
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
        const bool           bCountOk    = entry.canSpawnMore( _listAliveCount[static_cast<size_t>( entryIndex )] );
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
        {
            total += isEligible( static_cast<int32>( index ) ) ? listEntry[index]._weight : 0.0f;
        }
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
            alive._spawnID    = _nextSpawnID++;
            alive._entryIndex = _pendingIndex;
            _listAlive.push_back( alive );

            SpawnEvent event;
            event._kind    = SpawnEvent::Kind::Spawned;
            event._entryID = entry._id;
            event._time    = _time;
            event._cost    = entry._cost;
            event._spawnID = alive._spawnID;
            _eventBuffer.push( event );
            _pendingIndex = -1;
            ++spawnCount;
        }
        return spawnCount;
    }

    bool SpawnDirector::notifyDespawned( uint32 spawnID )
    {
        for ( size_t index = 0; index < _listAlive.size(); ++index )
        {
            if ( _listAlive[index]._spawnID != spawnID )
                continue;
            const int32          entryIndex = _listAlive[index]._entryIndex;
            const SpawnEntryDef& entry      = _pTable->getEntries()[static_cast<size_t>( entryIndex )];
            _listAlive.erase( _listAlive.begin() + static_cast<ptrdiff_t>( index ) );
            --_listAliveCount[static_cast<size_t>( entryIndex )];
            const float32 refund = _bRefundOnDespawn == SW_TRUE ? entry._cost : 0.0f;
            _budget              = MathUtil::min( _pTable->getMaxBudget(), _budget + refund );

            SpawnEvent event;
            event._kind    = SpawnEvent::Kind::Despawned;
            event._entryID = entry._id;
            event._time    = _time;
            event._cost    = refund;
            event._spawnID = spawnID;
            _eventBuffer.push( event );
            return true;
        }
        return false;
    }

    void SpawnDirector::pollEvents( vector<SpawnEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    int32 SpawnDirector::getAliveCount( const hashed_string& entryID ) const
    {
        const optional<int32> entryIndex = _pTable != nullptr ? _pTable->findEntryIndex( entryID ) : optional<int32>{};
        return entryIndex.has_value() ? _listAliveCount[static_cast<size_t>( *entryIndex )] : 0;
    }

    hashed_string SpawnDirector::getPendingEntry() const
    {
        return _pendingIndex >= 0 ? _pTable->getEntries()[static_cast<size_t>( _pendingIndex )]._id : hashed_string{};
    }

    void SpawnDirector::collectAliveSpawnIDs( vector<uint32>& outListSpawnID ) const
    {
        outListSpawnID.clear();
        outListSpawnID.reserve( _listAlive.size() );
        for ( const SpawnAlive& alive : _listAlive )
        {
            outListSpawnID.push_back( alive._spawnID );
        }
    }

    void SpawnDirector::serializeState( StateIO& io )
    {
        const int32 entryCount = static_cast<int32>( _listAliveCount.size() );
        // 살아 있는 것마다 id(4) + 항목(4)
        io.list( _listAlive, 8, [&]( SpawnAlive& alive )
        {
            io.value( alive._spawnID );
            io.value( alive._entryIndex );
            io.check( 0 <= alive._entryIndex && alive._entryIndex < entryCount );
        } );
        io.expect( static_cast<uint32>( _listAliveCount.size() ) );
        if ( io.isOk() == false )
            return;
        for ( int32& aliveCount : _listAliveCount )
        {
            io.value( aliveCount );
        }
        io.list( _listAllowedTag );
        io.value( _random );
        io.value( _budget );
        io.value( _budgetScale );
        io.value( _time );
        io.value( _pendingIndex );
        io.value( _nextSpawnID );
        io.flag( _bRefundOnDespawn );
        io.check( -1 <= _pendingIndex && _pendingIndex < entryCount );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
        }
    }

} // namespace sw

namespace sw
{
    /** @brief `SpawnTable` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( SpawnTable, SpawnTable, SW_GF_API, SpawnTable::kAssetType.withLoader( AssetTypeLoader::of<SpawnTable>( MemoryTag::Game ) ) );
} // namespace sw
