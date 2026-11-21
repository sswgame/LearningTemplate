/**
 * @file FrontierCityGameModeComponent.h
 * @brief 개척 마을의 도시 디렉터 — 월드 상태(`GameStateComponent`)와 플레이어 지갑(`WalletComponent`)에 시작 돈을 넣는 첫 디렉터. 도시 키트(`CitySimulation`)를 공유 땅 · 지갑 · 시계 위에 돌립니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameSpeedControl.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/SimulationViewSync.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class WalletComponent;
    class GameStateComponent;

    /**
     * @class FrontierCityGameModeComponent
     * @brief 도시 한 판입니다. 같은 오브젝트의 맨 앞 `GameStateComponent` 를 열고 플레이어 지갑(`WalletComponent`)에 새 판이면 두 통화의 시작 돈을 넣고, 첫 틱에 기념 건물(막힌 땅)과 도로(막히지 않은 땅)를 놓습니다.
     * @details 도시는 공유 땅을 빌린다(`CitySimulation::bindLand`, 원점 (0, 0)) — 같은 땅을 RTS 디렉터가 빌리므로 도시가 지은 칸에 RTS 는 짓지 못하고 유닛은 돌아간다.
     *          공유 시계의 날 넘김이 도시의 달 결산이다(`settleMonth`). `FastForward`(Space)를 누르는 동안, 또는 자동 플레이면 시계가 `_fastTimeScale` 배로 흐른다.
     *          시계를 미는 것은 이 디렉터 하나다(RTS 는 읽기만). 상태는 도시 하나(구간 'CITY')와 배치를 마쳤는지이고, 돈 · 시계 · 땅은 공유 상태에 있다.
     */
    REFLECT( Category = "Frontier", DisplayName = "Frontier City Game Mode", Tooltip = "Runs the city kit on the shared land, wallet and clock and spawns the building tiles" )
    class FrontierCityGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "FTCT" );
        static constexpr uint32 kStateVersion = 1;

        FrontierCityGameModeComponent();
        virtual ~FrontierCityGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        const CitySimulation& getCity() const { return _city; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bViewsDirty == SW_TRUE; }

    private:
        /** @brief 빈 도시를 열고 공유 땅을 빌립니다(시작과 복원이 같은 설정으로 연다). 카탈로그가 없으면 false 입니다. */
        [[nodiscard]] static bool initializeCity( CitySimulation& outCity, GameStateComponent& state, WalletComponent& wallet );
        /** @brief 첫 틱의 배치 — 기념 건물과 도로입니다. 놓지 못하면 이유를 남긴다(데이터 오류). */
        void layOutTown();
        /** @brief 지금 도시(건물 · 도로)를 동기화에 쌓습니다 — 바뀐 슬롯만 맞출 목록에 들어간다. */
        void             collectTileViews();
        GameObjectHandle spawnBuildingView( GameObjectManager& manager, size_t buildingIndex, const CityBuildingDef& def );
        GameObjectHandle spawnRoadView( GameObjectManager& manager, size_t tileIndex );

    private:
        PROPERTY( Category = "Prefabs", Tooltip = "One tile view (a cube)" )
        AssetRef<PrefabAsset> _tilePrefab;
        PROPERTY( Category = "Economy", DisplayName = "Starting Deben", Tooltip = "City money a fresh game starts with", Min = 0 )
        int32 _startingDeben;
        PROPERTY( Category = "Economy", DisplayName = "Starting Minerals", Tooltip = "RTS money a fresh game starts with (the same shared wallet)", Min = 0 )
        int32 _startingMinerals;
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
        PROPERTY( Category = "Time", DisplayName = "Fast Time Scale", Tooltip = "Clock speed while fast forward is held (up to the game speed limit)", Min = 1.0, Max = 8.0 )
        float32 _fastTimeScale;
        PROPERTY( Category = "Look", DisplayName = "Building Color" )
        float4 _buildingColor;
        PROPERTY( Category = "Look", DisplayName = "Road Color" )
        float4 _roadColor;

        CitySimulation                             _city;
        MaterialTintCache                          _tintCache;
        SimulationViewSync<const CityBuildingDef*> _buildingViews; ///< 건물 번호 → 지금 건물 정의(허물렸으면 nullptr)
        SimulationViewSync<uint8>                  _roadViews;     ///< 타일 번호(y × 너비 + x) → 도로(1)
        GameSpeedControl                           _speed;         ///< 배속(빨리 감기를 누르는 동안 `_fastTimeScale`) — 공유 시계와 시뮬레이션이 같은 값을 받는다
        PROPERTY( RuntimeState )
        uint8                  _bLaidOut    : 1;
        uint8                  _bViewsDirty : 1;
        [[maybe_unused]] uint8 _reserved    : 6;
    };
} // namespace sw
