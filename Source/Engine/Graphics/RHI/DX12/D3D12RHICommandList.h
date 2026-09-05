/**
 * @file D3D12RHICommandList.h
 * @brief 진짜 네이티브 ID3D12GraphicsCommandList 를 소유하는 IRHICommandList 구현체
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/RHICommandListForward.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    class D3D12RHIDevice;

    /**
     * @class D3D12RHICommandList
     * @brief 자신만의 `ID3D12GraphicsCommandList`/기록 상태를 소유하는 `IRHICommandList`.
     * @details 예전엔 `IRHICommandList`(RHIDeferredCommandList) 가 모든 호출을 소프트웨어 `Cmd` 벡터에
     *          쌓았다가 프레임 끝에 디바이스 공유 커맨드 리스트 하나에 재생(replay)했다 — Immediate/
     *          Deferred Context 가 실제로는 같은 리스트를 가리키는 별칭이었다. 이 클래스는 `Cmd` 벡터
     *          없이 `IRHICommandList` 호출을 그 자리에서 바로 자신의 네이티브 리스트에 기록한다
     *          (`D3D12RHICommandContext` 로직을 재사용, `_cmdList`/`_state` 만 자신의 것을 가리킴).
     *          얼로케이터는 **리스트마다 전용**이다. 예전엔 "프레임당 리스트 1개"라는 전제로 디바이스의
     *          프레임 링 얼로케이터를 빌려 썼는데, `RenderGraph::executeParallel` 이 패스마다 리스트를
     *          만들어 동시에 기록하게 되면서 그 전제가 깨졌다 — 여러 리스트가 한 얼로케이터를 공유하면
     *          D3D12 계약 위반(기록 중 Reset, 동시 기록)이라 커맨드 메모리가 서로 덮어써지고 GPU 가
     *          쓰레기를 실행해 PageFault/DEVICE_HUNG 으로 이어졌다. 이제 디바이스 풀에서 리스트+얼로케이터
     *          쌍을 빌리고, 다 쓰면 GPU 펜스 통과 후 풀로 돌려준다. 리스트 객체가 프레임을 넘어
     *          재사용되는 경우(`FrameRenderer::_frameCmd`)에도 같은 쌍을 다시 Reset 하지 않고 매
     *          `beginCommandList` 마다 쌍을 교체한다 — 같은 계약 위반이기 때문이다.
     */
    class D3D12RHICommandList : public IRHICommandList
    {
    public:
        /** @brief 디바이스 풀에서 리스트+전용 얼로케이터 쌍을 빌립니다(Close 상태). */
        explicit D3D12RHICommandList( D3D12RHIDevice* pDevice );
        /** @brief 빌린 쌍을 GPU 펜스 통과 후 풀로 돌려보냅니다. */
        ~D3D12RHICommandList() override;

        D3D12RHICommandList( const D3D12RHICommandList& )            = delete;
        D3D12RHICommandList& operator=( const D3D12RHICommandList& ) = delete;

        /** @brief 이 리스트가 유효한(생성에 성공한) 네이티브 커맨드 리스트를 갖고 있으면 true. */
        bool isValid() const { return _entry._list != nullptr; }
        /** @brief `IRHIDevice::executeCommandList` 가 실제 제출에 쓰는 네이티브 포인터. */
        ID3D12GraphicsCommandList* getNativeCommandList() const { return _entry._list.Get(); }

        void beginCommandList() override;
        void endCommandList() override;

        SW_FORWARD_RHI_COMMAND_LIST( _context )

    private:
        D3D12RHIDevice*        _pDevice;
        D3D12CommandListEntry  _entry;            ///< 이 리스트 전용 커맨드 리스트 + 얼로케이터
        uint8                  _bEntryDirty{ 0 }; ///< 현재 쌍에 이미 기록한 적이 있는가(있으면 다음 begin 때 교체)
        D3D12RecordingState    _state;
        D3D12RHICommandContext _context;
    };
} // namespace sw

#endif
