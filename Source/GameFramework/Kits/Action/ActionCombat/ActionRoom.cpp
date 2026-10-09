#include "pch.h"

#include "GameFramework/Kits/Action/ActionCombat/ActionRoom.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/Damage/DamageMath.h"
#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Action/ActionCombat/ActionCombatEvents.h"

namespace sw
{
    SW_LOG_CALLER( "ActionRoom" );

    namespace
    {
        /**
         * @brief 이 킷의 플레이어 쪽 조절 값입니다. **한 자리에 모아 둡니다.** 적의 수치는 몬스터 정의(`MonsterDef`)에 있습니다.
         * @details 같은 값을 두 자리에 적지 말 것 — 예컨대 대시 쿨다운은 `update()` 가 넣는 값이자
         *          `getDashFill()` 의 분모라, 한쪽만 바꾸면 **게이지가 거짓말을 합니다**(쿨다운을 1.2 초로
         *          늘렸는데 게이지는 0.85 초에 가득 차는 식).
         */
        struct ActionRoomTuning
        {
            /** @brief 대시 쿨다운(초)입니다. `getDashFill()` 의 분모이기도 합니다. */
            static constexpr float32 kDashCooldown = 0.85f;
            /** @brief 대시가 주는 무적 시간(초)입니다. */
            static constexpr float32 kDashInvulnerable = 0.22f;
            /** @brief 맞았을 때의 무적 시간(초)입니다. 대시가 주는 것보다 **깁니다.** */
            static constexpr float32 kHitInvulnerable = 0.7f;
            /** @brief 플레이어 공격 쿨다운(초)입니다. */
            static constexpr float32 kAttackCooldown = 0.28f;
            /** @brief 플레이어 공격 한 번의 피해입니다 — 적의 방어(`MonsterDef::_def`)를 빼고 들어간다(내장 그런트 방어 0 → 34, 보스 방어 16 → 18). */
            static constexpr float32 kPlayerAttackDamage = 34.0f;
        };

        /** @brief 룸에 세울 적 하나 — 종 id 와 자리(m)입니다. */
        struct ActionRoomSpawnDef
        {
            const utf8* _pMonsterId;
            float32     _x;
            float32     _y;
        };

        struct ActionRoomInternal
        {
            static constexpr const utf8* kGruntId = "grunt";
            static constexpr const utf8* kBossId  = "boss";

            /** @brief 룸 종류마다의 배치입니다. 쓰는 게임이 생기면 맵의 스폰 지점으로 옮길 자리다. */
            static constexpr ActionRoomSpawnDef kArrEntranceSpawn[] = {
                {kGruntId, 6.0f, 3.0f},
                {kGruntId, 8.0f, 5.0f}
            };
            static constexpr ActionRoomSpawnDef kArrHallSpawn[] = {
                {kGruntId, 5.0f, 2.5f},
                {kGruntId, 8.0f, 4.0f},
                {kGruntId, 6.5f, 5.5f}
            };
            static constexpr ActionRoomSpawnDef kArrBossSpawn[] = {
                { kBossId, 7.0f, 4.0f }
            };

            /**
             * @brief 카탈로그가 없거나 그 종이 없을 때 쓰는 내장 정의입니다 — 데이터 없이도 룸이 돈다. 값은 옛 상수 그대로다
             *        (`ActionCombatTest.ActionRoomEnemyNumbersStayTheSame`).
             * @return @p monsterId 가 내장 종이 아니면 false
             */
            static bool findBuiltInMonster( const hashed_string& monsterId, MonsterDef& outDef )
            {
                if ( monsterId == hashed_string( kGruntId ) )
                {
                    outDef         = MonsterDef{};
                    outDef._id     = kGruntId;
                    outDef._name   = "Grunt";
                    outDef._hp     = 30;
                    outDef._maxHp  = 30;
                    outDef._atk    = 8;
                    outDef._def    = 0;
                    outDef._speed  = 1.8f;
                    outDef._radius = 0.32f;
                    return true;
                }
                if ( monsterId == hashed_string( kBossId ) )
                {
                    outDef                   = MonsterDef{};
                    outDef._id               = kBossId;
                    outDef._name             = "Boss";
                    outDef._hp               = 220;
                    outDef._maxHp            = 220;
                    outDef._atk              = 12;
                    outDef._def              = 16; // 플레이어 공격 34 → 18
                    outDef._speed            = 0.9f;
                    outDef._radius           = 0.7f;
                    outDef._attackCoolTime   = 1.6f;
                    outDef._firstAttackDelay = 1.2f;

                    // 겨냥한 한 발과 겨냥을 90° 돌린 옆 한 발.
                    MonsterShotDef aimed;
                    aimed._angleDegrees = 0.0f;
                    aimed._speed        = 4.5f;
                    aimed._lifeTime     = 2.5f;
                    aimed._radius       = 0.22f;
                    aimed._damage       = 10;
                    MonsterShotDef side = aimed;
                    side._angleDegrees  = 90.0f;
                    side._speed         = 3.2f;
                    side._lifeTime      = 1.8f;
                    outDef._listShot.push_back( aimed );
                    outDef._listShot.push_back( side );
                    return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ActionRoom::ActionRoom()
        : _site{}
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
        startFight( ActionRoomKind::Hall );
        for ( const ActionRoomSpawnDef& spawn : ActionRoomInternal::kArrEntranceSpawn )
        {
            spawnMonster( hashed_string( spawn._pMonsterId ), float2{ spawn._x, spawn._y } );
        }
    }

    void ActionRoom::beginHall()
    {
        startFight( ActionRoomKind::Hall );
        for ( const ActionRoomSpawnDef& spawn : ActionRoomInternal::kArrHallSpawn )
        {
            spawnMonster( hashed_string( spawn._pMonsterId ), float2{ spawn._x, spawn._y } );
        }
    }

    void ActionRoom::beginBoss()
    {
        startFight( ActionRoomKind::Boss );
        for ( const ActionRoomSpawnDef& spawn : ActionRoomInternal::kArrBossSpawn )
        {
            spawnMonster( hashed_string( spawn._pMonsterId ), float2{ spawn._x, spawn._y } );
        }
    }

    void ActionRoom::startFight( ActionRoomKind kind )
    {
        clear();
        _kind = kind;
        // 들어오면 문이 닫힌다 — 진입이 닫은 것이다(오버월드는 이 존의 워프를 막는다).
        sendGateState( true, true );
    }

    void ActionRoom::onPlayerDefeated()
    {
        if ( _kind == ActionRoomKind::None )
            return;
        PlayerDefeatedInRoomEvent defeated;
        defeated._returnMapPath = _site._returnMapPath;
        GameEventUtil::send( defeated );
        // 싸움이 끝났다 — 문을 열고 룸을 비운다(다시 들어오면 처음부터).
        sendGateState( false, false );
        clear();
    }

    void ActionRoom::sendGateState( bool bLocked, bool bTriggered ) const
    {
        ClearGateStateChangedEvent gate;
        gate._zoneId     = _site._zoneId;
        gate._bLocked    = bLocked ? SW_TRUE : SW_FALSE;
        gate._bTriggered = bTriggered ? SW_TRUE : SW_FALSE;
        GameEventUtil::send( gate );
    }

    float32 ActionRoom::getDashFill() const
    {
        if ( _dashCooldown.isActive() == false )
            return 1.0f;
        return MathUtil::saturate( 1.0f - ( _dashCooldown.getRemaining() / ActionRoomTuning::kDashCooldown ) );
    }

    float32 ActionRoom::getBossHpFill() const
    {
        if ( _kind != ActionRoomKind::Boss )
            return 0.0f;
        // 보스 룸 적 전부의 HP 합이다 — 보스 하나면 그 보스의 비율이고, 쓰러진 적은 0 이다.
        float32 hp    = 0.0f;
        float32 hpMax = 0.0f;
        for ( const Actor& actor : _listActor )
        {
            hp += actor._hp;
            hpMax += static_cast<float32>( _listMonsterDef[actor._defIndex]._maxHp );
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

        _attackCooldown.tick( deltaTime );
        _dashCooldown.tick( deltaTime );
        _invulnerable.tick( deltaTime );

        if ( input._bDashPressed == SW_TRUE && _dashCooldown.isActive() == false )
        {
            _dashCooldown.start( ActionRoomTuning::kDashCooldown );
            // **줄이지 않는다.** 그냥 대입하면 맞고 얻은 0.7 초짜리 무적이 대시 한 번에
            // 0.22 초로 **깎인다.** 대시가 피해를 덜 보게 해야 하는데 오히려 더 보게 된다.
            _invulnerable.extendTo( ActionRoomTuning::kDashInvulnerable );
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

        DebugDrawQueue* pDbg = game::getService<DebugDrawQueue>();
        if ( pDbg == nullptr )
            return;

        const float4 actorColor = ( _kind == ActionRoomKind::Boss ) ? float4( 1.0f, 0.25f, 0.2f, 1.0f ) : float4( 1.0f, 0.55f, 0.2f, 1.0f );
        for ( const Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            const float32 radius = _listMonsterDef[actor._defIndex]._radius;
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

    void ActionRoom::spawnMonster( const hashed_string& monsterId, const float2& position )
    {
        const int32 defIndex = findOrAddMonsterDef( monsterId );
        if ( defIndex < 0 )
            return;
        const MonsterDef& def = _listMonsterDef[static_cast<size_t>( defIndex )];

        Actor actor{};
        actor._position = position;
        actor._hp       = static_cast<float32>( def._hp );
        actor._defIndex = static_cast<uint16>( defIndex );
        // 쏘는 종만 시간을 잰다 — 첫 발은 나타나고 `_firstAttackDelay` 뒤다.
        if ( def._listShot.empty() == false )
            actor._attackTimer.start( def._firstAttackDelay );
        _listActor.push_back( actor );
    }

    int32 ActionRoom::findOrAddMonsterDef( const hashed_string& monsterId )
    {
        for ( size_t defIndex = 0; defIndex < _listMonsterDef.size(); ++defIndex )
        {
            if ( hashed_string( _listMonsterDef[defIndex]._id.c_str() ) == monsterId )
                return static_cast<int32>( defIndex );
        }

        // 게임이 건 카탈로그가 먼저다. 걸린 카탈로그에 없는 id 는 알린다 — 데이터 오타가 내장 수치로 조용히 바뀌면 "고쳤는데 그대로" 를 찾을 길이 없다.
        MonsterDef            def;
        const MonsterCatalog* pCatalog = game::getService<MonsterCatalog>();
        const MonsterDef*     pFound   = ( pCatalog != nullptr ) ? pCatalog->findMonster( monsterId ) : nullptr;
        if ( pFound != nullptr )
        {
            def = *pFound;
        }
        else
        {
            if ( pCatalog != nullptr )
                SW_LOG_WARNING( "Monster '%#' is not in the monster catalog - the action room uses its built-in definition", monsterId.c_str() );
            if ( ActionRoomInternal::findBuiltInMonster( monsterId, def ) == false )
            {
                SW_LOG_WARNING( "Monster '%#' has no definition - the action room does not spawn it", monsterId.c_str() );
                return -1;
            }
        }
        _listMonsterDef.push_back( def );
        return static_cast<int32>( _listMonsterDef.size() - 1 );
    }

    void ActionRoom::tryPlayerAttack( const ActionRoomFrameInput& input )
    {
        if ( _attackCooldown.isActive() )
            return;
        if ( input._bAttackPressed == SW_FALSE )
            return;

        _attackCooldown.start( ActionRoomTuning::kAttackCooldown );
        const AABB atk = playerAttackBox( input._playerPos._x, input._playerPos._y, input._facing );
        for ( Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( atk, kLayerPlayerAtk, computeActorBounds( actor ), kLayerEnemy, _layers ) == false )
                continue;
            // 방어 식은 유닛 스탯과 같다(고정 방어를 빼고 최소 1) — 같은 몬스터 정의를 두 곳이 다르게 읽지 않는다.
            const float32 defense = static_cast<float32>( _listMonsterDef[actor._defIndex]._def );
            actor._hp -= DamageMath::applyArmor( ActionRoomTuning::kPlayerAttackDamage, defense, 0.0f, 1.0f );
            if ( actor._hp <= 0.0f )
            {
                actor._hp     = 0.0f;
                actor._bAlive = SW_FALSE;
            }
        }
    }

    void ActionRoom::updateActors( float32 deltaTime, float32 playerX, float32 playerY, ActionRoomFrameResult& out )
    {
        for ( Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            const MonsterDef& def = _listMonsterDef[actor._defIndex];

            const float2 toPlayer = float2{ playerX - actor._position._x, playerY - actor._position._y }.normalize();
            actor._position._x += toPlayer._x * def._speed * deltaTime;
            actor._position._y += toPlayer._y * def._speed * deltaTime;

            if ( def._listShot.empty() )
                continue;
            actor._attackTimer.tick( deltaTime );
            if ( actor._attackTimer.isActive() )
                continue;
            // 늦음을 이어 발사 빈도가 fps 에 매이지 않게 한다(한 간격까지 — 멈춘 프레임 뒤에 몰아 쏘지 않는다).
            actor._attackTimer.restart( def._attackCoolTime );
            ++out._enemyVolleyCount;

            // 탄은 정의의 줄마다 한 발 — 겨냥 방향을 `_angleDegrees` 만큼 돌린 쪽으로 난다(0 은 겨냥, 90 은 (−y, x)).
            const float2 aim = float2{ playerX - actor._position._x, playerY - actor._position._y }.normalize();
            for ( const MonsterShotDef& shot : def._listShot )
            {
                const float32 radian = MathUtil::toRadian( shot._angleDegrees );
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
            projectile._life.tick( deltaTime );
            if ( projectile._life.isActive() == false )
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
        if ( _invulnerable.isActive() )
            return;

        // 룸이 돌려주는 피해는 방어 전 값이다 — 플레이어의 방어 · HP 는 게임이 든다.
        const AABB hurt = playerHurtBox( playerX, playerY );
        for ( const Actor& actor : _listActor )
        {
            if ( actor._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( hurt, kLayerPlayer, computeActorBounds( actor ), kLayerEnemy, _layers ) == false )
                continue;
            out._damageToPlayer += _listMonsterDef[actor._defIndex]._atk;
            _invulnerable.start( ActionRoomTuning::kHitInvulnerable );
            return;
        }
        for ( Projectile& projectile : _listProjectile )
        {
            if ( projectile._bAlive == SW_FALSE )
                continue;
            if ( queryOverlaps( hurt, kLayerPlayer, projectile.bounds(), kLayerProjectile, _layers ) == false )
                continue;
            out._damageToPlayer += projectile._damage;
            projectile._bAlive = SW_FALSE;
            _invulnerable.start( ActionRoomTuning::kHitInvulnerable );
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

        // 클리어는 한 번만 알린다(위의 `_bCleared` 가 막는다). 결과를 알리고 문을 연다.
        RoomClearedEvent cleared;
        cleared._mapPath       = _site._mapPath;
        cleared._bBossDefeated = out._bBossDefeated;
        GameEventUtil::send( cleared );
        sendGateState( false, false );
    }

    AABB ActionRoom::computeActorBounds( const Actor& actor ) const
    {
        const float32 radius = _listMonsterDef[actor._defIndex]._radius;
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
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 자리 · 충돌 층은 사본이 그대로 든다.
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

        // 종 정의는 id 로 다시 찾는다 — 같은 순서로 더해 적의 종 칸이 그대로 맞는다.
        uint32 defCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, defCount ) == false )
            return false;
        restored._listMonsterDef.clear();
        for ( uint32 defIndex = 0; defIndex < defCount; ++defIndex )
        {
            hashed_string monsterId;
            if ( StateArchiveUtil::readName( archive, monsterId ) == false )
                return false;
            if ( restored.findOrAddMonsterDef( monsterId ) != static_cast<int32>( defIndex ) )
                return false;
        }

        uint32 actorCount = 0;
        // 적마다 자리(8) + 체력(4) + 사격 시간(4) + 종 칸(2) + 생존(1)
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
        // 투사체마다 자리(8) + 속도(8) + 수명(4) + 반지름(4) + 피해(4) + 생존(1)
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
