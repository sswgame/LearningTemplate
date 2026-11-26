/**
 * @file NileCityPlanner.h
 * @brief NileCity 의 땅과 자동 도시 계획 — 타일맵 에셋의 땅 칠하기와 `-gv_nileAutoPlay=1` 이 따라 짓는 순서표입니다.
 *
 * @details 화면 · 입력을 모르는 순수 규칙이라 엔진 밖 하네스에서 그대로 돌려 볼 수 있습니다(키트 `CitySimulation` 만 씁니다).
 *          순서표는 도로 고리 → 우물 → 범람원 농장 → 곡물 창고 → 바자 → 집 → 순회 서비스 → 다음 구역 순서이고, 단계마다 돈 · 인구 임계값이 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    class Archive;
    class CitySimulation;
    class Wallet;
    struct TileMapXMLData;

    /**
     * @brief 자동 계획의 튜닝값입니다. 값은 `NileGameModeComponent` 의 PROPERTY 가 채운다(기본값도 그 생성자에 있다).
     * @details 길이 단위는 칸입니다. 계획표의 다른 좌표(구역 경계 · 집 줄)는 순서표 자체다(`data/autoplan.xml`).
     */
    struct NileCityLayout
    {
        int32 _roadX{};             ///< 범람원 농장과 도시 사이 남북 도로
        int32 _farmX{};             ///< 3×3 농장의 왼쪽 — 어느 줄에서나 범람원이어야 한다
        int32 _secondBlockPeople{}; ///< 2 구역을 짓기 시작하는 인구
        int32 _thirdBlockPeople{};  ///< 3 구역을 짓기 시작하는 인구
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NileAutoPlanStepDef
     * @brief 자동 계획 파일(`data/autoplan.xml`)의 `<Road>` · `<Building>` 원소 하나의 칸입니다. 속성 이름은 칸 이름에서 `_` 를 뗀 것이다(`XMLFields::readProperties`).
     * @details 칸 좌표("x,y", x 는 배치 이름 road · farm 일 수 있다)는 배치를 알아야 풀리므로 글 그대로 둔다 — 푸는 쪽은 `NileCityPlanner::reset` 이다.
     */
    REFLECT()
    struct NileAutoPlanStepDef
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "First cell \"x,y\" - x may be a layout name (road, farm)" )
        string _from{};
        PROPERTY( Tooltip = "Last cell of a road line or a building row; not written means the first cell" )
        string _to{};
        PROPERTY( Required, Tooltip = "Block (1..3) whose starting population gates this step" )
        int32 _block{ 0 };
        PROPERTY( Min = 0, Tooltip = "People needed beyond the block's starting population" )
        int32 _extra{ 0 };
        PROPERTY( Tooltip = "Building id - Building steps only" )
        string _id{};
    };
} // namespace sw

namespace sw
{
    /** @brief 자동 계획 파일의 한 단계 — 원소 이름(`Road` · `Building`)과 그 칸입니다. */
    struct NileAutoPlanStep
    {
        bool                _bRoad{ false };
        NileAutoPlanStepDef _def{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class NileCityPlanner
     * @brief 나일 강가 땅을 칠하고, 자동 플레이의 짓기 순서표를 한 단계씩 실행합니다. 진행은 리플렉션 상태 칸입니다(`StateIO::reflected`).
     */
    REFLECT()
    class NileCityPlanner
    {
    public:
        REFLECT_BODY();

        /// @brief 타일맵에서 땅을 적은 데이터 레이어 이름입니다. 칸 값은 땅 목록의 번호다
        static constexpr const utf8* kTerrainLayer = "terrain";

        NileCityPlanner();

        /**
         * @brief 타일맵 @p tileMap 의 데이터 레이어 `kTerrainLayer` 대로 땅을 칠합니다. 칸 값은 @p listTerrain(도시 카탈로그의 `<Terrain>` id)의 번호입니다.
         * @return 레이어가 없거나 크기가 도시와 다르거나 목록의 땅이 카탈로그에 없거나 칸 값이 목록 밖이면 오류를 남기고 false 이며 칠하지 않습니다.
         */
        [[nodiscard]] static bool paintTerrain( CitySimulation& city, const TileMapXMLData& tileMap, const vector<hashed_string>& listTerrain );

        /**
         * @brief 순서표를 데이터 @p planPath(자동 계획 XML)와 @p layout 으로 처음부터 다시 짓습니다. 만든 직후에는 순서표가 비어 있습니다.
         * @details 계획의 칸 · 건물 · 인구 임계값은 데이터가 기준이다(코드에 좌표가 없다). 칸의 x 는 배치의 이름(road · farm)일 수 있고,
         *          임계값은 구역(1 · 2 · 3 — 배치의 구역 인구) + extra 다.
         * @return 파일을 읽지 못했거나 단계 하나라도 모양이 틀리면 오류를 남기고 false 이며 순서표는 빈다.
         */
        [[nodiscard]] bool reset( const NileCityLayout& layout, const AssetID& assetID );
        /**
         * @brief 자동 계획 파일 @p planPath 의 단계를 배치 없이 읽습니다(원소 이름 · 칸의 형식과 필수 칸까지). 칸 좌표는 풀지 않는다.
         * @return 파일이나 단계 하나라도 읽지 못하면 오류를 남기고 false 이며 @p outListStep 은 빈다. 데이터 종류 등록(`NileAutoPlanDataKind.cpp`)도 이것으로 읽어 본다.
         */
        [[nodiscard]] static bool readPlanFile( const AssetID& assetID, vector<NileAutoPlanStep>& outListStep );
        /**
         * @brief 돈 · 인구가 되는 데까지 순서표를 실행합니다. 이번에 지은(또는 깐) 단계 수입니다.
         * @details 돈이 모자라거나 인구 임계값에 못 미치면 멈추고 다음 부름에 이어 갑니다. 놓을 수 없는 자리(겹침 · 땅)는 경고 없이 건너뜁니다.
         */
        int32 advance( CitySimulation& city, const Wallet& wallet, int32 roadCost );
        bool  isFinished() const { return _nextStep >= static_cast<int32>( _listStep.size() ); }
        int32 getSkippedCount() const { return _skippedCount; }
        int32 getStepCount() const { return static_cast<int32>( _listStep.size() ); }
        int32 getNextStep() const { return _nextStep; }

        /**
         * @brief 진행(다음 단계 · 건너뛴 수)을 씁니다. 계획표 자체는 `reset` 이 다시 짓는다.
         *        `writeState` 의 바이트로 진행을 바꿉니다. 계획표 밖이면 false 이고 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

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

    private:
        SW_CODEC_STATE vector<PlanStep> _listStep;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _nextStep;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _skippedCount;
    };
} // namespace sw
