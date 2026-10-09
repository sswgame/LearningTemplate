/**
 * @file FrameRendererReadback.cpp
 * @brief 첨부를 CPU 로 읽어 오는 길입니다. 테스트의 픽셀 비교, `-gv_screenshot` PPM 덤프, 실행 중 스크린샷(`RenderThread::requestScreenshot` — PNG)이 씁니다.
 * @details **프레임 경로가 아닙니다.** 둘 다 GPU 를 기다리므로(readbackTexture2D) 프레임 안에서 부르면 파이프라인이 멈춥니다.
 *          창 캡처가 백엔드마다 되고 안 되고가 갈려서, 네 백엔드를 같은 기준으로 비교하려면 이쪽을 씁니다
 *          (Graphics/README "백엔드 시각 검증"). 그래서 그리는 코드와 파일을 나눠 둡니다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Resource/Image/ImageFileWriter.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    namespace
    {
        struct FrameRendererReadbackInternal
        {
            /**
             * @brief 읽어 온 픽셀을 빈틈없는 RGBA8(위 행부터, 알파 255)로 풉니다. PPM · PNG 가 같이 씁니다. 3 채널 미만 포맷이면 false 입니다.
             * @details **half 첨부를 바이트로 읽으면 안 된다.** HDR 첨부(R16G16B16A16_FLOAT)를 8비트로 가정하고 앞 세 바이트를 집으면
             *          가수 하위 바이트가 색이 되어 무의미한 그림이 나온다(bytesPerPixel 은 8 이라 크기 검사도 통과한다). 디퍼드 파이프라인은
             *          LitColor 부터 TaaColor 까지 넷이 이 포맷이다. HDR 은 [0,1] 로 자르고 톤매핑은 하지 않는다 — "무엇이 들어 있나" 를 보는 덤프다.
             */
            static bool unpackRgba8( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, vector<uint8>& outRgbaBytes )
            {
                const uint32 bytesPerPixel = getRhiFormatBytesPerPixel( format );
                if ( bytesPerPixel < 3 )
                    return false;
                const bool bHalf = ( format == RHIFormat::R16G16B16A16_FLOAT );
                const bool bBgra = ( format == RHIFormat::B8G8R8A8_UNORM );
                outRgbaBytes.clear();
                outRgbaBytes.reserve( static_cast<size_t>( layout._width ) * layout._height * 4 );
                for ( uint32 row = 0; row < layout._height; ++row )
                {
                    const uint8* pRow = byte.data() + static_cast<size_t>( row ) * layout._rowBytes;
                    for ( uint32 column = 0; column < layout._width; ++column )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( column ) * bytesPerPixel;
                        if ( bHalf )
                        {
                            const uint16* pHalf = reinterpret_cast<const uint16*>( pPixel );
                            for ( uint32 channel = 0; channel < 3; ++channel )
                            {
                                outRgbaBytes.push_back( FrameRendererUtil::halfToUnorm8( pHalf[channel] ) );
                            }
                        }
                        else
                        {
                            outRgbaBytes.push_back( bBgra ? pPixel[2] : pPixel[0] );
                            outRgbaBytes.push_back( pPixel[1] );
                            outRgbaBytes.push_back( bBgra ? pPixel[0] : pPixel[2] );
                        }
                        outRgbaBytes.push_back( 255 );
                    }
                }
                return true;
            }

            /** @brief RGBA8 을 PPM(P6 — 아스키 머리말 + RGB 8bit)으로 씁니다. */
            [[nodiscard]] static bool writePpm( string_view outFilePath, const vector<uint8>& rgbaBytes, uint32 width, uint32 height )
            {
                StringBuilder<constant::kMaxBuffer64> header;
                // PPM 헤더의 구분자는 아무 공백이면 된다. 공백만 써서 이스케이프 없이 적는다.
                header.appendFormat( "P6 %# %# 255 ", width, height );
                const size_t  pixelCount = static_cast<size_t>( width ) * height;
                vector<uint8> outBytes;
                outBytes.reserve( header.size() + pixelCount * 3 );
                outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( header.c_str() ), reinterpret_cast<const uint8*>( header.c_str() ) + header.size() );
                for ( size_t pixel = 0; pixel < pixelCount; ++pixel )
                {
                    outBytes.push_back( rgbaBytes[pixel * 4 + 0] );
                    outBytes.push_back( rgbaBytes[pixel * 4 + 1] );
                    outBytes.push_back( rgbaBytes[pixel * 4 + 2] );
                }
                return FileUtil::writeFile( outFilePath, outBytes.data(), outBytes.size() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{

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
        for ( const RenderPassAttachment& attachment : _pipelineResource.getDesc()._listAttachment )
        {
            if ( attachment._name == attachmentName )
            {
                outFormat = FrameRendererUtil::parseAttachmentFormat( attachment._format );
                break;
            }
        }
        if ( _pDevice->getResourceFactory()->readbackTexture2D( texture, 0, 0, outBytes, outLayout ) == false )
        {
            SW_LOG_ERROR( "readbackTransient: readbackTexture2D 실패 ('%#').", string( attachmentName ).c_str() );
            return false;
        }
        return outLayout._width != 0 && outLayout._height != 0;
    }

    bool FrameRenderer::readbackPresentCapture( vector<uint8>& outByte, RHITextureMipSpan& outLayout )
    {
        if ( _pDevice == nullptr || isPresentCaptureEnabled() == false )
            return false;
        if ( _pDevice->getResourceFactory()->readbackTexture2D( _presentCapture, 0, 0, outByte, outLayout ) == false )
        {
            SW_LOG_ERROR( "readbackPresentCapture: readbackTexture2D 실패." );
            return false;
        }
        return outLayout._width != 0 && outLayout._height != 0;
    }

    bool FrameRenderer::dumpPresentCaptureToFile( string_view outFilePath )
    {
        if ( outFilePath.empty() )
            return false;

        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        if ( readbackPresentCapture( bytes, layout ) == false )
            return false;
        return writeImageFile( bytes, layout, constant::kBackBufferFormat, outFilePath );
    }

    bool FrameRenderer::dumpTransientToFile( string_view attachmentName, string_view outFilePath )
    {
        if ( _pDevice == nullptr || outFilePath.empty() )
            return false;

        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        RHIFormat         format = RHIFormat::R8G8B8A8_UNORM;
        if ( readbackTransient( attachmentName, bytes, layout, format ) == false )
            return false;
        return writeImageFile( bytes, layout, format, outFilePath );
    }

    bool FrameRenderer::dumpTextureToFile( RHITextureHandle texture, RHIFormat format, string_view outFilePath )
    {
        if ( _pDevice == nullptr || texture == 0 || outFilePath.empty() )
            return false;

        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        if ( _pDevice->getResourceFactory()->readbackTexture2D( texture, 0, 0, bytes, layout ) == false || layout._width == 0 || layout._height == 0 )
        {
            SW_LOG_ERROR( "dumpTextureToFile: readbackTexture2D failed (%#).", string( outFilePath ).c_str() );
            return false;
        }
        return writeImageFile( bytes, layout, format, outFilePath );
    }

    bool FrameRenderer::writeImageFile( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, string_view outFilePath )
    {
        vector<uint8> rgbaBytes;
        if ( FrameRendererReadbackInternal::unpackRgba8( byte, layout, format, rgbaBytes ) == false )
        {
            SW_LOG_ERROR( "writeImageFile: format %# cannot be written as an image.", static_cast<uint32>( format ) );
            return false;
        }

        bool bWritten = false;
        if ( StringUtil::endsWith( outFilePath, ".png", true ) )
            bWritten = ImageFileWriter::writePngRgba8( outFilePath, rgbaBytes, layout._width, layout._height );
        else
            bWritten = FrameRendererReadbackInternal::writePpm( outFilePath, rgbaBytes, layout._width, layout._height );
        if ( bWritten == false )
        {
            SW_LOG_ERROR( "writeImageFile: could not write %#.", string( outFilePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Screenshot: %#×%# -> %#", layout._width, layout._height, string( outFilePath ).c_str() );
        return true;
    }
} // namespace sw
