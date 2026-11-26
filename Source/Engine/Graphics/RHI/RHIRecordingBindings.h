/**
 * @file RHIRecordingBindings.h
 * @brief 기록 스트림 하나에 건 파이프라인 · 셰이더 슬롯 · 정점 스트림 · 루트 상수입니다. 공용 커맨드 컨텍스트(`IRHICommandContext`)가 들고, 백엔드는 드로우 · 디스패치 직전에 바뀐 것만 네이티브로 냅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIGeometryBindings.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#include <limits>

namespace sw
{
    /** @brief 루트 상수(푸시 상수)를 쓴 결과입니다. */
    enum class RHIRootConstantWrite : uint8
    {
        Written,  ///< 썼다
        Empty,    ///< 쓸 값이 없다(개수 0 · 포인터 없음)
        Overflow, ///< 블록(`shaderslot::kRootConstantDwords`)을 넘는다 — 한 칸도 쓰지 않았다
    };

    /**
     * @struct RHIRootConstants
     * @brief 바인드 포인트 하나의 루트 상수 블록입니다. 바뀐 구간만 다음 드로우 · 디스패치가 냅니다.
     * @details 새 스트림의 값은 0 이고 모두 바뀐 것으로 시작한다 — 상수 버퍼로 흉내 내는 백엔드(DX11 · GL · WebGPU)가 앞 스트림의 값을 읽지 않는다.
     */
    struct SW_API RHIRootConstants
    {
        uint32 _arrDword[shaderslot::kRootConstantDwords]{};
        uint32 _dirtyBegin{ 0 };                             ///< 아직 내지 않은 첫 칸
        uint32 _dirtyEnd{ shaderslot::kRootConstantDwords }; ///< 아직 내지 않은 구간의 끝(시작과 같으면 낼 것이 없다)

        /** @brief @p count 칸을 @p firstDword 부터 씁니다. 블록을 넘는 쓰기는 자르지 않고 통째로 거절합니다. */
        [[nodiscard]] RHIRootConstantWrite write( uint32 count, const void* pData, uint32 firstDword );
        [[nodiscard]] bool                 isDirty() const { return _dirtyBegin < _dirtyEnd; }
        void                               markFlushed() { _dirtyBegin = _dirtyEnd = 0; }
        /** @brief 네이티브 쪽 값을 다른 것이 덮었습니다 — 다음 드로우 · 디스패치가 블록 전체를 다시 냅니다. */
        void markAllDirty()
        {
            _dirtyBegin = 0;
            _dirtyEnd   = shaderslot::kRootConstantDwords;
        }
    };

    /** @brief 셰이더 슬롯의 종류(레지스터 글자)입니다. */
    enum class RHIShaderSlotKind : uint8
    {
        ConstantBuffer,  ///< b#
        ShaderResource,  ///< t# — `RHIShaderSlots::kBufferResourceCount` 앞은 버퍼, 뒤는 텍스처(에뮬 백엔드의 머티리얼 · 엔진 칸)
        UnorderedAccess, ///< u#
        Count,
    };

    /**
     * @struct RHIShaderSlots
     * @brief 바인드 포인트(그래픽스 · 컴퓨트) 하나의 셰이더 슬롯 표입니다. 걸린 비트와 아직 내지 않은 비트를 종류마다 셉니다.
     * @details 바뀐 비트 가운데 걸린 칸은 걸고 걸리지 않은 칸은 뗀다 — 파이프라인이 바뀌어 비운 칸이 네이티브에 남지 않는다. 언리얼 RHI 의 state cache 와 같은 자리다.
     */
    struct SW_API RHIShaderSlots
    {
        static constexpr uint32 kConstantBufferCount  = shaderslot::kConstantBufferSlotCount;
        static constexpr uint32 kBufferResourceCount  = shaderslot::kSRVSlotCount;
        static constexpr uint32 kShaderResourceCount  = shaderslot::kEngineTexture0 + shaderslot::kEngineTextureCount;
        static constexpr uint32 kUnorderedAccessCount = shaderslot::kComputeTextureUAV0 + shaderslot::kComputeTextureUAVSlotCount;
        static constexpr uint32 kKindCount            = static_cast<uint32>( RHIShaderSlotKind::Count );
        static_assert( kShaderResourceCount <= std::numeric_limits<uint64>::digits, "t 슬롯 비트가 uint64 에 들어가야 한다" );
        static_assert( kBufferResourceCount <= shaderslot::kMaterialTexture0, "버퍼 슬롯 띠는 텍스처 슬롯 앞에서 끝난다" );

        RHIDescriptorIndex _arrIndex[kKindCount][kShaderResourceCount];
        uint64             _arrBoundMask[kKindCount]{}; ///< 걸린 칸
        uint64             _arrDirtyMask[kKindCount]{}; ///< 마지막으로 낸 뒤 바뀐 칸(걸림 · 뗌 모두)

        RHIShaderSlots();

        /** @brief 칸에 번호를 겁니다(무효 번호는 뗀다). 같은 번호면 아무것도 바뀌지 않습니다. @p slot 은 종류의 슬롯 수보다 작아야 합니다. */
        void bind( RHIShaderSlotKind kind, uint32 slot, RHIDescriptorIndex index );
        /** @brief 걸린 칸을 모두 뗍니다(파이프라인이 바뀌었다). */
        void clear();
        /** @brief 네이티브 쪽 바인딩을 다른 것이 지웠습니다 — 걸린 칸을 모두 다시 냅니다. */
        void markAllDirty();
        /** @brief 걸린 칸 하나를 다시 냅니다(네이티브에서 그 칸만 뗐다). */
        void markDirty( RHIShaderSlotKind kind, uint32 slot );

        [[nodiscard]] bool isBound( RHIShaderSlotKind kind, uint32 slot ) const { return ( _arrBoundMask[static_cast<uint32>( kind )] & ( uint64{ 1 } << slot ) ) != 0; }
        [[nodiscard]] bool isDirty( RHIShaderSlotKind kind ) const { return _arrDirtyMask[static_cast<uint32>( kind )] != 0; }
        /** @brief 칸에 건 번호입니다. 걸리지 않았으면 무효 번호입니다. */
        [[nodiscard]] RHIDescriptorIndex get( RHIShaderSlotKind kind, uint32 slot ) const { return _arrIndex[static_cast<uint32>( kind )][slot]; }

        /** @brief 바뀐 칸마다 @p visit( 칸 번호, 번호 )를 부르고 바뀐 비트를 지웁니다. 뗀 칸은 무효 번호로 옵니다. 템플릿이라 간접 호출이 없습니다. */
        template <typename VisitFn>
        void flush( RHIShaderSlotKind kind, VisitFn&& visit )
        {
            uint64 dirty                               = _arrDirtyMask[static_cast<uint32>( kind )];
            _arrDirtyMask[static_cast<uint32>( kind )] = 0;
            for ( uint32 slot = 0; dirty != 0; ++slot, dirty >>= 1 )
            {
                if ( ( dirty & 1u ) != 0 )
                    visit( slot, get( kind, slot ) );
            }
        }
    };

    /** @brief 파이프라인을 거는 자리입니다. 그래픽스와 컴퓨트의 슬롯 · 루트 상수는 서로 독립입니다(D3D12 루트 인자와 같다). */
    enum class RHIBindPoint : uint8
    {
        Graphics,
        Compute,
        Count,
    };

    /**
     * @struct RHIRecordingBindings
     * @brief 기록 스트림 하나의 바인딩 전부입니다(`IRHICommandContext` 의 멤버 — 규약을 지키는 코드가 한 벌이다).
     */
    struct SW_API RHIRecordingBindings
    {
        static constexpr uint32 kBindPointCount = static_cast<uint32>( RHIBindPoint::Count );

        RHIGeometryBindings    _geometry;                          ///< 정점 슬롯과 인덱스 버퍼
        RHIShaderSlots         _arrSlots[kBindPointCount];         ///< 바인드 포인트마다의 셰이더 슬롯
        RHIRootConstants       _arrRootConstants[kBindPointCount]; ///< 바인드 포인트마다의 루트 상수
        RHIPipelineStateHandle _graphicsPSO{ 0 };                  ///< 건 그래픽스 파이프라인(0 이면 없음)
        RHIPipelineStateHandle _computePSO{ 0 };                   ///< 건 컴퓨트 파이프라인(0 이면 없음)

        RHIShaderSlots&   getSlots( RHIBindPoint point ) { return _arrSlots[static_cast<uint32>( point )]; }
        RHIRootConstants& getRootConstants( RHIBindPoint point ) { return _arrRootConstants[static_cast<uint32>( point )]; }

        /** @brief 그래픽스 파이프라인을 겁니다. 다른 파이프라인이면 셰이더 슬롯과 정점 슬롯을 비우고, 같은 파이프라인이면 그대로 둡니다. */
        void setGraphicsPipeline( RHIPipelineStateHandle pso );
        /** @brief 컴퓨트 파이프라인을 겁니다. 다른 파이프라인이면 컴퓨트 슬롯을 비웁니다. */
        void setComputePipeline( RHIPipelineStateHandle pso );
        /** @brief 렌더 패스를 여는 것은 인코더를 새로 여는 것과 같다 — 그래픽스 파이프라인 · 슬롯 · 정점 · 인덱스를 비웁니다(WebGPU · Metal 인코더와 같다). 루트 상수는 남긴다. */
        void resetGraphics();
    };
} // namespace sw
