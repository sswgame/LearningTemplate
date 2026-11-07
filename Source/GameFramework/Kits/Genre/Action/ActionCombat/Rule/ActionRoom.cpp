#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Rule/ActionRoom.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatEvents.h"
#include "GameFramework/Runtime/Character/Combat/Damage/DamageMath.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Utility/DepletingValue.h"

namespace sw
{
    namespace
    {
        /** @brief 룸의 적 탄이 묻는 세계입니다. 벽이 없고 유도하지 않으며, 플레이어 맞음은 걸음 뒤에 룸이 본다(무적이 한 프레임 한 번을 정한다). */
        class ActionRoomProjectileQuery : public IProjectileWorld
        {
        public:
            bool findTargetPoint( const ProjectileState& /*projectile*/, float3& /*outPoint*/ ) const override { return false; }
            bool sweep( const ProjectileState& /*projectile*/, const float3& /*from*/, const float3& /*to*/, ProjectileHit& /*outHit*/ ) const override { return false; }
        };

        struct ActionRoomInternal
        {
            /**
             * @brief 종 @p def 의 사격 줄마다 탄 정의를 찾아 풉니다. 쏘지 않는 종은 빈 목록으로 true 입니다.
             * @details 탄 카탈로그는 몬스터 카탈로그와 같은 길(게임 서비스)로 받는다. 방에는 벽이 없어 수명도 사거리도 없는 탄은 영원히 남으므로 받지 않는다.
             */
            [[nodiscard]] static bool resolveShots( const MonsterDef& def, vector<ResolvedShot>& outListShot )
            {
                outListShot.clear();
                if ( def._listShot.empty() )
                    return true;
                const ProjectileCatalog* pProjectiles = game::findSubsystem<const ProjectileCatalog>();
                if ( pProjectiles == nullptr )
                {
                    SW_LOG_ERROR_ID( "ActionRoom.ShooterNoProjectileCatalog", "Monster '%#' shoots but no ProjectileCatalog is registered as a game service - the action room does not spawn it", def._id );
                    return false;
                }
                for ( const MonsterShotDef& shot : def._listShot )
                {
                    const ProjectileDef* pProjectile = pProjectiles->find( shot._projectileID );
                    if ( pProjectile == nullptr || ( pProjectile->_lifetime <= 0.0f && pProjectile->_range <= 0.0f ) )
                    {
                        SW_LOG_ERROR_ID( "ActionRoom.ShotProjectileUnusable", "Monster '%#' shoots projectile '%#' that is not in the projectile catalog or has neither lifetime nor range - the action room does not spawn it",
                                         def._id, shot._projectileID.c_str() );
                        return false;
                    }
                    outListShot.push_back( ResolvedShot::resolve( *pProjectile, {}, {} ) );
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ActionRoom::ActionRoom()
        : _site{}
        , _settings{}
        , _layout{}
        , _kind{ ActionRoomKind::None }
        , _layers{}
        , _listActor{}
        , _projectiles{}
        , _listMonsterDef{}
        , _listMonsterShot{}
        , _attackCooldown{}
        , _dashCooldown{}
        , _invulnerable{}
        , _bCleared{ SW_FALSE }
        , _reserved{ 0 }
    {
        _layers.resetDefaults();
        _layers.setLayerCollision( kLayerPlayer, kLayerPlayer, false );
        _layers.setLayerCollision( kLayerPlayerAtk, kLayerPlayer, false );
        _layers.setLayerCollision( kLayerPlayerAtk, kLayerProjectile, false );
        _layers.setLayerCollision( kLayerEnemy, kLayerEnemy, false );
        _layers.setLayerCollision( kLayerProjectile, kLayerEnemy, false );
        _projectiles.setPlaneNormal( float3{ 0.0f, 0.0f, 1.0f } ); // 룸 평면 (x, y)
    }

    void ActionRoom::clear()
    {
        _kind = ActionRoomKind::None;
        _listActor.clear();
        _projectiles.clear();
        _listMonsterDef.clear();
        _listMonsterShot.clear();
        _attackCooldown.clear();
        _dashCooldown.clear();
        _invulnerable.clear();
        _bCleared = SW_FALSE;
    }

    void ActionRoom::beginEntrance()
    {
        beginFight( ActionRoomKind::Hall, _layout._listEntranceSpawn, "entrance" );
    }

    void ActionRoom::beginHall()
    {
        beginFight( ActionRoomKind::Hall, _layout._listHallSpawn, "hall" );
    }

    void ActionRoom::beginBoss()
    {
        beginFight( ActionRoomKind::Boss, _layout._listBossSpawn, "boss" );
    }

    void ActionRoom::beginFight( ActionRoomKind kind, const vector<ActionRoomSpawnDef>& listSpawn, const utf8* pKindName )
    {
        startFight( kind );
        if ( listSpawn.empty() )
            SW_LOG_ERROR_ID( "Action.RoomLayoutNoSpawns", "Action room layout has no %# spawns - the fight starts without enemies (ActionRoom::setLayout)", pKindName );
        for ( const ActionRoomSpawnDef& spawn : listSpawn )
        {
            spawnMonster( spawn._monsterID, spawn._position );
        }
    }

    void ActionRoom::startFight( ActionRoomKind kind )
    {
        clear();
        _kind = kind;
        // 들어오면 문이 닫힌다. 진입이 닫은 것이다(오버월드는 이 존의 워프를 막는다).
        sendGateState( true, true );
    }

    void ActionRoom::onPlayerDefeated()
    {
        if ( _kind == ActionRoomKind::None )
            return;
        PlayerDefeatedInRoomEvent defeated;
        defeated._returnMapPath = _site._returnMapPath;
        GameEventUtil::send( defeated );
        // 싸움이 끝났다. 문을 열고 룸을 비운다(다시 들어오면 처음부터 시작한다).
        sendGateState( false, false );
        clear();
    }

    void ActionRoom::sendGateState( bool bLocked, bool bTriggered ) const
    {
        ClearGateStateChangedEvent gate;
        gate._zoneID     = _site._zoneID;
        gate._bLocked    = bLocked ? SW_TRUE : SW_FALSE;
        gate._bTriggered = bTriggered ? SW_TRUE : SW_FALSE;
        GameEventUtil::send( gate );
    }

    float32 ActionRoom::getDashFill() const
    {
        if ( _dashCooldown.isActive() == false )
            return 1.0f;
        return MathUtil::saturate( 1.0f - ( _dashCooldown.getRemaining() / _settings._dashCooldownSeconds ) );
    }

    float32 ActionRoom::getBossHPFill() const
    {
        if ( _kind != ActionRoomKind::Boss )
            return 0.0f;
        // 보스 룸 적 전부의 HP 합이다. 보스가 하나면 그 보스의 비율이고, 쓰러진 적은 0이다.
        float32 hp    = 0.0f;
        float32 hpMax = 0.0f;
        for ( const Actor& actor : _listActor )
        {
            hp += actor._hp;
            hpMax += static_cast<float32>( _listMonsterDef[actor._defIndex]._stats._maxHP );
        }
        if ( hpMax <= 0.0f )
            return 0.0f;
        return MathUtil::saturate( hp / hpMax );
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
        // 음수 시간은 적과 탄을 거꾸로 움직인다.
        if ( _kind == ActionRoomKind::None || deltaTime < 0.0f )
            return result;

        _attackCooldown.update( deltaTime );
        _dashCooldown.update( deltaTime );
        _invulnerable.update( deltaTime );

        if ( input._bDashPressed == SW_TRUE && _dashCooldown.isActive() == false )
        {
            _dashCooldown.start( _settings._dashCooldownSeconds );
            // **줄이지 않는다.** 그냥 대입하면 맞아서 얻은 0.7초짜리 무적이 대시 한 번에
            // 0.22초로 **깎인다.** 대시가 피해를 덜 받게 해야 하는데 오히려 더 받게 된다.
            _invulnerable.extendTo( _settings._dashInvulnerableSeconds );
            result._bDashStarted = SW_TRUE;
        }

        tryPlayerAttack( input );
        updateActors( deltaTime, input._playerPos._x, input._playerPos._y, result );
        updateProjectiles( deltaTime );
        resolvePlayerHits( input._playerPos._x, input._playerPos._y, result );
        refreshCleared( result );
        return result;
    }

    void ActionRoom::drawDebug() const
    {
        if ( _kind == ActionRoomKind::None )
            return;

        DebugDrawQueue* pDbg = game::findSubsystem<DebugDrawQueue>();
        if ( pDbg == nullptr )
            return;

        const float4 actorColor = ( _kind == ActionRoomKind::Boss ) ? float4( 1.0f, 0.25f, 0.2f, 1.0f ) : float4( 1.0f, 0.55f, 0.2f, 1.0f );
        for ( const Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            const float32 radius = _listMonsterDef[actor._defIndex]._stats._radius;
            pDbg->drawSphere( float3( actor._position._x, 0.5f, actor._position._y ), radius, actorColor );
        }
        for ( const ProjectileState& projectile : _projectiles.getProjectiles() )
        {
            pDbg->drawSphere( float3( projectile._position._x, 0.4f, projectile._position._y ), projectile._radius, float4( 1.0f, 0.9f, 0.2f, 1.0f ) );
        }
    }

    AABB ActionRoom::computeProjectileBounds( const ProjectileState& projectile )
    {
        const float3& position = projectile._position;
        const float32 radius   = projectile._radius;
        return AABB{
            float3{position._x - radius, 0.0f, position._y - radius},
            float3{position._x + radius, 1.0f, position._y + radius}
        };
    }

    void ActionRoom::spawnMonster( const hashed_string& monsterID, const float2& position )
    {
        const int32 defIndex = getOrCreateMonsterDef( monsterID );
        if ( defIndex < 0 )
            return;
        const MonsterDef& def = _listMonsterDef[static_cast<size_t>( defIndex )];

        Actor actor{};
        actor._position = position;
        actor._hp       = static_cast<float32>( def._stats._hp );
        actor._defIndex = static_cast<uint16>( defIndex );
        // 쏘는 종만 시간을 측정한다. 첫 발은 나타난 뒤 `_firstAttackDelay` 가 지나야 나간다.
        if ( def._listShot.empty() == false )
            actor._attackTimer.start( def._ai._firstAttackDelay );
        _listActor.push_back( actor );
    }

    int32 ActionRoom::getOrCreateMonsterDef( const hashed_string& monsterID )
    {
        for ( size_t defIndex = 0; defIndex < _listMonsterDef.size(); ++defIndex )
        {
            if ( hashed_string( _listMonsterDef[defIndex]._id.c_str() ) == monsterID )
                return static_cast<int32>( defIndex );
        }

        // 정의의 출처는 게임이 등록한 카탈로그 하나다. 코드 폴백이 없어 데이터 오타가 내장 수치로 조용히 바뀌지 않는다.
        const MonsterCatalog* pCatalog = game::findSubsystem<const MonsterCatalog>();
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "ActionRoom.MonsterNoMonsterCatalogRegistered", "Monster '%#': no MonsterCatalog is registered as a game service - the action room does not spawn it", monsterID.c_str() );
            return -1;
        }
        const MonsterDef* pFound = pCatalog->findMonster( monsterID );
        if ( pFound == nullptr )
        {
            SW_LOG_ERROR_ID( "Action.RoomMonsterNotInCatalog", "Monster '%#' is not in the monster catalog - the action room does not spawn it", monsterID.c_str() );
            return -1;
        }
        vector<ResolvedShot> listShot;
        if ( ActionRoomInternal::resolveShots( *pFound, listShot ) == false )
            return -1;
        _listMonsterDef.push_back( *pFound );
        _listMonsterShot.push_back( std::move( listShot ) );
        return static_cast<int32>( _listMonsterDef.size() - 1 );
    }

    void ActionRoom::tryPlayerAttack( const ActionRoomFrameInput& input )
    {
        if ( _attackCooldown.isActive() )
            return;
        if ( input._bAttackPressed == SW_FALSE )
            return;

        _attackCooldown.start( _settings._attackCooldownSeconds );
        const AABB atk = playerAttackBox( input._playerPos._x, input._playerPos._y, input._facing );
        for ( Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( atk, kLayerPlayerAtk, computeActorBounds( actor ), kLayerEnemy, _layers ) == false )
                continue;
            // 방어 공식은 유닛 스탯과 같다(고정 방어를 빼고 최소 1을 보장한다). 같은 몬스터 정의를 두 곳이 다르게 읽지 않는다.
            const float32 defense = static_cast<float32>( _listMonsterDef[actor._defIndex]._stats._def );
            if ( DepletingValue::take( actor._hp, DamageMath::applyArmor( _settings._playerAttackDamage, defense, 0.0f, 1.0f ) )._bDepleted == SW_TRUE )
                actor._bAlive = SW_FALSE;
        }
    }

    void ActionRoom::updateActors( float32 deltaTime, float32 playerX, float32 playerY, ActionRoomFrameResult& outResult )
    {
        for ( Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            const MonsterDef& def = _listMonsterDef[actor._defIndex];

            const float2 toPlayer = float2{ playerX - actor._position._x, playerY - actor._position._y }.normalize();
            actor._position._x += toPlayer._x * def._stats._speed * deltaTime;
            actor._position._y += toPlayer._y * def._stats._speed * deltaTime;

            if ( def._listShot.empty() )
                continue;
            actor._attackTimer.update( deltaTime );
            if ( actor._attackTimer.isActive() )
                continue;
            // 늦은 시간을 이어서 발사 빈도가 fps 에 묶이지 않게 한다(최대 한 간격까지만 이어 가므로, 멈춘 프레임 뒤에 몰아 쏘지 않는다).
            actor._attackTimer.restart( def._ai._attackCoolTime );
            ++outResult._enemyVolleyCount;

            // 탄은 정의의 줄마다 한 발이다. 겨냥 방향을 `_angle`(라디안)만큼 돌린 쪽으로 난다(0은 겨냥 방향, π/2 는 (−y, x) 방향). 수치는 그 줄의 탄 정의다.
            const float2                aim      = float2{ playerX - actor._position._x, playerY - actor._position._y }.normalize();
            const vector<ResolvedShot>& listShot = _listMonsterShot[actor._defIndex];
            for ( size_t shotIndex = 0; shotIndex < def._listShot.size(); ++shotIndex )
            {
                const float32 radian = def._listShot[shotIndex]._angle;
                const float32 cosine = MathUtil::cos( radian );
                const float32 sine   = MathUtil::sin( radian );
                const float3  origin{ actor._position._x, actor._position._y, 0.0f };
                const float3  direction{ aim._x * cosine - aim._y * sine, aim._x * sine + aim._y * cosine, 0.0f };
                // 룸 평면(법선 Z)에는 중력이 없다 — −Y 중력을 넘기면 평면 안에서 탄을 끌어내린다.
                (void)_projectiles.spawn( ProjectileState::launch( listShot[shotIndex], origin, direction, 0.0f ) ); // 탄 id 는 쓰지 않는다
            }
        }
    }

    void ActionRoom::updateProjectiles( float32 deltaTime )
    {
        const ActionRoomProjectileQuery query;
        (void)_projectiles.step( deltaTime, query ); // 사건은 쓰지 않는다
    }

    void ActionRoom::resolvePlayerHits( float32 playerX, float32 playerY, ActionRoomFrameResult& outResult )
    {
        if ( _invulnerable.isActive() )
            return;

        // 룸이 반환하는 피해는 방어를 빼기 전 값이다. 플레이어의 방어와 HP 는 게임이 가진다.
        const AABB hurt = playerHurtBox( playerX, playerY );
        for ( const Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( hurt, kLayerPlayer, computeActorBounds( actor ), kLayerEnemy, _layers ) == false )
                continue;
            outResult._damageToPlayer += _listMonsterDef[actor._defIndex]._stats._atk;
            _invulnerable.start( _settings._hitInvulnerableSeconds );
            return;
        }
        for ( ProjectileState& projectile : _projectiles.accessProjectiles() )
        {
            if ( queryOverlaps( hurt, kLayerPlayer, computeProjectileBounds( projectile ), kLayerProjectile, _layers ) == false )
                continue;
            outResult._damageToPlayer += static_cast<int32>( projectile._damage ); // 쏠 때 탄 정의의 피해다. 룸의 피해는 정수라 소수는 버린다
            projectile._bAlive = SW_FALSE;
            _projectiles.removeDead();
            _invulnerable.start( _settings._hitInvulnerableSeconds );
            return;
        }
    }

    void ActionRoom::refreshCleared( ActionRoomFrameResult& outResult )
    {
        if ( _bCleared == SW_TRUE )
            return;
        if ( getAliveEnemyCount() > 0 )
            return;
        _bCleared                    = SW_TRUE;
        outResult._bClearedThisFrame = SW_TRUE;
        // 적을 하나도 세우지 못한 보스 룸(배치 없음 · 카탈로그 오류)은 비어서 끝난 것이지 보스를 쓰러뜨린 것이 아니다.
        if ( _kind == ActionRoomKind::Boss && _listActor.empty() == false )
            outResult._bBossDefeated = SW_TRUE;
        // 이긴 방에서 남은 탄에 맞아 지지 않게 탄을 지운다(건전 · 하데스처럼 클리어가 탄을 거둔다).
        _projectiles.clear();

        // 클리어는 한 번만 알린다(위의 `_bCleared` 가 막는다). 결과를 알리고 문을 연다.
        RoomClearedEvent cleared;
        cleared._mapPath       = _site._mapPath;
        cleared._bBossDefeated = outResult._bBossDefeated;
        GameEventUtil::send( cleared );
        sendGateState( false, false );
    }

    AABB ActionRoom::computeActorBounds( const Actor& actor ) const
    {
        const float32 radius = _listMonsterDef[actor._defIndex]._stats._radius;
        return AABB{
            float3{actor._position._x - radius, 0.0f, actor._position._y - radius},
            float3{actor._position._x + radius, 1.0f, actor._position._y + radius}
        };
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
        }
        constexpr float32 radius  = 0.45f;
        const float32     centerX = x + ox;
        const float32     centerY = y + oy;
        return AABB{
            float3{centerX - radius, 0.0f, centerY - radius},
            float3{centerX + radius, 1.0f, centerY + radius}
        };
    }

    void ActionRoom::serializeState( StateIO& io )
    {
        io.enumValue( _kind, ActionRoomKind::Boss );
        SW_STATE_BITFLAG( io, _bCleared );
        io.value( _attackCooldown );
        io.value( _dashCooldown );
        io.value( _invulnerable );

        // 종 정의는 id 로 다시 찾는다. 같은 순서로 추가하므로 적의 종 인덱스가 그대로 맞는다.
        uint32 defCount = static_cast<uint32>( _listMonsterDef.size() );
        io.count( 4, defCount );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            _listMonsterDef.clear();
            _listMonsterShot.clear();
        }
        for ( uint32 defIndex = 0; defIndex < defCount; ++defIndex )
        {
            hashed_string monsterID = io.isWriting() ? hashed_string( _listMonsterDef[defIndex]._id ) : hashed_string{};
            io.value( monsterID );
            if ( io.isReading() && ( io.isOk() == false || io.check( getOrCreateMonsterDef( monsterID ) == static_cast<int32>( defIndex ) ) == false ) )
                return;
        }

        // 적마다 위치(8) + 체력(4) + 사격 시간(4) + 종 인덱스(2) + 생존(1) 바이트
        io.list( _listActor, 19, [&]( Actor& actor )
        {
            io.value( actor._position );
            io.value( actor._hp );
            io.value( actor._attackTimer );
            io.value( actor._defIndex );
            SW_STATE_BITFLAG( io, actor._bAlive );
            if ( io.check( actor._defIndex < defCount ) )
            {
                io.check( 0.0f <= actor._hp && actor._hp <= static_cast<float32>( _listMonsterDef[actor._defIndex]._stats._maxHP ) );
            }
        } );

        // 탄 수치(유한 · 음수 아닌 반지름 · 수명)는 시뮬레이션이 보고, 룸 평면과 피해는 여기서 본다.
        io.value( _projectiles );
        if ( io.isReading() )
        {
            for ( const ProjectileState& projectile : _projectiles.getProjectiles() )
            {
                io.check( 0.0f <= projectile._damage && projectile._position._z == 0.0f && projectile._velocity._z == 0.0f );
            }
        }
    }

} // namespace sw
