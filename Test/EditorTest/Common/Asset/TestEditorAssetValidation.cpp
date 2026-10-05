#include "pch.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorAssetValidationTest] 검증 스크립트의 결과 줄을 심각도 · 경로 · 규칙 · 메시지로 나눈다 — 요약 줄 · 빈 줄은 결과가 아니다
 * @details 출력 형식은 `Scripts/common/AssetValidation.py` 의 `Finding.format` 이다(심각도 7 칸 · 경로 · `: [규칙] ` · 메시지). 메시지 안의
 *          `: [` 나 `] ` 가 경로 · 규칙을 망가뜨리면 안 된다.
 */
SW_TEST_CASE( EditorAssetValidationTest, FindingLinesAreSplitIntoFields )
{
    AssetValidationFinding finding;
    SW_ASSERT_TRUE( EditorAssetValidation::parseFindingLine( "ERROR   game/p/maps/a.scene.xml: [references-exist] x@y points at 'b: [c] d'", finding ) );
    SW_EXPECT_EQUAL( string( "error" ), finding._severity );
    SW_EXPECT_EQUAL( string( "game/p/maps/a.scene.xml" ), finding._path );
    SW_EXPECT_EQUAL( string( "references-exist" ), finding._rule );
    SW_EXPECT_EQUAL( string( "x@y points at 'b: [c] d'" ), finding._message );

    SW_ASSERT_TRUE( EditorAssetValidation::parseFindingLine( "WARNING game/p/models/m.mesh: [orphans] nothing names this file\r", finding ) );
    SW_EXPECT_EQUAL( string( "warning" ), finding._severity );
    SW_EXPECT_EQUAL( string( "orphans" ), finding._rule );

    SW_EXPECT_FALSE( EditorAssetValidation::parseFindingLine( "[ValidateAssets] 1 file(s): 0 error(s), 0 warning(s), 0 info, 23 rule(s)", finding ) );
    SW_EXPECT_FALSE( EditorAssetValidation::parseFindingLine( "", finding ) );
    SW_EXPECT_FALSE( EditorAssetValidation::parseFindingLine( "FATAL   a: [b] c", finding ) );
}

/**
 * @brief [EditorAssetValidationTest] 검증 명령은 스크립트와 파일 경로를 따옴표로 감싸고 경고까지 묻는다
 */
SW_TEST_CASE( EditorAssetValidationTest, CommandQuotesScriptAndFiles )
{
    const string command = EditorAssetValidation::makeCommand( "C:/My Project", { "game/a b/x.scene.xml", "engine/m.material" } );
    SW_EXPECT_TRUE( command.find( "\"C:/My Project/Scripts/qa/ValidateAssets.py\"" ) != string::npos );
    SW_EXPECT_TRUE( command.find( "--severity warning --files \"game/a b/x.scene.xml\" \"engine/m.material\"" ) != string::npos );
}
