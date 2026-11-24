/**
 * @file MonsterCollectorGameModeComponent.h
 * @brief 몬스터 수집 키트(`GF_MonsterCollector`)의 디렉터 — 풀밭을 걸어 야생 몬스터를 만나고 정한 수를 잡는 한 판을 진행합니다.
 *        키트의 전투(`MonsterBattle`) · 보관함(`MonsterStorage`) · 조우 표(`MonsterCollectorCatalog`)를 씬 데이터로 열고 명령으로 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈
 *          (`GF_Client_MonsterCollector` 의 `MonsterCollectorAvatarComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          전투는 명령 하나가 라운드 하나다(야생 쪽은 기대 피해가 가장 큰 기술을 쓴다). 쓰러진 개체를 바꾸는 동안은 교체 명령만 받는다. 쉼터에서는 파티를 회복하고 볼을 채운다.
 *          정한 수를 잡으면 승리, 파티가 모두 쓰러지면 패배다. 판이 끝나면 `_resultHoldSeconds` 뒤 미니게임을 끝내고(디렉터 계약 ③) 잡은 수만큼의 보상을 결과 값으로 낸다.
 *          장르 완결 루프 시나리오: `game/playground/automation/monstercollector_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterBattle.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterCollectorCatalog.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterInstance.h"
#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/World/Encounter/StepEncounter.h"

namespace sw
{
    /** @brief 몬스터 수집 판의 단계입니다(감시 값 `MonsterCollector.Phase` 의 수). */
    ENUM()
    enum class MonsterCollectorPhase : uint8
    {
        Explore = 0, ///< 풀밭을 걷는다
        Battle,      ///< 야생 전투 명령을 기다린다
        Victory,     ///< 정한 수를 잡았다
        Defeat       ///< 파티가 모두 쓰러졌다
    };
} // namespace sw

namespace sw
{
    /**
     * @class MonsterCollectorGameModeComponent
     * @brief 걷기(걸음 조우) ↔ 야생 전투(기술 · 볼 · 교체 · 도망) → 정한 수 포획 · 전멸을 명령으로 진행합니다.
     */
    REFLECT( Category = "MonsterCollector", DisplayName = "Monster Collector Game Mode", Tooltip = "Runs one MonsterCollector outing: step encounters, wild battles from commands and a capture goal" )
    class SW_KIT_API MonsterCollectorGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        MonsterCollectorGameModeComponent();
        virtual ~MonsterCollectorGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(게임 스레드) ----
        /** @brief 걷는 축을 넘깁니다. 다음 걸음이 쓰고 마지막 값으로 덮습니다. 걷는 단계가 아니면 받지 않고 false 입니다. */
        [[nodiscard]] bool submitMove( float2 move );
        /** @brief 나선 개체의 기술 슬롯 @p moveSlot 으로 라운드를 돌립니다. 전투 중이 아니거나 빈 슬롯이거나 교체를 기다리면 false 입니다. */
        [[nodiscard]] bool submitAttack( int32 moveSlot );
        /** @brief 볼 하나를 던져 라운드를 돌립니다. 볼이 없거나 전투 명령을 받지 않으면 false 입니다. */
        [[nodiscard]] bool submitBall();
        /** @brief 파티의 @p partyIndex 로 바꿉니다. 쓰러진 자리를 채우는 교체는 라운드를 쓰지 않습니다. 바꿀 수 없으면 false 입니다. */
        [[nodiscard]] bool submitSwitch( int32 partyIndex );
        /** @brief 야생 전투에서 도망칩니다. 실패하면 상대만 움직입니다. 전투 명령을 받지 않으면 false 입니다. */
        [[nodiscard]] bool submitRun();
        /** @brief 쉼터에서 파티를 모두 회복하고 볼을 처음 수로 채웁니다. 쉼터 밖이면 false 입니다. */
        [[nodiscard]] bool submitRest();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 이 판의 카탈로그입니다. 열기 전이면 nullptr 입니다. */
        const MonsterCollectorCatalog* findCatalog() const { return _pCatalog.get(); }
        const MonsterStorage&          getStorage() const { return _storage; }
        const MonsterBattle&           getBattle() const { return _battle; }
        MonsterCollectorPhase          getGamePhase() const { return _phase; }
        int32                          getPhase() const { return static_cast<int32>( _phase ); }
        /** @brief 지금 나선 개체입니다(걷는 동안은 파티 맨 앞). 판이 열리기 전이면 nullptr 입니다. */
        const MonsterInstance* findLead() const;
        /** @brief 쓰러진 개체 대신 나설 개체를 고르기를 기다리면 true 입니다. */
        bool isAwaitingSwitch() const { return _phase == MonsterCollectorPhase::Battle && _battle.needsSwitch( MonsterBattle::kPlayerSide ); }
        /** @brief 야생 상대의 HP 입니다. 전투 중이 아니면 0 입니다. */
        int32 getFoeHP() const;
        int32 getRemainingBallCount() const { return _remainingBallCount; }
        int32 getCaptureCount() const { return _captureCount; }
        int32 getCaptureGoal() const { return _captureGoal; }
        /** @brief 이 판에서 연 전투 수입니다. */
        int32 getBattleCount() const { return _battleCount; }
        int32 getPartySize() const { return static_cast<int32>( _storage.getParty().size() ); }
        /** @brief 파티 맨 앞 개체의 총 경험치입니다(감시 값). 판이 열리기 전이면 0 입니다. */
        int32            getLeaderXP() const;
        AbsolutePosition getPlayerPosition() const { return toAbsolute( _playerPosition ); }
        AbsolutePosition getRestPosition() const { return toAbsolute( _restPosition ); }
        float32          getPlayerX() const { return _playerPosition._x; }
        /** @brief 쉼터를 쓸 수 있는 거리 안이면 true 입니다. */
        bool isNearRest() const;

        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        /** @brief 잡은 수 × `_captureReward` 를 돈으로 냅니다. 패배한 판은 내지 않습니다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 땅 평면 자리를 절대 자리로 바꿉니다(시뮬레이션은 높이를 모른다). */
        static AbsolutePosition toAbsolute( const float2& position ) { return AbsolutePosition{
            float3{ position._x, 0.0f, position._y }
 }; }
        /** @brief 씬 데이터의 시작 개체로 보관함을 엽니다. 모르는 종이거나 하나도 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openStorage();
        void               updateExplore( float32 deltaTime );
        /** @brief 조우 표에서 야생 개체를 뽑아 전투를 엽니다. 맞는 조우가 없으면 걷기를 잇습니다. */
        void openBattle();
        /** @brief 플레이어 행동과 야생의 행동으로 라운드 하나를 돌립니다. 쓸 수 없는 행동이면 false 입니다. */
        [[nodiscard]] bool playRound( const MonsterAction& action );
        /** @brief 야생의 기술 슬롯입니다 — 기대 피해가 가장 큰 것, 쓸 것이 없으면 비어 있습니다. */
        [[nodiscard]] optional<int32> findFoeMoveSlot() const;
        /** @brief 전투 알림의 소리를 쌓습니다 — 쓴 기술은 기술 표의 소리, 나머지는 이 판의 이벤트다. */
        void queueBattleSound( const MonsterBattleEvent& event );
        /** @brief 라운드 뒤: 알림을 로그로 내고 전투가 끝났으면 파티를 돌려받아 다음 단계로 넘깁니다. */
        void settleBattle();
        /** @brief 전투가 끝난 뒤 진화 조건이 맞은 파티 개체를 진화시킵니다. */
        void evolveParty();
        /** @brief 승리 · 패배로 판을 끝내고 결과를 보여 주는 시간을 셉니다. */
        void finishGame( MonsterCollectorPhase phase );

    private:
        PROPERTY( Category = "Rules", DisplayName = "Element Chart", Tooltip = "Type chart of the battles (required)" )
        AssetRef<ElementChart> _elementChart;
        PROPERTY( Category = "Party", DisplayName = "Starters", Tooltip = "Species ids (catalog) of the starting party" )
        vector<CatalogKey<MonsterSpeciesDef>> _listStarter;
        PROPERTY( Category = "Party", DisplayName = "Starter Level", Min = 1, Max = 100 )
        int32 _starterLevel;
        PROPERTY( Category = "Party", DisplayName = "Box Capacity", Min = 0 )
        int32 _boxCapacity;
        PROPERTY( Category = "Encounter", DisplayName = "Area", Tooltip = "Area name of the encounter tables (catalog)" )
        hashed_string _area;
        PROPERTY( Category = "Encounter", DisplayName = "Time Of Day", Tooltip = "Time group of the encounter tables" )
        hashed_string _timeOfDay;
        PROPERTY( Category = "Encounter", DisplayName = "Rate", Tooltip = "Encounter chance per step", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _encounterRate;
        PROPERTY( Category = "Encounter", DisplayName = "Grace Steps", Tooltip = "Steps without an encounter after the previous one", Min = 0 )
        int32 _graceSteps;
        PROPERTY( Category = "Capture", DisplayName = "Balls", Tooltip = "Balls carried at the start and after a rest", Min = 0 )
        int32 _ballCount;
        PROPERTY( Category = "Capture", DisplayName = "Ball Multiplier", Min = 0.1 )
        float32 _ballMultiplier;
        PROPERTY( Category = "Capture", DisplayName = "Goal", Tooltip = "Captures that finish the outing", Min = 1 )
        int32 _captureGoal;
        PROPERTY( Category = "Capture", DisplayName = "Reward", Tooltip = "Money per capture reported to the hub", Min = 0 )
        int32 _captureReward;
        PROPERTY( Category = "Field", DisplayName = "Start", Tooltip = "Absolute ground-plane position (X, Z) the player starts at" )
        float2 _startPosition;
        PROPERTY( Category = "Field", DisplayName = "Rest", Tooltip = "Absolute ground-plane position (X, Z) of the camp" )
        float2 _restPosition;
        PROPERTY( Category = "Field", DisplayName = "Field Min", Tooltip = "Lowest ground-plane corner the player can walk to" )
        float2 _fieldMin;
        PROPERTY( Category = "Field", DisplayName = "Field Max", Tooltip = "Highest ground-plane corner the player can walk to" )
        float2 _fieldMax;
        PROPERTY( Category = "Field", DisplayName = "Use Radius", Tooltip = "Distance at which the camp can be used", Min = 0.1, Units = m )
        float32 _useRadius;
        PROPERTY( Category = "Field", DisplayName = "Move Speed", Min = 0.0, Units = "m/s" )
        float32 _moveSpeed;
        PROPERTY( Category = "Field", DisplayName = "Body Radius", Tooltip = "Radius of the walking body against the static bodies of the scene", Min = 0.05, Units = m )
        float32 _bodyRadius;
        PROPERTY( Category = "Field", DisplayName = "Step Length", Tooltip = "Distance walked per encounter step", Min = 0.1, Units = m )
        float32 _stepLength;
        PROPERTY( Category = "Rules", DisplayName = "Seed", Min = 1 )
        uint32 _seed;
        PROPERTY( Category = "Result", DisplayName = "Result Hold", Tooltip = "Seconds the result stays on screen before the minigame finishes", Min = 0.1, Units = s )
        float32 _resultHoldSeconds;

        SharedAssetRef<MonsterCollectorCatalog> _pCatalog;
        SharedAssetRef<ElementChart>            _pChart;
        MonsterStorage                          _storage;
        MonsterBattle                           _battle;
        StepEncounterCounter                    _encounterCounter;
        GameRandom                              _random;
        vector<MonsterBattleEvent>              _listBattleEvent;
        Countdown                               _resultHold; ///< 판이 끝난 뒤 미니게임을 끝내기까지(결과를 보여 주는 시간)
        PROPERTY( RuntimeState )
        float2 _playerPosition;
        float2 _commandMove; ///< 마지막 입력 명령의 이동 축 — 클라이언트가 프레임마다 덮는 입력이라 싣지 않는다
        PROPERTY( RuntimeState, Min = 0 )
        float32 _strideProgress; ///< 다음 걸음까지 걸은 거리(m)
        PROPERTY( RuntimeState, Min = 0 )
        int32 _remainingBallCount;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _captureCount;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _battleCount; ///< 연 전투 수 — 전투마다의 난수 시드가 된다(저장을 거쳐도 같은 전투가 나온다)
        PROPERTY( RuntimeState )
        MonsterCollectorPhase _phase;
    };
} // namespace sw
