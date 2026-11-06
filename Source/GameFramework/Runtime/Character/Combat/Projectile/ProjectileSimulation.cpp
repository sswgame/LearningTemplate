#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"

#include "Core/Math/DeterministicMathUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct ProjectileSimulationInternal
        {
            static constexpr float32 kTiny          = 1.0e-5f;
            static constexpr float32 kBounceLift    = 1.0e-3f; ///< 튕긴 탄을 표면에서 띄우는 거리(같은 표면에 다시 닿지 않게)
            static constexpr uint8   kFlagAlive     = 1u;
            static constexpr uint8   kFlagReflected = 2u;

            /** @brief @p value 에서 평면 법선 @p normal(단위 또는 0) 성분을 뺍니다. */
            static float3 constrain( const float3& value, const float3& normal )
            {
                return normal.getLengthSquared() > 0.0f ? value - normal * value.dot( normal ) : value;
            }

            /**
             * @brief @p direction 을 @p desired 쪽으로 최대 @p maxRadian 만큼 돌립니다(구면 보간). 둘 다 단위 벡터여야 합니다.
             * @details 정반대면 돌 축이 정해지지 않는다. 평면 제약이 있으면 그 법선을 축으로 돌고(카트의 뒤쪽 대상), 없으면 그대로 간다.
             */
            static float3 rotateToward( const float3& direction, const float3& desired, float32 maxRadian, const float3& planeNormal )
            {
                const float32 cosAngle = MathUtil::clamp( direction.dot( desired ), -1.0f, 1.0f );
                const float32 angle    = DeterministicMathUtil::acos( cosAngle );
                if ( angle <= maxRadian || angle < kTiny )
                    return desired;
                const float32 sinAngle = DeterministicMathUtil::sin( angle );
                if ( sinAngle < kTiny )
                {
                    if ( planeNormal.getLengthSquared() <= 0.0f )
                        return direction;
                    // 평면 안에서 법선을 축으로 돈다: d' = d cos θ + (n × d) sin θ
                    const float3 side = planeNormal.cross( direction );
                    return ( direction * DeterministicMathUtil::cos( maxRadian ) + side * DeterministicMathUtil::sin( maxRadian ) ).normalizeOr( direction, kTiny );
                }
                const float32 ratio    = maxRadian / angle;
                const float32 fromPart = DeterministicMathUtil::sin( ( 1.0f - ratio ) * angle ) / sinAngle;
                const float32 toPart   = DeterministicMathUtil::sin( ratio * angle ) / sinAngle;
                return ( direction * fromPart + desired * toPart ).normalizeOr( direction, kTiny );
            }

            static bool isFinite( const float3& value ) { return MathUtil::isFinite( value._x ) && MathUtil::isFinite( value._y ) && MathUtil::isFinite( value._z ); }

            static void pushEvent( vector<ProjectileEvent>& outListEvent, const ProjectileState& projectile, const ProjectileHit& hit, ProjectileEvent::Kind kind )
            {
                ProjectileEvent event;
                event._projectile = projectile;
                event._hit        = hit;
                event._kind       = kind;
                outListEvent.push_back( event );
            }

            /** @brief 탄 하나를 한 걸음 진행합니다. */
            static void stepOne( ProjectileState& projectile, float32 deltaTime, const float3& planeNormal, const IProjectileWorld& world, vector<ProjectileEvent>& outListEvent )
            {
                projectile._previousPosition = projectile._position;
                projectile._age += deltaTime;
                if ( projectile._lifetime > 0.0f && projectile._age >= projectile._lifetime )
                {
                    projectile._bAlive = SW_FALSE;
                    ProjectileHit none;
                    none._position = projectile._position;
                    pushEvent( outListEvent, projectile, none, ProjectileEvent::Kind::Expired );
                    return;
                }

                // 유도: 속력은 그대로 두고 방향만 꺾는다.
                float3 targetPoint{};
                if ( projectile._turnRate > 0.0f && world.findTargetPoint( projectile, targetPoint ) )
                {
                    const float32 speed = projectile.getSpeed();
                    if ( speed > kTiny )
                    {
                        const float3 direction = projectile._velocity / speed;
                        const float3 desired   = constrain( targetPoint - projectile._position, planeNormal ).normalizeOr( direction, kTiny );
                        projectile._velocity   = rotateToward( direction, desired, projectile._turnRate * deltaTime, planeNormal ) * speed;
                    }
                }
                if ( projectile._drag > 0.0f )
                    projectile._velocity = projectile._velocity * MathUtil::max( 0.0f, 1.0f - projectile._drag * deltaTime );
                projectile._velocity._y -= projectile._gravity * deltaTime;
                projectile._velocity = constrain( projectile._velocity, planeNormal );

                // 사거리 끝에서 선분을 자른다. 자른 걸음은 남은 거리를 정확히 채워 끝난다(실수 합이 사거리에 못 미쳐 남는 탄이 없다).
                float3     segment   = projectile._velocity * deltaTime;
                bool       bRangeEnd = false;
                const bool bRanged   = projectile._range > 0.0f;
                if ( bRanged )
                {
                    const float32 length = segment.getLength();
                    const float32 remain = MathUtil::max( 0.0f, projectile._range - projectile._travelled );
                    if ( length >= remain )
                    {
                        segment   = length > kTiny ? segment * ( remain / length ) : float3{};
                        bRangeEnd = true;
                    }
                }
                const float3  from        = projectile._position;
                const float3  to          = from + segment;
                const float32 segmentSize = segment.getLength();

                ProjectileHit hit;
                if ( world.sweep( projectile, from, to, hit ) )
                {
                    projectile._travelled += float3::getDistance( from, hit._position );
                    projectile._position = hit._position;
                    if ( hit._kind == ProjectileHitKind::Surface && projectile._bounceLeft > 0 )
                    {
                        --projectile._bounceLeft;
                        projectile._velocity = constrain( float3::reflect( projectile._velocity, hit._normal ) * projectile._restitution, planeNormal );
                        projectile._position = hit._position + hit._normal * kBounceLift;
                        projectile._lastHit  = -1;
                        pushEvent( outListEvent, projectile, hit, ProjectileEvent::Kind::Bounced );
                        return;
                    }
                    if ( hit._kind == ProjectileHitKind::Target && projectile._pierceLeft > 0 )
                    {
                        --projectile._pierceLeft;
                        projectile._lastHit = hit._other;
                        pushEvent( outListEvent, projectile, hit, ProjectileEvent::Kind::Hit );
                        return; // shortcut: 남은 선분은 다음 걸음에 간다(클래스 설명)
                    }
                    projectile._bAlive = SW_FALSE;
                    pushEvent( outListEvent, projectile, hit, ProjectileEvent::Kind::Hit );
                    return;
                }
                projectile._position  = to;
                projectile._travelled = bRangeEnd ? projectile._range : projectile._travelled + segmentSize;
                if ( bRangeEnd )
                {
                    projectile._bAlive = SW_FALSE;
                    ProjectileHit none;
                    none._position = projectile._position;
                    pushEvent( outListEvent, projectile, none, ProjectileEvent::Kind::Expired );
                }
            }
        };
    } // namespace

    ProjectileState::ProjectileState()
        : _defID{}
        , _position{}
        , _previousPosition{}
        , _velocity{}
        , _damage{ 0.0f }
        , _age{ 0.0f }
        , _travelled{ 0.0f }
        , _lifetime{ 0.0f }
        , _range{ 0.0f }
        , _gravity{ 0.0f }
        , _drag{ 0.0f }
        , _turnRate{ 0.0f }
        , _radius{ 0.0f }
        , _blastRadius{ 0.0f }
        , _restitution{ 1.0f }
        , _id{ 0 }
        , _payload{ 0 }
        , _owner{ -1 }
        , _team{ -1 }
        , _target{ -1 }
        , _lastHit{ -1 }
        , _pierceLeft{ 0 }
        , _bounceLeft{ 0 }
        , _bAlive{ SW_TRUE }
        , _bReflected{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ProjectileState ProjectileState::launch( const ResolvedShot& shot, const float3& origin, const float3& direction, float32 gravity )
    {
        ProjectileState projectile;
        projectile._defID            = shot._projectileID;
        projectile._position         = origin;
        projectile._previousPosition = origin;
        projectile._velocity         = direction * shot._speed;
        projectile._damage           = shot._damage;
        projectile._lifetime         = shot._lifetime;
        projectile._range            = shot._range;
        projectile._gravity          = gravity * shot._gravityScale;
        projectile._drag             = shot._drag;
        projectile._turnRate         = MathUtil::toRadian( shot._homingRate );
        projectile._radius           = shot._radius;
        projectile._blastRadius      = shot._blastRadius;
        projectile._restitution      = shot._restitution;
        projectile._pierceLeft       = shot._pierceCount;
        projectile._bounceLeft       = shot._bounceCount;
        return projectile;
    }

    void ProjectileState::deflect( int32 team, float32 speedScale, float32 damageScale )
    {
        _velocity   = _velocity * -speedScale;
        _damage     = _damage * damageScale;
        _team       = team;
        _lastHit    = -1;
        _bReflected = SW_TRUE;
    }

    ProjectileSimulation::ProjectileSimulation()
        : _listProjectile{}
        , _planeNormal{}
        , _nextID{ 1 }
    {
    }

    void ProjectileSimulation::setPlaneNormal( const float3& normal )
    {
        _planeNormal = normal.normalizeOr( float3{}, ProjectileSimulationInternal::kTiny );
    }

    uint32 ProjectileSimulation::spawn( const ProjectileState& projectile )
    {
        ProjectileState added = projectile;
        added._id             = _nextID++;
        added._bAlive         = SW_TRUE;
        added._velocity       = ProjectileSimulationInternal::constrain( added._velocity, _planeNormal );
        _listProjectile.push_back( added );
        return added._id;
    }

    void ProjectileSimulation::step( float32 deltaTime, const IProjectileWorld& world, vector<ProjectileEvent>& outListEvent )
    {
        if ( deltaTime <= 0.0f )
            return;
        for ( ProjectileState& projectile : _listProjectile )
        {
            if ( projectile._bAlive == SW_TRUE )
                ProjectileSimulationInternal::stepOne( projectile, deltaTime, _planeNormal, world, outListEvent );
        }
        removeDead();
    }

    void ProjectileSimulation::removeDead()
    {
        size_t writeIndex = 0;
        for ( size_t readIndex = 0; readIndex < _listProjectile.size(); ++readIndex )
        {
            if ( _listProjectile[readIndex]._bAlive == SW_TRUE )
                _listProjectile[writeIndex++] = _listProjectile[readIndex];
        }
        _listProjectile.resize( writeIndex );
    }

    void ProjectileSimulation::clear()
    {
        _listProjectile.clear();
    }

    void ProjectileSimulation::writeState( Archive& outArchive ) const
    {
        // 쓰는 쪽이 끝냈지만 아직 빼지 않은 탄(`removeDead` 전)은 싣지 않는다 — 읽기는 살아 있는 탄만 받는다.
        uint32 aliveCount = 0;
        for ( const ProjectileState& projectile : _listProjectile )
        {
            aliveCount += projectile._bAlive == SW_TRUE ? 1u : 0u;
        }
        outArchive << _nextID;
        outArchive << aliveCount;
        for ( const ProjectileState& projectile : _listProjectile )
        {
            if ( projectile._bAlive == SW_FALSE )
                continue;
            const uint8 flags = static_cast<uint8>( ( projectile._bAlive == SW_TRUE ? ProjectileSimulationInternal::kFlagAlive : 0u ) |
                                                    ( projectile._bReflected == SW_TRUE ? ProjectileSimulationInternal::kFlagReflected : 0u ) );
            StateArchiveUtil::writeName( outArchive, projectile._defID );
            outArchive << projectile._position;
            outArchive << projectile._velocity;
            outArchive << projectile._damage;
            outArchive << projectile._age;
            outArchive << projectile._travelled;
            outArchive << projectile._lifetime;
            outArchive << projectile._range;
            outArchive << projectile._gravity;
            outArchive << projectile._drag;
            outArchive << projectile._turnRate;
            outArchive << projectile._radius;
            outArchive << projectile._blastRadius;
            outArchive << projectile._restitution;
            outArchive << projectile._id;
            outArchive << projectile._payload;
            outArchive << projectile._owner;
            outArchive << projectile._team;
            outArchive << projectile._target;
            outArchive << projectile._lastHit;
            outArchive << projectile._pierceLeft;
            outArchive << projectile._bounceLeft;
            outArchive << flags;
        }
    }

    bool ProjectileSimulation::readState( Archive& archive )
    {
        using Internal = ProjectileSimulationInternal;
        uint32 nextID  = 0;
        uint32 count   = 0;
        archive >> nextID;
        // 탄마다 이름(4) + 위치 · 속도(24) + 실수 11 개(44) + id · 반응 번호(8) + 정수 6 개(24) + 깃발(1) 바이트
        if ( archive.isError() || nextID == 0 || archive.readCount( 105, count ) == false )
            return false;
        vector<ProjectileState> listProjectile( count );
        for ( ProjectileState& projectile : listProjectile )
        {
            uint8 flags = 0;
            if ( StateArchiveUtil::readName( archive, projectile._defID ) == false )
                return false;
            archive >> projectile._position;
            archive >> projectile._velocity;
            archive >> projectile._damage;
            archive >> projectile._age;
            archive >> projectile._travelled;
            archive >> projectile._lifetime;
            archive >> projectile._range;
            archive >> projectile._gravity;
            archive >> projectile._drag;
            archive >> projectile._turnRate;
            archive >> projectile._radius;
            archive >> projectile._blastRadius;
            archive >> projectile._restitution;
            archive >> projectile._id;
            archive >> projectile._payload;
            archive >> projectile._owner;
            archive >> projectile._team;
            archive >> projectile._target;
            archive >> projectile._lastHit;
            archive >> projectile._pierceLeft;
            archive >> projectile._bounceLeft;
            archive >> flags;
            const bool bFinite = Internal::isFinite( projectile._position ) && Internal::isFinite( projectile._velocity ) && MathUtil::isFinite( projectile._damage ) &&
                                 MathUtil::isFinite( projectile._age ) && MathUtil::isFinite( projectile._travelled ) && MathUtil::isFinite( projectile._lifetime ) &&
                                 MathUtil::isFinite( projectile._range ) && MathUtil::isFinite( projectile._gravity ) && MathUtil::isFinite( projectile._drag ) &&
                                 MathUtil::isFinite( projectile._turnRate ) && MathUtil::isFinite( projectile._radius ) && MathUtil::isFinite( projectile._blastRadius ) &&
                                 MathUtil::isFinite( projectile._restitution );
            const bool bRange = 0.0f <= projectile._age && 0.0f <= projectile._travelled && 0.0f <= projectile._lifetime && 0.0f <= projectile._range &&
                                ( projectile._range <= 0.0f || projectile._travelled <= projectile._range ) && 0.0f <= projectile._drag && 0.0f <= projectile._turnRate &&
                                0.0f <= projectile._radius && 0.0f <= projectile._blastRadius && 0.0f <= projectile._restitution && projectile._restitution <= 1.0f;
            // 쓰는 때(걸음 사이)에는 끝난 탄이 빠져 있다 — 살아 있지 않은 탄은 깨진 바이트다.
            const bool bCounts = 0 < projectile._id && projectile._id < nextID && 0 <= projectile._pierceLeft && 0 <= projectile._bounceLeft &&
                                 ( flags & Internal::kFlagAlive ) != 0u && flags <= ( Internal::kFlagAlive | Internal::kFlagReflected );
            if ( archive.isError() || bFinite == false || bRange == false || bCounts == false )
                return false;
            projectile._previousPosition = projectile._position;
            projectile._bAlive           = ( flags & Internal::kFlagAlive ) != 0u ? SW_TRUE : SW_FALSE;
            projectile._bReflected       = ( flags & Internal::kFlagReflected ) != 0u ? SW_TRUE : SW_FALSE;
        }
        _nextID         = nextID;
        _listProjectile = std::move( listProjectile );
        return true;
    }
} // namespace sw
