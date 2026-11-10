// TelemetrySchema — 정의 구조체를 스키마로 읽기(사용자 설정 스키마 테스트에서 옮겼다 — 사용자 설정은 텔레메트리를 보지 않는다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Telemetry/TelemetrySchema.h"

/**
 * @brief [TelemetrySchemaTest] 텔레메트리 스키마의 파이프라인 · 사건 · 칸을 정의 구조체로 읽고, 범위 밖 값은 파일을 받지 않는다
 */
SW_TEST_CASE( TelemetrySchemaTest, ReadsThroughSchemas )
{
    sw::TelemetrySchema schema;
    SW_REQUIRE_TRUE( schema.loadFromXMLText( "<TelemetrySchema version=\"2\"><Pipeline batchEvents=\"8\" sessionSample=\"0.5\" maxTotalBytes=\"4096\"/>"
                                             "<Event id=\"match.end\" sample=\"0.25\"><Field name=\"score\" type=\"Int\" required=\"true\"/></Event></TelemetrySchema>" ) );
    SW_EXPECT_EQUAL( 2, schema.getVersion() );
    SW_EXPECT_EQUAL( uint32{ 8 }, schema.getSettings()._batchEvents );
    SW_EXPECT_EQUAL( uint64{ 4096 }, schema.getSettings()._maxTotalBytes );
    const sw::TelemetryEventDef* pEvent = schema.findEvent( sw::hashed_string( "match.end" ) );
    SW_REQUIRE_TRUE( pEvent != nullptr && pEvent->_listField.size() == 1 );
    SW_EXPECT_TRUE( pEvent->_listField[0]._type == sw::TelemetryFieldType::Int && pEvent->_listField[0]._bRequired );

    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    sw::TelemetrySchema broken;
    SW_EXPECT_FALSE( broken.loadFromXMLText( "<TelemetrySchema><Pipeline sessionSample=\"2\"/></TelemetrySchema>" ) );
}
