#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/HeightfieldImporter.h"
#include "Editor/Common/Asset/ModelImporter.h"
#include "Editor/Common/Asset/TextureImporter.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/IAssetCache.h"

#include "TestFramework/TestFramework.h"

SW_TEST_CASE( EditorAssetTypeTest, MatchesKnownSuffixes )
{
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Scene, "maps/town.scene.xml" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Prefab, "prefabs/hero.prefab.xml" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::AnimGraph, "anim/idle.anim.json" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::TileMap, "maps/overworld.tilemap.xml" ) );
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::TileMap, "maps/town.scene.xml" ) );
    // 죽은 확장자는 **음성으로** 못 박는다. 되살아나면 여기서 걸린다.
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Material, "mats/hero.mat" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Material, "mats/hero.material" ) );
}

/**
 * @brief [EditorAssetTypeTest] 씬 · 프리팹 판정은 쿠커의 규칙 하나다(`AssetCookPath`)
 * @details 에디터가 접미사 표를 따로 들면 쿠커가 쿠킹하지 않는 이름도 씬 · 프리팹으로 본다 — `.scene` 이 어디든 든 `.xml`(`forest.scenery.xml`),
 *          `_scene.xml`, 확장자 없는 `.scene` · `.prefab`. 그런 파일은 에디터에서는 열리고 저장되지만 배포본에서는 "Shipping requires cooked binary"
 *          로 멈춘다. 쿠킹본 `.prefab.bin` 이 프리팹 편집기로 열리면 저장이 거절되고(`PrefabAsset::saveToFile` 은 소스만 쓴다), 카탈로그가 프리팹을
 *          `.prefab.xml` 하나로 세면 JSON 프리팹이 빠진다.
 */
SW_TEST_CASE( EditorAssetTypeTest, SceneAndPrefabFollowTheCookersSourceRule )
{
    using sw::editor::EditorAssetType;
    using sw::editor::EditorAssetTypeRegistry;

    const utf8* const arrPath[] = {
        "maps/town.scene.xml",
        "maps/TOWN.SCENE.XML",
        "maps/forest.scenery.xml",
        "maps/level_scene.xml",
        "maps/a.scene",
        "maps/a.scene.bin",
        "maps/a.scene.xml.bak",
        "prefabs/hero.prefab.xml",
        "prefabs/hero.prefab.json",
        "prefabs/hero.prefab.bin",
        "prefabs/hero.prefab",
        "prefabs/hero.xml",
    };
    for ( const utf8* pPath : arrPath )
    {
        const bool bCookedScene  = sw::AssetCookPath::isCookableSource( pPath, sw::AssetKind::Scene );
        const bool bCookedPrefab = sw::AssetCookPath::isCookableSource( pPath, sw::AssetKind::Prefab );
        SW_EXPECT_TRUE_MSG( EditorAssetTypeRegistry::matches( EditorAssetType::Scene, pPath ) == bCookedScene, pPath );
        SW_EXPECT_TRUE_MSG( EditorAssetTypeRegistry::matches( EditorAssetType::Prefab, pPath ) == bCookedPrefab, pPath );
    }

    // 규칙 자체가 비어 있으면 위의 대조는 아무것도 지키지 않는다 — 소스는 맞고, 쿠커가 쿠킹하지 않는 이름은 아니다.
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::matches( EditorAssetType::Scene, "maps/TOWN.SCENE.XML" ) );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::matches( EditorAssetType::Prefab, "prefabs/hero.prefab.json" ) );
    SW_EXPECT_FALSE( EditorAssetTypeRegistry::matches( EditorAssetType::Scene, "maps/forest.scenery.xml" ) );
    SW_EXPECT_FALSE( EditorAssetTypeRegistry::matches( EditorAssetType::Scene, "maps/level_scene.xml" ) );
    SW_EXPECT_FALSE( EditorAssetTypeRegistry::matches( EditorAssetType::Scene, "maps/a.scene" ) );
    SW_EXPECT_FALSE( EditorAssetTypeRegistry::matches( EditorAssetType::Prefab, "prefabs/hero.prefab.bin" ) );
    SW_EXPECT_FALSE( EditorAssetTypeRegistry::matches( EditorAssetType::Prefab, "prefabs/hero.prefab" ) );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findPanelTitleForPath( "prefabs/hero.prefab.bin" ).empty() );
    SW_EXPECT_STREQ( "Prefab Editor", sw::string{ EditorAssetTypeRegistry::findPanelTitleForPath( "prefabs/hero.prefab.json" ) }.c_str() );

    // 접미사를 펼치는 쪽(핫 리로드 감시 · 대화상자 필터)도 같은 표다.
    sw::vector<sw::string> listPrefabSuffix;
    EditorAssetTypeRegistry::appendSuffixes( EditorAssetType::Prefab, listPrefabSuffix );
    sw::vector<sw::string_view> listCookSuffix;
    sw::AssetCookPath::appendSourceSuffixes( sw::AssetKind::Prefab, listCookSuffix );
    SW_ASSERT_EQUAL( listCookSuffix.size(), listPrefabSuffix.size() );
    for ( size_t index = 0; index < listCookSuffix.size(); ++index )
        SW_EXPECT_TRUE( listPrefabSuffix[index] == listCookSuffix[index] );
    sw::vector<sw::string> listSceneSuffix;
    EditorAssetTypeRegistry::appendSuffixes( EditorAssetType::Scene, listSceneSuffix );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listSceneSuffix.size() );
    SW_EXPECT_STREQ( ".scene.xml", listSceneSuffix[0].c_str() );

    // 카탈로그가 세는 길 — JSON 프리팹도 세고, 쿠킹본 · 쿠킹하지 않는 이름은 세지 않는다.
    const sw::string folder = test::makeTempDirectory( "assetkind" );
    for ( const utf8* pName : { "a.prefab.xml", "b.prefab.json", "c.prefab.bin", "d.scene.xml", "e.scenery.xml" } )
        SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( folder + "/" + pName, "<x/>" ) );
    sw::vector<sw::string> listPrefabFile;
    sw::vector<sw::string> listSceneFile;
    SW_ASSERT_TRUE( EditorAssetTypeRegistry::collectFiles( EditorAssetType::Prefab, folder, listPrefabFile ) );
    SW_ASSERT_TRUE( EditorAssetTypeRegistry::collectFiles( EditorAssetType::Scene, folder, listSceneFile ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listPrefabFile.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listSceneFile.size() );
    sw::vector<sw::string> listMissing;
    SW_EXPECT_FALSE( EditorAssetTypeRegistry::collectFiles( EditorAssetType::Prefab, folder + "/nope", listMissing ) );
}

SW_TEST_CASE( EditorAssetTypeTest, PanelTitlesAndToolKinds )
{
    SW_EXPECT_STREQ( "Prefab Editor", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetType::Prefab ) );
    SW_EXPECT_STREQ( "Animation Graph",
                     sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetType::AnimGraph ) );

    SW_EXPECT_STREQ( "Sequencer", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetType::Sequence ) );
    SW_EXPECT_STREQ( "Material", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetType::Material ) );

    uint32                             kindCount{ 0 };
    const sw::editor::EditorAssetType* pKind = sw::editor::EditorAssetTypeRegistry::getToolPanelKinds( kindCount );
    SW_ASSERT_NOT_NULL( pKind );
    SW_EXPECT_TRUE( kindCount >= 7 );

    bool bHasAnim{ false };
    bool bHasMaterial{ false };
    for ( uint32 index = 0; index < kindCount; ++index )
    {
        if ( pKind[index] == sw::editor::EditorAssetType::AnimGraph )
            bHasAnim = true;
        if ( pKind[index] == sw::editor::EditorAssetType::Material )
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
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::AnimGraph, "a.anim.json" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Data, "a.anim.json" ) );
}

SW_TEST_CASE( EditorAssetTypeTest, AllAssetKindsAndMatchesAny )
{
    // 1) Texture, Shader, Audio, DialogueGraph, SpriteClip 매칭 검증
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Texture, "textures/albedo.png" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Texture, "textures/normal.dds" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Shader, "shaders/pbr.hlsl" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Shader, "shaders/common.hlsli" ) );
    // 쿠킹된 산출물은 셰이더 소스가 아니다.
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Shader, "shaders/bin/opengl/pbr_ps.spv" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Audio, "audio/bgm.wav" ) );
    SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::Audio, "audio/sfx.ogg" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::DialogueGraph, "dialogue/intro.dialogue.json" ) );
    SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetType::SpriteClip, "sprites/run.sprite.json" ) );

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
 * @details 패널이 타입 이름을 하나씩 비교해 뱃지를 붙이면 게임이 만든 컴포넌트에는 뱃지가 없고, 엔진이 컴포넌트를 늘릴 때마다
 *          패널을 같이 고쳐야 한다. Category 를 쓰면 등록된 어떤 컴포넌트든 뱃지가 붙는다. 그 규약을 여기서 고정한다.
 */
SW_TEST_CASE( EditorAssetTypeTest, HierarchyBadgeComesFromReflectionCategory )
{
    sw::string badge;

    // Category 가 없으면 아무것도 붙지 않는다.
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
 * @details 설정 파일을 다루는 세 곳(EditorConfig 의 load/save, `EditorToolDefaults::loadFromHostPath`)이 이 함수 하나를 쓴다.
 *          "절대 경로인가" 를 손으로 다시 적으면 `FileUtil::isAbsolutePath` 와 어긋나기 쉽다(드라이브 문자가 글자인지 보지 않는 식).
 */
SW_TEST_CASE( EditorAssetTypeTest, ProjectRelativePathLeavesAbsoluteAlone )
{
    // 절대 경로는 구분자만 정규화되고 그대로 나온다 — 프로젝트 루트가 앞에 붙지 않는다.
    const sw::string driveAbs = sw::editor::EditorUtil::resolveProjectRelativePath( "C:\\Temp\\editortooldefaults.json" );
    SW_EXPECT_STREQ( "C:/Temp/editortooldefaults.json", driveAbs.c_str() );

    const sw::string rootAbs = sw::editor::EditorUtil::resolveProjectRelativePath( "/var/tmp/editortooldefaults.json" );
    SW_EXPECT_STREQ( "/var/tmp/editortooldefaults.json", rootAbs.c_str() );

    // 상대 경로는 프로젝트 루트가 있으면 그 아래로 간다. 루트를 못 찾으면 입력 그대로다 —
    // 어느 쪽이든 **입력이 뒤에 그대로 남아 있어야** 한다.
    const sw::string relative = sw::editor::EditorUtil::resolveProjectRelativePath( "Config" + sw::string( 1, '\\' ) + "Editor/editortooldefaults.json" );
    SW_EXPECT_TRUE( relative.find( "Config/Editor/editortooldefaults.json" ) != sw::string::npos );
    SW_EXPECT_TRUE( relative.find( "\\" ) == sw::string::npos ); // 역슬래시는 남지 않는다

    // 빈 입력은 빈 결과이거나 루트 그 자체다 — 어느 쪽이든 터지지 않는다.
    const sw::string empty = sw::editor::EditorUtil::resolveProjectRelativePath( "" );
    SW_EXPECT_TRUE( empty.find( ".." ) == sw::string::npos );
}

/**
 * @brief [EditorAssetTypeTest] 핫 리로드 경로는 표가 정한다 — 파일 종류마다 다시 읽을 엔진 캐시와(있으면) 먼저 돌릴 임포터
 * @details `AssetHotReload` 에는 종류별 코드가 없으므로, 종류를 더하거나 빼는 일은 이 표의 한 줄로 끝나야 한다.
 *          이미지는 SpriteClip 이미지 줄보다 앞선 Texture 줄로 가서 임포트하는 임포터를 탄다. `.dds` 는 임포터가 넘기고 캐시가 다시 읽는다.
 */
SW_TEST_CASE( EditorAssetTypeTest, ReloadRouteComesFromTheTable )
{
    using sw::editor::AssetReloadRoute;
    using sw::editor::EditorAssetTypeRegistry;

    const AssetReloadRoute texture = EditorAssetTypeRegistry::findReloadRoute( "game/empty/textures/hero.dds" );
    SW_ASSERT_NOT_NULL( texture._pCacheKindName );
    SW_EXPECT_STREQ( "Texture", texture._pCacheKindName );
    SW_EXPECT_TRUE( texture._pfnImportSource == &sw::editor::TextureImporter::importChangedSourceImage );
    SW_EXPECT_FALSE( texture._pfnImportSource( "game/empty/textures/hero.dds" ) ); // 임포트된 결과는 캐시가 다시 읽는다

    const AssetReloadRoute spriteImage = EditorAssetTypeRegistry::findReloadRoute( "game/empty/sprites/hero.png" );
    SW_ASSERT_NOT_NULL( spriteImage._pCacheKindName );
    SW_EXPECT_STREQ( "Texture", spriteImage._pCacheKindName );

    const AssetReloadRoute material = EditorAssetTypeRegistry::findReloadRoute( "engine/materials/sprite2d.material" );
    SW_ASSERT_NOT_NULL( material._pCacheKindName );
    SW_EXPECT_STREQ( "Material", material._pCacheKindName );
    SW_EXPECT_TRUE( material._pfnImportSource == nullptr );

    // 메시 에셋은 캐시가 다시 읽고, glTF 원본은 모델 임포터를 먼저 탄다(쓰인 `.mesh` 가 다음 이벤트로 온다).
    const AssetReloadRoute mesh = EditorAssetTypeRegistry::findReloadRoute( "game/empty/models/crate.mesh" );
    SW_ASSERT_NOT_NULL( mesh._pCacheKindName );
    SW_EXPECT_STREQ( "Mesh", mesh._pCacheKindName );
    SW_EXPECT_TRUE( mesh._pfnImportSource == nullptr );
    const AssetReloadRoute model = EditorAssetTypeRegistry::findReloadRoute( "game/empty/models_raw/crate.glb" );
    SW_ASSERT_NOT_NULL( model._pCacheKindName );
    SW_EXPECT_STREQ( "Mesh", model._pCacheKindName );
    SW_EXPECT_TRUE( model._pfnImportSource == &sw::editor::ModelImporter::importChangedSourceModel );

    const AssetReloadRoute prefab = EditorAssetTypeRegistry::findReloadRoute( "prefabs/hero.prefab.xml" );
    SW_ASSERT_NOT_NULL( prefab._pCacheKindName );
    SW_EXPECT_STREQ( "Prefab", prefab._pCacheKindName );

    const AssetReloadRoute clip = EditorAssetTypeRegistry::findReloadRoute( "engine/textures/ui/digits.sprite.json" );
    SW_ASSERT_NOT_NULL( clip._pCacheKindName );
    SW_EXPECT_STREQ( "SpriteClip", clip._pCacheKindName );

    // 다시 읽을 캐시가 없는 종류는 핫 리로드 대상이 아니다.
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findReloadRoute( "engine/shaders/forward.hlsl" )._pCacheKindName == nullptr );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findReloadRoute( "maps/town.scene.xml" )._pCacheKindName == nullptr );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findReloadRoute( "" )._pCacheKindName == nullptr );

    // 감시 확장자는 같은 표에서 온다 — 경로가 있는 종류만.
    sw::vector<sw::string> listSuffix{};
    EditorAssetTypeRegistry::appendReloadableSuffixes( listSuffix );
    const auto hasSuffix = [&listSuffix]( sw::string_view suffix )
    {
        for ( const sw::string& candidate : listSuffix )
        {
            if ( candidate == suffix )
                return true;
        }
        return false;
    };
    SW_EXPECT_TRUE( hasSuffix( ".material" ) );
    SW_EXPECT_TRUE( hasSuffix( ".sprite.json" ) );
    SW_EXPECT_FALSE( hasSuffix( ".hlsl" ) );
}

/**
 * @brief [EditorAssetTypeTest] 표에 적힌 핫 리로드 캐시 이름은 전부 엔진 등록부에 있다
 * @details 이름이 어긋나면 핫 리로드가 그 종류에서 "캐시 없음" 경고만 남기고 아무것도 다시 읽지 않는다 — 에디터 표와 엔진 등록을 이 테스트가 묶는다.
 */
SW_TEST_CASE( EditorAssetTypeTest, EveryReloadCacheNameIsRegisteredInTheEngine )
{
    sw::vector<sw::string_view> listKindName{};
    sw::editor::EditorAssetTypeRegistry::appendReloadCacheKindNames( listKindName );
    SW_ASSERT_TRUE( listKindName.empty() == false );

    sw::AssetManager resources;
    for ( sw::string_view kindName : listKindName )
    {
        const sw::IAssetCache* pCache = resources.findAssetCache( kindName );
        SW_EXPECT_NOT_NULL( pCache );
        if ( pCache != nullptr )
            SW_EXPECT_TRUE( kindName == pCache->getAssetKindName() );
    }
}

/**
 * @brief [EditorAssetTypeTest] 종류 표는 모든 종류에 이름 · 브라우저 라벨 · 아이콘을 하나씩 준다
 * @details 아이콘 · 색 · 퀵 런처 분류가 종류별 분기 체인으로 따로 적혀 있으면 새 종류는 체인마다 빠진다(아이콘 체인에서 빠진
 *          종류는 Data 아이콘으로 보인다). 표 하나에서 읽으므로 칸이 빠지면 컴파일이 멈추고(static_assert), 여기서는 공개 API 로 같은 것을 본다.
 */
SW_TEST_CASE( EditorAssetTypeTest, EveryKindHasIconColorAndCategory )
{
    using sw::editor::EditorAssetType;
    using sw::editor::EditorAssetTypeInfo;
    using sw::editor::EditorAssetTypeRegistry;

    uint32                           infoCount{ 0 };
    const EditorAssetTypeInfo* const pInfo = EditorAssetTypeRegistry::getKindInfos( infoCount );
    SW_ASSERT_NOT_NULL( pInfo );
    SW_EXPECT_EQUAL( static_cast<uint32>( EditorAssetType::Count ) - 1, infoCount );

    for ( uint32 kindValue = 1; kindValue < static_cast<uint32>( EditorAssetType::Count ); ++kindValue )
    {
        const EditorAssetTypeInfo* pKindInfo = EditorAssetTypeRegistry::findKindInfo( static_cast<EditorAssetType>( kindValue ) );
        SW_ASSERT_NOT_NULL( pKindInfo );
        SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( pKindInfo->_pDisplayName ) == false );
        SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( pKindInfo->_pBrowserLabel ) == false );
        SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( pKindInfo->_pIcon ) == false );
        const bool bHasColor = pKindInfo->_bAccentColor || pKindInfo->_color._a > 0.0f;
        SW_EXPECT_TRUE_MSG( bHasColor, pKindInfo->_pDisplayName );
    }
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKindInfo( EditorAssetType::Unknown ) == nullptr );
}

/**
 * @brief [EditorAssetTypeTest] 경로의 종류는 판정 표의 첫 일치 줄 하나다
 * @details 아이콘 · 색 · 퀵 런처 분류 · 카탈로그 · 썸네일 · 열기 · 드롭이 모두 `findKind` 로 종류를 정한다. 겹치는 접미사에서 어느 종류가
 *          이기는지를 못 박는다 — `.seq.json` 은 Data 가 아니라 Sequence 다.
 */
SW_TEST_CASE( EditorAssetTypeTest, FindKindTakesTheFirstMatchingRow )
{
    using sw::editor::EditorAssetType;
    using sw::editor::EditorAssetTypeRegistry;

    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "maps/town.scene.xml" ) == EditorAssetType::Scene );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "prefabs/hero.prefab.json" ) == EditorAssetType::Prefab );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "sprites/hero.png" ) == EditorAssetType::Texture );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "anim/idle.anim.json" ) == EditorAssetType::AnimGraph );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "cut/intro.seq.json" ) == EditorAssetType::Sequence );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "maps/overworld.tilemap.xml" ) == EditorAssetType::TileMap );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "config/input.ini" ) == EditorAssetType::Data );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "notes/todo.txt" ) == EditorAssetType::Unknown );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "" ) == EditorAssetType::Unknown );
}

/**
 * @brief [EditorAssetTypeTest] 임포트 대화상자의 확장자는 종류 표의 임포트 칸이 정한다
 * @details 씬 · 데이터의 접미사는 임포트 목록에 없다(씬은 열기, 데이터는 `.json` · `.txt` 만 따로 더한다). 예외 종류를 이름으로 적지 않고 표의 칸으로 둔다.
 */
SW_TEST_CASE( EditorAssetTypeTest, ImportExtensionsFollowTheImportableColumn )
{
    sw::vector<sw::string> listExtension{};
    sw::editor::EditorAssetTypeRegistry::appendImportExtensions( listExtension );
    const auto hasExtension = [&listExtension]( sw::string_view extension )
    {
        for ( const sw::string& candidate : listExtension )
        {
            if ( candidate == extension )
                return true;
        }
        return false;
    };
    SW_EXPECT_TRUE( hasExtension( ".png" ) );
    SW_EXPECT_TRUE( hasExtension( ".prefab.xml" ) );
    SW_EXPECT_TRUE( hasExtension( ".material" ) );
    SW_EXPECT_TRUE( hasExtension( ".json" ) );
    SW_EXPECT_FALSE( hasExtension( ".scene.xml" ) );
    SW_EXPECT_FALSE( hasExtension( ".ini" ) );
    SW_EXPECT_FALSE( hasExtension( ".kv" ) );
    SW_EXPECT_FALSE( hasExtension( ".xml" ) );
}

/**
 * @brief [EditorAssetTypeTest] `heightfields_raw/` 의 `.png` · `.r16` 은 텍스처가 아니라 높이장 임포트로 간다 — 핫 리로드가 `.r16` 도 본다
 * @details 높이장 줄이 텍스처 줄보다 뒤에 있으면 높이장 원본 PNG 가 텍스처 임포터로 가 "textures_raw 아래에 있어야" 경고만 남고 다시 임포트되지 않는다.
 */
SW_TEST_CASE( EditorAssetTypeTest, RawHeightfieldRoutesToTheHeightfieldImporter )
{
    using sw::editor::EditorAssetType;
    using sw::editor::EditorAssetTypeRegistry;

    SW_EXPECT_TRUE( EditorAssetType::Heightfield == EditorAssetTypeRegistry::findKind( "game/empty/heightfields_raw/valley.png" ) );
    SW_EXPECT_TRUE( EditorAssetType::Heightfield == EditorAssetTypeRegistry::findKind( "game/empty/heightfields_raw/valley_holes.png" ) );
    SW_EXPECT_TRUE( EditorAssetType::Heightfield == EditorAssetTypeRegistry::findKind( "game/empty/heightfields_raw/valley.r16" ) );
    SW_EXPECT_TRUE( EditorAssetType::Heightfield == EditorAssetTypeRegistry::findKind( "game/empty/heightfields/valley.heightfield" ) );
    SW_EXPECT_TRUE( EditorAssetType::Texture == EditorAssetTypeRegistry::findKind( "game/empty/textures_raw/terrain/grass.png" ) );

    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findReloadRoute( "game/empty/heightfields_raw/valley.png" )._pfnImportSource ==
                    &sw::editor::HeightfieldImporter::importChangedSourceHeightfield );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findReloadRoute( "game/empty/textures_raw/terrain/grass.png" )._pfnImportSource ==
                    &sw::editor::TextureImporter::importChangedSourceImage );

    sw::vector<sw::string> listSuffix{};
    EditorAssetTypeRegistry::appendReloadableSuffixes( listSuffix );
    bool bWatchesR16 = false;
    for ( const sw::string& suffix : listSuffix )
        bWatchesR16 = bWatchesR16 || suffix == ".r16";
    SW_EXPECT_TRUE( bWatchesR16 );
}
