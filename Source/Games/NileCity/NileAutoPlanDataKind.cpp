#include "pch.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"

#include "Games/NileCity/NileCityPlanner.h"

namespace sw
{
    namespace
    {
        [[nodiscard]] bool loadNileAutoPlan( const AssetID& id, vector<NileAutoPlanStep>& outListStep )
        {
            return NileCityPlanner::readPlanFile( id, outListStep );
        }
    } // namespace

    /** @brief NileCity 자동 계획(`NileAutoPlanStepDef` 의 원소 목록)의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 알아본다). */
    SW_ASSET_TYPE( NileAutoPlan, AssetTypeDef::makeDataFile( "nileautoplan", AssetPathRule{ "game/", {}, "/data/autoplan.xml" }, AssetTypeLoader::with<vector<NileAutoPlanStep>, &loadNileAutoPlan>( MemoryTag::Game ) ) );
} // namespace sw
