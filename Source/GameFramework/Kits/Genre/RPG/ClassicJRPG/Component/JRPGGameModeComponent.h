/**
 * @file JRPGGameModeComponent.h
 * @brief 클래식 JRPG 키트(`GF_ClassicJRPG`)의 디렉터 — 길 하나를 걸어 걸음 조우 전투를 넘고 길 끝의 보스를 이기는 한 판을 진행합니다.
 *        키트의 파티(`JRPGParty`) · 걸음 조우(`JRPGEncounterWalker`) · 전투(`JRPGBattle`)를 씬 데이터로 열고 명령으로 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈
 *          (`GF_Client_ClassicJRPG` 의 `JRPGAvatarComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          전투는 살아 있는 멤버마다 명령을 하나씩 받고(`submitCommand`), 마지막 멤버의 명령이 라운드를 돌린다. 쉼터에서는 돈을 내고 쉬며 쓰러진 멤버도 일으킨다.
 *          보스를 이기면 승리, 파티가 모두 쓰러지면 패배다. 판이 끝나면 `_resultHoldSeconds` 뒤 미니게임을 끝내고(디렉터 계약 ③) 번 돈을 결과 값으로 낸다.
 *          장르 완결 루프 시나리오: `game/playground/automation/classicjrpg_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGBattle.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGCatalog.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGEncounter.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGParty.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    /** @brief 클래식 JRPG 판의 단계입니다(감시 값 `JRPG.Phase` 의 수). */
    ENUM()
    enum class JRPGGamePhase : uint8
    {
        Explore = 0, ///< 길을 걷는다
        Battle,      ///< 전투 명령을 기다린다
        Victory,     ///< 보스를 이겼다
        Defeat       ///< 파티가 모두 쓰러졌다
    };
} // namespace sw

namespace sw
{
    /**
     * @class JRPGGameModeComponent
     * @brief 걷기(걸음 조우) ↔ 전투(멤버마다 명령 하나) → 길 끝의 보스 → 승리 · 패배를 명령으로 진행합니다.
     */
    REFLECT( Category = "ClassicJRPG", DisplayName = "JRPG Game Mode", Tooltip = "Runs one ClassicJRPG trail: step encounters, round battles from commands and the boss at the end" )
    class SW_KIT_API JRPGGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        JRPGGameModeComponent();
        virtual ~JRPGGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(게임 스레드) ----
        /** @brief 걷는 축을 넘깁니다. 다음 걸음이 쓰고 마지막 값으로 덮습니다. 걷는 단계가 아니면 받지 않고 false 입니다. */
        [[nodiscard]] bool submitMove( float2 move );
        /**
         * @brief 명령을 기다리는 멤버(`findCommandMember`)의 이번 라운드 명령을 정합니다. 살아 있는 마지막 멤버의 명령이면 라운드를 돌립니다.
         * @return 명령을 기다리지 않거나 쓸 수 없는 명령(MP 부족, 모르는 주문, 없는 대상)이면 아무것도 바꾸지 않고 false 입니다.
         */
        [[nodiscard]] bool submitCommand( const JRPGCommand& command );
        /** @brief 이번 전투에서 도망칩니다. 전투 중이 아니거나 보스전이면 false 입니다. 도망에 실패하면 적만 움직이고 true 입니다(명령은 썼다). */
        [[nodiscard]] bool submitFlee();
        /** @brief 쉼터에서 쉽니다 — 쓰러진 멤버를 일으키고(멤버마다 교회 값) 모두 회복합니다(여관 값). 쉼터 밖이거나 돈이 모자라면 false 입니다. */
        [[nodiscard]] bool submitRest();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 이 판의 카탈로그입니다. 열기 전이면 nullptr 입니다. */
        const JRPGCatalog* findCatalog() const { return _pCatalog.get(); }
        const JRPGParty&   getParty() const { return _party; }
        const JRPGBattle&  getBattle() const { return _battle; }
        JRPGGamePhase      getGamePhase() const { return _phase; }
        int32              getPhase() const { return static_cast<int32>( _phase ); }
        /** @brief 명령을 기다리는 멤버 번호입니다. 전투 중이 아니면 비어 있습니다. */
        [[nodiscard]] optional<int32> findCommandMember() const;
        /** @brief 명령을 기다리는 멤버 번호입니다(감시 값). 기다리지 않으면 -1 입니다. */
        int32 getCommandMember() const;
        /** @brief 지금 전투에서 살아 있는 적 수입니다. 전투 중이 아니면 0 입니다. */
        int32 getEnemiesAlive() const;
        int32 getBattleRound() const { return _battle.getRound(); }
        /** @brief 이 판에서 연 전투 수입니다. */
        int32 getBattleCount() const { return _battleCount; }
        int32 getGold() const;
        /** @brief 파티 맨 앞 멤버의 HP · 경험치입니다(감시 값). 판이 열리기 전이면 0 입니다. */
        int32            getLeaderHP() const;
        int32            getLeaderXP() const;
        AbsolutePosition getPlayerPosition() const { return toAbsolute( _playerPosition ); }
        AbsolutePosition getGoalPosition() const { return toAbsolute( _goalPosition ); }
        AbsolutePosition getRestPosition() const { return toAbsolute( _restPosition ); }
        float32          getPlayerX() const { return _playerPosition._x; }
        /** @brief 쉼터를 쓸 수 있는 거리 안이면 true 입니다. */
        bool isNearRest() const { return isNear( _restPosition ); }

        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        /** @brief 번 돈(지갑 - 시작 돈)을 냅니다. 패배한 판은 내지 않습니다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 땅 평면 자리를 절대 자리로 바꿉니다(시뮬레이션은 높이를 모른다). */
        static AbsolutePosition toAbsolute( const float2& position ) { return AbsolutePosition{
            float3{ position._x, 0.0f, position._y }
 }; }
        /** @brief 씬 데이터(영웅 · 지역 · 보스)를 카탈로그로 봅니다. 모르는 id 가 있거나 비었으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateSetup() const;
        /** @brief 씬 데이터의 영웅으로 파티를 엽니다. */
        [[nodiscard]] bool openParty();
        bool               isNear( const float2& target ) const;
        void               updateExplore( float32 deltaTime );
        /** @brief 적 무리로 전투를 열고 첫 멤버의 명령을 기다립니다. */
        void openBattle( const vector<hashed_string>& listEnemyID, bool bBoss );
        /** @brief @p firstMember 부터 살아 있는 첫 멤버입니다. 없으면 비어 있습니다. */
        [[nodiscard]] optional<int32> findLivingMember( int32 firstMember ) const;
        /** @brief 라운드 뒤: 알림을 로그로 내고 승패를 정합니다. */
        void settleBattle();
        /** @brief 승리 · 패배로 판을 끝내고 결과를 보여 주는 시간을 셉니다. */
        void finishGame( JRPGGamePhase phase );

    private:
        PROPERTY( Category = "Party", DisplayName = "Heroes", Tooltip = "Class ids (catalog) of the party, one member each" )
        vector<CatalogKey<JRPGClassDef>> _listHero;
        PROPERTY( Category = "Trail", DisplayName = "Area", Tooltip = "Encounter area (catalog) rolled on every step" )
        CatalogKey<JRPGAreaDef> _area;
        PROPERTY( Category = "Trail", DisplayName = "Boss", Tooltip = "Enemy ids (catalog) waiting at the goal" )
        vector<CatalogKey<JRPGEnemyDef>> _listBoss;
        PROPERTY( Category = "Trail", DisplayName = "Start", Tooltip = "Absolute ground-plane position (X, Z) the party starts at" )
        float2 _startPosition;
        PROPERTY( Category = "Trail", DisplayName = "Goal", Tooltip = "Absolute ground-plane position (X, Z) of the boss" )
        float2 _goalPosition;
        PROPERTY( Category = "Trail", DisplayName = "Rest", Tooltip = "Absolute ground-plane position (X, Z) of the inn" )
        float2 _restPosition;
        PROPERTY( Category = "Trail", DisplayName = "Trail Min", Tooltip = "Lowest ground-plane corner the party can walk to" )
        float2 _trailMin;
        PROPERTY( Category = "Trail", DisplayName = "Trail Max", Tooltip = "Highest ground-plane corner the party can walk to" )
        float2 _trailMax;
        PROPERTY( Category = "Trail", DisplayName = "Use Radius", Tooltip = "Distance at which the inn and the boss react", Min = 0.1, Units = m )
        float32 _useRadius;
        PROPERTY( Category = "Trail", DisplayName = "Move Speed", Min = 0.0, Units = "m/s" )
        float32 _moveSpeed;
        PROPERTY( Category = "Trail", DisplayName = "Step Length", Tooltip = "Distance walked per encounter step", Min = 0.1, Units = m )
        float32 _stepLength;
        PROPERTY( Category = "Battle", DisplayName = "Battle Settings" )
        JRPGBattleSettings _battleSettings;
        PROPERTY( Category = "Battle", DisplayName = "Seed", Min = 1 )
        uint32 _seed;
        PROPERTY( Category = "Economy", DisplayName = "Start Gold", Min = 0 )
        int32 _startGold;
        PROPERTY( Category = "Economy", DisplayName = "Inn Price", Tooltip = "Gold per living member to rest", Min = 0 )
        int32 _innPrice;
        PROPERTY( Category = "Economy", DisplayName = "Revive Price", Tooltip = "Gold per level to revive a fallen member", Min = 0 )
        int32 _revivePrice;
        PROPERTY( Category = "Result", DisplayName = "Result Hold", Tooltip = "Seconds the result stays on screen before the minigame finishes", Min = 0.1, Units = s )
        float32 _resultHoldSeconds;

        SharedAssetRef<JRPGCatalog> _pCatalog;
        JRPGParty                   _party;
        Wallet                      _wallet; ///< 이 판의 돈(파티가 빌린다) — 결과 값으로만 바탕 판에 나간다
        JRPGEncounterWalker         _walker;
        JRPGBattle                  _battle;
        vector<JRPGBattleEvent>     _listBattleEvent;
        vector<JRPGPartyEvent>      _listPartyEvent;
        Countdown                   _resultHold; ///< 판이 끝난 뒤 미니게임을 끝내기까지(결과를 보여 주는 시간)
        PROPERTY( RuntimeState )
        float2 _playerPosition;
        float2 _commandMove; ///< 마지막 입력 명령의 이동 축 — 클라이언트가 프레임마다 덮는 입력이라 싣지 않는다
        PROPERTY( RuntimeState, Min = 0 )
        float32 _strideProgress; ///< 다음 걸음까지 걸은 거리(m)
        PROPERTY( RuntimeState, Min = 0 )
        int32 _commandMember; ///< 전투 중 명령을 기다리는 멤버
        PROPERTY( RuntimeState, Min = 0 )
        int32 _battleCount; ///< 연 전투 수 — 전투마다의 난수 시드가 된다(저장을 거쳐도 같은 전투가 나온다)
        PROPERTY( RuntimeState )
        JRPGGamePhase _phase;
        PROPERTY( RuntimeState, Min = 0, Max = 1 )
        uint8 _bBossBattle;
    };
} // namespace sw
