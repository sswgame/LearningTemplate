/**
 * @file RHISurfaceTransform.h
 * @brief 스왑체인 서피스의 회전(pre-rotation)과 논리 크기 ↔ 실제 이미지 크기 · 좌표 대응입니다.
 * @details 회전한 휴대 기기 화면(Android)에서 Vulkan 스왑체인을 서피스의 지금 회전(`currentTransform`)으로 만들면 합성기가 다시 돌리지 않는다 —
 *          대신 엔진이 그림을 미리 돌려 써야 한다. 엔진의 모든 쓰기(Present · Canvas · 캡처 → 화면 · 개발 겹치기 · UI)는 **논리 크기**(사용자가 보는 방향) 대기
 *          텍스처에 하고, 끝에 출력 패스 하나(`RenderPassType::ScreenRotation`)가 이 대응으로 실제 스왑체인 이미지에 돌려 그린다(`FrameScreenRotation`).
 *          언리얼 모바일도 백버퍼 앞 마지막 패스에서 돌리고, Godot 은 Vulkan 스왑체인을 currentTransform 으로 만들어 블릿에서 돌린다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @enum RHISurfaceTransform
     * @brief 스왑체인 이미지가 화면에 나가기 전에 엔진이 미리 돌려 둔 각도입니다(시계 방향 — Vulkan `VK_SURFACE_TRANSFORM_ROTATE_*_BIT_KHR` 와 같은 뜻).
     * @details 거울(mirror) 회전은 받지 않는다 — 스왑체인이 그것을 currentTransform 으로 쓰지 않고 합성기에 맡긴다(`VulkanRHISwapChain::create`).
     */
    enum class RHISurfaceTransform : uint8
    {
        Identity,  ///< 돌리지 않는다(데스크톱 · 기기의 기본 방향)
        Rotate90,  ///< 시계 방향 90 도
        Rotate180, ///< 180 도
        Rotate270, ///< 시계 방향 270 도
    };

    /**
     * @struct RHISurfaceUVMapping
     * @brief 실제 이미지의 UV(0..1, 위가 0)를 논리 그림의 UV 로 옮기는 2x2 행렬과 이동입니다 — `logical = M × physical + offset`.
     * @details 회전 출력 패스(screen_rotation.hlsl)가 픽셀마다 이 식으로 논리 대기 텍스처를 읽는다. 행렬 원소는 -1 · 0 · 1 뿐이다.
     */
    struct RHISurfaceUVMapping
    {
        float32 _m00{ 1.0f };
        float32 _m01{ 0.0f };
        float32 _m10{ 0.0f };
        float32 _m11{ 1.0f };
        float32 _offsetU{ 0.0f };
        float32 _offsetV{ 0.0f };
    };

    /** @brief 픽셀 사각형(왼쪽 위 원점)입니다 — 회전 출력 패스의 뷰포트 · 시저를 논리 좌표에서 실제 이미지 좌표로 옮길 때 쓴다. */
    struct RHISurfaceRect
    {
        uint32 _x{ 0 };
        uint32 _y{ 0 };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
    };

    /**
     * @struct RHISurfaceRotation
     * @brief 서피스 회전과 논리 크기입니다. 실제 이미지 크기(회전 출력 패스의 뷰포트 · 시저)와 UV 대응을 여기서 구한다.
     * @details 논리 크기 = 렌더러가 보는 백버퍼 크기(`IRHIDevice::getBackBufferWidth/Height`), 실제 크기 = 스왑체인 이미지 크기(90 · 270 이면 너비 · 높이를 맞바꾼 것).
     */
    struct RHISurfaceRotation
    {
        RHISurfaceTransform _transform{ RHISurfaceTransform::Identity };
        uint32              _logicalWidth{ 0 };
        uint32              _logicalHeight{ 0 };

        /** @brief 돌리지 않으면 true 입니다 — 회전 출력 패스도 대기 텍스처도 없다. */
        constexpr bool isIdentity() const { return _transform == RHISurfaceTransform::Identity; }
        /** @brief 90 · 270 이면 true 입니다 — 실제 이미지의 너비 · 높이가 논리와 맞바뀐다. */
        constexpr bool isAxisSwapped() const { return _transform == RHISurfaceTransform::Rotate90 || _transform == RHISurfaceTransform::Rotate270; }
        /** @brief 실제 스왑체인 이미지의 너비입니다(회전 출력 패스의 뷰포트 · 시저 너비). */
        constexpr uint32 getPhysicalWidth() const { return isAxisSwapped() ? _logicalHeight : _logicalWidth; }
        /** @brief 실제 스왑체인 이미지의 높이입니다(회전 출력 패스의 뷰포트 · 시저 높이). */
        constexpr uint32 getPhysicalHeight() const { return isAxisSwapped() ? _logicalWidth : _logicalHeight; }

        /**
         * @brief 논리 좌표의 사각형 @p rect 를 실제 이미지 좌표로 옮깁니다(뷰포트 · 시저). 논리 전체 사각형은 실제 이미지 전체가 된다.
         * @details 식은 https://developer.android.com/games/optimize/vulkan-prerotation 의 "Non-Full Screen Viewport and Scissor" 와 같다(bufferWidth · bufferHeight = 실제 크기).
         */
        constexpr RHISurfaceRect mapLogicalRect( const RHISurfaceRect& rect ) const
        {
            const uint32 physicalWidth  = getPhysicalWidth();
            const uint32 physicalHeight = getPhysicalHeight();
            switch ( _transform )
            {
                case RHISurfaceTransform::Rotate90:
                    return RHISurfaceRect{ physicalWidth - rect._height - rect._y, rect._x, rect._height, rect._width };
                case RHISurfaceTransform::Rotate180:
                    return RHISurfaceRect{ physicalWidth - rect._width - rect._x, physicalHeight - rect._height - rect._y, rect._width, rect._height };
                case RHISurfaceTransform::Rotate270:
                    return RHISurfaceRect{ rect._y, physicalHeight - rect._width - rect._x, rect._height, rect._width };
                case RHISurfaceTransform::Identity:
                    break;
            }
            return rect;
        }

        /**
         * @brief 실제 이미지 UV → 논리 UV 대응입니다.
         * @details 시계 방향 90 도로 미리 돌린 이미지는 논리 (u, v) 가 실제 (1 - v, u) 에 놓인다 — 그 역은 u = V, v = 1 - U 다. 180 · 270 도 같은 식으로 구한다.
         */
        constexpr RHISurfaceUVMapping computeLogicalUVMapping() const
        {
            switch ( _transform )
            {
                case RHISurfaceTransform::Rotate90:
                    return RHISurfaceUVMapping{ 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f };
                case RHISurfaceTransform::Rotate180:
                    return RHISurfaceUVMapping{ -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f };
                case RHISurfaceTransform::Rotate270:
                    return RHISurfaceUVMapping{ 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 0.0f };
                case RHISurfaceTransform::Identity:
                    break;
            }
            return RHISurfaceUVMapping{};
        }
    };
} // namespace sw
