/**
 * @file FrontierSkirmishGameModeComponent.h
 * @brief 개척 마을의 RTS 디렉터 — 공유 지갑을 플레이어 0 에 빌려 주고, 공유 땅 위에서 일꾼이 병영을 짓고 해병이 도시 건물을 돌아 순찰합니다.
 * @details 1 대 1 판과 같은 키트 디렉터(`RTSGameModeComponent`)를 잇는다 — 유닛 카탈로그 · 시작 광물은 바탕의 씬 데이터, 규칙은 `RTSWorld` 하나, 모습은 같은 키트 클라이언트 뷰
 *          (`RTSBattlefieldViewComponent`)다. 이 파일은 두 키트를 섞는 게임 몫(공유 땅 · 지갑, 시연 명령)만 든다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/RTSGameModeComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

namespace sw
{
    class WalletComponent;
    class GameStateComponent;

    /**
     * @class FrontierSkirmishGameModeComponent
     * @brief RTS 한 판입니다. 앞에 붙은 도시 디렉터(`CityGameModeComponent`)가 연 공유 상태를 빌리고, 새 판이면 시작 광물 · 일꾼 · 해병을 놓습니다.
     * @details RTS 는 공유 땅을 빌린다(`RTSWorld::bindLand`, 원점 (0, 0)) — 도시가 지은 칸은 막혀 짓지 못하고 경로가 돌아간다. 돈은 공유 지갑(플레이어 0)이다.
     *          둘째 틱에 일꾼에게 `_barracksX`, `_barracksY` 에 병영 짓기를, 해병에게 두 지점(`_patrol*`) 사이 이동을 준다. 해병은 도착할 때마다 반대쪽으로 다시 간다.
     *          이 디렉터는 입력과 시계를 만지지 않는다(도시 디렉터가 민다). 모습은 키트 클라이언트 뷰가 세운다.
     */
    REFLECT( Category = "Frontier", DisplayName = "Frontier Skirmish Game Mode", Tooltip = "Runs the RTS kit on the shared land and wallet" )
    class FrontierSkirmishGameModeComponent : public RTSGameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr FormatDef kStateFormat{ "FTSK" };

        FrontierSkirmishGameModeComponent();
        virtual ~FrontierSkirmishGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        const RTSWorld& getWorld() const override { return _world; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;

    private:
        /** @brief 빈 월드를 열고 공유 땅과 지갑을 겁니다(시작과 복원이 같은 설정으로 연다). 격자 크기는 공유 땅의 크기이고, 월드가 열리지 않으면 false 입니다. */
        [[nodiscard]] bool initializeWorld( RTSWorld& outWorld, GameStateComponent& state, WalletComponent& wallet ) const;
        /** @brief 둘째 틱의 명령 — 병영 짓기와 첫 순찰 이동입니다. 거절되면 이유를 남긴다(데이터 오류). */
        void issueOpeningOrders();
        /** @brief 해병이 멈췄으면 순찰의 다른 끝으로 보냅니다. */
        void continuePatrol();

    private:
        PROPERTY( Category = "Units", DisplayName = "Worker", Tooltip = "RTS catalog id of the starting worker" )
        CatalogKey<RTSUnitDef> _workerUnit;
        PROPERTY( Category = "Units", DisplayName = "Soldier", Tooltip = "RTS catalog id of the patrolling soldier" )
        CatalogKey<RTSUnitDef> _soldierUnit;
        PROPERTY( Category = "Units", DisplayName = "Barracks", Tooltip = "RTS catalog id of the building the worker builds" )
        CatalogKey<RTSUnitDef> _barracksUnit;
        PROPERTY( Category = "Layout", DisplayName = "Barracks X", Min = 0 )
        int32 _barracksX;
        PROPERTY( Category = "Layout", DisplayName = "Barracks Y", Min = 0 )
        int32 _barracksY;
        PROPERTY( Category = "Layout", DisplayName = "Player Start", Tooltip = "Start position of the player on the shared land; required (the skirmish does not open off the land)", Units = m )
        float3 _playerStart;
        PROPERTY( Category = "Layout", DisplayName = "Worker Start", Units = m )
        float3 _workerStart;
        PROPERTY( Category = "Layout", DisplayName = "Patrol A", Tooltip = "First patrol end (also the soldier's start)", Units = m )
        float3 _patrolA;
        PROPERTY( Category = "Layout", DisplayName = "Patrol B", Units = m )
        float3 _patrolB;

        SW_CODEC_STATE RTSWorld  _world;
        SW_CODEC_STATE RTSUnitID _worker;
        SW_CODEC_STATE RTSUnitID _soldier;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _tickCount;
        PROPERTY( RuntimeState )
        uint8                  _bPatrolToB : 1; ///< 해병이 지금 B 쪽으로 간다
        [[maybe_unused]] uint8 _reserved   : 7;
    };
} // namespace sw
