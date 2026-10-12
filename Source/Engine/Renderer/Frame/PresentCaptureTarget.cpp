#include "pch.h"

#include "Engine/Renderer/Frame/PresentCaptureTarget.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    // 로그 머리는 옮기기 전과 같게 둔다(로그를 읽는 스크립트와 사람이 같은 이름으로 찾는다).
    SW_LOG_CALLER( "FrameRenderer" );

    PresentCaptureTarget::PresentCaptureTarget()
        : _texture{ 0 }
        , _width{ 0 }
        , _height{ 0 }
        , _bEnabled{ SW_FALSE }
        , _bMismatchLogged{ 0 }
    {
    }

    void PresentCaptureTarget::setEnabled( IRHIDevice* pDevice, bool bEnabled )
    {
        _bEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        if ( bEnabled )
            return;
        if ( _texture != 0 && pDevice != nullptr )
        {
            pDevice->getResourceFactory()->destroyTexture( _texture );
            _texture = 0;
        }
    }

    void PresentCaptureTarget::ensure( IRHIDevice* pDevice, uint32 outputWidth, uint32 outputHeight )
    {
        if ( pDevice == nullptr || _bEnabled == SW_FALSE )
            return;
        // 캡처는 주 출력과 크기가 같아야 한다(주 시점 · 화면 사각형 뷰가 그 안에 그린다). 출력 크기가 바뀌었으면 다시 만든다.
        const uint32 width  = outputWidth != 0 ? outputWidth : FrameRendererUtil::kDefaultTransientSize;
        const uint32 height = outputHeight != 0 ? outputHeight : FrameRendererUtil::kDefaultTransientSize;
        if ( _texture != 0 )
        {
            if ( _width == width && _height == height )
                return;
            pDevice->getResourceFactory()->destroyTexture( _texture );
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
        _texture                       = pDevice->getResourceFactory()->createTexture2D( captureDesc );
        _width                         = width;
        _height                        = height;
    }

    void PresentCaptureTarget::release( IRHIDevice* pDevice )
    {
        if ( _texture != 0 && pDevice != nullptr )
            pDevice->getResourceFactory()->destroyTexture( _texture );
        _texture = 0;
    }

    bool PresentCaptureTarget::matchesOutput( uint32 width, uint32 height, RHIFormat format )
    {
        // 캡처는 계약 포맷(kBackBufferFormat)으로 출력 크기에 맞춰 만든다(ensure). 게임 뷰 RT 도 같은 포맷이라 그대로 복사된다.
        const bool bSameSize   = _width == width && _height == height;
        const bool bSameFormat = format == constant::kBackBufferFormat;
        if ( bSameSize && bSameFormat )
            return true;
        if ( _bMismatchLogged.exchange( 1 ) == 0 )
            SW_LOG_WARNING( "Present capture skipped - the output render target (%#x%#, format %#) does not match the capture (%#x%#, format %#)",
                            width, height, static_cast<uint32>( format ), _width, _height, static_cast<uint32>( constant::kBackBufferFormat ) );
        return false;
    }

    void PresentCaptureTarget::copyFromOutput( IRHICommandList& cmd, RHITextureHandle output, uint32 width, uint32 height, RHIFormat format )
    {
        if ( matchesOutput( width, height, format ) == false )
            return;
        cmd.blitTexture( output, _texture );
    }

    bool PresentCaptureTarget::readback( IRHIDevice* pDevice, vector<uint8>& outBytes, RHITextureMipSpan& outLayout ) const
    {
        if ( pDevice == nullptr || isActive() == false )
            return false;
        if ( pDevice->getResourceFactory()->readbackTexture2D( _texture, 0, 0, outBytes, outLayout ) == false )
        {
            SW_LOG_ERROR( "readbackPresentCapture: readbackTexture2D 실패." );
            return false;
        }
        return outLayout._width != 0 && outLayout._height != 0;
    }
} // namespace sw
