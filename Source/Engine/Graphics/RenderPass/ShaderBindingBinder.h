/**
 * @file ShaderBindingBinder.h
 * @brief 리플렉션 레이아웃 + 프레임 상수값 + 리소스 레지스트리로 실제 GPU 바인딩을 수행한다.
 * @details FrameRenderer 가 `g_ViewProj`, `g_World`, `g_KeyLightColor`, `g_TexShadow`(=bindless idx) 등을
 *          이름으로 채우면, 이 클래스가 ShaderBindingLayout 을 읽어 CB 바이트를 조립하고 슬롯별로 바인딩한다.
 *          C++ 미러 struct 를 두지 않는다 — 셰이더만 고치면 자동으로 따라온다.
 */
#pragma once
#include "Core/Container/array.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class FrameResourceRegistry;
    class IRHICommandList;
    class IRHIDevice;
    class ShaderBindingLayout;

    /**
     * @class PassConstantValues
     * @brief 이름 기반 값 저장소 (float4x4/float4/float/uint). 대형 미러 struct 대체.
     */
    class SW_API PassConstantValues
    {
    public:
        static constexpr uint32 kMaxValueBytes = 64; ///< float4x4 하나 크기

        /** @brief 모든 값을 비운다. */
        void clear() { _listEntry.clear(); }

        /** @brief float4x4 값을 이름으로 설정(upsert)한다. */
        void setMatrix( hashed_string name, const float4x4& value );
        /** @brief float4 값을 이름으로 설정한다. */
        void setFloat4( hashed_string name, const float4& value );
        /** @brief float32 값을 이름으로 설정한다. */
        void setFloat( hashed_string name, float32 value );
        /** @brief uint 값을 이름으로 설정한다. */
        void setUint( hashed_string name, uint32 value );
        /** @brief 원시 바이트 값을 이름으로 설정한다. */
        void setBytes( hashed_string name, const void* pData, uint32 byteSize );

        /** @brief 이름으로 값을 찾는다 (없으면 nullptr). */
        const uint8* find( hashed_string name, uint32& outSize ) const;

    private:
        struct Entry
        {
            hashed_string                _name;
            uint32                       _size{ 0 };
            array<uint8, kMaxValueBytes> _data{};
        };

        vector<Entry> _listEntry;
    };

    /// @brief 엔진(PassCB 등) 상수 버퍼 슬롯 — 버퍼 핸들 + bindless 인덱스.
    struct EngineConstantBufferSlot
    {
        RHIBufferHandle    _buffer{ 0 };
        RHIDescriptorIndex _index{ kInvalidDescriptorIndex };
    };

    /**
     * @struct ShaderBindingBinder
     * @brief 드로우 직전 레이아웃을 따라 CB 바이트를 조립·업로드하고 슬롯별로 바인딩한다.
     */
    struct SW_API ShaderBindingBinder
    {
        /**
         * @brief 그래픽스 드로우 바인딩을 수행한다.
         * @param bNativeBindless true 면 텍스처 인덱스는 이미 CB 에 기록되어 별도 바인딩이 필요 없다 (DX12/VK).
         *        구조버퍼는 텍스처와 달리 네이티브 bindless 여도 백엔드별로 명시 바인딩이 필요할 수 있어
         *        이 플래그로 스킵하지 않는다 (각 백엔드 bindStructuredBuffer 가 자체 판단).
         */
        static void bindGraphics( IRHIDevice& device, IRHICommandList& cmd,
                                  const ShaderBindingLayout&      layout,
                                  const FrameResourceRegistry&    registry,
                                  const PassConstantValues&       values,
                                  const EngineConstantBufferSlot& engineCb,
                                  RHIDescriptorIndex              materialCb,
                                  bool                            bNativeBindless );
    };
} // namespace sw
