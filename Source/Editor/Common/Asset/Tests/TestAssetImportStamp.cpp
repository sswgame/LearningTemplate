// AssetImportStampUtil — 원본이 사라진 스탬프 줄의 결과(파일 · 옆 폴더)를 지우는지, 어긋난 원본을 DDC 에서 받는지. 가짜 임포터가 원본 하나를 결과 파일과 옆 폴더 하나로 쓴다.
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/AssetImportStamp.h"

#include "Engine/Asset/Cook/CookRecord.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 부모 폴더를 만들고 글을 씁니다. */
    bool writeText( const string& path, string_view text )
    {
        return FileUtil::ensureParentDirectoryExists( FileUtil::toDiskPath( path ) ) && FileUtil::writeTextFile( FileUtil::toDiskPath( path ), text );
    }

    /** @brief `things_raw/<y>.src` 를 `things/<y>.out` 과 옆 폴더 `things/<y>/side.txt` 로 쓰는 임포터입니다. */
    class FakeThingImporter final : public IRawAssetImporter
    {
    public:
        explicit FakeThingImporter( string settings = "default" )
            : _settings{ std::move( settings ) }
        {
        }

        string_view getRawFolderName() const override { return "things_raw"; }
        string_view getStampHeader() const override { return "fake-things 1"; }
        string_view getImportedLabel() const override { return "thing"; }
        bool        isSourceFile( string_view path ) const override { return FileUtil::hasExtension( path, ".src" ); }
        string      makeImportedPath( string_view sourcePath ) const override { return AssetImportStampUtil::makeImportedPath( sourcePath, "things_raw", "things", ".out" ); }
        uint64      computeSourceHash( string_view sourcePath, string_view /*assetPath*/ ) const override
        {
            return HashUtil::combine( AssetImportStampUtil::computeFileHash( sourcePath ), HashUtil::computeStringHash64( _settings.c_str(), _settings.size(), false ) );
        }
        uint64 findCodeFingerprint() const override { return 1; }
        void   collectImportedFiles( string_view importedPath, vector<string>& outListPath ) const override
        {
            const string sidePath = FileUtil::joinPath( FileUtil::removeExtension( importedPath ), "side.txt" );
            for ( const string& path : { string( importedPath ), sidePath } )
            {
                if ( FileUtil::isRegularFile( FileUtil::toDiskPath( path ) ) )
                    outListPath.push_back( path );
            }
        }
        uint32      getImportCount() const { return _importCount.load(); }
        const utf8* findUnsupportedReason( string_view /*sourcePath*/ ) const override { return nullptr; }
        bool        importSource( string_view /*sourcePath*/, string_view importedPath, string_view /*assetPath*/ ) const override
        {
            _importCount.fetch_add( 1 );
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "out:%#", _settings );
            return writeText( string( importedPath ), textBuilder.view() ) && writeText( FileUtil::joinPath( FileUtil::removeExtension( importedPath ), "side.txt" ), "side" );
        }

    private:
        string                 _settings;         ///< 원본 해시에 섞는 규칙 글(DDC 키를 가른다)
        mutable atomic<uint32> _importCount{ 0 }; ///< 테스트 계수 — 임포터가 불린 수(병렬 임포트에서도 센다)
    };

    /** @brief 같은 원본(`pack/things_raw/a.src`)을 둔 새 체크아웃 폴더입니다(결과 · 스탬프 없음). */
    string makeCheckout( string_view name )
    {
        const string root = test::makeScratchPath( name );
        (void)writeText( FileUtil::joinPath( root, "pack/things_raw/a.src" ), "a" );
        return root;
    }
} // namespace

/**
 * @brief [AssetImportStampTest] 원본을 지우고 다시 임포트하면 그 결과 파일과 옆 폴더가 지워지고 남은 원본의 결과는 그대로인지 검증
 * @details 결과는 생성물이라 깨끗한 클론에는 없다 — 남겨 두면 지운 원본을 아직 가리키는 코드가 이 PC 에서만 그려져 숨는다.
 */
SW_TEST_CASE( AssetImportStampTest, RemovingASourceRemovesItsResult )
{
    const string root = test::makeScratchPath( "AssetImportStampOrphan" );
    SW_REQUIRE_TRUE( writeText( FileUtil::joinPath( root, "pack/things_raw/gone.src" ), "a" ) );
    SW_REQUIRE_TRUE( writeText( FileUtil::joinPath( root, "pack/things_raw/kept.src" ), "b" ) );
    const FakeThingImporter importer;
    const DerivedDataCache  cache{ vector<DerivedDataLayer>{ DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite } } }; // 프로젝트 DDC 에 쓰지 않는다
    SW_REQUIRE_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_REQUIRE_TRUE( FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "pack/things/gone.out" ) ) ) );
    SW_REQUIRE_TRUE( FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "pack/things/gone/side.txt" ) ) ) );

    SW_REQUIRE_TRUE( FileUtil::removeFile( FileUtil::toDiskPath( FileUtil::joinPath( root, "pack/things_raw/gone.src" ) ) ) );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "pack/things/gone.out" ) ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "pack/things/gone" ) ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "pack/things/kept.out" ) ) ) );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::CheckOnly, nullptr, cache ).isClean() );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( FileUtil::toDiskPath( root ) ) );
}

/**
 * @brief [AssetImportStampTest] 다른 체크아웃이 임포트해 DDC 에 넣은 원본은 새 체크아웃에서 임포터를 부르지 않고 같은 결과(파일 · 옆 폴더)로 받는다
 */
SW_TEST_CASE( AssetImportStampTest, StaleSourceIsServedFromCache )
{
    const DerivedDataCache  cache{ vector<DerivedDataLayer>{ DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite } } };
    const string            firstRoot = makeCheckout( "ImportCacheFirst" );
    const FakeThingImporter first;
    SW_REQUIRE_TRUE( AssetImportStampUtil::importAll( firstRoot, first, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_REQUIRE_EQUAL( 1u, first.getImportCount() );

    const string             secondRoot = makeCheckout( "ImportCacheSecond" );
    const FakeThingImporter  second;
    const AssetImportSummary summary = AssetImportStampUtil::importAll( secondRoot, second, AssetImportMode::ImportStale, nullptr, cache );
    SW_EXPECT_TRUE( summary.isClean() );
    SW_EXPECT_EQUAL( 0u, second.getImportCount() );
    SW_EXPECT_EQUAL( 1u, summary._importedCount );
    string text;
    SW_EXPECT_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( secondRoot, "pack/things/a.out" ) ), text ) && text == "out:default" );
    SW_EXPECT_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( secondRoot, "pack/things/a/side.txt" ) ), text ) && text == "side" );
    // 받은 결과로 스탬프도 맞았다 — 다음 대조는 깨끗하다.
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( secondRoot, second, AssetImportMode::CheckOnly, nullptr, cache ).isClean() );
}

/**
 * @brief [AssetImportStampTest] 규칙(설정)이 다르면 같은 원본이라도 DDC 미스라 임포터가 불린다
 */
SW_TEST_CASE( AssetImportStampTest, DifferentSettingsMiss )
{
    const DerivedDataCache  cache{ vector<DerivedDataLayer>{ DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite } } };
    const FakeThingImporter first( "fast" );
    SW_REQUIRE_TRUE( AssetImportStampUtil::importAll( makeCheckout( "ImportSettingsFirst" ), first, AssetImportMode::ImportStale, nullptr, cache ).isClean() );

    const string            secondRoot = makeCheckout( "ImportSettingsSecond" );
    const FakeThingImporter second( "best" );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( secondRoot, second, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_EXPECT_EQUAL( 1u, second.getImportCount() );
    string text;
    SW_EXPECT_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( secondRoot, "pack/things/a.out" ) ), text ) && text == "out:best" );
}
