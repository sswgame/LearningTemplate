/**
 * @file ActionRoom.h
 * @brief 던전 · 보스 룸용 실시간 클리어 게이트 전투입니다(던그리드 스타일 아이디어).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/AABB.h"
#include "Engine/Physics/CollisionLayers.h"

#include "GameFramework/Base/FacingDir.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/GameFrameworkMinimal.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 룸 종류 · 프레임 입출력 (FacingDir 은 Base/FacingDir.h)
    // ------------------------------------------------------------------------------
    /** @brief 액션 룸 전투 종류입니다. */
    enum class ActionRoomKind : uint8
    {
        None = 0,
        Hall,
        Boss
    };

    /** @brief 한 프레임의 플레이어 입력입니다(위치 · 방향 · 공격/대시). */
    struct ActionRoomFrameInput
    {
        float2                 _playerPos{ 0.0f, 0.0f };
        FacingDir              _facing = FacingDir::Right;
        uint8                  _bAttackPressed : 1;
        uint8                  _bDashPressed   : 1;
        [[maybe_unused]] uint8 _reserved       : 6;

        /** @brief 공격 · 대시 비트를 0 으로 둡니다. */
        ActionRoomFrameInput()
            : _bAttackPressed{ SW_FALSE }
            , _bDashPressed{ SW_FALSE }
            , _reserved{ 0 } {}
    };

    /** @brief 한 프레임의 전투 결과입니다(피격 · 클리어 · 대시 시작). */
    struct ActionRoomFrameResult
    {
        int32                  _damageToPlayer{ 0 }; ///< 이번 프레임 플레이어 피해
        uint8                  _bClearedThisFrame : 1;
        uint8                  _bBossDefeated     : 1;
        uint8                  _bDashStarted      : 1;
        [[maybe_unused]] uint8 _reserved          : 5;

        /** @brief 클리어 · 보스 · 대시 비트를 0 으로 둡니다. */
        ActionRoomFrameResult()
            : _bClearedThisFrame{ SW_FALSE }
            , _bBossDefeated{ SW_FALSE }
            , _bDashStarted{ SW_FALSE }
            , _reserved{ 0 } {}
    };

    /**
     * @brief 이 룸이 어디에 있는지입니다 — 룸 이벤트에 실립니다(`ActionRoom::setSite`).
     * @details 룸은 전투만 압니다. 어느 맵 · 어느 존의 룸인지, 지면 어디로 돌아가는지는 룸을 연 게임이 정합니다.
     */
    struct ActionRoomSite
    {
        string _mapPath;       ///< 이 룸의 맵(`RoomClearedEvent::_mapPath`)
        string _zoneId;        ///< 클리어 게이트가 걸린 존(`ClearGateStateChangedEvent::_zoneId`)
        string _returnMapPath; ///< 지면 돌아갈 오버월드 맵(`PlayerDefeatedInRoomEvent::_returnMapPath`)
    };

    // ------------------------------------------------------------------------------
    // 2) ActionRoom — 적/투사체 스폰, 클리어 시 게이트 개방
    // ------------------------------------------------------------------------------
    /**
     * @brief 적 · 보스 투사체를 스폰하고, 클리어하면 클리어 게이트를 엽니다.
     * @details 상태가 바뀌는 자리마다 "game" 채널에 룸 이벤트를 냅니다(언리얼 GameMode 의 브로드캐스트 — `GameEventUtil::send`):
     *          - 전투 시작(`beginEntrance` · `beginHall` · `beginBoss`) — 게이트가 닫힌다(`ClearGateStateChangedEvent` 잠김 · 진입 트리거).
     *          - 클리어 — `RoomClearedEvent`(보스였는지) 뒤 게이트가 열린다.
     *          - 플레이어 패배(`onPlayerDefeated`) — `PlayerDefeatedInRoomEvent` 뒤 게이트가 열리고 룸이 비워진다.
     *          HUD · 오버월드는 결과를 `update` 의 반환값에서 되묻지 않고 이 이벤트로 받는다.
     */
    class SW_GF_API ActionRoom
    {
    public:
        /** @brief 비활성(None) · 게이트 닫힘으로 시작합니다. */
        ActionRoom();

        /** @brief 이 룸이 어디에 있는지 정합니다(이벤트에 실린다). `clear` 로 지워지지 않습니다. */
        void setSite( const ActionRoomSite& site ) { _site = site; }
        /** @brief 이 룸이 어디에 있는지 반환합니다. */
        const ActionRoomSite& getSite() const { return _site; }

        /** @brief 룸 상태와 액터를 비웁니다. */
        void clear();
        /** @brief 입구 연출을 시작합니다. */
        void beginEntrance();
        /** @brief 홀 전투를 시작합니다. */
        void beginHall();
        /** @brief 보스 전투를 시작합니다. */
        void beginBoss();

        /** @brief 룸이 활성인지 반환합니다. */
        bool isActive() const { return _kind != ActionRoomKind::None; }
        /** @brief 룸 종류를 반환합니다. */
        ActionRoomKind getKind() const { return _kind; }
        /** @brief 클리어 여부를 반환합니다. */
        bool isCleared() const { return _bCleared != SW_FALSE; }
        /** @brief 플레이어 무적 여부를 반환합니다. */
        bool isPlayerInvulnerable() const { return _invulnTimer > 0.0f; }
        /** @brief 대시 쿨다운 게이지(0~1)를 반환합니다. */
        float32 getDashFill() const;
        /** @brief 보스 HP 게이지(0~1)를 반환합니다. */
        float32 getBossHpFill() const;
        /** @brief 살아 있는 적 수를 반환합니다. */
        int32 getAliveEnemyCount() const;

        /** @brief 한 프레임 전투를 갱신합니다. 이 프레임에 클리어했으면 룸 이벤트를 냅니다(클래스 설명). */
        ActionRoomFrameResult update( float32 deltaTime, const ActionRoomFrameInput& input );
        /**
         * @brief 플레이어가 이 룸에서 졌습니다 — `PlayerDefeatedInRoomEvent` 와 게이트 열림을 내고 룸을 비웁니다. 룸이 활성이 아니면 아무것도 하지 않습니다.
         * @details 룸은 플레이어 HP 를 들지 않습니다(피해는 `ActionRoomFrameResult::_damageToPlayer` 로 돌려주고 게임이 깎는다). 그래서 패배는 HP 를
         *          가진 게임이 알립니다 — 언리얼에서 폰의 죽음을 GameMode 에 알리고 GameMode 가 브로드캐스트하는 것과 같다.
         */
        void onPlayerDefeated();
        /** @brief 디버그 오버레이를 그립니다. */
        void drawDebug() const;

    private:
        /** @brief 적 액터 종류입니다. */
        enum class ActorKind : uint8
        {
            Grunt = 0,
            Boss
        };

        /** @brief 적 위치 · HP · 공격 타이머입니다. */
        struct Actor
        {
            ActorKind              _kind;
            float2                 _position;
            float32                _hp;
            float32                _hpMax;
            float32                _radius;
            float32                _speed;
            float32                _attackTimer; ///< 다음 투사체까지
            uint8                  _bAlive   : 1;
            [[maybe_unused]] uint8 _reserved : 7;

            /** @brief 살아 있는 상태로 둡니다. */
            Actor()
                : _kind{ ActorKind::Grunt }
                , _position{}
                , _hp{ 1.0f }
                , _hpMax{ 1.0f }
                , _radius{ 0.35f }
                , _speed{ 1.6f }
                , _attackTimer{ 0.0f }
                , _bAlive{ SW_TRUE }
                , _reserved{ 0 }
            {
            }

            /** @brief 액터 AABB 를 반환합니다. */
            AABB bounds() const;
        };

        /** @brief 적 투사체입니다. */
        struct Projectile
        {
            float2                 _position;
            float2                 _velocity;
            float32                _life; ///< 남은 수명(초)
            float32                _radius;
            uint8                  _bAlive   : 1;
            [[maybe_unused]] uint8 _reserved : 7;

            /** @brief 살아 있는 상태로 둡니다. */
            Projectile()
                : _position{}
                , _velocity{}
                , _life{ 0.0f }
                , _radius{ 0.2f }
                , _bAlive{ SW_TRUE }
                , _reserved{ 0 }
            {
            }

            /** @brief 투사체 AABB 를 반환합니다. */
            AABB bounds() const;
        };

        /** @brief 그런트를 스폰합니다. */
        void spawnGrunt( float32 x, float32 y );
        /** @brief 보스를 스폰합니다. */
        void spawnBoss( float32 x, float32 y );
        /** @brief 플레이어 공격을 시도합니다. */
        void tryPlayerAttack( const ActionRoomFrameInput& input );
        /** @brief 액터를 갱신합니다. */
        void updateActors( float32 deltaTime, float32 playerX, float32 playerY );
        /** @brief 투사체를 갱신합니다. */
        void updateProjectiles( float32 deltaTime );
        /** @brief 플레이어 피격을 처리합니다. */
        void resolvePlayerHits( float32 playerX, float32 playerY, ActionRoomFrameResult& out );
        /** @brief 클리어 상태를 갱신합니다. 이 프레임에 클리어했으면 룸 이벤트를 냅니다. */
        void refreshCleared( ActionRoomFrameResult& out );
        /** @brief 전투를 시작합니다 — 룸을 비우고 종류를 정한 뒤 게이트가 닫혔음을 알립니다. 적 스폰은 부른 쪽이 한다. */
        void startFight( ActionRoomKind kind );
        /** @brief 클리어 게이트가 바뀌었음을 알립니다(`ClearGateStateChangedEvent`). */
        void sendGateState( bool bLocked, bool bTriggered ) const;
        /** @brief 플레이어 피격 박스를 반환합니다. */
        AABB playerHurtBox( float32 x, float32 y ) const;
        /** @brief 플레이어 공격 박스를 반환합니다. */
        AABB playerAttackBox( float32 x, float32 y, FacingDir facing ) const;

        static constexpr uint8 kLayerPlayer     = 0; ///< 플레이어 히트
        static constexpr uint8 kLayerEnemy      = 1; ///< 적 히트
        static constexpr uint8 kLayerPlayerAtk  = 2; ///< 플레이어 공격
        static constexpr uint8 kLayerProjectile = 3; ///< 적 투사체

        ActionRoomSite         _site;
        ActionRoomKind         _kind;
        CollisionLayers        _layers;
        vector<Actor>          _listActor;
        vector<Projectile>     _listProjectile;
        float32                _attackCooldown;
        float32                _dashCooldown;
        float32                _invulnTimer; ///< 피격 후 무적
        float32                _bossMaxHp;
        uint8                  _bCleared : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
