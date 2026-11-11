// AssetReferenceFields — 경로 칸 목록으로 뽑기 · 있음 판정 · 고쳐 쓰기. 저장소의 칸 목록(Config/Dev/AssetReferenceFields.json)을 그대로 읽는다.
// 같은 규칙의 파이썬 쪽(Scripts/lint/AssetReferences.py)이 같은 글에서 같은 경로를 뽑아야 한다 — 둘을 고치면 이 글로 함께 본다.
#include "pch.h"

#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Reference/AssetReferenceFields.h"

using namespace sw;

namespace
{
    /** @brief 저장소의 칸 목록을 읽습니다. */
    bool loadRepositoryFields( AssetReferenceFields& outParams )
    {
        return outParams.load( ProcessRoots::get()._project.str() );
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
    SW_REQUIRE_TRUE( loadRepositoryFields( params ) );
    const vector<string> listPath = extractPaths( params, R"(<Root><Item _base="common/data/items.xml#helmet_knight"/><Item _base="helmet_knight"/></Root>)", AssetReferenceSyntax::XML );
    SW_REQUIRE_EQUAL( listPath.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_EQUAL( listPath[0], string( "common/data/items.xml" ) );
}

/**
 * @brief [AssetReferenceFieldsTest] XML 은 목록에 든 속성 · 요소 글 · 목록 원소만 경로이고, 주석 · 목록 밖 칸 · 도메인 없는 값 · 틀(`{…}`)은 아닌지 검증
 */
SW_TEST_CASE( AssetReferenceFieldsTest, ExtractsOnlyListedFieldsFromXML )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryFields( params ) );
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
    SW_REQUIRE_TRUE( loadRepositoryFields( params ) );
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
    SW_REQUIRE_TRUE( loadRepositoryFields( params ) );
    AssetIDSet assets;
    assets.addFile( "common/models_raw/kaykit/knight.glb" );
    assets.addFile( "game/x/textures_raw/wall.png" );
    SW_EXPECT_TRUE( params.canResolve( "common/models/kaykit/knight.mesh", assets ) );
    SW_EXPECT_TRUE( params.canResolve( "common/models/kaykit/knight/clips", assets ) ); // 옆 폴더
    SW_EXPECT_TRUE( params.canResolve( "common/models/kaykit/knight/knight.skeleton.xml", assets ) );
    SW_EXPECT_TRUE( params.canResolve( "game/x/textures/wall.dds", assets ) );
    SW_EXPECT_TRUE( params.canResolve( "common/models_raw", assets ) ); // 폴더
    SW_EXPECT_FALSE( params.canResolve( "common/models/kaykit/rogue.mesh", assets ) );
    SW_EXPECT_FALSE( params.canResolve( "game/x/textures/floor.dds", assets ) );
    SW_EXPECT_FALSE( params.isChecked( "scratch/assetmanage/probe.material" ) );
    SW_EXPECT_FALSE( params.isReferrer( "common/models/kaykit/knight/materials/a.material" ) ); // 임포터가 쓴 산출물
    SW_EXPECT_TRUE( params.isReferrer( "game/x/maps/a.scene.xml" ) );
    SW_EXPECT_FALSE( params.isReferrer( "game/x/redirects.xml" ) ); // 리다이렉트 표는 옛 id 를 적는다 — 참조가 아니다
}

/**
 * @brief [AssetReferenceFieldsTest] 고쳐 쓰기가 경로 칸의 값만 바꾸고(칸 밖 · 주석은 그대로), 원본을 옮기면 산출물 경로도 바꾸는지 검증
 */
SW_TEST_CASE( AssetReferenceFieldsTest, RewritesOnlyPathFields )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( loadRepositoryFields( params ) );
    string text = "<!-- game/x/a.material -->\n"
                  "<M _materialPath=\"game/x/a.material\" _meshID=\"game/x/models/rock.mesh\" note=\"game/x/a.material\"/>\n";
    SW_EXPECT_EQUAL( params.rewrite( text, AssetReferenceSyntax::XML, params.makeRewriteRules( "game/x/a.material", "game/y/b.material", false ) ), 1u );
    SW_EXPECT_TRUE( text.find( "_materialPath=\"game/y/b.material\"" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "<!-- game/x/a.material -->" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "note=\"game/x/a.material\"" ) != string::npos );

    SW_EXPECT_EQUAL( params.rewrite( text, AssetReferenceSyntax::XML, params.makeRewriteRules( "game/x/models_raw/rock.glb", "game/x/models_raw/stone.glb", false ) ), 1u );
    SW_EXPECT_TRUE( text.find( "_meshID=\"game/x/models/stone.mesh\"" ) != string::npos );
}
