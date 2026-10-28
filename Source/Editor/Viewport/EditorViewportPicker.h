/**
 * @file EditorViewportPicker.h
 * @brief 씬 뷰의 클릭 선택과 호버 강조입니다. 마우스 아래 오브젝트를 찾는 일은 둘이 같은 함수로 합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class CameraComponent;
    class Component;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    struct ViewportToolbarSettings;

    /**
     * @class EditorViewportPicker
     * @brief 씬 뷰 캔버스의 피킹입니다. 빌보드를 먼저 보고 레이 피킹(`EditorViewportRayPick`)을 합니다.
     * @details `EditorViewportClient` 가 값 멤버로 들고 직접 부릅니다. 어떤 컴포넌트 종류를 집는지는 ImGui 없는 `EditorViewportRayPick` 의 테이블이 정하고,
     *          이 클래스는 마우스 · 잠금 · 숨김 · 호버 빈도만 다룹니다.
     */
    class EditorViewportPicker
    {
    public:
        EditorViewportPicker();

        /** @brief 캔버스를 왼쪽 단추로 눌렀으면 마우스 아래 오브젝트를 선택하고, 빈 곳이면 선택을 비웁니다. 기즈모 위나 Alt 중이면 하지 않습니다. */
        void processClick( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasClicked, const ViewportToolbarSettings& settings );
        /**
         * @brief 호버 오브젝트를 정합니다. 마우스가 움직였을 때만 피킹하며 프레임당 많아야 한 번입니다.
         * @param bGizmoDragging 기즈모를 끄는 중이면 true 입니다. 그동안은 호버를 막습니다.
         */
        void processHover( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasHovered, bool bGizmoDragging,
                           const ViewportToolbarSettings& settings );

        /** @brief 마우스 아래의 오브젝트 id 입니다. 호버 강조에 쓰며, 없거나 막혀 있으면 0 입니다. */
        uint64 getHoveredObjectID() const { return _hoveredObjectID; }

        /** @brief 마우스 아래 오브젝트를 찾습니다. 빌보드를 먼저 보고 레이 피킹을 하며, 잠그거나 숨긴 오브젝트는 빈 곳으로 처리합니다. 클릭 선택 · 호버 · 우클릭 메뉴가 함께 씁니다. */
        [[nodiscard]] bool findObjectUnderMouse( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, const ViewportToolbarSettings& settings,
                                                 GameObject*& pOutObject, Component*& pOutComponent ) const;

    private:
        uint64  _hoveredObjectID;      ///< 마우스 아래 오브젝트. 0 이면 없다
        float64 _lastHoverPickSeconds; ///< 마지막 호버 피킹에 걸린 시간(초)
        float64 _lastHoverPickTime;    ///< 그 피킹을 한 ImGui 시각(초)
        float2  _hoverMousePos;        ///< 마지막으로 호버 피킹한 마우스 위치. 그대로이면 다시 찾지 않는다
    };
} // namespace sw::editor
