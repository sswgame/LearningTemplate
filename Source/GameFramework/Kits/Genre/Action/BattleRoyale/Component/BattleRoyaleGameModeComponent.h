/**
 * @file BattleRoyaleGameModeComponent.h
 * @brief 배틀로얄 키트(`GF_BattleRoyale`)의 디렉터 — 낙하(비행기 → 자유 낙하 → 낙하산) → 좁아지는 구역의 전투 → 결과 한 판을 진행합니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트 — `BattleRoyaleMatch` · `BattleRoyaleFlightPath` · `BattleRoyaleSkydiver` · `BattleRoyaleGroundItem`)을 들고 씬과 잇는다.
 *          입력 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_BattleRoyale` 의 `BattleRoyaleViewComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          0 번이 플레이어이고 나머지는 컴퓨터다(같은 분대의 동료 포함). 몸 · 사격은 여기서 최소로 둔다 — 평면을 걷고 사거리 안의 가장 가까운 보이는 적을 쏜다(명중 확률). 건물(씬의 정적 바디)은 걷기 · 시야를 막고 컴퓨터는 씬 내비메시의 길로 돌아간다(`BattleRoyaleSceneTerrain`). 구역 피해 · 기절 · 부활 · 전멸 · 순위는 키트 규칙이다.
 *          총은 기반 무기 표(`WeaponCatalog` · `ProjectileCatalog`)의 것이고 이 판이 쓰는 것은 `_listWeaponID`(나쁜 것부터)다 — 모두 첫 총으로 시작해 바닥 · 보급 상자에서 더 나은 총과 탄(무기 표의 `ammo` 아이템)을 줍는다. 수치는 카탈로그(`BattleRoyaleCatalog`)와 씬 데이터다. 판이 끝나거나 플레이어 분대가 전멸하면 `_resultHoldSeconds` 뒤 미니게임을 끝낸다(디렉터 계약 ③) — 이기면 결과 값 `money`(`_victoryMoney`).
 *          장르 완결 루프 시나리오: `game/playground/automation/battleroyale_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleCatalog.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleDrop.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleLoot.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleMatch.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleSceneTerrain.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"

namespace sw
{
    /** @brief 판의 단계입니다(감시 값 `BattleRoyale.Phase` 의 수). */
    ENUM()
    enum class BattleRoyalePhase : uint8
    {
        Drop = 0, ///< 비행기에서 내려 땅에 닿기까지(모두 닿으면 전투)
        Battle,   ///< 구역이 좁아지는 전투
        Ended     ///< 판이 끝났거나 플레이어 분대가 전멸했다
    };
} // namespace sw

namespace sw
{
    /**
     * @class BattleRoyaleGameModeComponent
     * @brief 낙하 → 전투 → 결과를 플레이어 입력 명령과 컴퓨터 분대로 진행합니다.
     */
    REFLECT( Category = "BattleRoyale", DisplayName = "Battle Royale Game Mode", Tooltip = "Runs one BattleRoyale match: drop, shrinking zone fight and result; person 0 from input commands, the rest by the computer" )
    class SW_KIT_API BattleRoyaleGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kPlayer      = 0;
        static constexpr int32 kPlayerSquad = 0;

        BattleRoyaleGameModeComponent();
        virtual ~BattleRoyaleGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 이번 프레임의 플레이어 입력을 넘깁니다. 이동 축 · 사격 · 쓰기를 누른 채인지는 마지막 값으로 덮고, 새로 누름은 다음 걸음이 쓸 때까지 모읍니다(뛰어내리기 · 낙하산 · 반자동 총).
         * @details 쓰기를 누른 채 기절한 동료 곁에 서 있으면 살린다.
         * @return 판이 열렸고 끝나지 않아 입력을 받으면 true 입니다(`isAcceptingInput`).
         */
        [[nodiscard]] bool submitPlayerInput( const float2& move, bool bFireHeld, bool bUseHeld, bool bUsePressed );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 끝나지 않아 입력 명령을 받으면 true 입니다. */
        bool                                  isAcceptingInput() const override { return isStarted() && _phase != BattleRoyalePhase::Ended; }
        const BattleRoyaleMatch&              getMatch() const { return _match; }
        const vector<BattleRoyaleGroundItem>& getGroundItems() const { return _listGroundItem; }
        /** @brief 바닥 무더기 목록이 바뀔 때마다 늘어나는 번호입니다 — 뷰가 다시 세울지 정한다. */
        uint32 getGroundItemRevision() const { return _groundItemRevision; }
        int32  getPersonCount() const { return static_cast<int32>( _listPerson.size() ); }
        /** @brief 사람의 절대 자리입니다(비행기 · 낙하 높이 포함). 없는 번호면 원점입니다. */
        AbsolutePosition getPersonPosition( int32 person ) const;
        /** @brief 판 좌표(0..맵 크기)의 자리를 땅 위의 절대 자리로 옮깁니다. */
        AbsolutePosition toAbsolute( const float2& mapPosition ) const { return AbsolutePosition{ float3( mapPosition._x, _groundHeight, mapPosition._y ) }; }
        /** @brief 플레이어의 체력 비율(0..1, 기절 중이면 0)입니다. */
        float32 computePlayerHealthRatio() const;
        bool    isPlayerDowned() const;
        /** @brief 플레이어가 든 총의 아이템입니다(표시 이름의 원본). 판이 열리기 전이면 nullptr 입니다. */
        const ItemDef* findPlayerWeaponItem() const;
        int32          getPlayerMagazine() const { return _listPerson.empty() ? 0 : _listPerson[kPlayer]._weapon.getMagazineAmmo(); }
        /** @brief 플레이어가 든 총에 더 넣을 탄(예비탄과 인벤토리)입니다. */
        int32 getPlayerSpareAmmo() const { return _listPerson.empty() ? 0 : countSpareAmmo( kPlayer ); }
        /** @brief 플레이어 분대가 이겼으면 true 입니다(끝난 뒤). */
        bool              hasWon() const { return _phase == BattleRoyalePhase::Ended && _match.getMatchState().getWinningTeam() == kPlayerSquad; }
        BattleRoyalePhase getMatchPhase() const { return _phase; }
        int32             getPhase() const { return static_cast<int32>( _phase ); }
        int32             getRemaining() const { return _match.countRemainingPlayers(); }
        float32           getZoneRadius() const { return _match.getZone().getRadius(); }
        /** @brief 아직 비행기에서 내리지 않았으면 0, 낙하 중이면 1, 땅에 닿았으면 2 입니다. */
        int32 getPlayerDropStage() const;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateMatch( float32 deltaTime ) override;
        /** @brief 이겼으면 결과 값 `money`(`_victoryMoney`)를 냅니다. 지면 넘길 값이 없다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 사람 하나의 몸입니다(체력 · 장비 · 판 자리는 키트 판이 가진다). 번호는 `BattleRoyaleMatch` 의 사람 번호다. */
        struct Person
        {
            BattleRoyaleSkydiver _skydiver{};
            WeaponState          _weapon{};
            vector<float2>       _listRoute{}; ///< 컴퓨터가 지날 길의 점 — 다음 생각에 다시 구하므로 싣지 않는다(아래도)
            Countdown            _routeHold{};
            float32              _jumpTime{ 0.0f };    ///< 컴퓨터가 뛰어내릴 비행 시각(초)
            int32                _weaponIndex{ 0 };    ///< 든 총(`_listWeaponID` 의 자리 — 클수록 낫다)
            uint8                _bJumped{ SW_FALSE }; ///< 비행기에서 내렸다
        };

        /** @brief 씬 데이터의 분대로 판을 엽니다. 카탈로그 · 분대 구성이 틀리면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openMatch( BattleRoyaleMatch& outMatch ) const;
        /** @brief `_listWeaponID` 의 총마다 기본 탄 · 장비 아이템 · 탄 아이템이 표에 있는지 봅니다. 아니면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateWeapons() const;
        /** @brief 비행 경로와 뛰어내릴 시각 · 첫 총을 정하고 바닥 전리품을 놓습니다. */
        /** @brief 이 판의 씨앗(`_matchSeed`)과 카탈로그로 비행 경로를 정합니다. 비행 설정이 범위 밖이면 false 입니다. */
        [[nodiscard]] bool makeFlightPath();
        [[nodiscard]] bool prepareDrop();
        void               stepDrop( float32 deltaTime );
        void               stepBattle( float32 deltaTime );
        /** @brief 땅에 닿은 사람 @p person 을 맵 안의 설 자리(건물 안이면 그 곁)로 판에 알립니다. */
        void landPerson( int32 person );
        void stepPlayer( float32 deltaTime );
        /** @brief 컴퓨터 하나의 걸음 — 구역 안으로, 기절한 동료에게, 가까운 적에게 순으로 움직이고 닿으면 쏜다. */
        void stepBot( int32 person, float32 deltaTime );
        /** @brief 컴퓨터 @p person 을 @p goal 로 가는 내비메시 길을 따라 한 걸음 옮깁니다. 길은 `_botRouteInterval` 마다 다시 구한다. */
        void walkRoute( int32 person, const float2& goal, float32 deltaTime );
        /** @brief @p person 을 @p direction 으로 한 걸음 옮깁니다. 건물에 막히면 미끄러지고 맵 밖으로 나가지 않는다. */
        void movePerson( int32 person, const float2& direction, float32 speed, float32 deltaTime );
        /** @brief 사거리 안의 가장 가까운 보이는 적에게 방아쇠를 당깁니다. 알마다 명중 확률로 맞힌다(퍼짐 광선은 쓰지 않는다 — 사람은 바디가 없다). */
        void fire( int32 person, bool bTriggerJustPressed, float32 hitChance );
        /** @brief 총 @p weaponIndex 를 탄창을 채워 듭니다(예비탄 없음). 표에 없는 총이면 false 입니다. */
        [[nodiscard]] bool equipWeapon( Person& inoutPerson, int32 weaponIndex ) const;
        /** @brief 아이템 @p itemID 가 나타내는 총의 자리입니다. 이 판의 총이 아니면 없음입니다. */
        optional<int32> findWeaponIndex( const hashed_string& itemID ) const;
        /** @brief 이 판의 총이 쓰는 탄 아이템이면 true 입니다. */
        bool isAmmoItem( const hashed_string& itemID ) const;
        /** @brief 총의 시간을 흘리고, 빈 탄창은 인벤토리의 탄으로 재장전을 시작합니다. */
        void updateWeapon( int32 person, float32 deltaTime );
        /** @brief 탄창이나 더 넣을 탄이 있으면 true 입니다. */
        bool canShoot( int32 person ) const { return _listPerson[static_cast<size_t>( person )]._weapon.getMagazineAmmo() > 0 || countSpareAmmo( person ) > 0; }
        /** @brief 든 총에 더 넣을 탄(예비탄과 인벤토리의 탄 아이템)입니다. */
        int32 countSpareAmmo( int32 person ) const;
        /** @brief 컴퓨터가 가지러 갈 만한 무더기(더 나은 총, 든 총의 모자란 탄) 가운데 @p range 안의 가장 가까운 자리입니다. */
        optional<float2> findWantedItem( int32 person, float32 range ) const;
        /** @brief 닿는 바닥 무더기를 줍고 치웁니다. */
        void pickUpItems( int32 person );
        /** @brief 무더기 하나를 줍습니다 — 더 나은 총은 바꿔 들고, 탄은 인벤토리에, 방어구 · 가방은 빈 자리에. 줍지 못하면 false 입니다(바닥에 남는다). */
        [[nodiscard]] bool tryTakeItem( int32 person, const BattleRoyaleGroundItem& item );

        /** @brief @p range 안에서 @p isCandidate( 사람 ) 이 고른 가장 가까운 다른 사람입니다. 조건은 지금까지의 가장 가까운 사람보다 가까울 때만 묻는다(시야 광선을 아낀다). */
        template <typename TPredicate>
        optional<int32> findNearestPerson( int32 person, float32 range, TPredicate isCandidate ) const;
        /** @brief 맵 어디든 가장 가까운 선 적입니다(보이지 않아도). */
        optional<int32> findNearestEnemy( int32 person ) const;
        optional<int32> findVisibleEnemy( int32 person, float32 range ) const;
        optional<int32> findDownedMate( int32 person, float32 range ) const;
        /** @brief 사람 @p person 의 자리에서 나는 소리를 쌓습니다(플레이어 자신이 아닌 사람의 소리도 들린다). */
        void queuePersonSound( const utf8* pEventName, int32 person );
        /** @brief 사람 @p person 의 총에서 이번 걸음에 난 소리(무기 표)를 그 자리에서 쌓습니다. */
        void queueWeaponSound( int32 person );
        void settle();
        void endGame();

    private:
        PROPERTY( Category = "Squads", DisplayName = "Squad Count", Min = 2 )
        int32 _squadCount;
        PROPERTY( Category = "Squads", DisplayName = "Squad Size", Min = 1 )
        int32 _squadSize;
        PROPERTY( Category = "Match", DisplayName = "Seed", Tooltip = "Seed of the zone, the flight, the loot and the computer people (not 0)", Min = 1 )
        uint32                _seed;
        SW_CODEC_STATE uint32 _matchSeed; ///< 이 판의 씨앗(`startGame` 이 `_seed` 에서 정한다 — 서버 월드는 난수라 싣는다: 다시 읽은 판이 같은 비행 경로 · 구역으로 열린다)
        PROPERTY( Category = "Match", DisplayName = "Ground Height", Units = m )
        float32 _groundHeight;
        PROPERTY( Category = "People", DisplayName = "Player Speed", Min = 0.1, Units = "m/s" )
        float32 _playerSpeed;
        PROPERTY( Category = "People", DisplayName = "Bot Speed", Min = 0.1, Units = "m/s" )
        float32 _botSpeed;
        PROPERTY( Category = "People", DisplayName = "Body Radius", Tooltip = "Radius of the walking body that buildings stop", Min = 0.05, Units = m )
        float32 _bodyRadius;
        PROPERTY( Category = "People", DisplayName = "Eye Height", Tooltip = "Height of sight and fire lines - lower cover does not block them", Min = 0.1, Units = m )
        float32 _eyeHeight;
        PROPERTY( Category = "People", DisplayName = "Bot Route Interval", Tooltip = "Seconds between two navmesh routes of one computer person", Min = 0.05, Units = s )
        float32 _botRouteInterval;
        PROPERTY( Category = "Weapon", DisplayName = "Weapons", Tooltip = "Weapons of this match, worst first - everyone starts with the first and picks up better ones (weapon table ids, each with an item and an ammo item)" )
        vector<CatalogKey<WeaponDef>> _listWeaponID;
        PROPERTY( Category = "Weapon", DisplayName = "Start Reserve Ammo", Min = 0 )
        int32 _startReserveAmmo;
        PROPERTY( Category = "Weapon", DisplayName = "Player Hit Chance", Min = 0.0, Max = 1.0 )
        float32 _playerHitChance;
        PROPERTY( Category = "Weapon", DisplayName = "Bot Hit Chance", Min = 0.0, Max = 1.0 )
        float32 _botHitChance;
        PROPERTY( Category = "Weapon", DisplayName = "Head Chance", Tooltip = "Chance that a hit lands on the head (the helmet absorbs) instead of the body", Min = 0.0, Max = 1.0 )
        float32 _headChance;
        PROPERTY( Category = "Use", DisplayName = "Pickup Radius", Min = 0.1, Units = m )
        float32 _pickupRadius;
        PROPERTY( Category = "Use", DisplayName = "Revive Range", Min = 0.1, Units = m )
        float32 _reviveRange;
        PROPERTY( Category = "Use", DisplayName = "Bot Rescue Range", Tooltip = "A computer person walks to a downed squad mate inside this range", Min = 0.1, Units = m )
        float32 _botRescueRange;
        PROPERTY( Category = "Use", DisplayName = "Bot Loot Range", Tooltip = "A computer person walks to a better weapon or missing ammo inside this range", Min = 0.1, Units = m )
        float32 _botLootRange;
        PROPERTY( Category = "Loot", DisplayName = "Loot Data", Tooltip = "Loot catalog of the loot spots and supply drops (required when any is placed)" )
        AssetRef<LootCatalog> _lootDataPath;
        PROPERTY( Category = "Loot", DisplayName = "Loot Spot Kind", Tooltip = "LootSpot id (catalog) of every loot position" )
        hashed_string _lootSpotKind;
        PROPERTY( Category = "Loot", DisplayName = "Loot Positions", Tooltip = "Map positions of the loot spots", Units = m )
        vector<float2> _listLootPosition;
        PROPERTY( Category = "Result", DisplayName = "Victory Money", Tooltip = "Result value 'money' reported on victory", Min = 0 )
        int32 _victoryMoney;

        BoundAssetRef<BattleRoyaleCatalog>    _pCatalog;
        BoundAssetRef<WeaponCatalog>          _pWeaponCatalog;
        BoundAssetRef<ProjectileCatalog>      _pProjectileCatalog;
        BoundAssetRef<ItemCatalog>            _pItemCatalog;
        BoundAssetRef<LootCatalog>            _pLootCatalog; ///< 전리품 지점 · 보급 상자가 있을 때만 있다
        SW_TRANSIENT BattleRoyaleSceneTerrain _terrain;      ///< 판(`_match`)이 빌린다 — 먼저 만들고 나중에 사라진다
        SW_CODEC_STATE BattleRoyaleMatch      _match;
        SW_TRANSIENT BattleRoyaleFlightPath   _flightPath; ///< 씨앗과 카탈로그로 정해진다 — 싣지 않는다
        SW_CODEC_STATE FixedStepTimer         _timer;
        SW_CODEC_STATE GameRandom             _random;
        SW_CODEC_STATE vector<Person> _listPerson;
        SW_CODEC_STATE vector<BattleRoyaleGroundItem> _listGroundItem;
        SW_TRANSIENT vector<BattleRoyaleEvent> _listEvent;
        SW_TRANSIENT vector<BattleRoyaleZoneEvent> _listZoneEvent;
        PROPERTY( RuntimeState, Min = 0 )
        float32 _flightTime;
        PROPERTY( RuntimeState )
        BattleRoyalePhase      _phase;
        SW_CODEC_STATE uint32  _groundItemRevision;
        SW_TRANSIENT float2    _commandMove; ///< 마지막 입력 명령 — 클라이언트 입력이라 싣지 않는다(아래 깃발도)
        SW_TRANSIENT uint8     _bFireHeld   : 1;
        SW_TRANSIENT uint8     _bUseHeld    : 1;
        SW_TRANSIENT uint8     _bUseQueued  : 1; ///< 다음 걸음이 쓸 쓰기 누름
        SW_TRANSIENT uint8     _bFireQueued : 1; ///< 다음 걸음이 쓸 사격 누름(반자동 총)
        [[maybe_unused]] uint8 _reserved    : 4;
    };
} // namespace sw
