// AssetImportStampUtil — 원본이 사라진 스탬프 줄의 결과(파일 · 옆 폴더)를 지우는지. 가짜 임포터가 원본 하나를 결과 파일과 옆 폴더 하나로 쓴다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/AssetImportStamp.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 부모 폴더를 만들고 글을 씁니다. */
    bool writeText( const string& path, string_view text )
    {
        return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, text );
    }

    /** @brief `things_raw/<y>.src` 를 `things/<y>.out` 과 옆 폴더 `things/<y>/side.txt` 로 쓰는 임포터입니다. */
    class FakeThingImporter final : public IRawAssetImporter
    {
    public:
        string_view getRawFolderName() const override { return "things_raw"; }
        string_view getStampHeader() const override { return "fake-things 1"; }
        string_view getImportedLabel() const override { return "thing"; }
        bool        isSourceFile( string_view path ) const override { return FileUtil::hasExtension( path, ".src" ); }
        string      makeImportedPath( string_view sourcePath ) const override { return AssetImportStampUtil::makeImportedPath( sourcePath, "things_raw", "things", ".out" ); }
        uint64      computeSourceHash( string_view sourcePath, string_view /*assetPath*/ ) const override { return AssetImportStampUtil::computeFileHash( sourcePath ); }
        const utf8* findUnsupportedReason( string_view /*sourcePath*/ ) const override { return nullptr; }
        bool        importSource( string_view /*sourcePath*/, string_view importedPath, string_view /*assetPath*/ ) const override
        {
            return writeText( string( importedPath ), "out" ) && writeText( FileUtil::joinPath( FileUtil::removeExtension( importedPath ), "side.txt" ), "side" );
        }
    };
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
    SW_REQUIRE_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr ).isClean() );
    SW_REQUIRE_TRUE( FileUtil::exists( FileUtil::joinPath( root, "pack/things/gone.out" ) ) );
    SW_REQUIRE_TRUE( FileUtil::exists( FileUtil::joinPath( root, "pack/things/gone/side.txt" ) ) );

    SW_REQUIRE_TRUE( FileUtil::removeFile( FileUtil::joinPath( root, "pack/things_raw/gone.src" ) ) );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::ImportStale, nullptr ).isClean() );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::joinPath( root, "pack/things/gone.out" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::joinPath( root, "pack/things/gone" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( FileUtil::joinPath( root, "pack/things/kept.out" ) ) );
    SW_EXPECT_TRUE( AssetImportStampUtil::importAll( root, importer, AssetImportMode::CheckOnly, nullptr ).isClean() );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}
