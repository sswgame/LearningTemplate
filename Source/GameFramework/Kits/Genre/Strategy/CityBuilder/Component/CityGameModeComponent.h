/**
 * @file CityGameModeComponent.h
 * @brief 도시 건설 키트(`GF_CityBuilder`)의 디렉터 — 월드 상태(`GameStateComponent`)를 열고 플레이어 지갑(조종자의 `WalletComponent`)에 시작 돈을 넣은 뒤, 도시(`CitySimulation`)를 공유 땅 · 지갑 · 시계 위에 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 빨리 감기 입력과 건물 · 도로 모습은 클라이언트 모듈
 *          (`GF_Client_CityBuilder` 의 `CityFastForwardInputComponent` · `CityViewComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          땅을 나누는 다른 키트의 디렉터(RTS 등)는 같은 오브젝트에서 이 디렉터 뒤에 붙는다 — 판을 여는 설정은 이 디렉터의 씬 데이터 하나다.
 */
#pragma once
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"

namespace sw
{
    class WalletComponent;
    class GameStateComponent;

    /**
     * @class CityGameModeComponent
     * @brief 도시 한 판입니다. 같은 오브젝트의 맨 앞 `GameStateComponent` 를 열고 플레이어 지갑(조종자의 `WalletComponent`)에 새 판이면 시작 돈을 넣고, 첫 틱에 기념 건물(막힌 땅)과 도로(막히지 않은 땅)를 놓습니다.
     * @details 도시는 공유 땅을 빌린다(`CitySimulation::bindLand`, 원점 (0, 0)) — 같은 땅을 다른 디렉터가 빌리면 도시가 지은 칸에 그쪽은 짓지 못하고 유닛은 돌아간다.
     *          공유 시계의 날 넘김이 도시의 달 결산이다(`settleMonth`). 빨리 감기 명령(`submitFastForward`)을 받은 동안, 또는 자동 플레이면 시계와 도시가 `_fastTimeScale` 배로 흐른다.
     *          시계를 미는 것은 이 디렉터 하나다. 상태는 도시 하나(구간 'CITY')와 배치를 마쳤는지이고, 돈 · 시계 · 땅은 공유 상태에 있다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "City Game Mode", Tooltip = "Opens the shared land, wallet and clock, lays out the town and runs the city kit on them" )
    class SW_KIT_API CityGameModeComponent : public CityBuilderGameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "FTCT" );
        static constexpr uint32 kStateVersion = 1;

        CityGameModeComponent();
        virtual ~CityGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(판이 열린 동안만 받는다, 게임 스레드) ----
        /** @brief 빨리 감기를 누르고 있는지 넘깁니다 — 다음 걸음이 쓴다(놓을 때까지 다시 넘기지 않아도 유지된다). 판이 열리기 전이면 false 입니다. */
        [[nodiscard]] bool submitFastForward( bool bHeld );
        /** @brief 시계는 빨리 감기(`submitFastForward`)로만 민다 — 일시정지 · 배속 입력은 거절합니다. */
        [[nodiscard]] bool submitTimeInput( bool bPause, bool bFaster, bool bSlower ) override;

        /** @brief 도시 금고는 플레이어 지갑입니다. 없으면 nullptr 입니다. */
        const Wallet* findCityWallet() const override;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 빈 도시를 열고 공유 땅을 빌립니다(시작과 복원이 같은 설정으로 연다). 설정이 범위 밖이면 도시가 오류를 남기고 false 입니다. */
        [[nodiscard]] bool initializeCity( CitySimulation& outCity, GameStateComponent& state, WalletComponent& wallet ) const;
        /** @brief 첫 틱의 배치 — 기념 건물과 도로입니다. 놓지 못하면 이유를 남긴다(데이터 오류). */
        void layOutTown();

    private:
        PROPERTY( Category = "World", DisplayName = "Land Size", Tooltip = "Cells on one side of the square shared land (1 cell = 1 m); required", Min = 1 )
        int32 _landSize;
        PROPERTY( Category = "World", DisplayName = "Seconds Per Day", Tooltip = "Real seconds in one day of the shared clock (one city month) at normal speed; required", Min = 1, Units = s )
        float32 _secondsPerDay;
        PROPERTY( Category = "World", DisplayName = "Start Hour", Tooltip = "Hour of the shared clock when a fresh game opens", Min = 0, Max = 23.99 )
        float32 _startHour;
        PROPERTY( Required, Category = "Layout", DisplayName = "Landmark Building", Tooltip = "City catalog id of the building laid out on the first tick" )
        CatalogKey<CityBuildingDef> _landmarkBuilding;
        PROPERTY( Category = "Layout", DisplayName = "Landmark X", Min = 0 )
        int32 _landmarkX;
        PROPERTY( Category = "Layout", DisplayName = "Landmark Y", Min = 0 )
        int32 _landmarkY;
        PROPERTY( Category = "Layout", DisplayName = "Road Start X", Min = 0 )
        int32 _roadFromX;
        PROPERTY( Category = "Layout", DisplayName = "Road Start Y", Min = 0 )
        int32 _roadFromY;
        PROPERTY( Category = "Layout", DisplayName = "Road End X", Min = 0 )
        int32 _roadToX;
        PROPERTY( Category = "Layout", DisplayName = "Road End Y", Min = 0 )
        int32 _roadToY;
        PROPERTY( Category = "Time", DisplayName = "Fast Time Scale", Tooltip = "Clock speed while fast forward is held (up to the game speed limit)", Min = 1.0 )
        float32 _fastTimeScale;

        PROPERTY( RuntimeState )
        uint8                  _bLaidOut     : 1;
        uint8                  _bFastForward : 1; ///< 지금 빨리 감기 명령(`submitFastForward`)
        [[maybe_unused]] uint8 _reserved     : 6;
    };
} // namespace sw
