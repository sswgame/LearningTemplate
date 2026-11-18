/**
 * @file EditorViewportPicker.h
 * @brief 씬 뷰의 클릭 선택 · 상자 선택 · 호버 강조 View 입니다. 마우스 · 수정 키 · 호버 빈도만 다루고, 판정과 선택 적용은 `EditorViewportPick`(Model)이 id 로 합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Viewport/Model/EditorViewportPick.h"

struct ImDrawList;

namespace sw::editor
{
    /**
     * @class EditorViewportPicker
     * @brief 씬 뷰 캔버스의 피킹입니다. 이번 프레임에 그은 아이콘 배지를 먼저 보고 레이 피킹을 한다(`EditorViewportPick::findUnderMouse`).
     * @details `EditorViewportClient` 가 값 멤버로 들고 시각화 그리기 뒤에 부릅니다(배지 화면 자리가 이번 프레임 값이다). 입력은 값(`EditorViewportPickInput`)만 받는다.
     */
    class EditorViewportPicker
    {
    public:
        EditorViewportPicker();

        /**
         * @brief 캔버스를 왼쪽 단추로 눌렀으면 마우스 아래 오브젝트를 선택하고, 빈 곳이면 선택을 비우고 상자 선택을 시작합니다. 기즈모 위나 Alt 중이면 하지 않습니다.
         * @details Ctrl 은 그 오브젝트를 넣고 빼고(Toggle), Shift 는 더한다(Add) — 언리얼 뷰포트와 같다. 수정 키가 있으면 빈 곳을 눌러도 선택을 비우지 않는다.
         */
        void processClick( const EditorViewportPickInput& input, bool bCanvasClicked, bool bGizmoBusy );
        /**
         * @brief 호버 오브젝트를 정합니다. 마우스가 움직였을 때만 피킹하며 프레임당 많아야 한 번입니다.
         * @param bGizmoBusy `EditorViewportGizmo::isMouseOverDrawnGizmo` 입니다.
         */
        void processHover( const EditorViewportPickInput& input, bool bCanvasHovered, bool bGizmoBusy );

        /**
         * @brief 빈 곳에서 시작한 왼쪽 끌기를 상자 선택으로 다룹니다. 끄는 동안 상자를 그리고, 떼면 상자에 걸친 오브젝트를 고릅니다(`EditorViewportPick::selectInRect`).
         * @details 유니티 씬 뷰 · Godot 3D 뷰의 끌기 선택과 같다(언리얼은 Ctrl+Alt 끌기).
         */
        void processMarquee( const EditorViewportPickInput& input, ImDrawList* pDrawList );

        /** @brief 상자 선택을 기다리거나 끄는 중이면 true 입니다(호버가 양보한다). */
        bool isMarqueeActive() const { return _bMarqueeArmed == SW_TRUE; }

        /** @brief 마우스 아래의 오브젝트 id 입니다. 호버 강조에 쓰며, 없거나 막혀 있으면 0 입니다. */
        uint64 getHoveredObjectID() const { return _hoveredObjectID; }

        /** @brief 마우스 아래 오브젝트를 찾습니다. 클릭 선택 · 호버 · 우클릭 메뉴가 함께 씁니다. 마우스가 캔버스 밖이면 false 입니다. */
        [[nodiscard]] static bool findObjectUnderMouse( const EditorViewportPickInput& input, EditorViewportPickHit& outHit );

    private:
        uint64  _hoveredObjectID;      ///< 마우스 아래 오브젝트. 0 이면 없다
        float64 _lastHoverPickSeconds; ///< 마지막 호버 피킹에 걸린 시간(초)
        float64 _lastHoverPickTime;    ///< 그 피킹을 한 ImGui 시각(초)
        float2  _hoverMousePos;        ///< 마지막으로 호버 피킹한 마우스 위치. 그대로이면 다시 찾지 않는다
        float2  _marqueeStart;         ///< 상자 선택을 시작한 화면 위치
        uint8   _marqueeMode;          ///< 상자 선택의 `EditorPickSelectMode`(누를 때의 수정 키)
        uint8   _bMarqueeArmed : 1;    ///< 빈 곳을 눌러 상자 선택을 기다리는 중(끌기 문턱을 넘으면 상자가 보인다)
    };
} // namespace sw::editor
