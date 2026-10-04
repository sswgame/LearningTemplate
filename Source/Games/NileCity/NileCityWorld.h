/**
 * @file NileCityWorld.h
 * @brief NileCity 의 한 판 — 도시 시뮬레이션(키트 `CitySimulation`)을 들고 땅 · 도로 · 건물 · 일꾼을 내장 도형으로 그리며, 마우스로 짓고 허뭅니다.
 *
 * @details 규칙(노동 · 순회 일꾼 · 물자 사슬 · 집 진화 · 범람)은 키트가, 땅 모양과 자동 계획은 `NileCityPlanner` 가 맡습니다. 여기는
 *          "어디를 눌렀는가" 와 "어떻게 보이는가" 만 압니다. 화면은 북쪽(+Z)을 비스듬히 내려다보는 직교 카메라이고 칸 하나가 1 m 입니다.
 *          `-gv_nileAutoPlay=1` 이면 계획표대로 도로 고리 · 우물 · 농장 · 창고 · 시장 · 집을 지으며 달마다 `[Nile] month N pop P money M` 을 남깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Kits/Strategy/CityBuilder/CitySimulation.h"
#include "GameFramework/Stage/PrimitiveStage.h"

#include "Games/NileCity/NileCityPlanner.h"

namespace sw
{
    class InputManager;

    /**
     * @class NileCityWorld
     * @brief 도시 한 판입니다. 시뮬레이션은 무대를 걷어도(핫 리로드 · 씬 바뀜) 남고, 무대는 다음 갱신에서 그 상태대로 다시 섭니다.
     */
    class NileCityWorld
    {
    public:
        static constexpr int32 kStartingMoney = 1500;

        NileCityWorld();
        ~NileCityWorld();

        NileCityWorld( const NileCityWorld& )            = delete;
        NileCityWorld& operator=( const NileCityWorld& ) = delete;

        /** @brief 카탈로그를 빌리고 땅을 칠한 새 도시를 엽니다. */
        void initialize( const CityCatalog* pCatalog );
        /** @brief 무대를 세웁니다. 이미 섰으면 true, 씬 서비스가 아직 없으면 false 입니다. */
        [[nodiscard]] bool spawn();
        void               despawn();
        /** @brief 한 프레임 — 입력(또는 자동 계획) · 시뮬레이션 · 알림 · 모습 · 카메라. */
        void update( float32 deltaTime );
        bool isInitialized() const { return _pCatalog != nullptr; }

    private:
        /** @brief 시뮬레이션 건물 한 칸의 모습입니다(같은 번호). */
        struct BuildingView
        {
            GameObjectHandle       _object{};
            const CityBuildingDef* _pDef{ nullptr };
            int32                  _level{ -1 };
            uint8                  _bInhabited{ SW_FALSE };
        };

        void spawnTerrain();
        void syncRoads();
        void syncBuildings();
        void syncWalkers();
        void updateInput( float32 deltaTime, const InputManager& input );
        void updateCursor( const InputManager& input );
        void updateCamera();
        void drainEvents();
        void placeSelected();
        void logStatus() const;
        /** @brief 마우스가 가리키는 땅 칸입니다. 맵 밖이면 false 입니다. */
        [[nodiscard]] bool findCursorTile( const InputManager& input, int2& outTile ) const;
        const utf8*        getToolName() const;

        PrimitiveStage                 _stage;
        CitySimulation                 _city;
        NileCityPlanner                _planner;
        vector<CityEvent>              _listEvent;
        vector<GameObjectHandle>       _listRoadObject; ///< 칸마다(도로가 아니면 무효)
        vector<BuildingView>           _listBuildingView;
        vector<GameObjectHandle>       _listWalkerObject;  ///< 일꾼 수만큼 쓰고 남으면 숨긴다
        vector<int32>                  _listWalkerLookKey; ///< 오브젝트마다 지금 입은 색(일꾼 종류 · 서비스)
        vector<const CityBuildingDef*> _listTool;          ///< 0 은 도로(nullptr)
        const CityCatalog*             _pCatalog;
        GameObjectHandle               _cursorObject;
        float3                         _cameraFocus;
        int2                           _cursorTile;
        float32                        _cameraHeight; ///< 직교 화면 높이(m)
        float32                        _timeScale;
        int32                          _selectedTool;
        int32                          _monthCount;
        int32                          _evolvedCount; ///< 이번 달 오른 집 수(달 끝 로그)
        uint8                          _bCursorValid;
        uint8                          _bPaused;
        uint8                          _bAutoPlan; ///< P 로 켠 자동 계획(전역 변수와 별개)
        uint8                          _bSpawned;
    };
} // namespace sw
