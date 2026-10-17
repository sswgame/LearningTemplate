#include "pch.h"

#include "Editor/Common/Asset/HeightfieldImporter.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ImageUtil.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/LayerWeightsData.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    SW_LOG_CALLER( "HeightfieldImporter" );

    namespace
    {
        struct HeightfieldImporterInternal
        {
            static constexpr string_view kRawFolder         = "heightfields_raw";
            static constexpr string_view kImportedFolder    = "heightfields";
            static constexpr string_view kImportStampHeader = "SWHEIGHTFIELDIMPORT 1";
            static constexpr string_view kHoleSuffix        = "_holes";
            static constexpr string_view kSplatSuffix       = "_splat";
            /** @brief 임포트 동작을 바꾸면 올린다 — 모든 스탬프가 어긋남이 되어 한 번 다시 임포트한다. */
            static constexpr uint32 kImporterVersion = 1;
            /** @brief 스플랫 임포트 동작을 바꾸면 올린다(스플랫 원본의 스탬프만 어긋난다). */
            static constexpr uint32 kSplatImporterVersion = 1;
            /** @brief PNG 머리(IHDR)의 색 형식 바이트 위치와 RGBA 값입니다(PNG 명세 11.2.2). */
            static constexpr size_t kPngColorTypeOffset = 25;
            static constexpr uint8  kPngColorTypeRgba   = 6;

            /** @brief 원본 옆의 구멍 마스크 경로(`x.png` → `x_holes.png`)입니다. */
            static string makeHolePath( string_view sourcePath ) { return FileUtil::removeExtension( sourcePath ) + string( kHoleSuffix ) + ".png"; }

            /** @brief PNG 이고 알파 채널을 가졌으면 true 입니다. 디코더는 알파가 없으면 255 로 채우므로 디코딩 전에 머리로 가린다. */
            static bool isRgbaPng( const vector<uint8>& bytes )
            {
                constexpr uint8 kArrPngSignature[] = { 0x89, 'P', 'N', 'G' };
                if ( bytes.size() <= kPngColorTypeOffset )
                    return false;
                for ( size_t index = 0; index < SW_COUNT_OF( kArrPngSignature ); ++index )
                {
                    if ( bytes[index] != kArrPngSignature[index] )
                        return false;
                }
                return bytes[kPngColorTypeOffset] == kPngColorTypeRgba;
            }

            [[nodiscard]] static bool readRaw16( const vector<uint8>& bytes, string_view sourcePath, HeightfieldData& outData )
            {
                const size_t sampleCount          = bytes.size() / 2;
                const uint32 side                 = static_cast<uint32>( MathUtil::round( MathUtil::sqrt( static_cast<float64>( sampleCount ) ) ) );
                const bool   bInvalidSampleLayout = bytes.size() % 2 != 0 || static_cast<size_t>( side ) * side != sampleCount || side < 2;
                if ( bInvalidSampleLayout )
                {
                    SW_LOG_ERROR( "Heightfield '%#' (.r16) is %# bytes - expected a square of 16-bit samples", sourcePath, static_cast<uint64>( bytes.size() ) );
                    return false;
                }
                outData._resolution = side;
                outData._listHeight.resize( sampleCount );
                for ( size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
                {
                    outData._listHeight[sampleIndex] = static_cast<uint16>( bytes[sampleIndex * 2] | ( bytes[sampleIndex * 2 + 1] << 8 ) );
                }
                return true;
            }

            /** @brief 곁 구멍 마스크를 읽어 칸 마스크로 씁니다. 없으면 구멍이 없습니다. */
            [[nodiscard]] static bool readHoles( string_view sourcePath, HeightfieldData& inoutData )
            {
                const string holePath = makeHolePath( sourcePath );
                if ( FileUtil::exists( holePath ) == false )
                    return true;
                vector<uint8>  bytes;
                vector<uint16> listSample;
                int32          width{ 0 };
                int32          height{ 0 };
                if ( FileUtil::readFile( holePath, bytes ) == false || ImageUtil::loadGray16FromMemory( bytes.data(), bytes.size(), listSample, width, height ) == false )
                {
                    SW_LOG_ERROR( "Heightfield hole mask '%#' could not be read", holePath.c_str() );
                    return false;
                }
                const uint32 cells       = inoutData._resolution - 1;
                const bool   bCellSize   = static_cast<uint32>( width ) == cells && static_cast<uint32>( height ) == cells;
                const bool   bSampleSize = static_cast<uint32>( width ) == inoutData._resolution && static_cast<uint32>( height ) == inoutData._resolution;
                if ( bCellSize == false && bSampleSize == false )
                {
                    SW_LOG_ERROR( "Heightfield hole mask '%#' is %#x%# - expected %#x%# (cells) or %#x%# (samples)", holePath.c_str(), width, height, cells, cells,
                                  inoutData._resolution, inoutData._resolution );
                    return false;
                }
                inoutData._listHoleCell.assign( static_cast<size_t>( cells ) * cells, 0u );
                bool bAnyHole = false;
                for ( uint32 cellZ = 0; cellZ < cells; ++cellZ )
                {
                    for ( uint32 cellX = 0; cellX < cells; ++cellX )
                    {
                        // 16 비트로 읽었다 — 128 미만(8 비트 기준)이 구멍이다.
                        const bool bHole                                                      = listSample[static_cast<size_t>( cellZ ) * static_cast<uint32>( width ) + cellX] < 128u * 257u;
                        inoutData._listHoleCell[static_cast<size_t>( cellZ ) * cells + cellX] = bHole ? 1u : 0u;
                        bAnyHole                                                              = bAnyHole || bHole;
                    }
                }
                if ( bAnyHole == false )
                    inoutData._listHoleCell.clear();
                return true;
            }
        };

        /**
         * @class HeightfieldRawImporter
         * @brief 높이장 원본이 일괄 임포트에 답하는 것들입니다.
         */
        class HeightfieldRawImporter final : public IRawAssetImporter
        {
        public:
            string_view getRawFolderName() const override { return HeightfieldImporterInternal::kRawFolder; }
            string_view getStampHeader() const override { return HeightfieldImporterInternal::kImportStampHeader; }
            string_view getImportedLabel() const override { return "지형 에셋"; }
            bool        isSourceFile( string_view path ) const override
            {
                return HeightfieldImporter::isSourceHeightfield( path ) || HeightfieldImporter::isSourceLayerWeights( path );
            }
            string makeImportedPath( string_view sourcePath ) const override
            {
                if ( HeightfieldImporter::isSourceLayerWeights( sourcePath ) )
                    return HeightfieldImporter::makeImportedLayerWeightsPath( sourcePath );
                return HeightfieldImporter::makeImportedHeightfieldPath( sourcePath );
            }
            uint64 computeSourceHash( string_view sourcePath, string_view resourcePath ) const override
            {
                (void)resourcePath;
                return HeightfieldImporter::computeSourceHash( sourcePath );
            }
            const utf8* findUnsupportedReason( string_view sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view resourcePath ) const override
            {
                (void)resourcePath;
                if ( HeightfieldImporter::isSourceLayerWeights( sourcePath ) )
                {
                    LayerWeightsData data;
                    if ( HeightfieldImporter::readLayerWeights( sourcePath, data ) == false )
                        return false;
                    return FileUtil::ensureParentDirectoryExists( importedPath ) && data.saveToFile( importedPath );
                }
                return HeightfieldImporter::importHeightfield( sourcePath, importedPath );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool HeightfieldImporter::isSourceHeightfield( string_view path )
    {
        using Internal = HeightfieldImporterInternal;
        if ( FileUtil::hasAnyExtension( path, { ".png", ".r16" } ) == false )
            return false;
        const string stem = FileUtil::removeExtension( path );
        return StringUtil::endsWith( stem, Internal::kHoleSuffix, true ) == false && StringUtil::endsWith( stem, Internal::kSplatSuffix, true ) == false;
    }

    bool HeightfieldImporter::isSourceLayerWeights( string_view path )
    {
        using Internal = HeightfieldImporterInternal;
        return FileUtil::hasExtension( path, ".png" ) && StringUtil::endsWith( FileUtil::removeExtension( path ), Internal::kSplatSuffix, true );
    }

    string HeightfieldImporter::makeImportedLayerWeightsPath( string_view rawPath )
    {
        using Internal = HeightfieldImporterInternal;
        if ( isSourceLayerWeights( rawPath ) == false )
            return {};
        // `x_splat.png` 의 결과는 높이장과 같은 이름 `x.layerweights` 다 — 접미사를 떼고 같은 길로 바꾼다.
        const string stem = FileUtil::removeExtension( rawPath );
        const string name = stem.substr( 0, stem.size() - Internal::kSplatSuffix.size() ) + ".png";
        return AssetImportStampUtil::makeImportedPath( name, Internal::kRawFolder, Internal::kImportedFolder, LayerWeightsData::kExtension );
    }

    bool HeightfieldImporter::readLayerWeights( string_view sourcePath, LayerWeightsData& outData )
    {
        using Internal = HeightfieldImporterInternal;
        outData        = LayerWeightsData{};
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read splat source '%#'", sourcePath );
            return false;
        }
        if ( Internal::isRgbaPng( bytes ) == false )
        {
            SW_LOG_ERROR( "Splat source '%#' is not an RGBA PNG - each channel is one layer weight, so the alpha channel (layer 3) is required", sourcePath );
            return false;
        }
        RawImageData image;
        if ( ImageUtil::loadImageFromMemory( bytes.data(), bytes.size(), image ) == false )
            return false;
        outData._width  = static_cast<uint32>( image._width );
        outData._height = static_cast<uint32>( image._height );
        outData._bytes  = std::move( image._bytes );
        if ( outData.isValid() == false )
        {
            SW_LOG_ERROR( "Splat source '%#' is %#x%# - at most %# per side", sourcePath, image._width, image._height, LayerWeightsData::kMaxResolution );
            outData = LayerWeightsData{};
            return false;
        }
        return true;
    }

    bool HeightfieldImporter::isRawHeightfieldPath( string_view path )
    {
        if ( FileUtil::hasAnyExtension( path, { ".png", ".r16" } ) == false )
            return false;
        return makeImportedHeightfieldPath( path ).empty() == false;
    }

    bool HeightfieldImporter::importChangedSourceHeightfield( string_view relativePath )
    {
        if ( isRawHeightfieldPath( relativePath ) == false )
            return false;
        // 바뀐 파일 하나가 아니라 어긋난 것 전부를 일괄 임포트로 — 스탬프를 적는 길이 하나여야 명령줄 임포트와 같은 판정을 받는다(구멍 마스크도 원본 해시에 든다).
        const AssetImportSummary summary = importAllHeightfields( ResourcePaths::getRootFolderPath(), AssetImportMode::ImportStale );
        for ( const string& problem : summary._listProblem )
        {
            SW_LOG_ERROR( "Heightfield import failed: %#", problem.c_str() );
        }
        SW_LOG_INFO( "Heightfield import: %# changed -> %# imported", relativePath, summary._importedCount );
        return true;
    }

    string HeightfieldImporter::makeImportedHeightfieldPath( string_view rawPath )
    {
        using Internal = HeightfieldImporterInternal;
        return AssetImportStampUtil::makeImportedPath( rawPath, Internal::kRawFolder, Internal::kImportedFolder, HeightfieldData::kExtension );
    }

    uint64 HeightfieldImporter::computeSourceHash( string_view sourcePath )
    {
        using Internal = HeightfieldImporterInternal;
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
            return 0;
        StringBuilder<constant::kMaxBuffer64> versionText;
        uint64                                hash = StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        if ( isSourceLayerWeights( sourcePath ) )
        {
            versionText.appendFormat( "splat=%#;format=%#", Internal::kSplatImporterVersion, LayerWeightsData::kVersion );
            return StringUtil::computeHash64( versionText.c_str(), versionText.size(), false, hash );
        }
        versionText.appendFormat( "importer=%#;format=%#", Internal::kImporterVersion, HeightfieldData::kVersion );
        hash = StringUtil::computeHash64( versionText.c_str(), versionText.size(), false, hash );
        // 곁 구멍 마스크도 원본이다 — 그것만 고쳐도 어긋남이어야 한다.
        vector<uint8> holeBytes;
        if ( FileUtil::readFile( Internal::makeHolePath( sourcePath ), holeBytes ) && holeBytes.empty() == false )
            hash = StringUtil::computeHash64( reinterpret_cast<const utf8*>( holeBytes.data() ), holeBytes.size(), false, hash );
        return hash;
    }

    bool HeightfieldImporter::readHeightfield( string_view sourcePath, HeightfieldData& outData )
    {
        using Internal = HeightfieldImporterInternal;
        outData        = HeightfieldData{};
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read heightfield source '%#'", sourcePath );
            return false;
        }
        if ( FileUtil::hasExtension( sourcePath, ".r16" ) )
        {
            if ( Internal::readRaw16( bytes, sourcePath, outData ) == false )
                return false;
        }
        else
        {
            int32 width{ 0 };
            int32 height{ 0 };
            if ( ImageUtil::loadGray16FromMemory( bytes.data(), bytes.size(), outData._listHeight, width, height ) == false )
                return false;
            if ( width != height || width < 2 )
            {
                SW_LOG_ERROR( "Heightfield '%#' is %#x%# - it must be square and at least 2x2", sourcePath, width, height );
                outData = HeightfieldData{};
                return false;
            }
            outData._resolution = static_cast<uint32>( width );
        }
        if ( outData._resolution > HeightfieldData::kMaxResolution )
        {
            SW_LOG_ERROR( "Heightfield '%#' has resolution %# - at most %#", sourcePath, outData._resolution, HeightfieldData::kMaxResolution );
            return false;
        }
        return Internal::readHoles( sourcePath, outData );
    }

    bool HeightfieldImporter::importHeightfield( string_view sourcePath, string_view outputPath )
    {
        HeightfieldData data;
        if ( readHeightfield( sourcePath, data ) == false )
            return false;
        if ( FileUtil::ensureParentDirectoryExists( outputPath ) == false )
        {
            SW_LOG_ERROR( "Cannot create the folder for '%#'", outputPath );
            return false;
        }
        return data.saveToFile( outputPath );
    }

    AssetImportSummary HeightfieldImporter::importAllHeightfields( string_view resourceRoot, AssetImportMode mode )
    {
        const HeightfieldRawImporter importer;
        return AssetImportStampUtil::importAll( resourceRoot, importer, mode );
    }
} // namespace sw::editor
