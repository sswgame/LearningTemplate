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
 * @brief [EditorAssetValidationTest] 검증 명령은 스크립트와 파일 경로를 인자 하나씩으로 넘기고 경고까지 묻는다 — 공백이 든 경로도 한 인자다
 */
SW_TEST_CASE( EditorAssetValidationTest, CommandPassesScriptAndFilesAsArguments )
{
    const ProcessCommand  command      = EditorAssetValidation::makeCommand( "C:/My Project", { "game/a b/x.scene.xml", "engine/m.material" } );
    const vector<string>& listArgument = command._listArgument;
    SW_ASSERT_TRUE( listArgument.size() >= 6 );
    const size_t scriptIndex = listArgument.size() - 6; // 실행기 앞 인자(`py -3` 의 `-3`)는 플랫폼마다 다르다
    SW_EXPECT_EQUAL( string( "C:/My Project/Scripts/qa/ValidateAssets.py" ), listArgument[scriptIndex] );
    SW_EXPECT_EQUAL( string( "--severity" ), listArgument[scriptIndex + 1] );
    SW_EXPECT_EQUAL( string( "warning" ), listArgument[scriptIndex + 2] );
    SW_EXPECT_EQUAL( string( "--files" ), listArgument[scriptIndex + 3] );
    SW_EXPECT_EQUAL( string( "game/a b/x.scene.xml" ), listArgument[scriptIndex + 4] );
    SW_EXPECT_EQUAL( string( "engine/m.material" ), listArgument[scriptIndex + 5] );
}
