#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Network/Transport/NetEmulation.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/NetEmulationPresetTable.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [NetEmulationPresetTableTest] 엔진 팩의 회선 프리셋 표를 읽고 이름(대소문자 무시)으로 조건을 만든다 — 꺼짐은 조건이 꺼져 있고, 끊김은 전부 잃는다
 * @details 값의 기준은 데이터(`netemulation.presets.xml`)다. 여기서는 단위 변환(ms → 초, % → 비율, KB/s → B/s)과 조회만 본다.
 */
SW_TEST_CASE( NetEmulationPresetTableTest, ShippedTableMakesConditionsByName )
{
    NetEmulationPresetTable table;
    SW_ASSERT_TRUE( table.loadFromResource( sw::ResourceID{ ::test::getEngineDefaultAssets()._netEmulationPresets.get() } ) );
    SW_ASSERT_TRUE( table._listPreset.empty() == false );

    const NetEmulationPresetDef* pLTE = table.findPreset( "LTE" );
    SW_ASSERT_NOT_NULL( pLTE );
    NetEmulationConditions conditions;
    SW_ASSERT_TRUE( table.tryGetConditions( "lte", conditions ) );
    SW_EXPECT_NEAR_EQUAL( conditions._latency, static_cast<float64>( pLTE->_latencyMs ) / static_cast<float64>( constant::kMillisecondsPerSecond ), 1.0e-9 );
    SW_EXPECT_NEAR_EQUAL( conditions._lossRate, pLTE->_lossPercent / 100.0f, 1.0e-6f );
    SW_EXPECT_EQUAL( conditions._bandwidthBytesPerSecond, pLTE->_bandwidthKilobytesPerSecond * static_cast<int32>( constant::kBytesPerKilobyte ) );

    SW_ASSERT_TRUE( table.tryGetConditions( "off", conditions ) );
    SW_EXPECT_FALSE( conditions.isActive() );
    SW_ASSERT_TRUE( table.tryGetConditions( "disconnected", conditions ) );
    SW_EXPECT_NEAR_EQUAL( conditions._lossRate, 1.0f, 1.0e-6f );
    SW_EXPECT_FALSE( table.tryGetConditions( "dialup", conditions ) );
    SW_EXPECT_TRUE( table.makePresetNames().find( "badwifi" ) != string::npos );
}

/**
 * @brief [NetEmulationPresetTableTest] 틀린 표는 로드 시점에 오류다 — 모르는 속성, 범위 밖 값, 겹친 이름, 빈 표. 실패하면 표가 비어 코드 폴백이 없다
 */
SW_TEST_CASE( NetEmulationPresetTableTest, BrokenTableIsRejectedAtLoad )
{
    struct BrokenCase
    {
        const utf8* _pXML;
        const utf8* _pExpectedID;
    };
    const BrokenCase arrCase[] = {
        {                                   "<NetEmulationPresetTable><_listPreset><NetEmulationPresetDef _name=\"a\" _label=\"A\" _pingMs=\"5\"/></_listPreset></NetEmulationPresetTable>","Serialization.OrphanValue"                                                                                                                                                                                            },
        {                            "<NetEmulationPresetTable><_listPreset><NetEmulationPresetDef _name=\"a\" _label=\"A\" _lossPercent=\"150\"/></_listPreset></NetEmulationPresetTable>", "Serialization.ValueOutOfRange"},
        {"<NetEmulationPresetTable><_listPreset><NetEmulationPresetDef _name=\"a\" _label=\"A\"/><NetEmulationPresetDef _name=\"A\" _label=\"B\"/></_listPreset></NetEmulationPresetTable>",
         "Net.PresetDuplicate"                                                                                                                                                                                              },
        {                                                                                                  "<NetEmulationPresetTable><_listPreset></_listPreset></NetEmulationPresetTable>",          "Net.PresetTableEmpty"},
    };
    for ( const BrokenCase& brokenCase : arrCase )
    {
        const string path = test::makeTempPath( "broken.presets.xml" );
        SW_ASSERT_TRUE( FileUtil::writeTextFile( path, brokenCase._pXML ) );
        SW_TEST_DEFENSIVE_SCOPE( "a broken preset table is reported" );
        test::ScopedLogCollector logCollector;
        NetEmulationPresetTable  table;
        SW_EXPECT_FALSE_MSG( table.loadFromResource( sw::ResourceID{ ::test::makeTempResourceID( path ) } ), brokenCase._pXML );
        SW_EXPECT_TRUE( table._listPreset.empty() );
        SW_EXPECT_TRUE_MSG( logCollector.countWithID( brokenCase._pExpectedID ) >= 1u, logCollector.joined().c_str() );
    }
}
