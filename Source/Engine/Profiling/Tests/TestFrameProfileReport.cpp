// 프레임 프로파일의 기계 판독 보고 — JSON 왕복과 칸 이름(회귀 스크립트가 읽는 계약).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Profiling/FrameProfileReport.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

using namespace sw;

/** @brief [FrameProfileReportTest] 쓴 JSON 을 다시 읽으면 메타데이터 · 히치 · 할당 · 구간 표가 그대로 온다. 칸 이름은 스크립트가 읽는 계약이다 */
SW_TEST_CASE( FrameProfileReportTest, JsonRoundTrips )
{
    FrameProfileReport report;
    report._build                  = "Debug";
    report._rhiBackend             = "Vulkan";
    report._scene                  = "engine/scenes/empty.scene.xml";
    report._frames                 = 600;
    report._elapsedMicro           = 10000000;
    report._microPerFrame          = 16666;
    report._listWorstFrameMicro    = { 40000, 30000, 20000, 17000, 16900 };
    report._over16ms               = 4;
    report._allocationTracked      = 1;
    report._allocationsPerFrameX10 = 125;
    FrameProfileScopeSummary scope;
    scope._name             = "GT.Frame";
    scope._avgMicro         = 16000;
    scope._callsPerFrameX10 = 10;
    report._listScope.push_back( scope );

    const string path = test::makeScratchPath( "report.profile.json" );
    SW_REQUIRE_TRUE( report.saveToFile( path ) );
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path, text ) );
    SW_EXPECT_TRUE( text.find( "\"_allocationsPerFrameX10\"" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "\"_schemaVersion\"" ) != string::npos );

    FrameProfileReport read;
    SW_REQUIRE_TRUE( JSONSerializer::deserialize( &read, *FrameProfileReport::StaticType(), text ) );
    SW_EXPECT_EQUAL( FrameProfileReport::kSchemaVersion, read._schemaVersion );
    SW_EXPECT_EQUAL( string( "Vulkan" ), read._rhiBackend );
    SW_EXPECT_EQUAL( int64( 600 ), read._frames );
    SW_REQUIRE_EQUAL( size_t( 5 ), read._listWorstFrameMicro.size() );
    SW_EXPECT_EQUAL( int64( 40000 ), read._listWorstFrameMicro[0] );
    SW_EXPECT_EQUAL( int64( 125 ), read._allocationsPerFrameX10 );
    SW_REQUIRE_EQUAL( size_t( 1 ), read._listScope.size() );
    SW_EXPECT_EQUAL( string( "GT.Frame" ), read._listScope[0]._name );
    SW_EXPECT_EQUAL( int64( 16000 ), read._listScope[0]._avgMicro );
}
