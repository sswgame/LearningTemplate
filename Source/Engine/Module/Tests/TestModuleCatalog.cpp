#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Module/ModuleCatalog.h"

using namespace sw;

namespace
{
    struct ModuleCatalogTestUtil
    {
        /** @brief 어디서나 켜지는 모듈 하나의 매니페스트입니다. */
        static ModuleManifest makeManifest( const utf8* pName, ModuleKind kind = ModuleKind::Kit, const vector<string>& listDependency = {} )
        {
            ModuleManifest manifest;
            manifest._name              = pName;
            manifest._kind              = kind;
            manifest._platformMask      = static_cast<uint8>( ModulePlatform::Windows ) | static_cast<uint8>( ModulePlatform::Linux );
            manifest._configurationMask = static_cast<uint8>( ModuleConfiguration::Dev ) | static_cast<uint8>( ModuleConfiguration::Shipping );
            manifest._targetMask        = static_cast<uint8>( ModuleTarget::Client ) | static_cast<uint8>( ModuleTarget::Server );
            for ( const string& dependency : listDependency )
            {
                manifest._listDependency.push_back( ModuleDependency{ dependency, ModuleVersion{} } );
            }
            return manifest;
        }

        static void add( ModuleCatalog& catalog, ModuleManifest manifest )
        {
            string error;
            SW_REQUIRE_TRUE( catalog.addManifest( std::move( manifest ), error ) );
        }

        static ModuleResolveContext makeContext( const utf8* pProject = "Game" )
        {
            ModuleResolveContext context;
            context._projectModule = pProject;
            context._targetMask    = static_cast<uint8>( ModuleTarget::Client );
            return context;
        }

        static size_t indexOf( const ModuleResolution& resolution, string_view name )
        {
            for ( size_t index = 0; index < resolution._listLoadOrder.size(); ++index )
            {
                if ( string_view( resolution._listLoadOrder[index] ) == name )
                    return index;
            }
            return resolution._listLoadOrder.size();
        }

        static string findReason( const ModuleResolution& resolution, string_view name )
        {
            for ( const ModuleInactiveEntry& entry : resolution._listInactive )
            {
                if ( string_view( entry._name ) == name )
                    return entry._reason;
            }
            return {};
        }

        static void expectParseError( string_view json, const utf8* pExpected )
        {
            ModuleManifest manifest;
            string         error;
            SW_EXPECT_FALSE( ModuleCatalog::parseManifest( json, {}, manifest, error ) );
            SW_EXPECT_TRUE_MSG( error.find( pExpected ) != string::npos, error.c_str() );
        }

        static constexpr const utf8* kValid = R"({
            "_name": "GF_Kit", "_version": "1.2.3", "_kind": "Kit", "_description": "desc",
            "_listDependency": [ { "_name": "GameFramework", "_minVersion": "1.0.0" } ],
            "_listPlatform": [ "Windows", "Linux" ], "_listConfiguration": [ "Dev" ], "_listTarget": [ "Client", "Server" ],
            "_bEnabledByDefault": false })";
    };
    using Util = ModuleCatalogTestUtil;
} // namespace

// ---------------------------------------------------------------------- 판

SW_TEST_CASE( ModuleVersionTest, ParsesOnlyThreeNumbers )
{
    ModuleVersion version;
    SW_REQUIRE_TRUE( ModuleVersion::parse( "1.20.3", version ) );
    SW_EXPECT_EQUAL( 20u, version._minor );
    SW_EXPECT_TRUE( version.toString() == "1.20.3" );
    SW_EXPECT_FALSE( ModuleVersion::parse( "", version ) );
    SW_EXPECT_FALSE( ModuleVersion::parse( "1.2", version ) );
    SW_EXPECT_FALSE( ModuleVersion::parse( "1.2.3.4", version ) );
    SW_EXPECT_FALSE( ModuleVersion::parse( "1..3", version ) );
    SW_EXPECT_FALSE( ModuleVersion::parse( "1.2.", version ) );
    SW_EXPECT_FALSE( ModuleVersion::parse( "a.b.c", version ) );
    SW_EXPECT_FALSE( ModuleVersion::parse( "99999999.0.0", version ) );
}

SW_TEST_CASE( ModuleVersionTest, ComparesMajorThenMinorThenPatch )
{
    const ModuleVersion version{ 2, 3, 4 };
    SW_EXPECT_TRUE( version.isAtLeast( ModuleVersion{ 2, 3, 4 } ) );
    SW_EXPECT_TRUE( version.isAtLeast( ModuleVersion{ 1, 9, 9 } ) );
    SW_EXPECT_TRUE( version.isAtLeast( ModuleVersion{ 2, 2, 9 } ) );
    SW_EXPECT_FALSE( version.isAtLeast( ModuleVersion{ 2, 3, 5 } ) );
    SW_EXPECT_FALSE( version.isAtLeast( ModuleVersion{ 3, 0, 0 } ) );
}

// ---------------------------------------------------------------------- 매니페스트 읽기

SW_TEST_CASE( ModuleCatalogTest, ParsesAManifest )
{
    ModuleManifest manifest;
    string         error;
    SW_REQUIRE_TRUE( ModuleCatalog::parseManifest( Util::kValid, {}, manifest, error ) );
    SW_EXPECT_TRUE( manifest._name == "GF_Kit" );
    SW_EXPECT_TRUE( manifest._kind == ModuleKind::Kit );
    SW_EXPECT_EQUAL( 2u, manifest._version._minor );
    SW_EXPECT_FALSE( manifest._bEnabledByDefault );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( manifest._platformMask ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( manifest._configurationMask ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, manifest._listDependency.size() );
    SW_EXPECT_TRUE( manifest._listDependency[0]._name == "GameFramework" );
}

SW_TEST_CASE( ModuleCatalogTest, FileNameMustMatchTheModuleName )
{
    ModuleManifest manifest;
    string         error;
    SW_EXPECT_TRUE( ModuleCatalog::parseManifest( Util::kValid, "D:/x/GF_Kit.module.json", manifest, error ) );
    SW_EXPECT_FALSE( ModuleCatalog::parseManifest( Util::kValid, "D:/x/Other.module.json", manifest, error ) );
    SW_EXPECT_TRUE( error.find( "must be named" ) != string::npos );
}

SW_TEST_CASE( ModuleCatalogTest, BrokenManifestsNameTheProblem )
{
    SW_EXPECT_LOG( "JSON.ParseError" );
    Util::expectParseError( "not json", "not valid JSON" );
    Util::expectParseError( R"({ "_version": "1.0.0" })", "_name is missing" );
    Util::expectParseError( R"({ "_name": "M", "_version": "x" })", "_version must be" );
    Util::expectParseError( R"({ "_name": "M", "_version": "1.0.0" })", "_kind is missing" );
    Util::expectParseError( R"({ "_name": "M", "_version": "1.0.0", "_kind": "Kit" })", "_listPlatform" );
    Util::expectParseError( R"({ "_name": "M", "_version": "1.0.0", "_kind": "Kit", "_listPlatform": ["Linux"] })", "_listConfiguration" );
    Util::expectParseError( R"({ "_name": "M", "_version": "1.0.0", "_kind": "Kit", "_listPlatform": ["Linux"], "_listConfiguration": ["Dev"] })", "_listTarget" );
    // 오타는 조용히 지나가지 않는다.
    Util::expectParseError( R"({ "_name": "M", "_versoin": "1.0.0" })", "_versoin" );

    const string common = R"("_version": "1.0.0", "_listPlatform": ["Linux"], "_listConfiguration": ["Dev"], "_listTarget": ["Client"])";
    Util::expectParseError( R"({ "_name": "M", "_kind": "Kit", "_listDependency": [ { "_name": "M" } ], )" + common + " }", "names the module itself" );
    Util::expectParseError( R"({ "_name": "M", "_kind": "Kit", "_listDependency": [ { "_name": "" } ], )" + common + " }", "has no name" );
    Util::expectParseError( R"({ "_name": "M", "_kind": "Kit", "_listDependency": [ { "_name": "D", "_minVersion": "x" } ], )" + common + " }", "bad _minVersion" );
    Util::expectParseError( R"({ "_name": "M", "_kind": "Kit", "_listModuleOverride": [ { "_name": "D" } ], )" + common + " }", "only a Game module" );
    Util::expectParseError( R"({ "_name": "M", "_kind": "Game", "_listModuleOverride": [ { "_name": "" } ], )" + common + " }", "module override" );
}

// 에디터 확장은 Dev 전용이고 에디터에 의존한다(CMake 검사와 같다).
SW_TEST_CASE( ModuleCatalogTest, EditorExtensionsMustBeDevAndNeedTheEditor )
{
    Util::expectParseError( R"({ "_name": "E", "_version": "1.0.0", "_kind": "EditorExtension", "_listPlatform": ["Linux"], "_listConfiguration": ["Dev", "Shipping"], "_listTarget": ["Client"] })", "Dev only" );
    Util::expectParseError( R"({ "_name": "E", "_version": "1.0.0", "_kind": "EditorExtension", "_listPlatform": ["Linux"], "_listConfiguration": ["Dev"], "_listTarget": ["Client"] })", "must list" );
}

SW_TEST_CASE( ModuleCatalogTest, DuplicateNamesAreRefused )
{
    ModuleCatalog catalog;
    Util::add( catalog, Util::makeManifest( "A" ) );
    string error;
    SW_EXPECT_FALSE( catalog.addManifest( Util::makeManifest( "A" ), error ) );
    SW_EXPECT_TRUE( error.find( "declared twice" ) != string::npos );
    SW_EXPECT_EQUAL( size_t{ 1 }, catalog.getManifestCount() );
    SW_EXPECT_TRUE( catalog.findManifest( "A" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findManifest( "B" ) == nullptr );
}

// ---------------------------------------------------------------------- 해석

SW_TEST_CASE( ModuleCatalogTest, LoadOrderPutsDependenciesFirst )
{
    ModuleCatalog catalog;
    Util::add( catalog, Util::makeManifest( "Game", ModuleKind::Game, { "KitB" } ) );
    Util::add( catalog, Util::makeManifest( "KitB", ModuleKind::Kit, { "KitA" } ) );
    Util::add( catalog, Util::makeManifest( "KitA" ) );
    ModuleResolution resolution;
    string           error;
    SW_REQUIRE_TRUE( catalog.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_TRUE( Util::indexOf( resolution, "KitA" ) < Util::indexOf( resolution, "KitB" ) );
    SW_EXPECT_TRUE( Util::indexOf( resolution, "KitB" ) < Util::indexOf( resolution, "Game" ) );
    SW_EXPECT_TRUE( resolution.isActive( "Game" ) );
    SW_EXPECT_FALSE( resolution.isActive( "Nope" ) );
}

SW_TEST_CASE( ModuleCatalogTest, CycleIsReportedWithItsPath )
{
    ModuleCatalog catalog;
    Util::add( catalog, Util::makeManifest( "A", ModuleKind::Kit, { "B" } ) );
    Util::add( catalog, Util::makeManifest( "B", ModuleKind::Kit, { "A" } ) );
    ModuleResolution resolution;
    string           error;
    SW_EXPECT_FALSE( catalog.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_TRUE( error.find( "cycle" ) != string::npos );
    SW_EXPECT_TRUE( error.find( "A" ) != string::npos && error.find( "B" ) != string::npos );
}

SW_TEST_CASE( ModuleCatalogTest, DependencyProblemsAreErrors )
{
    string           error;
    ModuleResolution resolution;

    ModuleCatalog missing;
    Util::add( missing, Util::makeManifest( "A", ModuleKind::Kit, { "Ghost" } ) );
    SW_EXPECT_FALSE( missing.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_TRUE( error.find( "missing module" ) != string::npos );

    ModuleCatalog  tooOld;
    ModuleManifest user = Util::makeManifest( "A" );
    user._listDependency.push_back( ModuleDependency{
        "B", ModuleVersion{ 2, 0, 0 }
    } );
    Util::add( tooOld, std::move( user ) );
    ModuleManifest old = Util::makeManifest( "B" );
    old._version       = ModuleVersion{ 1, 0, 0 };
    Util::add( tooOld, std::move( old ) );
    SW_EXPECT_FALSE( tooOld.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_TRUE( error.find( "2.0.0 or later" ) != string::npos );

    ModuleCatalog disabled;
    Util::add( disabled, Util::makeManifest( "A", ModuleKind::Kit, { "B" } ) );
    ModuleManifest off     = Util::makeManifest( "B" );
    off._bEnabledByDefault = false;
    Util::add( disabled, std::move( off ) );
    SW_EXPECT_FALSE( disabled.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_TRUE( error.find( "disabled by default" ) != string::npos );
}

SW_TEST_CASE( ModuleCatalogTest, UnavailableModulesAreInactiveWithAReason )
{
    ModuleCatalog  catalog;
    ModuleManifest linuxOnly = Util::makeManifest( "LinuxOnly" );
    linuxOnly._platformMask  = static_cast<uint8>( ModulePlatform::Linux );
    Util::add( catalog, std::move( linuxOnly ) );
    ModuleManifest shipOnly     = Util::makeManifest( "ShipOnly" );
    shipOnly._configurationMask = static_cast<uint8>( ModuleConfiguration::Shipping );
    Util::add( catalog, std::move( shipOnly ) );
    ModuleManifest serverOnly = Util::makeManifest( "ServerOnly" );
    serverOnly._targetMask    = static_cast<uint8>( ModuleTarget::Server );
    Util::add( catalog, std::move( serverOnly ) );
    ModuleManifest defaultOff     = Util::makeManifest( "DefaultOff" );
    defaultOff._bEnabledByDefault = false;
    Util::add( catalog, std::move( defaultOff ) );

    ModuleResolution resolution;
    string           error;
    SW_REQUIRE_TRUE( catalog.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_TRUE( resolution._listLoadOrder.empty() );
    SW_REQUIRE_EQUAL( size_t{ 4 }, resolution._listInactive.size() );
    SW_EXPECT_TRUE( Util::findReason( resolution, "LinuxOnly" ) == "not available on Windows" );
    SW_EXPECT_TRUE( Util::findReason( resolution, "ShipOnly" ) == "not built for Dev" );
    SW_EXPECT_TRUE( Util::findReason( resolution, "ServerOnly" ) == "not built for the Client target" );
    SW_EXPECT_TRUE( Util::findReason( resolution, "DefaultOff" ) == "disabled by default" );
    SW_EXPECT_TRUE( resolution._listInactive[0]._name == "DefaultOff" ); // 이름 순이다
}

SW_TEST_CASE( ModuleCatalogTest, ProjectOverridesWinOverDefaults )
{
    ModuleCatalog catalog;
    Util::add( catalog, Util::makeManifest( "Game", ModuleKind::Game ) );
    Util::add( catalog, Util::makeManifest( "On" ) );
    ModuleManifest off     = Util::makeManifest( "Off" );
    off._bEnabledByDefault = false;
    Util::add( catalog, std::move( off ) );
    SW_EXPECT_TRUE( catalog.setProjectOverride( "Game", "On", false ) );
    SW_EXPECT_TRUE( catalog.setProjectOverride( "Game", "Off", true ) );
    SW_EXPECT_TRUE( catalog.setProjectOverride( "Game", "Off", true ) ); // 고쳐 쓴다(줄이 늘지 않는다)
    SW_EXPECT_EQUAL( size_t{ 2 }, catalog.findManifest( "Game" )->_listModuleOverride.size() );
    SW_EXPECT_FALSE( catalog.setProjectOverride( "NoProject", "On", false ) );

    ModuleResolution resolution;
    string           error;
    SW_REQUIRE_TRUE( catalog.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_FALSE( resolution.isActive( "On" ) );
    SW_EXPECT_TRUE( Util::findReason( resolution, "On" ) == "disabled by the project" );
    SW_EXPECT_TRUE( resolution.isActive( "Off" ) );
}

// 꺼 두려던 모듈 이름을 틀리면 켜진 채 남는다 — 오류로 알린다.
SW_TEST_CASE( ModuleCatalogTest, OverrideOfAnUnknownModuleIsAnError )
{
    ModuleCatalog catalog;
    Util::add( catalog, Util::makeManifest( "Game", ModuleKind::Game ) );
    SW_REQUIRE_TRUE( catalog.setProjectOverride( "Game", "Typo", false ) );
    ModuleResolution resolution;
    string           error;
    SW_EXPECT_FALSE( catalog.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_TRUE( error.find( "unknown module 'Typo'" ) != string::npos );
}

// 프로젝트는 기본값으로 꺼져 있어도 켜져 있다.
SW_TEST_CASE( ModuleCatalogTest, TheProjectIsAlwaysOn )
{
    ModuleCatalog  catalog;
    ModuleManifest game     = Util::makeManifest( "Game", ModuleKind::Game );
    game._bEnabledByDefault = false;
    Util::add( catalog, std::move( game ) );
    ModuleResolution resolution;
    string           error;
    SW_REQUIRE_TRUE( catalog.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_TRUE( resolution.isActive( "Game" ) );
}

// 에디터 확장은 의존이 꺼지면 함께 꺼진다. 오류가 아니다(그 확장만 없다).
SW_TEST_CASE( ModuleCatalogTest, EditorExtensionFollowsItsDependency )
{
    ModuleCatalog  catalog;
    ModuleManifest kit     = Util::makeManifest( "KitOff" );
    kit._bEnabledByDefault = false;
    Util::add( catalog, std::move( kit ) );
    Util::add( catalog, Util::makeManifest( "Ext", ModuleKind::EditorExtension, { "KitOff" } ) );
    ModuleResolution resolution;
    string           error;
    SW_REQUIRE_TRUE( catalog.resolve( Util::makeContext(), resolution, error ) );
    SW_EXPECT_FALSE( resolution.isActive( "Ext" ) );
    SW_EXPECT_TRUE( Util::findReason( resolution, "Ext" ).find( "needs KitOff" ) != string::npos );
}

// 확장의 의존이 확장이면 목록 순서와 상관없이 함께 꺼져야 한다 — 뒤에 있는 확장이 꺼진 줄 모르면 해석 전체가 오류로 멈춘다.
SW_TEST_CASE( ModuleCatalogTest, EditorExtensionChainFollowsWhateverTheOrder )
{
    ModuleCatalog  catalog;
    ModuleManifest kit     = Util::makeManifest( "KitOff" );
    kit._bEnabledByDefault = false;
    Util::add( catalog, Util::makeManifest( "ExtFirst", ModuleKind::EditorExtension, { "ExtSecond" } ) );
    Util::add( catalog, Util::makeManifest( "ExtSecond", ModuleKind::EditorExtension, { "KitOff" } ) );
    Util::add( catalog, std::move( kit ) );
    ModuleResolution resolution;
    string           error;
    SW_REQUIRE_TRUE_MSG( catalog.resolve( Util::makeContext(), resolution, error ), error.c_str() );
    SW_EXPECT_FALSE( resolution.isActive( "ExtFirst" ) );
    SW_EXPECT_FALSE( resolution.isActive( "ExtSecond" ) );
}

SW_TEST_CASE( ModuleCatalogTest, MissingFolderIsAnError )
{
    ModuleCatalog catalog;
    string        error;
    SW_EXPECT_FALSE( catalog.loadDirectory( test::makeScratchPath( "SwNoSuchModuleFolder" ), error ) );
    SW_EXPECT_TRUE( error.find( "does not exist" ) != string::npos );
}
