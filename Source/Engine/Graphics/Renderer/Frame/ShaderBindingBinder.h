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
#include "Engine/Graphics/Renderer/Frame/PassConstantValues.h"

namespace sw
{
    class FrameResourceRegistry;
    class IRHICommandList;
    class IRHIDevice;
    class ShaderBindingLayout;

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
