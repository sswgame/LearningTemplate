/**
 * @file FrameRendererReadback.cpp
 * @brief 첨부를 CPU 로 읽어 오는 길입니다. 테스트의 픽셀 비교, `-gv_screenshot` PPM 덤프, 실행 중 스크린샷(`RenderThread::requestScreenshot` — PNG)이 씁니다.
 * @details **프레임 경로가 아닙니다.** 둘 다 GPU 를 기다리므로(readbackTexture2D) 프레임 안에서 부르면 파이프라인이 멈춥니다.
 *          창 캡처가 백엔드마다 되고 안 되고가 갈려서, 네 백엔드를 같은 기준으로 비교하려면 이쪽을 씁니다
 *          (Graphics/README "백엔드 시각 검증"). 그래서 그리는 코드와 파일을 나눠 둡니다.
 *          바이트를 파일로 바꾸는 일은 `RenderTargetImageFile`, Present 캡처 텍스처는 `PresentCaptureTarget` 이 맡습니다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/RenderTargetImageFile.h"

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
        // 깊이 첨부는 네 백엔드 모두 텍셀마다 0..1 float32 하나로 읽힌다(스텐실 없음). 선언 포맷(D24S8)을 그대로 넘기면 바이트를 색으로 잘못 푼다.
        if ( FrameRendererUtil::isDepthFormat( outFormat ) )
            outFormat = kRHIDepthReadbackFormat;
        return outLayout._width != 0 && outLayout._height != 0;
    }

    bool FrameRenderer::readbackPresentCapture( vector<uint8>& outByte, RHITextureMipSpan& outLayout )
    {
        return _presentCapture.readback( _pDevice, outByte, outLayout );
    }

    bool FrameRenderer::dumpPresentCaptureToFile( string_view outFilePath )
    {
        if ( outFilePath.empty() )
            return false;

        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        if ( readbackPresentCapture( bytes, layout ) == false )
            return false;
        return RenderTargetImageFile::writeImageFile( bytes, layout, constant::kBackBufferFormat, outFilePath );
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
        return RenderTargetImageFile::writeImageFile( bytes, layout, format, outFilePath );
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
        return RenderTargetImageFile::writeImageFile( bytes, layout, format, outFilePath );
    }
} // namespace sw
