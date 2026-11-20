/**
 * @file D3D11RHISwapChain.h
 * @brief 창 하나에 붙는 D3D11 백버퍼입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX/DXGIColorSpace.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    /**
     * @class D3D11RHISwapChain
     * @brief 창 하나의 백버퍼입니다. **스왑체인을 넘겨받아 보관하고, 크기를 바꾸고, RTV 를 다시 잡고, 화면에 표시합니다.**
     * @details D3D11 은 메인 창의 스왑체인이 디바이스와 `D3D11CreateDeviceAndSwapChain` 한 호출에서 **함께**
     *          태어납니다. 그래서 메인 창은 `attach` 로 넘겨받아 소유하고, 다른 창(`IRHIDevice::createSwapChain`)은 `createLike` 가
     *          메인 창의 서술을 그대로 따라 만듭니다(포맷 · 버퍼 수 · 티어링 플래그가 같아야 같은 Present 경로를 쓴다).
     * @note 백버퍼 RTV 를 **매 프레임 다시 잡습니다.** `DXGI_SWAP_EFFECT_FLIP_DISCARD` 는 백버퍼를
     *       돌려 쓰기 때문에, 지난 프레임의 RTV 를 그대로 쓰면 Present 가 보여 줄 버퍼와 다른 버퍼에
     *       그리게 됩니다.
     */
    class D3D11RHISwapChain final : public IRHISwapChain
    {
    public:
        D3D11RHISwapChain()                                      = default;
        ~D3D11RHISwapChain() override                            = default;
        D3D11RHISwapChain( const D3D11RHISwapChain& )            = delete;
        D3D11RHISwapChain& operator=( const D3D11RHISwapChain& ) = delete;

        /**
         * @brief 디바이스와 함께 만들어진 스왑체인을 넘겨받습니다.
         * @param swapChainFlags 생성에 쓴 DXGI 플래그. `ResizeBuffers` 가 같은 값을 다시 넘겨야 합니다.
         * @return DXGI 창 연결(Alt+Enter)을 끄지 못하면 false 이고 넘겨받지 않습니다.
         */
        [[nodiscard]] bool attach( IDXGISwapChain* pSwapChain, HWND hWnd, uint32 width, uint32 height, uint32 swapChainFlags, RHIFormat sdrFormat );

        /** @brief @p source(메인 창)와 같은 서술로 @p hWnd 에 스왑체인을 만듭니다. 실패하면 이유를 남기고 false 입니다. */
        [[nodiscard]] bool createLike( ID3D11Device* pDevice, const D3D11RHISwapChain& source, HWND hWnd, uint32 width, uint32 height );

        /** @brief 백버퍼 RTV 와 스왑체인을 모두 놓습니다. */
        void shutdown();

        /**
         * @brief 백버퍼 크기를 바꿉니다.
         * @warning 부르기 전에 RTV 를 놓고 컨텍스트 상태를 비워야 합니다. 백버퍼 참조가 하나라도
         *          남아 있으면 `ResizeBuffers` 가 실패합니다.
         */
        bool resize( uint32 width, uint32 height );

        /**
         * @brief 색 공간을 바꿉니다 — 그 색 공간의 포맷(`getSwapChainFormat`)으로 `ResizeBuffers` 한 뒤 `SetColorSpace1` · `SetHDRMetaData` 를 알린다.
         * @warning `resize` 와 같다 — RTV 를 놓고 컨텍스트 상태를 비운 뒤 부릅니다. 실패하면 옛 포맷 · 색 공간 그대로입니다.
         */
        [[nodiscard]] bool setColorSpace( RHIColorSpace colorSpace, const RHIDisplayMetadata& metadata );
        /** @brief 색 공간은 그대로 두고 HDR10 마스터링 값만 다시 알립니다. HDR10 이 아니면 값만 기억합니다. 실패하면 옛 값을 다시 알리고 false 입니다. */
        [[nodiscard]] bool setDisplayMetadata( const RHIDisplayMetadata& metadata );

        /** @brief 이번 프레임이 그릴 백버퍼의 RTV 를 새로 잡습니다. */
        void acquireNextImage( ID3D11Device* pDevice );

        /** @brief 백버퍼 RTV 를 놓습니다. */
        void releaseBackBufferRTV();

        /**
         * @brief 화면에 표시합니다.
         * @details VSync 를 끈 경우 티어링 플래그를 함께 넘깁니다. 스왑체인 생성 플래그와 **짝이어야**
         *          합니다(RHIDxgiTearing.h).
         */
        HRESULT present( bool vsync );

        bool isValid() const { return _swapChain != nullptr; }

        /** @brief 백버퍼 포맷입니다 — 색 공간에서 정한다(`getSwapChainFormat`). */
        RHIFormat getFormat() const { return getSwapChainFormat( _colorSpace, _sdrFormat ); }
        /** @brief 지금 화면에 내는 색 공간입니다. */
        RHIColorSpace getColorSpace() const { return _colorSpace; }
        /** @brief `ResizeBuffers` 에 다시 넘길 서술입니다(버퍼 수 0 = 그대로). */
        DXGISwapChainBuffers makeBuffers() const { return DXGISwapChainBuffers{ _swapChain.Get(), 0u, _width, _height, _swapChainFlags, _sdrFormat }; }
        /** @brief 창이 놓인 모니터가 받는 출력입니다(넘겨받을 때 · 크기 · 색 공간을 바꿀 때 묻는다). */
        const DXGIDisplayOutputSupport& getDisplayOutputSupport() const { return _displayOutputSupport; }

        uint32 getWidth() const override { return _width; }
        uint32 getHeight() const override { return _height; }

        /** @brief 현재 백버퍼의 RTV 를 반환합니다. 아직 잡지 않았으면 nullptr 입니다. */
        ID3D11RenderTargetView* getBackBufferRTV() const { return _backBufferRTV.Get(); }

        /** @brief 백버퍼 텍스처를 새로 얻습니다(복사 대상용). 실패하면 nullptr 입니다. */
        Microsoft::WRL::ComPtr<ID3D11Texture2D> getBackBufferTexture() const;

    private:
        Microsoft::WRL::ComPtr<IDXGISwapChain>         _swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _backBufferRTV;

        HWND   _pHWnd{ nullptr };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
        /// @brief 생성할 때 쓴 DXGI 플래그입니다. `ResizeBuffers` 가 같은 값을 다시 넘겨야 합니다.
        uint32 _swapChainFlags{ 0 };
        /// @brief SDR 일 때의 백버퍼 포맷입니다(다른 창은 늘 이 포맷의 SDR 로 만든다).
        RHIFormat _sdrFormat{ constant::render::kBackBufferFormat };
        /// @brief 지금 화면에 내는 색 공간입니다. `setColorSpace` 만 바꾼다.
        RHIColorSpace _colorSpace{ RHIColorSpace::SRGBNonLinear };
        /// @brief 창이 놓인 모니터가 받는 출력입니다.
        DXGIDisplayOutputSupport _displayOutputSupport{};
        /// @brief 마지막으로 알린 HDR10 마스터링 값입니다 — `ResizeBuffers` 뒤에 다시 알린다.
        RHIDisplayMetadata _displayMetadata{};
    };
} // namespace sw
#endif
