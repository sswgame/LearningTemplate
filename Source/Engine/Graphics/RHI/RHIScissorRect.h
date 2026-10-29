/**
 * @file RHIScissorRect.h
 * @brief 가위(scissor) 사각형입니다. 캔버스 그리기 목록처럼 이것만 쓰는 헤더가 RHI 서술체 전체를 끌지 않게 따로 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct RHIScissorRect
     * @brief 가위(scissor) 사각형입니다. 이 밖의 픽셀은 래스터라이저가 버립니다. 단위는 렌더 타깃 픽셀이고 원점은 왼쪽 위입니다(`RHIViewport` 와 같은 규약).
     * @details `IRHICommandList::setScissorRect` 로 설정한다. UI 스크롤 영역이나 패널처럼 축 정렬 사각형으로 자를 때 쓰며, 일괄(batch)을 끊지 않고 클리핑할 수 있다.
     *          Slate, Dear ImGui, Godot 도 사각 클리핑을 가위로 처리한다. 둥글거나 회전한 클리핑은 셰이더가 처리한다.
     */
    struct RHIScissorRect
    {
        uint32 _x{ 0 };      ///< 왼쪽 위 X (px)
        uint32 _y{ 0 };      ///< 왼쪽 위 Y (px)
        uint32 _width{ 0 };  ///< 너비 (px). 0 이면 아무것도 그리지 않는다
        uint32 _height{ 0 }; ///< 높이 (px). 0 이면 아무것도 그리지 않는다
    };
} // namespace sw
