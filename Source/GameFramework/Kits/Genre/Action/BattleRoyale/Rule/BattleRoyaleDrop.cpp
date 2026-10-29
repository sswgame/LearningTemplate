#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleDrop.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"

namespace sw
{
    namespace
    {
        struct BattleRoyaleDropInternal
        {
            /** @brief 길이를 1로 자른 조종 방향을 반환합니다. */
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
    bool BattleRoyaleFlightPath::makeRandom( const BattleRoyaleFlightDef& settings, float32 mapSize, uint32 seed, BattleRoyaleFlightPath& outPath )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR( "BattleRoyaleFlightPath: refused flight settings - %#", rangeError.c_str() );
            return false;
        }
        if ( mapSize < 1.0f )
        {
            SW_LOG_ERROR( "BattleRoyaleFlightPath: refused map size %# (needs >= 1)", mapSize );
            return false;
        }
        GameRandom    random{ seed };
        const float32 size      = mapSize;
        const float32 half      = size * 0.5f;
        const float32 angle     = random.nextFloat() * MathUtil::kPi * 2.0f;
        const float32 offset    = random.nextRange( -1.0f, 1.0f ) * settings._offsetRatio * half;
        const float2  direction = float2{ MathUtil::cos( angle ), MathUtil::sin( angle ) };
        const float2  origin    = float2{ half - direction._y * offset, half + direction._x * offset };
        float32       minT      = -size * 4.0f;
        float32       maxT      = size * 4.0f;
        BattleRoyaleDropInternal::clipAxis( origin._x, direction._x, size, minT, maxT );
        BattleRoyaleDropInternal::clipAxis( origin._y, direction._y, size, minT, maxT );

        outPath                 = BattleRoyaleFlightPath{};
        outPath._start          = float2{ origin._x + direction._x * minT, origin._y + direction._y * minT };
        outPath._end            = float2{ origin._x + direction._x * maxT, origin._y + direction._y * maxT };
        outPath._speed          = settings._speed;
        outPath._altitude       = settings._altitude;
        outPath._jumpStartRatio = settings._jumpStartRatio;
        outPath._jumpEndRatio   = settings._jumpEndRatio;
        return true;
    }

    float32 BattleRoyaleFlightPath::computeLength() const
    {
        const float32 dx = _end._x - _start._x;
        const float32 dy = _end._y - _start._y;
        return MathUtil::sqrt( dx * dx + dy * dy );
    }

    float32 BattleRoyaleFlightPath::computeDuration() const
    {
        return computeLength() / MathUtil::max( 1.0f, _speed );
    }

    float2 BattleRoyaleFlightPath::computePosition( float32 time ) const
    {
        const float32 duration = computeDuration();
        const float32 ratio    = duration > 0.0f ? MathUtil::saturate( time / duration ) : 1.0f;
        return float2{ MathUtil::lerp( _start._x, _end._x, ratio ), MathUtil::lerp( _start._y, _end._y, ratio ) };
    }

    bool BattleRoyaleFlightPath::canJump( float32 time ) const
    {
        return time >= computeJumpOpenTime() && time <= computeJumpCloseTime();
    }

    BattleRoyaleSkydiver::BattleRoyaleSkydiver()
        : _settings{}
        , _position{}
        , _groundHeight{ 0.0f }
        , _stage{ BattleRoyaleFallStage::Landed }
    {
    }

    void BattleRoyaleSkydiver::initialize( const BattleRoyaleFallDef& settings, const float3& position, float32 groundHeight )
    {
        _settings     = settings;
        _position     = position;
        _groundHeight = groundHeight;
        _stage        = position._y > groundHeight ? BattleRoyaleFallStage::FreeFall : BattleRoyaleFallStage::Landed;
        if ( _stage == BattleRoyaleFallStage::FreeFall && position._y - groundHeight <= settings._autoOpenHeight )
            _stage = BattleRoyaleFallStage::Parachute;
    }

    bool BattleRoyaleSkydiver::tryOpenParachute()
    {
        if ( _stage != BattleRoyaleFallStage::FreeFall )
            return false;
        _stage = BattleRoyaleFallStage::Parachute;
        return true;
    }

    void BattleRoyaleSkydiver::update( float32 deltaTime, const float2& steer )
    {
        const float2 direction = BattleRoyaleDropInternal::clampSteer( steer );
        // 단계 경계(낙하산이 펴지는 높이, 땅)에서 걸음을 나눠, 예측과 같은 위치에 내리게 한다.
        while ( deltaTime > 0.0f && _stage != BattleRoyaleFallStage::Landed )
        {
            const bool    bFreeFall       = _stage == BattleRoyaleFallStage::FreeFall;
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
            _stage       = bFreeFall ? BattleRoyaleFallStage::Parachute : BattleRoyaleFallStage::Landed;
        }
    }

    float3 BattleRoyaleSkydiver::predictLanding( const BattleRoyaleFallDef& settings, const float3& position, float32 groundHeight, const float2& steer, BattleRoyaleFallStage stage )
    {
        const float2 direction = BattleRoyaleDropInternal::clampSteer( steer );
        float3       landing   = position;
        if ( stage == BattleRoyaleFallStage::Landed )
            return landing;
        float32 height = MathUtil::max( 0.0f, position._y - groundHeight );
        if ( stage == BattleRoyaleFallStage::FreeFall && height > settings._autoOpenHeight )
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
