/**
 * @file D3D11RHIDeviceSubmission.cpp
 * @brief 프레임 시작·종료와 커맨드 리스트 제출 (DX12 · Vulkan · GL 의 같은 이름 파일과 같은 자리).
 */
#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    void D3D11RHIDevice::beginFrame( const float4& clearColor )
    {
        if ( _deviceContext == nullptr || _swapChain.isValid() == false )
            return;

        // FLIP_DISCARD 는 백버퍼를 돌려 쓴다 — Present 가 보여줄 그 버퍼에 그리도록 매 프레임 다시 잡는다.
        _swapChain.acquireNextImage( _device.Get() );
        // 정적 샘플러 세트는 컨텍스트 상태라 ClearState 로 사라질 수 있다 — 프레임마다 다시 건다(값싸다).
        bindStaticSamplers( _deviceContext.Get() );
        if ( _swapChain.getBackBufferRtv() == nullptr )
            return;

        // 백버퍼 바인딩/클리어는 더 이상 여기서 하지 않는다 — beginFrame 은 프레임 수명주기 전용이고,
        // 백버퍼를 타깃으로 삼는 건 beginRenderPass(핸들 0) 가 명시적으로 한다
        // (docs/05_RHI_FrameContract.md S2). RTV 재취득은 FLIP_DISCARD 때문에 수명주기에 속한다.
        (void)clearColor;

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportY        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        D3D11_VIEWPORT viewport;
        viewport.Width    = static_cast<float32>( _swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( _swapChain.getHeight() );
        viewport.MinDepth = kDefaultViewportMinDepth;
        viewport.MaxDepth = kDefaultViewportMaxDepth;
        viewport.TopLeftX = kDefaultViewportX;
        viewport.TopLeftY = kDefaultViewportY;
        _deviceContext->RSSetViewports( 1, &viewport );
    }

    void D3D11RHIDevice::endFrame( bool vsync, bool bPresent )
    {
    #if defined( SW_DEBUG )
        flushDebugMessages( "endFrame" );
    #endif
        if ( _swapChain.isValid() == false )
            return;

        _swapChain.releaseBackBufferRtv();

        if ( bPresent )
        {
            const HRESULT hr = _swapChain.present( vsync );
            if ( FAILED( hr ) )
                SW_LOG_ERROR( "Present failed hr=%#", static_cast<uint32>( hr ) );
        }

        _releaseQueue.tickFrame();
    }

    void D3D11RHIDevice::waitIdle()
    {
        if ( _deviceContext != nullptr )
            _deviceContext->Flush();
        _releaseQueue.flushAll();
    }

    unique_ptr<IRHICommandList> D3D11RHIDevice::createCommandList()
    {
        unique_ptr<D3D11RHICommandList> list = make_unique<D3D11RHICommandList>( this );
        if ( list->isValid() == false )
        {
            SW_LOG_ERROR( "D3D11RHIDevice::createCommandList: 네이티브 Deferred Context 생성 실패." );
            return nullptr;
        }
        return list;
    }

    void D3D11RHIDevice::registerCommandList( D3D11RHICommandList* pCmdList )
    {
        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        _listLiveCmdList.push_back( pCmdList );
    }

    void D3D11RHIDevice::unregisterCommandList( D3D11RHICommandList* pCmdList )
    {
        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        for ( size_t index = 0; index < _listLiveCmdList.size(); ++index )
        {
            if ( _listLiveCmdList[index] != pCmdList )
                continue;
            _listLiveCmdList[index] = _listLiveCmdList.back();
            _listLiveCmdList.pop_back();
            return;
        }
    }

    void D3D11RHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        if ( pCmdList == nullptr || _deviceContext == nullptr )
            return;
        auto*              pNative = static_cast<D3D11RHICommandList*>( pCmdList );
        ID3D11CommandList* pList   = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;
        // DX11 은 스트림을 자를 필요가 없다. 기록 대상(Deferred Context)과 제출 대상(Immediate
        // Context)이 처음부터 분리돼 있어서, 이 호출은 Immediate Context 스트림의 '지금 이 지점'에
        // 그대로 끼워진다 — DX12/Vulkan 이 세그먼트를 잘라 얻는 순서 보장을 공짜로 갖는다.
        _deviceContext->ExecuteCommandList( pList, FALSE );

        // 남은 차이는 제출 시점뿐이다. Immediate Context 는 커맨드를 모아뒀다가 드라이버가 정한
        // 때(보통 Present)에 GPU 로 보내므로, 오류가 나면 어느 리스트 때문인지 알 수 없다.
        // 즉시 모드에서는 리스트마다 밀어내 그 경계에서 오류가 드러나게 한다.
        if ( _bImmediateSubmit )
            _deviceContext->Flush();
    }
} // namespace sw
#endif
