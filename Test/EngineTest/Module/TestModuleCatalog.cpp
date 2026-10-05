/**
 * @file TestModuleCatalog.cpp
 * @brief 모듈 매니페스트 — 읽기 · 거절, 의존 순서, 순환 · 없는 의존 · 꺼진 의존 · 낮은 버전 오류, 프로젝트가 끈 모듈 · 플랫폼 · 구성, 저장소 매니페스트와 빌드 해석의 일치.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 시험용 매니페스트 하나입니다(모든 플랫폼 · 구성, 기본 켜짐). */
    sw::ModuleManifest makeManifest( const utf8* pName, sw::ModuleKind kind, std::initializer_list<const utf8*> listDependency )
    {
        sw::ModuleManifest manifest{};
        manifest._name              = pName;
        manifest._kind              = kind;
        manifest._version           = sw::ModuleVersion{ 1, 0, 0 };
        manifest._platformMask      = static_cast<uint8>( sw::ModulePlatform::Windows ) | static_cast<uint8>( sw::ModulePlatform::Linux );
        manifest._configurationMask = static_cast<uint8>( sw::ModuleConfiguration::Dev ) | static_cast<uint8>( sw::ModuleConfiguration::Shipping );
        manifest._sourcePath        = sw::string( pName ) + ".module.json";
        for ( const utf8* pDependency : listDependency )
            manifest._listDependency.push_back( sw::ModuleDependency{ pDependency, sw::ModuleVersion{} } );
        return manifest;
    }

    /** @brief 매니페스트들을 담은 카탈로그를 만듭니다. */
    sw::ModuleCatalog makeCatalog( std::initializer_list<sw::ModuleManifest> listManifest )
    {
        sw::ModuleCatalog catalog;
        for ( const sw::ModuleManifest& manifest : listManifest )
        {
            sw::string error;
            SW_EXPECT_TRUE_MSG( catalog.addManifest( manifest, error ), error.c_str() );
        }
        return catalog;
    }

    /** @brief 적재 순서를 공백으로 이어 붙입니다. */
    sw::string joinOrder( const sw::ModuleResolution& resolution )
    {
        sw::string text;
        for ( const sw::string& name : resolution._listLoadOrder )
            text += ( text.empty() ? "" : " " ) + name;
        return text;
    }
} // namespace

/**
 * @brief [ModuleCatalogTest] 의존이 먼저, 동점은 이름 순으로 적재 순서를 정한다(목록 순서와 무관)
 */
SW_TEST_CASE( ModuleCatalogTest, LoadOrderPutsDependenciesFirst )
{
    const sw::ModuleCatalog catalog = makeCatalog( {
        makeManifest( "SWGame", sw::ModuleKind::Game, { "GF_Combat", "GameFramework" } ),
        makeManifest( "GF_Combat", sw::ModuleKind::Kit, { "GameFramework", "GF_World" } ),
        makeManifest( "GF_World", sw::ModuleKind::Kit, { "GameFramework" } ),
        makeManifest( "GameFramework", sw::ModuleKind::GameFramework, {} ),
        makeManifest( "EditorModule", sw::ModuleKind::Editor, {} ),
    } );
    sw::ModuleResolution    resolution;
    sw::string              error;
    SW_ASSERT_TRUE_MSG( catalog.resolve( sw::ModuleResolveContext{}, resolution, error ), error.c_str() );
    SW_EXPECT_STREQ( "EditorModule GameFramework GF_World GF_Combat SWGame", joinOrder( resolution ).c_str() );
    SW_EXPECT_TRUE( resolution._listInactive.empty() );
}

/**
 * @brief [ModuleCatalogTest] 순환은 오류이고, 그 경로(A -> B -> A)를 말한다
 */
SW_TEST_CASE( ModuleCatalogTest, CycleIsAnErrorThatNamesThePath )
{
    const sw::ModuleCatalog catalog = makeCatalog( {
        makeManifest( "SWGame", sw::ModuleKind::Game, { "GF_A" } ),
        makeManifest( "GF_A", sw::ModuleKind::Kit, { "GF_B" } ),
        makeManifest( "GF_B", sw::ModuleKind::Kit, { "GF_C" } ),
        makeManifest( "GF_C", sw::ModuleKind::Kit, { "GF_A" } ),
    } );
    sw::ModuleResolution    resolution;
    sw::string              error;
    SW_EXPECT_FALSE( catalog.resolve( sw::ModuleResolveContext{}, resolution, error ) );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( error, "cycle" ), error.c_str() );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( error, "GF_A -> GF_B -> GF_C -> GF_A" ), error.c_str() );
    SW_EXPECT_TRUE( resolution._listLoadOrder.empty() );
}

/**
 * @brief [ModuleCatalogTest] 매니페스트가 없는 의존 · 꺼진 의존 · 낮은 버전은 오류이고, 무엇이 왜인지 말한다
 */
SW_TEST_CASE( ModuleCatalogTest, MissingDisabledOrOldDependencyIsAnError )
{
    {
        const sw::ModuleCatalog catalog = makeCatalog( { makeManifest( "SWGame", sw::ModuleKind::Game, { "GF_Ghost" } ) } );
        sw::ModuleResolution    resolution;
        sw::string              error;
        SW_EXPECT_FALSE( catalog.resolve( sw::ModuleResolveContext{}, resolution, error ) );
        SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( error, "'GF_Ghost', which has no manifest" ), error.c_str() );
    }
    {
        sw::ModuleManifest project = makeManifest( "SWGame", sw::ModuleKind::Game, { "GF_Farm" } );
        project._listModuleOverride.push_back( sw::ModuleOverride{ "GF_Farm", false } );
        const sw::ModuleCatalog catalog = makeCatalog( { project, makeManifest( "GF_Farm", sw::ModuleKind::Kit, {} ) } );
        sw::ModuleResolution    resolution;
        sw::string              error;
        SW_EXPECT_FALSE( catalog.resolve( sw::ModuleResolveContext{}, resolution, error ) );
        SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( error, "'GF_Farm', which is disabled by the project" ), error.c_str() );
    }
    {
        sw::ModuleManifest project = makeManifest( "SWGame", sw::ModuleKind::Game, {} );
        project._listDependency.push_back( sw::ModuleDependency{
            "GF_Farm", sw::ModuleVersion{ 2, 1, 0 }
        } );
        const sw::ModuleCatalog catalog = makeCatalog( { project, makeManifest( "GF_Farm", sw::ModuleKind::Kit, {} ) } );
        sw::ModuleResolution    resolution;
        sw::string              error;
        SW_EXPECT_FALSE( catalog.resolve( sw::ModuleResolveContext{}, resolution, error ) );
        SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( error, "needs 'GF_Farm' 2.1.0 or later, but 1.0.0" ), error.c_str() );
    }
    {
        sw::ModuleManifest project = makeManifest( "SWGame", sw::ModuleKind::Game, {} );
        project._listModuleOverride.push_back( sw::ModuleOverride{ "GF_Typo", false } );
        const sw::ModuleCatalog catalog = makeCatalog( { project } );
        sw::ModuleResolution    resolution;
        sw::string              error;
        SW_EXPECT_FALSE( catalog.resolve( sw::ModuleResolveContext{}, resolution, error ) );
        SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( error, "unknown module 'GF_Typo'" ), error.c_str() );
    }
}

/**
 * @brief [ModuleCatalogTest] 프로젝트가 끈 모듈 · 이 플랫폼 · 구성에 없는 모듈 · 기본 꺼짐은 적재하지 않고 이유를 남긴다. 프로젝트가 켜면 기본 꺼짐도 켜진다
 */
SW_TEST_CASE( ModuleCatalogTest, DisabledAndUnavailableModulesAreSkipped )
{
    sw::ModuleManifest project = makeManifest( "SWGame", sw::ModuleKind::Game, { "GameFramework" } );
    project._listModuleOverride.push_back( sw::ModuleOverride{ "GF_Voxel", false } );
    project._listModuleOverride.push_back( sw::ModuleOverride{ "GF_Optional", true } );

    sw::ModuleManifest windowsOnly  = makeManifest( "RHI_DX12", sw::ModuleKind::Rhi, {} );
    windowsOnly._platformMask       = static_cast<uint8>( sw::ModulePlatform::Windows );
    sw::ModuleManifest devOnly      = makeManifest( "EditorModule", sw::ModuleKind::Editor, {} );
    devOnly._configurationMask      = static_cast<uint8>( sw::ModuleConfiguration::Dev );
    sw::ModuleManifest offByDefault = makeManifest( "GF_Experimental", sw::ModuleKind::Kit, { "GameFramework" } );
    offByDefault._bEnabledByDefault = false;
    sw::ModuleManifest optional     = makeManifest( "GF_Optional", sw::ModuleKind::Kit, { "GameFramework" } );
    optional._bEnabledByDefault     = false;

    const sw::ModuleCatalog catalog = makeCatalog( { project, windowsOnly, devOnly, offByDefault, optional,
                                                     makeManifest( "GF_Voxel", sw::ModuleKind::Kit, { "GameFramework" } ),
                                                     makeManifest( "GameFramework", sw::ModuleKind::GameFramework, {} ) } );

    sw::ModuleResolveContext linuxShipping{};
    linuxShipping._platform      = sw::ModulePlatform::Linux;
    linuxShipping._configuration = sw::ModuleConfiguration::Shipping;
    sw::ModuleResolution resolution;
    sw::string           error;
    SW_ASSERT_TRUE_MSG( catalog.resolve( linuxShipping, resolution, error ), error.c_str() );
    SW_EXPECT_STREQ( "GameFramework GF_Optional SWGame", joinOrder( resolution ).c_str() );
    SW_ASSERT_EQUAL( size_t{ 4 }, resolution._listInactive.size() );
    SW_EXPECT_STREQ( "EditorModule", resolution._listInactive[0]._name.c_str() );
    SW_EXPECT_STREQ( "not built for Shipping", resolution._listInactive[0]._reason.c_str() );
    SW_EXPECT_STREQ( "GF_Experimental", resolution._listInactive[1]._name.c_str() );
    SW_EXPECT_STREQ( "disabled by default", resolution._listInactive[1]._reason.c_str() );
    SW_EXPECT_STREQ( "GF_Voxel", resolution._listInactive[2]._name.c_str() );
    SW_EXPECT_STREQ( "disabled by the project", resolution._listInactive[2]._reason.c_str() );
    SW_EXPECT_STREQ( "RHI_DX12", resolution._listInactive[3]._name.c_str() );
    SW_EXPECT_STREQ( "not available on Linux", resolution._listInactive[3]._reason.c_str() );
    SW_EXPECT_FALSE( resolution.isActive( "GF_Voxel" ) );
    SW_EXPECT_TRUE( resolution.isActive( "GF_Optional" ) );
}

/**
 * @brief [ModuleCatalogTest] 매니페스트 JSON 은 엄격하다 — 모르는 키 · 종류 · 플랫폼 · 틀린 버전 · 파일 이름 불일치 · 게임 아닌 모듈의 켜기 표는 거절한다
 */
SW_TEST_CASE( ModuleCatalogTest, ParsesManifestsStrictly )
{
    const utf8*        pGood = R"({ "_name": "GF_Farm", "_version": "1.2.3", "_kind": "Kit", "_description": "farm",
                             "_listDependency": [ { "_name": "GameFramework", "_minVersion": "1.0.0" } ],
                             "_listPlatform": [ "Windows", "Linux" ], "_listConfiguration": [ "Dev" ], "_bEnabledByDefault": false })";
    sw::ModuleManifest manifest;
    sw::string         error;
    SW_ASSERT_TRUE_MSG( sw::ModuleCatalog::parseManifest( pGood, "Source/X/GF_Farm.module.json", manifest, error ), error.c_str() );
    SW_EXPECT_STREQ( "GF_Farm", manifest._name.c_str() );
    SW_EXPECT_TRUE( manifest._kind == sw::ModuleKind::Kit );
    SW_EXPECT_EQUAL( uint32{ 2 }, manifest._version._minor );
    SW_ASSERT_EQUAL( size_t{ 1 }, manifest._listDependency.size() );
    SW_EXPECT_STREQ( "GameFramework", manifest._listDependency[0]._name.c_str() );
    SW_EXPECT_EQUAL( static_cast<uint8>( sw::ModuleConfiguration::Dev ), manifest._configurationMask );
    SW_EXPECT_FALSE( manifest._bEnabledByDefault );

    const utf8* const arrBad[] = {
        R"({ "_name": "GF_Farm", "_version": "1.2.3", "_kind": "Kit", "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ], "_enabled": true })",
        R"({ "_name": "GF_Farm", "_version": "1.2", "_kind": "Kit", "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ] })",
        R"({ "_name": "GF_Farm", "_version": "1.2.3", "_kind": "Plugin", "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ] })",
        R"({ "_name": "GF_Farm", "_version": "1.2.3", "_kind": "Kit", "_listPlatform": [ "Mac" ], "_listConfiguration": [ "Dev" ] })",
        R"({ "_name": "GF_Farm", "_version": "1.2.3", "_kind": "Kit", "_listPlatform": [ "Windows" ], "_listConfiguration": [] })",
        R"({ "_name": "GF_Farm", "_version": "1.2.3", "_kind": "Kit", "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ],
             "_listModuleOverride": [] })",
        R"({ "_name": "GF_Farm", "_version": "1.2.3", "_kind": "Kit", "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ],
             "_listDependency": [ { "_name": "GF_Farm" } ] })",
        R"({ "_name": "GF_Ranch", "_version": "1.2.3", "_kind": "Kit", "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ] })",
    };
    for ( const utf8* pBad : arrBad )
    {
        sw::string badError;
        SW_EXPECT_FALSE_MSG( sw::ModuleCatalog::parseManifest( pBad, "Source/X/GF_Farm.module.json", manifest, badError ), pBad );
        SW_EXPECT_FALSE_MSG( badError.empty(), pBad );
    }

    sw::ModuleCatalog catalog;
    SW_EXPECT_TRUE( catalog.addManifest( makeManifest( "GF_Farm", sw::ModuleKind::Kit, {} ), error ) );
    SW_EXPECT_FALSE( catalog.addManifest( makeManifest( "GF_Farm", sw::ModuleKind::Kit, {} ), error ) ); // 같은 이름 두 번
}

/**
 * @brief [ModuleCatalogTest] 저장소의 매니페스트가 모두 읽히고, 빌드가 해석한 적재 순서(`Bin/Modules/ResolvedModules.txt`)와 런타임 해석이 같다
 * @details CMake(`cmake/Engine/ModuleManifest.cmake`)와 C++(`ModuleCatalog`)가 같은 규칙을 따로 구현한다 — 어긋나면 빌드한 모듈과 App 이 올리는 모듈이 갈린다.
 *          배포본은 매니페스트를 복사하지 않는다(올릴 이미지가 없다).
 */
SW_TEST_CASE( ModuleCatalogTest, BuildAndRuntimeAgree )
{
    const sw::string catalogDirectory = sw::FileUtil::joinPath( sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() ), sw::ModuleCatalog::kCatalogFolder );
    const sw::string resolvedPath     = sw::FileUtil::joinPath( catalogDirectory, "ResolvedModules.txt" );
    if ( sw::FileUtil::fileExists( resolvedPath ) == false )
    {
        // 시험 실행 파일이 `Bin` 이 아닌 곳(배포본의 TestBin)에 있다 — 작업 폴더의 `Modules/` 를 본다.
        if ( sw::FileUtil::fileExists( sw::FileUtil::joinPath( sw::ModuleCatalog::kCatalogFolder, "ResolvedModules.txt" ) ) == false )
            SW_TEST_SKIP( "no module catalog next to the executable (Shipping does not copy manifests)" );
    }
    const sw::string directory = sw::FileUtil::fileExists( resolvedPath ) ? catalogDirectory : sw::string( sw::ModuleCatalog::kCatalogFolder );

    sw::ModuleCatalog catalog;
    sw::string        error;
    SW_ASSERT_TRUE_MSG( catalog.loadDirectory( directory, error ), error.c_str() );
    SW_EXPECT_TRUE( catalog.getManifestCount() > 0 );

    sw::ModuleResolveContext context{};
    context._platform      = sw::ModuleCatalog::getCurrentPlatform();
    context._configuration = sw::ModuleCatalog::getCurrentConfiguration();
    sw::ModuleResolution resolution;
    SW_ASSERT_TRUE_MSG( catalog.resolve( context, resolution, error ), error.c_str() );

    sw::string buildOrder;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( directory, "ResolvedModules.txt" ), buildOrder ) );
    sw::string runtimeOrder;
    for ( const sw::string& name : resolution._listLoadOrder )
        runtimeOrder += name + "\n";
    SW_EXPECT_STREQ( sw::StringUtil::replace( buildOrder, "\r\n", "\n" ).c_str(), runtimeOrder.c_str() );
}

/**
 * @brief [ModuleCatalogTest] 저장소의 모든 매니페스트(고르지 않은 게임의 것까지)가 읽힌다
 * @details 빌드는 활성 게임의 매니페스트만 읽으므로 다른 게임의 매니페스트가 틀려도 그 게임을 고르기 전까지 아무도 모른다.
 */
SW_TEST_CASE( ModuleCatalogTest, EveryRepositoryManifestParses )
{
    const sw::string       sourceDirectory = sw::FileUtil::joinPath( sw::ResourceUtil::getProjectFolderPath(), "Source" );
    sw::vector<sw::string> listFile;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( sourceDirectory, ".json", listFile, true ) );
    uint32 manifestCount = 0;
    for ( const sw::string& filePath : listFile )
    {
        if ( sw::StringUtil::endsWith( filePath, sw::ModuleCatalog::kManifestSuffix, true ) == false )
            continue;
        sw::string text;
        SW_ASSERT_TRUE( sw::FileUtil::readTextFile( filePath, text ) );
        sw::ModuleManifest manifest;
        sw::string         error;
        SW_EXPECT_TRUE_MSG( sw::ModuleCatalog::parseManifest( text, filePath, manifest, error ), error.c_str() );
        ++manifestCount;
    }
    SW_EXPECT_TRUE( manifestCount > 40 );
}
