/**
 * @file NileGameModeComponent.h
 * @brief NileCity 의 규칙을 돌리는 컴포넌트입니다 — 나일 강 땅(`NileCityPlanner`), 자동 계획, 열두 달 나일 달력(해가 바뀌면 범람), 도시 금고와 월별 로그를 맡습니다.
 * @details 도시 규칙(노동, 순회 일꾼, 물자 사슬, 집 진화, 범람)은 키트의 `CitySimulation` 이, 짓기 · 시간 명령과 알림은 키트 디렉터 바탕(`CityBuilderGameModeComponent`)이 맡는다.
 *          입력 · 커서 · 모습은 키트 클라이언트(`CityBuildInputComponent` · `CityViewComponent`)가 같은 오브젝트에서 이 디렉터를 읽고 명령해 처리한다 — 이 게임 코드는 그것을 모른다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"
#include "Engine/Serialization/Serializer/DataPath.h"
#include "Engine/TileMap/TileMapXML.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

#include "Games/NileCity/NileCityPlanner.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class NileGameModeComponent
     * @brief 나일 도시 한 판입니다. 플레이가 시작되면 도시 데이터를 읽고 땅을 칠한 새 도시를 엽니다.
     * @details 도시는 PROPERTY 가 아닌 런타임 상태이므로 세이브 · 핫 리로드가 본문(`serializeGameState`)으로 넘긴다. 금고 · 달력은 키트 하나만 쓰는 게임이라 디렉터가 보관한다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "Nile Game Mode", Tooltip = "Runs the Nile city: the river land, the auto plan, the Nile calendar and the city treasury" )
    class NileGameModeComponent : public CityBuilderGameModeComponent
    {
    public:
        REFLECT_BODY();

        NileGameModeComponent();
        virtual ~NileGameModeComponent() override;

        /** @brief 도시 상태(시뮬레이션, 자동 계획 진행, 금고, 달력, 배속)를 씁니다. `ComponentStateCodec::captureObject` 가 부릅니다. */
        void serializeGameState( StateIO& io ) override;
        /** @brief 자동 계획을 켜고 끕니다(설정 PROPERTY 와 전역 변수와는 별개인 실행 상태다). 판이 열리기 전이면 false 입니다. */
        [[nodiscard]] bool submitAutoPlanToggle() override;

        const Wallet* findCityWallet() const override { return &_wallet; }
        /** @brief 해 · 달 · 자동 계획 진행을 더한 도시 현황입니다. */
        void  logStatus() const override;
        int32 getMonthCount() const { return _monthCount; } ///< 감시 값 `Nile.Month`

    protected:
        /** @brief 도시 데이터를 읽고 땅을 칠한 새 도시를 엽니다. */
        [[nodiscard]] bool startGame() override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;

    private:
        /** @brief 나일 달력입니다. 아케트, 페레트, 셰무가 각각 네 달씩(열두 달)이며, 계절 하루가 한 달입니다. */
        WorldClockSettings makeClockSettings() const;
        /** @brief 이번 틱의 달력 알림에서 달이 넘어갈 때마다 도시를 결산합니다(같은 시점에 해가 바뀌었으면 범람이 일어납니다). */
        void settleMonths();
        /** @brief 이번 걸음의 도시 알림에서 달 · 진화 · 범람을 세고 달마다 로그를 남깁니다. */
        void logCityEvents();
        bool isAutoPlanOn() const;
        /** @brief 자동 계획 PROPERTY 를 플래너에 넘길 설정으로 모읍니다. */
        NileCityLayout makeCityLayout() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Auto Plan", Tooltip = "Build order the auto plan follows (roads, buildings and population thresholds); required" )
        DataPath _autoPlanPath;
        PROPERTY( Category = "City", DisplayName = "Seconds Per Month", Tooltip = "Real seconds in one city month before the time scale", Min = 1.0, Units = s )
        float32 _secondsPerMonth;

        PROPERTY( Category = "Map", DisplayName = "Tile Map", Tooltip = "Tile map of the land: the city size and the data layer 'terrain' whose values are numbers of Terrains; required" )
        AssetRef<TileMapXMLData> _tileMapPath;
        PROPERTY( Category = "Map", DisplayName = "Terrains", Tooltip = "Terrains (city catalog) the map's data layer 'terrain' numbers from 0; every value of the layer needs one" )
        vector<CatalogKey<CityTerrainDef>> _listTerrain;
        PROPERTY( Category = "Auto Plan", DisplayName = "Road X", Tooltip = "Tile x of the north-south road between the floodplain farms and the city", Min = 0 )
        int32 _roadX;
        PROPERTY( Category = "Auto Plan", DisplayName = "Farm X", Tooltip = "Left tile x of the 3x3 farms; must be floodplain on every row", Min = 0 )
        int32 _farmX;
        PROPERTY( Category = "Auto Plan", DisplayName = "Second Block Population", Tooltip = "Population at which the auto plan starts the second block", Min = 0 )
        int32 _secondBlockPeople;
        PROPERTY( Category = "Auto Plan", DisplayName = "Third Block Population", Tooltip = "Population at which the auto plan starts the third block", Min = 0 )
        int32 _thirdBlockPeople;

        Wallet                  _wallet; ///< 도시 금고. 키트 하나만 쓰는 게임이라 디렉터가 보관하고 빌려 준다.
        WorldClock              _clock;  ///< 달력. 하루가 한 달이다(계절 열둘 x 하루). 디렉터가 보관하고 시간을 흘린다.
        NileCityPlanner         _planner;
        vector<WorldClockEvent> _listClockEvent; ///< 이번 틱의 달력 알림(스크래치).
        PROPERTY( RuntimeState )
        int32 _monthCount;
        PROPERTY( RuntimeState )
        int32 _evolvedCount; ///< 이번 달 올라간 집 수(월말 로그).
        PROPERTY( RuntimeState )
        uint8                  _bAutoPlanToggle : 1; ///< P 로 켠 자동 계획(설정 PROPERTY 와 전역 변수와는 별개인 실행 상태다).
        [[maybe_unused]] uint8 _reserved        : 7;
    };
} // namespace sw
