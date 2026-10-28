/**
 * @file ActionRoom.h
 * @brief 던전과 보스 룸용 실시간 클리어 게이트 전투입니다(던그리드 스타일의 아이디어).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Physics/Collision/CollisionLayers.h"

#include "GameFramework/Base/Foundation/Utility/Grid/FacingDir.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"

namespace sw
{
    class Archive;

    // ------------------------------------------------------------------------------
    // 1) 룸 종류와 프레임 입출력(FacingDir 은 Utility/FacingDir.h)
    // ------------------------------------------------------------------------------
    /** @brief 액션 룸 전투 종류입니다. */
    enum class ActionRoomKind : uint8
    {
        None = 0,
        Hall,
        Boss
    };
} // namespace sw

namespace sw
{
    /** @brief 한 프레임의 플레이어 입력입니다(위치, 방향, 공격과 대시). */
    struct ActionRoomFrameInput
    {
        float2                 _playerPos{ 0.0f, 0.0f };
        FacingDir              _facing = FacingDir::Right;
        uint8                  _bAttackPressed : 1;
        uint8                  _bDashPressed   : 1;
        [[maybe_unused]] uint8 _reserved       : 6;

        /** @brief 공격과 대시 비트를 0으로 초기화합니다. */
        ActionRoomFrameInput()
            : _bAttackPressed{ SW_FALSE }
            , _bDashPressed{ SW_FALSE }
            , _reserved{ 0 } {}
    };
} // namespace sw

namespace sw
{
    /** @brief 한 프레임의 전투 결과입니다(피격, 클리어, 대시 시작). */
    struct ActionRoomFrameResult
    {
        int32                  _damageToPlayer{ 0 };   ///< 이번 프레임에 플레이어가 받은 피해(방어를 빼기 전 값이다. 플레이어의 방어와 HP 는 게임이 가진다)
        int32                  _enemyVolleyCount{ 0 }; ///< 이번 프레임에 적이 쏜 횟수(한 번에 여러 발을 쏴도 1이다. 소리와 연출에 쓴다)
        uint8                  _bClearedThisFrame : 1;
        uint8                  _bBossDefeated     : 1;
        uint8                  _bDashStarted      : 1;
        [[maybe_unused]] uint8 _reserved          : 5;

        /** @brief 클리어, 보스, 대시 비트를 0으로 초기화합니다. */
        ActionRoomFrameResult()
            : _bClearedThisFrame{ SW_FALSE }
            , _bBossDefeated{ SW_FALSE }
            , _bDashStarted{ SW_FALSE }
            , _reserved{ 0 } {}
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 이 룸이 어디에 있는지 나타냅니다. 룸 이벤트에 실립니다(`ActionRoom::setSite`).
     * @details 룸은 전투만 안다. 어느 맵과 존의 룸인지, 지면 어디로 돌아가는지는 룸을 연 게임이 정한다.
     */
    struct ActionRoomSite
    {
        string _mapPath;       ///< 이 룸의 맵(`RoomClearedEvent::_mapPath`)
        string _zoneID;        ///< 클리어 게이트가 걸린 존(`ClearGateStateChangedEvent::_zoneID`)
        string _returnMapPath; ///< 졌을 때 돌아갈 오버월드 맵(`PlayerDefeatedInRoomEvent::_returnMapPath`)
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) ActionRoom: 적과 투사체를 스폰하고, 클리어하면 게이트를 연다
    // ------------------------------------------------------------------------------
    /**
     * @brief 플레이어 쪽 조절 값입니다. 적의 수치는 몬스터 정의(`MonsterDef`)에 있습니다. 게임이 `setSettings` 로 넘기고, 넘기지 않으면 기본값입니다.
     * @details 대시 쿨다운은 `update` 가 넣는 값이자 `getDashFill` 의 분모라 이 칸 하나만 둔다(둘로 두면 게이지가 쿨다운과 어긋난다).
     */
    struct ActionRoomSettings
    {
        float32 _dashCooldownSeconds{ 0.85f };     ///< `getDashFill` 의 분모이기도 하다
        float32 _dashInvulnerableSeconds{ 0.22f }; ///< 대시가 주는 무적
        float32 _hitInvulnerableSeconds{ 0.7f };   ///< 맞았을 때의 무적 — 대시 무적보다 길다
        float32 _attackCooldownSeconds{ 0.28f };
        float32 _playerAttackDamage{ 34.0f }; ///< 적의 방어(`MonsterDef::_def`)를 뺀 값이 들어간다
    };
} // namespace sw

namespace sw
{
    /** @brief 룸에 스폰할 적 하나입니다. 종 id(`MonsterCatalog`)와 위치(m)입니다. */
    struct ActionRoomSpawnDef
    {
        hashed_string _monsterID;
        float2        _position{ 0.0f, 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 룸 종류마다 어느 종을 어디에 세울지입니다. 게임이 `setLayout` 으로 넘깁니다(언리얼의 레벨에 놓인 스폰 지점 자리).
     * @details 코드에 내장 배치가 없습니다. 비어 있는 종류로 싸움을 시작하면 오류를 남기고 적 없이 시작합니다.
     */
    struct ActionRoomLayout
    {
        vector<ActionRoomSpawnDef> _listEntranceSpawn;
        vector<ActionRoomSpawnDef> _listHallSpawn;
        vector<ActionRoomSpawnDef> _listBossSpawn;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 적과 보스의 투사체를 스폰하고, 클리어하면 클리어 게이트를 엽니다.
     * @details 상태가 바뀔 때마다 "game" 채널에 룸 이벤트를 낸다(언리얼 GameMode 의 브로드캐스트에 해당하며 `GameEventUtil::send` 를 쓴다).
     *          - 전투를 시작하면(`beginEntrance`, `beginHall`, `beginBoss`) 게이트가 닫힌다(`ClearGateStateChangedEvent` 의 잠김과 진입 트리거).
     *          - 클리어하면 `RoomClearedEvent`(보스였는지 포함)를 낸 뒤 게이트가 열린다.
     *          - 플레이어가 패배하면(`onPlayerDefeated`) `PlayerDefeatedInRoomEvent` 를 낸 뒤 게이트가 열리고 룸이 비워진다.
     *          HUD 와 오버월드는 `update` 의 반환값으로 결과를 확인하지 않고 이 이벤트로 받는다.
     *          적의 수치(HP, 반지름, 속도, 닿은 피해, 방어, 사격)는 게임이 게임 서비스로 등록한 `MonsterCatalog` 에서 종 id 로 찾는다. 코드에 내장 정의가 없으므로
     *          카탈로그가 없거나 그 종이 없으면 오류를 남기고 그 적을 세우지 않는다.
     *          싸움을 시작할 때 그 싸움에 나온 종의 정의를 복사해 둔다(싸움 도중 카탈로그가 다시 로드되어도 영향이 없다). 방 배치(어느 종을 어디에 둘지)는 게임이 넘기는 `ActionRoomLayout` 이다.
     *          적은 오브젝트가 아니라서 `UnitStatsComponent` 를 거치지 않고, 방어 공식만 같다(`DamageMath::applyArmor`).
     */
    class SW_KIT_API ActionRoom
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "AROM" );
        static constexpr uint32 kStateVersion = 1;

        /** @brief 비활성(None)과 게이트 닫힘 상태로 시작합니다. */
        ActionRoom();

        /** @brief 이 룸이 어디에 있는지 정합니다(이벤트에 실립니다). `clear` 로 지워지지 않습니다. */
        void setSite( const ActionRoomSite& site ) { _site = site; }
        /** @brief 이 룸이 어디에 있는지 반환합니다. */
        const ActionRoomSite& getSite() const { return _site; }
        /** @brief 플레이어 조절 값을 정합니다. `clear` 로 지워지지 않습니다. */
        void                      setSettings( const ActionRoomSettings& settings ) { _settings = settings; }
        const ActionRoomSettings& getSettings() const { return _settings; }
        /** @brief 룸 종류마다의 적 배치를 정합니다. `clear` 로 지워지지 않습니다. */
        void                    setLayout( const ActionRoomLayout& layout ) { _layout = layout; }
        const ActionRoomLayout& getLayout() const { return _layout; }

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
        bool isPlayerInvulnerable() const { return _invulnerable.isActive(); }
        /** @brief 대시 쿨다운 게이지(0~1)를 반환합니다. */
        float32 getDashFill() const;
        /** @brief 보스 룸의 HP 게이지(0~1)입니다. 적 HP 합을 최대 HP 합으로 나눈 값이고, 보스가 하나면 그 보스의 비율입니다. 보스 룸이 아니면 0 입니다. */
        float32 getBossHPFill() const;
        /** @brief 살아 있는 적 수를 반환합니다. */
        int32 getAliveEnemyCount() const;

        /** @brief 한 프레임 전투를 갱신합니다. 이 프레임에 클리어했으면 룸 이벤트를 냅니다(클래스 설명 참고). */
        ActionRoomFrameResult update( float32 deltaTime, const ActionRoomFrameInput& input );
        /**
         * @brief 플레이어가 이 룸에서 졌음을 알립니다. `PlayerDefeatedInRoomEvent` 와 게이트 열림을 내고 룸을 비웁니다. 룸이 활성이 아니면 아무것도 하지 않습니다.
         * @details 룸은 플레이어 HP 를 가지지 않는다(피해는 `ActionRoomFrameResult::_damageToPlayer` 로 돌려주고 게임이 깎는다). 그래서 패배는 HP 를 가진 게임이 알린다.
         *          언리얼에서 폰의 죽음을 GameMode 에 알리고 GameMode 가 브로드캐스트하는 것과 같다.
         */
        void onPlayerDefeated();
        /** @brief 디버그 오버레이를 그립니다. */
        void drawDebug() const;

        /**
         * @brief 종류, 클리어, 공격, 대시, 무적 남은 시간, 이번 싸움의 종 id를 씁니다(보스 최대 체력 같은 수치는 정의가 가지고 있습니다). 적(위치, 체력, 사격 시간, 종 인덱스, 생존)과 투사체도 씁니다.
         *        위치 정보(`ActionRoomSite`)는 게임이 `setSite` 로 설정하는 값이고 충돌 레이어는 생성자에서 정하는 값이라 싣지 않습니다.
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 종 정의는 지금의 카탈로그 서비스에서 다시 찾습니다. 카탈로그에 없는 종이거나 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다.
         *        상태를 바꿔도 룸 이벤트(게이트, 클리어)는 내지 않습니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 룸에 있는 적 하나입니다. 수치는 그 종의 정의(`_listMonsterDef[_defIndex]`)에서 읽습니다. */
        struct Actor
        {
            float2                 _position;
            float32                _hp;
            Countdown              _attackTimer; ///< 다음 사격까지 남은 시간(쏘는 종만)
            uint16                 _defIndex;    ///< `_listMonsterDef` 의 인덱스
            uint8                  _bAlive   : 1;
            [[maybe_unused]] uint8 _reserved : 7;

            /** @brief 살아 있는 상태로 초기화합니다. */
            Actor()
                : _position{}
                , _hp{ 1.0f }
                , _attackTimer{}
                , _defIndex{ 0 }
                , _bAlive{ SW_TRUE }
                , _reserved{ 0 }
            {
            }
        };

        /** @brief 적 투사체입니다. */
        struct Projectile
        {
            float2                 _position;
            float2                 _velocity;
            Countdown              _life; ///< 남은 수명(초)
            float32                _radius;
            int32                  _damage; ///< 맞은 플레이어에게 주는 피해(`MonsterShotDef::_damage`)
            uint8                  _bAlive   : 1;
            [[maybe_unused]] uint8 _reserved : 7;

            /** @brief 살아 있는 상태로 초기화합니다. */
            Projectile()
                : _position{}
                , _velocity{}
                , _life{}
                , _radius{ 0.2f }
                , _damage{ 0 }
                , _bAlive{ SW_TRUE }
                , _reserved{ 0 }
            {
            }

            /** @brief 투사체 AABB 를 반환합니다. */
            AABB bounds() const;
        };

        /** @brief 종 @p monsterID 의 적 하나를 @p position 에 스폰합니다. 정의가 없으면 스폰하지 않습니다(`getOrCreateMonsterDef` 가 오류를 남깁니다). */
        void spawnMonster( const hashed_string& monsterID, const float2& position );
        /** @brief 싸움을 시작하고 @p listSpawn 의 적을 세웁니다. 배치가 비어 있으면 오류를 남깁니다(@p pKindName 은 로그용 이름). */
        void beginFight( ActionRoomKind kind, const vector<ActionRoomSpawnDef>& listSpawn, const utf8* pKindName );
        /**
         * @brief 이번 싸움의 정의 목록에서 @p monsterID 의 인덱스를 찾습니다. 없으면 카탈로그 서비스에서 찾아 목록에 추가합니다.
         * @return 정의 인덱스입니다. 카탈로그가 없거나 그 id 가 없으면 -1 이고 오류를 남깁니다.
         */
        int32 getOrCreateMonsterDef( const hashed_string& monsterID );
        /** @brief 플레이어 공격을 시도합니다. */
        void tryPlayerAttack( const ActionRoomFrameInput& input );
        /** @brief 액터를 갱신합니다. 적이 쏜 횟수를 @p outResult 에 더합니다. */
        void updateActors( float32 deltaTime, float32 playerX, float32 playerY, ActionRoomFrameResult& outResult );
        /** @brief 투사체를 갱신합니다. */
        void updateProjectiles( float32 deltaTime );
        /** @brief 플레이어 피격을 처리합니다. */
        void resolvePlayerHits( float32 playerX, float32 playerY, ActionRoomFrameResult& outResult );
        /** @brief 클리어 상태를 갱신합니다. 이 프레임에 클리어했으면 룸 이벤트를 냅니다. */
        void refreshCleared( ActionRoomFrameResult& outResult );
        /** @brief 전투를 시작합니다. 룸을 비우고 종류를 정한 뒤 게이트가 닫혔음을 알립니다. 적 스폰은 호출한 쪽이 합니다. */
        void startFight( ActionRoomKind kind );
        /** @brief 클리어 게이트가 바뀌었음을 알립니다(`ClearGateStateChangedEvent`). */
        void sendGateState( bool bLocked, bool bTriggered ) const;
        /** @brief 적의 맞음 상자를 반환합니다(그 종의 `_radius`). */
        AABB computeActorBounds( const Actor& actor ) const;
        /** @brief 플레이어 피격 박스를 반환합니다. */
        AABB playerHurtBox( float32 x, float32 y ) const;
        /** @brief 플레이어 공격 박스를 반환합니다. */
        AABB playerAttackBox( float32 x, float32 y, FacingDir facing ) const;

        static constexpr uint8 kLayerPlayer     = 0; ///< 플레이어 히트
        static constexpr uint8 kLayerEnemy      = 1; ///< 적 히트
        static constexpr uint8 kLayerPlayerAtk  = 2; ///< 플레이어 공격
        static constexpr uint8 kLayerProjectile = 3; ///< 적 투사체

        ActionRoomSite         _site;
        ActionRoomSettings     _settings;
        ActionRoomLayout       _layout;
        ActionRoomKind         _kind;
        CollisionLayers        _layers;
        vector<Actor>          _listActor;
        vector<Projectile>     _listProjectile;
        vector<MonsterDef>     _listMonsterDef; ///< 이번 싸움에 나온 종의 정의 복사본(`Actor::_defIndex` 가 가리킨다)
        Countdown              _attackCooldown;
        Countdown              _dashCooldown;
        Countdown              _invulnerable; ///< 피격 후 무적 시간
        uint8                  _bCleared : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
