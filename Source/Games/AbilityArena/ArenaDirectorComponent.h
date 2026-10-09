/**
 * @file ArenaDirectorComponent.h
 * @brief AbilityArena 의 규칙을 돌리는 컴포넌트 — 웨이브 · 쓰러뜨린 수 · 플레이어 다시 세우기 · 투사체 스폰 · 로그, 그리고 유닛 프리팹 스폰 지시입니다.
 *
 * @details 언리얼 GameMode/GameState 의 자리입니다. 씬에 하나 둡니다. 어빌리티 · 이펙트 · 어트리뷰트는 프레임워크(`AbilitySystemComponent`)와 데이터
 *          (`Resource/game/abilityarena/data/abilities.xml`)가, 유닛 하나의 몸은 폰 쪽 `ArenaUnitComponent`(의도만 읽는다)가, 판단은 조종자
 *          (플레이어 조종자 · `ArenaEnemyAiComponent` · `ArenaAutoBattleAiComponent`)가 맡고, 여기는 "언제 누가 서고 누가 쓰러졌는가 · 누가 누구를 쥐는가" 만 압니다.
 *
 *          틱 규칙: 디렉터는 `TickGroup::PrePhysics` 에서 유닛 모습(자리 · 편 · 살아 있음)을 한 번 적고, 유닛 · 투사체(`DuringPhysics`) ·
 *          카메라(`PostUpdate`)는 그것을 **읽기만** 합니다(목록은 `data()` 로). 스폰 · 빙의는 틱 안에서 할 수 없으므로 요청을 쌓아 두고
 *          `executeOrDeferPostTick` 한 번으로 틱 뒤에 합니다. 효과음도 그때 냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/MaterialTintCache.h"
#include "GameFramework/Base/Gameplay/Ability/GameplayEffect.h"

#include "Games/AbilityArena/ArenaUnitComponent.h"

namespace sw
{
    class AbilitySystemComponent;
    class Archive;
    class GameObject;
    class GameObjectManager;
    class MaterialInstance;
    class MeshComponent;
    class PawnComponent;

    /** @brief 이번 프레임의 유닛 하나 — 디렉터가 `PrePhysics` 에서 적고 다른 그룹이 읽습니다. */
    struct ArenaUnitView
    {
        GameObjectHandle _object{};
        float3           _position{};
        ArenaUnitKind    _kind{ ArenaUnitKind::Grunt };
        uint8            _bAlive{ SW_FALSE };      ///< 쓰러지지 않았다(Dead 태그 없음)
        uint8            _bPlayerTeam{ SW_FALSE }; ///< `Team.Player` 태그
        uint8            _bEnemyTeam{ SW_FALSE };  ///< `Team.Enemy` 태그
    };
} // namespace sw

namespace sw
{
    /**
     * @class ArenaDirectorComponent
     * @brief 아레나 한 판입니다. 플레이가 시작되면 플레이어와 첫 웨이브를 세웁니다.
     * @details 판의 상태(웨이브 · 쓰러뜨린 수)는 핫 리로드에서 처음부터 다시 섭니다(PROPERTY 가 아닌 런타임 상태). 세운 유닛 · AI 조종자 · 투사체는 핸들로 들고,
     *          상태 저장 전에 걷습니다(`despawnViews`) — 남은 디렉터는 다음 틱에 플레이어와 지금 웨이브를 다시 세웁니다.
     *          **빙의**: 적은 세울 때 종류의 AI 프리팹(`_gruntAiPrefab` · `_casterAiPrefab`)을 함께 세워 쥐게 하고, 쓰러져 걷을 때 그 조종자도 걷습니다(언리얼
     *          GameMode 가 폰의 AIController 를 세우는 자리). 플레이어 폰은 자동 빙의(`Player0`)로 플레이어 조종자가 쥐고, 자동 플레이 스위치가 켜지면
     *          자동 전투 AI(`_autoBattleAiPrefab`)가, 꺼지면 플레이어 조종자가 다시 쥡니다 — 몸 안에 자동 플레이 분기가 없다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Director", Tooltip = "Runs the arena waves, kills, respawns and the runtime spawns" )
    class ArenaDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 쓰러진 뒤 걷기까지(s) — 유닛이 그동안 납작해진다. */
        static constexpr float32 kDeathLinger = 0.8f;

        ArenaDirectorComponent();
        virtual ~ArenaDirectorComponent() override;

        /** @brief 판의 진행(웨이브 · 처치 수)을 씁니다 — `ComponentStateStore::capture` 가 부릅니다. 유닛 · 투사체는 모습이라 걷고 다시 세운다. */
        void writeState( Archive& outArchive ) const override;

        // ---- 다른 컴포넌트가 읽는 것(PrePhysics 뒤의 그룹) ----
        /** @brief 이번 프레임의 유닛 모습입니다. 워커에서는 첨자 대신 `data()` 로 읽는다. */
        const vector<ArenaUnitView>& getUnitViews() const { return _listUnitView; }
        /** @brief @p object 의 유닛 모습입니다. 없으면 nullptr 입니다. */
        const ArenaUnitView* findUnitView( GameObjectHandle object ) const;
        /** @brief @p from 과 적대이고 살아 있는 가장 가까운 유닛의 모습입니다. @p maxRange 밖이면 nullptr 입니다. */
        const ArenaUnitView* findNearestHostileView( GameObjectHandle from, float32 maxRange ) const;
        /**
         * @brief @p object 유닛의 지금 자리(메시의 월드 자리)입니다. 모습에 없으면 false 입니다.
         * @details 틱 밖(조종자의 판단 — 게임 스레드)에서 부릅니다. 모습의 자리는 지난 PrePhysics 의 틱 전 자리라 한 프레임 늦다.
         */
        bool findUnitPosition( GameObjectHandle object, float3& outPosition ) const;
        /** @brief @p from 과 적대이고 살아 있는 가장 가까운 유닛의 지금 자리입니다. 없으면 false 입니다(틱 밖에서). */
        bool findNearestHostilePosition( GameObjectHandle from, float3& outPosition ) const;
        /** @brief @p from 과 적대이고 살아 있는 가장 가까운 유닛의 어빌리티 시스템입니다(어빌리티가 대상을 찾는다). */
        AbilitySystemComponent* findNearestHostile( const AbilitySystemComponent& from, float32 maxRange ) const;
        /**
         * @brief @p from 이 @p facing 쪽으로 투사체를 쏩니다. 처음 닿은 적대 유닛에 @p spec(과 있으면 @p extraSpec)을 겁니다.
         * @details 어느 스레드에서 불러도 됩니다 — 투사체는 틱 뒤(게임 스레드)에 세운다. 스펙은 쏜 순간 만든 것이라 쏜 쪽이 그 사이 쓰러져도 남습니다.
         */
        void launchProjectile( const AbilitySystemComponent& from, const float3& facing, const GameplayEffectSpec& spec, const GameplayEffectSpec& extraSpec,
                               float32 speed, float32 range ) const;
        /** @brief 이번 프레임 모습의 플레이어 자리(틱 전, 없으면 아레나 가운데)입니다. */
        const float3& getPlayerFocus() const { return _playerFocus; }
        /** @brief 이번 프레임 모습의 플레이어 오브젝트입니다(없으면 빈 핸들). 뒤 단계(PostPhysics 이후)는 이것의 트랜스폼으로 이번 프레임 자리를 읽는다. */
        GameObjectHandle getPlayerObject() const { return _playerObject; }
        float32          getArenaHalfSize() const { return _arenaHalfSize; }
        uint32           getWave() const { return _wave; }
        uint32           getKillCount() const { return _killCount; }
        /** @brief 이 판에서 플레이어 편이 쏜 투사체 수입니다(시나리오 탐침). */
        uint32 getPlayerShotCount() const { return _playerShotCount; }
        /** @brief 플레이어 폰을 지금 자동 전투 AI 가 쥐고 있으면 true 입니다(시나리오 탐침). */
        bool isPlayerDrivenByAi() const;

        /**
         * @brief 유닛(어빌리티 시스템)이 따르는 디렉터입니다 — 같은 오브젝트의 `ArenaUnitComponent` 가 든 핸들로 씬에서 찾습니다. 없으면 nullptr 입니다.
         * @details 어빌리티가 디렉터를 찾는 길입니다. 게임 모듈의 정적 서비스 포인터에 기대지 않으므로 핫 리로드가 디렉터를 다시 만들어도 이어집니다.
         */
        static const ArenaDirectorComponent* findForUnit( const AbilitySystemComponent& unit );
        /** @brief 폰(유닛)이 따르는 디렉터입니다 — 조종자가 판단할 때 찾는 길입니다. 없으면 nullptr 입니다. */
        static const ArenaDirectorComponent* findForPawn( const PawnComponent& pawn );

    protected:
        /** @brief 어빌리티 카탈로그가 올라와 있으면 판을 엽니다(웨이브 0 — 첫 틱이 플레이어와 웨이브 1 을 세운다). */
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               tickGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _listPendingUnit.empty() == false || _bPossessionDirty == SW_TRUE; }

    private:
        /** @brief 세울 유닛 하나 — 틱 뒤에 프리팹으로 선다. */
        struct SpawnRequest
        {
            float3        _position{};
            int32         _level{ 1 };
            ArenaUnitKind _kind{ ArenaUnitKind::Grunt };
        };

        /** @brief 세운 유닛 하나 — 쓰러짐을 세고 걷기까지의 시간을 든다. */
        struct ArenaUnit
        {
            GameObjectHandle _object{};
            GameObjectHandle _controller{};        ///< 적을 쥔 AI 조종자의 오브젝트(플레이어는 비었다 — 플레이어 조종자는 다시 선 몸을 다시 쥔다)
            float32          _deathTimer{ -1.0f }; ///< 0 이상이면 쓰러진 뒤 걷기까지 남은 시간
            ArenaUnitKind    _kind{ ArenaUnitKind::Grunt };
        };

        /** @brief 쏠 투사체 하나 — 틱 뒤(`launchProjectile` 이 미룬 일)에 세운다. */
        struct ProjectileRequest
        {
            GameplayEffectSpec _spec{};
            GameplayEffectSpec _extraSpec{};
            float3             _position{};
            float3             _velocity{};
            float32            _range{ 0.0f };
            uint8              _bFromPlayer{ SW_FALSE };
        };

    private:
        void               requestWave( bool bAdvance );
        void               requestUnit( ArenaUnitKind kind, const float3& position, int32 level );
        [[nodiscard]] bool spawnUnit( GameObjectManager& manager, const SpawnRequest& request );
        /** @brief 적 @p pawn 을 쥘 AI 조종자를 종류의 프리팹으로 세워 쥐게 합니다. 세운 조종자의 오브젝트입니다(없으면 빈 핸들). */
        GameObjectHandle spawnEnemyController( GameObjectManager& manager, ArenaUnitKind kind, PawnComponent& pawn );
        /** @brief 플레이어 폰을 자동 플레이 스위치에 맞는 조종자(자동 전투 AI · 플레이어 조종자)가 쥐게 합니다. 틱 밖(플러시)에서. */
        void syncPlayerPossession( GameObjectManager& manager );
        /** @brief 지금 모습의 플레이어 폰입니다. 없으면 nullptr 입니다. */
        PawnComponent* findPlayerPawn() const;
        void           spawnProjectile( const ProjectileRequest& request );
        /**
         * @brief 편 색 하나를 메시에 입힙니다 — 메시의 머티리얼(씬이 늘 들고 있는 팔레트 · 기본 머티리얼)에서 만든 인스턴스를 모두가 나눠 쓴다.
         * @details 색마다 머티리얼 에셋을 두면 그 머티리얼은 처음 스폰할 때 게임 스레드에서 올라가고 마지막 것이 사라질 때 내려간다 —
         *          렌더 스레드의 병렬 기록과 겹친다. 인스턴스는 렌더 스레드가 올린다.
         */
        void applyTint( MeshComponent& mesh, int32 tintIndex );

        void updateUnits( float32 deltaTime );
        void updateUnitViews();
        void updatePlayerRespawn( float32 deltaTime );
        void logStatus( float32 deltaTime );
        void pruneProjectiles();
        bool hasPendingUnit( ArenaUnitKind kind ) const;

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _playerPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _gruntPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _casterPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _projectilePrefab;
        PROPERTY( Category = "Prefabs", DisplayName = "Grunt AI Prefab", AssetPath, AssetType = "Prefab", Tooltip = "AI controller spawned to possess each grunt" )
        string _gruntAiPrefab;
        PROPERTY( Category = "Prefabs", DisplayName = "Caster AI Prefab", AssetPath, AssetType = "Prefab", Tooltip = "AI controller spawned to possess each caster" )
        string _casterAiPrefab;
        PROPERTY( Category = "Prefabs", DisplayName = "Auto Battle AI Prefab", AssetPath, AssetType = "Prefab", Tooltip = "AI controller that possesses the player while auto play is on" )
        string _autoBattleAiPrefab;
        PROPERTY( Category = "Arena", DisplayName = "Arena Half Size", Tooltip = "Units are kept inside this square", Min = 1.0, Units = m )
        float32 _arenaHalfSize;
        PROPERTY( Category = "Arena", DisplayName = "Wave Radius", Tooltip = "Radius of the circle the enemies spawn on", Min = 0.0, Units = m )
        float32 _waveRadius;
        PROPERTY( Category = "Arena", DisplayName = "Player Respawn Delay", Tooltip = "Seconds before a fallen player stands up again", Min = 0.0, Units = s )
        float32 _playerRespawnDelay;
        PROPERTY( Category = "Look", DisplayName = "Player Tint", Meta = "Color", Tooltip = "Multiplies the palette texture of the player model" )
        float4 _playerTint;
        PROPERTY( Category = "Look", DisplayName = "Grunt Tint", Meta = "Color", Tooltip = "Multiplies the palette texture of the grunt model" )
        float4 _gruntTint;
        PROPERTY( Category = "Look", DisplayName = "Caster Tint", Meta = "Color", Tooltip = "Multiplies the palette texture of the caster model" )
        float4 _casterTint;
        PROPERTY( Category = "Look", DisplayName = "Projectile Color", Meta = "Color", Tooltip = "Colour of the projectile sphere" )
        float4 _projectileTint;
        PROPERTY( Category = "Debug", DisplayName = "Status Log Interval", Tooltip = "Seconds between status log lines", Min = 0.1, Units = s )
        float32 _statusLogInterval;
        PROPERTY( Category = "Combat", DisplayName = "Unit Radius", Tooltip = "Projectiles start this far outside the caster body", Min = 0.0, Units = m )
        float32 _unitRadius;
        PROPERTY( Category = "Combat", DisplayName = "Projectile Radius", Min = 0.0, Units = m )
        float32 _projectileRadius;

        vector<ArenaUnit>        _listUnit;
        vector<ArenaUnitView>    _listUnitView;
        vector<GameObjectHandle> _listProjectile;
        vector<SpawnRequest>     _listPendingUnit;
        MaterialTintCache        _tintCache; ///< 플레이어 · Grunt · Caster · 투사체 색
        float3                   _playerFocus;
        GameObjectHandle         _playerObject;       ///< 이번 프레임 모습의 플레이어(`getPlayerObject`)
        GameObjectHandle         _autoBattleObject;   ///< 자동 전투 AI 조종자의 오브젝트(처음 켤 때 세운다)
        float32                  _playerRespawnTimer; ///< 0 이상이면 플레이어가 다시 서기까지 남은 시간
        float32                  _statusLogTimer;
        uint32                   _wave;
        uint32                   _killCount;
        uint32                   _playerShotCount;
        uint8                    _bUnitsRequested  : 1; ///< 플레이어 · 웨이브를 세우라고 했다(걷으면 다음 틱이 다시 청한다)
        uint8                    _bPossessionDirty : 1; ///< 플레이어 폰을 쥔 조종자가 자동 플레이 스위치와 맞지 않는다(틱 뒤 플러시가 바꾼다)
        uint8                    _reserved         : 6;
    };
} // namespace sw
