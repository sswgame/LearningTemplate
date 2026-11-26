// AssetReferenceFields — 경로 칸 목록으로 뽑기 · 있음 판정 · 고쳐 쓰기. 저장소의 칸 목록(Config/Dev/AssetReferenceFields.json)을 그대로 읽는다.
// 같은 규칙의 파이썬 쪽(Scripts/lint/asset_references.py)이 같은 글에서 같은 경로를 뽑아야 한다 — 둘을 고치면 이 글로 함께 본다.
#include "pch.h"

#include "Core/File/ProcessRoots.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Content/AssetRegistry.h"

using namespace sw;

namespace
{
    /** @brief 저장소의 칸 목록을 읽습니다. */
    bool loadRepositoryProperties( AssetReferenceFields& outParams )
    {
        return outParams.load( ProcessRoots::get()._project );
    }

    vector<string> extractPaths( const AssetReferenceFields& params, string_view text, AssetReferenceSyntax syntax )
    {
        vector<AssetReference> listReference;
        params.extract( text, syntax, listReference );
        vector<string> listPath;
        for ( const AssetReference& reference : listReference )
        {
            listPath.push_back( reference._path );
        }
        return listPath;
    }
} // namespace

/** @brief [AssetReferenceFieldsTest] 카탈로그 항목의 `_base` 는 `#` 앞의 파일만 참조이고, 같은 파일의 항목 id 는 경로가 아닌지 검증 */
SW_TEST_CASE( AssetReferenceFieldsTest, CatalogEntryBaseReferencesTheFileOnly )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryProperties( params ) );
    const vector<string> listPath = extractPaths( params, R"(<Root><Item _base="common/data/items.xml#helmet_knight"/><Item _base="helmet_knight"/></Root>)", AssetReferenceSyntax::XML );
    SW_REQUIRE_EQUAL( listPath.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_EQUAL( listPath[0], string( "common/data/items.xml" ) );
}

/**
 * @brief [AssetReferenceFieldsTest] XML 은 목록에 든 속성 · 요소 글 · 목록 원소만 경로이고, 주석 · 목록 밖 칸 · 도메인 없는 값 · 틀(`{…}`)은 아닌지 검증
 */
SW_TEST_CASE( AssetReferenceFieldsTest, ExtractsOnlyListedPropertiesFromXML )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryProperties( params ) );
    constexpr string_view kText    = "<Root>\n"
                                     "  <!-- <entity prefab=\"game/x/commented.prefab.xml\"/> -->\n"
                                     "  <entity prefab=\"Game/X/A.prefab.xml\" name=\"game/x/notapath.xml\"/>\n"
                                     "  <MeshComponent _meshID=\"Plane\" _materialPath=\"{bind:_material}\"/>\n"
                                     "  <_listDefaultGrowingModel>\n"
                                     "    <item>game/x/models/stage_a.mesh</item>\n"
                                     "  </_listDefaultGrowingModel>\n"
                                     "  <startMap>game/x/maps/start.scene.xml</startMap>\n"
                                     "</Root>\n";
    const vector<string>  listPath = extractPaths( params, kText, AssetReferenceSyntax::XML );
    SW_REQUIRE_EQUAL( listPath.size(), static_cast<size_t>( 3 ) );
    SW_EXPECT_EQUAL( listPath[0], string( "game/x/a.prefab.xml" ) ); // 소문자로 맞춘다
    SW_EXPECT_EQUAL( listPath[1], string( "game/x/models/stage_a.mesh" ) );
    SW_EXPECT_EQUAL( listPath[2], string( "game/x/maps/start.scene.xml" ) );

    vector<AssetReference> listReference;
    params.extract( kText, AssetReferenceSyntax::XML, listReference );
    SW_EXPECT_EQUAL( listReference[0]._line, 3u );
    SW_EXPECT_EQUAL( listReference[0]._field, string( "prefab" ) );
}

/**
 * @brief [AssetReferenceFieldsTest] JSON 키 값 · 글 배열과 CMake 인자 값(쉼표로 여럿, `${…}` 는 틀)을 뽑는지 검증
 */
SW_TEST_CASE( AssetReferenceFieldsTest, ExtractsJSONAndCMake )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryProperties( params ) );
    const vector<string> listJSON = extractPaths( params, R"({ "_materialPath": "engine/materials/a.material", "_listStyleSheet": [ "engine/ui/a.uistyle.xml", "x" ], "other": "engine/b" })",
                                                  AssetReferenceSyntax::JSON );
    SW_REQUIRE_EQUAL( listJSON.size(), static_cast<size_t>( 2 ) );
    SW_EXPECT_EQUAL( listJSON[0], string( "engine/materials/a.material" ) );
    SW_EXPECT_EQUAL( listJSON[1], string( "engine/ui/a.uistyle.xml" ) );

    const vector<string> listCMake =
        extractPaths( params, "COMMAND App --render-portraits=common/a.prefab.xml,common/b.prefab.xml -scenario=${swScenario}\n", AssetReferenceSyntax::CMake );
    SW_REQUIRE_EQUAL( listCMake.size(), static_cast<size_t>( 2 ) );
    SW_EXPECT_EQUAL( listCMake[1], string( "common/b.prefab.xml" ) );
}

/**
 * @brief [AssetReferenceFieldsTest] 임포트 산출물(`models/…`)은 같은 자리의 원본(`models_raw/<조상>.glb`)이 있으면 있고, 실행마다의 사본(`scratch/`)은 검사하지 않는지 검증
 */
SW_TEST_CASE( AssetReferenceFieldsTest, ImportedOutputsResolveFromTheirSource )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryProperties( params ) );
    AssetIDSet assets;
    assets.addFile( "common/models_raw/kaykit/knight.glb" );
    assets.addFile( "game/x/textures_raw/wall.png" );
    SW_EXPECT_TRUE( params.canResolve( "common/models/kaykit/knight.mesh", assets ) );
    SW_EXPECT_TRUE( params.canResolve( "common/models/kaykit/knight/clips", assets ) ); // 옆 폴더
    SW_EXPECT_TRUE( params.canResolve( "common/models/kaykit/knight/knight.skeleton.xml", assets ) );
    SW_EXPECT_TRUE( params.canResolve( "game/x/textures/wall.dds", assets ) );
    SW_EXPECT_TRUE( params.canResolve( "common/models_raw", assets ) ); // 폴더
    SW_EXPECT_FALSE( params.canResolve( "common/models/kaykit/rogue.mesh", assets ) );
    SW_EXPECT_FALSE( params.canResolve( "common/models/kaykit/knight.facial.xml", assets ) ); // glb 에서 나오지 않는 종류(손으로 적은 글)
    assets.addFile( "game/x/models_raw/avatar.vrm" );
    SW_EXPECT_TRUE( params.canResolve( "game/x/models/avatar/avatar.facial.xml", assets ) ); // vrm 에서는 나온다
    SW_EXPECT_FALSE( params.canResolve( "game/x/textures/floor.dds", assets ) );
    SW_EXPECT_FALSE( params.isChecked( "scratch/assetmanage/probe.material" ) );
    SW_EXPECT_FALSE( params.isReferrer( "common/models/kaykit/knight/materials/a.material" ) ); // 임포터가 쓴 산출물
    SW_EXPECT_TRUE( params.isReferrer( "game/x/maps/a.scene.xml" ) );
    SW_EXPECT_FALSE( params.isReferrer( "game/x/redirects.xml" ) ); // 리다이렉트 표는 옛 id 를 적는다 — 참조가 아니다
}

/**
 * @brief [AssetReferenceFieldsTest] 고쳐 쓰기가 경로 칸의 값만 바꾸고(칸 밖 · 주석은 그대로), 원본을 옮기면 산출물 경로도 바꾸는지 검증
 */
SW_TEST_CASE( AssetReferenceFieldsTest, RewritesOnlyPathProperties )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryProperties( params ) );
    string text = "<!-- game/x/a.material -->\n"
                  "<M _materialPath=\"game/x/a.material\" _meshID=\"game/x/models/rock.mesh\" note=\"game/x/a.material\"/>\n";
    SW_EXPECT_EQUAL( params.rewrite( text, AssetReferenceSyntax::XML, params.makeRewriteRules( "game/x/a.material", "game/y/b.material", false ) ), 1u );
    SW_EXPECT_TRUE( text.find( "_materialPath=\"game/y/b.material\"" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "<!-- game/x/a.material -->" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "note=\"game/x/a.material\"" ) != string::npos );

    SW_EXPECT_EQUAL( params.rewrite( text, AssetReferenceSyntax::XML, params.makeRewriteRules( "game/x/models_raw/rock.glb", "game/x/models_raw/stone.glb", false ) ), 1u );
    SW_EXPECT_TRUE( text.find( "_meshID=\"game/x/models/stone.mesh\"" ) != string::npos );
}

/**
 * @brief [AssetReferenceFieldsTest] 규칙이 여럿이면 앞 규칙이 이기고, 접두 규칙은 `/` · `.` 경계에서만 맞는지 검증(폴더 옮기기 — 규칙을 해시로 찾는다)
 */
SW_TEST_CASE( AssetReferenceFieldsTest, RewriteTakesTheFirstMatchingRuleAtBoundaries )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryProperties( params ) );
    const vector<AssetReferenceRewriteRule> listRule{
        AssetReferenceRewriteRule{"game/x/mats/a.material", "game/y/first.material", false},
        AssetReferenceRewriteRule{           "game/x/mats",           "game/z/mats",  true},
        AssetReferenceRewriteRule{"game/x/mats/b.material",  "game/y/late.material", false},
        AssetReferenceRewriteRule{    "game/x/models/rock",   "game/x/models/stone",  true},
    };
    string text = "<M _materialPath=\"game/x/mats/a.material\"/><M _materialPath=\"game/x/mats/b.material\"/><M _materialPath=\"game/x/matsother/c.material\"/>\n"
                  "<M _meshID=\"game/x/models/rock.mesh\"/><M _meshID=\"game/x/models/rocky.mesh\"/>\n";
    SW_EXPECT_EQUAL( params.rewrite( text, AssetReferenceSyntax::XML, listRule ), 3u );
    SW_EXPECT_TRUE( text.find( "game/y/first.material" ) != string::npos );  // 앞 규칙(같은 글)이 폴더 규칙보다 먼저다
    SW_EXPECT_TRUE( text.find( "game/z/mats/b.material" ) != string::npos ); // 폴더 규칙이 뒤의 같은 글 규칙보다 먼저다
    SW_EXPECT_TRUE( text.find( "game/x/matsother/c.material" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "game/x/models/stone.mesh" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "game/x/models/rocky.mesh" ) != string::npos );
}

/**
 * @brief [AssetReferenceFieldsTest] GUID 참조(`guid:<32자>`)는 런타임 색인으로 지금 경로를 풀어 참조가 되고(쿠킹 따라가기 · 참조 색인 · 검사가 본다),
 *        색인에 없는 GUID 는 글 그대로 참조라 "없는 경로" 로 잡힌다. 옮기기의 고쳐 쓰기는 GUID 참조를 건드리지 않는다(경로가 바뀌어도 GUID 는 그대로다)
 */
SW_TEST_CASE( AssetReferenceFieldsTest, GUIDReferencesResolveThroughTheIndexAndAreNotRewritten )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryProperties( params ) );
    const AssetGUID            guid = *AssetGUID::tryParse( "0000000000000000000000000000f1e1" );
    vector<AssetRegistryLayer> listLayer;
    listLayer.push_back( AssetRegistryLayer{ "test", { AssetRegistryEntry{ guid, "game/x/A.material" } } } );
    AssetRegistry previous = AssetRegistry::install( std::move( *AssetRegistry::merge( listLayer ) ) );

    string               text     = "<M _materialPath=\"guid:0000000000000000000000000000f1e1\" _meshID=\"guid:0000000000000000000000000000dead\"/>\n";
    const vector<string> listPath = extractPaths( params, text, AssetReferenceSyntax::XML );
    SW_REQUIRE_EQUAL( listPath.size(), static_cast<size_t>( 2 ) );
    SW_EXPECT_EQUAL( listPath[0], string( "game/x/a.material" ) );
    SW_EXPECT_EQUAL( listPath[1], string( "guid:0000000000000000000000000000dead" ) );
    AssetIDSet assets;
    assets.addFile( "game/x/a.material" );
    SW_EXPECT_TRUE( params.canResolve( listPath[0], assets ) );
    SW_EXPECT_FALSE( params.canResolve( listPath[1], assets ) );

    SW_EXPECT_EQUAL( params.rewrite( text, AssetReferenceSyntax::XML, params.makeRewriteRules( "game/x/a.material", "game/y/b.material", false ) ), 0u );
    SW_EXPECT_TRUE( text.find( "guid:0000000000000000000000000000f1e1" ) != string::npos );

    (void)AssetRegistry::install( std::move( previous ) );
}
