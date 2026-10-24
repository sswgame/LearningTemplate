#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Rule/ActionRoom.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/Damage/DamageMath.h"
#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatEvents.h"

namespace sw
{
    SW_LOG_CALLER( "ActionRoom" );

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
        , _listProjectile{}
        , _listMonsterDef{}
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
    }

    void ActionRoom::clear()
    {
        _kind = ActionRoomKind::None;
        _listActor.clear();
        _listProjectile.clear();
        _listMonsterDef.clear();
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
            SW_LOG_ERROR( "Action room layout has no %# spawns - the fight starts without enemies (ActionRoom::setLayout)", pKindName );
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

    float32 ActionRoom::getBossHpFill() const
    {
        if ( _kind != ActionRoomKind::Boss )
            return 0.0f;
        // 보스 룸 적 전부의 HP 합이다. 보스가 하나면 그 보스의 비율이고, 쓰러진 적은 0이다.
        float32 hp    = 0.0f;
        float32 hpMax = 0.0f;
        for ( const Actor& actor : _listActor )
        {
            hp += actor._hp;
            hpMax += static_cast<float32>( _listMonsterDef[actor._defIndex]._stats._maxHp );
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
        if ( _kind == ActionRoomKind::None )
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
        for ( const Projectile& projectile : _listProjectile )
        {
            if ( projectile._bAlive == SW_FALSE )
                continue;
            pDbg->drawSphere( float3( projectile._position._x, 0.4f, projectile._position._y ), projectile._radius, float4( 1.0f, 0.9f, 0.2f, 1.0f ) );
        }
    }

    AABB ActionRoom::Projectile::bounds() const
    {
        return AABB{
            float3{_position._x - _radius, 0.0f, _position._y - _radius},
            float3{_position._x + _radius, 1.0f, _position._y + _radius}
        };
    }

    void ActionRoom::spawnMonster( const hashed_string& monsterID, const float2& position )
    {
        const int32 defIndex = findOrAddMonsterDef( monsterID );
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

    int32 ActionRoom::findOrAddMonsterDef( const hashed_string& monsterID )
    {
        for ( size_t defIndex = 0; defIndex < _listMonsterDef.size(); ++defIndex )
        {
            if ( hashed_string( _listMonsterDef[defIndex]._id.c_str() ) == monsterID )
                return static_cast<int32>( defIndex );
        }

        // 정의의 출처는 게임이 등록한 카탈로그 하나다. 코드 폴백이 없어 데이터 오타가 내장 수치로 조용히 바뀌지 않는다.
        const MonsterCatalog* pCatalog = game::findSubsystem<MonsterCatalog>();
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR( "Monster '%#': no MonsterCatalog is registered as a game service - the action room does not spawn it", monsterID.c_str() );
            return -1;
        }
        const MonsterDef* pFound = pCatalog->findMonster( monsterID );
        if ( pFound == nullptr )
        {
            SW_LOG_ERROR_ID( "Action.RoomMonsterNotInCatalog", "Monster '%#' is not in the monster catalog - the action room does not spawn it", monsterID.c_str() );
            return -1;
        }
        _listMonsterDef.push_back( *pFound );
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
            actor._hp -= DamageMath::applyArmor( _settings._playerAttackDamage, defense, 0.0f, 1.0f );
            if ( actor._hp <= 0.0f )
            {
                actor._hp     = 0.0f;
                actor._bAlive = SW_FALSE;
            }
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

            // 탄은 정의의 줄마다 한 발이다. 겨냥 방향을 `_angle`(라디안)만큼 돌린 쪽으로 난다(0은 겨냥 방향, π/2 는 (−y, x) 방향).
            const float2 aim = float2{ playerX - actor._position._x, playerY - actor._position._y }.normalize();
            for ( const MonsterShotDef& shot : def._listShot )
            {
                const float32 radian = shot._angle;
                const float32 cosine = MathUtil::cos( radian );
                const float32 sine   = MathUtil::sin( radian );

                Projectile projectile{};
                projectile._position    = actor._position;
                projectile._velocity._x = ( aim._x * cosine - aim._y * sine ) * shot._speed;
                projectile._velocity._y = ( aim._x * sine + aim._y * cosine ) * shot._speed;
                projectile._life.start( shot._lifeTime );
                projectile._radius = shot._radius;
                projectile._damage = shot._damage;
                projectile._bAlive = SW_TRUE;
                _listProjectile.push_back( projectile );
            }
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
            projectile._life.update( deltaTime );
            if ( projectile._life.isActive() == false )
                projectile._bAlive = SW_FALSE;
        }

        VectorUtil::removeIf( _listProjectile, []( const Projectile& projectile )
        { return projectile._bAlive == SW_FALSE; } );
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
        for ( Projectile& projectile : _listProjectile )
        {
            if ( projectile._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( hurt, kLayerPlayer, projectile.bounds(), kLayerProjectile, _layers ) == false )
                continue;
            outResult._damageToPlayer += projectile._damage;
            projectile._bAlive = SW_FALSE;
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
        if ( _kind == ActionRoomKind::Boss )
            outResult._bBossDefeated = SW_TRUE;

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

    void ActionRoom::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint8>( _kind );
        outArchive << static_cast<uint8>( _bCleared );
        StateArchiveUtil::writeCountdown( outArchive, _attackCooldown );
        StateArchiveUtil::writeCountdown( outArchive, _dashCooldown );
        StateArchiveUtil::writeCountdown( outArchive, _invulnerable );
        outArchive << static_cast<uint32>( _listMonsterDef.size() );
        for ( const MonsterDef& def : _listMonsterDef )
        {
            outArchive << string_view( def._id );
        }
        outArchive << static_cast<uint32>( _listActor.size() );
        for ( const Actor& actor : _listActor )
        {
            outArchive << actor._position;
            outArchive << actor._hp;
            StateArchiveUtil::writeCountdown( outArchive, actor._attackTimer );
            outArchive << actor._defIndex;
            outArchive << static_cast<uint8>( actor._bAlive );
        }
        outArchive << static_cast<uint32>( _listProjectile.size() );
        for ( const Projectile& projectile : _listProjectile )
        {
            outArchive << projectile._position;
            outArchive << projectile._velocity;
            StateArchiveUtil::writeCountdown( outArchive, projectile._life );
            outArchive << projectile._radius;
            outArchive << projectile._damage;
            outArchive << static_cast<uint8>( projectile._bAlive );
        }
    }

    bool ActionRoom::readState( Archive& archive )
    {
        // 사본에 읽고 끝까지 맞으면 바꾼다. 위치 정보와 충돌 레이어는 사본이 그대로 가지고 있다.
        ActionRoom restored = *this;
        uint8      kind     = 0;
        uint8      bCleared = SW_FALSE;
        archive >> kind;
        archive >> bCleared;
        const bool bTimerRead = StateArchiveUtil::readCountdown( archive, restored._attackCooldown ) &&
                                StateArchiveUtil::readCountdown( archive, restored._dashCooldown ) && StateArchiveUtil::readCountdown( archive, restored._invulnerable );
        const bool bHeadValid = bTimerRead && archive.isOk() && kind <= static_cast<uint8>( ActionRoomKind::Boss ) && bCleared <= SW_TRUE;
        if ( bHeadValid == false )
            return false;
        restored._kind     = static_cast<ActionRoomKind>( kind );
        restored._bCleared = bCleared;

        // 종 정의는 id 로 다시 찾는다. 같은 순서로 추가하므로 적의 종 인덱스가 그대로 맞는다.
        uint32 defCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, defCount ) == false )
            return false;
        restored._listMonsterDef.clear();
        for ( uint32 defIndex = 0; defIndex < defCount; ++defIndex )
        {
            hashed_string monsterID;
            if ( StateArchiveUtil::readName( archive, monsterID ) == false )
                return false;
            if ( restored.findOrAddMonsterDef( monsterID ) != static_cast<int32>( defIndex ) )
                return false;
        }

        uint32 actorCount = 0;
        // 적마다 위치(8) + 체력(4) + 사격 시간(4) + 종 인덱스(2) + 생존(1) 바이트
        if ( StateArchiveUtil::readCount( archive, 19, actorCount ) == false )
            return false;
        restored._listActor.assign( actorCount, Actor{} );
        for ( Actor& actor : restored._listActor )
        {
            uint8 bAlive = SW_FALSE;
            archive >> actor._position;
            archive >> actor._hp;
            if ( StateArchiveUtil::readCountdown( archive, actor._attackTimer ) == false )
                return false;
            archive >> actor._defIndex;
            archive >> bAlive;
            const bool bActorValid = archive.isOk() && actor._defIndex < defCount && bAlive <= SW_TRUE;
            if ( bActorValid == false )
                return false;
            actor._bAlive = bAlive;
        }

        uint32 projectileCount = 0;
        // 투사체마다 위치(8) + 속도(8) + 수명(4) + 반지름(4) + 피해(4) + 생존(1) 바이트
        if ( StateArchiveUtil::readCount( archive, 29, projectileCount ) == false )
            return false;
        restored._listProjectile.assign( projectileCount, Projectile{} );
        for ( Projectile& projectile : restored._listProjectile )
        {
            uint8 bAlive = SW_FALSE;
            archive >> projectile._position;
            archive >> projectile._velocity;
            if ( StateArchiveUtil::readCountdown( archive, projectile._life ) == false )
                return false;
            archive >> projectile._radius;
            archive >> projectile._damage;
            archive >> bAlive;
            if ( archive.isError() || bAlive > SW_TRUE )
                return false;
            projectile._bAlive = bAlive;
        }
        *this = std::move( restored );
        return true;
    }
} // namespace sw
