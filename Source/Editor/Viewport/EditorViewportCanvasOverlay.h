/**
 * @file EditorViewportCanvasOverlay.h
 * @brief 씬 뷰 이미지 위에 ImGui 로 그리는 보조 그림입니다. 적응형 격자, 자(M 키), 방향 큐브가 있습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Viewport/Model/EditorGridUtil.h"

struct ImDrawList;

namespace sw::editor
{
    class EditorViewportCamera;

    /**
     * @class EditorViewportCanvasOverlay
     * @brief 씬 뷰 캔버스의 격자, 자, 방향 큐브를 그립니다. 상태는 자의 양 끝 두 점과 격자가 그린 결과입니다.
     * @details `EditorViewportClient` 가 값 멤버로 들고 직접 부릅니다. 오버레이 바(보기 · 표시 · 도구 · 트랜스폼)는 `EditorViewportOverlays` 이고,
     *          HUD 박스는 `EditorDebugHUDView` 입니다.
     */
    class EditorViewportCanvasOverlay
    {
    public:
        EditorViewportCanvasOverlay();

        /** @brief 자 도구입니다. M 키를 누르고 있거나 툴바의 자가 켜져 있으면 바닥(Y = 0)을 끌어 거리를 측정합니다. */
        void processRulerTool( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float32* pView, const float32* pProj, bool bShowRuler );

        /**
         * @brief 카메라 높이에 맞춰 간격을 고르는 격자를 그립니다. 2D 는 XY 평면, 3D 는 XZ 바닥입니다.
         * @details 선 모양은 월드 인덱스로만 정한다(`EditorGridUtil`). 그린 결과는 이 오버레이가 값으로 든 `EditorGridStats` 에 남아 자체 테스트가 읽는다(`getGridStats`).
         */
        void drawAdaptiveGrid( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float32* pView, const float32* pProj,
                               const float3& cameraPosition, bool b2DMode );
        /** @brief 격자가 마지막으로 그린 결과입니다. 에디터 시나리오 프로브(`Editor.Grid*`)와 자체 테스트가 읽습니다. */
        const EditorGridStats& getGridStats() const { return _gridStats; }
        /** @brief 오른쪽 위의 방향 큐브를 @p topInset 아래에 그립니다. 축 손잡이를 누르면 @p camera 를 그 축에서 보게 돌립니다. */
        static void drawOrientationCube( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, float32 topInset, EditorViewportCamera& camera );

    private:
        EditorGridStats        _gridStats; ///< 격자가 마지막으로 그린 결과(뷰포트마다 하나)
        float3                 _rulerStartWorld;
        float3                 _rulerEndWorld;
        uint8                  _bRulerActive : 1;
        [[maybe_unused]] uint8 _reserved     : 7;
    };
} // namespace sw::editor
