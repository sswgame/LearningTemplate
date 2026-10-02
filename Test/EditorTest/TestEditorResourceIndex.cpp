#include "pch.h"

#include "Editor/Common/Commands/EditorResourceIndex.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorResourceIndexTest] 퀵 런처의 분류는 애셋 종류 표와 같다 — Data 도 표의 Data 다
 * @details 퀵 런처가 분류를 따로 적어 "Data" 가 `.xml` · `.json` 둘이었다. 표의 Data 는 `.xml .json .ini .kv` 라 `.ini` · `.kv` 는 런처에 없었고,
 *          `.anim.json` · `.tilemap.xml` 은 자기 종류 대신 Data 로 떴으며, 머티리얼 · 오디오는 아예 색인되지 않았다.
 */
SW_TEST_CASE( EditorResourceIndexTest, CategoryComesFromTheKindTable )
{
    vector<string> listDataSuffix{};
    EditorAssetTypeRegistry::appendSuffixes( EditorAssetKind::Data, listDataSuffix );
    SW_ASSERT_TRUE( listDataSuffix.empty() == false );
    for ( const string& suffix : listDataSuffix )
    {
        const string             path = "Resource/game/empty/data/table" + suffix;
        EditorResourceIndexEntry entry{};
        SW_EXPECT_TRUE_MSG( EditorResourceIndex::classifyFile( path, entry ), path.c_str() );
        SW_EXPECT_TRUE_MSG( entry._kind == EditorAssetKind::Data, path.c_str() );
        SW_EXPECT_STREQ( "Data", entry._category.c_str() );
        SW_EXPECT_STREQ( ( "game/empty/data/table" + suffix ).c_str(), entry._path.c_str() );
    }

    struct Expectation
    {
        const utf8*     _pPath;
        EditorAssetKind _kind;
    };
    const Expectation arrExpectation[] = {
        {       "Resource/game/empty/maps/town.scene.xml",          EditorAssetKind::Scene},
        {       "Resource/game/empty/anim/idle.anim.json", EditorAssetKind::AnimationGraph},
        {"Resource/game/empty/maps/overworld.tilemap.xml",        EditorAssetKind::TileMap},
        {       "Resource/engine/materials/hero.material",       EditorAssetKind::Material},
        {             "Resource/game/empty/audio/hit.wav",          EditorAssetKind::Audio},
        {        "Resource/game/empty/cut/intro.seq.json",       EditorAssetKind::Sequence},
    };
    for ( const Expectation& expectation : arrExpectation )
    {
        EditorResourceIndexEntry entry{};
        SW_EXPECT_TRUE_MSG( EditorResourceIndex::classifyFile( expectation._pPath, entry ), expectation._pPath );
        SW_EXPECT_TRUE_MSG( entry._kind == expectation._kind, expectation._pPath );
        const EditorAssetKindInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( expectation._kind );
        SW_ASSERT_NOT_NULL( pInfo );
        SW_EXPECT_STREQ( pInfo->_pDisplayName, entry._category.c_str() );
    }

    // 알려진 종류가 아니거나 리소스 트리 밖이면 항목이 아니다.
    EditorResourceIndexEntry entry{};
    SW_EXPECT_FALSE( EditorResourceIndex::classifyFile( "Resource/game/empty/notes/todo.txt", entry ) );
    SW_EXPECT_FALSE( EditorResourceIndex::classifyFile( "../outside/table.json", entry ) );
}

/**
 * @brief [EditorResourceIndexTest] 리소스 카탈로그는 종류 표의 모든 종류를 한 줄씩 센다
 * @details 카탈로그가 종류 넷(씬 · 프리팹 · 텍스처 · 셰이더)을 구조체 필드와 호출 넷으로 따로 적어, 종류가 늘어도 카탈로그에는 나오지 않았다.
 */
SW_TEST_CASE( EditorResourceIndexTest, CatalogCountsEveryKindOfTheTable )
{
    const vector<string> listFile = {
        "maps/town.scene.xml",
        "prefabs/hero.prefab.xml",
        "prefabs/crate.prefab.json",
        "textures/a.png",
        "textures/b.dds",
        "shaders/pbr.hlsl",
        "config/input.ini",
        "anim/idle.anim.json",
        "notes/todo.txt",
    };
    EditorResourceCatalogCounts counts{};
    EditorResourceIndex::countFiles( listFile, counts );

    uint32 kindCount{ 0 };
    (void)EditorAssetTypeRegistry::getKindInfos( kindCount );
    SW_ASSERT_EQUAL( static_cast<size_t>( kindCount ), counts._listKindCount.size() );

    size_t total{ 0 };
    for ( const EditorResourceCatalogCount& row : counts._listKindCount )
    {
        SW_EXPECT_NOT_NULL( row._pLabel );
        total += row._count;
        size_t expected{ 0 };
        if ( row._kind == EditorAssetKind::Scene || row._kind == EditorAssetKind::Shader || row._kind == EditorAssetKind::Data ||
             row._kind == EditorAssetKind::AnimationGraph )
            expected = 1;
        else if ( row._kind == EditorAssetKind::Prefab || row._kind == EditorAssetKind::Texture )
            expected = 2;
        SW_EXPECT_TRUE_MSG( expected == row._count, row._pLabel );
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( 8 ), total ); // `todo.txt` 는 어떤 종류도 아니다
}
