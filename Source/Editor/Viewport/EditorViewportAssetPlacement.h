/**
 * @file EditorViewportAssetPlacement.h
 * @brief 씬 뷰 캔버스의 애셋 배치 View 입니다 — 끄는 동안 고스트(놓일 자리 · 표면 · 거절 이유)를 그리고, 놓이면 `EditorPlacementDrag`(Model)에 넘깁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Viewport/Model/EditorPlacementDrag.h"

struct ImDrawList;

namespace sw
{
    struct float4x4;
} // namespace sw

namespace sw::editor
{
    struct ViewportToolbarSettings;

    /**
     * @class EditorViewportAssetPlacement
     * @brief 씬 뷰에 끌어 온 애셋을 표면 위에 놓습니다. 표면 찾기 · 놓기 · 열기는 `EditorPlacementDrag`(Model), 이 클래스는 끌기 페이로드 · 회전 키 · 고스트 그리기 · 통계다.
     * @details 놓을 수 없는 종류(씬 · 머티리얼 …)는 고스트가 "놓으면 연다" 고 알리고 놓을 때 연다. Play 중에는 배치를 거절한다. 놓을 자리 강조는 이 캔버스 하나뿐이다.
     *          끄는 동안 회전 키로 위 축 둘레를 돌려 놓을 수 있다(하우징 · 건설 게임의 놓기 전 회전 — 언리얼 · 유니티는 놓은 뒤 기즈모로 돌린다). 고스트가 앞쪽 화살표와 각도를 보인다.
     */
    class EditorViewportAssetPlacement
    {
    public:
        EditorViewportAssetPlacement();

        /**
         * @brief 이번 프레임의 끌기를 처리합니다. 캔버스 위로 끌려 온 애셋은 고스트를 그리고, 놓였으면 만들거나 엽니다. 캔버스 이미지를 그린 창 안에서 부른다.
         * @param viewProj 캔버스 이미지를 그린 카메라의 뷰-투영 행렬입니다.
         */
        void process( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float4x4& viewProj, const ViewportToolbarSettings& settings );
        /** @brief 마지막으로 한 일입니다. */
        const EditorPlacementStats& getStats() const { return _stats; }

    private:
        /** @brief 놓일 자리 고리 · 법선 · 글을 그립니다. */
        void drawGhost( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float4x4& viewProj, const string& assetPath, const EditorPlacementGhost& ghost ) const;
        /** @brief 회전 키(입력 맵 `viewport.placementRotate` · `viewport.placementRotateBack`)를 읽어 끄는 동안의 회전(`_stats._yaw`)을 돌립니다. */
        void updateDragYaw();

    private:
        EditorPlacementDrag  _drag; ///< 끌기 한 번의 놓기(Model — 끄는 동안 정적 물리 사본을 든다)
        EditorPlacementStats _stats;
    };
} // namespace sw::editor
