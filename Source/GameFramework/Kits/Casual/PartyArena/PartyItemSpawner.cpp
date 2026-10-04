#include "pch.h"

#include "GameFramework/Kits/Casual/PartyArena/PartyItemSpawner.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "PartyItemSpawner" );
} // namespace sw

namespace sw
{
    PartyItemSpawner::PartyItemSpawner()
        : _catalog{}
        , _listInstance{}
        , _listEvent{}
        , _settings{}
        , _random{}
        , _spawnTimer{}
        , _nextSerial{ 1 }
    {
    }

    void PartyItemSpawner::initialize( const PartyItemSpawnSettings& settings, uint32 seed )
    {
        _settings = settings;
        _random.setSeed( seed );
        _listInstance.clear();
        _listEvent.clear();
        _nextSerial = 1;
        _spawnTimer.start( MathUtil::max( 0.0f, settings._minInterval ) );
    }

    float32 PartyItemSpawner::rollInterval()
    {
        const float32 low  = MathUtil::max( 0.0f, _settings._minInterval );
        const float32 high = MathUtil::max( low, _settings._maxInterval );
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
                event._itemId = instance._itemId;
                event._serial = instance._serial;
                _listEvent.push_back( event );
                continue;
            }
            _listInstance[writeIndex++] = instance;
        }
        _listInstance.resize( writeIndex );

        // 2) 새로 놓기 — 자리가 차 있으면 시계는 멈춰 있다(빈 자리가 나면 곧 나온다).
        if ( _catalog.isEmpty() || static_cast<int32>( _listInstance.size() ) >= _settings._maxActive )
            return;
        _spawnTimer.tick( deltaTime );
        if ( _spawnTimer.isActive() )
            return;
        spawnOne();
        _spawnTimer.start( rollInterval() );
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
        // 원판 안 고르게 — 반지름에 제곱근.
        const float32     radius = _settings._spawnRadius * MathUtil::sqrt( _random.nextFloat() );
        const float32     angle  = _random.nextRange( 0.0f, 2.0f * MathUtil::Pi );
        PartyItemInstance instance;
        instance._itemId   = pDef->_id;
        instance._serial   = _nextSerial++;
        instance._position = float3{ _settings._center._x + MathUtil::sin( angle ) * radius, _settings._center._y + _settings._height,
                                     _settings._center._z + MathUtil::cos( angle ) * radius };
        _listInstance.push_back( instance );

        PartyItemEvent event;
        event._kind   = PartyItemEvent::Kind::Spawned;
        event._itemId = instance._itemId;
        event._serial = instance._serial;
        _listEvent.push_back( event );
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
        event._itemId = outItem._itemId;
        event._serial = outItem._serial;
        event._player = player;
        _listEvent.push_back( event );
        return true;
    }

    void PartyItemSpawner::drainEvents( vector<PartyItemEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    bool PartyItemSpawner::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &PartyItemSpawner::loadRoot, path, "PartyItems" );
    }

    bool PartyItemSpawner::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &PartyItemSpawner::loadRoot, xmlText, sourceName, "PartyItems" );
    }

    uint32 PartyItemSpawner::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _settings._minInterval = root.getAttributeFloat( "minInterval", _settings._minInterval );
        _settings._maxInterval = root.getAttributeFloat( "maxInterval", _settings._maxInterval );
        _settings._lifetime    = root.getAttributeFloat( "lifetime", _settings._lifetime );
        _settings._spawnRadius = root.getAttributeFloat( "radius", _settings._spawnRadius );
        _settings._height      = root.getAttributeFloat( "height", _settings._height );
        _settings._maxActive   = root.getAttributeInt( "maxActive", _settings._maxActive );
        uint32 loadedCount     = 0;
        for ( XmlNode itemNode = root.findChild( "Item" ); itemNode; itemNode = itemNode.findNextSibling( "Item" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( itemNode, sourceName );
            if ( pId == nullptr )
                continue;
            PartyItemDef def;
            def._id             = hashed_string( pId );
            const utf8* pEffect = itemNode.findAttribute( "effect" );
            def._effect         = hashed_string( pEffect != nullptr ? pEffect : pId );
            def._weight         = itemNode.getAttributeFloat( "weight", def._weight );
            (void)def._stats.loadFromAttributes( itemNode, "id,effect,weight" );
            addItem( def );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Item> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
