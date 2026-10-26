#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyItemSpawner.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <optional>

namespace sw
{
    SW_LOG_CALLER( "PartyItemSpawner" );
} // namespace sw

namespace sw
{
    PartyItemSpawner::PartyItemSpawner()
        : _catalog{}
        , _listInstance{}
        , _eventBuffer{}
        , _settings{}
        , _random{}
        , _spawnTimer{}
        , _nextSerial{ 1 }
    {
    }

    bool PartyItemSpawner::initialize( const PartyItemSpawnSettings& settings, uint32 seed )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR( "PartyItemSpawner: refused settings - %#", rangeError.c_str() );
            return false;
        }
        _settings = settings;
        _random.setSeed( seed );
        _listInstance.clear();
        _eventBuffer.clear();
        _nextSerial = 1;
        _spawnTimer.start( settings._minInterval );
        return true;
    }

    void PartyItemSpawner::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listInstance.size() );
        for ( const PartyItemInstance& instance : _listInstance )
        {
            outArchive << instance._position;
            StateArchiveUtil::writeName( outArchive, instance._itemID );
            outArchive << instance._age;
            outArchive << instance._serial;
        }
        StateArchiveUtil::writeRandom( outArchive, _random );
        StateArchiveUtil::writeCountdown( outArchive, _spawnTimer );
        outArchive << _nextSerial;
    }

    bool PartyItemSpawner::readState( Archive& archive )
    {
        uint32 instanceCount = 0;
        // 놓인 것마다 위치(12) + 이름(4) + 나이(4) + 번호(4)
        if ( StateArchiveUtil::readCount( archive, 24, instanceCount ) == false )
            return false;
        vector<PartyItemInstance> listInstance( instanceCount, PartyItemInstance{} );
        for ( PartyItemInstance& instance : listInstance )
        {
            archive >> instance._position;
            if ( StateArchiveUtil::readName( archive, instance._itemID ) == false || findItem( instance._itemID ) == nullptr )
                return false;
            archive >> instance._age;
            archive >> instance._serial;
        }
        GameRandom random     = _random;
        Countdown  spawnTimer = {};
        int32      nextSerial = 0;
        const bool bRead      = StateArchiveUtil::readRandom( archive, random ) && StateArchiveUtil::readCountdown( archive, spawnTimer );
        archive >> nextSerial;
        if ( bRead == false || archive.isError() )
            return false;
        _listInstance = std::move( listInstance );
        _random       = random;
        _spawnTimer   = spawnTimer;
        _nextSerial   = nextSerial;
        _eventBuffer.clear();
        return true;
    }

    float32 PartyItemSpawner::rollInterval()
    {
        // 설정은 `initialize` 가 범위(`_minInterval` >= 0, `_maxInterval` >= `_minInterval`)를 거쳐 받았다.
        const float32 low  = _settings._minInterval;
        const float32 high = _settings._maxInterval;
        return high > low ? _random.nextRange( low, high ) : low;
    }

    void PartyItemSpawner::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        // 1) 수명.
        size_t writeIndex = 0;
        for ( size_t readIndex = 0; readIndex < _listInstance.size(); ++readIndex )
        {
            PartyItemInstance& instance = _listInstance[readIndex];
            instance._age += deltaTime;
            const bool bExpired = _settings._lifetime > 0.0f && instance._age >= _settings._lifetime;
            if ( bExpired )
            {
                PartyItemEvent event;
                event._kind   = PartyItemEvent::Kind::Expired;
                event._itemID = instance._itemID;
                event._serial = instance._serial;
                _eventBuffer.push( event );
                continue;
            }
            _listInstance[writeIndex++] = instance;
        }
        _listInstance.resize( writeIndex );

        // 2) 새로 놓기: 슬롯이 차 있으면 타이머는 멈춰 있고, 빈 슬롯이 나면 곧 나온다.
        if ( _catalog.isEmpty() || static_cast<int32>( _listInstance.size() ) >= _settings._maxActive )
            return;
        _spawnTimer.update( deltaTime );
        if ( _spawnTimer.isActive() )
            return;
        spawnOne();
        _spawnTimer.restart( rollInterval() ); // 늦은 만큼을 이어받아, 스폰 빈도가 fps 에 매이지 않게 한다
    }

    const PartyItemDef* PartyItemSpawner::pickWeighted()
    {
        const vector<PartyItemDef>& listDef = _catalog.getAll();
        const int32                 index   = _random.pickWeightedIndex( listDef, []( const PartyItemDef& def )
                          { return def._weight; } );
        return index >= 0 ? &listDef[static_cast<size_t>( index )] : nullptr;
    }

    void PartyItemSpawner::spawnOne()
    {
        const PartyItemDef* pDef = pickWeighted();
        if ( pDef == nullptr )
            return;
        // 원판 안에 고르게 놓으려고 반지름에 제곱근을 쓴다.
        const float32     radius = _settings._spawnRadius * MathUtil::sqrt( _random.nextFloat() );
        const float32     angle  = _random.nextRange( 0.0f, 2.0f * MathUtil::kPi );
        PartyItemInstance instance;
        instance._itemID   = pDef->_id;
        instance._serial   = _nextSerial++;
        instance._position = float3{ _settings._center._x + MathUtil::sin( angle ) * radius, _settings._center._y + _settings._height,
                                     _settings._center._z + MathUtil::cos( angle ) * radius };
        _listInstance.push_back( instance );

        PartyItemEvent event;
        event._kind   = PartyItemEvent::Kind::Spawned;
        event._itemID = instance._itemID;
        event._serial = instance._serial;
        _eventBuffer.push( event );
    }

    bool PartyItemSpawner::tryPickUp( const float3& position, float32 radius, int32 player, PartyItemInstance& outItem )
    {
        int32   bestIndex  = -1;
        float32 bestDistSq = radius * radius;
        for ( size_t index = 0; index < _listInstance.size(); ++index )
        {
            const float32 distSq = float3::getDistanceSquared( position, _listInstance[index]._position );
            if ( distSq <= bestDistSq )
            {
                bestDistSq = distSq;
                bestIndex  = static_cast<int32>( index );
            }
        }
        if ( bestIndex < 0 )
            return false;
        outItem = _listInstance[static_cast<size_t>( bestIndex )];
        _listInstance.erase( _listInstance.begin() + bestIndex );

        PartyItemEvent event;
        event._kind   = PartyItemEvent::Kind::PickedUp;
        event._itemID = outItem._itemID;
        event._serial = outItem._serial;
        event._player = player;
        _eventBuffer.push( event );
        return true;
    }

    void PartyItemSpawner::drainEvents( vector<PartyItemEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    uint32 PartyItemSpawner::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 루트 속성은 `PartyItemSpawnSettings` 스키마다(기본값 = 초기값, 범위 = PROPERTY). 가운데(`_center`)는 게임이 정한다.
        PartyItemSpawnSettings settings;
        if ( GameDataXML::readFields( root, settings, sourceName ) )
        {
            settings._center = _settings._center;
            _settings        = settings;
        }
        uint32 loadedCount = 0;
        for ( XMLNode itemNode = root.findChild(); itemNode; itemNode = itemNode.findNextSibling() )
        {
            if ( StringUtil::equals( itemNode.getName(), "Item", true ) == false )
            {
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, itemNode.getName() );
                continue;
            }
            // 아이템의 효과 수치는 속성 이름이 곧 스탯 이름이라 스키마가 없다 — id · effect · weight 만 형식이 있다.
            const utf8*                 pID    = GameDataXML::findRequiredID( itemNode, sourceName );
            const std::optional<string> effect = itemNode.readRequiredAttribute<string>( "effect" );
            std::optional<float32>      weight;
            const bool                  bWeight = itemNode.readOptionalAttribute( "weight", weight ) && ( weight.has_value() == false || *weight >= 0.0f );
            const bool                  bItem   = pID != nullptr && effect.has_value() && bWeight;
            if ( bItem == false )
            {
                SW_LOG_ERROR( "%#: <Item> needs id, effect and a weight of 0 or more - skipped", sourceName );
                continue;
            }
            PartyItemDef def;
            def._id     = hashed_string( pID );
            def._effect = hashed_string( effect->c_str() );
            if ( weight.has_value() )
                def._weight = *weight;
            (void)def._stats.loadFromAttributes( itemNode, "id,effect,weight" ); // 읽은 속성 수만 돌려주므로, 속성이 없으면 빈 스탯이다
            addItem( def );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_ERROR( "%#: no <Item> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
