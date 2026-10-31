// NetEmulationPresetTable — 엔진 팩의 회선 프리셋 표를 읽어 이름(대소문자 무시)으로 조건을 만들고(ms → 초, % → 비율, KB/s → B/s), 틀린 표는 로드 때 거절한다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/NetEmulationPresetTable.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;

/**
 * @brief [NetEmulationPresetTableTest] 엔진 팩의 회선 프리셋 표를 읽고 이름(대소문자 무시)으로 조건을 만든다 — 꺼짐은 조건이 꺼져 있고, 끊김은 전부 잃는다
 * @details 값의 기준은 데이터(`netemulation.presets.xml`)다. 여기서는 단위 변환(ms → 초, % → 비율, KB/s → B/s)과 조회만 본다.
 */
SW_TEST_CASE( NetEmulationPresetTableTest, ShippedTableMakesConditionsByName )
{
    NetEmulationPresetTable table;
    SW_REQUIRE_TRUE( table.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._netEmulationPresets.get() } ) );
    SW_REQUIRE_TRUE( table._listPreset.empty() == false );

    const NetEmulationPresetDef* pLTE = table.findPreset( "LTE" );
    SW_REQUIRE_NOT_NULL( pLTE );
    NetEmulationConditions conditions;
    SW_REQUIRE_TRUE( table.tryGetConditions( "lte", conditions ) );
    SW_EXPECT_NEAR( static_cast<float64>( pLTE->_latencyMs ) / static_cast<float64>( constant::kMillisecondsPerSecond ), conditions._latency, 1.0e-9 );
    SW_EXPECT_NEAR( pLTE->_lossPercent / 100.0f, conditions._lossRate, 1.0e-6 );
    SW_EXPECT_EQUAL( pLTE->_bandwidthKilobytesPerSecond * static_cast<int32>( constant::kBytesPerKilobyte ), conditions._bandwidthBytesPerSecond );

    SW_REQUIRE_TRUE( table.tryGetConditions( "off", conditions ) );
    SW_EXPECT_FALSE( conditions.isActive() );
    SW_REQUIRE_TRUE( table.tryGetConditions( "disconnected", conditions ) );
    SW_EXPECT_NEAR( 1.0f, conditions._lossRate, 1.0e-6 );
    SW_EXPECT_FALSE( table.tryGetConditions( "dialup", conditions ) );
    SW_EXPECT_TRUE( table.makePresetNames().find( "badwifi" ) != string::npos );
}

namespace
{
    struct BrokenPresetTable
    {
        const utf8* _pXML;
        const utf8* _pExpectedID;
    };

    const BrokenPresetTable kArrBrokenPresetTable[] = {
        {                                   "<NetEmulationPresetTable><_listPreset><NetEmulationPresetDef _name=\"a\" _label=\"A\" _pingMs=\"5\"/></_listPreset></NetEmulationPresetTable>",
         "Serialization.OrphanValue"                                                                                                                                                                               },
        {                            "<NetEmulationPresetTable><_listPreset><NetEmulationPresetDef _name=\"a\" _label=\"A\" _lossPercent=\"150\"/></_listPreset></NetEmulationPresetTable>",
         "Serialization.ValueOutOfRange"                                                                                                                                                                           },
        {"<NetEmulationPresetTable><_listPreset><NetEmulationPresetDef _name=\"a\" _label=\"A\"/><NetEmulationPresetDef _name=\"A\" _label=\"B\"/></_listPreset></NetEmulationPresetTable>",
         "Net.PresetDuplicate"                                                                                                                                                                                     },
        {                                                                                                  "<NetEmulationPresetTable><_listPreset></_listPreset></NetEmulationPresetTable>", "Net.PresetTableEmpty"},
    };
} // namespace

/**
 * @brief [NetEmulationPresetTableTest] 틀린 표는 로드 시점에 오류다 — 모르는 속성, 범위 밖 값, 겹친 이름, 빈 표. 실패하면 표가 비어 코드 폴백이 없다
 */
SW_TEST_CASE_EACH( NetEmulationPresetTableTest, BrokenTableIsRejectedAtLoad, kArrBrokenPresetTable )
{
    const string path = test::makeScratchPath( "broken.presets.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, param._pXML ) );
    SW_EXPECT_LOG( param._pExpectedID );
    NetEmulationPresetTable table;
    SW_EXPECT_FALSE_MSG( table.loadFromDiskFile( path ), param._pXML );
    SW_EXPECT_TRUE( table._listPreset.empty() );
}
