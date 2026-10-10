#include "pch.h"

#include "Editor/Common/Commands/EditorModuleOverrides.h"

#include "Engine/Module/ModuleCatalog.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    ModuleManifest makeManifest( const utf8* pName, ModuleKind kind, std::initializer_list<const utf8*> listDependency )
    {
        ModuleManifest manifest{};
        manifest._name              = pName;
        manifest._kind              = kind;
        manifest._version           = ModuleVersion{ 1, 0, 0 };
        manifest._platformMask      = static_cast<uint8>( ModulePlatform::Windows ) | static_cast<uint8>( ModulePlatform::Linux );
        manifest._configurationMask = static_cast<uint8>( ModuleConfiguration::Dev );
        manifest._targetMask        = static_cast<uint8>( ModuleTarget::Client ) | static_cast<uint8>( ModuleTarget::Server );
        for ( const utf8* pDependency : listDependency )
        {
            manifest._listDependency.push_back( ModuleDependency{ pDependency, ModuleVersion{} } );
        }
        return manifest;
    }
} // namespace

/**
 * @brief [EditorModuleOverridesTest] 기본 켜짐 모듈을 끄면 줄 하나가 생기고, 다시 켜면 그 줄이 사라진다(덮어쓰기는 기본과 다를 때만)
 */
SW_TEST_CASE( EditorModuleOverridesTest, SetOverrideAddsAndRemovesLine )
{
    const utf8* pProject = R"({ "_name": "SWGame", "_version": "1.0.0", "_kind": "Game", "_listModuleOverride": [ { "_name": "GF_Other", "_bEnabled": false } ] })";
    string      off;
    SW_ASSERT_TRUE( EditorModuleOverrideUtil::setOverride( pProject, "GF_Farm", false, true, off ) );
    SW_EXPECT_TRUE( off.find( "GF_Farm" ) != string::npos );
    SW_EXPECT_TRUE( off.find( "GF_Other" ) != string::npos );
    string on;
    SW_ASSERT_TRUE( EditorModuleOverrideUtil::setOverride( off, "GF_Farm", true, true, on ) );
    SW_EXPECT_TRUE( on.find( "GF_Farm" ) == string::npos );
    SW_EXPECT_TRUE( on.find( "GF_Other" ) != string::npos );
    string broken;
    SW_EXPECT_FALSE( EditorModuleOverrideUtil::setOverride( "{ not json", "GF_Farm", false, true, broken ) );
}

/**
 * @brief [EditorModuleOverridesTest] 키트를 끄면 그 키트에 의존하는 에디터 확장도 새로 꺼지는 목록에 오르고, 이유는 의존이다
 */
SW_TEST_CASE( EditorModuleOverridesTest, PreviewIncludesDependents )
{
    ModuleCatalog catalog;
    string        error;
    SW_ASSERT_TRUE( catalog.addManifest( makeManifest( "SWGame", ModuleKind::Game, { "GameFramework" } ), error ) );
    SW_ASSERT_TRUE( catalog.addManifest( makeManifest( "GameFramework", ModuleKind::GameFramework, {} ), error ) );
    SW_ASSERT_TRUE( catalog.addManifest( makeManifest( "EditorModule", ModuleKind::Editor, {} ), error ) );
    SW_ASSERT_TRUE( catalog.addManifest( makeManifest( "GF_Farm", ModuleKind::Kit, { "GameFramework" } ), error ) );
    SW_ASSERT_TRUE( catalog.addManifest( makeManifest( "GF_Editor_Farm", ModuleKind::EditorExtension, { "EditorModule", "GF_Farm" } ), error ) );
    vector<ModuleInactiveEntry> listNewlyInactive;
    vector<string>              listNewlyActive;
    SW_ASSERT_TRUE_MSG( EditorModuleOverrideUtil::previewToggle( catalog, ModuleResolveContext{}, "GF_Farm", false, listNewlyInactive, listNewlyActive, error ),
                        error.c_str() );
    SW_ASSERT_EQUAL( size_t{ 2 }, listNewlyInactive.size() );
    SW_EXPECT_STREQ( "GF_Editor_Farm", listNewlyInactive[0]._name.c_str() );
    SW_EXPECT_TRUE( listNewlyInactive[0]._reason.find( "GF_Farm" ) != string::npos );
    SW_EXPECT_STREQ( "GF_Farm", listNewlyInactive[1]._name.c_str() );
    SW_EXPECT_TRUE( listNewlyActive.empty() );
}

/**
 * @brief [EditorModuleOverridesTest] 순환이 있는 카탈로그는 미리보기가 false 와 이유를 낸다
 */
SW_TEST_CASE( EditorModuleOverridesTest, PreviewReportsCycleError )
{
    ModuleCatalog catalog;
    string        error;
    SW_ASSERT_TRUE( catalog.addManifest( makeManifest( "SWGame", ModuleKind::Game, {} ), error ) );
    SW_ASSERT_TRUE( catalog.addManifest( makeManifest( "GF_A", ModuleKind::Kit, { "GF_B" } ), error ) );
    SW_ASSERT_TRUE( catalog.addManifest( makeManifest( "GF_B", ModuleKind::Kit, { "GF_A" } ), error ) );
    vector<ModuleInactiveEntry> listNewlyInactive;
    vector<string>              listNewlyActive;
    SW_EXPECT_FALSE( EditorModuleOverrideUtil::previewToggle( catalog, ModuleResolveContext{}, "GF_A", true, listNewlyInactive, listNewlyActive, error ) );
    SW_EXPECT_FALSE( error.empty() );
}
