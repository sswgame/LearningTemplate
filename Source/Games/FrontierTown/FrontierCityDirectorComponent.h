/**
 * @file FrontierCityDirectorComponent.h
 * @brief 개척 마을의 도시 디렉터 — 공유 상태(`SessionStateComponent`)를 여는 첫 디렉터. 도시 키트(`CitySimulation`)를 공유 땅 · 지갑 · 시계 위에 돌립니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/MaterialTintCache.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"

namespace sw
{
    class SessionStateComponent;

    /**
     * @class FrontierCityDirectorComponent
     * @brief 도시 한 판입니다. 같은 오브젝트의 맨 앞 `SessionStateComponent` 를 열고(새 판이면 두 통화의 시작 돈), 첫 틱에 기념 건물(막힌 땅)과 도로(막히지 않은 땅)를 놓습니다.
     * @details 도시는 공유 땅을 빌린다(`CitySimulation::bindLand`, 원점 (0, 0)) — 같은 땅을 RTS 디렉터가 빌리므로 도시가 지은 칸에 RTS 는 짓지 못하고 유닛은 돌아간다.
     *          공유 시계의 날 넘김이 도시의 달 결산이다(`settleMonth`). `Frontier.FastForward`(Space)를 누르는 동안, 또는 자동 플레이면 시계가 `_fastTimeScale` 배로 흐른다.
     *          시계를 미는 것은 이 디렉터 하나다(RTS 는 읽기만). 상태는 도시 하나(구간 'CITY')와 배치를 마쳤는지이고, 돈 · 시계 · 땅은 공유 상태에 있다.
     */
    REFLECT( Category = "Frontier", DisplayName = "Frontier City Director", Tooltip = "Runs the city kit on the shared land, wallet and clock and spawns the building tiles" )
    class FrontierCityDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCcUtil::make( "FTCT" );
        static constexpr uint32 kStateVersion = 1;

        FrontierCityDirectorComponent();
        virtual ~FrontierCityDirectorComponent() override;

        void writeState( Archive& outArchive ) const override;

        const CitySimulation& getCity() const { return _city; }

    protected:
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bViewsDirty == SW_TRUE; }

    private:
        /** @brief 빈 도시를 열고 공유 땅을 빌립니다(시작과 복원이 같은 설정으로 연다). 카탈로그가 없으면 false 입니다. */
        [[nodiscard]] static bool initializeCity( CitySimulation& outCity, SessionStateComponent& state );
        /** @brief 첫 틱의 배치 — 기념 건물과 도로입니다. 놓지 못하면 이유를 남긴다(데이터 오류). */
        void layOutTown();
        void spawnTiles( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "One tile view (a cube)" )
        string _tilePrefab;
        PROPERTY( Category = "Economy", DisplayName = "Starting Deben", Tooltip = "City money a fresh game starts with", Min = 0 )
        int32 _startingDeben;
        PROPERTY( Category = "Economy", DisplayName = "Starting Minerals", Tooltip = "RTS money a fresh game starts with (the same shared wallet)", Min = 0 )
        int32 _startingMinerals;
        PROPERTY( Category = "Layout", DisplayName = "Landmark Building", Tooltip = "City catalog id of the building laid out on the first tick" )
        string _landmarkBuilding;
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
        PROPERTY( Category = "Time", DisplayName = "Fast Time Scale", Tooltip = "Clock speed while fast forward is held", Min = 1.0 )
        float32 _fastTimeScale;
        PROPERTY( Category = "Look", DisplayName = "Building Color" )
        float4 _buildingColor;
        PROPERTY( Category = "Look", DisplayName = "Road Color" )
        float4 _roadColor;

        CitySimulation           _city;
        MaterialTintCache        _tintCache;
        vector<GameObjectHandle> _listTileView;
        uint8                    _bLaidOut    : 1;
        uint8                    _bViewsDirty : 1;
        uint8                    _reserved    : 6;
    };
} // namespace sw
