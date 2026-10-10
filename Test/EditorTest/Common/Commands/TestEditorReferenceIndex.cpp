#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorReferenceIndex.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 시험마다 비운 임시 리소스 루트입니다. 소멸자가 지운다. */
    struct ScopedResourceRoot
    {
        string _path;

        explicit ScopedResourceRoot( string_view name )
            : _path{ FileUtil::joinPath( FileUtil::getTempDirectory(), name ) }
        {
            (void)FileUtil::removeDirectory( _path ); // 앞 실행이 남긴 것 — 없으면 그대로다
        }

        ~ScopedResourceRoot() { (void)FileUtil::removeDirectory( _path ); } // 임시 폴더라 남아도 다음 실행이 지운다

        [[nodiscard]] bool write( string_view resourceID, string_view text ) const
        {
            const string filePath = FileUtil::joinPath( _path, resourceID );
            return FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( filePath ) ) && FileUtil::writeTextFile( filePath, text );
        }
    };
} // namespace

/**
 * @brief [EditorReferenceIndexTest] 글에서 뽑는 참조는 실제로 있는 파일의 리소스 id 뿐이다
 * @details 이름 붙은 글이 우연히 경로와 같아도 파일이 없으면 세지 않는다. 대소문자는 리소스 id 처럼 소문자로 맞추고, 단어 중간의 `game/` 은 경로가 아니다.
 */
SW_TEST_CASE( EditorReferenceIndexTest, ExtractKeepsOnlyExistingResourceIDs )
{
    const vector<string> listKnownID = { "engine/models/cube.mesh" };
    const string_view    text        = "<Mesh path=\"Engine/Models/Cube.mesh\"/>\n<Name value=\"engine/not/a/file\"/>\n<Other value=\"mygame/engine/models/cube.mesh\"/>";

    vector<EditorAssetReference> listReference;
    EditorReferenceIndex::extractReferences( "game/empty/maps/a.scene.xml", text, listKnownID, listReference );

    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listReference.size() );
    SW_EXPECT_STREQ( "engine/models/cube.mesh", listReference[0]._targetPath.c_str() );
    SW_EXPECT_STREQ( "game/empty/maps/a.scene.xml", listReference[0]._referrerPath.c_str() );
    SW_EXPECT_EQUAL( 1u, listReference[0]._line );
}

/**
 * @brief [EditorReferenceIndexTest] "누가 쓰나" 와 "무엇을 쓰나" 는 서로의 역이다
 * @details 임시 리소스 루트에 텍스트 둘과 바이너리 하나를 두고 훑는다. 바이너리 안의 경로 글은 참조가 아니다.
 */
SW_TEST_CASE( EditorReferenceIndexTest, ReferrersAndDependenciesAreInverse )
{
    const ScopedResourceRoot root{ "sw_editor_reference_index_inverse" };
    SW_ASSERT_TRUE( root.write( "game/test/maps/a.scene.xml", "<Scene>\n  <Mesh path=\"game/test/models/b.mesh\"/>\n</Scene>\n" ) );
    SW_ASSERT_TRUE( root.write( "game/test/models/b.mesh", "binary game/test/maps/a.scene.xml" ) );
    SW_ASSERT_TRUE( root.write( "engine/materials/c.material", "{ \"_texture\": \"game/test/models/b.mesh\" }" ) );

    EditorReferenceIndexData data{};
    EditorReferenceIndex::scan( root._path, data );
    SW_EXPECT_EQUAL( 2u, data._scannedFileCount );
    EditorReferenceIndex index{};
    index.assign( std::move( data ) );
    SW_EXPECT_TRUE( index.isReady() );

    vector<EditorAssetReference> listReference;
    index.findReferrers( "game/test/models/b.mesh", listReference );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listReference.size() );
    SW_EXPECT_STREQ( "engine/materials/c.material", listReference[0]._referrerPath.c_str() );
    SW_EXPECT_STREQ( "game/test/maps/a.scene.xml", listReference[1]._referrerPath.c_str() );
    SW_EXPECT_EQUAL( 2u, listReference[1]._line );
    SW_EXPECT_EQUAL( 2u, index.countReferrerFiles( "GAME/test/models/b.mesh" ) );

    index.findDependencies( "game/test/maps/a.scene.xml", listReference );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listReference.size() );
    SW_EXPECT_STREQ( "game/test/models/b.mesh", listReference[0]._targetPath.c_str() );

    index.findReferrers( "game/test/maps/a.scene.xml", listReference );
    SW_EXPECT_TRUE( listReference.empty() ); // .mesh 는 바이너리라 훑지 않는다
}

/**
 * @brief [EditorReferenceIndexTest] 파일 하나를 다시 훑으면 그 파일이 지운 참조가 빠지고, 지운 파일은 참조자에서 빠진다
 */
SW_TEST_CASE( EditorReferenceIndexTest, RefreshFileDropsRemovedReferences )
{
    const ScopedResourceRoot root{ "sw_editor_reference_index_refresh" };
    SW_ASSERT_TRUE( root.write( "game/test/maps/a.scene.xml", "<Mesh path=\"game/test/models/b.mesh\"/>" ) );
    SW_ASSERT_TRUE( root.write( "game/test/maps/c.scene.xml", "<Mesh path=\"game/test/models/b.mesh\"/>" ) );
    SW_ASSERT_TRUE( root.write( "game/test/models/b.mesh", "binary" ) );

    EditorReferenceIndexData data{};
    EditorReferenceIndex::scan( root._path, data );
    EditorReferenceIndex index{};
    index.assign( std::move( data ) );
    SW_EXPECT_EQUAL( 2u, index.countReferrerFiles( "game/test/models/b.mesh" ) );

    SW_ASSERT_TRUE( root.write( "game/test/maps/a.scene.xml", "<Scene/>" ) );
    index.refreshFile( root._path, "game/test/maps/a.scene.xml" );
    SW_EXPECT_EQUAL( 1u, index.countReferrerFiles( "game/test/models/b.mesh" ) );

    SW_ASSERT_TRUE( FileUtil::removeFile( FileUtil::joinPath( root._path, "game/test/maps/c.scene.xml" ) ) );
    index.refreshFile( root._path, "game/test/maps/c.scene.xml" );
    SW_EXPECT_EQUAL( 0u, index.countReferrerFiles( "game/test/models/b.mesh" ) );
}
