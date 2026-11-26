/**
 * @file RHIViewport.h
 * @brief 렌더 패스가 그리는 크기와 뷰포트입니다.
 */
#pragma once
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Graphics/RHI/RHIEnums.h"

namespace sw
{
    /** @brief 렌더 패스가 그리는 크기(px)입니다. */
    struct RHIRenderPassExtent
    {
        uint32 _width{ 0 };
        uint32 _height{ 0 };
    };

    /**
     * @struct RHIViewport
     * @brief 렌더링 뷰포트 영역입니다. DirectX 규약이며 모든 백엔드가 같습니다.
     * @details 원점은 좌상단이고 픽셀 +Y 는 아래쪽이다. 클립 공간과 NDC 의 +Y 는 위쪽이다(Direct3D 규약).
     *          Vulkan 은 내부에서 negative-height 뷰포트로 바꾸고, OpenGL 은 glClipControl 로 맞춘다.
     *          오프스크린 FBO 는 GL_UPPER_LEFT, 기본 프레임버퍼는 GL_LOWER_LEFT 이고, 둘 다 GL_ZERO_TO_ONE 이다.
     */
    REFLECT()
    struct RHIViewport
    {
        REFLECT_BODY();
        PROPERTY()
        float32 _x{ 0.0f }; ///< 뷰포트 좌상단 X

        PROPERTY()
        float32 _y{ 0.0f }; ///< 뷰포트 좌상단 Y

        PROPERTY()
        float32 _width{ constant::window::kDefaultViewportWidth }; ///< 뷰포트 너비

        PROPERTY()
        float32 _height{ constant::window::kDefaultViewportHeight }; ///< 뷰포트 높이 (양수; Vulkan이 부호 반전)

        PROPERTY()
        float32 _minDepth{ 0.0f }; ///< 최소 깊이 (0.0~1.0)

        PROPERTY()
        float32 _maxDepth{ 1.0f }; ///< 최대 깊이 (0.0~1.0)
    };
} // namespace sw
