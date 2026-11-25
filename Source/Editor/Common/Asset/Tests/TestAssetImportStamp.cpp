// AssetImportStampUtil — 원본이 사라진 스탬프 줄의 결과(파일 · 옆 폴더)를 지우는지, 어긋난 원본을 DDC 에서 받는지. 가짜 임포터가 원본 하나를 결과 파일과 옆 폴더 하나로 쓴다.
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/AssetImportStamp.h"

#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Asset/Cook/Tests/DerivedDataCacheTestAccess.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 부모 폴더를 만들고 글을 씁니다. */
    bool writeText( const AbsolutePath& path, string_view text )
    {
        return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, text );
    }

    /** @brief `things_raw/<y>.src` 를 `things/<y>.out` 과 옆 폴더 `things/<y>/side.txt` 로 쓰는 임포터입니다. */
    class FakeThingImporter : public IRawAssetImporter
    {
    public:
        explicit FakeThingImporter( string settings = "default" )
            : _settings{ std::move( settings ) }
        {
        }

        string_view  getRawFolderName() const override { return "things_raw"; }
        string_view  getStampHeader() const override { return "fake-things 1"; }
        string_view  getImportedLabel() const override { return "thing"; }
        bool         isSourceFile( string_view path ) const override { return FileUtil::hasAnyExtension( path, { ".src", ".alt" } ); }
        AbsolutePath makeImportedPath( const AbsolutePath& sourcePath ) const override { return AssetImportStampUtil::makeImportedPath( sourcePath, "things_raw", "things", ".out" ); }
        uint64       computeSourceHash( const AbsolutePath& sourcePath, string_view /*assetPath*/ ) const override
        {
            return HashUtil::combine( AssetImportStampUtil::computeFileHash( sourcePath ), HashUtil::computeStringHash64( _settings.c_str(), _settings.size(), false ) );
        }
        uint64 findCodeFingerprint() const override { return 1; }
        void   collectImportedFiles( const AbsolutePath& importedPath, vector<AbsolutePath>& outListPath ) const override
        {
            for ( const AbsolutePath& path : { importedPath, importedPath.removeExtension().join( "side.txt" ) } )
            {
                if ( FileUtil::isRegularFile( path ) )
                    outListPath.push_back( path );
            }
        }
        uint32      getImportCount() const { return _importCount.load(); }
        const utf8* findUnsupportedReason( const AbsolutePath& /*sourcePath*/ ) const override { return nullptr; }
        bool        importSource( const AbsolutePath& /*sourcePath*/, const AbsolutePath& importedPath, string_view /*assetPath*/ ) const override
        {
            _importCount.fetch_add( 1 );
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "out:%#", _settings );
            return writeText( importedPath, textBuilder.view() ) && writeText( importedPath.removeExtension().join( "side.txt" ), "side" );
        }

    private:
        string                 _settings;         ///< 원본 해시에 섞는 규칙 글(DDC 키를 가른다)
        mutable atomic<uint32> _importCount{ 0 }; ///< 테스트 계수 — 임포터가 불린 수(병렬 임포트에서도 센다)
    };

    /** @brief 임포트하는 동안 원본이 바뀌는 것을 흉내 냅니다 — 결과를 쓴 뒤 원본에 다른 글을 씁니다(한 번만). */
    class EditedSourceImporter final : public FakeThingImporter
    {
    public:
        bool importSource( const AbsolutePath& sourcePath, const AbsolutePath& importedPath, string_view assetPath ) const override
        {
            const bool bImported = FakeThingImporter::importSource( sourcePath, importedPath, assetPath );
            return bImported && ( getImportCount() > 1 || writeText( sourcePath, "edited while importing" ) );
        }
    };

    /** @brief 같은 원본(`pack/things_raw/a.src`)을 둔 새 체크아웃 폴더입니다(결과 · 스탬프 없음). */
    AbsolutePath makeCheckout( string_view name )
    {
        const AbsolutePath root = test::makeScratchPath( name );
        (void)writeText( root.join( "pack/things_raw/a.src" ), "a" );
        return root;
    }
} // namespace

/**
 * @brief [AssetImportStampTest] 원본을 지우고 다시 임포트하면 그 결과 파일과 옆 폴더가 지워지고 남은 원본의 결과는 그대로인지 검증
 * @details 결과는 생성물이라 깨끗한 클론에는 없다 — 남겨 두면 지운 원본을 아직 가리키는 코드가 이 PC 에서만 그려져 숨는다.
 */
SW_TEST_CASE( AssetImportStampTest, RemovingASourceRemovesItsResult )
{
    const AbsolutePath root = test::makeScratchPath( "AssetImportStampOrphan" );
    SW_REQUIRE_TRUE( writeText( root.join( "pack/things_raw/gone.src" ), "a" ) );
    SW_REQUIRE_TRUE( writeText( root.join( "pack/things_raw/kept.src" ), "b" ) );
    const FakeThingImporter importer;
    const DerivedDataCache  cache = DerivedDataCacheTestAccess::make( vector<DerivedDataLayer>{
        DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite }
    } ); // 프로젝트 DDC 에 쓰지 않는다
    SW_REQUIRE_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_REQUIRE_TRUE( FileUtil::exists( root.join( "pack/things/gone.out" ) ) );
    SW_REQUIRE_TRUE( FileUtil::exists( root.join( "pack/things/gone/side.txt" ) ) );

    SW_REQUIRE_TRUE( FileUtil::removeFile( root.join( "pack/things_raw/gone.src" ) ) );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_EXPECT_FALSE( FileUtil::exists( root.join( "pack/things/gone.out" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( root.join( "pack/things/gone" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( root.join( "pack/things/kept.out" ) ) );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::CheckOnly, nullptr, cache ).isClean() );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [AssetImportStampTest] 원본을 지워도 임포터가 자기 결과라고 답하지 않은 파일(같은 이름 폴더의 손 데이터)은 남는지 검증
 */
SW_TEST_CASE( AssetImportStampTest, RemovingASourceKeepsFilesTheImporterDoesNotOwn )
{
    const AbsolutePath root = test::makeScratchPath( "AssetImportStampHandFile" );
    SW_REQUIRE_TRUE( writeText( root.join( "pack/things_raw/gone.src" ), "a" ) );
    const FakeThingImporter importer;
    const DerivedDataCache  cache = DerivedDataCacheTestAccess::make( vector<DerivedDataLayer>{
        DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite }
    } );
    SW_REQUIRE_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_REQUIRE_TRUE( writeText( root.join( "pack/things/gone/hand.txt" ), "hand" ) ); // 커밋한 손 데이터 — 결과가 아니다

    SW_REQUIRE_TRUE( FileUtil::removeFile( root.join( "pack/things_raw/gone.src" ) ) );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_EXPECT_FALSE( FileUtil::exists( root.join( "pack/things/gone.out" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( root.join( "pack/things/gone/side.txt" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( root.join( "pack/things/gone/hand.txt" ) ) );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [AssetImportStampTest] 다른 체크아웃이 임포트해 DDC 에 넣은 원본은 새 체크아웃에서 임포터를 부르지 않고 같은 결과(파일 · 옆 폴더)로 받는다
 */
SW_TEST_CASE( AssetImportStampTest, StaleSourceIsServedFromCache )
{
    const DerivedDataCache  cache     = DerivedDataCacheTestAccess::make( vector<DerivedDataLayer>{
        DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite }
    } );
    const AbsolutePath      firstRoot = makeCheckout( "ImportCacheFirst" );
    const FakeThingImporter first;
    SW_REQUIRE_TRUE( AssetImportStampUtil::importAll( firstRoot, first, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_REQUIRE_EQUAL( 1u, first.getImportCount() );

    const AbsolutePath       secondRoot = makeCheckout( "ImportCacheSecond" );
    const FakeThingImporter  second;
    const AssetImportSummary summary = AssetImportStampUtil::importAll( secondRoot, second, AssetImportMode::ImportStale, nullptr, cache );
    SW_EXPECT_TRUE( summary.isClean() );
    SW_EXPECT_EQUAL( 0u, second.getImportCount() );
    SW_EXPECT_EQUAL( 1u, summary._importedCount );
    string text;
    SW_EXPECT_TRUE( FileUtil::readTextFile( secondRoot.join( "pack/things/a.out" ), text ) && text == "out:default" );
    SW_EXPECT_TRUE( FileUtil::readTextFile( secondRoot.join( "pack/things/a/side.txt" ), text ) && text == "side" );
    // 받은 결과로 스탬프도 맞았다 — 다음 대조는 깨끗하다.
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( secondRoot, second, AssetImportMode::CheckOnly, nullptr, cache ).isClean() );
}

/**
 * @brief [AssetImportStampTest] 임포트하는 동안 원본이 바뀌면 그 결과를 스탬프 · DDC 에 앞 원본의 것으로 남기지 않고, 다음 임포트가 다시 만든다
 */
SW_TEST_CASE( AssetImportStampTest, SourceChangedWhileImportingIsNotRecorded )
{
    const DerivedDataCache     cache = DerivedDataCacheTestAccess::make( vector<DerivedDataLayer>{
        DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite }
    } );
    const AbsolutePath         root  = makeCheckout( "ImportSourceChanged" );
    const EditedSourceImporter edited;
    SW_EXPECT_FALSE( AssetImportStampUtil::importAll( root, edited, AssetImportMode::ImportStale, nullptr, cache ).isClean() );

    // 원본을 처음 내용으로 되돌린 다른 체크아웃은 DDC 에서 바뀐 원본의 결과를 받지 않는다.
    const FakeThingImporter clean;
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( makeCheckout( "ImportSourceChangedClean" ), clean, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_EXPECT_EQUAL( 1u, clean.getImportCount() );

    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, edited, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, edited, AssetImportMode::CheckOnly, nullptr, cache ).isClean() );
}

/**
 * @brief [AssetImportStampTest] 확장자만 다른 두 원본이 한 결과에 쓰면 뒤의 원본을 문제로 알리고(대조 · 임포트 모두) 앞의 원본만 임포트한다
 */
SW_TEST_CASE( AssetImportStampTest, SourcesWritingOneResultAreReported )
{
    const DerivedDataCache cache = DerivedDataCacheTestAccess::make( vector<DerivedDataLayer>{
        DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite }
    } );
    const AbsolutePath     root  = makeCheckout( "ImportCollision" );
    SW_REQUIRE_TRUE( writeText( root.join( "pack/things_raw/a.alt" ), "alt" ) );
    const FakeThingImporter  importer;
    const AssetImportSummary imported = AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), imported._listProblem.size() );
    SW_EXPECT_TRUE( imported._listProblem[0].find( "pack/things_raw/a.src" ) != string::npos );
    SW_EXPECT_EQUAL( 1u, importer.getImportCount() );
    // 다시 돌아도 앞 원본을 서로 덮어 다시 임포트하지 않는다.
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache )._listProblem.size() );
    SW_EXPECT_EQUAL( 1u, importer.getImportCount() );
    // 앞 원본을 지우면 결과는 남은 원본의 것이다 — 사라진 원본의 결과로 지우지 않는다.
    SW_REQUIRE_TRUE( FileUtil::removeFile( root.join( "pack/things_raw/a.alt" ) ) );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_EXPECT_TRUE( FileUtil::exists( root.join( "pack/things/a.out" ) ) );
    SW_REQUIRE_TRUE( writeText( root.join( "pack/things_raw/a.alt" ), "alt" ) );
    // 범위를 정한 임포트(핫 리로드)도 범위 밖 원본의 결과를 덮지 않는다.
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), AssetImportStampUtil::importSources( root, importer, vector<string>{ "pack/things_raw/a.src" }, nullptr, cache )._listProblem.size() );
    SW_EXPECT_EQUAL( 2u, importer.getImportCount() ); // 앞에서 a.src 를 한 번 더 임포트했다
}

/**
 * @brief [AssetImportStampTest] 규칙(설정)이 다르면 같은 원본이라도 DDC 미스라 임포터가 불린다
 */
SW_TEST_CASE( AssetImportStampTest, DifferentSettingsMiss )
{
    const DerivedDataCache  cache = DerivedDataCacheTestAccess::make( vector<DerivedDataLayer>{
        DerivedDataLayer{ test::makeScratchPath( "ImportDDC" ), DerivedDataAccess::ReadWrite }
    } );
    const FakeThingImporter first( "fast" );
    SW_REQUIRE_TRUE( AssetImportStampUtil::importAll( makeCheckout( "ImportSettingsFirst" ), first, AssetImportMode::ImportStale, nullptr, cache ).isClean() );

    const AbsolutePath      secondRoot = makeCheckout( "ImportSettingsSecond" );
    const FakeThingImporter second( "best" );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( secondRoot, second, AssetImportMode::ImportStale, nullptr, cache ).isClean() );
    SW_EXPECT_EQUAL( 1u, second.getImportCount() );
    string text;
    SW_EXPECT_TRUE( FileUtil::readTextFile( secondRoot.join( "pack/things/a.out" ), text ) && text == "out:best" );
}

/**
 * @brief [AssetImportStampTest] 파일 해시는 바이트의 FNV-1a 64 그대로이고(스탬프 · DDC 열쇠가 바뀌지 않는다), 같은 크기로 다시 써도 새 내용을 따르며 없거나 빈 파일은 0 이다
 */
SW_TEST_CASE( AssetImportStampTest, FileHashFollowsTheContent )
{
    const AbsolutePath path = test::makeScratchPath( "ImportFileHash.src" );
    SW_EXPECT_EQUAL( uint64{ 0 }, AssetImportStampUtil::computeFileHash( path ) );
    for ( const string_view text : { string_view{ "one" }, string_view{ "two" } } )
    {
        SW_REQUIRE_TRUE( writeText( path, text ) );
        SW_EXPECT_EQUAL( HashUtil::computeStringHash64( text.data(), text.size(), false ), AssetImportStampUtil::computeFileHash( path ) );
        SW_EXPECT_EQUAL( HashUtil::computeStringHash64( text.data(), text.size(), false ), AssetImportStampUtil::computeFileHash( path ) );
    }
    SW_REQUIRE_TRUE( writeText( path, "" ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, AssetImportStampUtil::computeFileHash( path ) );
}
