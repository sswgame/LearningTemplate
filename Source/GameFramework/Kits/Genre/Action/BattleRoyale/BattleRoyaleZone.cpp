#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleZone.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct BattleRoyaleZoneInternal
        {
            static float32 computeDistance( const float2& lhs, const float2& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dy = lhs._y - rhs._y;
                return MathUtil::sqrt( dx * dx + dy * dy );
            }

            /** @brief @p center 둘레의 반지름 @p radius 원판에서 고르게 뽑은 점을 반환합니다. */
            static float2 pickInDisk( GameRandom& random, const float2& center, float32 radius )
            {
                const float32 distance = radius * MathUtil::sqrt( random.nextFloat() );
                const float32 angle    = random.nextFloat() * MathUtil::kPi * 2.0f;
                return float2{ center._x + MathUtil::cos( angle ) * distance, center._y + MathUtil::sin( angle ) * distance };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BattleRoyaleZone::BattleRoyaleZone()
        : _settings{}
        , _eventBuffer{}
        , _pTerrain{ nullptr }
        , _random{}
        , _mapSize{ 1.0f }
        , _phaseIndex{ 0 }
        , _stage{ BattleRoyaleZoneStage::Final }
        , _stageRemaining{ 0.0f }
        , _center{}
        , _radius{ 0.0f }
        , _fromCenter{}
        , _fromRadius{ 0.0f }
        , _nextCenter{}
        , _nextRadius{ 0.0f }
    {
    }

    bool BattleRoyaleZone::initialize( const BattleRoyaleZoneDef& settings, float32 mapSize, uint32 seed, const IBrZoneTerrain& terrain )
    {
        // 맵 크기가 1 미만이면 맞추지 않고 거절한다(카탈로그의 `_mapSize` 는 `Min = 1` 이다).
        if ( mapSize < 1.0f )
        {
            SW_LOG_ERROR_ID( "BattleRoyaleZone.BattleRoyaleZoneRefusedMapSize", "BattleRoyaleZone: refused map size %# - it must be at least 1", mapSize );
            return false;
        }
        _settings = settings;
        _pTerrain = &terrain;
        _mapSize  = mapSize;
        _random.setSeed( seed );
        _eventBuffer.clear();
        const float32 half = _mapSize * 0.5f;
        _center            = float2{ half, half };
        _radius            = _settings._startRadius > 0.0f ? _settings._startRadius : half * MathUtil::sqrt( 2.0f );
        _fromCenter        = _center;
        _fromRadius        = _radius;
        beginPhase( 0 );
        return true;
    }

    void BattleRoyaleZone::update( float32 deltaTime )
    {
        while ( deltaTime > 0.0f && _stage != BattleRoyaleZoneStage::Final )
        {
            const float32 used = MathUtil::min( deltaTime, _stageRemaining );
            _stageRemaining -= used;
            deltaTime -= used;
            const BattleRoyaleZonePhaseDef* pPhase = findPhase();
            if ( _stage == BattleRoyaleZoneStage::Shrinking && pPhase != nullptr )
            {
                const float32 ratio = pPhase->_shrinkTime > 0.0f ? MathUtil::saturate( 1.0f - _stageRemaining / pPhase->_shrinkTime ) : 1.0f;
                _center             = float2{ MathUtil::lerp( _fromCenter._x, _nextCenter._x, ratio ), MathUtil::lerp( _fromCenter._y, _nextCenter._y, ratio ) };
                _radius             = MathUtil::lerp( _fromRadius, _nextRadius, ratio );
            }
            if ( _stageRemaining > 0.0f )
                break;
            if ( _stage == BattleRoyaleZoneStage::Waiting )
            {
                _stage          = BattleRoyaleZoneStage::Shrinking;
                _stageRemaining = pPhase != nullptr ? pPhase->_shrinkTime : 0.0f;
                _fromCenter     = _center;
                _fromRadius     = _radius;
                pushEvent( BattleRoyaleZoneEvent::Kind::ShrinkStarted );
                if ( _stageRemaining <= 0.0f )
                {
                    _center = _nextCenter;
                    _radius = _nextRadius;
                }
                continue;
            }
            // 다 줄었으면 다음 단계로 넘어간다.
            _center = _nextCenter;
            _radius = _nextRadius;
            beginPhase( _phaseIndex + 1 );
        }
    }

    bool BattleRoyaleZone::isInside( const float2& position ) const
    {
        return BattleRoyaleZoneInternal::computeDistance( position, _center ) <= _radius;
    }

    float32 BattleRoyaleZone::computeDistanceOutside( const float2& position ) const
    {
        return MathUtil::max( 0.0f, BattleRoyaleZoneInternal::computeDistance( position, _center ) - _radius );
    }

    float32 BattleRoyaleZone::computeDamagePerSecond( const float2& position ) const
    {
        if ( isInside( position ) )
            return 0.0f;
        if ( const BattleRoyaleZonePhaseDef* pPhase = findPhase() )
            return pPhase->_damagePerSecond;
        return _settings._listPhase.empty() ? 0.0f : _settings._listPhase.back()._damagePerSecond;
    }

    bool BattleRoyaleZone::pickPointInside( GameRandom& random, float2& outPosition ) const
    {
        for ( int32 attempt = 0; attempt < _settings._centerAttempts; ++attempt )
        {
            const float2 candidate = BattleRoyaleZoneInternal::pickInDisk( random, _center, _radius );
            if ( isCenterAllowed( candidate, 0.0f ) )
            {
                outPosition = candidate;
                return true;
            }
        }
        outPosition = _center;
        return false;
    }

    void BattleRoyaleZone::pollEvents( vector<BattleRoyaleZoneEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    bool BattleRoyaleZone::writeState( BitWriter& writer ) const
    {
        return ReplicatedLayout::writeFull( *this, writer );
    }

    bool BattleRoyaleZone::readState( BitReader& reader )
    {
        BattleRoyaleZone loaded = *this; // 단계 검사까지 지난 뒤에만 옮긴다
        if ( ReplicatedLayout::readFull( reader, loaded, nullptr ) != FixedReadResult::Ok || loaded.isPhaseValid() == false )
            return false;
        *this = std::move( loaded );
        return true;
    }

    void BattleRoyaleZone::serializeState( StateIO& io )
    {
        io.reflected( *this );
        io.value( _random );
        if ( io.isReading() && io.isOk() )
        {
            io.check( isPhaseValid() );
            _eventBuffer.clear();
        }
    }

    bool BattleRoyaleZone::isCenterAllowed( const float2& center, float32 radius ) const
    {
        // 원이 맵 밖으로 나가지 않게 한다. 원이 맵보다 크면 중심이 맵 안에 있기만 하면 된다.
        const float32 margin            = MathUtil::min( radius, _mapSize * 0.5f );
        const bool    bCenterOutsideMap = center._x < margin || center._y < margin || center._x > _mapSize - margin || center._y > _mapSize - margin;
        if ( bCenterOutsideMap )
            return false;
        return _pTerrain->isZoneCenterAllowed( center );
    }

    void BattleRoyaleZone::beginPhase( int32 phaseIndex )
    {
        _phaseIndex = phaseIndex;
        if ( phaseIndex >= static_cast<int32>( _settings._listPhase.size() ) )
        {
            _stage          = BattleRoyaleZoneStage::Final;
            _stageRemaining = 0.0f;
            _nextCenter     = _center;
            _nextRadius     = _radius;
            pushEvent( BattleRoyaleZoneEvent::Kind::FinalZone );
            return;
        }
        const BattleRoyaleZonePhaseDef& phase = _settings._listPhase[static_cast<size_t>( phaseIndex )];
        _nextRadius                           = _radius * phase._radiusRatio;
        _nextCenter                           = _center;
        for ( int32 attempt = 0; attempt < _settings._centerAttempts; ++attempt )
        {
            const float2 candidate = BattleRoyaleZoneInternal::pickInDisk( _random, _center, _radius - _nextRadius );
            if ( isCenterAllowed( candidate, _nextRadius ) )
            {
                _nextCenter = candidate;
                break;
            }
        }
        _stage          = BattleRoyaleZoneStage::Waiting;
        _stageRemaining = phase._waitTime;
        pushEvent( BattleRoyaleZoneEvent::Kind::NextZoneRevealed );
    }

    bool BattleRoyaleZone::isPhaseValid() const
    {
        const int32 phaseCount = static_cast<int32>( _settings._listPhase.size() );
        return _phaseIndex <= phaseCount && ( _stage == BattleRoyaleZoneStage::Final || _phaseIndex < phaseCount );
    }

    const BattleRoyaleZonePhaseDef* BattleRoyaleZone::findPhase() const
    {
        if ( _phaseIndex < 0 || _phaseIndex >= static_cast<int32>( _settings._listPhase.size() ) )
            return nullptr;
        return &_settings._listPhase[static_cast<size_t>( _phaseIndex )];
    }

    void BattleRoyaleZone::pushEvent( BattleRoyaleZoneEvent::Kind kind )
    {
        BattleRoyaleZoneEvent event;
        event._kind   = kind;
        event._phase  = _phaseIndex;
        event._center = _stage == BattleRoyaleZoneStage::Waiting ? _nextCenter : _center;
        event._radius = _stage == BattleRoyaleZoneStage::Waiting ? _nextRadius : _radius;
        _eventBuffer.push( event );
    }
} // namespace sw
