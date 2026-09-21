/**
 * @file IRenderSurface.h
 * @brief RHI 가 스왑체인을 걸 표면 — 창 시스템을 모르는 채로 필요한 것만 묻는 계약
 * @details 언리얼의 RHI 는 `void*` 창 핸들로 `FRHIViewport` 를 만들고, 창 시스템(Slate)은 RHI 위에 있다.
 *          이 저장소도 같은 방향이어야 한다: `Graphics` 는 `Window` 를 include 하지 않고, `IWindow` 가
 *          이 인터페이스를 **구현**한다. 그래서 인터페이스는 둘 다 볼 수 있는 `Common` 에 있다.
 *
 *          다섯 가지가 전부다 — 스왑체인 생성에 드는 핸들·디스플레이·크기, 그리고 백엔드 교체가
 *          네이티브 표면 재생성을 요구할 때(OpenGL ↔ DXGI 는 픽셀 포맷이 창에 붙는다) 부를 훅.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class IRenderSurface
     * @brief 스왑체인이 붙을 네이티브 표면. `IWindow` 가 구현하고 `RHI`·`IRHIDevice` 가 소비합니다.
     */
    class SW_API IRenderSurface
    {
    public:
        /** @brief 빈 표면 인터페이스. */
        IRenderSurface() = default;
        /** @brief 가상 소멸. */
        virtual ~IRenderSurface() = default;
        /** @brief 복사를 금지합니다 — 표면은 창의 정체성이다. */
        IRenderSurface( const IRenderSurface& ) = delete;
        /** @brief 대입을 금지합니다. */
        IRenderSurface& operator=( const IRenderSurface& ) = delete;

        /** @brief 네이티브 표면 핸들 (HWND · X11 Window · NSView). */
        virtual void* getSurfaceHandle() const = 0;
        /** @brief 네이티브 디스플레이 연결 (X11 Display*). 없으면 nullptr. */
        virtual void* getSurfaceDisplay() const = 0;
        /** @brief 표면 너비 (픽셀). */
        virtual uint32 getSurfaceWidth() const = 0;
        /** @brief 표면 높이 (픽셀). */
        virtual uint32 getSurfaceHeight() const = 0;
        /**
         * @brief 네이티브 표면을 같은 자리에 다시 만듭니다 — 백엔드 교체가 그것을 요구할 때만 불립니다.
         * @return 다시 만들었으면 true. 지원하지 않는 플랫폼은 false 를 돌려주고, 호출자는 그대로 진행한다.
         */
        virtual bool recreateSurface() = 0;
    };
} // namespace sw
