/**
 * @file Engine/Presentation/ViewCameraProvider.h
 * @brief 이번 프레임의 뷰 카메라를 묻는 델리게이트(`ViewCameraProviderDelegate`)입니다 — 호스트의 뷰 요청(`FrameViewports`)과 렌더 창구(`IEngineRenderDriver`)가 함께 쓴다.
 * @details 렌더 창구가 층 2(Presentation)라 층 4(Graphics/View)의 뷰 요청 헤더를 읽지 못해 형식 이름만 여기 둔다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

namespace sw
{
    class CameraComponent;

    /**
     * @brief 이번 프레임의 뷰 카메라를 반환합니다.
     * @details tick 안의 핫 리로드와 씬 전환이 GameObject 를 파괴할 수 있으므로,
     *          카메라는 미리 잡아 두지 않고 파괴 단계가 끝난 뒤 이 델리게이트로 조회합니다.
     */
    SW_DECLARE_DELEGATE( CameraComponent*, ViewCameraProviderDelegate, void );
} // namespace sw
