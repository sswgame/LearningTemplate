#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorToolDefaults.h"

#include "TestFramework/TestFramework.h"

// EditorToolDefaultsTest — 에디터 도구 시드 파일(editortooldefaults.json) 읽기(패널 점검 D25). ImGui 없음.

/**
 * @brief [EditorToolDefaultsTest] 파일이 없는 것은 실패가 아니다 — 기본값을 쓰고 true 를 돌려준다. 깨진 파일만 false 이고 값을 바꾸지 않는다
 * @details 파일은 기본값과 다른 값이 있을 때만 생기는데, 없을 때 false 를 돌려줘 에디터가 깨끗한 실행마다 "Editor data could not be read" 경고를 남겼다.
 */
SW_TEST_CASE( EditorToolDefaultsTest, MissingFileIsNotAFailure )
{
    using sw::editor::EditorToolDefaults;
    const sw::string missingPath = sw::FileUtil::joinPath( test::makeTempDirectory( "editor_tool_defaults_missing" ), "editortooldefaults.json" );
    SW_ASSERT_FALSE( sw::FileUtil::exists( missingPath ) );

    EditorToolDefaults defaults{};
    defaults._defaultMap = "stale";
    SW_EXPECT_TRUE( defaults.loadFromHostPath( missingPath ) );
    SW_EXPECT_EQUAL( EditorToolDefaults{}._defaultMap, defaults._defaultMap ); // 파일이 없으면 내장 기본값

    // 깨진 파일은 실패이고 앞 값을 그대로 둔다.
    const sw::string brokenPath = sw::FileUtil::joinPath( test::makeTempDirectory( "editor_tool_defaults_broken" ), "editortooldefaults.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( brokenPath, "{ not json" ) );
    defaults._defaultMap = "kept";
    SW_TEST_DEFENSIVE_SCOPE( "a broken editortooldefaults.json is reported" );
    SW_EXPECT_FALSE( defaults.loadFromHostPath( brokenPath ) );
    SW_EXPECT_EQUAL( sw::string( "kept" ), defaults._defaultMap );
}
