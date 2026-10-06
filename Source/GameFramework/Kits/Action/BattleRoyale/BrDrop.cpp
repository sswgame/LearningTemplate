#include "pch.h"

#include "GameFramework/Kits/Action/BattleRoyale/BrDrop.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Utility/GameRandom.h"

namespace sw
{
    namespace
    {
        struct BrDropInternal
        {
            /** @brief 길이를 1 로 자른 조종 방향입니다. */
            static float2 clampSteer( const float2& steer )
            {
                const float32 length = MathUtil::sqrt( steer._x * steer._x + steer._y * steer._y );
                if ( length <= 1.0f )
                    return steer;
                return float2{ steer._x / length, steer._y / length };
            }

            /** @brief 한 축의 슬랩 [0, size] 로 선분 범위 [inoutMin, inoutMax] 를 좁힙니다. */
            static void clipAxis( float32 origin, float32 direction, float32 size, float32& inoutMin, float32& inoutMax )
            {
                if ( MathUtil::abs( direction ) < MathUtil::kEpsilon )
                    return;
                float32 enter = ( 0.0f - origin ) / direction;
                float32 leave = ( size - origin ) / direction;
                if ( enter > leave )
                {
                    const float32 swapValue = enter;
                    enter                   = leave;
                    leave                   = swapValue;
                }
                inoutMin = MathUtil::max( inoutMin, enter );
                inoutMax = MathUtil::min( inoutMax, leave );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BrFlightPath BrFlightPath::makeRandom( const BrFlightSettings& settings, float32 mapSize, uint32 seed )
    {
        GameRandom    random{ seed };
        const float32 size      = MathUtil::max( 1.0f, mapSize );
        const float32 half      = size * 0.5f;
        const float32 angle     = random.nextFloat() * MathUtil::kPi * 2.0f;
        const float32 offset    = random.nextRange( -1.0f, 1.0f ) * settings._offsetRatio * half;
        const float2  direction = float2{ MathUtil::cos( angle ), MathUtil::sin( angle ) };
        const float2  origin    = float2{ half - direction._y * offset, half + direction._x * offset };
        float32       minT      = -size * 4.0f;
        float32       maxT      = size * 4.0f;
        BrDropInternal::clipAxis( origin._x, direction._x, size, minT, maxT );
        BrDropInternal::clipAxis( origin._y, direction._y, size, minT, maxT );

        BrFlightPath path;
        path._start          = float2{ origin._x + direction._x * minT, origin._y + direction._y * minT };
        path._end            = float2{ origin._x + direction._x * maxT, origin._y + direction._y * maxT };
        path._speed          = MathUtil::max( 1.0f, settings._speed );
        path._altitude       = settings._altitude;
        path._jumpStartRatio = settings._jumpStartRatio;
        path._jumpEndRatio   = settings._jumpEndRatio;
        return path;
    }

    float32 BrFlightPath::computeLength() const
    {
        const float32 dx = _end._x - _start._x;
        const float32 dy = _end._y - _start._y;
        return MathUtil::sqrt( dx * dx + dy * dy );
    }

    float32 BrFlightPath::computeDuration() const { return computeLength() / MathUtil::max( 1.0f, _speed ); }

    float2 BrFlightPath::computePosition( float32 time ) const
    {
        const float32 duration = computeDuration();
        const float32 ratio    = duration > 0.0f ? MathUtil::saturate( time / duration ) : 1.0f;
        return float2{ MathUtil::lerp( _start._x, _end._x, ratio ), MathUtil::lerp( _start._y, _end._y, ratio ) };
    }

    bool BrFlightPath::canJump( float32 time ) const { return time >= computeJumpOpenTime() && time <= computeJumpCloseTime(); }

    BrSkydiver::BrSkydiver()
        : _settings{}
        , _position{}
        , _groundHeight{ 0.0f }
        , _stage{ BrFallStage::Landed }
    {
    }

    void BrSkydiver::initialize( const BrFallSettings& settings, const float3& position, float32 groundHeight )
    {
        _settings     = settings;
        _position     = position;
        _groundHeight = groundHeight;
        _stage        = position._y > groundHeight ? BrFallStage::FreeFall : BrFallStage::Landed;
        if ( _stage == BrFallStage::FreeFall && position._y - groundHeight <= settings._autoOpenHeight )
            _stage = BrFallStage::Parachute;
    }

    bool BrSkydiver::tryOpenParachute()
    {
        if ( _stage != BrFallStage::FreeFall )
            return false;
        _stage = BrFallStage::Parachute;
        return true;
    }

    void BrSkydiver::update( float32 deltaTime, const float2& steer )
    {
        const float2 direction = BrDropInternal::clampSteer( steer );
        // 단계 경계(낙하산이 펴지는 높이 · 땅)에서 걸음을 나눠 예측과 같은 자리에 내린다.
        while ( deltaTime > 0.0f && _stage != BrFallStage::Landed )
        {
            const bool    bFreeFall       = _stage == BrFallStage::FreeFall;
            const float32 verticalSpeed   = bFreeFall ? _settings._freeFallSpeed : _settings._parachuteSpeed;
            const float32 horizontalSpeed = bFreeFall ? _settings._freeFallHorizontalSpeed : _settings._parachuteHorizontalSpeed;
            const float32 stageFloor      = bFreeFall ? _groundHeight + _settings._autoOpenHeight : _groundHeight;
            const float32 timeToFloor     = MathUtil::max( 0.0f, ( _position._y - stageFloor ) / verticalSpeed );
            const float32 used            = MathUtil::min( deltaTime, timeToFloor );
            _position._x += direction._x * horizontalSpeed * used;
            _position._z += direction._y * horizontalSpeed * used;
            _position._y -= verticalSpeed * used;
            deltaTime -= used;
            if ( used < timeToFloor )
                break;
            _position._y = stageFloor;
            _stage       = bFreeFall ? BrFallStage::Parachute : BrFallStage::Landed;
        }
    }

    float3 BrSkydiver::predictLanding( const BrFallSettings& settings, const float3& position, float32 groundHeight, const float2& steer, BrFallStage stage )
    {
        const float2 direction = BrDropInternal::clampSteer( steer );
        float3       landing   = position;
        if ( stage == BrFallStage::Landed )
            return landing;
        float32 height = MathUtil::max( 0.0f, position._y - groundHeight );
        if ( stage == BrFallStage::FreeFall && height > settings._autoOpenHeight )
        {
            const float32 freeFallTime = ( height - settings._autoOpenHeight ) / settings._freeFallSpeed;
            landing._x += direction._x * settings._freeFallHorizontalSpeed * freeFallTime;
            landing._z += direction._y * settings._freeFallHorizontalSpeed * freeFallTime;
            height = settings._autoOpenHeight;
        }
        const float32 parachuteTime = height / settings._parachuteSpeed;
        landing._x += direction._x * settings._parachuteHorizontalSpeed * parachuteTime;
        landing._z += direction._y * settings._parachuteHorizontalSpeed * parachuteTime;
        landing._y = groundHeight;
        return landing;
    }
} // namespace sw
