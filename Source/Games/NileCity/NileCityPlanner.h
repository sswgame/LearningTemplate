/**
 * @file NileCityPlanner.h
 * @brief NileCity 의 땅과 자동 도시 계획 — 절차 지형(나일 강 · 범람원 · 사막 · 바위)과 `-gv_nileAutoPlay=1` 이 따라 짓는 순서표입니다.
 *
 * @details 화면 · 입력을 모르는 순수 규칙이라 엔진 밖 하네스에서 그대로 돌려 볼 수 있습니다(키트 `CitySimulation` 만 씁니다).
 *          순서표는 도로 고리 → 우물 → 범람원 농장 → 곡물 창고 → 바자 → 집 → 순회 서비스 → 다음 구역 순서이고, 단계마다 돈 · 인구 임계값이 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class Archive;
    class CitySimulation;
    class Wallet;

    /**
     * @brief 땅 모양과 자동 계획의 튜닝값입니다. 값은 `NileDirectorComponent` 의 PROPERTY 가 채운다(기본값도 그 생성자에 있다).
     * @details 길이 단위는 칸입니다. 계획표의 다른 좌표(구역 경계 · 집 줄)는 순서표 자체라 코드에 둔다.
     */
    struct NileCityLayout
    {
        float32 _riverCenterX{};      ///< 강 한가운데 x 의 평균(칸)
        float32 _riverMeander{};      ///< 강이 좌우로 굽이치는 폭(칸)
        float32 _riverMeanderRate{};  ///< y 한 칸마다 굽이 위상(라디안)
        float32 _riverHalfWidth{};    ///< 강 반폭(칸)
        float32 _eastFloodWidth{};    ///< 동쪽 범람원 폭 — 농장 · 도시가 이쪽에 선다
        float32 _westFloodWidth{};    ///< 서쪽 범람원 폭
        float32 _desertDuneHeight{};  ///< 사막 경계가 모래 언덕처럼 굽는 폭(칸)
        float32 _desertDuneRate{};    ///< y 한 칸마다 언덕 위상(라디안)
        int32   _desertStartX{};      ///< 동쪽 사막이 시작하는 x
        int32   _rockRidgeX{};        ///< 이 x 부터 동쪽 끝 바위 능선
        int32   _roadX{};             ///< 범람원 농장과 도시 사이 남북 도로
        int32   _farmX{};             ///< 3×3 농장의 왼쪽 — 어느 줄에서나 범람원이어야 한다
        int32   _secondBlockPeople{}; ///< 2 구역을 짓기 시작하는 인구
        int32   _thirdBlockPeople{};  ///< 3 구역을 짓기 시작하는 인구
    };
} // namespace sw

namespace sw
{
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
        static void paintTerrain( CitySimulation& city, const NileCityLayout& layout );
        /** @brief 칸의 강 한가운데 x(실수)입니다 — 지형과 화면이 같은 곡선을 씁니다. */
        static float32 computeRiverCenter( const NileCityLayout& layout, int32 y );

        /**
         * @brief 순서표를 데이터 @p planPath(자동 계획 XML)와 @p layout 으로 처음부터 다시 짓습니다. 만든 직후에는 순서표가 비어 있습니다.
         * @details 계획의 칸 · 건물 · 인구 임계값은 데이터가 정본이다(코드에 좌표가 없다). 칸의 x 는 배치의 이름(road · farm)일 수 있고,
         *          임계값은 구역(1 · 2 · 3 — 배치의 구역 인구) + extra 다.
         * @return 파일을 읽지 못했거나 단계 하나라도 모양이 틀리면 오류를 남기고 false 이며 순서표는 빈다.
         */
        [[nodiscard]] bool reset( const NileCityLayout& layout, string_view planPath );
        /**
         * @brief 돈 · 인구가 되는 데까지 순서표를 실행합니다. 이번에 지은(또는 깐) 단계 수입니다.
         * @details 돈이 모자라거나 인구 임계값에 못 미치면 멈추고 다음 부름에 이어 갑니다. 놓을 수 없는 자리(겹침 · 땅)는 경고 없이 건너뜁니다.
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
