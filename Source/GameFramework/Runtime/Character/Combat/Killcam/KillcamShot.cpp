#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamShot.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"

namespace sw
{
    namespace
    {
        struct KillcamShotInternal
        {
            /** @brief 따라가기 구간 하나 — 탄이 @p fromDistance 에서 @p toDistance 까지 간다. */
            struct Segment
            {
                float32 _fromDistance{ 0.0f };
                float32 _toDistance{ 0.0f };
                float32 _followSeconds{ 0.0f };
            };

            static float32 computeFollowSeconds( float32 fromDistance, float32 toDistance, const KillcamSettings& settings )
            {
                return MathUtil::max( toDistance - fromDistance, 0.0f ) / MathUtil::max( settings._bulletSpeed, 0.5f );
            }

            /** @brief 탄을 뒤에서 따라가는 카메라입니다. */
            static KillcamPose makeFollowPose( const KillcamShot& shot, const KillcamSettings& settings, float32 distance, int32 nextHit )
            {
                KillcamPose pose;
                pose._bullet   = shot.getOrigin() + shot.getDirection() * distance;
                pose._position = pose._bullet - shot.getDirection() * settings._followDistance + float3::Up * settings._followHeight;
                pose._lookAt   = pose._bullet + shot.getDirection();
                pose._hitIndex = nextHit;
                pose._phase    = KillcamPhase::Follow;
                return pose;
            }

            /** @brief 명중을 옆 앞에서 보는 클로즈업입니다(탄 방향에서 비껴 맞은 자리와 탄이 함께 보인다). */
            static KillcamPose makeHoldPose( const KillcamShot& shot, const KillcamSettings& settings, int32 hitIndex )
            {
                const KillcamHit& hit  = shot.getHits()[static_cast<size_t>( hitIndex )];
                const float3      side = float3::Up.cross( shot.getDirection() ).normalizeOr( float3::Right, 1e-4f );
                KillcamPose       pose;
                pose._bullet   = hit._position;
                pose._position = hit._position - shot.getDirection() * ( settings._closeUpDistance * 0.7f ) + side * ( settings._closeUpDistance * 0.7f ) + float3::Up * settings._followHeight;
                pose._lookAt   = hit._position;
                pose._hitIndex = hitIndex;
                pose._phase    = KillcamPhase::HitHold;
                return pose;
            }

            /** @brief 끝 자리의 거리입니다(명중이 없으면 끝 자리, 있으면 마지막 명중). */
            static float32 computeEndDistance( const KillcamShot& shot )
            {
                return ( shot.getEnd() - shot.getOrigin() ).getLength();
            }
        };
    } // namespace

    KillcamHit::KillcamHit()
        : _target{}
        , _zone{}
        , _position{}
        , _distance{ 0.0f }
        , _bKilled{ SW_FALSE }
        , _bSevered{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    KillcamShot::KillcamShot()
        : _listHit{}
        , _time{ 0.0 }
        , _origin{}
        , _direction{ float3::Forward }
        , _end{}
    {
    }

    void KillcamShot::begin( const float3& origin, const float3& direction, float64 time )
    {
        _listHit.clear();
        _origin    = origin;
        _direction = direction.normalizeOr( float3::Forward, 1e-6f );
        _end       = origin;
        _time      = time;
    }

    bool KillcamShot::addHit( const KillcamHit& hit )
    {
        if ( _listHit.size() >= kMaxHits )
            return false;
        // 거리 순 자리 — 판정이 차례로 넣지 않아도(산탄 · 병렬 질의) 연출은 출발점에서 먼 쪽으로 간다.
        auto it = _listHit.begin();
        while ( it != _listHit.end() && it->_distance <= hit._distance )
        {
            ++it;
        }
        _listHit.insert( it, hit );
        return true;
    }

    bool KillcamShot::addProjectileEvent( const ProjectileEvent& event, GameObjectHandle target, bool bKilled )
    {
        if ( event._kind != ProjectileEvent::Kind::Hit )
            return false;
        KillcamHit hit;
        hit._position = event._hit._position;
        hit._target   = target;
        hit._zone     = event._hit._surface;
        hit._distance = ( event._hit._position - _origin ).getLength();
        hit._bKilled  = bKilled ? SW_TRUE : SW_FALSE;
        return addHit( hit );
    }

    void KillcamShot::finish( const float3& end )
    {
        _end = end;
    }

    bool KillcamShot::hasKill() const
    {
        for ( const KillcamHit& hit : _listHit )
        {
            if ( hit._bKilled == SW_TRUE )
                return true;
        }
        return false;
    }

    float32 KillcamShot::computeDuration( const KillcamShot& shot, const KillcamSettings& settings )
    {
        using Internal    = KillcamShotInternal;
        float32 seconds   = 0.0f;
        float32 travelled = 0.0f;
        for ( const KillcamHit& hit : shot.getHits() )
        {
            seconds += Internal::computeFollowSeconds( travelled, hit._distance, settings ) + settings._hitHoldSeconds;
            travelled = MathUtil::max( travelled, hit._distance );
        }
        if ( shot.getHits().empty() )
            seconds = Internal::computeFollowSeconds( 0.0f, Internal::computeEndDistance( shot ), settings );
        return MathUtil::min( seconds, settings._maxSeconds );
    }

    KillcamPose KillcamShot::sample( const KillcamShot& shot, const KillcamSettings& settings, float32 seconds )
    {
        const float32 duration = computeDuration( shot, settings );
        KillcamPose   pose     = sampleClamped( shot, settings, MathUtil::clamp( seconds, 0.0f, duration ) );
        // 길이 상한에서 잘린 연출도 끝은 끝이다 — 단계로 알린다(카메라는 그 자리를 유지).
        if ( seconds >= duration )
            pose._phase = KillcamPhase::Done;
        return pose;
    }

    KillcamPose KillcamShot::sampleClamped( const KillcamShot& shot, const KillcamSettings& settings, float32 seconds )
    {
        using Internal                    = KillcamShotInternal;
        const vector<KillcamHit>& listHit = shot.getHits();
        if ( listHit.empty() )
            return Internal::makeFollowPose( shot, settings, MathUtil::min( seconds * settings._bulletSpeed, Internal::computeEndDistance( shot ) ), -1 );
        float32 left      = seconds;
        float32 travelled = 0.0f;
        for ( int32 hitIndex = 0; hitIndex < static_cast<int32>( listHit.size() ); ++hitIndex )
        {
            const KillcamHit& hit           = listHit[static_cast<size_t>( hitIndex )];
            const float32     followSeconds = Internal::computeFollowSeconds( travelled, hit._distance, settings );
            if ( left < followSeconds )
                return Internal::makeFollowPose( shot, settings, travelled + left * settings._bulletSpeed, hitIndex );
            left -= followSeconds;
            const bool bLastHit = hitIndex + 1 == static_cast<int32>( listHit.size() );
            if ( left < settings._hitHoldSeconds || bLastHit )
                return Internal::makeHoldPose( shot, settings, hitIndex );
            left -= settings._hitHoldSeconds;
            travelled = MathUtil::max( travelled, hit._distance );
        }
        return Internal::makeHoldPose( shot, settings, static_cast<int32>( listHit.size() ) - 1 );
    }
} // namespace sw
