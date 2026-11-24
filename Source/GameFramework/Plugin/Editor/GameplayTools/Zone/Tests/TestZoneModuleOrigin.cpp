// ZoneModuleOriginTest — 지역 볼륨 모듈 줄의 출처: 덮어쓰기가 없으면 물려받음, 프리셋에 없으면 더함, 있으면 방식대로(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Zone/ZoneModuleOrigin.h"

using namespace sw;
using namespace sw::editor;

/** @brief [ZoneModuleOriginTest] 이름으로 찾은 덮어쓰기와 프리셋 유무가 출처를 정한다 */
SW_TEST_CASE( ZoneModuleOriginTest, OverrideAndPresetDecideTheOrigin )
{
    vector<ZoneModuleDef> listOverride( 2 );
    listOverride[0]._name = hashed_string( "heat" );
    listOverride[0]._op   = ZoneOverrideOp::Merge;
    listOverride[1]._name = hashed_string( "fog" );
    listOverride[1]._op   = ZoneOverrideOp::Replace;

    const ZoneModuleDef* pHeat = ZoneModuleOriginUtil::findOverride( listOverride, hashed_string( "heat" ) );
    const ZoneModuleDef* pFog  = ZoneModuleOriginUtil::findOverride( listOverride, hashed_string( "fog" ) );
    SW_REQUIRE_NOT_NULL( pHeat );
    SW_REQUIRE_NOT_NULL( pFog );
    SW_EXPECT_NULL( ZoneModuleOriginUtil::findOverride( listOverride, hashed_string( "wind" ) ) );

    SW_EXPECT_TRUE( ZoneModuleOriginUtil::classify( nullptr, true ) == ZoneModuleOrigin::Inherited );
    SW_EXPECT_TRUE( ZoneModuleOriginUtil::classify( pHeat, true ) == ZoneModuleOrigin::Changed );
    SW_EXPECT_TRUE( ZoneModuleOriginUtil::classify( pFog, true ) == ZoneModuleOrigin::Replaced );
    SW_EXPECT_TRUE( ZoneModuleOriginUtil::classify( pFog, false ) == ZoneModuleOrigin::Added );
    listOverride[1]._op = ZoneOverrideOp::Remove;
    SW_EXPECT_TRUE( ZoneModuleOriginUtil::classify( pFog, true ) == ZoneModuleOrigin::Removed );
}
