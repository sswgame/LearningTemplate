/**
 * @file D3D11RHISwapChain.h
 * @brief 창 하나에 붙는 D3D11 백버퍼
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    /**
     * @class D3D11RHISwapChain
     * @brief 창 하나의 백버퍼. **받아서 갖고 · 크기를 바꾸고 · RTV 를 다시 잡고 · 표시한다.**
     * @details D3D11 은 디바이스와 스왑체인이 `D3D11CreateDeviceAndSwapChain` 한 호출에서 **함께**
     *          태어난다. 그래서 이 클래스는 스왑체인을 만들지 않고 `attach` 로 넘겨받아 소유한다 —
     *          DX12/Vulkan 이 스왑체인을 따로 만드는 것과 다른 점이고, 세대 차이가 드러나는 자리다.
     * @note 백버퍼 RTV 를 **매 프레임 다시 잡는다.** `DXGI_SWAP_EFFECT_FLIP_DISCARD` 는 백버퍼를
     *       돌려 쓰기 때문에, 지난 프레임의 RTV 를 그대로 쓰면 Present 가 보여줄 버퍼와 다른 버퍼에
     *       그리게 된다.
     */
    class D3D11RHISwapChain
    {
    public:
        D3D11RHISwapChain()                                      = default;
        ~D3D11RHISwapChain()                                     = default;
        D3D11RHISwapChain( const D3D11RHISwapChain& )            = delete;
        D3D11RHISwapChain& operator=( const D3D11RHISwapChain& ) = delete;

        /** @brief 디바이스와 함께 만들어진 스왑체인을 넘겨받습니다. */
        void attach( IDXGISwapChain* pSwapChain, HWND hWnd, uint32 width, uint32 height );

        /** @brief 백버퍼 RTV 와 스왑체인을 모두 놓습니다. */
        void shutdown();

        /**
         * @brief 백버퍼 크기를 바꿉니다.
         * @warning 호출 전에 RTV 를 놓고 컨텍스트 상태를 비워야 합니다 — 백버퍼 참조가 하나라도
         *          남아 있으면 `ResizeBuffers` 가 실패합니다.
         */
        bool resize( uint32 width, uint32 height );

        /** @brief 이번 프레임이 그릴 백버퍼의 RTV 를 새로 잡습니다. */
        void acquireNextImage( ID3D11Device* pDevice );

        /** @brief 백버퍼 RTV 를 놓습니다. */
        void releaseBackBufferRtv();

        /** @brief 화면에 표시합니다. */
        HRESULT present( bool vsync );

        bool isValid() const { return _swapChain != nullptr; }

        uint32 getWidth() const { return _width; }
        uint32 getHeight() const { return _height; }

        /** @brief 현재 백버퍼의 RTV. 아직 잡지 않았으면 nullptr. */
        ID3D11RenderTargetView* getBackBufferRtv() const { return _backBufferRtv.Get(); }

        /** @brief 백버퍼 텍스처를 새로 얻습니다(복사 대상용). 실패하면 nullptr. */
        Microsoft::WRL::ComPtr<ID3D11Texture2D> getBackBufferTexture() const;

        IDXGISwapChain* getNative() const { return _swapChain.Get(); }

    private:
        Microsoft::WRL::ComPtr<IDXGISwapChain>         _swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _backBufferRtv;

        HWND   _pHWnd{ nullptr };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
    };
} // namespace sw
#endif
