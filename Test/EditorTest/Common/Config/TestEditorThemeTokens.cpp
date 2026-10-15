#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Config/EditorThemeTokens.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include "TestFramework/TestFramework.h"

// EditorThemeTokensTest — 테마 토큰 파일(Resource/editor/theme/<이름>.tokens.json) 읽기와 해석. ImGui 없음.

/**
 * @brief [EditorThemeTokensTest] 저장소의 토큰 벌은 모두 읽히고 해석되며, 다크 · 라이트 · 고대비가 있다
 * @details 키가 빠지거나 색 글자가 틀리면 에디터는 그 벌을 목록에서 빼고 오류만 남긴다. 화면을 띄우지 않고 여기서 잡는다.
 */
SW_TEST_CASE( EditorThemeTokensTest, EveryTokenSetInTheRepositoryLoads )
{
    sw::vector<sw::string> listName;
    sw::editor::EditorThemeTokens::listThemeNames( listName );
    for ( const utf8* pRequired : { "dark", "light", "highcontrast" } )
    {
        bool bFound = false;
        for ( const sw::string& name : listName )
        {
            bFound = bFound || name == pRequired;
        }
        SW_EXPECT_TRUE( bFound );
    }
    for ( const sw::string& name : listName )
    {
        sw::editor::EditorThemeConfig config{};
        sw::string                    error;
        SW_EXPECT_TRUE( sw::editor::EditorThemeTokens::loadTheme( name, config, error ) );
        SW_EXPECT_EQUAL( name, config._name ); // 파일 이름과 `_name` 이 같아야 설정에 저장한 이름으로 다시 찾는다
        SW_EXPECT_TRUE( config._typography._fontSize > 0.0f );
    }
}

/**
 * @brief [EditorThemeTokensTest] 색 글자가 형식에 맞지 않거나 비면 해석이 실패하고 그 키를 알린다. 결과는 바뀌지 않는다
 */
SW_TEST_CASE( EditorThemeTokensTest, ResolveRejectsMalformedColor )
{
    sw::editor::EditorThemeConfig dark{};
    sw::string                    error;
    SW_ASSERT_TRUE( sw::editor::EditorThemeTokens::loadTheme( sw::editor::EditorThemeTokens::kDefaultName, dark, error ) );

    sw::editor::EditorThemeDef def{};
    def._name                               = "broken";
    def._typography._fontSize               = 16.0f;
    def._typography._iconScale              = 0.8f;
    def._density                            = 1.0f;
    sw::editor::EditorThemeConfig untouched = dark;
    SW_EXPECT_FALSE( sw::editor::EditorThemeTokens::resolve( def, untouched, error ) ); // 색이 모두 비었다
    SW_EXPECT_TRUE( error.find( "_color._text" ) != sw::string::npos );
    SW_EXPECT_EQUAL( dark._name, untouched._name );

    def._color._text = "red";
    SW_EXPECT_FALSE( sw::editor::EditorThemeTokens::resolve( def, untouched, error ) );
    SW_EXPECT_TRUE( error.find( "'red'" ) != sw::string::npos );
}

/**
 * @brief [EditorThemeTokensTest] 토큰 파일이 바뀌면 핫 리로드가 테마 다시 읽기로 보낸다(엔진 캐시로 가지 않는다)
 */
SW_TEST_CASE( EditorThemeTokensTest, TokenFileChangeRoutesToThemeReload )
{
    const sw::editor::AssetReloadRoute route = sw::editor::EditorAssetTypeRegistry::findReloadRoute( "editor/theme/dark.tokens.json" );
    SW_EXPECT_TRUE( route._pfnImportSource == &sw::editor::EditorThemeTokens::importChangedTokenFile );
}
