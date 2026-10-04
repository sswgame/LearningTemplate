#include "pch.h"

#include "GameFramework/Kits/Action/BattleRoyale/BrZone.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    namespace
    {
        struct BrZoneInternal
        {
            static constexpr int32 kMaxPhaseBits = 1024; ///< 직렬화 단계 번호 상한

            static float32 computeDistance( const float2& lhs, const float2& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dy = lhs._y - rhs._y;
                return MathUtil::sqrt( dx * dx + dy * dy );
            }

            /** @brief @p center 둘레 반지름 @p radius 원판에서 고르게 고른 점입니다. */
            static float2 pickInDisk( GameRandom& random, const float2& center, float32 radius )
            {
                const float32 distance = radius * MathUtil::sqrt( random.nextFloat() );
                const float32 angle    = random.nextFloat() * MathUtil::Pi * 2.0f;
                return float2{ center._x + MathUtil::cos( angle ) * distance, center._y + MathUtil::sin( angle ) * distance };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BrZone::BrZone()
        : _settings{}
        , _eventBuffer{}
        , _pTerrain{ nullptr }
        , _random{}
        , _center{}
        , _fromCenter{}
        , _nextCenter{}
        , _radius{ 0.0f }
        , _fromRadius{ 0.0f }
        , _nextRadius{ 0.0f }
        , _mapSize{ 1.0f }
        , _stageRemaining{ 0.0f }
        , _phaseIndex{ 0 }
        , _stage{ BrZoneStage::Final }
    {
    }

    void BrZone::initialize( const BrZoneSettings& settings, float32 mapSize, uint32 seed, const IBrZoneTerrain* pTerrain )
    {
        _settings = settings;
        _pTerrain = pTerrain;
        _mapSize  = MathUtil::max( 1.0f, mapSize );
        _random.setSeed( seed );
        _eventBuffer.clear();
        const float32 half = _mapSize * 0.5f;
        _center            = float2{ half, half };
        _radius            = _settings._startRadius > 0.0f ? _settings._startRadius : half * MathUtil::sqrt( 2.0f );
        _fromCenter        = _center;
        _fromRadius        = _radius;
        beginPhase( 0 );
    }

    void BrZone::update( float32 deltaTime )
    {
        while ( deltaTime > 0.0f && _stage != BrZoneStage::Final )
        {
            const float32 used = MathUtil::min( deltaTime, _stageRemaining );
            _stageRemaining -= used;
            deltaTime -= used;
            const BrZonePhaseDef* pPhase = findPhase();
            if ( _stage == BrZoneStage::Shrinking && pPhase != nullptr )
            {
                const float32 ratio = pPhase->_shrinkTime > 0.0f ? MathUtil::saturate( 1.0f - _stageRemaining / pPhase->_shrinkTime ) : 1.0f;
                _center             = float2{ MathUtil::lerp( _fromCenter._x, _nextCenter._x, ratio ), MathUtil::lerp( _fromCenter._y, _nextCenter._y, ratio ) };
                _radius             = MathUtil::lerp( _fromRadius, _nextRadius, ratio );
            }
            if ( _stageRemaining > 0.0f )
                break;
            if ( _stage == BrZoneStage::Waiting )
            {
                _stage          = BrZoneStage::Shrinking;
                _stageRemaining = pPhase != nullptr ? pPhase->_shrinkTime : 0.0f;
                _fromCenter     = _center;
                _fromRadius     = _radius;
                pushEvent( BrZoneEvent::Kind::ShrinkStarted );
                if ( _stageRemaining <= 0.0f )
                {
                    _center = _nextCenter;
                    _radius = _nextRadius;
                }
                continue;
            }
            // 다 줄었다 — 다음 단계.
            _center = _nextCenter;
            _radius = _nextRadius;
            pushEvent( BrZoneEvent::Kind::ShrinkFinished );
            beginPhase( _phaseIndex + 1 );
        }
    }

    bool BrZone::isInside( const float2& position ) const { return BrZoneInternal::computeDistance( position, _center ) <= _radius; }

    float32 BrZone::computeDistanceOutside( const float2& position ) const
    {
        return MathUtil::max( 0.0f, BrZoneInternal::computeDistance( position, _center ) - _radius );
    }

    float32 BrZone::computeDamagePerSecond( const float2& position ) const
    {
        if ( isInside( position ) )
            return 0.0f;
        if ( const BrZonePhaseDef* pPhase = findPhase() )
            return pPhase->_damagePerSecond;
        return _settings._listPhase.empty() ? 0.0f : _settings._listPhase.back()._damagePerSecond;
    }

    bool BrZone::pickPointInside( GameRandom& random, float2& outPosition ) const
    {
        for ( int32 attempt = 0; attempt < _settings._centerAttempts; ++attempt )
        {
            const float2 candidate = BrZoneInternal::pickInDisk( random, _center, _radius );
            if ( isCenterAllowed( candidate, 0.0f ) )
            {
                outPosition = candidate;
                return true;
            }
        }
        outPosition = _center;
        return false;
    }

    void BrZone::drainEvents( vector<BrZoneEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void BrZone::writeState( BitWriter& writer ) const
    {
        writer.writeInt( _phaseIndex, 0, BrZoneInternal::kMaxPhaseBits );
        writer.writeInt( static_cast<int32>( _stage ), 0, 2 );
        writer.writeFloat( _stageRemaining );
        writer.writeFloat( _center._x );
        writer.writeFloat( _center._y );
        writer.writeFloat( _radius );
        writer.writeFloat( _fromCenter._x );
        writer.writeFloat( _fromCenter._y );
        writer.writeFloat( _fromRadius );
        writer.writeFloat( _nextCenter._x );
        writer.writeFloat( _nextCenter._y );
        writer.writeFloat( _nextRadius );
    }

    bool BrZone::readState( BitReader& reader )
    {
        const int32   phaseIndex     = reader.readInt( 0, BrZoneInternal::kMaxPhaseBits );
        const int32   stage          = reader.readInt( 0, 2 );
        const float32 stageRemaining = reader.readFloat();
        const float32 centerX        = reader.readFloat();
        const float32 centerY        = reader.readFloat();
        const float32 radius         = reader.readFloat();
        const float32 fromX          = reader.readFloat();
        const float32 fromY          = reader.readFloat();
        const float32 fromRadius     = reader.readFloat();
        const float32 nextX          = reader.readFloat();
        const float32 nextY          = reader.readFloat();
        const float32 nextRadius     = reader.readFloat();
        if ( reader.hasOverflowed() )
            return false;
        _phaseIndex     = phaseIndex;
        _stage          = static_cast<BrZoneStage>( stage );
        _stageRemaining = stageRemaining;
        _center         = float2{ centerX, centerY };
        _radius         = radius;
        _fromCenter     = float2{ fromX, fromY };
        _fromRadius     = fromRadius;
        _nextCenter     = float2{ nextX, nextY };
        _nextRadius     = nextRadius;
        return true;
    }

    bool BrZone::isCenterAllowed( const float2& center, float32 radius ) const
    {
        // 원이 맵 밖으로 나가지 않게 — 원이 맵보다 크면 중심이 맵 안이기만 하면 된다.
        const float32 margin = MathUtil::min( radius, _mapSize * 0.5f );
        if ( center._x < margin || center._y < margin || center._x > _mapSize - margin || center._y > _mapSize - margin )
            return false;
        return _pTerrain == nullptr || _pTerrain->isZoneCenterAllowed( center );
    }

    void BrZone::beginPhase( int32 phaseIndex )
    {
        _phaseIndex = phaseIndex;
        if ( phaseIndex >= static_cast<int32>( _settings._listPhase.size() ) )
        {
            _stage          = BrZoneStage::Final;
            _stageRemaining = 0.0f;
            _nextCenter     = _center;
            _nextRadius     = _radius;
            pushEvent( BrZoneEvent::Kind::FinalZone );
            return;
        }
        const BrZonePhaseDef& phase = _settings._listPhase[static_cast<size_t>( phaseIndex )];
        _nextRadius                 = _radius * phase._radiusRatio;
        _nextCenter                 = _center;
        for ( int32 attempt = 0; attempt < _settings._centerAttempts; ++attempt )
        {
            const float2 candidate = BrZoneInternal::pickInDisk( _random, _center, _radius - _nextRadius );
            if ( isCenterAllowed( candidate, _nextRadius ) )
            {
                _nextCenter = candidate;
                break;
            }
        }
        _stage          = BrZoneStage::Waiting;
        _stageRemaining = phase._waitTime;
        pushEvent( BrZoneEvent::Kind::NextZoneRevealed );
    }

    const BrZonePhaseDef* BrZone::findPhase() const
    {
        if ( _phaseIndex < 0 || _phaseIndex >= static_cast<int32>( _settings._listPhase.size() ) )
            return nullptr;
        return &_settings._listPhase[static_cast<size_t>( _phaseIndex )];
    }

    void BrZone::pushEvent( BrZoneEvent::Kind kind )
    {
        BrZoneEvent event;
        event._kind   = kind;
        event._phase  = _phaseIndex;
        event._center = _stage == BrZoneStage::Waiting ? _nextCenter : _center;
        event._radius = _stage == BrZoneStage::Waiting ? _nextRadius : _radius;
        _eventBuffer.push( event );
    }
} // namespace sw
