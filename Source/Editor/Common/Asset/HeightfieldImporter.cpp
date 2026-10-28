#include "pch.h"

#include "Editor/Common/Asset/HeightfieldImporter.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ImageUtil.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetPaths.h"
#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/LayerWeightsData.h"

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
            /// 레이어 4..7 의 곁 파일 접미사(`x_splat.png` 옆의 `x_splat1.png`). 없으면 레이어 4..7 은 0 이다.
            static constexpr string_view kSplatHighSuffix = "_splat1";
            /** @brief 임포트 동작을 바꾸면 올립니다. 모든 스탬프가 어긋나 한 번 다시 임포트합니다. */
            static constexpr uint32 kImporterVersion = 1;
            /** @brief 스플랫 임포트 동작을 바꾸면 올립니다. 스플랫 원본의 스탬프만 어긋납니다. */
            static constexpr uint32 kSplatImporterVersion = 2;

            /** @brief 원본 옆의 구멍 마스크 경로(`x.png` → `x_holes.png`)입니다. */
            static string makeHolePath( string_view sourcePath ) { return FileUtil::removeExtension( sourcePath ) + string( kHoleSuffix ) + ".png"; }

            /** @brief 스플랫 원본 옆의 레이어 4..7 파일 경로(`x_splat.png` → `x_splat1.png`)입니다. */
            static string makeHighSplatPath( string_view sourcePath ) { return FileUtil::removeExtension( sourcePath ) + "1.png"; }

            /**
             * @brief 바뀐 파일(리소스 루트 기준)이 영향을 주는 원본의 경로 후보를 반환합니다. 곁 파일은 그것을 쓰는 원본으로 옮기고(원본 해시에 들어간다), 그 밖에는 그 파일 자신입니다.
             * @details 후보 중 폴더에 없는 것은 일괄 임포트의 범위가 무시한다. 구멍 마스크의 원본은 `.png` 일 수도 `.r16` 일 수도 있어 둘 다 담는다.
             */
            static vector<string> findAffectedSources( string_view relativePath )
            {
                const string   stem = FileUtil::removeExtension( relativePath );
                vector<string> listSource;
                if ( StringUtil::endsWith( stem, kHoleSuffix, true ) )
                {
                    const string base = stem.substr( 0, stem.size() - kHoleSuffix.size() );
                    listSource.push_back( base + ".png" );
                    listSource.push_back( base + ".r16" );
                }
                else if ( StringUtil::endsWith( stem, kSplatHighSuffix, true ) )
                {
                    listSource.push_back( stem.substr( 0, stem.size() - 1 ) + ".png" ); // `x_splat1` → `x_splat`
                }
                else
                {
                    listSource.push_back( string( relativePath ) );
                }
                return listSource;
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

            /** @brief 스플랫 PNG 한 장(RGBA - 채널마다 레이어 하나)을 읽습니다. 디코더는 알파가 없는 원본도 255 로 채워 RGBA 로 펼치므로 원본 채널 수로 가립니다. */
            [[nodiscard]] static bool readSplatImage( string_view path, RawImageData& outImage )
            {
                vector<uint8> bytes;
                if ( FileUtil::readFile( path, bytes ) == false || bytes.empty() )
                {
                    SW_LOG_ERROR( "Failed to read splat source '%#'", path );
                    return false;
                }
                if ( ImageUtil::loadImageFromMemory( bytes.data(), bytes.size(), outImage ) == false )
                    return false;
                if ( outImage._sourceChannelCount != RawImageData::kChannelCount )
                {
                    SW_LOG_ERROR( "Splat source '%#' has %# channels, not RGBA - each channel is one layer weight, so the alpha channel (a layer) is required", path,
                                  outImage._sourceChannelCount );
                    return false;
                }
                return true;
            }

            /** @brief 옆에 둔 구멍 마스크 파일을 읽어 셀 마스크로 씁니다. 없으면 구멍이 없습니다. */
            [[nodiscard]] static bool readHoles( string_view sourcePath, HeightfieldData& inoutData )
            {
                const string holePath = makeHolePath( sourcePath );
                if ( FileUtil::exists( holePath ) == false )
                    return true;
                vector<uint8>  bytes;
                vector<uint16> listSample;
                int32          width{ 0 };
                int32          height{ 0 };
                if ( FileUtil::readFile( holePath, bytes ) == false )
                {
                    SW_LOG_ERROR( "Heightfield hole mask '%#' could not be opened", holePath.c_str() );
                    return false;
                }
                if ( ImageUtil::loadGray16FromMemory( bytes.data(), bytes.size(), listSample, width, height ) == false )
                {
                    SW_LOG_ERROR( "Heightfield hole mask '%#' is not a readable 16-bit grayscale image", holePath.c_str() );
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
                        // 16 비트로 읽었다. 8 비트 기준으로 128 미만이 구멍이다.
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
            uint64 computeSourceHash( string_view sourcePath, string_view assetPath ) const override
            {
                (void)assetPath;
                return HeightfieldImporter::computeSourceHash( sourcePath );
            }
            const utf8* findUnsupportedReason( string_view sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view assetPath ) const override
            {
                (void)assetPath;
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
        return StringUtil::endsWith( stem, Internal::kHoleSuffix, true ) == false && StringUtil::endsWith( stem, Internal::kSplatSuffix, true ) == false &&
               StringUtil::endsWith( stem, Internal::kSplatHighSuffix, true ) == false;
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
        // `x_splat.png` 의 결과는 높이장과 같은 이름 `x.layerweights` 다. 접미사를 제거하고 같은 경로로 바꾼다.
        const string stem = FileUtil::removeExtension( rawPath );
        const string name = stem.substr( 0, stem.size() - Internal::kSplatSuffix.size() ) + ".png";
        return AssetImportStampUtil::makeImportedPath( name, Internal::kRawFolder, Internal::kImportedFolder, LayerWeightsData::kExtension );
    }

    bool HeightfieldImporter::readLayerWeights( string_view sourcePath, LayerWeightsData& outData )
    {
        using Internal = HeightfieldImporterInternal;
        outData        = LayerWeightsData{};
        // 레이어 0..3 은 `x_splat.png`, 레이어 4..7 은 곁 파일 `x_splat1.png`(없으면 0)다. 둘은 같은 크기여야 한다.
        RawImageData lowImage;
        if ( Internal::readSplatImage( sourcePath, lowImage ) == false )
            return false;
        RawImageData highImage;
        const string highPath = Internal::makeHighSplatPath( sourcePath );
        const bool   bHasHigh = FileUtil::exists( highPath );
        if ( bHasHigh )
        {
            if ( Internal::readSplatImage( highPath, highImage ) == false )
                return false;
            if ( highImage._width != lowImage._width || highImage._height != lowImage._height )
            {
                SW_LOG_ERROR( "Splat source '%#' is %#x%# but '%#' is %#x%# - both layer groups must have the same size", sourcePath, lowImage._width, lowImage._height,
                              highPath.c_str(), highImage._width, highImage._height );
                return false;
            }
        }
        outData._width  = static_cast<uint32>( lowImage._width );
        outData._height = static_cast<uint32>( lowImage._height );
        if ( outData._width > LayerWeightsData::kMaxResolution || outData._height > LayerWeightsData::kMaxResolution )
        {
            SW_LOG_ERROR( "Splat source '%#' is %#x%# - at most %# per side", sourcePath, lowImage._width, lowImage._height, LayerWeightsData::kMaxResolution );
            outData = LayerWeightsData{};
            return false;
        }
        const size_t texelCount = static_cast<size_t>( outData._width ) * outData._height;
        outData._bytes.assign( texelCount * LayerWeightsData::kLayerCount, 0u );
        for ( size_t texel = 0; texel < texelCount; ++texel )
        {
            Memory::copy( outData._bytes.data() + texel * LayerWeightsData::kLayerCount, lowImage._bytes.data() + texel * 4, 4 );
            if ( bHasHigh )
                Memory::copy( outData._bytes.data() + texel * LayerWeightsData::kLayerCount + 4, highImage._bytes.data() + texel * 4, 4 );
        }
        if ( outData.isValid() == false )
        {
            SW_LOG_ERROR( "Splat source '%#' is %#x%# - at most %# per side", sourcePath, lowImage._width, lowImage._height, LayerWeightsData::kMaxResolution );
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
        // 바뀐 파일이 영향을 주는 원본만 임포트한다(루트 전체를 훑지 않는다). 스탬프를 적는 길은 명령줄 임포트와 같은 한 곳이다. 구멍 마스크 · 곁 스플랫도 원본 해시에 들어간다.
        const HeightfieldRawImporter importer;
        // 핫 리로드의 임포트 잡이 워커에서 부르므로 작업 시스템을 찾지 않고 직렬로 돈다(워커는 에디터 서비스 범위를 읽지 않는다).
        const AssetImportSummary summary =
            AssetImportStampUtil::importSources( AssetPaths::getRootFolderPath(), importer, HeightfieldImporterInternal::findAffectedSources( relativePath ), nullptr );
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
        uint64                                hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        if ( isSourceLayerWeights( sourcePath ) )
        {
            versionText.appendFormat( "splat=%#;format=%#", Internal::kSplatImporterVersion, LayerWeightsData::kVersion );
            hash = HashUtil::computeStringHash64( versionText.c_str(), versionText.size(), false, hash );
            // 옆에 둔 레이어 4..7 파일(`x_splat1.png`)도 원본이다. 그것만 고쳐도 어긋난 것으로 판정해야 한다.
            vector<uint8> highBytes;
            const string  highPath  = Internal::makeHighSplatPath( sourcePath );
            const bool    bHighRead = FileUtil::exists( highPath ) && FileUtil::readFile( highPath, highBytes ); // 곁 파일은 선택이라 없는 것이 정상이다 - 읽기 오류로 알리지 않는다
            if ( bHighRead && highBytes.empty() == false )
                hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( highBytes.data() ), highBytes.size(), false, hash );
            return hash;
        }
        versionText.appendFormat( "importer=%#;format=%#", Internal::kImporterVersion, HeightfieldData::kVersion );
        hash = HashUtil::computeStringHash64( versionText.c_str(), versionText.size(), false, hash );
        // 옆에 둔 구멍 마스크도 원본이다. 그것만 고쳐도 어긋난 것으로 판정해야 한다.
        vector<uint8> holeBytes;
        const string  holePath  = Internal::makeHolePath( sourcePath );
        const bool    bHoleRead = FileUtil::exists( holePath ) && FileUtil::readFile( holePath, holeBytes ); // 구멍 마스크도 선택이라 없는 것이 정상이다
        if ( bHoleRead && holeBytes.empty() == false )
            hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( holeBytes.data() ), holeBytes.size(), false, hash );
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
        return AssetImportStampUtil::importAll( resourceRoot, importer, mode, editor::findSubsystem<TaskManager>() ); // 선택: 없으면 이 스레드가 직렬로 임포트한다(계약)
    }
} // namespace sw::editor
