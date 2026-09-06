/**
 * @file D3D11RHICommandList.h
 * @brief 진짜 네이티브 `ID3D11DeviceContext`(Deferred Context)를 소유하는 IRHICommandList 구현체
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/RHICommandListForwarder.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    class D3D11RHIDevice;

    /**
     * @class D3D11RHICommandList
     * @brief 자신만의 네이티브 `ID3D11DeviceContext`(Deferred Context)를 소유하는 `IRHICommandList`.
     * @details 예전엔 `IRHICommandList`(RHIDeferredCommandList) 가 모든 호출을 소프트웨어 `Cmd` 벡터에
     *          쌓았다가 프레임 끝에 디바이스 단일 Immediate Context에 재생(replay)했다. 이 클래스는
     *          `Cmd` 벡터 없이 `IRHICommandList` 호출을 그 자리에서 바로 자신의 네이티브 Deferred
     *          Context에 기록한다(`D3D11RHICommandContext` 로직을 재사용, `_pNativeContext` 만 자신의
     *          것을 가리킴). D3D11 런타임은 드라이버가 커맨드 리스트를 네이티브로 지원하지 않아도
     *          소프트웨어로 에뮬레이션하므로 `CreateDeferredContext`/`FinishCommandList` 는 항상 동작한다.
     *          제출은 디바이스의 단일 Immediate Context에서 `ExecuteCommandList` 로 한다 — DX11은 한
     *          스레드에서 발행한 Immediate Context 호출 순서가 곧 실행 순서이므로(Vulkan의 세마포어
     *          재정렬 문제 없음) 프레임 스트림의 스왑체인 begin/end 호출과 섞여도 순서가 보장된다.
     */
    class D3D11RHICommandList : public RHICommandListForwarder<D3D11RHICommandContext>
    {
    public:
        /** @brief 자신의 네이티브 Deferred Context를 만듭니다. */
        explicit D3D11RHICommandList( D3D11RHIDevice* pDevice );
        ~D3D11RHICommandList() override;

        D3D11RHICommandList( const D3D11RHICommandList& )            = delete;
        D3D11RHICommandList& operator=( const D3D11RHICommandList& ) = delete;

        /** @brief 이 리스트가 유효한(생성에 성공한) 네이티브 Deferred Context를 갖고 있으면 true. */
        bool isValid() const { return _pNativeContext != nullptr; }
        /** @brief `IRHIDevice::executeCommandList` 가 실제 제출에 쓰는 네이티브 커맨드 리스트. */
        ID3D11CommandList* getNativeCommandList() const { return _pFinishedList.Get(); }

        void beginCommandList() override;
        void endCommandList() override;

        /**
         * @brief 기록해 둔 커맨드 리스트를 버리고 Deferred Context 바인딩을 비웁니다.
         * @details `ID3D11CommandList` 는 **자기가 바인딩한 리소스의 참조를 붙들고 있다.** 백버퍼
         *          RTV 도 마찬가지라, 지난 프레임에 기록된 리스트가 살아 있으면 `ResizeBuffers` 가
         *          DXGI_ERROR_INVALID_CALL 로 거부된다 — 창 크기 변경이 조용히 실패하는 원인이었다.
         */
        void releaseRecordedState();

        /**
         * @brief 디바이스가 먼저 내려갈 때, 디바이스를 다시 만지지 않도록 연결을 끊습니다.
         * @details 커맨드 리스트가 디바이스보다 오래 살 수 있습니다 — 그때 소멸자가 이미 파괴된
         *          디바이스의 등록 목록을 건드리면 죽은 뮤텍스를 잠그게 됩니다. 끊긴 리스트는
         *          더 이상 쓸 수 없고, 놓아 주는 것만 안전합니다.
         */
        void detachFromDevice();

    private:
        /** @brief 디바이스에 Deferred Context 생성을 요청합니다. */
        static Microsoft::WRL::ComPtr<ID3D11DeviceContext> createNativeContext( D3D11RHIDevice* pDevice );

        D3D11RHIDevice*                             _pDevice{ nullptr };
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _pNativeContext;
        Microsoft::WRL::ComPtr<ID3D11CommandList>   _pFinishedList;
        D3D11RHICommandContext                      _context;
    };
} // namespace sw

#endif
