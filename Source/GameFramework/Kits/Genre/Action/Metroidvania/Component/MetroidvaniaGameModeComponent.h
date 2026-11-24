/**
 * @file MetroidvaniaGameModeComponent.h
 * @brief 메트로배니아 키트(`GF_Metroidvania`)의 디렉터 — 방을 탐험하고 능력을 얻어 보스를 쓰러뜨리는 한 판을 진행합니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트 — 능력 · 지도 · 부적 · 죽음과 휴식 · 전투원)과 공용 2D 몸(`PlatformerMotor2D`) · 방 그래프(`AreaGraph`)를 들고 씬과 잇는다.
 *          입력 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_Metroidvania` 의 `MetroidvaniaViewComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          지형은 씬 데이터(칸 글자 줄 — `PlatformTileMap` 과 같은 글자)이고, 방은 방 그래프의 사각형(x · y · w · h, 칸 좌표)으로 알며, 적 · 줍는 것 · 지점은 카탈로그 id 에 씬의 자리를 맞춘 것이다.
 *          죽으면 가진 통화가 그 자리의 시체가 되고 마지막으로 쉰 곳에서 되살아난다 — 시체 곁에 서면 되찾는다. 목표 플래그(`_goalFlag` — 보스 플래그)가 켜지면 이기고 `_resultHoldSeconds` 뒤
 *          미니게임을 끝낸다(디렉터 계약 ③) — 가진 통화를 결과 값 `money` 로 내보낸다. 도중에 나가도 가진 통화는 나간다.
 *          장르 완결 루프 시나리오: `game/playground/automation/metroidvania_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/MetroidvaniaAbilitySet.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/MetroidvaniaCatalog.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/MetroidvaniaDeathRestState.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/MetroidvaniaDuelist.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/MetroidvaniaMapState.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/MetroidvaniaTrinketLoadout.h"
#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

namespace sw
{
    /** @brief 판의 단계입니다(감시 값 `Metroidvania.Phase` 의 수). */
    ENUM()
    enum class MetroidvaniaPhase : uint8
    {
        Explore = 0, ///< 탐험 · 전투
        Dead,        ///< 죽어 되살아나기를 기다린다
        Won          ///< 목표를 이뤘다
    };

    /** @brief 한 프레임의 플레이어 입력입니다. 누름(`…Pressed`)은 이번 프레임에 눌렀다는 뜻이고 디렉터가 다음 걸음까지 모은다. */
    struct MetroidvaniaPlayerCommand
    {
        float2 _move{ 0.0f, 0.0f };
        uint8  _bJumpHeld{ SW_FALSE };
        uint8  _bGuardHeld{ SW_FALSE };
        uint8  _bJumpPressed{ SW_FALSE };
        uint8  _bDashPressed{ SW_FALSE };
        uint8  _bAttackPressed{ SW_FALSE };
        uint8  _bParryPressed{ SW_FALSE };
        uint8  _bUsePressed{ SW_FALSE };   ///< 곁의 지점을 쓴다(쉬기 · 지도 사기 · 빠른 이동)
        uint8  _bFlaskPressed{ SW_FALSE }; ///< 물약을 마신다
    };

    /** @brief 씬에 놓인 적 하나입니다. */
    struct MetroidvaniaEnemy
    {
        MetroidvaniaDuelist _duelist{};
        hashed_string       _spawnID{}; ///< 처치 기록의 위치 id(놓인 순서로 정해진다)
        hashed_string       _enemyID{};
        float2              _position{};
        Countdown           _startup{};  ///< 공격이 닿기까지(0 이면 준비 동작 중이 아니다)
        Countdown           _cooldown{}; ///< 다음 공격 준비까지
        uint8               _bBoss{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MetroidvaniaGameModeComponent
     * @brief 탐험 → 능력 · 부적 줍기 → 쉬기 · 지도 → 전투 → 보스를 플레이어 입력 명령으로 진행합니다.
     */
    REFLECT( Category = "Metroidvania", DisplayName = "Metroidvania Game Mode", Tooltip = "Runs one Metroidvania exploration (rooms, abilities, rest, duels, boss) from player input commands" )
    class SW_KIT_API MetroidvaniaGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        MetroidvaniaGameModeComponent();
        virtual ~MetroidvaniaGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /** @brief 이번 프레임의 플레이어 입력을 넘깁니다. 이동 · 누른 채는 마지막 값으로 덮고 누름은 다음 걸음이 쓸 때까지 모읍니다. 판이 열렸고 이기기 전이면 true 입니다(`isAcceptingInput`). */
        [[nodiscard]] bool submitPlayerInput( const MetroidvaniaPlayerCommand& command );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 이기기 전이라 입력 명령을 받으면 true 입니다(죽어 있는 동안도 받는다). */
        bool                              isAcceptingInput() const { return isStarted() && _phase != MetroidvaniaPhase::Won; }
        const PlatformerMotor2D&          getMotor() const { return _motor; }
        const MetroidvaniaDuelist&        getPlayer() const { return _player; }
        const MetroidvaniaDeathRestState& getDeathRest() const { return _deathRest; }
        const MetroidvaniaMapState&       getMapState() const { return _mapState; }
        const vector<MetroidvaniaEnemy>&  getEnemies() const { return _listEnemy; }
        const vector<float2>&             getPickupPositions() const { return _listPickupPosition; }
        /** @brief 평면 자리(x 오른쪽, y 위)를 절대 자리로 옮깁니다. */
        static AbsolutePosition toAbsolute( const float2& position ) { return AbsolutePosition{ float3( position._x, position._y, 0.0f ) }; }
        /** @brief 적 @p index 가 서 있으면 true 입니다(쓰러지지 않았다). */
        bool isEnemyAlive( size_t index ) const { return index < _listEnemy.size() && _deathRest.isSpawnAlive( _listEnemy[index]._spawnID ); }
        /** @brief 줍는 것 @p index 를 아직 줍지 않았으면 true 입니다. */
        bool                 isPickupLeft( size_t index ) const { return index < _listPickup.size() && _mapState.isCollected( _listPickup[index].key() ) == false; }
        const hashed_string& getArea() const { return _area; }
        MetroidvaniaPhase    getExplorePhase() const { return _phase; }
        int32                getPhase() const { return static_cast<int32>( _phase ); }
        float32              getPlayerX() const { return _motor.getPosition()._x; }
        int32                getAbilityCount() const { return static_cast<int32>( _abilities.getAbilities().size() ); }
        /** @brief 가진 통화입니다. */
        int32 getCurrency() const;
        int32 getVisitedAreaCount() const { return _graph.getVisitedCount(); }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        /** @brief 가진 통화를 결과 값 `money` 로 냅니다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 씬 배치(적 · 줍는 것 · 지점)의 길이와 카탈로그 id 를 봅니다. 틀리면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validatePlacement() const;
        [[nodiscard]] bool populateTerrain();
        /** @brief 씬 배치로 적을 세웁니다. 카탈로그 체력 규칙이 거절되면 false 입니다. */
        [[nodiscard]] bool populateEnemies( vector<MetroidvaniaEnemy>& outListEnemy ) const;
        void               stepExplore( float32 deltaTime );
        void               stepMovement( float32 deltaTime );
        void               stepPlayerCombat( float32 deltaTime );
        void               stepEnemies( float32 deltaTime );
        /** @brief 지금 자리의 방 · 줍는 것 · 시체를 봅니다. */
        void stepMarkers();
        /** @brief 바라보는 쪽 사거리 안의 적을 모두 칩니다. */
        void swing();
        void hitEnemy( MetroidvaniaEnemy& enemy );
        /** @brief 곁의 지점을 씁니다 — 열고, 쉬는 곳이면 쉬고 그 지역 지도를 사고, 빠른 이동 정거장이면 다음 정거장으로 간다. */
        void interactWithSite();
        void restAt( const MetroidvaniaSiteDef& site );
        void collectPickup( const hashed_string& pickupID );
        void killPlayer();
        /** @brief 되살아날 지점(없으면 시작 자리)에 몸을 세웁니다. */
        void            respawnPlayer();
        optional<int32> findSiteInReach() const;
        float2          findSitePosition( const hashed_string& siteID ) const;
        /** @brief 자리 @p position 을 품은 방입니다. 어느 방도 아니면 nullptr 입니다. */
        const AreaDef* findAreaAt( const float2& position ) const;
        void           settle();

    private:
        PROPERTY( Category = "Data", DisplayName = "Area Data", Tooltip = "Area graph of the rooms, rectangles in tile units (required)" )
        AssetRef<AreaGraph> _areaDataPath;
        PROPERTY( Category = "Map", DisplayName = "Terrain Rows", Tooltip = "Terrain text rows, top first ('#' wall, '-' one-way, 'H' ladder, '^' hazard, anything else empty)" )
        vector<string> _listTerrainRow;
        PROPERTY( Category = "Map", DisplayName = "Start", Units = m )
        float2 _startPosition;
        PROPERTY( Category = "Map", DisplayName = "Fall Y", Tooltip = "Below this Y the player dies", Units = m )
        float32 _fallY;
        PROPERTY( Category = "Map", DisplayName = "Reach", Tooltip = "Distance inside which pickups are taken and sites used", Min = 0.1, Units = m )
        float32 _markerReach;
        PROPERTY( Category = "Map", DisplayName = "Enemies", Tooltip = "Enemy ids (catalog), one per position" )
        vector<CatalogKey<MetroidvaniaEnemyDef>> _listEnemyKind;
        PROPERTY( Category = "Map", DisplayName = "Enemy Positions", Units = m )
        vector<float2> _listEnemyPosition;
        PROPERTY( Category = "Map", DisplayName = "Pickups", Tooltip = "Pickup ids (catalog), one per position" )
        vector<CatalogKey<MetroidvaniaPickupDef>> _listPickup;
        PROPERTY( Category = "Map", DisplayName = "Pickup Positions", Units = m )
        vector<float2> _listPickupPosition;
        PROPERTY( Category = "Map", DisplayName = "Sites", Tooltip = "Site ids (catalog), one per position" )
        vector<CatalogKey<MetroidvaniaSiteDef>> _listSite;
        PROPERTY( Category = "Map", DisplayName = "Site Positions", Units = m )
        vector<float2> _listSitePosition;
        PROPERTY( Category = "Map", DisplayName = "Goal Flag", Tooltip = "Boss flag that wins the game (required)" )
        hashed_string _goalFlag;
        PROPERTY( Category = "Player", DisplayName = "Damage", Min = 0.0 )
        float32 _playerDamage;
        PROPERTY( Category = "Player", DisplayName = "Poise Damage", Min = 0.0 )
        float32 _playerPoiseDamage;
        PROPERTY( Category = "Player", DisplayName = "Reach", Min = 0.1, Units = m )
        float32 _playerReach;
        PROPERTY( Category = "Player", DisplayName = "Attack Interval", Min = 0.05, Units = s )
        float32 _playerAttackInterval;
        PROPERTY( Category = "Enemy", DisplayName = "Damage", Min = 0.0 )
        float32 _enemyDamage;
        PROPERTY( Category = "Enemy", DisplayName = "Boss Damage", Min = 0.0 )
        float32 _bossDamage;
        PROPERTY( Category = "Enemy", DisplayName = "Boss Damage Taken Scale", Min = 0.01, Max = 1.0 )
        float32 _bossDamageTakenScale;
        PROPERTY( Category = "Enemy", DisplayName = "Reach", Min = 0.1, Units = m )
        float32 _enemyReach;
        PROPERTY( Category = "Enemy", DisplayName = "Startup", Tooltip = "Seconds from the attack pose to the hit (the parry window)", Min = 0.05, Units = s )
        float32 _enemyStartup;
        PROPERTY( Category = "Enemy", DisplayName = "Interval", Min = 0.05, Units = s )
        float32 _enemyInterval;
        PROPERTY( Category = "Result", DisplayName = "Respawn Delay", Min = 0.05, Units = s )
        float32 _respawnDelaySeconds;
        PROPERTY( Category = "Result", DisplayName = "Result Hold", Tooltip = "Seconds the result stays on screen before the minigame finishes", Min = 0.1, Units = s )
        float32 _resultHoldSeconds;
        PROPERTY( Category = "Result", DisplayName = "Seed", Tooltip = "Seed of the loot rolls (not 0)", Min = 1 )
        uint32 _seed;

        SharedAssetRef<MetroidvaniaCatalog> _pCatalog;
        SharedAssetRef<AreaGraph>           _pAreas; ///< 방 정의(탐색 상태는 `_graph` 사본이 가진다)
        AreaGraph                           _graph;  ///< 방문 · 발견
        PlatformTileMap                     _terrain;
        PlatformerMotor2D                   _motor;
        MetroidvaniaAbilitySet              _abilities;
        MetroidvaniaMapState                _mapState;  ///< `_graph` 를 빌린다
        MetroidvaniaDeathRestState          _deathRest; ///< `_wallet` 을 빌린다
        MetroidvaniaTrinketLoadout          _loadout;
        MetroidvaniaDuelist                 _player;
        Wallet                              _wallet;
        GameFlags                           _flags;
        GameRandom                          _random;
        FixedStepTimer                      _timer;
        Countdown                           _attackCooldown;
        Countdown                           _respawnDelay;
        Countdown                           _resultHold;
        vector<MetroidvaniaEnemy>           _listEnemy;
        vector<MetroidvaniaDeathRestEvent>  _listEvent;
        MetroidvaniaPlayerCommand           _command; ///< 마지막 입력 명령(누름은 다음 걸음까지 모은다) — 클라이언트 입력이라 싣지 않는다
        PROPERTY( RuntimeState )
        hashed_string _area; ///< 지금 방(어느 방도 아니면 마지막 방)
        PROPERTY( RuntimeState )
        MetroidvaniaPhase _phase;
    };
} // namespace sw
