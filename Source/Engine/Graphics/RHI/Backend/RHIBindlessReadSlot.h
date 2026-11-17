/**
 * @file RHIBindlessReadSlot.h
 * @brief 기록 경로가 bindless 번호로 락 없이 읽는 값 한 칸입니다(DX12 — 번호마다 한 칸, 표 크기는 고정).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    /** @brief bindless 번호 한 칸이 어느 등록표에 살아 있는가입니다. SRV · UAV 표가 번호 공간 하나를 나눈다(DX12). */
    enum class RHIBindlessReadRegistry : uint8
    {
        Empty,
        ShaderResource,
        UnorderedAccess
    };

    /**
     * @class RHIBindlessReadSlot
     * @brief 기록 경로가 bindless 번호로 읽는 값입니다(`D3D12RHICommandContext::resolveBufferAddress` · `resolveOfflineView`, 진단 `isBindlessIndexLive`).
     * @details 쓰기(`publish` · `retire`)는 쓰는 쪽의 배타 락 아래에서 하고, 읽기는 락 없이 `_registry` 를 acquire 로 본 뒤 값을 읽는다. 해제는 `_registry` 만 비운다 —
     *          주소 · 뷰는 번호가 펜스 뒤 다시 나갈 때까지 그대로라 해제와 겹친 읽기가 옛 값을 받아도 그 자원과 디스크립터는 GPU 펜스까지 살아 있다.
     *          진단(`BindlessIndexValidator`)이 드로우마다 묻던 생존 확인도 이 칸만 본다 — 예전엔 `_bindlessMutex` 읽기 락과 등록표 찾기를 거쳤다(L-1).
     *          언리얼 `FD3D12BindlessDescriptorAllocator` 도 고정 힙 번호를 락 없이 쓴다(기억에 의존한 비교).
     */
    class RHIBindlessReadSlot
    {
    public:
        /** @brief 값을 다 쓴 뒤 @p registry 로 공개합니다. @p bTexture 면 텍스처 SRV(주소 없음), 아니면 버퍼입니다. */
        void publish( RHIBindlessReadRegistry registry, uint64 gpuAddress, uint64 offlineView, uint32 cbAlignedSize, bool bTexture )
        {
            _gpuAddress.store( gpuAddress, std::memory_order_relaxed );
            _offlineView.store( offlineView, std::memory_order_relaxed );
            _cbAlignedSize.store( cbAlignedSize, std::memory_order_relaxed );
            _bTexture.store( bTexture ? SW_TRUE : SW_FALSE, std::memory_order_relaxed );
            _registry.store( static_cast<uint8>( registry ), std::memory_order_release );
        }
        /** @brief 칸을 비웁니다(값은 남긴다 — 겹친 읽기가 옛 값을 받아도 펜스까지 산 자원이다). */
        void retire() { _registry.store( static_cast<uint8>( RHIBindlessReadRegistry::Empty ), std::memory_order_release ); }

        /** @brief 칸이 @p registry 에 살아 있는지 봅니다(acquire — 참이면 값을 읽어도 된다). */
        bool isIn( RHIBindlessReadRegistry registry ) const { return _registry.load( std::memory_order_acquire ) == static_cast<uint8>( registry ); }
        /** @brief SRV 표에 살아 있고 종류가 @p space 와 맞는지 봅니다(힙 하나를 나누므로 텍스처 자리에 버퍼 뷰가 있으면 Texture2D 로 읽는 셰이더가 틀린 디스크립터를 읽는다). */
        bool isLive( RHIBindlessIndexSpace space ) const
        {
            if ( isIn( RHIBindlessReadRegistry::ShaderResource ) == false )
                return false;
            const bool bTexture = _bTexture.load( std::memory_order_relaxed ) != SW_FALSE;
            return space == RHIBindlessIndexSpace::Texture ? bTexture : bTexture == false;
        }

        uint64 getGPUAddress() const { return _gpuAddress.load( std::memory_order_relaxed ); }
        uint64 getOfflineView() const { return _offlineView.load( std::memory_order_relaxed ); }
        uint32 getCBAlignedSize() const { return _cbAlignedSize.load( std::memory_order_relaxed ); }

    private:
        atomic<uint64> _gpuAddress{ 0 };    ///< 버퍼의 GPU 주소(텍스처는 0)
        atomic<uint64> _offlineView{ 0 };   ///< 오프라인 힙 뷰(D3D12 `D3D12_CPU_DESCRIPTOR_HANDLE::ptr`)
        atomic<uint32> _cbAlignedSize{ 0 }; ///< 링 상수 버퍼면 프레임 슬롯 크기, 아니면 0
        atomic<uint8>  _bTexture{ SW_FALSE };
        atomic<uint8>  _registry{ static_cast<uint8>( RHIBindlessReadRegistry::Empty ) };
    };
} // namespace sw
