#include "pch.h"

#include "Editor/Common/Asset/TextureBaker.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/ImageUtil.h"
#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Resource/ResourceUtil.h"

#if defined( TileShape )
    #undef TileShape
#endif
#if defined( CursorShape )
    #undef CursorShape
#endif
#if defined( PixmapShape )
    #undef PixmapShape
#endif

#include <DirectXTex.h>

namespace sw::editor
{
    namespace
    {
        DXGI_FORMAT resolveFormatInternal( string_view formatStr, bool bSrgb )
        {
            if ( formatStr == "BC1_UNORM" || formatStr == "bc1" )
                return bSrgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
            if ( formatStr == "BC2_UNORM" || formatStr == "bc2" )
                return bSrgb ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
            if ( formatStr == "BC3_UNORM" || formatStr == "bc3" )
                return bSrgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
            if ( formatStr == "BC4_UNORM" || formatStr == "bc4" )
                return DXGI_FORMAT_BC4_UNORM;
            if ( formatStr == "BC5_UNORM" || formatStr == "bc5" )
                return DXGI_FORMAT_BC5_UNORM;
            if ( formatStr == "BC6H_UF16" || formatStr == "bc6h" )
                return DXGI_FORMAT_BC6H_UF16;
            if ( formatStr == "BC7_UNORM" || formatStr == "bc7" )
                return bSrgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
            if ( formatStr == "B8G8R8A8_UNORM" || formatStr == "bgra8" )
                return bSrgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
            if ( formatStr == "R8G8B8A8_UNORM" || formatStr == "rgba8" )
                return bSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

            return bSrgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
        }

        /**
         * @struct SwizzleLayoutInternal
         * @brief 스위즐 하나가 정하는 두 가지, **바이트를 어떻게 놓는가와 그것을 무슨 이름으로 부르는가**입니다.
         * @details 이 둘이 따로 있으면 반드시 어긋납니다. 예전에는 채널 섞기가 `applyChannelManipulations` 의 if 사슬에, 이름이
         *          `bakeTexture` 의 `_swizzle == BGRA ? BGRA : RGBA` 삼항에 있었습니다. 그래서 `ARGB` 는 섞기 쪽만 알고 이름 쪽은
         *          몰랐고, 바이트는 옮겨졌는데 결과물에는 "RGBA" 라고 적혀 나가 색이 깨졌습니다. 이제 스위즐을 하나 더하려면
         *          **이 표에 한 줄**만 더하면 됩니다.
         */
        struct SwizzleLayoutInternal
        {
            /** @brief 결과의 n 번째 바이트를 원본(언제나 RGBA)의 몇 번째 바이트에서 가져올지입니다. */
            uint8 _arrSourceChannel[4];
            /** @brief 알파를 255 로 덮을지 여부입니다(RGB1). */
            uint8 _bForceOpaqueAlpha;
            /** @brief 그렇게 놓인 바이트 배열의 DXGI 이름입니다. */
            DXGI_FORMAT _format;
        };

        const SwizzleLayoutInternal& swizzleLayoutInternal( TextureSwizzle swizzle )
        {
            // **BGRA 와 ARGB 는 같은 것이다.** D3D9 의 `D3DFMT_A8R8G8B8` 은 메모리에서 B,G,R,A 순서이고 DXGI 는 그것을 `B8G8R8A8`
            // 이라 부른다. 열거형 주석의 "레거시 ARGB" 가 그 뜻이다. 예전 코드는 ARGB 를 왼쪽으로 한 칸 돌려 RGBA 를 GBAR 로
            // 만들었는데, 그것은 어떻게 읽어도 ARGB 가 아니다.
            static constexpr SwizzleLayoutInternal kRgba{
                { 0, 1, 2, 3 },
                SW_FALSE,
                DXGI_FORMAT_R8G8B8A8_UNORM
            };
            static constexpr SwizzleLayoutInternal kBgra{
                { 2, 1, 0, 3 },
                SW_FALSE,
                DXGI_FORMAT_B8G8R8A8_UNORM
            };
            static constexpr SwizzleLayoutInternal kRgb1{
                { 0, 1, 2, 3 },
                SW_TRUE,
                DXGI_FORMAT_R8G8B8A8_UNORM
            };

            switch ( swizzle )
            {
                case TextureSwizzle::BGRA:
                case TextureSwizzle::ARGB:
                    return kBgra;
                case TextureSwizzle::RGB1:
                    return kRgb1;
                case TextureSwizzle::RGBA:
                    return kRgba;
            }
        }

        /**
         * @struct TextureBakerInternal
         * @brief 원본(`textures_raw/`)과 구운 DDS(`textures/`)를 잇는 경로 규칙과 `bake.stamp` 읽기 · 쓰기 · 대조입니다.
         */
        struct TextureBakerInternal
        {
            /** @brief 원본 이미지를 두는 폴더 이름입니다. 쿠킹이 팩에서 뺍니다(`Config/Engine/PackConfig.json`). */
            static constexpr string_view kRawTextureFolder = "textures_raw";
            /** @brief 구운 DDS 가 가는, 원본 폴더 옆 폴더 이름입니다. 런타임은 여기의 DDS 만 읽습니다. */
            static constexpr string_view kBakedTextureFolder = "textures";
            /** @brief 원본 폴더마다 하나 두는 스탬프 파일 이름입니다. */
            static constexpr string_view kBakeStampFileName = "bake.stamp";
            /** @brief 스탬프 머리 줄입니다. 형식이나 판정이 바뀌면 올립니다 — 옛 스탬프는 전부 어긋남이 되어 한 번 다시 굽습니다. */
            static constexpr string_view kBakeStampHeader = "SWTEXBAKE 1";
            /** @brief 같은 원본 · 규칙에서 다른 바이트를 내게 굽기를 바꾸면 올립니다. 원본 해시에 섞입니다. */
            static constexpr uint32 kBakerVersion = 1;

            /** @brief 스탬프 한 줄입니다. */
            struct StampEntry
            {
                uint64 _sourceHash{ 0 };
                uint64 _bakedHash{ 0 };
            };

            /** @brief 굽는 원본 이미지 확장자인지 봅니다. `.dds` · `bake.stamp` · `.meta` 같은 것은 원본이 아닙니다. */
            static bool isSourceImage( string_view path ) { return FileUtil::hasAnyExtension( path, { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr" } ); }

            /**
             * @brief 경로를 `.../textures_raw` 와 그 아래 상대 경로로 나눕니다. `textures_raw` 는 경로 구간 경계에서만 찾습니다.
             */
            static bool splitRawPath( string_view path, string& outRawFolder, string& outRelativePath )
            {
                const string      normalized = FileUtil::normalizeSeparators( path );
                const string_view normalizedView{ normalized };
                for ( size_t pos = normalizedView.find( kRawTextureFolder ); pos != string_view::npos; pos = normalizedView.find( kRawTextureFolder, pos + 1 ) )
                {
                    const size_t endPos         = pos + kRawTextureFolder.size();
                    const bool   bStartsSegment = pos == 0 || normalized[pos - 1] == '/';
                    const bool   bEndsSegment   = endPos + 1 < normalized.size() && normalized[endPos] == '/';
                    if ( bStartsSegment && bEndsSegment )
                    {
                        outRawFolder    = normalized.substr( 0, endPos );
                        outRelativePath = normalized.substr( endPos + 1 );
                        return true;
                    }
                }
                return false;
            }

            /** @brief 파일 바이트 그대로의 FNV-1a 64 입니다. 없거나 비었으면 0 입니다. */
            static uint64 computeFileHash( string_view path )
            {
                vector<uint8> bytes;
                if ( FileUtil::fileExists( path ) == false || FileUtil::readFile( path, bytes ) == false || bytes.empty() )
                    return 0;
                return StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
            }

            static string formatHash( uint64 hash )
            {
                StringBuilder<constant::kMaxBuffer32> text;
                text.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
                return string( text.c_str(), text.size() );
            }

            /**
             * @brief 스탬프를 읽습니다. 없거나 머리 줄이 다르면 빈 표입니다(= 모든 원본이 어긋남). 형식이 아닌 줄은 건너뜁니다.
             */
            static void readStamp( const string& stampPath, map<string, StampEntry>& outMapEntry )
            {
                outMapEntry.clear();
                string text;
                if ( FileUtil::fileExists( stampPath ) == false || FileUtil::readTextFile( stampPath, text ) == false )
                    return;

                bool   bHeaderSeen = false;
                size_t lineStart   = 0;
                while ( lineStart < text.size() )
                {
                    size_t lineEnd = text.find( '\n', lineStart );
                    if ( lineEnd == string::npos )
                        lineEnd = text.size();
                    const string_view line = StringUtil::trim( string_view( text ).substr( lineStart, lineEnd - lineStart ) );
                    lineStart              = lineEnd + 1;

                    if ( bHeaderSeen == false )
                    {
                        if ( line != kBakeStampHeader )
                            return;
                        bHeaderSeen = true;
                        continue;
                    }

                    const size_t firstSpace  = line.find( ' ' );
                    const size_t secondSpace = firstSpace == string_view::npos ? string_view::npos : line.find( ' ', firstSpace + 1 );
                    if ( secondSpace == string_view::npos || secondSpace + 1 >= line.size() )
                        continue;

                    StampEntry entry;
                    const bool bSourceParsed = StringUtil::parseUint64( line.substr( 0, firstSpace ), entry._sourceHash, 16 );
                    const bool bBakedParsed  = StringUtil::parseUint64( line.substr( firstSpace + 1, secondSpace - firstSpace - 1 ), entry._bakedHash, 16 );
                    if ( bSourceParsed && bBakedParsed )
                        outMapEntry[string( line.substr( secondSpace + 1 ) )] = entry;
                }
            }

            /** @brief 스탬프 본문입니다. 줄은 경로 순이라 같은 표는 같은 바이트입니다. */
            static string makeStampText( const map<string, StampEntry>& mapEntry )
            {
                string text = string( kBakeStampHeader ) + "\n";
                for ( const auto& [relativePath, entry] : mapEntry )
                {
                    text += formatHash( entry._sourceHash );
                    text += ' ';
                    text += formatHash( entry._bakedHash );
                    text += ' ';
                    text += relativePath;
                    text += '\n';
                }
                return text;
            }

            /**
             * @brief 원본 하나가 스탬프와 어긋난 이유입니다. 맞으면 nullptr 입니다.
             */
            static const utf8* findDriftReason( const StampEntry* pStamped, const StampEntry& current )
            {
                if ( current._sourceHash == 0 )
                    return "원본을 읽지 못했습니다";
                if ( current._bakedHash == 0 )
                    return "DDS 가 없습니다";
                if ( pStamped == nullptr )
                    return "스탬프에 없습니다 (구운 적이 없습니다)";
                if ( pStamped->_sourceHash != current._sourceHash )
                    return "원본이나 규칙이 바뀌었는데 다시 굽지 않았습니다";
                if ( pStamped->_bakedHash != current._bakedHash )
                    return "DDS 가 구운 결과와 다릅니다 (손으로 바꿨습니다)";
                return nullptr;
            }

            /**
             * @brief `textures_raw` 폴더 하나를 그 폴더의 스탬프와 대조하고, BakeStale 이면 어긋난 것을 굽고 스탬프를 다시 씁니다.
             */
            static void bakeRawFolder( const string& rootDir, const string& rawFolder, const TextureImportConfig& config, TextureBakeMode mode,
                                       TextureBakeSummary& inoutSummary )
            {
                const string            stampPath = FileUtil::joinPath( rawFolder, kBakeStampFileName );
                map<string, StampEntry> mapStamped;
                readStamp( stampPath, mapStamped );

                vector<string> listFile;
                (void)FileUtil::collectFiles( rawFolder, "", listFile, true ); // 폴더는 방금 훑어서 찾았다
                std::sort( listFile.begin(), listFile.end() );

                map<string, StampEntry> mapCurrent;
                for ( const string& file : listFile )
                {
                    const string sourcePath = FileUtil::normalizeSeparators( file );
                    if ( isSourceImage( sourcePath ) == false || sourcePath.size() <= rawFolder.size() + 1 || sourcePath.size() <= rootDir.size() + 1 )
                        continue;

                    const string relativePath = sourcePath.substr( rawFolder.size() + 1 );
                    const string resourcePath = sourcePath.substr( rootDir.size() + 1 );
                    ++inoutSummary._sourceCount;

                    if ( FileUtil::hasExtension( sourcePath, ".hdr" ) )
                    {
                        inoutSummary._listProblem.push_back( resourcePath + ": HDR 원본은 굽지 않습니다 (디코더가 8비트라 값이 잘립니다)" );
                        continue;
                    }

                    TextureImportRule rule;
                    if ( config.findMatchingRule( resourcePath, rule ) == false )
                        rule = TextureImportRule{};

                    const string bakedPath = TextureBaker::makeBakedTexturePath( sourcePath );
                    StampEntry   current;
                    current._sourceHash = TextureBaker::computeSourceHash( sourcePath, rule );
                    current._bakedHash  = computeFileHash( bakedPath );

                    const auto        itStamped = mapStamped.find( relativePath );
                    const utf8* const pReason   = findDriftReason( itStamped != mapStamped.end() ? &itStamped->second : nullptr, current );
                    if ( pReason == nullptr )
                    {
                        mapCurrent[relativePath] = current;
                        continue;
                    }

                    if ( mode == TextureBakeMode::CheckOnly )
                    {
                        inoutSummary._listProblem.push_back( resourcePath + ": " + pReason );
                        continue;
                    }

                    if ( TextureBaker::bakeTexture( sourcePath, bakedPath, rule ) == false )
                    {
                        inoutSummary._listProblem.push_back( resourcePath + ": 굽지 못했습니다" );
                        continue;
                    }

                    current._bakedHash       = computeFileHash( bakedPath );
                    mapCurrent[relativePath] = current;
                    ++inoutSummary._bakedCount;
                }

                // 원본이 사라진 줄. 구운 DDS 는 다른 것이 참조할 수 있어 지우지 않는다 — 줄만 지운다.
                for ( const auto& [relativePath, entry] : mapStamped )
                {
                    if ( mapCurrent.find( relativePath ) != mapCurrent.end() )
                        continue;
                    const string sourcePath = FileUtil::joinPath( rawFolder, relativePath );
                    if ( FileUtil::fileExists( sourcePath ) )
                        continue; // 원본은 있다 — 위에서 어긋남 · 실패로 이미 보고했다
                    if ( mode == TextureBakeMode::CheckOnly )
                        inoutSummary._listProblem.push_back( sourcePath.substr( rootDir.size() + 1 ) + ": 원본이 없는데 스탬프에 남아 있습니다" );
                }

                if ( mode == TextureBakeMode::CheckOnly )
                    return;

                // 내용이 같으면 쓰지 않는다 — 맞는 트리에서 굽기를 돌려도 작업 트리가 더러워지지 않는다. 체크아웃이 줄 끝을 CRLF 로
                // 바꿔 둘 수 있으므로 CR 은 빼고 비교한다.
                const bool bStampExists = FileUtil::fileExists( stampPath );
                if ( mapCurrent.empty() && bStampExists == false )
                    return;
                const string stampText = makeStampText( mapCurrent );
                string       existingText;
                if ( bStampExists && FileUtil::readTextFile( stampPath, existingText ) )
                {
                    existingText.erase( std::remove( existingText.begin(), existingText.end(), '\r' ), existingText.end() );
                    if ( existingText == stampText )
                        return;
                }
                if ( FileUtil::writeTextFile( stampPath, stampText ) == false )
                    inoutSummary._listProblem.push_back( "스탬프를 쓰지 못했습니다: " + stampPath );
            }
        };
    } // namespace

    SW_LOG_CALLER( "TextureBaker" );

    bool TextureBaker::bakeTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureBakeResult* pOutResult )
    {
        if ( pOutResult != nullptr )
        {
            pOutResult->_sourcePath      = string( sourcePath );
            pOutResult->_outputPath      = string( outputPath );
            pOutResult->_bSuccess        = SW_FALSE;
            pOutResult->_sourceSizeBytes = FileUtil::getFileSize( sourcePath );
        }

        // 1) ImageUtil 로 소스 이미지 디코딩
        RawImageData rawImage;
        if ( ImageUtil::loadImage( sourcePath, rawImage ) == false || rawImage.isValid() == false )
        {
            SW_LOG_ERROR( "Failed to decode source image: %#", sourcePath );
            return false;
        }

        const size_t totalPixels = static_cast<size_t>( rawImage._width ) * static_cast<size_t>( rawImage._height );

        applyChannelManipulations( rawImage, rule, totalPixels );

        const DXGI_FORMAT targetFormat = resolveFormatInternal( rule._format, rule._bSrgb == SW_TRUE );

        // 2) DirectXTex 기본 Image 구성
        DirectX::Image baseImage{};
        baseImage.width  = static_cast<size_t>( rawImage._width );
        baseImage.height = static_cast<size_t>( rawImage._height );
        // 채널 섞기와 같은 표에서 가져온다. 둘이 어긋날 여지를 아예 없앤다.
        // 원본 이미지 바이트는 이미 sRGB 인코딩이다. 결과가 sRGB 형식이면 바이트에 **이름만** sRGB 를 붙인다 — UNORM 으로 두면
        // 아래 변환 · 압축이 선형 → sRGB 곡선을 한 번 더 적용한다. 밉 필터도 이 이름을 보고 선형 공간에서 섞는다.
        const DXGI_FORMAT layoutFormat = swizzleLayoutInternal( rule._swizzle )._format;
        baseImage.format               = DirectX::IsSRGB( targetFormat ) ? DirectX::MakeSRGB( layoutFormat ) : layoutFormat;
        baseImage.rowPitch             = static_cast<size_t>( rawImage._width ) * 4;
        baseImage.slicePitch           = baseImage.rowPitch * static_cast<size_t>( rawImage._height );
        baseImage.pixels               = rawImage._bytes.data();

        // WIC 필터는 Windows 에만 있고 결과가 OS 구현에 달렸다. 구운 DDS 를 커밋하고 그 해시를 스탬프에 적으므로, 어디서 굽든 같은
        // 바이트가 나오는 DirectXTex 자체 필터만 쓴다.
        constexpr DirectX::TEX_FILTER_FLAGS kFilterFlags = DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_FORCE_NON_WIC;

        // 3) 켜져 있으면 밉맵 생성
        DirectX::ScratchImage mipChain;
        if ( rule._bGenerateMips == SW_TRUE )
        {
            const HRESULT hr = DirectX::GenerateMipMaps( baseImage, kFilterFlags, 0, mipChain );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR(
                    "DirectX::GenerateMipMaps failed (hr=0x%#) for %#",
                    Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                    sourcePath.data() );
                return false;
            }
        }
        else
        {
            const HRESULT hr = mipChain.InitializeFromImage( baseImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR(
                    "DirectX::InitializeFromImage failed (hr=0x%#) for %#",
                    Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                    sourcePath.data() );
                return false;
            }
        }

        const uint32 mipCount = static_cast<uint32>( mipChain.GetMetadata().mipLevels );

        // 4) 압축 또는 포맷 변환
        DirectX::ScratchImage finalImage;

        if ( DirectX::IsCompressed( targetFormat ) )
        {
            const HRESULT hr = DirectX::Compress(
                mipChain.GetImages(),
                mipChain.GetImageCount(),
                mipChain.GetMetadata(),
                targetFormat,
                DirectX::TEX_COMPRESS_DEFAULT,
                DirectX::TEX_THRESHOLD_DEFAULT,
                finalImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR(
                    "DirectX::Compress failed (hr=0x%#) for format %#",
                    Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                    static_cast<uint32>( targetFormat ) );
                return false;
            }
        }
        else if ( targetFormat != mipChain.GetMetadata().format )
        {
            const HRESULT hr = DirectX::Convert(
                mipChain.GetImages(),
                mipChain.GetImageCount(),
                mipChain.GetMetadata(),
                targetFormat,
                kFilterFlags,
                DirectX::TEX_THRESHOLD_DEFAULT,
                finalImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR(
                    "DirectX::Convert failed (hr=0x%#) for format %#",
                    Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                    static_cast<uint32>( targetFormat ) );
                return false;
            }
        }
        else
        {
            finalImage = std::move( mipChain );
        }

        // 5) DDS 파일로 저장
        const string outputDir = FileUtil::getDirectoryPart( outputPath );
        if ( outputDir.empty() == false )
            FileUtil::ensureDirectoryExists( outputDir );

        const wstring wOutPath = StringUtil::utf8ToUtf16( string( outputPath ).c_str() );

        const HRESULT hrSave = DirectX::SaveToDDSFile(
            finalImage.GetImages(),
            finalImage.GetImageCount(),
            finalImage.GetMetadata(),
            DirectX::DDS_FLAGS_NONE,
            wOutPath.c_str() );

        if ( FAILED( hrSave ) )
        {
            SW_LOG_ERROR(
                "DirectX::SaveToDDSFile failed (hr=0x%#) for %#",
                Fmt( static_cast<uint32>( hrSave ), Format( 8, Format::Padding::Zero ).hex() ),
                outputPath.data() );
            return false;
        }

        const uint64 outputSizeBytes = FileUtil::getFileSize( outputPath );
        if ( pOutResult != nullptr )
        {
            pOutResult->_width           = static_cast<uint32>( rawImage._width );
            pOutResult->_height          = static_cast<uint32>( rawImage._height );
            pOutResult->_mipCount        = mipCount;
            pOutResult->_outputSizeBytes = outputSizeBytes;
            pOutResult->_bSuccess        = SW_TRUE;
        }

        SW_LOG_INFO( "Baked texture: %# -> %# (Format: %d, Mips: %u, %llu -> %llu bytes)",
                     sourcePath.data(), outputPath.data(), targetFormat, mipCount,
                     FileUtil::getFileSize( sourcePath ), outputSizeBytes );

        return true;
    }

    void TextureBaker::applyChannelManipulations( RawImageData& rawImage, const TextureImportRule& rule, size_t totalPixels )
    {
        if ( rawImage._bytes.size() < totalPixels * 4 )
            return;

        uint8*                       pData  = rawImage._bytes.data();
        const SwizzleLayoutInternal& layout = swizzleLayoutInternal( rule._swizzle );

        // **그린 반전이 먼저다.** 입력은 언제나 RGBA 이므로 이 시점의 초록 자리는 1번으로 정해져 있다. 섞은 뒤에 뒤집으려면
        // "결과의 1번이 초록" 이라는 가정이 필요한데, 그것은 지금 스위즐들에서 우연히 맞을 뿐 새 스위즐을 더하는 순간 조용히 틀린다.
        if ( rule._bInvertGreen == SW_TRUE )
        {
            for ( size_t index = 0; index < totalPixels; ++index )
            {
                pData[index * 4 + 1] = static_cast<uint8>( 255 - pData[index * 4 + 1] );
            }
        }

        for ( size_t index = 0; index < totalPixels; ++index )
        {
            uint8* pPixel = pData + index * 4;

            const uint8 arrSource[4] = { pPixel[0], pPixel[1], pPixel[2], pPixel[3] };
            for ( size_t channel = 0; channel < 4; ++channel )
            {
                pPixel[channel] = arrSource[layout._arrSourceChannel[channel]];
            }

            if ( layout._bForceOpaqueAlpha != SW_FALSE )
                pPixel[3] = 255;
        }
    }

    bool TextureBaker::importChangedSourceImage( string_view relativePath )
    {
        if ( FileUtil::hasExtension( relativePath, ".dds" ) )
            return false;

        if ( FileUtil::hasExtension( relativePath, ".hdr" ) )
        {
            SW_LOG_WARNING( "HDR 은 자동 베이크 대상이 아닙니다 (8비트로 잘린다): %#", relativePath );
            return true;
        }

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        const string  normalized   = FileUtil::normalizeSeparators( FileUtil::joinPath( resourceRoot, relativePath ) );
        if ( makeBakedTexturePath( normalized ).empty() )
        {
            SW_LOG_WARNING( "소스 이미지는 `%#` 아래에 있어야 구워집니다: %#", TextureBakerInternal::kRawTextureFolder, relativePath );
            return true;
        }

        // 바뀐 파일 하나가 아니라 어긋난 것 전부를 일괄 굽기로 굽는다 — 스탬프를 적는 길이 하나여야 에디터에서 구운 것과
        // `App --bake-textures` 로 구운 것이 같은 판정을 받는다. 내용이 그대로면(저장만 다시 했다) 굽지 않는다.
        // 설정 파일이 없으면 기본 규칙이다. 깨졌으면 로드가 알리고 기본 규칙으로 굽는다.
        TextureImportConfig config{};
        (void)config.loadFromFile( EditorUtil::resolveEditorConfigFile( getEditorData()._textureImportConfigFile.c_str() ) );
        const TextureBakeSummary summary = bakeAllTextures( resourceRoot, config, TextureBakeMode::BakeStale );
        for ( const string& problem : summary._listProblem )
        {
            SW_LOG_ERROR( "텍스처 베이크 실패: %#", problem.c_str() );
        }

        SW_LOG_INFO( "텍스처 일괄 굽기: %# 바뀜 -> %#개 구움", relativePath, summary._bakedCount );
        return true;
    }

    TextureBakeSummary TextureBaker::bakeAllTextures( string_view resourceRoot, const TextureImportConfig& config, TextureBakeMode mode )
    {
        TextureBakeSummary summary;
        const string       rootDir = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );

        vector<string> listFolder;
        if ( rootDir.empty() || FileUtil::collectFolders( rootDir, listFolder, true ) == false )
        {
            summary._listProblem.push_back( "리소스 루트를 훑지 못했습니다: " + rootDir );
            return summary;
        }

        std::sort( listFolder.begin(), listFolder.end() );
        for ( const string& folder : listFolder )
        {
            const string normalizedFolder = FileUtil::normalizeSeparators( folder );
            if ( FileUtil::getFileNamePart( normalizedFolder ) == TextureBakerInternal::kRawTextureFolder )
                TextureBakerInternal::bakeRawFolder( rootDir, normalizedFolder, config, mode, summary );
        }
        return summary;
    }

    string TextureBaker::makeBakedTexturePath( string_view rawTexturePath )
    {
        string rawFolder;
        string relativePath;
        if ( TextureBakerInternal::splitRawPath( rawTexturePath, rawFolder, relativePath ) == false )
            return {};

        const string domainFolder = FileUtil::getDirectoryPart( rawFolder );
        const string bakedFolder  = domainFolder.empty() ? string( TextureBakerInternal::kBakedTextureFolder )
                                                         : FileUtil::joinPath( domainFolder, TextureBakerInternal::kBakedTextureFolder );
        return FileUtil::replaceExtension( FileUtil::joinPath( bakedFolder, relativePath ), ".dds" );
    }

    uint64 TextureBaker::computeSourceHash( string_view sourcePath, const TextureImportRule& rule )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
            return 0;

        // 규칙은 **해석한 결과**로 섞는다. "bc7" 과 "BC7_UNORM", BGRA 와 ARGB 처럼 같은 결과를 내는 표기는 같은 해시다.
        const SwizzleLayoutInternal&           layout = swizzleLayoutInternal( rule._swizzle );
        StringBuilder<constant::kMaxBuffer256> ruleText;
        ruleText.appendFormat( "baker=%#;format=%#;channel=%#%#%#%#;opaque=%#;mips=%#;srgb=%#;invertGreen=%#",
                               TextureBakerInternal::kBakerVersion,
                               static_cast<uint32>( resolveFormatInternal( rule._format, rule._bSrgb == SW_TRUE ) ),
                               static_cast<uint32>( layout._arrSourceChannel[0] ), static_cast<uint32>( layout._arrSourceChannel[1] ),
                               static_cast<uint32>( layout._arrSourceChannel[2] ), static_cast<uint32>( layout._arrSourceChannel[3] ),
                               static_cast<uint32>( layout._bForceOpaqueAlpha ), static_cast<uint32>( rule._bGenerateMips ), static_cast<uint32>( rule._bSrgb ),
                               static_cast<uint32>( rule._bInvertGreen ) );

        const uint64 contentHash = StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        return StringUtil::computeHash64( ruleText.c_str(), ruleText.size(), false, contentHash );
    }

    string TextureBaker::makeDefaultImportConfigPath()
    {
        const EditorData defaults{};
        const string     projectRoot = EditorUtil::getProjectRootPath();
        if ( projectRoot.empty() )
            return {};

        const string configDir = FileUtil::joinPath( FileUtil::joinPath( projectRoot, defaults._configFolder ), defaults._editorConfigFolder );
        return FileUtil::joinPath( configDir, defaults._textureImportConfigFile );
    }
} // namespace sw::editor
