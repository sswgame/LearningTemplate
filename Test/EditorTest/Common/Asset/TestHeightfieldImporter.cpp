#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/HeightfieldImporter.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/LayerWeightsData.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// HeightfieldImporterTest — 16 비트 PNG · `.r16` 원본과 곁 구멍 마스크를 `.heightfield` 로, RGBA 스플랫을 `.layerweights` 로 임포트한다(값 · 구멍 · 스탬프).

namespace
{
    struct TestHeightfieldImporterInternal
    {
        /** @brief 3 × 3, 16 비트 회색 PNG — 행: (0, 1000, 65535) (300, 40000, 2) (7, 8, 60000). */
        static constexpr uint8 kArrGray16Png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
                                                   0x00, 0x03, 0x10, 0x00, 0x00, 0x00, 0x00, 0x23, 0xD3, 0x36, 0x20, 0x00, 0x00, 0x00, 0x1D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63,
                                                   0x60, 0x60, 0x60, 0x7E, 0xF1, 0xFF, 0x3F, 0x03, 0xA3, 0xCE, 0x1C, 0x07, 0x06, 0x26, 0x06, 0x06, 0x76, 0x06, 0x8E, 0x57, 0x09, 0x00,
                                                   0x3C, 0x64, 0x05, 0x4E, 0xC6, 0xE8, 0x17, 0xA9, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
        /** @brief 2 × 2, 8 비트 구멍 마스크 — 칸 (1, 0) 만 0(구멍). */
        static constexpr uint8 kArrHolePng[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
                                                 0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x57, 0xDD, 0x52, 0xF8, 0x00, 0x00, 0x00, 0x0E, 0x49, 0x44, 0x41,
                                                 0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0xFF, 0x3F, 0x00, 0x07, 0xFE, 0x02, 0xFE, 0x4A, 0xD1, 0x84, 0xBF, 0x00,
                                                 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
        /** @brief 2 × 2, 8 비트 RGBA 스플랫 — 행: (255, 0, 0, 0) (0, 255, 0, 0) / (0, 0, 255, 0) (10, 20, 30, 195). */
        static constexpr uint8 kArrSplatRgbaPng[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
                                                      0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41,
                                                      0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0x00, 0x04, 0x60, 0x02, 0x48, 0x72, 0x89, 0xC8, 0x1D, 0x06, 0x00, 0x24, 0x52, 0x03,
                                                      0xFD, 0xFF, 0x9D, 0xFA, 0x04, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
        /** @brief 같은 그림의 RGB(알파 없음) PNG — 스플랫 원본으로 받지 않는다. */
        static constexpr uint8 kArrSplatRgbPng[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
                                                     0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xFD, 0xD4, 0x9A, 0x73, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41,
                                                     0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0xC0, 0xC0, 0x00, 0xC2, 0x0C, 0xFF, 0xB9, 0x44, 0xE4, 0x00, 0x1A, 0x58, 0x03, 0x3A,
                                                     0xE2, 0x92, 0x6E, 0xD9, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

        [[nodiscard]] static bool writeBytes( const sw::string& path, const uint8* pData, size_t size )
        {
            return sw::FileUtil::ensureParentDirectoryExists( path ) && sw::FileUtil::writeFile( path, pData, size );
        }
    };
} // namespace

/**
 * @brief [HeightfieldImporterTest] 16 비트 PNG 는 값 그대로, `.r16` 은 작은 엔디언 정사각형으로 읽고, 곁 구멍 마스크가 칸 구멍이 된다
 */
SW_TEST_CASE( HeightfieldImporterTest, ReadsSixteenBitSourcesAndHoles )
{
    using Internal          = TestHeightfieldImporterInternal;
    const sw::string folder = test::makeTempDirectory( "heightfield_import_read" );
    const sw::string png    = sw::FileUtil::joinPath( folder, "hill.png" );
    SW_ASSERT_TRUE( Internal::writeBytes( png, Internal::kArrGray16Png, sizeof( Internal::kArrGray16Png ) ) );
    SW_ASSERT_TRUE( Internal::writeBytes( sw::FileUtil::joinPath( folder, "hill_holes.png" ), Internal::kArrHolePng, sizeof( Internal::kArrHolePng ) ) );

    sw::HeightfieldData data;
    SW_ASSERT_TRUE( sw::editor::HeightfieldImporter::readHeightfield( png, data ) );
    SW_EXPECT_EQUAL( 3u, data._resolution );
    SW_EXPECT_EQUAL( static_cast<uint16>( 65535 ), data.getSample( 2, 0 ) );
    SW_EXPECT_EQUAL( static_cast<uint16>( 40000 ), data.getSample( 1, 1 ) );
    SW_EXPECT_EQUAL( static_cast<uint16>( 7 ), data.getSample( 0, 2 ) );
    SW_EXPECT_TRUE( data.isHoleCell( 1, 0 ) );
    SW_EXPECT_FALSE( data.isHoleCell( 0, 0 ) );
    SW_EXPECT_FALSE( data.isHoleCell( 1, 1 ) );

    // .r16: 2 × 2 = (1, 513), (65280, 2)
    const uint8      arrRaw[] = { 0x01, 0x00, 0x01, 0x02, 0x00, 0xFF, 0x02, 0x00 };
    const sw::string raw      = sw::FileUtil::joinPath( folder, "flat.r16" );
    SW_ASSERT_TRUE( Internal::writeBytes( raw, arrRaw, sizeof( arrRaw ) ) );
    SW_ASSERT_TRUE( sw::editor::HeightfieldImporter::readHeightfield( raw, data ) );
    SW_EXPECT_EQUAL( 2u, data._resolution );
    SW_EXPECT_EQUAL( static_cast<uint16>( 513 ), data.getSample( 1, 0 ) );
    SW_EXPECT_EQUAL( static_cast<uint16>( 65280 ), data.getSample( 0, 1 ) );
    SW_EXPECT_TRUE( data._listHoleCell.empty() );

    SW_EXPECT_TRUE( sw::editor::HeightfieldImporter::isSourceHeightfield( png ) );
    SW_EXPECT_FALSE( sw::editor::HeightfieldImporter::isSourceHeightfield( sw::FileUtil::joinPath( folder, "hill_holes.png" ) ) );
}

/**
 * @brief [HeightfieldImporterTest] 일괄 임포트는 `heightfields/` 에 쓰고 스탬프를 남긴다 · 곁 구멍 마스크만 바꿔도 어긋남이다
 */
SW_TEST_CASE( HeightfieldImporterTest, StampTracksSourceAndHoleMask )
{
    using Internal                = TestHeightfieldImporterInternal;
    const sw::string resourceRoot = test::makeTempDirectory( "heightfield_import_resource" );
    const sw::string rawFolder    = sw::FileUtil::joinPath( resourceRoot, "game/test/heightfields_raw" );
    SW_ASSERT_TRUE( Internal::writeBytes( sw::FileUtil::joinPath( rawFolder, "hill.png" ), Internal::kArrGray16Png, sizeof( Internal::kArrGray16Png ) ) );

    sw::editor::AssetImportSummary summary = sw::editor::HeightfieldImporter::importAllHeightfields( resourceRoot, sw::editor::AssetImportMode::CheckOnly );
    SW_EXPECT_EQUAL( size_t( 1 ), summary._listProblem.size() );
    summary = sw::editor::HeightfieldImporter::importAllHeightfields( resourceRoot, sw::editor::AssetImportMode::ImportStale );
    SW_EXPECT_EQUAL( 1u, summary._importedCount );
    const sw::string imported = sw::FileUtil::joinPath( resourceRoot, "game/test/heightfields/hill.heightfield" );
    SW_EXPECT_TRUE( sw::FileUtil::exists( imported ) );
    SW_EXPECT_TRUE( sw::editor::HeightfieldImporter::importAllHeightfields( resourceRoot, sw::editor::AssetImportMode::CheckOnly ).isClean() );

    // 곁 구멍 마스크가 생기면 다시 임포트할 것이 된다.
    SW_ASSERT_TRUE( Internal::writeBytes( sw::FileUtil::joinPath( rawFolder, "hill_holes.png" ), Internal::kArrHolePng, sizeof( Internal::kArrHolePng ) ) );
    SW_EXPECT_EQUAL( size_t( 1 ), sw::editor::HeightfieldImporter::importAllHeightfields( resourceRoot, sw::editor::AssetImportMode::CheckOnly )._listProblem.size() );
    SW_EXPECT_EQUAL( 1u, sw::editor::HeightfieldImporter::importAllHeightfields( resourceRoot, sw::editor::AssetImportMode::ImportStale )._importedCount );
    sw::HeightfieldData data;
    sw::vector<uint8>   bytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( imported, bytes ) );
    SW_ASSERT_TRUE( data.loadFromMemory( bytes, imported ) );
    SW_EXPECT_TRUE( data.isHoleCell( 1, 0 ) );
}

/**
 * @brief [HeightfieldImporterTest] 바깥 도구의 스플랫(`x_splat.png`, RGBA)은 `heightfields/x.layerweights` 로 바이트 그대로 임포트한다 · 알파 없는 PNG 는 거절한다
 * @details 스플랫은 높이장의 곁 파일이 아니라 스탬프 줄을 따로 갖는 원본이다 — 높이장 원본이 아니고(`isSourceHeightfield`), 스플랫만 바꿔도 어긋남이다.
 */
SW_TEST_CASE( HeightfieldImporterTest, ImportsSplatSourceAsLayerWeights )
{
    using Internal                = TestHeightfieldImporterInternal;
    const sw::string resourceRoot = test::makeTempDirectory( "heightfield_import_splat" );
    const sw::string rawFolder    = sw::FileUtil::joinPath( resourceRoot, "game/test/heightfields_raw" );
    const sw::string splatSource  = sw::FileUtil::joinPath( rawFolder, "hill_splat.png" );
    SW_ASSERT_TRUE( Internal::writeBytes( sw::FileUtil::joinPath( rawFolder, "hill.png" ), Internal::kArrGray16Png, sizeof( Internal::kArrGray16Png ) ) );
    SW_ASSERT_TRUE( Internal::writeBytes( splatSource, Internal::kArrSplatRgbaPng, sizeof( Internal::kArrSplatRgbaPng ) ) );

    SW_EXPECT_TRUE( sw::editor::HeightfieldImporter::isSourceLayerWeights( splatSource ) );
    SW_EXPECT_FALSE( sw::editor::HeightfieldImporter::isSourceHeightfield( splatSource ) );
    SW_EXPECT_TRUE( sw::editor::HeightfieldImporter::isRawHeightfieldPath( splatSource ) );

    sw::editor::AssetImportSummary summary = sw::editor::HeightfieldImporter::importAllHeightfields( resourceRoot, sw::editor::AssetImportMode::ImportStale );
    SW_EXPECT_TRUE( summary.isClean() );
    SW_EXPECT_EQUAL( 2u, summary._importedCount );
    const sw::string imported = sw::FileUtil::joinPath( resourceRoot, "game/test/heightfields/hill.layerweights" );
    SW_EXPECT_TRUE( sw::FileUtil::normalizeSeparators( imported ) ==
                    sw::FileUtil::normalizeSeparators( sw::editor::HeightfieldImporter::makeImportedLayerWeightsPath( splatSource ) ) );

    sw::LayerWeightsData data;
    sw::vector<uint8>    bytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( imported, bytes ) );
    SW_ASSERT_TRUE( data.loadFromMemory( bytes, imported ) );
    SW_EXPECT_EQUAL( 2u, data._width );
    SW_EXPECT_EQUAL( 2u, data._height );
    const uint8 arrExpected[] = { 255, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255, 0, 10, 20, 30, 195 };
    SW_ASSERT_EQUAL( sizeof( arrExpected ), data._bytes.size() );
    for ( size_t index = 0; index < sizeof( arrExpected ); ++index )
    {
        SW_EXPECT_EQUAL( arrExpected[index], data._bytes[index] );
    }
    SW_EXPECT_TRUE( sw::editor::HeightfieldImporter::importAllHeightfields( resourceRoot, sw::editor::AssetImportMode::CheckOnly ).isClean() );

    // 알파 없는 스플랫은 읽지 않는다(디코더가 알파를 255 로 채우면 레이어 3 이 말없이 가득 찬다). 원본이 바뀌었으니 어긋남이고 임포트는 실패한다.
    SW_ASSERT_TRUE( Internal::writeBytes( splatSource, Internal::kArrSplatRgbPng, sizeof( Internal::kArrSplatRgbPng ) ) );
    sw::LayerWeightsData rejected;
    SW_EXPECT_FALSE( sw::editor::HeightfieldImporter::readLayerWeights( splatSource, rejected ) );
    summary = sw::editor::HeightfieldImporter::importAllHeightfields( resourceRoot, sw::editor::AssetImportMode::ImportStale );
    SW_EXPECT_EQUAL( size_t( 1 ), summary._listProblem.size() );
    SW_EXPECT_EQUAL( 0u, summary._importedCount );
}

/**
 * @brief [HeightfieldImporterTest] 저장소의 모든 높이장 원본이 커밋된 `.heightfield` 와 스탬프로 맞는다
 * @details 지면 `App --import-heightfields` 로 임포트하고 `.heightfield` 와 `heightfields_raw/import.stamp` 를 함께 커밋한다.
 */
SW_TEST_CASE( HeightfieldImporterTest, RepositoryRawHeightfieldsMatchTheirAssets )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::editor::AssetImportSummary summary =
        sw::editor::HeightfieldImporter::importAllHeightfields( sw::ResourcePaths::getRootFolderPath(), sw::editor::AssetImportMode::CheckOnly );
    sw::string problemText;
    for ( const sw::string& problem : summary._listProblem )
    {
        problemText += problem;
        problemText += "\n";
    }
    SW_EXPECT_TRUE_MSG( summary.isClean(), problemText.c_str() );
    SW_EXPECT_EQUAL( 0u, summary._importedCount );
}
