/**
 * @file KartGameModeComponent.h
 * @brief 카트 레이싱 키트(`GF_KartRacing`)의 디렉터 — 경기 한 판을 진행합니다. 키트의 트랙(`KartTrack`) · 경기(`KartRace`)를 씬 데이터로 열고, 사람 차는 명령(`submitDriverInput`)으로, 나머지는 AI 로 달립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_KartRacing` 의 `KartRaceViewComponent`)이
 *          이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다. 자동 플레이(`isAutoPlayOn`)면 사람 차도 AI 가 몬다.
 *          트랙 좌표는 절대 자리다(씬의 원점이 옮겨져도 경기는 그대로다). 트랙은 게임 기능이 건 카탈로그(`KartTrackCatalog`), 아이템 표 · 차 튜닝은 이 칸들이 고른다.
 *          순위가 정해지면 결과를 잠시 보이고 미니게임을 끝낸다 — 1 등이면 `win`, 아니면 `lose` 이고 순위 상금을 결과 값으로 낸다.
 *          시나리오: `game/playground/automation/kartracing.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/KartItems.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/KartRace.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/KartTrack.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Movement/KinematicVehicleMotor.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    struct AbsolutePosition;

    /**
     * @class KartGameModeComponent
     * @brief 출발 격자 → 카운트다운 → 바퀴 → 결승 순위 → 결과를 진행합니다.
     */
    REFLECT( Category = "KartRacing", DisplayName = "Kart Race Game Mode", Tooltip = "Runs one KartRacing race: the player kart from driver commands, the rivals by the kart AI, to the finish places" )
    class SW_KIT_API KartGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 사람 차의 번호입니다 — 늘 먼저 세운다(격자 맨 앞). */
        static constexpr int32 kPlayerRacer = 0;

        KartGameModeComponent();
        virtual ~KartGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /** @brief 사람 차의 이번 프레임 입력입니다(눌림은 다음 고정 걸음까지 남는다 — `KartRace::setInput`). 입력을 받지 않는 때(`isAcceptingInput`)면 false 입니다. */
        [[nodiscard]] bool submitDriverInput( const KartRacerInput& input );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 경기가 열렸고 사람이 모는 차가 결승 전이면 true 입니다(자동 플레이면 false). */
        bool             isAcceptingInput() const;
        const KartRace&  getRace() const { return _race; }
        const KartTrack& getTrack() const { return _track; }
        /** @brief 차 @p racer 의 절대 자리입니다. 없는 번호면 원점입니다. */
        AbsolutePosition getRacerPosition( int32 racer ) const;
        int32            getPhase() const { return static_cast<int32>( _race.getPhase() ); }
        int32            getPlayerPlace() const;
        int32            getPlayerFinishPlace() const;
        float32          getPlayerSpeed() const;
        int32            getFinishedCount() const;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 지금 카탈로그로 트랙을 풀고 @p outRace 를 열어 차를 모두 세웁니다(출발은 하지 않는다). 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openRace( KartRace& outRace );
        /** @brief 경기 이벤트를 로그로 냅니다. */
        void reportEvents();
        /** @brief 플레이어 차의 알림에 맞는 소리를 쌓습니다. */
        void queuePlayerSound( const KartRaceEvent& event );

    private:
        PROPERTY( Category = "Data", DisplayName = "Track", Required, Tooltip = "Track id in the catalog" )
        CatalogKey<KartTrackDef> _trackID;
        PROPERTY( Category = "Data", DisplayName = "Items On", Tooltip = "Races with item boxes; off is the only way to race without items" )
        bool _bItems;
        PROPERTY( Category = "Data", DisplayName = "Items", Tooltip = "Item and rank table file (required while items are on)", EditCondition = "_bItems" )
        AssetRef<KartItemCatalog> _itemCatalog;
        PROPERTY( Category = "Data", DisplayName = "Vehicle", Required, Tooltip = "Vehicle tuning every kart drives (*.vehicle.xml)" )
        AssetRef<KinematicVehicleSettings> _vehicle;
        PROPERTY( Category = "Race", DisplayName = "Bots", Tooltip = "Karts driven by the AI", Min = 1, Max = 7 )
        int32 _botCount;
        PROPERTY( Category = "Race", DisplayName = "Countdown", Min = 0.0, Units = s )
        float32 _countdownSeconds;
        PROPERTY( Category = "Race", DisplayName = "Finish Timeout", Tooltip = "Time after the first finish before the rest is ranked as it stands", Min = 0.0, Units = s )
        float32 _finishTimeoutSeconds;
        PROPERTY( Category = "Race", DisplayName = "Result Hold", Tooltip = "Time the places stay on screen before the minigame ends", Min = 0.1, Units = s )
        float32 _resultHoldSeconds;
        PROPERTY( Category = "Race", DisplayName = "Seed" )
        uint32 _seed;
        PROPERTY( Category = "Race", DisplayName = "First Reward", Tooltip = "Money for first place", Min = 0 )
        int32 _firstReward;
        PROPERTY( Category = "Race", DisplayName = "Reward Step", Tooltip = "Money less for every place after the first (never below 0)", Min = 0 )
        int32 _rewardStep;

        SharedAssetRef<KartTrackCatalog>         _pTrackCatalog;
        SharedAssetRef<KartItemCatalog>          _pItemCatalog;       ///< 아이템을 끈 경기면 비었다
        SharedAssetRef<ProjectileCatalog>        _pProjectileCatalog; ///< 아이템이 쏘는 탄(아이템을 끈 경기면 비었다)
        SharedAssetRef<KinematicVehicleSettings> _pVehicle;
        KartTrack                                _track; ///< 경기 · 차가 빌려 쓴다 — 경기보다 먼저 선언한다
        KartRace                                 _race;
        vector<KartRaceEvent>                    _listEvent;
        Countdown                                _resultHold; ///< 순위가 정해진 뒤 결과를 보이는 시간
        PROPERTY( RuntimeState )
        uint8 _bResultShown : 1; ///< 결과 보이기를 시작했다
        PROPERTY( RuntimeState )
        uint8                  _bFinishRequested : 1; ///< 미니게임 끝내기를 맡겼다
        [[maybe_unused]] uint8 _reserved         : 6;
    };
} // namespace sw
