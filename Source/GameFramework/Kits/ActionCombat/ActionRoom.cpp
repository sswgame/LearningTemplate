#include "pch.h"

#include "GameFramework/Kits/ActionCombat/ActionRoom.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"

#include "GameFramework/Base/GameService.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 이 킷의 조절 값 — **한 자리에 모아 둔다.**
         * @details 여기 흩어져 있던 숫자 중 `0.85` 는 두 자리에 각각 적혀 있었다. `update()` 가
         *          대시할 때 `_dashCooldown = 0.85f` 로 넣고, 게이지를 만드는 `getDashFill()` 이
         *          **자기 몫으로 또 `kDashCd = 0.85f` 를 들고** 나눗셈을 했다. 값을 바꾸면
         *          한쪽만 따라가서 **게이지가 거짓말을 한다** — 쿨다운을 1.2 초로 늘리면
         *          게이지는 0.85 초에 이미 가득 찬다. 나머지도 같은 이유로 함께 모았다.
         */
        struct ActionRoomTuning
        {
            /** @brief 대시 쿨다운(초). `getDashFill()` 의 분모이기도 하다. */
            static constexpr float32 kDashCooldown = 0.85f;
            /** @brief 대시가 주는 무적 시간(초). */
            static constexpr float32 kDashInvulnerable = 0.22f;
            /** @brief 맞았을 때의 무적 시간(초). 대시가 주는 것보다 **길다.** */
            static constexpr float32 kHitInvulnerable = 0.7f;
            /** @brief 플레이어 공격 쿨다운(초). */
            static constexpr float32 kAttackCooldown = 0.28f;

            /** @brief 보스가 탄을 쏘는 간격(초)과 첫 발까지의 시간. */
            static constexpr float32 kBossFireInterval   = 1.6f;
            static constexpr float32 kBossFirstFireDelay = 1.2f;

            /** @brief 플레이어 공격이 주는 피해 — 보스는 덜 아프다. */
            static constexpr float32 kDamageToBoss  = 18.0f;
            static constexpr float32 kDamageToGrunt = 34.0f;
            /** @brief 플레이어가 받는 피해. */
            static constexpr int32 kDamageFromBoss       = 12;
            static constexpr int32 kDamageFromGrunt      = 8;
            static constexpr int32 kDamageFromProjectile = 10;
        };
    } // namespace

    ActionRoom::ActionRoom()
        : _kind{ ActionRoomKind::None }
        , _layers{}
        , _listActor{}
        , _listProjectile{}
        , _attackCooldown{ 0.0f }
        , _dashCooldown{ 0.0f }
        , _invulnTimer{ 0.0f }
        , _bossMaxHp{ 1.0f }
        , _bCleared{ SW_FALSE }
        , _reserved{ 0 }
    {
        _layers.resetDefaults();
        _layers.setLayerCollision( kLayerPlayer, kLayerPlayer, false );
        _layers.setLayerCollision( kLayerPlayerAtk, kLayerPlayer, false );
        _layers.setLayerCollision( kLayerPlayerAtk, kLayerProjectile, false );
        _layers.setLayerCollision( kLayerEnemy, kLayerEnemy, false );
        _layers.setLayerCollision( kLayerProjectile, kLayerEnemy, false );
    }

    void ActionRoom::clear()
    {
        _kind = ActionRoomKind::None;
        _listActor.clear();
        _listProjectile.clear();
        _attackCooldown = 0.0f;
        _dashCooldown   = 0.0f;
        _invulnTimer    = 0.0f;
        _bossMaxHp      = 1.0f;
        _bCleared       = SW_FALSE;
    }

    void ActionRoom::beginEntrance()
    {
        clear();
        _kind = ActionRoomKind::Hall;
        spawnGrunt( 6.0f, 3.0f );
        spawnGrunt( 8.0f, 5.0f );
    }

    void ActionRoom::beginHall()
    {
        clear();
        _kind = ActionRoomKind::Hall;
        spawnGrunt( 5.0f, 2.5f );
        spawnGrunt( 8.0f, 4.0f );
        spawnGrunt( 6.5f, 5.5f );
    }

    void ActionRoom::beginBoss()
    {
        clear();
        _kind = ActionRoomKind::Boss;
        spawnBoss( 7.0f, 4.0f );
    }

    float32 ActionRoom::getDashFill() const
    {
        if ( _dashCooldown <= 0.0f )
            return 1.0f;
        return MathUtil::saturate( 1.0f - ( _dashCooldown / ActionRoomTuning::kDashCooldown ) );
    }

    float32 ActionRoom::getBossHpFill() const
    {
        if ( _kind != ActionRoomKind::Boss || _bossMaxHp <= 0.0f )
            return 0.0f;
        for ( const Actor& actor : _listActor )
        {
            if ( actor._kind == ActorKind::Boss && actor._bAlive == SW_TRUE )
                return MathUtil::saturate( actor._hp / _bossMaxHp );
        }
        return 0.0f;
    }

    int32 ActionRoom::getAliveEnemyCount() const
    {
        int32 count = 0;
        for ( const Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_TRUE )
                ++count;
        }
        return count;
    }

    ActionRoomFrameResult ActionRoom::update( float32 deltaTime, const ActionRoomFrameInput& input )
    {
        ActionRoomFrameResult result{};
        if ( _kind == ActionRoomKind::None )
            return result;

        _attackCooldown = MathUtil::max( 0.0f, _attackCooldown - deltaTime );
        _dashCooldown   = MathUtil::max( 0.0f, _dashCooldown - deltaTime );
        _invulnTimer    = MathUtil::max( 0.0f, _invulnTimer - deltaTime );

        if ( input._bDashPressed == SW_TRUE && _dashCooldown <= 0.0f )
        {
            _dashCooldown = ActionRoomTuning::kDashCooldown;
            // **줄이지 않는다.** 그냥 대입하면 맞고 얻은 0.7 초짜리 무적이 대시 한 번에
            // 0.22 초로 **깎인다** — 대시가 피해를 덜 보게 해야 하는데 오히려 더 보게 했다.
            _invulnTimer         = MathUtil::max( _invulnTimer, ActionRoomTuning::kDashInvulnerable );
            result._bDashStarted = SW_TRUE;
        }

        tryPlayerAttack( input );
        updateActors( deltaTime, input._playerPos._x, input._playerPos._y );
        updateProjectiles( deltaTime );
        resolvePlayerHits( input._playerPos._x, input._playerPos._y, result );
        refreshCleared( result );
        return result;
    }

    void ActionRoom::drawDebug() const
    {
        if ( _kind == ActionRoomKind::None )
            return;

        DebugDrawQueue* pDbg = game::getService<DebugDrawQueue>();
        if ( pDbg == nullptr )
            return;

        for ( const Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            const float4 color = ( actor._kind == ActorKind::Boss )
                                   ? float4( 1.0f, 0.25f, 0.2f, 1.0f )
                                   : float4( 1.0f, 0.55f, 0.2f, 1.0f );
            pDbg->drawSphere( float3( actor._position._x, 0.5f, actor._position._y ), actor._radius, color );
        }
        for ( const Projectile& projectile : _listProjectile )
        {
            if ( projectile._bAlive == SW_FALSE )
                continue;
            pDbg->drawSphere( float3( projectile._position._x, 0.4f, projectile._position._y ), projectile._radius, float4( 1.0f, 0.9f, 0.2f, 1.0f ) );
        }
    }

    AABB ActionRoom::Actor::bounds() const
    {
        return AABB{
            float3{_position._x - _radius, 0.0f, _position._y - _radius},
            float3{_position._x + _radius, 1.0f, _position._y + _radius}
        };
    }

    AABB ActionRoom::Projectile::bounds() const
    {
        return AABB{
            float3{_position._x - _radius, 0.0f, _position._y - _radius},
            float3{_position._x + _radius, 1.0f, _position._y + _radius}
        };
    }

    void ActionRoom::spawnGrunt( float32 x, float32 y )
    {
        Actor a{};
        a._kind        = ActorKind::Grunt;
        a._position._x = x;
        a._position._y = y;
        a._hpMax       = 30.0f;
        a._hp          = a._hpMax;
        a._radius      = 0.32f;
        a._speed       = 1.8f;
        a._bAlive      = SW_TRUE;
        _listActor.push_back( a );
    }

    void ActionRoom::spawnBoss( float32 x, float32 y )
    {
        Actor a{};
        a._kind        = ActorKind::Boss;
        a._position._x = x;
        a._position._y = y;
        a._hpMax       = 220.0f;
        a._hp          = a._hpMax;
        a._radius      = 0.7f;
        a._speed       = 0.9f;
        a._attackTimer = ActionRoomTuning::kBossFirstFireDelay;
        a._bAlive      = SW_TRUE;
        _bossMaxHp     = a._hpMax;
        _listActor.push_back( a );
    }

    void ActionRoom::tryPlayerAttack( const ActionRoomFrameInput& input )
    {
        if ( _attackCooldown > 0.0f )
            return;
        if ( input._bAttackPressed == SW_FALSE )
            return;

        _attackCooldown = ActionRoomTuning::kAttackCooldown;
        const AABB atk  = playerAttackBox( input._playerPos._x, input._playerPos._y, input._facing );
        for ( Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( atk, kLayerPlayerAtk, actor.bounds(), kLayerEnemy, _layers ) == false )
                continue;
            const float32 dmg =
                ( actor._kind == ActorKind::Boss ) ? ActionRoomTuning::kDamageToBoss : ActionRoomTuning::kDamageToGrunt;
            actor._hp -= dmg;
            if ( actor._hp <= 0.0f )
            {
                actor._hp     = 0.0f;
                actor._bAlive = SW_FALSE;
            }
        }
    }

    void ActionRoom::updateActors( float32 deltaTime, float32 playerX, float32 playerY )
    {
        for ( Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;

            const float2 toPlayer = float2{ playerX - actor._position._x, playerY - actor._position._y }.normalize();
            actor._position._x += toPlayer._x * actor._speed * deltaTime;
            actor._position._y += toPlayer._y * actor._speed * deltaTime;

            if ( actor._kind != ActorKind::Boss )
                continue;

            actor._attackTimer -= deltaTime;
            if ( actor._attackTimer > 0.0f )
                continue;
            actor._attackTimer = ActionRoomTuning::kBossFireInterval;

            const float2 projDir = float2{ playerX - actor._position._x, playerY - actor._position._y }.normalize();

            Projectile projectile{};
            projectile._position._x = actor._position._x;
            projectile._position._y = actor._position._y;
            projectile._velocity._x = projDir._x * 4.5f;
            projectile._velocity._y = projDir._y * 4.5f;
            projectile._life        = 2.5f;
            projectile._radius      = 0.22f;
            projectile._bAlive      = SW_TRUE;
            _listProjectile.push_back( projectile );

            Projectile projectile2   = projectile;
            projectile2._velocity._x = -projDir._y * 3.2f;
            projectile2._velocity._y = projDir._x * 3.2f;
            projectile2._life        = 1.8f;
            _listProjectile.push_back( projectile2 );
        }
    }

    void ActionRoom::updateProjectiles( float32 deltaTime )
    {
        for ( Projectile& projectile : _listProjectile )
        {
            if ( projectile._bAlive == SW_FALSE )
                continue;
            projectile._position._x += projectile._velocity._x * deltaTime;
            projectile._position._y += projectile._velocity._y * deltaTime;
            projectile._life -= deltaTime;
            if ( projectile._life <= 0.0f )
                projectile._bAlive = SW_FALSE;
        }

        _listProjectile.erase(
            std::remove_if( _listProjectile.begin(), _listProjectile.end(),
                            []( const Projectile& proj )
        { return proj._bAlive == SW_FALSE; } ),
            _listProjectile.end() );
    }

    void ActionRoom::resolvePlayerHits( float32 playerX, float32 playerY, ActionRoomFrameResult& out )
    {
        if ( _invulnTimer > 0.0f )
            return;

        const AABB hurt = playerHurtBox( playerX, playerY );
        for ( const Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( hurt, kLayerPlayer, actor.bounds(), kLayerEnemy, _layers ) == false )
                continue;
            out._damageToPlayer +=
                ( actor._kind == ActorKind::Boss ) ? ActionRoomTuning::kDamageFromBoss : ActionRoomTuning::kDamageFromGrunt;
            _invulnTimer = ActionRoomTuning::kHitInvulnerable;
            return;
        }
        for ( Projectile& projectile : _listProjectile )
        {
            if ( projectile._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( hurt, kLayerPlayer, projectile.bounds(), kLayerProjectile, _layers ) == false )
                continue;
            out._damageToPlayer += ActionRoomTuning::kDamageFromProjectile;
            projectile._bAlive = SW_FALSE;
            _invulnTimer       = ActionRoomTuning::kHitInvulnerable;
            return;
        }
    }

    void ActionRoom::refreshCleared( ActionRoomFrameResult& out )
    {
        if ( _bCleared == SW_TRUE )
            return;
        if ( getAliveEnemyCount() > 0 )
            return;
        _bCleared              = SW_TRUE;
        out._bClearedThisFrame = SW_TRUE;
        if ( _kind == ActionRoomKind::Boss )
            out._bBossDefeated = SW_TRUE;
    }

    AABB ActionRoom::playerHurtBox( float32 x, float32 y ) const
    {
        constexpr float32 radius = 0.28f;
        return AABB{
            float3{x - radius, 0.0f, y - radius},
            float3{x + radius, 1.0f, y + radius}
        };
    }

    AABB ActionRoom::playerAttackBox( float32 x, float32 y, FacingDir facing ) const
    {
        float32 ox{ 0.0f };
        float32 oy{ 0.0f };
        switch ( facing )
        {
            case FacingDir::Up:
            {
                oy = -0.85f;
                break;
            }
            case FacingDir::Down:
            {
                oy = 0.85f;
                break;
            }
            case FacingDir::Left:
            {
                ox = -0.85f;
                break;
            }
            case FacingDir::Right:
            {
                ox = 0.85f;
                break;
            }
            default:
                break;
        }
        constexpr float32 radius  = 0.45f;
        const float32     centerX = x + ox;
        const float32     centerY = y + oy;
        return AABB{
            float3{centerX - radius, 0.0f, centerY - radius},
            float3{centerX + radius, 1.0f, centerY + radius}
        };
    }
} // namespace sw
