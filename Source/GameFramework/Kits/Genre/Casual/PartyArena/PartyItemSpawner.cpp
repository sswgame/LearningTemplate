#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyItemSpawner.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
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
            SW_LOG_ERROR_ID( "PartyItemSpawner.PartyItemSpawnerRefusedSettings", "PartyItemSpawner: refused settings - %#", rangeError.c_str() );
            return false;
        }
        _settings = settings;
        _random.setSeed( seed );
        _listInstance.clear();
        _eventBuffer.clear();
        _nextSerial = 1;
        _spawnTimer.start( _settings._minInterval );
        return true;
    }

    void PartyItemSpawner::serializeState( StateIO& io )
    {
        // 놓인 것마다 위치(12) + 이름(4) + 나이(4) + 번호(4)
        io.list( _listInstance, 24, [&]( PartyItemInstance& instance )
        {
            io.value( instance._position );
            io.value( instance._itemID );
            io.value( instance._age );
            io.value( instance._serial );
            io.check( findItem( instance._itemID ) != nullptr && 0.0f <= instance._age && 1 <= instance._serial );
        } );
        io.check( _listInstance.size() <= static_cast<size_t>( _settings._maxActive ) );
        io.value( _random );
        io.value( _spawnTimer );
        io.value( _nextSerial );
        io.check( 1 <= _nextSerial );
        if ( io.isWriting() )
            return;
        // 번호는 하나씩 오르므로 놓인 것은 모두 다음 번호보다 작다. 어기면 새 아이템이 같은 번호를 받는다.
        for ( const PartyItemInstance& instance : _listInstance )
        {
            io.check( instance._serial < _nextSerial );
        }
        _eventBuffer.clear();
    }

    float32 PartyItemSpawner::rollInterval()
    {
        // 0 <= 최소 <= 최대 는 `initialize` 가 선언된 범위로 거절해 둔 계약이다.
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
        const optional<int32>       index   = _random.pickWeightedIndex( listDef, []( const PartyItemDef& def )
                { return def._weight; } );
        return index.has_value() ? &listDef[static_cast<size_t>( *index )] : nullptr;
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
        int32   bestIndex  = constant::invalid_index::kInt32;
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

    void PartyItemSpawner::pollEvents( vector<PartyItemEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    uint32 PartyItemSpawner::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 루트 속성은 `PartyItemSpawnSettings` 스키마다(기본값 = 초기값, 범위 = PROPERTY). 가운데(`_center`)는 게임이 정한다.
        // 틀리면 파일을 읽지 않는다 — 규칙은 기본값인 채 아이템만 읽혀 성공으로 보이지 않게 한다.
        PartyItemSpawnSettings settings;
        if ( root.readProperties( settings, sourceName ) == false )
            return 0;
        settings._center                       = _settings._center;
        _settings                              = settings;
        using Element                          = XMLCatalogElement<PartyItemSpawner>;
        static constexpr Element kArrElement[] = {
            Element::of<&PartyItemSpawner::loadItem>( "Item" ),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool PartyItemSpawner::loadItem( const XMLNode& node, string_view sourceName )
    {
        // 아이템의 효과 수치는 속성 이름이 곧 스탯 이름이라 스키마가 없다 — id · effect · weight 만 형식이 있다.
        const utf8*       pID    = XMLReadUtil::findRequiredID( node, sourceName );
        const auto        effect = node.readRequiredAttribute<string>( "effect" );
        optional<float32> weight;
        const bool        bWeight = node.readOptionalAttribute( "weight", weight ) && ( weight.has_value() == false || *weight >= 0.0f );
        const bool        bItem   = pID != nullptr && effect.has_value() && bWeight;
        if ( bItem == false )
        {
            SW_LOG_ERROR_ID( "PartyItemSpawner.ItemNeedsIdEffect", "%#: <Item> needs id, effect and a weight of 0 or more - skipped", sourceName );
            return false;
        }
        PartyItemDef def;
        def._id     = hashed_string( pID );
        def._effect = hashed_string( effect->c_str() );
        if ( weight.has_value() )
            def._weight = *weight;
        def._stats.addAttributes( node, "id,effect,weight" ); // 읽은 속성 수만 돌려주므로, 속성이 없으면 빈 스탯이다
        return GameDataXML::addUnique( def, sourceName, _catalog );
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<PartyItemSpawner>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( PartyItemSpawner, PartyItemSpawner, SW_KIT_API, PartyItemSpawner::kAssetType.withLoader( AssetTypeLoader::of<PartyItemSpawner>( MemoryTag::Game ) ) );
} // namespace sw
