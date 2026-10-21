/**
 * @file FarmVillageDirectorComponent.h
 * @brief HarvestValley 마을 사람의 하루 일정을 돌리는 디렉터입니다. 일정 데이터(`data/villagers.schedules.xml`)를 GameFramework `ScheduleSystem` 으로 돌리고 마을 사람마다 몸(프리팹)을 스폰합니다.
 * @details 농장 디렉터와 같은 오브젝트의 **뒤**에 붙입니다. 판 상태(`SessionStateComponent`)의 시계를 잠금 없이 읽으려면 같은 오브젝트여야 하고, 농장 디렉터가 판 상태를 연 뒤에 시작해야 한다.
 *          상태 바이트에는 머리 다음에 일정 시스템의 저장 상태(`ScheduleSaveState` — 시각 · 끼어들기 · 끼어든 자리 · 호감도 · 약속 결과 · 예약)를 리플렉션 바이너리로 싣는다.
 *          계획 자체는 싣지 않는다 — 그 상태로 다시 세우면 같은 계획이 나온다. 복원은 일정이 서 있으면 바로, 아직이면 `startGame` 이 일정을 세운 직후에 건다.
 *          옛 판(머리만 있던 1 판)의 바이트는 읽지 않는다(버전이 다르면 읽기 실패).
 *          길: 마을 내비 격자(`_navOrigin` · `_navWidth` · `_navHeight` · `_navCellSize`, 장애물은 이름 목록 `_navObstacleNames` 의 씬 오브젝트 자리)를 세우고,
 *          일정의 화면 안 경로(고운 경로)를 그 격자의 A* 로 한다(`NavGridSchedulePathing`). 계획 경로는 곧은 선이라 도착 시각은 격자와 무관하다.
 *          몸은 `FarmVillagerComponent` 가 `NavAgent` 로 이 디렉터가 적은 일정상 위치를 쫓는다 — 언리얼의 "움직이는 목표로 이동" 과 같다. 시계를 감아
 *          일정상 위치가 몸에서 `_bodySnapDistance` 넘게 떨어지면 몸을 그 자리에 놓는다(잠 · 빨리 감기 · 자동화 `AdvanceGameTime`).
 *          자동화는 `Schedule.*` 탐침(NPC id)과 `Object.*` 탐침(`Villager.<id>`)으로 본다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/SchedulePathing.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Actor/Navigation/GridPathfinder.h"
#include "GameFramework/Base/Actor/Navigation/NavAgent.h"
#include "GameFramework/Base/Actor/Navigation/NavGrid.h"
#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"

namespace sw
{
    class Archive;
    class GameObjectManager;

    /**
     * @class FarmVillageDirectorComponent
     * @brief 마을 사람 일정의 디렉터입니다. 틱(PrePhysics)에서 일정을 공유 시계까지 흘리고 마을 사람마다 일정상 위치와 몸의 걸음 설정을 적어 둡니다. 뷰는 그것만 읽습니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Village Director", Tooltip = "Runs the villagers' daily schedules on the shared game clock and spawns their bodies" )
    class FarmVillageDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        FarmVillageDirectorComponent();
        virtual ~FarmVillageDirectorComponent() override;

        void writeState( Archive& outArchive ) const override;

        /** @brief 마을 사람 @p npcIndex 의 일정상 위치(지면)입니다. 뷰가 PostUpdate 에서 읽습니다. 범위 밖이면 원점입니다. */
        float3 getVillagerPosition( int32 npcIndex ) const;
        int32  getVillagerCount() const { return static_cast<int32>( _listPosition.size() ); }
        /** @brief 마을 사람 @p npcIndex 의 몸이 이번 틱에 쓸 걸음 설정입니다. 최고 속도는 일정의 걷는 속도를 지금 시계 빠르기로 바꾼 값 × `_bodyCatchUpScale` 입니다. */
        NavAgentSettings makeBodySettings( int32 npcIndex ) const;
        /** @brief @p npcIndex 를 뺀 마을 사람들의 일정상 위치입니다(몸의 떨어지기에 쓴다 — 다른 몸은 같은 틱에 병렬로 움직이므로 읽지 않는다). */
        void           collectNeighborPositions( int32 npcIndex, vector<float3>& outListPosition ) const;
        const NavGrid& getNavGrid() const { return _navGrid; }
        float32        getBodySnapDistance() const { return _bodySnapDistance; }

    protected:
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 내비 격자를 세우고 장애물 이름 목록의 씬 오브젝트 자리를 막습니다. 격자 크기가 없으면 false 입니다. */
        [[nodiscard]] bool rebuildNavGrid();

        PROPERTY( Category = "Data", DisplayName = "Schedule Data", AssetPath, Tooltip = "Schedule catalog XML (places, routines, NPCs)" )
        string _scheduleDataPath;
        PROPERTY( Category = "Prefabs", DisplayName = "Villager Prefab", AssetPath, AssetType = "Prefab", Tooltip = "Body spawned for each schedule NPC (needs a FarmVillagerComponent)" )
        string _villagerPrefab;
        PROPERTY( Category = "Navigation", DisplayName = "Nav Origin", Tooltip = "World corner of cell (0, 0) of the village nav grid", Units = m )
        float3 _navOrigin;
        PROPERTY( Category = "Navigation", DisplayName = "Nav Width", Tooltip = "Cells along X (required)", Min = 0 )
        int32 _navWidth;
        PROPERTY( Category = "Navigation", DisplayName = "Nav Height", Tooltip = "Cells along Z (required)", Min = 0 )
        int32 _navHeight;
        PROPERTY( Category = "Navigation", DisplayName = "Nav Cell Size", Tooltip = "Edge of one nav cell (required)", Min = 0.0, Units = m )
        float32 _navCellSize;
        PROPERTY( Category = "Navigation", DisplayName = "Obstacle Names", Tooltip = "Comma-separated object names that block the grid; Tree also blocks Tree_2, Tree_3 ..." )
        string _navObstacleNames;
        PROPERTY( Category = "Navigation", DisplayName = "Obstacle Radius", Tooltip = "Cells within this distance of an obstacle object are blocked", Min = 0.0, Units = m )
        float32 _navObstacleRadius;
        PROPERTY( Category = "Navigation", DisplayName = "Body Catch Up Scale", Tooltip = "Body top speed as a multiple of the schedule walking speed at the current clock rate", Min = 1.0 )
        float32 _bodyCatchUpScale;
        PROPERTY( Category = "Navigation", DisplayName = "Body Snap Distance", Tooltip = "A body this far from its scheduled spot is placed there (clock skips)", Min = 0.0, Units = m )
        float32 _bodySnapDistance;
        PROPERTY( Category = "Navigation", DisplayName = "Body Acceleration Per Speed",
                  Tooltip = "Acceleration limit as a multiple of the body top speed (1/s); 10 reaches top speed in 0.1 s", Min = 0.0 )
        float32 _bodyAccelerationPerSpeed;
        PROPERTY( Category = "Navigation", DisplayName = "Body Slow Distance", Tooltip = "The body starts slowing this close to its scheduled spot (lag is about this / catch up scale)",
                  Min = 0.0, Units = m )
        float32 _bodySlowDistance;

        ScheduleCatalog        _catalog;
        NavGrid                _navGrid;
        GridPathfinder         _pathfinder;
        NavGridSchedulePathing _finePathing; ///< 화면 안 경로(`_navGrid` · `_pathfinder` 를 빌린다 — 위 두 멤버보다 뒤에 선언한다)
        ScheduleSystem         _schedule;
        ScheduleSaveState      _pendingSave;     ///< 일정이 서기 전에 읽은 저장 상태(`startGame` 이 건다)
        bool                   _bHasPendingSave; ///< `_pendingSave` 를 아직 걸지 않았다
        vector<float3>         _listPosition;    ///< 마을 사람마다 이번 틱의 일정상 위치(뷰가 읽는다)
        vector<float32>        _listBodySpeed;   ///< 마을 사람마다 이번 틱 몸의 최고 속도(m/s)
    };
} // namespace sw
