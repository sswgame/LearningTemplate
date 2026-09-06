/**
 * @file D3D12RHISwapChain.h
 * @brief 창 하나에 붙는 D3D12 백버퍼 묶음
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    /**
     * @class D3D12RHISwapChain
     * @brief 창 하나의 백버퍼 묶음. **만들고 · 크기를 바꾸고 · 다음 백버퍼를 고르고 · 표시한다.**
     * @details 예전엔 이 상태가 전부 `D3D12RHIDevice` 의 멤버로 흩어져 있어서, 백버퍼 리소스 상태를
     *          바꾸는 코드가 세 곳(`beginRenderPass` / `blitTexture` / `endFrame`)에 각자 복사돼
     *          있었다. 상태와 그 상태를 바꾸는 배리어를 한 객체가 함께 가지면 어긋날 수 없다.
     * @note RTV 힙은 **디바이스가 소유한다** — 오프스크린 렌더타깃과 같은 힙을 쓰기 때문이다.
     *       백버퍼는 그 힙의 앞쪽 `bufferCount()` 칸을 차지하고, 오프스크린은 그 뒤부터 쓴다.
     */
    class D3D12RHISwapChain
    {
    public:
        D3D12RHISwapChain()                                      = default;
        ~D3D12RHISwapChain()                                     = default;
        D3D12RHISwapChain( const D3D12RHISwapChain& )            = delete;
        D3D12RHISwapChain& operator=( const D3D12RHISwapChain& ) = delete;

        /**
         * @brief DXGI 스왑체인을 만듭니다.
         * @details 창 핸들이 없거나 크기가 0이면 **네이티브 스왑체인 없이 크기만 기억한 상태**로
         *          성공합니다 — 오프스크린 전용(테스트/헤드리스) 디바이스가 그 경로입니다.
         */
        bool initialize( IDXGIFactory4* pFactory, ID3D12CommandQueue* pQueue, const RHISwapChainDesc& desc );

        /** @brief 백버퍼와 네이티브 스왑체인을 모두 놓습니다. */
        void shutdown();

        /**
         * @brief 백버퍼를 얻어 RTV 를 만듭니다. RTV 힙의 앞쪽 `bufferCount()` 칸을 씁니다.
         * @note 하나라도 실패하면 **전부 비웁니다.** 일부만 null 인 채로 두면 크기는 정상이라
         *       범위 검사를 통과해버리고, null 리소스가 그대로 배리어에 들어가 GPU PageFault 가 됩니다.
         */
        void createBackBuffers( ID3D12Device* pDevice, ID3D12DescriptorHeap* pRtvHeap, uint32 rtvDescriptorSize );

        /** @brief 백버퍼만 놓습니다 (스왑체인은 유지). ResizeBuffers 전에 반드시 필요합니다. */
        void releaseBackBuffers();

        /**
         * @brief 백버퍼 크기를 바꿉니다.
         * @warning 호출 전에 GPU 가 이전 프레임을 다 썼음이 보장돼야 하고, 백버퍼도 놓여 있어야 합니다.
         * @return ResizeBuffers 성공 여부. 실패하면 백버퍼가 비워진 채로 남습니다.
         */
        bool resize( uint32 width, uint32 height );

        /** @brief 다음에 그릴 백버퍼를 고릅니다 (DXGI 가 정해 준 현재 인덱스를 읽어옵니다). */
        void acquireNextImage();

        /** @brief 화면에 표시합니다. 네이티브 스왑체인이 없으면 S_OK 로 아무것도 하지 않습니다. */
        HRESULT present( bool vsync );

        /**
         * @brief 현재 백버퍼를 `stateAfter` 로 전이합니다. 이미 그 상태면 아무것도 하지 않습니다.
         * @details 백버퍼의 리소스 상태는 커맨드 리스트가 아니라 **리소스 자체에 속한 전역 상태**라
         *          이 객체가 들고 있습니다.
         */
        void transitionTo( ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES stateAfter );

        /** @brief 표시 직후의 상태(PRESENT)로 되돌려 기록합니다 — Present 후 동기화용. */
        void markPresented();

        /** @brief 네이티브 스왑체인이 있는지 (오프스크린 전용 디바이스면 false). */
        bool isValid() const { return _swapChain != nullptr; }

        /** @brief 지금 그릴 수 있는 백버퍼가 준비돼 있는지. */
        bool isBackBufferReady() const { return _backBufferIndex < _listBackBuffer.size(); }

        uint32 getBackBufferIndex() const { return _backBufferIndex; }
        uint32 getBufferCount() const { return _bufferCount; }
        uint32 getWidth() const { return _width; }
        uint32 getHeight() const { return _height; }

        /** @brief 현재 백버퍼 리소스. 준비 안 됐으면 nullptr. */
        ID3D12Resource* getCurrentBackBuffer() const;

        /** @brief 현재 백버퍼의 RTV. 준비 안 됐으면 `{ 0 }`. */
        D3D12_CPU_DESCRIPTOR_HANDLE getCurrentRtv() const;

        D3D12_RESOURCE_STATES getState() const;
        IDXGISwapChain3*      getNative() const { return _swapChain.Get(); }

    private:
        Microsoft::WRL::ComPtr<IDXGISwapChain3>        _swapChain;
        vector<Microsoft::WRL::ComPtr<ID3D12Resource>> _listBackBuffer;
        vector<D3D12_CPU_DESCRIPTOR_HANDLE>            _listBackBufferRtv;

        HWND   _pHWnd{ nullptr };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
        uint32 _bufferCount{ 2 };
        uint32 _backBufferIndex{ 0 };

        /// @brief 현재 백버퍼의 실제 리소스 상태. `transitionTo` 만 이 값을 바꿉니다.
        /// @details `_stateMutex` 로 보호한다 — RenderGraph::executeParallel 이 같은 웨이브의 패스
        ///          콜백을 여러 태스크 스레드에서 동시에 돌리는데, 백버퍼를 타깃으로 하는 패스가
        ///          둘 이상이면 그 콜백들이 동시에 이 상태를 읽고 바꾼다. 락이 없으면 둘 다
        ///          "아직 RENDER_TARGET 이 아니다" 를 보고 각자 배리어를 쏴서, 두 번째 것이
        ///          before==after 가 된다(D3D12 검증 오류 → 디바이스 제거).
        mutable mutex         _stateMutex;
        D3D12_RESOURCE_STATES _state{ D3D12_RESOURCE_STATE_PRESENT };
    };
} // namespace sw
#endif
