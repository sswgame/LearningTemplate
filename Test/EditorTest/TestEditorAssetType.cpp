#include "pch.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include "TestFramework/TestFramework.h"

SW_TEST_CASE( EditorAssetTypeTest, MatchesKnownSuffixes )
{
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Scene, "maps/town.scene.xml" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Prefab, "prefabs/hero.prefab.xml" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::AnimationGraph, "anim/idle.anim.json" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::TileMap, "maps/overworld.tilemap.xml" ) );
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::TileMap, "maps/town.scene.xml" ) );
    // 죽은 확장자는 **음성으로** 못 박는다. 되살아나면 여기서 걸린다.
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Material, "mats/hero.mat" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Material, "mats/hero.material" ) );
}

SW_TEST_CASE( EditorAssetTypeTest, PanelTitlesAndToolKinds )
{
    SW_EXPECT_STREQ( "Prefab Editor", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::Prefab ) );
    SW_EXPECT_STREQ( "Animation Graph",
                     sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::AnimationGraph ) );

    SW_EXPECT_STREQ( "Sequencer", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::Sequence ) );
    SW_EXPECT_STREQ( "Material", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::Material ) );

    uint32                             kindCount{ 0 };
    const sw::editor::EditorAssetKind* pKind = sw::editor::EditorAssetTypeRegistry::getToolPanelKinds( kindCount );
    SW_ASSERT_NOT_NULL( pKind );
    SW_EXPECT_TRUE( kindCount >= 7 );

    bool bHasAnim{ false };
    bool bHasMaterial{ false };
    for ( uint32 index = 0; index < kindCount; ++index )
    {
        if ( pKind[index] == sw::editor::EditorAssetKind::AnimationGraph )
            bHasAnim = true;
        if ( pKind[index] == sw::editor::EditorAssetKind::Material )
            bHasMaterial = true;
    }
    SW_EXPECT_TRUE( bHasAnim );
    SW_EXPECT_TRUE( bHasMaterial );
}

SW_TEST_CASE( EditorAssetTypeTest, FindPanelTitleLongestSuffix )
{
    const auto animTitle = sw::editor::EditorAssetTypeRegistry::findPanelTitleForPath( "content/hero.anim.json" );
    SW_EXPECT_STREQ( "Animation Graph", sw::string{ animTitle }.c_str() );

    const auto matTitle = sw::editor::EditorAssetTypeRegistry::findPanelTitleForPath( "content/hero.material" );
    SW_EXPECT_STREQ( "Material", sw::string{ matTitle }.c_str() );

    const auto unknownTitle = sw::editor::EditorAssetTypeRegistry::findPanelTitleForPath( "readme.md" );
    SW_EXPECT_TRUE( unknownTitle.empty() );
}

SW_TEST_CASE( EditorAssetTypeTest, DataDoesNotStealAnimJson )
{
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::AnimationGraph, "a.anim.json" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Data, "a.anim.json" ) );
}

SW_TEST_CASE( EditorAssetTypeTest, AllAssetKindsAndMatchesAny )
{
    // 1) Texture, Shader, Audio, DialogueGraph, SpriteClip 매칭 검증
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Texture, "textures/albedo.png" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Texture, "textures/normal.dds" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Shader, "shaders/pbr.hlsl" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Shader, "shaders/common.hlsli" ) );
    // 구운 산출물은 셰이더 소스가 아니다.
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Shader, "shaders/bin/opengl/pbr_ps.spv" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Audio, "audio/bgm.wav" ) );
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Audio, "audio/sfx.ogg" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::DialogueGraph, "dialogue/intro.dialogue.json" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::SpriteClip, "sprites/run.sprite.json" ) );

    // 2) matchesAny 및 matchesOther 검증
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matchesAny( "maps/town.scene.xml" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matchesAny( "textures/hero.png" ) );
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matchesAny( "notes/todo.txt" ) );

    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matchesOther( "notes/todo.txt" ) );
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matchesOther( "maps/town.scene.xml" ) );

    // 3) appendImportExtensions 검증
    sw::vector<sw::string> listImportExts;
    sw::editor::EditorAssetTypeRegistry::appendImportExtensions( listImportExts );
    SW_EXPECT_FALSE( listImportExts.empty() );

    // 4) getBrowserFilters 검증
    uint32                                      filterCount = 0;
    const sw::editor::EditorAssetBrowserFilter* pFilters    = sw::editor::EditorAssetTypeRegistry::getBrowserFilters( filterCount );
    SW_ASSERT_NOT_NULL( pFilters );
    SW_EXPECT_TRUE( filterCount > 0 );
}

/**
 * @brief [EditorAssetTypeTest] 계층 뱃지는 리플렉션 Category 에서 나온다
 * @details 예전에는 Hierarchy 패널이 타입 이름 7개를 if/else 로 비교했다 — 게임이 자기
 *          컴포넌트를 넣으면 뱃지가 없고, 엔진이 컴포넌트를 늘리면 패널을 같이 고쳐야 했다.
 *          Category 를 쓰면 등록된 어떤 컴포넌트든 뱃지가 붙는다. 그 규약을 여기서 고정한다.
 */
SW_TEST_CASE( EditorAssetTypeTest, HierarchyBadgeComesFromReflectionCategory )
{
    sw::string badge;

    // Category 가 없으면 아무것도 붙지 않는다 (예전의 else 분기와 같다).
    sw::editor::EditorUtil::appendCategoryBadge( "", badge );
    SW_EXPECT_TRUE( badge.empty() );

    sw::editor::EditorUtil::appendCategoryBadge( "Camera", badge );
    SW_EXPECT_STREQ( "[Camera]", badge.c_str() );

    // 두 번째부터는 공백으로 띄운다.
    sw::editor::EditorUtil::appendCategoryBadge( "Rendering 3D", badge );
    SW_EXPECT_STREQ( "[Camera] [Rendering 3D]", badge.c_str() );

    // 같은 Category 컴포넌트가 여럿 붙어 있어도 뱃지는 하나다.
    sw::editor::EditorUtil::appendCategoryBadge( "Rendering 3D", badge );
    SW_EXPECT_STREQ( "[Camera] [Rendering 3D]", badge.c_str() );

    // 다른 Category 는 계속 쌓인다 — 게임이 만든 Category 도 그대로 뜬다.
    sw::editor::EditorUtil::appendCategoryBadge( "MyGameStuff", badge );
    SW_EXPECT_STREQ( "[Camera] [Rendering 3D] [MyGameStuff]", badge.c_str() );
}

/**
 * @brief [EditorAssetTypeTest] 프로젝트 상대 경로 해석은 이미 절대인 경로를 건드리지 않는다
 * @details 이 다섯 줄이 설정 파일을 다루는 **세 곳에 복사**되어 있었고(EditorConfig 의 load/save,
 *          EditorData::loadFromHostPath), 셋 다 "절대 경로인가" 를 손으로 다시 적었다 —
 *          그 손 판정은 `FileUtil::isAbsolutePath` 와 **달랐다**(드라이브 문자가 글자인지 보지
 *          않는다). 한 자리로 모으면서 진짜 판정을 쓰게 했다.
 */
SW_TEST_CASE( EditorAssetTypeTest, ProjectRelativePathLeavesAbsoluteAlone )
{
    // 절대 경로는 구분자만 정규화되고 그대로 나온다 — 프로젝트 루트가 앞에 붙지 않는다.
    const sw::string driveAbs = sw::editor::EditorUtil::resolveProjectRelativePath( "C:\\Temp\\editordata.json" );
    SW_EXPECT_STREQ( "C:/Temp/editordata.json", driveAbs.c_str() );

    const sw::string rootAbs = sw::editor::EditorUtil::resolveProjectRelativePath( "/var/tmp/editordata.json" );
    SW_EXPECT_STREQ( "/var/tmp/editordata.json", rootAbs.c_str() );

    // 상대 경로는 프로젝트 루트가 있으면 그 아래로 간다. 루트를 못 찾으면 입력 그대로다 —
    // 어느 쪽이든 **입력이 뒤에 그대로 남아 있어야** 한다.
    const sw::string relative = sw::editor::EditorUtil::resolveProjectRelativePath( "Config" + sw::string( 1, '\\' ) + "Editor/editordata.json" );
    SW_EXPECT_TRUE( relative.find( "Config/Editor/editordata.json" ) != sw::string::npos );
    SW_EXPECT_TRUE( relative.find( "\\" ) == sw::string::npos ); // 역슬래시는 남지 않는다

    // 빈 입력은 빈 결과이거나 루트 그 자체다 — 어느 쪽이든 터지지 않는다.
    const sw::string empty = sw::editor::EditorUtil::resolveProjectRelativePath( "" );
    SW_EXPECT_TRUE( empty.find( ".." ) == sw::string::npos );
}
