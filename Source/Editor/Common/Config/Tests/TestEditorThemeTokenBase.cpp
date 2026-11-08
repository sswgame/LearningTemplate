#include "pch.h"

#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Config/EditorThemeTokens.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorThemeTokenBaseTestUtil
    {
        static constexpr string_view kParent = R"({
            "_name": "parentset", "_displayName": "Parent",
            "_color": { "Base.Text": "#FFFFFF", "Base.Back": "#000000" },
            "_metric": { "Typography.FontSize": 14, "Typography.IconScale": 1, "Layout.Density": 1, "Spacing.Gap": 4 }
        })";

        [[nodiscard]] static string writeTheme( string_view name, string_view text )
        {
            const string path = test::makeScratchPath( string( name ) + EditorThemeTokens::kExtension );
            return FileUtil::writeTextFile( path, text ) ? path : string{};
        }
    };
    using Util = EditorThemeTokenBaseTestUtil;
} // namespace

/** @brief [EditorThemeTokenBaseTest] 자식 벌은 다른 값만 적는다 — 적은 키는 자식 값, 나머지는 부모 값이고 이름은 물려받지 않는다 */
SW_TEST_CASE( EditorThemeTokenBaseTest, ChildOverridesOnlyWrittenKeys )
{
    SW_REQUIRE_FALSE( Util::writeTheme( "parentset", Util::kParent ).empty() );
    const string childPath = Util::writeTheme( "childset", R"({
        "_name": "childset", "_displayName": "Child", "_base": "parentset",
        "_color": { "Base.Back": "#102030" },
        "_metric": { "Spacing.Gap": 8 }
    })" );
    SW_REQUIRE_FALSE( childPath.empty() );

    const expected<EditorThemeConfig> loaded = EditorThemeTokens::loadThemeFile( childPath );
    SW_REQUIRE_TRUE_MSG( loaded.has_value(), loaded.has_value() ? string_view{} : loaded.error().message() );
    const EditorThemeConfig& config = *loaded;
    SW_EXPECT_EQUAL( string( "childset" ), config._name );
    const float32* pGap = config.findMetric( hashed_string( "Spacing.Gap" ) );
    SW_REQUIRE_NOT_NULL( pGap );
    SW_EXPECT_NEAR( 8.0f, *pGap, 0.0f );
    const float32* pFontSize = config.findMetric( hashed_string( "Typography.FontSize" ) );
    SW_REQUIRE_NOT_NULL( pFontSize );
    SW_EXPECT_NEAR( 14.0f, *pFontSize, 0.0f );

    // 키 묶음은 부모와 같다 — 벌끼리의 키 대조가 그대로 돈다.
    const expected<EditorThemeConfig> parentConfig = EditorThemeTokens::loadThemeFile( test::makeScratchPath( "parentset.tokens.json" ) );
    SW_REQUIRE_TRUE( parentConfig.has_value() );
    string mismatchKey;
    SW_EXPECT_FALSE( EditorThemeTokens::findKeyMismatch( *parentConfig, config, mismatchKey ) );
}

/** @brief [EditorThemeTokenBaseTest] 없는 부모와 순환은 읽기 실패다 — 자식 칸만으로 읽지 않는다 */
SW_TEST_CASE( EditorThemeTokenBaseTest, MissingParentAndCycleFail )
{
    const string orphanPath = Util::writeTheme( "orphanset", R"({ "_name": "orphanset", "_base": "nosuchset", "_color": { "Base.Text": "#FFFFFF" }, "_metric": { "Spacing.Gap": 8 } })" );
    SW_REQUIRE_FALSE( orphanPath.empty() );
    const expected<EditorThemeConfig> orphan = EditorThemeTokens::loadThemeFile( orphanPath );
    SW_REQUIRE_FALSE( orphan.has_value() );
    SW_EXPECT_TRUE( orphan.error().message().find( "file not found" ) != string_view::npos );

    SW_REQUIRE_FALSE( Util::writeTheme( "cycleb", R"({ "_name": "cycleb", "_base": "cyclea" })" ).empty() );
    const string cyclePath = Util::writeTheme( "cyclea", R"({ "_name": "cyclea", "_base": "cycleb" })" );
    SW_REQUIRE_FALSE( cyclePath.empty() );
    const expected<EditorThemeConfig> cycle = EditorThemeTokens::loadThemeFile( cyclePath );
    SW_REQUIRE_FALSE( cycle.has_value() );
    SW_EXPECT_TRUE( cycle.error().kind() == ErrorKind::Cycle );
}
