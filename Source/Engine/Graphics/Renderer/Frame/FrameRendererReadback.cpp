/**
 * @file FrameRendererReadback.cpp
 * @brief 첨부를 CPU 로 읽어 오는 길 — 테스트의 픽셀 비교와 `-gv_screenshot` PPM 덤프.
 * @details **프레임 경로가 아니다.** 둘 다 GPU 를 기다리므로(readbackTexture2D) 프레임 안에서 부르면 파이프라인이 멈춘다.
 *          창 캡처가 백엔드마다 되고 안 되고가 갈려서, 네 백엔드를 같은 기준으로 비교하려면 이쪽을 쓴다
 *          (Graphics/README "백엔드 시각 검증"). 그래서 그리는 코드와 파일을 나눠 둔다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    bool FrameRenderer::readbackTransient( string_view attachmentName, vector<uint8>& outBytes, RHITextureMipSpan& outLayout, RHIFormat& outFormat )
    {
        if ( _pDevice == nullptr )
            return false;
        const RHITextureHandle texture = findTransient( attachmentName );
        if ( texture == 0 )
        {
            SW_LOG_ERROR( "readbackTransient: 트랜지언트 '%#' 를 찾지 못했습니다.", string( attachmentName ).c_str() );
            return false;
        }
        outFormat = RHIFormat::R8G8B8A8_UNORM;
        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            if ( att._name == attachmentName )
            {
                outFormat = FrameRendererUtil::parseAttachmentFormat( att._format );
                break;
            }
        }
        if ( _pDevice->getResource()->readbackTexture2D( texture, 0, outBytes, outLayout ) == false )
        {
            SW_LOG_ERROR( "readbackTransient: readbackTexture2D 실패 ('%#').", string( attachmentName ).c_str() );
            return false;
        }
        return outLayout._width != 0 && outLayout._height != 0;
    }

    bool FrameRenderer::dumpTransientToPpm( string_view attachmentName, string_view outFilePath )
    {
        if ( _pDevice == nullptr || outFilePath.empty() )
            return false;

        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        RHIFormat         format = RHIFormat::R8G8B8A8_UNORM;
        if ( readbackTransient( attachmentName, bytes, layout, format ) == false )
            return false;
        const uint32 bytesPerPixel = getRHIFormatBytesPerPixel( format );
        if ( bytesPerPixel < 3 )
        {
            SW_LOG_ERROR( "dumpTransientToPpm: PPM 으로 덤프할 수 없는 포맷입니다 ('%#').", string( attachmentName ).c_str() );
            return false;
        }

        // PPM(P6): 아스키 헤더 + RGB 8bit.
        // **half 첨부를 바이트로 읽으면 안 된다.** HDR 첨부(R16G16B16A16_FLOAT)를 8비트로 가정하고
        // pPixel[0..2] 를 집어 오면 가수 하위 바이트가 색이 되어 **무의미한 그림**이 나온다 —
        // 그런데 bytesPerPixel 은 8 이라 아래 검사도 통과한다. 디퍼드 파이프라인은 LitColor 부터
        // TaaColor 까지 넷이 이 포맷이라, 중간 단계를 눈으로 확인할 길이 그동안 없었다.
        const bool                            bHalf = ( format == RHIFormat::R16G16B16A16_FLOAT );
        const bool                            bBgra = ( format == RHIFormat::B8G8R8A8_UNORM );
        StringBuilder<constant::kMaxBuffer64> header;
        // PPM 헤더의 구분자는 임의의 공백이면 된다 — 공백만 써서 이스케이프 없이 적는다.
        header.appendFormat( "P6 %# %# 255 ", layout._width, layout._height );

        vector<uint8> outBytes;
        outBytes.reserve( header.size() + static_cast<size_t>( layout._width ) * layout._height * 3 );
        outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( header.c_str() ),
                         reinterpret_cast<const uint8*>( header.c_str() ) + header.size() );
        for ( uint32 row = 0; row < layout._height; ++row )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
            for ( uint32 col = 0; col < layout._width; ++col )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( col ) * bytesPerPixel;
                if ( bHalf )
                {
                    // HDR 을 [0,1] 로 자르고 8비트로 옮긴다. 톤매핑은 하지 않는다 — 이 덤프는
                    // 그림을 예쁘게 보려는 게 아니라 "무엇이 들어 있나" 를 보려는 것이다.
                    const uint16* pHalf = reinterpret_cast<const uint16*>( pPixel );
                    for ( uint32 channel = 0; channel < 3; ++channel )
                        outBytes.push_back( FrameRendererUtil::halfToUnorm8( pHalf[channel] ) );
                    continue;
                }
                outBytes.push_back( bBgra ? pPixel[2] : pPixel[0] );
                outBytes.push_back( pPixel[1] );
                outBytes.push_back( bBgra ? pPixel[0] : pPixel[2] );
            }
        }

        if ( FileUtil::writeFile( outFilePath, outBytes.data(), outBytes.size() ) == false )
        {
            SW_LOG_ERROR( "dumpTransientToPpm: 파일 쓰기 실패 (%#).", string( outFilePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Screenshot: '%#' %#×%# -> %#", string( attachmentName ).c_str(), layout._width, layout._height,
                     string( outFilePath ).c_str() );
        return true;
    }
} // namespace sw
