/**
 * @file ConquestGameModeComponent.h
 * @brief 횡스크롤 정복 키트(`GF_SideScrollConquest`)의 디렉터 — 정복 한 판을 진행합니다. 키트의 판(`ConquestWorld`)을 카탈로그로 열고 시작 건물을 지은 뒤, 지휘관 이동 · 부대 명령 · 훈련을 명령으로 받아 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_SideScrollConquest` 의 `ConquestViewComponent`)이
 *          이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다. 자동 플레이(`isAutoPlayOn`)면 훈련하고 돌격하며 지휘관이 전선을 따라 나아간다.
 *          판의 x 는 절대 자리의 X 다(높이 0, 깊이 0). 거점을 모두 얻으면 `win`(상금), 첫 마을을 잃으면 `lose` 로 미니게임을 끝낸다.
 *          시나리오: `game/playground/automation/sidescrollconquest.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestWorld.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    struct AbsolutePosition;

    /**
     * @class ConquestGameModeComponent
     * @brief 시작 건물 → 훈련 · 진격 · 점령(명령) → 승패 → 결과를 진행합니다.
     */
    REFLECT( Version = 2, Category = "SideScrollConquest", DisplayName = "Conquest Game Mode", Tooltip = "Runs one side scroll conquest: the commander, squad orders and training from commands, to victory or defeat" )
    class SW_KIT_API ConquestGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        ConquestGameModeComponent();
        virtual ~ConquestGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /** @brief 지휘관의 이동 축(-1..1)입니다. 다음 명령까지 유지된다. 명령을 받지 않는 때(`isAwaitingCommand`)면 false 입니다. */
        [[nodiscard]] bool submitCommanderMove( float32 axis );
        /** @brief 부대에 명령합니다(따르기 · 대기 · 돌격). 명령을 받지 않는 때면 false 입니다. */
        [[nodiscard]] bool submitOrder( ConquestOrder order );
        /** @brief 시작 건물에서 병사 하나를 훈련합니다. 명령을 받지 않는 때면 규칙에 묻지 않고 `InvalidBuilding` 입니다. */
        [[nodiscard]] ConquestResult submitTrain();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 승패가 나지 않았으며 사람이 지휘하면 true 입니다(자동 플레이면 false). */
        bool                 isAwaitingCommand() const;
        const ConquestWorld& getWorld() const { return _world; }
        /** @brief 이 판의 카탈로그입니다. 열기 전이면 nullptr 입니다. */
        const ConquestCatalog* findCatalog() const { return _pCatalog.get(); }
        /** @brief 판의 x 를 절대 자리로 옮깁니다. */
        static AbsolutePosition makePosition( float32 x );
        int32                   getVictory() const { return _world.isVictory() ? 1 : 0; }
        int32                   getDefeat() const { return _world.isDefeat() ? 1 : 0; }
        int32                   getTerritory() const { return isStarted() ? _world.computeTerritory() : 0; }
        int32                   getSquadSize() const { return _world.getSquadSize(); }
        float32                 getCommanderX() const { return _world.getCommander()._x; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( GameSeconds frameTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 씬 데이터의 시작 건물을 지을 수 있는지 봅니다(건물이 병사를 훈련하고 거점 · 병종이 카탈로그에 있다). 틀렸으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateLayout() const;
        /** @brief 자동 플레이의 한 걸음 — 훈련하고, 돌격을 명령하고, 지휘관을 오른쪽으로 보냅니다. */
        void stepAutoPlay();
        /** @brief 이벤트를 로그로 냅니다. */
        void reportEvents();

    private:
        PROPERTY( Category = "Start", DisplayName = "Start Building", Required, Tooltip = "Building placed at the start site; it trains the soldiers" )
        CatalogKey<ConquestBuildingDef> _startBuilding;
        PROPERTY( Category = "Start", DisplayName = "Start Site", Required, Tooltip = "Player site the start building stands on" )
        CatalogKey<ConquestSiteDef> _startSite;
        PROPERTY( Category = "Start", DisplayName = "Trained Unit", Required, Tooltip = "Unit the training command trains" )
        CatalogKey<ConquestUnitDef> _trainUnit;
        PROPERTY( Category = "Match", DisplayName = "Result Hold", Tooltip = "Time the result stays on screen before the minigame ends", Min = 0.1, Units = s )
        float32 _resultHoldSeconds;
        PROPERTY( Category = "Match", DisplayName = "Win Reward", Tooltip = "Money for taking every site", Min = 0 )
        int32 _winReward;

        SharedAssetRef<ConquestCatalog> _pCatalog;
        ConquestWorld                   _world;
        ConsumerLease                   _eventLease;
        vector<ConquestEvent>           _listEvent;
        Countdown                       _resultHold;
    };
} // namespace sw
