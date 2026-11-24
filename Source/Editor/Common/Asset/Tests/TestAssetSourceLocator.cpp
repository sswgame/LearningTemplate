// AssetSourceLocatorTest — 원본 바이트의 자리: 곁의 파일 · 내용 주소 저장소 · 없음(오류), 저장소 밖 원본의 나열(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/AssetSourceLocator.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetGUID.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct AssetSourceLocatorTestUtil
    {
        static constexpr const utf8* kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

        static void write( const AbsolutePath& path, string_view text )
        {
            SW_EXPECT_TRUE( FileUtil::ensureParentDirectoryExists( path ) );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( path, text ) );
        }

        /** @brief @p name 의 내용 주소 서술 글입니다. */
        static string makeStoredMeta( string_view name )
        {
            StringBuilder<constant::buffer::kMax512> builder;
            builder.appendFormat( "<AssetMeta _guid=\"%#\" _source=\"store:%#/%#\" />", AssetGUID::create().toString(), kHash, name );
            return string( builder.view() );
        }
    };
} // namespace

/** @brief [AssetSourceLocatorTest] 곁의 파일이 있으면 그 파일, 없으면 서술의 내용 주소를 저장소(Git LFS 배치)에서 푼다. 저장소에 없거나 서술이 곁의 파일을 적었으면 오류다 */
SW_TEST_CASE( AssetSourceLocatorTest, BesideFileThenStoreThenError )
{
    using Util                   = AssetSourceLocatorTestUtil;
    const AbsolutePath root      = sw::test::makeScratchPath( "sourcelocator" );
    const AbsolutePath storeRoot = root.join( "store" );

    const AbsolutePath beside = root.join( "models_raw/beside.glb" );
    Util::write( beside, "beside" );
    const expected<AbsolutePath> besidePath = AssetSourceLocator::resolve( beside );
    SW_REQUIRE_TRUE( besidePath.has_value() );
    SW_EXPECT_TRUE( *besidePath == beside );

    const AbsolutePath stored = root.join( "models_raw/stored.glb" );
    Util::write( AssetMeta::makePath( stored ), Util::makeStoredMeta( "stored.glb" ) );
    const expected<AbsolutePath> notFetched = AssetSourceLocator::resolveInStore( stored, storeRoot );
    SW_REQUIRE_FALSE( notFetched.has_value() );
    SW_EXPECT_TRUE( notFetched.error().kind() == ErrorKind::NotFound );

    const AbsolutePath storeFile = storeRoot.join( "01" ).join( "23" ).join( Util::kHash );
    Util::write( storeFile, "stored" );
    const expected<AbsolutePath> storePath = AssetSourceLocator::resolveInStore( stored, storeRoot );
    SW_REQUIRE_TRUE( storePath.has_value() );
    SW_EXPECT_TRUE( *storePath == storeFile );

    const AbsolutePath lost = root.join( "models_raw/lost.glb" );
    Util::write( AssetMeta::makePath( lost ), AssetMeta::makeText( "models_raw/lost.glb", AssetGUID::create() ) );
    SW_EXPECT_FALSE( AssetSourceLocator::resolveInStore( lost, storeRoot ).has_value() );
}

/** @brief [AssetSourceLocatorTest] 파일 없이 저장소 주소만 적은 원본만 더한다 — 있는 파일과 짝 잃은 서술은 더하지 않는다 */
SW_TEST_CASE( AssetSourceLocatorTest, OnlyStoredSourcesWithoutAFileAreListed )
{
    using Util              = AssetSourceLocatorTestUtil;
    const AbsolutePath root = sw::test::makeScratchPath( "sourcelist" );
    Util::write( root.join( "beside.glb" ), "beside" );
    Util::write( AssetMeta::makePath( root.join( "beside.glb" ) ), AssetMeta::makeText( "beside.glb", AssetGUID::create() ) );
    Util::write( AssetMeta::makePath( root.join( "sub/stored.glb" ) ), Util::makeStoredMeta( "stored.glb" ) );
    Util::write( AssetMeta::makePath( root.join( "lost.glb" ) ), AssetMeta::makeText( "lost.glb", AssetGUID::create() ) );

    vector<AbsolutePath> listSourcePath;
    AssetSourceLocator::collectStoredSources( root, listSourcePath );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listSourcePath.size() );
    SW_EXPECT_TRUE( StringUtil::endsWith( listSourcePath[0].str(), "/sub/stored.glb" ) );
}

/** @brief [AssetSourceLocatorTest] 저장소 설정이 읽힌다 — 저장소의 설정 파일이 정한 한 자리다 */
SW_TEST_CASE( AssetSourceLocatorTest, StoreRootComesFromConfig )
{
    SW_EXPECT_TRUE( AssetSourceLocator::loadStoreRoot().has_value() );
}
