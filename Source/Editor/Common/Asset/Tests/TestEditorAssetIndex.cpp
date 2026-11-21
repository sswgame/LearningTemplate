/**
 * @file TestEditorAssetIndex.cpp
 * @brief 에디터 에셋 색인(`EditorAssetIndex`)이 바뀐 파일만 고치는지 봅니다 — 생긴 파일 · 사라진 파일 · 옮긴 파일(GUID 참조가 따라간다).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/EditorAssetIndex.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetRegistry.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorAssetIndexTestInternal
    {
        static void write( const string& root, string_view assetID, string_view text )
        {
            const string absPath = FileUtil::joinPath( root, assetID );
            SW_EXPECT_TRUE( FileUtil::ensureParentDirectoryExists( FileUtil::toDiskPath( absPath ) ) && FileUtil::writeTextFile( FileUtil::toDiskPath( absPath ), text ) ); // 도우미라 REQUIRE(케이스 본문 전용)를 쓰지 않는다
        }

        static void remove( const string& root, string_view assetID ) { SW_EXPECT_TRUE( FileUtil::removeFile( FileUtil::toDiskPath( FileUtil::joinPath( root, assetID ) ) ) ); }

        static EditorAssetIndex scan( const string& root )
        {
            EditorAssetIndexData data{};
            EditorAssetIndex::scan( root, {}, data, nullptr );
            EditorAssetIndex index;
            index.assign( std::move( data ) );
            return index;
        }

        static vector<string> findReferrerIDs( const EditorAssetIndex& index, string_view targetID )
        {
            vector<EditorAssetReference> listReference;
            index.findReferrers( targetID, listReference );
            vector<string> listReferrer;
            for ( const EditorAssetReference& reference : listReference )
            {
                listReferrer.push_back( reference._referrerPath );
            }
            return listReferrer;
        }
    };
} // namespace

/**
 * @brief [EditorAssetIndexTest] 없던 파일이 생기면 그 파일만 넣어도 이미 그 경로를 적어 둔 참조자가 보인다(다른 파일을 다시 읽지 않는다). 바뀐 참조자도 그 파일만 다시 읽는다.
 */
SW_TEST_CASE( EditorAssetIndexTest, AddedFileRefreshesIncrementally )
{
    using Internal    = EditorAssetIndexTestInternal;
    const string root = sw::test::makeScratchPath( "added" );
    Internal::write( root, "game/a/scene.scene.xml", "<Scene>\n  <Sprite _texture=\"game/a/later.png\"/>\n  <Sprite _texture=\"game/a/now.png\"/>\n</Scene>\n" );
    Internal::write( root, "game/a/now.png", "png" );
    EditorAssetIndex index = Internal::scan( root );
    SW_EXPECT_EQUAL( size_t{ 1 }, Internal::findReferrerIDs( index, "game/a/now.png" ).size() );
    SW_EXPECT_TRUE( Internal::findReferrerIDs( index, "game/a/later.png" ).empty() ); // 아직 없는 파일

    Internal::write( root, "game/a/later.png", "png" );
    index.applyChanges( root, { "game/a/later.png" } );
    SW_EXPECT_TRUE( index.containsFile( "game/a/later.png" ) );
    const vector<string> listReferrer = Internal::findReferrerIDs( index, "game/a/later.png" );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listReferrer.size() );
    SW_EXPECT_EQUAL( string( "game/a/scene.scene.xml" ), listReferrer[0] );

    // 참조자를 고치면 그 파일의 줄만 바뀐다.
    Internal::write( root, "game/a/scene.scene.xml", "<Scene>\n  <Sprite _texture=\"game/a/later.png\"/>\n</Scene>\n" );
    index.applyChanges( root, { "game/a/scene.scene.xml" } );
    SW_EXPECT_TRUE( Internal::findReferrerIDs( index, "game/a/now.png" ).empty() );
    vector<EditorAssetReference> listDependency;
    index.findDependencies( "game/a/scene.scene.xml", listDependency );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDependency.size() );
    SW_EXPECT_EQUAL( string( "game/a/later.png" ), listDependency[0]._targetPath );
    SW_EXPECT_EQUAL( 2u, listDependency[0]._line );
}

/**
 * @brief [EditorAssetIndexTest] 사라진 파일은 그 줄 · 그 파일이 적은 참조 · 정체성 줄이 빠진다. 사라진 폴더는 그 아래 줄이 모두 빠진다.
 */
SW_TEST_CASE( EditorAssetIndexTest, RemovedFileDropsItsReferences )
{
    using Internal       = EditorAssetIndexTestInternal;
    const string    root = sw::test::makeScratchPath( "removed" );
    const AssetGUID guid = AssetGUID::create();
    Internal::write( root, "game/a/target.png", "png" );
    Internal::write( root, "game/a/referrer.prefab.xml", AssetGUIDHeader::write( "<Prefab>\n  <Sprite _texture=\"game/a/target.png\"/>\n</Prefab>\n", guid ) );
    Internal::write( root, "game/b/one.png", "png" );
    Internal::write( root, "game/b/two.png", "png" );
    EditorAssetIndex index = Internal::scan( root );
    SW_EXPECT_EQUAL( 1u, index.countReferrerFiles( "game/a/target.png" ) );
    SW_REQUIRE_TRUE( index.findPath( guid ).has_value() );

    Internal::remove( root, "game/a/referrer.prefab.xml" );
    index.applyChanges( root, { "game/a/referrer.prefab.xml" } );
    SW_EXPECT_FALSE( index.containsFile( "game/a/referrer.prefab.xml" ) );
    SW_EXPECT_EQUAL( 0u, index.countReferrerFiles( "game/a/target.png" ) );
    SW_EXPECT_FALSE( index.findPath( guid ).has_value() );

    // 사라진 대상은 참조자가 글에 적어 두어도 묻지 않는다(대상 없음).
    Internal::write( root, "game/a/referrer.prefab.xml", "<Prefab>\n  <Sprite _texture=\"game/a/target.png\"/>\n</Prefab>\n" );
    Internal::remove( root, "game/a/target.png" );
    index.applyChanges( root, { "game/a/referrer.prefab.xml", "game/a/target.png" } );
    SW_EXPECT_TRUE( Internal::findReferrerIDs( index, "game/a/target.png" ).empty() );
    vector<EditorAssetReference> listDependency;
    index.findDependencies( "game/a/referrer.prefab.xml", listDependency );
    SW_EXPECT_TRUE( listDependency.empty() );

    SW_REQUIRE_TRUE( FileUtil::removeDirectory( FileUtil::toDiskPath( FileUtil::joinPath( root, "game/b" ) ) ) );
    index.applyChanges( root, { "game/b" } );
    SW_EXPECT_FALSE( index.containsFile( "game/b/one.png" ) );
    SW_EXPECT_FALSE( index.containsFile( "game/b/two.png" ) );
    SW_EXPECT_TRUE( index.containsFile( "game/a/referrer.prefab.xml" ) );
}

/**
 * @brief [EditorAssetIndexTest] GUID 로 적은 참조는 대상을 옮겨도(옛 id 삭제 + 새 id 생김) 참조자를 다시 읽지 않고 새 자리를 가리킨다 — 머리 글과 `.meta` 원본 모두.
 */
SW_TEST_CASE( EditorAssetIndexTest, MoveKeepsReferrers )
{
    using Internal             = EditorAssetIndexTestInternal;
    const string    root       = sw::test::makeScratchPath( "moved" );
    const AssetGUID prefabGUID = AssetGUID::create();
    const AssetGUID soundGUID  = AssetGUID::create();
    const string    prefabText = AssetGUIDHeader::write( "<Prefab/>\n", prefabGUID );
    Internal::write( root, "game/a/hero.prefab.xml", prefabText );
    Internal::write( root, "game/a/hit.ogg", "ogg" );
    Internal::write( root, "game/a/hit.ogg.meta", AssetMeta::makeText( "game/a/hit.ogg", soundGUID ) );
    StringBuilder<constant::buffer::kMax256> sceneText;
    sceneText.appendFormat( "<Scene>\n  <Spawn _prefab=\"%#\"/>\n  <Sound _clip=\"%#\"/>\n</Scene>\n", AssetRegistry::makeReferenceText( prefabGUID ), AssetRegistry::makeReferenceText( soundGUID ) );
    Internal::write( root, "game/a/level.scene.xml", sceneText.view() );
    EditorAssetIndex index = Internal::scan( root );
    SW_EXPECT_EQUAL( 1u, index.countReferrerFiles( "game/a/hero.prefab.xml" ) );
    SW_EXPECT_EQUAL( 1u, index.countReferrerFiles( "game/a/hit.ogg" ) );

    Internal::write( root, "game/b/hero.prefab.xml", prefabText );
    Internal::remove( root, "game/a/hero.prefab.xml" );
    Internal::write( root, "game/b/hit.ogg", "ogg" );
    Internal::write( root, "game/b/hit.ogg.meta", AssetMeta::makeText( "game/b/hit.ogg", soundGUID ) );
    Internal::remove( root, "game/a/hit.ogg" );
    Internal::remove( root, "game/a/hit.ogg.meta" );
    // 새 자리가 먼저 와도 옛 자리를 빼는 일이 새 줄을 지우지 않는다.
    index.applyChanges( root, { "game/b/hero.prefab.xml", "game/a/hero.prefab.xml", "game/b/hit.ogg", "game/b/hit.ogg.meta", "game/a/hit.ogg", "game/a/hit.ogg.meta" } );

    SW_EXPECT_EQUAL( string( "game/b/hero.prefab.xml" ), index.findPath( prefabGUID ).value_or( string{} ) );
    SW_EXPECT_EQUAL( string( "game/b/hit.ogg" ), index.findPath( soundGUID ).value_or( string{} ) );
    const vector<string> listReferrer = Internal::findReferrerIDs( index, "game/b/hero.prefab.xml" );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listReferrer.size() );
    SW_EXPECT_EQUAL( string( "game/a/level.scene.xml" ), listReferrer[0] );
    SW_EXPECT_EQUAL( 1u, index.countReferrerFiles( "game/b/hit.ogg" ) );
    SW_EXPECT_EQUAL( 0u, index.countReferrerFiles( "game/a/hero.prefab.xml" ) );

    vector<EditorAssetReference> listDependency;
    index.findDependencies( "game/a/level.scene.xml", listDependency );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listDependency.size() );
    SW_EXPECT_EQUAL( string( "game/b/hero.prefab.xml" ), listDependency[0]._targetPath );
    SW_EXPECT_TRUE( listDependency[0]._bByGUID ); // 옮기기의 다시 쓰기가 건너뛴다 — 고칠 글이 없다
    SW_EXPECT_EQUAL( string( "game/b/hit.ogg" ), listDependency[1]._targetPath );
}

/**
 * @brief [EditorAssetIndexTest] 시나리오 사본 도메인(`scratch/`)도 경로 참조의 시작이다 — 빠지면 사본 안의 참조(이름 바꾸기가 고칠 씬)를 세지 못한다. 경계 안의 글(`mygame/`)은 아니다.
 */
SW_TEST_CASE( EditorAssetIndexTest, ScratchCopiesAreReferenceDomains )
{
    vector<EditorAssetLink> listLink;
    EditorAssetIndex::extractLinks( "<A _a=\"scratch/run1/x.png\" _b=\"mygame/x.png\" _base=\"game/p.xml#id\"/>", listLink );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listLink.size() );
    SW_EXPECT_EQUAL( string( "scratch/run1/x.png" ), listLink[0]._target );
    SW_EXPECT_FALSE( listLink[0]._bInlined );
    SW_EXPECT_EQUAL( string( "game/p.xml" ), listLink[1]._target );
    SW_EXPECT_TRUE( listLink[1]._bInlined );
}

/**
 * @brief [EditorAssetIndexTest] 열쇠는 소문자 리소스 id 이고 목록에는 디스크 표기가 그대로 나온다 — 전체 훑기와 한 파일 갱신이 같다. `.meta` 는 목록의 파일이 아니다.
 */
SW_TEST_CASE( EditorAssetIndexTest, DisplayPathKeepsDiskSpelling )
{
    using Internal    = EditorAssetIndexTestInternal;
    const string root = sw::test::makeScratchPath( "spelling" );
    Internal::write( root, "game/a/HeroIcon.png", "png" );
    Internal::write( root, "game/a/HeroIcon.png.meta", AssetMeta::makeText( "game/a/HeroIcon.png", AssetGUID::create() ) );
    EditorAssetIndex index = Internal::scan( root );
    Internal::write( root, "game/a/LaterIcon.png", "png" );
    index.applyChanges( root, { "game/a/LaterIcon.png" } );

    SW_EXPECT_TRUE( index.containsFile( "game/a/heroicon.png" ) );
    SW_EXPECT_FALSE( index.containsFile( "game/a/heroicon.png.meta" ) );
    SW_EXPECT_TRUE( index.findGUID( "game/a/HeroIcon.png" ).has_value() );
    vector<string> listPath;
    index.collectDisplayPaths( listPath );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPath.size() );
    SW_EXPECT_EQUAL( string( "game/a/HeroIcon.png" ), listPath[0] );
    SW_EXPECT_EQUAL( string( "game/a/LaterIcon.png" ), listPath[1] );
}
