/**
 * @file FrontierSkirmishDirectorComponent.h
 * @brief 개척 마을의 RTS 디렉터 — 공유 지갑을 플레이어 0 에 빌려 주고, 공유 땅 위에서 일꾼이 병영을 짓고 해병이 도시 건물을 돌아 순찰합니다.
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
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

namespace sw
{
    class SessionStateComponent;

    /**
     * @class FrontierSkirmishDirectorComponent
     * @brief RTS 한 판입니다. 도시 디렉터가 연 공유 상태를 읽고(이미 열렸으면 그대로), 새 판이면 일꾼과 해병을 놓습니다.
     * @details RTS 는 공유 땅을 빌린다(`RTSWorld::bindLand`, 원점 (0, 0)) — 도시가 지은 칸은 막혀 짓지 못하고 경로가 돌아간다. 돈은 공유 지갑(플레이어 0)이다.
     *          둘째 틱에 일꾼에게 `_barracksX`, `_barracksY` 에 병영 짓기를, 해병에게 두 지점(`_patrol*`) 사이 이동을 준다. 해병은 도착할 때마다 반대쪽으로 다시 간다.
     *          이 디렉터는 입력과 시계를 만지지 않는다(도시 디렉터가 민다). 모습은 유닛마다 판 하나이고, 유닛이 있으면 틱마다 위치를 옮긴다.
     */
    REFLECT( Category = "Frontier", DisplayName = "Frontier Skirmish Director", Tooltip = "Runs the RTS kit on the shared land and wallet and spawns the unit tiles" )
    class FrontierSkirmishDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCcUtil::make( "FTSK" );
        static constexpr uint32 kStateVersion = 1;

        FrontierSkirmishDirectorComponent();
        virtual ~FrontierSkirmishDirectorComponent() override;

        void writeState( Archive& outArchive ) const override;

        const RTSWorld& getWorld() const { return _world; }

    protected:
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bViewsDirty == SW_TRUE; }

    private:
        /** @brief 빈 월드를 열고 공유 땅과 지갑을 겁니다(시작과 복원이 같은 설정으로 연다). 카탈로그가 없으면 false 입니다. */
        [[nodiscard]] static bool initializeWorld( RTSWorld& outWorld, SessionStateComponent& state );
        /** @brief 둘째 틱의 명령 — 병영 짓기와 첫 순찰 이동입니다. 거절되면 이유를 남긴다(데이터 오류). */
        void issueOpeningOrders();
        /** @brief 해병이 멈췄으면 순찰의 다른 끝으로 보냅니다. */
        void continuePatrol();
        void syncUnitViews( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "One unit view (a cube)" )
        string _unitPrefab;
        PROPERTY( Category = "Units", DisplayName = "Worker", Tooltip = "RTS catalog id of the starting worker" )
        string _workerUnit;
        PROPERTY( Category = "Units", DisplayName = "Soldier", Tooltip = "RTS catalog id of the patrolling soldier" )
        string _soldierUnit;
        PROPERTY( Category = "Units", DisplayName = "Barracks", Tooltip = "RTS catalog id of the building the worker builds" )
        string _barracksUnit;
        PROPERTY( Category = "Layout", DisplayName = "Barracks X", Min = 0 )
        int32 _barracksX;
        PROPERTY( Category = "Layout", DisplayName = "Barracks Y", Min = 0 )
        int32 _barracksY;
        PROPERTY( Category = "Layout", DisplayName = "Worker Start", Units = m )
        float3 _workerStart;
        PROPERTY( Category = "Layout", DisplayName = "Patrol A", Tooltip = "First patrol end (also the soldier's start)", Units = m )
        float3 _patrolA;
        PROPERTY( Category = "Layout", DisplayName = "Patrol B", Units = m )
        float3 _patrolB;
        PROPERTY( Category = "Look", DisplayName = "Unit Color" )
        float4 _unitColor;
        PROPERTY( Category = "Look", DisplayName = "Building Color" )
        float4 _buildingColor;

        RTSWorld                 _world;
        MaterialTintCache        _tintCache;
        vector<GameObjectHandle> _listUnitView;
        RTSUnitID                _worker;
        RTSUnitID                _soldier;
        int32                    _tickCount;
        uint8                    _bPatrolToB  : 1; ///< 해병이 지금 B 쪽으로 간다
        uint8                    _bViewsDirty : 1;
        uint8                    _reserved    : 6;
    };
} // namespace sw
