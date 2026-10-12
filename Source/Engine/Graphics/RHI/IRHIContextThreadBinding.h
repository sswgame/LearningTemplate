/**
 * @file IRHIContextThreadBinding.h
 * @brief 디바이스의 선택 능력인 그래픽스 컨텍스트 스레드 바인딩입니다. 컨텍스트를 소유 스레드 하나에서만 쓰는 디바이스(DX11, OpenGL)만 구현합니다.
 */
#pragma once

namespace sw
{
    /**
     * @class IRHIContextThreadBinding
     * @brief 드로우와 Present 컨텍스트를 호출한 스레드에 붙이고 뗍니다.
     * @details DX11 즉시 컨텍스트와 OpenGL(wgl/egl MakeCurrent)이 이 능력을 가진다. DX12 와 Vulkan 은 컨텍스트가 스레드에 묶이지 않아 구현하지 않는다.
     *          디바이스가 생성자에서 `IRHIDevice::_pContextThreadBinding` 에 자기를 적고, 쓰는 쪽은 `IRHIDevice::findContextThreadBinding()` 이
     *          nullptr 이면 붙이고 뗄 것이 없다고 본다(`IRHIDevice::requiresExclusiveContextThread` 가 같은 질문이다). 묻는 비용은 멤버 하나를 읽는 것뿐이다.
     */
    class IRHIContextThreadBinding
    {
    public:
        /**
         * @brief 호출한 스레드에 그래픽스 컨텍스트를 붙입니다(RenderThread 진입 시).
         * @details OpenGL 은 wglMakeCurrent, glXMakeCurrent 를 부른다. DX11 은 MakeCurrent 가 없어 즉시 컨텍스트가 있는지만 답한다.
         *          다른 스레드에서 부르지 않는 것이 곧 배타성이다.
         */
        virtual bool bindGraphicsContext() = 0;

        /** @brief 스레드 바인딩을 해제합니다(OpenGL 은 MakeCurrent(null)). */
        virtual void unbindGraphicsContext() = 0;

    protected:
        /** @brief 이 인터페이스로는 지우지 않습니다. 수명은 디바이스가 정합니다. */
        ~IRHIContextThreadBinding() = default;
    };
} // namespace sw
