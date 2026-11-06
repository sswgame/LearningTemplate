/**
 * @file EditorViewportPicker.h
 * @brief 씬 뷰의 클릭 선택과 호버 강조입니다. 마우스 아래 오브젝트를 찾는 일은 둘이 같은 함수로 합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

struct ImDrawList;

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

        /**
         * @brief 캔버스를 왼쪽 단추로 눌렀으면 마우스 아래 오브젝트를 선택하고, 빈 곳이면 선택을 비우고 상자 선택을 시작합니다. 기즈모 위나 Alt 중이면 하지 않습니다.
         * @details Ctrl 은 그 오브젝트를 넣고 빼고(Toggle), Shift 는 더한다(Add) — 언리얼 뷰포트와 같다. 수정 키가 있으면 빈 곳을 눌러도 선택을 비우지 않는다.
         */
        void processClick( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasClicked, bool bGizmoBusy, const ViewportToolbarSettings& settings );
        /**
         * @brief 호버 오브젝트를 정합니다. 마우스가 움직였을 때만 피킹하며 프레임당 많아야 한 번입니다.
         * @param bGizmoBusy 지난 프레임에 그린 기즈모 위에 있거나 끄는 중이면 true 입니다. ImGuizmo::IsOver 를 직접 보지 않는다 - 기즈모를 그리지 않는 프레임에도 마지막 행렬로 답해, 선택을 푼 뒤 그 자리의 클릭이 먹지 않는다.
         */
        void processHover( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasHovered, bool bGizmoBusy,
                           const ViewportToolbarSettings& settings );

        /**
         * @brief 빈 곳에서 시작한 왼쪽 끌기를 상자 선택으로 다룹니다. 끄는 동안 상자를 그리고, 떼면 원점이 상자 안에 든 오브젝트를 고릅니다.
         * @details 유니티 씬 뷰 · Godot 3D 뷰의 끌기 선택과 같다(언리얼은 Ctrl+Alt 끌기). 판정은 오브젝트 원점의 화면 위치다(Godot 와 같은 기준 — 메시 경계로 재면
         *          빛 · 카메라처럼 경계가 없는 오브젝트가 빠진다). 잠근 · 숨긴 오브젝트와 화면을 그리는 에디터 카메라는 고르지 않는다(클릭 선택과 같은 규칙).
         */
        void processMarquee( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, ImDrawList* pDrawList );

        /** @brief 상자 선택을 기다리거나 끄는 중이면 true 입니다(호버가 양보한다). */
        bool isMarqueeActive() const { return _bMarqueeArmed == SW_TRUE; }

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
        float2  _marqueeStart;         ///< 상자 선택을 시작한 화면 위치
        uint8   _marqueeMode;          ///< 상자 선택의 `SelectionMode`(누를 때의 수정 키)
        uint8   _bMarqueeArmed : 1;    ///< 빈 곳을 눌러 상자 선택을 기다리는 중(끌기 문턱을 넘으면 상자가 보인다)
    };
} // namespace sw::editor
