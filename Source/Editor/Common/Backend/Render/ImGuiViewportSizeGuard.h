/**
 * @file ImGuiViewportSizeGuard.h
 * @brief 멀티 뷰포트 창의 ImGui 크기를 HWND 클라이언트 크기에 맞추는 보정 (DXGI 백엔드 공용)
 *
 * @details DXGI 스왑체인을 DXGI_SCALING_NONE 으로 만들면 스왑체인 크기와 HWND 클라이언트
 *          크기가 정확히 같아야 한다. ImGui 가 플랫폼 창을 만들거나 크기를 바꿀 때 넘겨주는
 *          크기는 그 둘이 어긋날 수 있어서, 창 생성/리사이즈 콜백을 가로채 HWND 를 직접 재고
 *          그 값으로 맞춰 준다.
 *
 *          이 보정은 **DXGI 의 사정**이지 특정 렌더러의 사정이 아니다. 예전에는 DX11 과 DX12
 *          백엔드가 이 메커니즘(원본 콜백 저장 · 가로채기 함수 두 개 · 설치/해제)을 통째로
 *          한 벌씩 들고 있었다 — 고칠 일이 생기면 두 곳을 같이 고쳐야 하고, DXGI 를 쓰는
 *          백엔드를 하나 더 만들면 또 한 벌이 늘어난다. 한 곳에 둔다.
 */
#pragma once

#if defined( SW_PLATFORM_WINDOWS )

namespace sw::editor
{
    /** @brief ImGui 멀티 뷰포트 창 크기를 HWND 에 맞추는 보정 (DXGI 백엔드 공용) */
    class ImGuiViewportSizeGuard
    {
    public:
        /**
         * @brief ImGui 의 창 생성/리사이즈 콜백을 가로채 크기 보정을 겁니다.
         * @details 이미 걸려 있으면 아무 것도 하지 않습니다(중복 설치 안전).
         *          ImGui 렌더러 백엔드 초기화가 성공한 **뒤에** 부르십시오 —
         *          그때 Renderer_CreateWindow 가 채워집니다.
         */
        static void install();

        /** @brief 저장해 둔 원본 콜백 포인터를 놓습니다. 백엔드 종료 시 부르십시오. */
        static void clear();
    };
} // namespace sw::editor

#endif // SW_PLATFORM_WINDOWS
