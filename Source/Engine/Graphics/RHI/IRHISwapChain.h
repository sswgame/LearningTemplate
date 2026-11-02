/**
 * @file IRHISwapChain.h
 * @brief 창 하나의 스왑체인입니다(백버퍼 · 획득 · 표시 · 크기). 메인 창도 그 가운데 하나입니다.
 * @details 언리얼 `FRHIViewport`(`RHICreateViewport` · `RHIResizeViewport`) · Godot 4 `RenderingDeviceDriver::swap_chain_create` 자리다.
 *          두 엔진 모두 창마다 스왑체인을 따로 두고 메인 창을 특별하게 다루지 않는다 — 디바이스에 스왑체인 하나를 박으면 둘째 창(에디터 플로팅 창)이
 *          RHI 밖에서 제 스왑체인을 따로 짓게 되고, 동기화 결함이 그 사본에 숨는다(ImGui Vulkan 뷰포트의 세마포어 재사용).
 *
 *          수명은 디바이스가 쥔다 — `IRHIDevice::createSwapChain` 이 만들고 `destroySwapChain` 이 부순다. 그리기는 렌더 패스가 고른다:
 *          `RHIRenderPassBeginInfo::_pSwapChain` 의 백버퍼(컬러 타깃 핸들 0)에 그리면 그 프레임의 `endFrame` 이 함께 표시한다.
 *          획득 · 표시 · 재생성은 백엔드의 구체 클래스(`<백엔드>RHISwapChain`)가 하고, 이 인터페이스는 바깥이 알아야 할 크기만 보인다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class IRHISwapChain
     * @brief 창 하나의 스왑체인입니다. 백엔드마다 구현이 하나씩입니다(`D3D11RHISwapChain` · `D3D12RHISwapChain` · `VulkanRHISwapChain` · `OpenGLRHISwapChain`).
     */
    class SW_API IRHISwapChain
    {
    public:
        IRHISwapChain()                                  = default;
        virtual ~IRHISwapChain()                         = default;
        IRHISwapChain( const IRHISwapChain& )            = delete;
        IRHISwapChain& operator=( const IRHISwapChain& ) = delete;

        /** @brief 백버퍼 너비(픽셀)입니다. 마지막 생성 · 크기 변경의 값입니다. */
        virtual uint32 getWidth() const = 0;
        /** @brief 백버퍼 높이(픽셀)입니다. */
        virtual uint32 getHeight() const = 0;
    };
} // namespace sw
