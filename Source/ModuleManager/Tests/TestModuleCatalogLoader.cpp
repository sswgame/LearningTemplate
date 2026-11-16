// ModuleCatalogLoaderTest — 함께 지은 게임 팩만 닿는 키트를 빼는 규칙과, 실행 파일 옆 매니페스트의 실제 읽기 · 해석(nogpu).
// App 이 ModuleManager 를 통째로 링크해 이 등록이 남는다(`Source/App/CMakeLists.txt` 의 `sw_link_whole_archive`) — Server 는 이 스위트를 돌리지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "ModuleManager/ModuleCatalogLoader.h"

using namespace sw;

namespace
{
    struct ModuleCatalogLoaderTestUtil
    {
        static ModuleManifest makeManifest( const utf8* pName, ModuleKind kind, const vector<string>& listDependency = {} )
        {
            ModuleManifest manifest;
            manifest._name = pName;
            manifest._kind = kind;
            for ( const string& dependency : listDependency )
            {
                manifest._listDependency.push_back( ModuleDependency{ dependency, ModuleVersion{} } );
            }
            return manifest;
        }

        static void addOverride( ModuleManifest& inoutManifest, const utf8* pName, bool bEnabled )
        {
            ModuleOverride entry;
            entry._name     = pName;
            entry._bEnabled = bEnabled;
            inoutManifest._listModuleOverride.push_back( entry );
        }

        static bool isKept( const vector<ModuleManifest>& listManifest, string_view name )
        {
            for ( const ModuleManifest& manifest : listManifest )
            {
                if ( manifest._name == name )
                    return true;
            }
            return false;
        }

        static const ModuleInactiveEntry* findRemoved( const vector<ModuleInactiveEntry>& listRemoved, string_view name )
        {
            for ( const ModuleInactiveEntry& entry : listRemoved )
            {
                if ( entry._name == name )
                    return &entry;
            }
            return nullptr;
        }
    };
    using Util = ModuleCatalogLoaderTestUtil;
} // namespace

/**
 * @brief [ModuleCatalogLoaderTest] 함께 지은 게임 팩이 없으면 아무것도 빼지 않는다 - 게임이 닿지 않는 키트도 남는다
 */
SW_TEST_CASE( ModuleCatalogLoaderTest, NoGamePacksRemovesNothing )
{
    vector<ModuleManifest>      listManifest{ Util::makeManifest( "Project", ModuleKind::Game ), Util::makeManifest( "GF_Lonely", ModuleKind::Kit ) };
    vector<ModuleInactiveEntry> listRemoved;
    ModuleCatalogLoader::removeUnreachedGamePackKits( listManifest, {}, "Alpha", listRemoved );
    SW_EXPECT_EQUAL( size_t( 2 ), listManifest.size() );
    SW_EXPECT_TRUE( listRemoved.empty() );
}

/**
 * @brief [ModuleCatalogLoaderTest] 다른 게임 팩만 닿는 키트(의존으로 이어진 것까지)를 빼고 까닭을 적는다. 고른 게임이 의존 · 표로 켠 키트와 아무 게임도 닿지 않는 키트는 남고, 표가 끈 항목은 닿은 것이 아니다
 */
SW_TEST_CASE( ModuleCatalogLoaderTest, RemovesKitsOnlyOtherGamePacksReach )
{
    ModuleManifest project = Util::makeManifest( "Project", ModuleKind::Game, { "GF_ByDependency" } );
    Util::addOverride( project, "GF_ByTable", true );
    Util::addOverride( project, "GF_PackOnly", false );

    ModuleManifest otherGame = Util::makeManifest( "Project", ModuleKind::Game, { "GF_PackOnly", "GF_ByDependency" } );
    Util::addOverride( otherGame, "GF_ByTable", true );

    vector<ModuleManifest> listManifest{
        project,
        Util::makeManifest( "GF_ByDependency", ModuleKind::Kit ),
        Util::makeManifest( "GF_ByTable", ModuleKind::Kit ),
        Util::makeManifest( "GF_PackOnly", ModuleKind::Kit, { "GF_PackOnlyDeep" } ),
        Util::makeManifest( "GF_PackOnlyDeep", ModuleKind::Kit ),
        Util::makeManifest( "GF_Unused", ModuleKind::Kit ),
    };
    vector<ModuleInactiveEntry> listRemoved;
    ModuleCatalogLoader::removeUnreachedGamePackKits( listManifest, { otherGame }, "Alpha", listRemoved );

    SW_EXPECT_TRUE( Util::isKept( listManifest, "Project" ) );
    SW_EXPECT_TRUE( Util::isKept( listManifest, "GF_ByDependency" ) );
    SW_EXPECT_TRUE( Util::isKept( listManifest, "GF_ByTable" ) );
    SW_EXPECT_TRUE( Util::isKept( listManifest, "GF_Unused" ) );
    SW_EXPECT_FALSE( Util::isKept( listManifest, "GF_PackOnly" ) );
    SW_EXPECT_FALSE( Util::isKept( listManifest, "GF_PackOnlyDeep" ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listRemoved.size() );
    const ModuleInactiveEntry* pEntry = Util::findRemoved( listRemoved, "GF_PackOnly" );
    SW_REQUIRE_NOT_NULL( pEntry );
    SW_EXPECT_TRUE( pEntry->_reason == "not used by Alpha" );
}

/**
 * @brief [ModuleCatalogLoaderTest] 쓰는 쪽(고른 게임의 키트 · 키트 아닌 모듈)의 의존은 다른 팩이 닿아도 남는다 - 빼면 resolve 가 없는 의존으로 멈춘다
 */
SW_TEST_CASE( ModuleCatalogLoaderTest, KeepsDependenciesOfUsedModules )
{
    const ModuleManifest   otherGame = Util::makeManifest( "Project", ModuleKind::Game, { "GF_SharedDeep", "GF_EditorNeeds" } );
    vector<ModuleManifest> listManifest{
        Util::makeManifest( "Project", ModuleKind::Game, { "GF_Used" } ),
        Util::makeManifest( "GF_Used", ModuleKind::Kit, { "GF_SharedDeep" } ),
        Util::makeManifest( "GF_SharedDeep", ModuleKind::Kit ),
        Util::makeManifest( "Editor", ModuleKind::Editor, { "GF_EditorNeeds" } ),
        Util::makeManifest( "GF_EditorNeeds", ModuleKind::Kit ),
    };
    vector<ModuleInactiveEntry> listRemoved;
    ModuleCatalogLoader::removeUnreachedGamePackKits( listManifest, { otherGame }, "Alpha", listRemoved );
    SW_EXPECT_EQUAL( size_t( 5 ), listManifest.size() );
    SW_EXPECT_TRUE( listRemoved.empty() );
}

/**
 * @brief [ModuleCatalogLoaderTest] 뺀 키트에 기대는 키트와 에디터 확장도 사슬을 따라 빠진다. 팩 목록에 없어도 남기면 resolve 가 없는 의존으로 멈춘다
 */
SW_TEST_CASE( ModuleCatalogLoaderTest, RemovesDependentsOfRemovedKitsTransitively )
{
    const ModuleManifest   otherGame = Util::makeManifest( "Project", ModuleKind::Game, { "GF_PackOnly" } );
    vector<ModuleManifest> listManifest{
        Util::makeManifest( "Project", ModuleKind::Game ),
        Util::makeManifest( "GF_EditorPlugin_Far", ModuleKind::EditorExtension, { "GF_Leaning" } ), // 사슬 끝이 먼저 나와도 다시 훑어 잡는다
        Util::makeManifest( "GF_Leaning", ModuleKind::Kit, { "GF_PackOnly" } ),
        Util::makeManifest( "GF_PackOnly", ModuleKind::Kit ),
        Util::makeManifest( "GF_EditorPlugin_Free", ModuleKind::EditorExtension ),
    };
    vector<ModuleInactiveEntry> listRemoved;
    ModuleCatalogLoader::removeUnreachedGamePackKits( listManifest, { otherGame }, "Alpha", listRemoved );
    SW_EXPECT_EQUAL( size_t( 3 ), listRemoved.size() );
    SW_EXPECT_NOT_NULL( Util::findRemoved( listRemoved, "GF_EditorPlugin_Far" ) );
    SW_EXPECT_NOT_NULL( Util::findRemoved( listRemoved, "GF_Leaning" ) );
    SW_EXPECT_NOT_NULL( Util::findRemoved( listRemoved, "GF_PackOnly" ) );
    SW_EXPECT_TRUE( Util::isKept( listManifest, "GF_EditorPlugin_Free" ) );
    SW_EXPECT_TRUE( Util::isKept( listManifest, "Project" ) );
}

/**
 * @brief [ModuleCatalogLoaderTest] 이 실행 파일 옆 `Modules/` 를 실제로 읽어 해석한다 - 켜진 모듈은 모두 카탈로그에 있고, 꺼진 목록은 이름 순이다
 * @details 틀린 매니페스트 · 폴더 없음의 오류 길은 이 경로가 실행 파일 위치로 고정이라 여기서 보지 않는다(해석 규칙은 `ModuleCatalogTest`).
 */
SW_TEST_CASE( ModuleCatalogLoaderTest, LoadsManifestsBesideTheExecutable )
{
    ModuleCatalog    catalog;
    ModuleResolution resolution;
    SW_REQUIRE_TRUE( ModuleCatalogLoader::loadAndResolve( static_cast<uint8>( ModuleTarget::Client ), catalog, resolution ) );
    SW_EXPECT_TRUE( catalog.getManifestCount() > 0 );
    SW_EXPECT_FALSE( resolution._listLoadOrder.empty() );
    for ( const string& name : resolution._listLoadOrder )
    {
        SW_EXPECT_NOT_NULL( catalog.findManifest( name ) );
    }
    for ( size_t index = 1; index < resolution._listInactive.size(); ++index )
    {
        SW_EXPECT_FALSE( resolution._listInactive[index]._name < resolution._listInactive[index - 1]._name );
    }
}
