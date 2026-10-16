#include "pch.h"

#include "Engine/Renderer/Frame/Capture/FrameCapture.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/Capture/RenderTargetImageFile.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
    // 로그 머리는 렌더러와 같게 둔다(로그를 읽는 스크립트와 사람이 같은 이름으로 찾는다).
    SW_LOG_CALLER( "FrameRenderer" );

    FrameCapture::FrameCapture( IRHIDevice* const& pDevice, const TransientAttachmentPool& mainPool, const RenderPipelineAsset& pipeline )
        : _pDevice{ pDevice }
        , _mainPool{ mainPool }
        , _pipeline{ pipeline }
        , _texture{ 0 }
        , _width{ 0 }
        , _height{ 0 }
        , _outputWidth{ 0 }
        , _outputHeight{ 0 }
        , _bEnabled{ SW_FALSE }
        , _bMismatchLogged{ 0 }
    {
    }

    void FrameCapture::setPresentEnabled( bool bEnabled )
    {
        _bEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        if ( bEnabled )
        {
            ensurePresent( _outputWidth, _outputHeight );
            return;
        }
        if ( _texture != 0 && _pDevice != nullptr )
        {
            _pDevice->getResourceFactory()->destroyTexture( _texture );
            _texture = 0;
        }
    }

    void FrameCapture::ensurePresent( uint32 outputWidth, uint32 outputHeight )
    {
        _outputWidth  = outputWidth;
        _outputHeight = outputHeight;
        if ( _pDevice == nullptr || _bEnabled == SW_FALSE )
            return;
        // 캡처는 주 출력과 크기가 같아야 한다(주 시점과 화면 사각형 뷰가 그 안에 그린다). 출력 크기가 바뀌었으면 다시 만든다.
        const uint32 width  = outputWidth != 0 ? outputWidth : FrameRendererUtil::kDefaultTransientSize;
        const uint32 height = outputHeight != 0 ? outputHeight : FrameRendererUtil::kDefaultTransientSize;
        if ( _texture != 0 )
        {
            if ( _width == width && _height == height )
                return;
            _pDevice->getResourceFactory()->destroyTexture( _texture );
            _texture = 0;
        }

        // 포맷은 **계약값**이다. 백버퍼가 실제로 무엇을 채택했든(Vulkan 은 서피스 협상 결과) 캡처는
        // 늘 같은 포맷이라 PPM 으로 푸는 쪽이 한 가지만 알면 된다. Present PSO 변종에도 이 포맷이 있다.
        RHITextureDesc captureDesc{};
        captureDesc._width             = width;
        captureDesc._height            = height;
        captureDesc._format            = constant::kBackBufferFormat;
        captureDesc._bIsRenderTarget   = SW_TRUE;
        captureDesc._bIsShaderResource = SW_TRUE;
        _texture                       = _pDevice->getResourceFactory()->createTexture2D( captureDesc );
        _width                         = width;
        _height                        = height;
    }

    void FrameCapture::releasePresent()
    {
        if ( _texture != 0 && _pDevice != nullptr )
            _pDevice->getResourceFactory()->destroyTexture( _texture );
        _texture = 0;
    }

    bool FrameCapture::matchesOutput( uint32 width, uint32 height, RHIFormat format )
    {
        // 캡처는 계약 포맷(kBackBufferFormat)으로 출력 크기에 맞춰 만든다(ensurePresent). 게임 뷰 RT 도 같은 포맷이라 그대로 복사된다.
        const bool bSameSize   = _width == width && _height == height;
        const bool bSameFormat = format == constant::kBackBufferFormat;
        if ( bSameSize && bSameFormat )
            return true;
        // 포맷을 모르는 출력은 아직(또는 이미) 없는 렌더 타깃이다. 에디터 모듈을 다시 로드하는 프레임의 게임 뷰가 그 예다. 어긋남이 아니라 건너뛸 프레임이다.
        if ( format == RHIFormat::Unknown )
            return false;
        if ( _bMismatchLogged.exchange( 1 ) == 0 )
            SW_LOG_WARNING( "Present capture skipped - the output render target (%#x%#, format %#) does not match the capture (%#x%#, format %#)",
                            width, height, static_cast<uint32>( format ), _width, _height, static_cast<uint32>( constant::kBackBufferFormat ) );
        return false;
    }

    void FrameCapture::copyPresentFromOutput( IRHICommandList& cmd, RHITextureHandle output, uint32 width, uint32 height, RHIFormat format )
    {
        if ( matchesOutput( width, height, format ) )
            cmd.blitTexture( output, _texture );
    }

    bool FrameCapture::readbackPresent( vector<uint8>& outBytes, RHITextureMipSpan& outLayout ) const
    {
        if ( _pDevice == nullptr || isPresentActive() == false )
            return false;
        if ( _pDevice->getResourceFactory()->readbackTexture2D( _texture, 0, 0, outBytes, outLayout ) == false )
        {
            SW_LOG_ERROR( "readbackPresent: readbackTexture2D 실패." );
            return false;
        }
        return outLayout._width != 0 && outLayout._height != 0;
    }

    bool FrameCapture::dumpPresentToFile( string_view outFilePath ) const
    {
        if ( outFilePath.empty() )
            return false;

        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        if ( readbackPresent( bytes, layout ) == false )
            return false;
        return RenderTargetImageFile::writeImageFile( bytes, layout, constant::kBackBufferFormat, outFilePath );
    }

    bool FrameCapture::readbackTransient( string_view attachmentName, vector<uint8>& outBytes, RHITextureMipSpan& outLayout, RHIFormat& outFormat ) const
    {
        if ( _pDevice == nullptr )
            return false;
        const RHITextureHandle texture = _mainPool.findTexture( attachmentName );
        if ( texture == 0 )
        {
            SW_LOG_ERROR( "readbackTransient: 트랜지언트 '%#' 를 찾지 못했습니다.", string( attachmentName ).c_str() );
            return false;
        }
        outFormat = RHIFormat::R8G8B8A8_UNORM;
        for ( const RenderPassAttachment& attachment : _pipeline.getDesc()._listAttachment )
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

    bool FrameCapture::dumpTransientToFile( string_view attachmentName, string_view outFilePath ) const
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

    bool FrameCapture::dumpTextureToFile( RHITextureHandle texture, RHIFormat format, string_view outFilePath ) const
    {
        const bool bInvalidDumpArguments = _pDevice == nullptr || texture == 0 || outFilePath.empty();
        if ( bInvalidDumpArguments )
            return false;

        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        const bool        bReadbackFailed = _pDevice->getResourceFactory()->readbackTexture2D( texture, 0, 0, bytes, layout ) == false || layout._width == 0 || layout._height == 0;
        if ( bReadbackFailed )
        {
            SW_LOG_ERROR( "dumpTextureToFile: readbackTexture2D failed (%#).", string( outFilePath ).c_str() );
            return false;
        }
        return RenderTargetImageFile::writeImageFile( bytes, layout, format, outFilePath );
    }
} // namespace sw
