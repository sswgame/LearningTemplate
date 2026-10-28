#include "pch.h"

#include "Engine/Asset/AssetDataKind.h"

#include "Games/NileCity/NileCityPlanner.h"

namespace sw
{
    namespace
    {
        [[nodiscard]] bool loadNileAutoPlan( const AssetID& id )
        {
            vector<NileAutoPlanStep> listStep;
            return NileCityPlanner::readPlanFile( id, listStep );
        }
    } // namespace

    /** @brief NileCity 자동 계획(`NileAutoPlanStepDef` 의 원소 목록)의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 알아본다). */
    SW_RESOURCE_DATA_KIND( NileAutoPlan, "nileautoplan", "game/", nullptr, "/data/autoplan.xml", &loadNileAutoPlan );
} // namespace sw
