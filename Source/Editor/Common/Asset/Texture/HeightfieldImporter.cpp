#include "pch.h"

#include "Editor/Common/Asset/Texture/HeightfieldImporter.h"

#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/AssetSourceLocator.h"
#include "Editor/Common/Asset/Texture/RawImageData.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/LayerWeightsAsset.h"

namespace sw::editor
{
    namespace
    {
        struct HeightfieldImporterInternal
        {
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
            static AbsolutePath makeHolePath( const AbsolutePath& sourcePath )
            {
                return sourcePath.removeExtension().appendSuffix( kHoleSuffix ).appendSuffix( ".png" );
            }

            /** @brief 스플랫 원본 옆의 레이어 4..7 파일 경로(`x_splat.png` → `x_splat1.png`)입니다. */
            static AbsolutePath makeHighSplatPath( const AbsolutePath& sourcePath )
            {
                return sourcePath.removeExtension().appendSuffix( "1.png" );
            }

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
                    const string_view                        base = string_view( stem ).substr( 0, stem.size() - kHoleSuffix.size() );
                    StringBuilder<constant::buffer::kMax256> pathBuilder;
                    pathBuilder.appendFormat( "%#.png", base );
                    listSource.push_back( string( pathBuilder.view() ) );
                    pathBuilder.clear();
                    pathBuilder.appendFormat( "%#.r16", base );
                    listSource.push_back( string( pathBuilder.view() ) );
                }
                else if ( StringUtil::endsWith( stem, kSplatHighSuffix, true ) )
                {
                    StringBuilder<constant::buffer::kMax256> pathBuilder;
                    pathBuilder.appendFormat( "%#.png", string_view( stem ).substr( 0, stem.size() - 1 ) ); // `x_splat1` → `x_splat`
                    listSource.push_back( string( pathBuilder.view() ) );
                }
                else
                {
                    listSource.push_back( string( relativePath ) );
                }
                return listSource;
            }

            [[nodiscard]] static bool readRaw16( const vector<uint8>& bytes, string_view sourcePath, HeightfieldAsset& outData )
            {
                const size_t sampleCount          = bytes.size() / 2;
                const uint32 side                 = static_cast<uint32>( MathUtil::round( MathUtil::sqrt( static_cast<float64>( sampleCount ) ) ) );
                const bool   bInvalidSampleLayout = bytes.size() % 2 != 0 || static_cast<size_t>( side ) * side != sampleCount || side < 2;
                if ( bInvalidSampleLayout )
                {
                    SW_LOG_ERROR_ID( "HeightfieldImporter.HeightfieldR16BytesExpected", "Heightfield '%#' (.r16) is %# bytes - expected a square of 16-bit samples", sourcePath, static_cast<uint64>( bytes.size() ) );
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
            [[nodiscard]] static bool readSplatImage( const AbsolutePath& path, RawImageData& outImage )
            {
                vector<uint8> bytes;
                if ( AssetSourceLocator::readBytes( path, bytes ) == false )
                {
                    SW_LOG_ERROR_ID( "HeightfieldImporter.FailedReadSplatSource", "Failed to read splat source '%#'", path.str() );
                    return false;
                }
                if ( RawImageData::loadImageFromMemory( bytes.data(), bytes.size(), outImage ) == false )
                    return false;
                if ( outImage._sourceChannelCount != RawImageData::kChannelCount )
                {
                    SW_LOG_ERROR_ID( "HeightfieldImporter.SplatSourceChannelsNot", "Splat source '%#' has %# channels, not RGBA - each channel is one layer weight, so the alpha channel (a layer) is required", path.str(),
                                     outImage._sourceChannelCount );
                    return false;
                }
                return true;
            }

            /** @brief 옆에 둔 구멍 마스크 파일을 읽어 셀 마스크로 씁니다. 없으면 구멍이 없습니다. */
            [[nodiscard]] static bool readHoles( const AbsolutePath& sourcePath, HeightfieldAsset& inoutData )
            {
                const AbsolutePath holePath = makeHolePath( sourcePath );
                if ( FileUtil::exists( holePath ) == false )
                    return true;
                vector<uint8>  bytes;
                vector<uint16> listSample;
                int32          width{ 0 };
                int32          height{ 0 };
                if ( FileUtil::readFile( holePath, bytes ) == false )
                {
                    SW_LOG_ERROR_ID( "HeightfieldImporter.HeightfieldHoleMaskCould", "Heightfield hole mask '%#' could not be opened", holePath.c_str() );
                    return false;
                }
                if ( RawImageData::loadGray16FromMemory( bytes.data(), bytes.size(), listSample, width, height ) == false )
                {
                    SW_LOG_ERROR_ID( "HeightfieldImporter.HeightfieldHoleMaskNot", "Heightfield hole mask '%#' is not a readable 16-bit grayscale image", holePath.c_str() );
                    return false;
                }
                const uint32 cells       = inoutData._resolution - 1;
                const bool   bCellSize   = static_cast<uint32>( width ) == cells && static_cast<uint32>( height ) == cells;
                const bool   bSampleSize = static_cast<uint32>( width ) == inoutData._resolution && static_cast<uint32>( height ) == inoutData._resolution;
                if ( bCellSize == false && bSampleSize == false )
                {
                    SW_LOG_ERROR_ID( "HeightfieldImporter.HeightfieldHoleMaskX", "Heightfield hole mask '%#' is %#x%# - expected %#x%# (cells) or %#x%# (samples)", holePath.c_str(), width, height, cells, cells,
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
            string_view getRawFolderName() const override { return HeightfieldImporter::kRawFolder; }
            string_view getStampHeader() const override { return HeightfieldImporterInternal::kImportStampHeader; }
            string_view getImportedLabel() const override { return "지형 에셋"; }
            uint64      findCodeFingerprint() const override { return CookCodeFingerprint::find( "HeightfieldImport" ); }
            bool        isSourceFile( string_view path ) const override
            {
                return HeightfieldImporter::isSourceHeightfield( path ) || HeightfieldImporter::isSourceLayerWeights( path );
            }
            AbsolutePath makeImportedPath( const AbsolutePath& sourcePath ) const override
            {
                if ( HeightfieldImporter::isSourceLayerWeights( sourcePath.str() ) )
                    return HeightfieldImporter::makeImportedLayerWeightsPath( sourcePath );
                return HeightfieldImporter::makeImportedHeightfieldPath( sourcePath );
            }
            uint64 computeSourceHash( const AbsolutePath& sourcePath, string_view assetPath ) const override
            {
                (void)assetPath;
                return HeightfieldImporter::computeSourceHash( sourcePath );
            }
            const utf8* findUnsupportedReason( const AbsolutePath& sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            [[nodiscard]] bool importSource( const AbsolutePath& sourcePath, const AbsolutePath& importedPath, string_view assetPath ) const override
            {
                (void)assetPath;
                if ( HeightfieldImporter::isSourceLayerWeights( sourcePath.str() ) )
                {
                    LayerWeightsAsset data;
                    if ( HeightfieldImporter::readLayerWeights( sourcePath, data ) == false )
                        return false;
                    return data.saveToFile( importedPath );
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
        if ( AssetIdentityRules::getProjectRules().isImportSource( kRawFolder, path ) == false )
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

    AbsolutePath HeightfieldImporter::makeImportedLayerWeightsPath( const AbsolutePath& rawPath )
    {
        using Internal = HeightfieldImporterInternal;
        if ( isSourceLayerWeights( rawPath.str() ) == false )
            return {};
        // `x_splat.png` 의 결과는 높이장과 같은 이름 `x.layerweights` 다. 접미사를 제거하고 같은 경로로 바꾼다.
        const string                             stem = FileUtil::removeExtension( FileUtil::getFileNamePart( rawPath.str() ) );
        StringBuilder<constant::buffer::kMax256> nameBuilder;
        nameBuilder.appendFormat( "%#.png", string_view( stem ).substr( 0, stem.size() - Internal::kSplatSuffix.size() ) );
        return AssetImportStampUtil::makeImportedPath( rawPath.getParent().join( nameBuilder.view() ), kRawFolder, Internal::kImportedFolder, LayerWeightsAsset::kExtension );
    }

    bool HeightfieldImporter::readLayerWeights( const AbsolutePath& sourcePath, LayerWeightsAsset& outData )
    {
        using Internal = HeightfieldImporterInternal;
        outData        = LayerWeightsAsset{};
        // 레이어 0..3 은 `x_splat.png`, 레이어 4..7 은 곁 파일 `x_splat1.png`(없으면 0)다. 둘은 같은 크기여야 한다.
        RawImageData lowImage;
        if ( Internal::readSplatImage( sourcePath, lowImage ) == false )
            return false;
        RawImageData       highImage;
        const AbsolutePath highPath = Internal::makeHighSplatPath( sourcePath );
        const bool         bHasHigh = FileUtil::exists( highPath );
        if ( bHasHigh )
        {
            if ( Internal::readSplatImage( highPath, highImage ) == false )
                return false;
            if ( highImage._width != lowImage._width || highImage._height != lowImage._height )
            {
                SW_LOG_ERROR_ID( "HeightfieldImporter.SplatSourceXBut", "Splat source '%#' is %#x%# but '%#' is %#x%# - both layer groups must have the same size", sourcePath.str(), lowImage._width, lowImage._height,
                                 highPath.c_str(), highImage._width, highImage._height );
                return false;
            }
        }
        outData._width  = static_cast<uint32>( lowImage._width );
        outData._height = static_cast<uint32>( lowImage._height );
        if ( outData._width > LayerWeightsAsset::kMaxResolution || outData._height > LayerWeightsAsset::kMaxResolution )
        {
            SW_LOG_ERROR_ID( "HeightfieldImporter.SplatSourceXMost", "Splat source '%#' is %#x%# - at most %# per side", sourcePath.str(), lowImage._width, lowImage._height, LayerWeightsAsset::kMaxResolution );
            outData = LayerWeightsAsset{};
            return false;
        }
        const size_t texelCount = static_cast<size_t>( outData._width ) * outData._height;
        outData._bytes.assign( texelCount * LayerWeightsAsset::kLayerCount, 0u );
        for ( size_t texel = 0; texel < texelCount; ++texel )
        {
            Memory::copy( outData._bytes.data() + texel * LayerWeightsAsset::kLayerCount, lowImage._bytes.data() + texel * 4, 4 );
            if ( bHasHigh )
                Memory::copy( outData._bytes.data() + texel * LayerWeightsAsset::kLayerCount + 4, highImage._bytes.data() + texel * 4, 4 );
        }
        if ( outData.isValid() == false )
        {
            SW_LOG_ERROR_ID( "HeightfieldImporter.SplatSourceXMost", "Splat source '%#' is %#x%# - at most %# per side", sourcePath.str(), lowImage._width, lowImage._height, LayerWeightsAsset::kMaxResolution );
            outData = LayerWeightsAsset{};
            return false;
        }
        return true;
    }

    bool HeightfieldImporter::isRawHeightfieldPath( string_view path )
    {
        return AssetIdentityRules::getProjectRules().isImportSource( kRawFolder, path ) && AssetImportStampUtil::isUnderRawFolder( path, kRawFolder );
    }

    void HeightfieldImporter::importChangedSources( AssetSourceBatch& batch )
    {
        // 바뀐 파일이 영향을 주는 원본만 임포트한다(루트 전체를 훑지 않는다). 스탬프를 적는 길은 명령줄 임포트와 같은 한 곳이다. 구멍 마스크 · 곁 스플랫도 원본 해시에 들어간다.
        vector<AssetSourceBatchEntry> listEntry;
        for ( size_t itemIndex = 0; itemIndex < batch._listItem.size(); ++itemIndex )
        {
            AssetSourceBatchItem& item = batch._listItem[itemIndex];
            item._bRan                 = true;
            item._bHandled             = isRawHeightfieldPath( item._relativePath );
            if ( item._bHandled )
                listEntry.push_back( AssetSourceBatchEntry{ itemIndex, HeightfieldImporterInternal::findAffectedSources( item._relativePath ) } );
        }
        const HeightfieldRawImporter importer;
        AssetImportStampUtil::importChangedSources( batch, listEntry, importer );
    }

    AbsolutePath HeightfieldImporter::makeImportedHeightfieldPath( const AbsolutePath& rawPath )
    {
        using Internal = HeightfieldImporterInternal;
        return AssetImportStampUtil::makeImportedPath( rawPath, kRawFolder, Internal::kImportedFolder, HeightfieldAsset::kExtension );
    }

    uint64 HeightfieldImporter::computeSourceHash( const AbsolutePath& sourcePath )
    {
        using Internal = HeightfieldImporterInternal;
        uint64 hash    = AssetSourceLocator::computeHash( sourcePath );
        if ( hash == 0 )
            return 0;
        StringBuilder<constant::buffer::kMax64> versionText;
        if ( isSourceLayerWeights( sourcePath.str() ) )
        {
            versionText.appendFormat( "splat=%#;format=%#", Internal::kSplatImporterVersion, LayerWeightsAsset::kVersion );
            hash = HashUtil::computeStringHash64( versionText.c_str(), versionText.size(), false, hash );
            // 옆에 둔 레이어 4..7 파일(`x_splat1.png`)도 원본이다. 그것만 고쳐도 어긋난 것으로 판정해야 한다.
            vector<uint8>      highBytes;
            const AbsolutePath highPath  = Internal::makeHighSplatPath( sourcePath );
            const bool         bHighRead = FileUtil::exists( highPath ) && FileUtil::readFile( highPath, highBytes ); // 곁 파일은 선택이라 없는 것이 정상이다 - 읽기 오류로 알리지 않는다
            if ( bHighRead && highBytes.empty() == false )
                hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( highBytes.data() ), highBytes.size(), false, hash );
            return hash;
        }
        versionText.appendFormat( "importer=%#;format=%#", Internal::kImporterVersion, HeightfieldAsset::kVersion );
        hash = HashUtil::computeStringHash64( versionText.c_str(), versionText.size(), false, hash );
        // 옆에 둔 구멍 마스크도 원본이다. 그것만 고쳐도 어긋난 것으로 판정해야 한다.
        vector<uint8>      holeBytes;
        const AbsolutePath holePath  = Internal::makeHolePath( sourcePath );
        const bool         bHoleRead = FileUtil::exists( holePath ) && FileUtil::readFile( holePath, holeBytes ); // 구멍 마스크도 선택이라 없는 것이 정상이다
        if ( bHoleRead && holeBytes.empty() == false )
            hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( holeBytes.data() ), holeBytes.size(), false, hash );
        return hash;
    }

    bool HeightfieldImporter::readHeightfield( const AbsolutePath& sourcePath, HeightfieldAsset& outData )
    {
        using Internal = HeightfieldImporterInternal;
        outData        = HeightfieldAsset{};
        vector<uint8> bytes;
        if ( AssetSourceLocator::readBytes( sourcePath, bytes ) == false )
        {
            SW_LOG_ERROR_ID( "HeightfieldImporter.FailedReadHeightfieldSource", "Failed to read heightfield source '%#'", sourcePath.str() );
            return false;
        }
        if ( FileUtil::hasExtension( sourcePath.str(), ".r16" ) )
        {
            if ( Internal::readRaw16( bytes, sourcePath.str(), outData ) == false )
                return false;
        }
        else
        {
            int32 width{ 0 };
            int32 height{ 0 };
            if ( RawImageData::loadGray16FromMemory( bytes.data(), bytes.size(), outData._listHeight, width, height ) == false )
                return false;
            if ( width != height || width < 2 )
            {
                SW_LOG_ERROR_ID( "HeightfieldImporter.HeightfieldXMustSquare", "Heightfield '%#' is %#x%# - it must be square and at least 2x2", sourcePath.str(), width, height );
                outData = HeightfieldAsset{};
                return false;
            }
            outData._resolution = static_cast<uint32>( width );
        }
        if ( outData._resolution > HeightfieldAsset::kMaxResolution )
        {
            SW_LOG_ERROR_ID( "HeightfieldImporter.HeightfieldResolutionMost", "Heightfield '%#' has resolution %# - at most %#", sourcePath.str(), outData._resolution, HeightfieldAsset::kMaxResolution );
            return false;
        }
        return Internal::readHoles( sourcePath, outData );
    }

    bool HeightfieldImporter::importHeightfield( const AbsolutePath& sourcePath, const AbsolutePath& outputPath )
    {
        HeightfieldAsset data;
        if ( readHeightfield( sourcePath, data ) == false )
            return false;
        return data.saveToFile( outputPath );
    }

    AssetImportSummary HeightfieldImporter::importAllHeightfields( const AbsolutePath& resourceRoot, AssetImportMode mode )
    {
        const HeightfieldRawImporter     importer;
        const expected<DerivedDataCache> derivedDataCache = DerivedDataCache::open( DerivedDataBucket::Import );
        if ( derivedDataCache.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax512> problemBuilder;
            problemBuilder.appendFormat( "DDC config: %# (%#)", derivedDataCache.error().message(), derivedDataCache.error().detail() );
            return AssetImportSummary::makeFailed( string( problemBuilder.view() ) );
        }
        return AssetImportStampUtil::importAll( resourceRoot, importer, mode, editor::findSubsystem<TaskManager>(), *derivedDataCache ); // 선택: 없으면 이 스레드가 직렬로 임포트한다(계약)
    }
} // namespace sw::editor
