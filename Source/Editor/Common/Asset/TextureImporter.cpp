#include "pch.h"

#include "Editor/Common/Asset/TextureImporter.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ImageUtil.h"
#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Resource/ResourceUtil.h"

#include "sw/config/ConfigConstants.h"

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

            if ( formatStr == "R16G16B16A16_FLOAT" || formatStr == "rgba16f" )
                return DXGI_FORMAT_R16G16B16A16_FLOAT;

            return bSrgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
        }

        /** @brief 임포트 결과(밉까지 끝난 이미지)를 DDS 로 쓰고 결과를 채웁니다. 8 비트 · HDR 두 갈래가 같이 씁니다. */
        [[nodiscard]] bool saveImportedDdsInternal( const DirectX::ScratchImage& finalImage, string_view sourcePath, string_view outputPath, TextureImportResult* pOutResult )
        {
            const string outputDir = FileUtil::getDirectoryPart( outputPath );
            if ( outputDir.empty() == false )
                FileUtil::ensureDirectoryExists( outputDir );

            const wstring wOutPath = StringUtil::utf8ToUtf16( string( outputPath ).c_str() );
            const HRESULT hrSave   = DirectX::SaveToDDSFile( finalImage.GetImages(), finalImage.GetImageCount(), finalImage.GetMetadata(), DirectX::DDS_FLAGS_NONE,
                                                             wOutPath.c_str() );
            if ( FAILED( hrSave ) )
            {
                SW_LOG_ERROR( "DirectX::SaveToDDSFile failed (hr=0x%#) for %#", Fmt( static_cast<uint32>( hrSave ), Format( 8, Format::Padding::Zero ).hex() ),
                              outputPath );
                return false;
            }

            const DirectX::TexMetadata& metadata        = finalImage.GetMetadata();
            const uint32                mipCount        = static_cast<uint32>( metadata.mipLevels );
            const uint64                outputSizeBytes = FileUtil::getFileSize( outputPath );
            if ( pOutResult != nullptr )
            {
                pOutResult->_width           = static_cast<uint32>( metadata.width );
                pOutResult->_height          = static_cast<uint32>( metadata.height );
                pOutResult->_mipCount        = mipCount;
                pOutResult->_outputSizeBytes = outputSizeBytes;
                pOutResult->_bSuccess        = SW_TRUE;
            }
            SW_LOG_INFO( "Imported texture: %# -> %# (Format: %#, Mips: %#, %# -> %# bytes)", sourcePath, outputPath,
                         static_cast<uint32>( metadata.format ), mipCount, FileUtil::getFileSize( sourcePath ), outputSizeBytes );
            return true;
        }

        /**
         * @brief `.hdr`(Radiance RGBE)을 부동소수점으로 읽어 BC6H_UF16 또는 R16G16B16A16_FLOAT 로 임포트합니다.
         * @details 규칙의 포맷이 다른 것(8 비트)이면 실패로 알린다 — 1 을 넘는 값을 자르지 않는다. 밉은 선형 값 그대로 섞는다(sRGB 가 아니다).
         */
        [[nodiscard]] bool importHdrTextureInternal( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureImportResult* pOutResult )
        {
            const DXGI_FORMAT targetFormat = resolveFormatInternal( rule._format, false );
            if ( targetFormat != DXGI_FORMAT_BC6H_UF16 && targetFormat != DXGI_FORMAT_R16G16B16A16_FLOAT )
            {
                SW_LOG_ERROR( "HDR source needs format bc6h or rgba16f (rule '%#'): %#", rule._format.c_str(), sourcePath );
                return false;
            }
            const wstring         wSourcePath = StringUtil::utf8ToUtf16( string( sourcePath ).c_str() );
            DirectX::TexMetadata  metadata{};
            DirectX::ScratchImage sourceImage;
            HRESULT               hr = DirectX::LoadFromHDRFile( wSourcePath.c_str(), &metadata, sourceImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR( "DirectX::LoadFromHDRFile failed (hr=0x%#) for %#", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                              sourcePath );
                return false;
            }
            // 8 비트 갈래와 같은 자체 필터 — 어디서 임포트해도 같은 바이트다.
            constexpr DirectX::TEX_FILTER_FLAGS kFilterFlags = DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_FORCE_NON_WIC;
            DirectX::ScratchImage               mipChain;
            hr = ( rule._bGenerateMips == SW_TRUE ) ? DirectX::GenerateMipMaps( *sourceImage.GetImage( 0, 0, 0 ), kFilterFlags, 0, mipChain )
                                                    : mipChain.InitializeFromImage( *sourceImage.GetImage( 0, 0, 0 ) );
            DirectX::ScratchImage finalImage;
            if ( SUCCEEDED( hr ) )
            {
                hr = ( targetFormat == DXGI_FORMAT_BC6H_UF16 )
                       ? DirectX::Compress( mipChain.GetImages(), mipChain.GetImageCount(), mipChain.GetMetadata(), targetFormat, DirectX::TEX_COMPRESS_DEFAULT,
                                            DirectX::TEX_THRESHOLD_DEFAULT, finalImage )
                       : DirectX::Convert( mipChain.GetImages(), mipChain.GetImageCount(), mipChain.GetMetadata(), targetFormat, kFilterFlags,
                                           DirectX::TEX_THRESHOLD_DEFAULT, finalImage );
            }
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR( "HDR mip/compress failed (hr=0x%#) for %#", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                              sourcePath );
                return false;
            }
            return saveImportedDdsInternal( finalImage, sourcePath, outputPath, pOutResult );
        }

        /**
         * @struct SwizzleLayoutInternal
         * @brief 스위즐 하나가 정하는 두 가지, **바이트를 어떻게 놓는가와 그것을 무슨 이름으로 부르는가**입니다.
         * @details 이 둘이 따로 있으면 반드시 어긋납니다 — 섞기 쪽만 아는 스위즐은 바이트는 옮겨지는데 결과물에 "RGBA" 라고
         *          적혀 나가 색이 깨집니다. 스위즐을 하나 더하려면 **이 표에 한 줄**만 더하면 됩니다.
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
            // 이라 부른다. 열거형 주석의 "레거시 ARGB" 가 그 뜻이다. 주의: RGBA 를 왼쪽으로 한 칸 돌린 GBAR 은 어떻게 읽어도
            // ARGB 가 아니다.
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
         * @struct TextureImporterInternal
         * @brief 원본(`textures_raw/`)과 임포트된 DDS(`textures/`)를 잇는 이름과 버전입니다. 스탬프 절차는 `AssetImportStampUtil` 이 합니다.
         */
        struct TextureImporterInternal
        {
            /** @brief 원본 이미지를 두는 폴더 이름입니다. 쿠킹이 팩에서 뺍니다(`Config/Engine/PackConfig.json`). */
            static constexpr string_view kRawTextureFolder = "textures_raw";
            /** @brief 임포트된 DDS 가 가는, 원본 폴더 옆 폴더 이름입니다. 런타임은 여기의 DDS 만 읽습니다. */
            static constexpr string_view kImportedTextureFolder = "textures";
            /** @brief 스탬프 머리 줄입니다. 형식이나 판정이 바뀌면 올립니다 — 옛 스탬프는 전부 어긋남이 되어 한 번 다시 임포트합니다. */
            static constexpr string_view kImportStampHeader = "SWTEXIMPORT 1";
            /** @brief 같은 원본 · 규칙에서 다른 바이트를 내게 임포트를 바꾸면 올립니다. 원본 해시에 섞입니다. */
            static constexpr uint32 kImporterVersion = 1;

            /** @brief 임포트하는 원본 이미지 확장자인지 봅니다. `.dds` · `import.stamp` · `.meta` 같은 것은 원본이 아닙니다. */
            static bool isSourceImage( string_view path ) { return FileUtil::hasAnyExtension( path, { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr" } ); }
        };

        /**
         * @class TextureRawImporter
         * @brief 텍스처 원본이 일괄 임포트에 답하는 것들입니다. 원본마다 임포트 설정에서 규칙을 고릅니다.
         */
        class TextureRawImporter final : public IRawAssetImporter
        {
        public:
            explicit TextureRawImporter( const TextureImportConfig& config )
                : _config{ config }
            {
            }

            string_view getRawFolderName() const override { return TextureImporterInternal::kRawTextureFolder; }
            string_view getStampHeader() const override { return TextureImporterInternal::kImportStampHeader; }
            string_view getImportedLabel() const override { return "DDS"; }
            bool        isSourceFile( string_view path ) const override { return TextureImporterInternal::isSourceImage( path ); }
            string      makeImportedPath( string_view sourcePath ) const override { return TextureImporter::makeImportedTexturePath( sourcePath ); }
            uint64      computeSourceHash( string_view sourcePath, string_view resourcePath ) const override
            {
                return TextureImporter::computeSourceHash( sourcePath, findRule( resourcePath ) );
            }
            const utf8* findUnsupportedReason( string_view sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view resourcePath ) const override
            {
                return TextureImporter::importTexture( sourcePath, importedPath, findRule( resourcePath ) );
            }

        private:
            /** @brief 리소스 루트 기준 경로에 맞는 규칙입니다. 없으면 기본 규칙입니다. */
            TextureImportRule findRule( string_view resourcePath ) const
            {
                TextureImportRule rule;
                if ( _config.findMatchingRule( resourcePath, rule ) == false )
                    rule = TextureImportRule{};
                return rule;
            }

            const TextureImportConfig& _config;
        };
    } // namespace

    SW_LOG_CALLER( "TextureImporter" );

    bool TextureImporter::importTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureImportResult* pOutResult )
    {
        if ( pOutResult != nullptr )
        {
            pOutResult->_sourcePath      = string( sourcePath );
            pOutResult->_outputPath      = string( outputPath );
            pOutResult->_bSuccess        = SW_FALSE;
            pOutResult->_sourceSizeBytes = FileUtil::getFileSize( sourcePath );
        }

        // `.hdr`(Radiance RGBE)은 1 을 넘는 값을 지닌다 — 8 비트 디코더(stb)를 거치지 않고 부동소수점으로 읽어 HDR 포맷으로만 임포트한다.
        if ( FileUtil::hasExtension( sourcePath, ".hdr" ) )
            return importHdrTextureInternal( sourcePath, outputPath, rule, pOutResult );

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

        // WIC 필터는 Windows 에만 있고 결과가 OS 구현에 달렸다. 임포트된 DDS 를 커밋하고 그 해시를 스탬프에 적으므로, 어디서 임포트하든 같은
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
        return saveImportedDdsInternal( finalImage, sourcePath, outputPath, pOutResult );
    }

    void TextureImporter::applyChannelManipulations( RawImageData& rawImage, const TextureImportRule& rule, size_t totalPixels )
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

    bool TextureImporter::importChangedSourceImage( string_view relativePath )
    {
        if ( FileUtil::hasExtension( relativePath, ".dds" ) )
            return false;

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        const string  normalized   = FileUtil::normalizeSeparators( FileUtil::joinPath( resourceRoot, relativePath ) );
        if ( makeImportedTexturePath( normalized ).empty() )
        {
            SW_LOG_WARNING( "소스 이미지는 `%#` 아래에 있어야 임포트됩니다: %#", TextureImporterInternal::kRawTextureFolder, relativePath );
            return true;
        }

        // 바뀐 파일 하나가 아니라 어긋난 것 전부를 일괄 임포트로 임포트한다 — 스탬프를 적는 길이 하나여야 에디터에서 임포트된 것과
        // `App --import-textures` 로 임포트된 것이 같은 판정을 받는다. 내용이 그대로면(저장만 다시 했다) 임포트하지 않는다.
        // 설정 파일이 없으면 기본 규칙이다. 깨졌으면 로드가 알리고 기본 규칙으로 임포트한다.
        TextureImportConfig config{};
        (void)config.loadFromFile( makeDefaultImportConfigPath() );
        const AssetImportSummary summary = importAllTextures( resourceRoot, config, AssetImportMode::ImportStale );
        for ( const string& problem : summary._listProblem )
        {
            SW_LOG_ERROR( "텍스처 임포트 실패: %#", problem.c_str() );
        }

        SW_LOG_INFO( "텍스처 일괄 임포트: %# 바뀜 -> %#개 임포트함", relativePath, summary._importedCount );
        return true;
    }

    AssetImportSummary TextureImporter::importAllTextures( string_view resourceRoot, const TextureImportConfig& config, AssetImportMode mode )
    {
        const TextureRawImporter importer{ config };
        return AssetImportStampUtil::importAll( resourceRoot, importer, mode );
    }

    string TextureImporter::makeImportedTexturePath( string_view rawTexturePath )
    {
        return AssetImportStampUtil::makeImportedPath( rawTexturePath, TextureImporterInternal::kRawTextureFolder, TextureImporterInternal::kImportedTextureFolder,
                                                       ".dds" );
    }

    uint64 TextureImporter::computeSourceHash( string_view sourcePath, const TextureImportRule& rule )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
            return 0;

        // 규칙은 **해석한 결과**로 섞는다. "bc7" 과 "BC7_UNORM", BGRA 와 ARGB 처럼 같은 결과를 내는 표기는 같은 해시다.
        const SwizzleLayoutInternal&           layout = swizzleLayoutInternal( rule._swizzle );
        StringBuilder<constant::kMaxBuffer256> ruleText;
        ruleText.appendFormat( "importer=%#;format=%#;channel=%#%#%#%#;opaque=%#;mips=%#;srgb=%#;invertGreen=%#",
                               TextureImporterInternal::kImporterVersion,
                               static_cast<uint32>( resolveFormatInternal( rule._format, rule._bSrgb == SW_TRUE ) ),
                               static_cast<uint32>( layout._arrSourceChannel[0] ), static_cast<uint32>( layout._arrSourceChannel[1] ),
                               static_cast<uint32>( layout._arrSourceChannel[2] ), static_cast<uint32>( layout._arrSourceChannel[3] ),
                               static_cast<uint32>( layout._bForceOpaqueAlpha ), static_cast<uint32>( rule._bGenerateMips ), static_cast<uint32>( rule._bSrgb ),
                               static_cast<uint32>( rule._bInvertGreen ) );

        const uint64 contentHash = StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        return StringUtil::computeHash64( ruleText.c_str(), ruleText.size(), false, contentHash );
    }

    string TextureImporter::makeDefaultImportConfigPath()
    {
        const string projectRoot = EditorUtil::getProjectRootPath();
        if ( projectRoot.empty() )
            return {};
        return FileUtil::joinPath( FileUtil::joinPath( projectRoot, config::kDirConfigEditor ), EditorUtil::kTextureImportConfigFileName );
    }
} // namespace sw::editor
