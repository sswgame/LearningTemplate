#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorToolDefaults.h"

#include "TestFramework/TestFramework.h"

// EditorToolDefaultsTest — 에디터 도구 시드 파일(editortooldefaults.json) 읽기. ImGui 없음.

namespace
{
    struct EditorToolDefaultsTestInternal
    {
        /** @brief 필수 칸(`Required`)을 모두 적은 가장 작은 파일 본문입니다. */
        static constexpr const utf8* kCompleteJSON =
            "{ \"_uiPreviewDocument\": \"engine/ui/pause.ui.xml\", \"_inputReplayFile\": \"engine/replay/demo_01.swreplay\", \"_clearColor\": \"0.1,0.2,0.3,1.0\", "
            "\"_listBaseFont\": [ \"a.ttf\" ], \"_listKoreanFont\": [ \"b.ttf\" ] }";
        /** @brief 앞서 읽힌 값으로 둘 맵 경로입니다. `DataPath` 는 데이터 읽기만 채우므로 파일로 넣는다. */
        static constexpr const utf8* kKeptMap = "game/empty/maps/kept.tilemap.json";

        /** @brief 필수 칸과 @ref kKeptMap 을 적은 파일을 읽어 @p outDefaults 의 `_defaultMap` 을 채웁니다. */
        [[nodiscard]] static bool loadKeptMap( sw::editor::EditorToolDefaults& outDefaults )
        {
            const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "editor_tool_defaults_kept" ), "editortooldefaults.json" );
            const sw::string text = sw::string( "{ \"_defaultMap\": \"" ) + kKeptMap +
                                    "\", \"_uiPreviewDocument\": \"engine/ui/pause.ui.xml\", \"_inputReplayFile\": \"engine/replay/demo_01.swreplay\", \"_clearColor\": \"0.1,0.2,0.3,1.0\", \"_listBaseFont\": [ \"a.ttf\" ], "
                                    "\"_listKoreanFont\": [ \"b.ttf\" ] }";
            return sw::FileUtil::writeTextFile( path, text ) && outDefaults.loadFromHostPath( path ) && outDefaults._defaultMap == kKeptMap;
        }
    };
} // namespace

/**
 * @brief [EditorToolDefaultsTest] 값의 정본은 파일 하나다 — 없거나 깨졌거나 필수 칸이 빠지면 실패이고 앞 값을 그대로 둔다(내장 기본값으로 대신하지 않는다)
 * @details 예전에는 헤더 초기값이 기본값이라 파일이 없어도 성공했다. 그러면 파일과 헤더가 같은 값의 두 정본이 되고, 파일을 지워도 아무도 모른다.
 */
SW_TEST_CASE( EditorToolDefaultsTest, MissingBrokenOrIncompleteFileFails )
{
    using sw::editor::EditorToolDefaults;
    SW_TEST_DEFENSIVE_SCOPE( "a missing, broken or incomplete editortooldefaults.json is reported" );
    EditorToolDefaults defaults{};
    SW_ASSERT_TRUE( EditorToolDefaultsTestInternal::loadKeptMap( defaults ) );
    const sw::string_view keptMap = EditorToolDefaultsTestInternal::kKeptMap;

    const sw::string missingPath = sw::FileUtil::joinPath( test::makeTempDirectory( "editor_tool_defaults_missing" ), "editortooldefaults.json" );
    SW_ASSERT_FALSE( sw::FileUtil::exists( missingPath ) );
    SW_EXPECT_FALSE( defaults.loadFromHostPath( missingPath ) );
    SW_EXPECT_TRUE( defaults._defaultMap == keptMap );

    const sw::string brokenPath = sw::FileUtil::joinPath( test::makeTempDirectory( "editor_tool_defaults_broken" ), "editortooldefaults.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( brokenPath, "{ not json" ) );
    SW_EXPECT_FALSE( defaults.loadFromHostPath( brokenPath ) );
    SW_EXPECT_TRUE( defaults._defaultMap == keptMap );

    // 필수 칸(글꼴 목록 · 클리어 색)이 빠지면 선택 칸이 맞아도 실패다.
    const sw::string incompletePath = sw::FileUtil::joinPath( test::makeTempDirectory( "editor_tool_defaults_incomplete" ), "editortooldefaults.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( incompletePath, "{ \"_defaultMap\": \"game/empty/maps/probe.tilemap.json\" }" ) );
    {
        test::ScopedLogCollector logs;
        SW_EXPECT_FALSE( defaults.loadFromHostPath( incompletePath ) );
        SW_EXPECT_TRUE_MSG( logs.joined().find( "_listBaseFont" ) != sw::string::npos, logs.joined() );
    }
    SW_EXPECT_TRUE( defaults._defaultMap == keptMap );
}

/**
 * @brief [EditorToolDefaultsTest] 필수 칸을 모두 적은 파일은 읽히고, 적지 않은 선택 칸은 "없음"(빈 값)이다
 */
SW_TEST_CASE( EditorToolDefaultsTest, CompleteFileLoads )
{
    using sw::editor::EditorToolDefaults;
    const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "editor_tool_defaults_complete" ), "editortooldefaults.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, EditorToolDefaultsTestInternal::kCompleteJSON ) );
    EditorToolDefaults defaults{};
    SW_ASSERT_TRUE( EditorToolDefaultsTestInternal::loadKeptMap( defaults ) ); // 앞서 읽힌 값 — 다음 읽기가 비운다
    SW_ASSERT_TRUE( defaults.loadFromHostPath( path ) );
    SW_EXPECT_FALSE( defaults._defaultMap.isSet() );
    SW_ASSERT_EQUAL( size_t{ 1 }, defaults._listBaseFont.size() );
    SW_EXPECT_EQUAL( sw::string( "a.ttf" ), defaults._listBaseFont[0] );
    SW_EXPECT_NEAR_EQUAL( 0.3f, defaults._clearColor._z, 1e-6f );
}

/**
 * @brief [EditorToolDefaultsTest] 저장소의 Config/Editor/editortooldefaults.json 은 필수 칸을 모두 적어 엄격하게 읽힌다
 */
SW_TEST_CASE( EditorToolDefaultsTest, CommittedFileLoads )
{
    sw::editor::EditorToolDefaults defaults{};
    SW_ASSERT_TRUE( defaults.loadFromHostPath() );
    SW_EXPECT_FALSE( defaults._listBaseFont.empty() );
    SW_EXPECT_FALSE( defaults._listKoreanFont.empty() );
}
