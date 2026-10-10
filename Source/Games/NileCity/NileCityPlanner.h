/**
 * @file NileCityPlanner.h
 * @brief NileCity 의 땅과 자동 도시 계획 — 절차 지형(나일 강 · 범람원 · 사막 · 바위)과 `-gv_nileAutoPlay=1` 이 따라 짓는 순서표입니다.
 *
 * @details 화면 · 입력을 모르는 순수 규칙이라 엔진 밖 하네스에서 그대로 돌려 볼 수 있습니다(키트 `CitySimulation` 만 씁니다).
 *          순서표는 도로 고리 → 우물 → 범람원 농장 → 곡물 창고 → 바자 → 집 → 순회 서비스 → 다음 구역 순서이고, 단계마다 돈 · 인구 문턱이 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class Archive;
    class CitySimulation;
    class Wallet;

    /**
     * @class NileCityPlanner
     * @brief 나일 강가 땅을 칠하고, 자동 플레이의 짓기 순서표를 한 단계씩 실행합니다.
     */
    class NileCityPlanner
    {
    public:
        static constexpr int32 kMapWidth  = 48;
        static constexpr int32 kMapHeight = 40;

        NileCityPlanner();

        /** @brief 강(남북으로 굽이친다) · 양쪽 범람원 · 서쪽과 동쪽 끝의 사막 · 사막의 바위를 칠합니다. 결정적입니다(난수 없음). */
        static void paintTerrain( CitySimulation& city );
        /** @brief 칸의 강 한가운데 x(실수)입니다 — 지형과 화면이 같은 곡선을 씁니다. */
        static float32 computeRiverCenter( int32 y );

        /** @brief 순서표를 처음부터 다시 둡니다. */
        void reset();
        /**
         * @brief 돈 · 인구가 되는 데까지 순서표를 실행합니다. 이번에 지은(또는 깐) 단계 수입니다.
         * @details 돈이 모자라거나 인구 문턱에 못 미치면 멈추고 다음 부름에 이어 갑니다. 놓을 수 없는 자리(겹침 · 땅)는 경고 없이 건너뜁니다.
         */
        int32 advance( CitySimulation& city, const Wallet& wallet, int32 roadCost );
        bool  isFinished() const { return _nextStep >= static_cast<int32>( _listStep.size() ); }
        int32 getSkippedCount() const { return _skippedCount; }
        int32 getStepCount() const { return static_cast<int32>( _listStep.size() ); }
        int32 getNextStep() const { return _nextStep; }

        /** @brief 진행(다음 단계 · 건너뛴 수)을 씁니다. 계획표 자체는 `reset` 이 다시 짓는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 진행을 바꿉니다. 계획표 밖이면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 순서표 한 칸 — 도로 한 줄(ㄱ 자) 또는 건물 하나입니다. */
        struct PlanStep
        {
            hashed_string _buildingID{}; ///< 비면 도로
            int2          _from{};
            int2          _to{};
            int32         _minPopulation{ 0 };
        };

        void addRoad( const int2& from, const int2& to, int32 minPopulation );
        void addBuilding( const utf8* pBuildingID, int32 x, int32 y, int32 minPopulation );

        vector<PlanStep> _listStep;
        int32            _nextStep;
        int32            _skippedCount;
    };
} // namespace sw
