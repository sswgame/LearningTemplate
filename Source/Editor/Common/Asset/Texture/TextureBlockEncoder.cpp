#include "pch.h"

#include "Editor/Common/Asset/Texture/TextureBlockEncoder.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/Texture/RawImageData.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"

#include <astcenc/astcenc.h>
#include <etcpak/ProcessRGB.hpp>

namespace sw::editor
{
    namespace
    {
        struct TextureBlockEncoderInternal
        {
            static constexpr uint32 kBlockSize = 4;
            /** @brief 띠 하나의 화소 행 수입니다(블록 높이의 배수) — BC 압축의 띠(`TextureImporter` `BandCompressJob`)와 같은 크기다. */
            static constexpr uint32 kBandRows = 64;
            static_assert( kBandRows % kBlockSize == 0, "bands must cover whole 4x4 block rows" );

            /** @brief 밉의 가로 띠 하나입니다. */
            struct Band
            {
                const uint8* _pRGBA{ nullptr };
                uint8*       _pOut{ nullptr };
                uint32       _rowCount{ 0 };
                bool         _bEncoded{ false };
            };

            /** @brief 스레드마다 하나인 astcenc 문맥입니다 — 문맥 만들기(표 계산)가 무거워 밉마다 만들지 않는다. 스레드가 끝나면 놓는다. */
            struct ASTCContext
            {
                astcenc_context* _pContext{ nullptr };

                ~ASTCContext()
                {
                    if ( _pContext != nullptr )
                        astcenc_context_free( _pContext );
                }
            };

            /** @brief 이 스레드의 ASTC 4x4 LDR 문맥입니다. 만들지 못하면 오류를 남기고 nullptr 입니다. */
            static astcenc_context* getThreadASTCContext()
            {
                thread_local ASTCContext t_context{};
                if ( t_context._pContext != nullptr )
                    return t_context._pContext;
                astcenc_config   config{};
                astcenc_error    status   = astcenc_config_init( ASTCENC_PRF_LDR, kBlockSize, kBlockSize, 1, ASTCENC_PRE_MEDIUM, 0, &config ); // 품질은 astcenc `-medium`(쿠킹 기본 자리)
                astcenc_context* pContext = nullptr;
                if ( status == ASTCENC_SUCCESS )
                    status = astcenc_context_alloc( &config, 1, &pContext, nullptr );
                if ( status != ASTCENC_SUCCESS )
                {
                    SW_LOG_ERROR_ID( "TextureBlockEncoder.ASTCContextFailed", "astcenc could not create a 4x4 LDR context: %#", astcenc_get_error_string( status ) );
                    return nullptr;
                }
                t_context._pContext = pContext;
                return pContext;
            }

            /** @brief astcenc 로 ASTC 4x4 블록을 @p pOut 에 씁니다. 가장자리 블록은 astcenc 가 마지막 화소를 되풀이해 채운다. */
            static bool encodeASTC( const uint8* pRGBA, uint32 width, uint32 height, uint8* pOut, size_t outBytes )
            {
                astcenc_context* const pContext = getThreadASTCContext();
                if ( pContext == nullptr )
                    return false;
                // 입력 칸이 const 가 아니다(astcenc 의 slice 배열) — 원본을 건드리지 않게 사본을 넘긴다.
                vector<uint8> pixels( pRGBA, pRGBA + static_cast<size_t>( width ) * height * 4 );
                void*         pSlice = pixels.data();
                astcenc_image image{};
                image.dim_x     = width;
                image.dim_y     = height;
                image.dim_z     = 1;
                image.data_type = ASTCENC_TYPE_U8;
                image.data      = &pSlice;
                const astcenc_swizzle swizzle{ ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
                const astcenc_error   status = astcenc_compress_image( pContext, &image, &swizzle, pOut, outBytes, 0 );
                (void)astcenc_compress_reset( pContext ); // 다음 밉이 같은 문맥을 쓴다
                if ( status != ASTCENC_SUCCESS )
                {
                    SW_LOG_ERROR_ID( "TextureBlockEncoder.ASTCFailed", "astcenc failed to compress a %#x%# mip: %#", width, height, astcenc_get_error_string( status ) );
                    return false;
                }
                return true;
            }

            /**
             * @brief etcpak 이 읽는 모양으로 바꿉니다 — 블록 수의 배수 크기(가장자리 화소를 되풀이한다)이고 화소는 BGRA 다(etcpak 은 PNG 를 BGR 로 읽어 그 순서를 가정한다).
             */
            static vector<uint32> makeETCSource( const uint8* pRGBA, uint32 width, uint32 height, uint32 paddedWidth, uint32 paddedHeight )
            {
                vector<uint32> pixels( static_cast<size_t>( paddedWidth ) * paddedHeight );
                for ( uint32 y = 0; y < paddedHeight; ++y )
                {
                    for ( uint32 x = 0; x < paddedWidth; ++x )
                    {
                        const uint8* pSource = pRGBA + ( static_cast<size_t>( MathUtil::min( y, height - 1 ) ) * width + MathUtil::min( x, width - 1 ) ) * 4;
                        pixels[static_cast<size_t>( y ) * paddedWidth + x] =
                            static_cast<uint32>( pSource[2] ) | ( static_cast<uint32>( pSource[1] ) << 8 ) | ( static_cast<uint32>( pSource[0] ) << 16 ) | ( static_cast<uint32>( pSource[3] ) << 24 );
                    }
                }
                return pixels;
            }

            /** @brief etcpak 으로 ETC2 RGBA8(EAC 알파 + 색) · EAC R11 · RG11 블록을 @p pOut 에 씁니다. 블록은 줄 우선이고 바이트 배치는 명세 그대로다(etcpak 이 바꿔 쓴다). */
            static void encodeETC( RHIFormat format, const uint8* pRGBA, uint32 width, uint32 height, uint8* pOut )
            {
                const uint32         blocksX = ( width + kBlockSize - 1 ) / kBlockSize;
                const uint32         blocksY = ( height + kBlockSize - 1 ) / kBlockSize;
                const vector<uint32> source  = makeETCSource( pRGBA, width, height, blocksX * kBlockSize, blocksY * kBlockSize );
                const uint32         blocks  = blocksX * blocksY;
                const bool           bTwo    = format != RHIFormat::EAC_R11_UNORM;
                vector<uint64>       encoded( static_cast<size_t>( blocks ) * ( bTwo ? 2 : 1 ) );
                const size_t         width4 = static_cast<size_t>( blocksX ) * kBlockSize;
                if ( format == RHIFormat::ETC2_RGBA8_UNORM )
                    CompressEtc2Rgba( source.data(), encoded.data(), blocks, width4, false ); // 휴리스틱 끔 — 쿠킹은 오프라인이라 planar 를 포함한 모든 모드를 본다(켜면 2 축 그라디언트가 26 dB 로 무너진다)
                else if ( format == RHIFormat::EAC_RG11_UNORM )
                    CompressEacRg( source.data(), encoded.data(), blocks, width4 );
                else
                    CompressEacR( source.data(), encoded.data(), blocks, width4 );
                Memory::copy( pOut, encoded.data(), encoded.size() * sizeof( uint64 ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool TextureBlockEncoder::isEncodable( RHIFormat format )
    {
        return format == RHIFormat::ASTC_4x4_UNORM || format == RHIFormat::ETC2_RGBA8_UNORM || format == RHIFormat::EAC_R11_UNORM || format == RHIFormat::EAC_RG11_UNORM;
    }

    bool TextureBlockEncoder::encodeMip( RHIFormat format, const uint8* pRGBA, uint32 width, uint32 height, vector<uint8>& ioBytes, TaskManager* pTaskManager )
    {
        using Internal = TextureBlockEncoderInternal;
        if ( isEncodable( format ) == false )
        {
            SW_LOG_ERROR_ID( "TextureBlockEncoder.UnknownFormat", "TextureBlockEncoder cannot encode RHIFormat %#", static_cast<uint32>( format ) );
            return false;
        }
        const uint32 blockBytes = RHIFormatUtil::getBlockInfo( format )._blockBytes;
        const uint32 blocksX    = ( width + Internal::kBlockSize - 1 ) / Internal::kBlockSize;
        const uint32 blocksY    = ( height + Internal::kBlockSize - 1 ) / Internal::kBlockSize;
        const size_t offset     = ioBytes.size();
        const size_t mipBytes   = static_cast<size_t>( blocksX ) * blocksY * blockBytes;
        ioBytes.resize( offset + mipBytes );

        vector<Internal::Band> listBand;
        for ( uint32 firstRow = 0; firstRow < height; firstRow += Internal::kBandRows )
        {
            Internal::Band band;
            band._pRGBA    = pRGBA + static_cast<size_t>( firstRow ) * width * RawImageData::kChannelCount;
            band._pOut     = ioBytes.data() + offset + static_cast<size_t>( firstRow / Internal::kBlockSize ) * blocksX * blockBytes;
            band._rowCount = MathUtil::min( Internal::kBandRows, height - firstRow );
            listBand.push_back( band );
        }
        const auto encodeBand = [format, width, blocksX, blockBytes]( uint32, Internal::Band& band )
        {
            if ( format == RHIFormat::ASTC_4x4_UNORM )
            {
                const size_t bandBytes = static_cast<size_t>( ( band._rowCount + Internal::kBlockSize - 1 ) / Internal::kBlockSize ) * blocksX * blockBytes;
                band._bEncoded         = Internal::encodeASTC( band._pRGBA, width, band._rowCount, band._pOut, bandBytes );
                return;
            }
            Internal::encodeETC( format, band._pRGBA, width, band._rowCount, band._pOut );
            band._bEncoded = true;
        };
        if ( pTaskManager != nullptr )
            pTaskManager->runParallelInto<Internal::Band>( listBand, 2, encodeBand );
        else
        {
            for ( Internal::Band& band : listBand )
                encodeBand( 0, band );
        }
        for ( const Internal::Band& band : listBand )
        {
            if ( band._bEncoded )
                continue;
            ioBytes.resize( offset );
            return false;
        }
        return true;
    }
} // namespace sw::editor
