/**
 * @file EditorNodeGraph.h
 * @brief imgui-node-editor 컨텍스트 수명주기 및 캔버스 Begin/End
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"

namespace ax::NodeEditor
{
    struct EditorContext;
} // namespace ax::NodeEditor

namespace sw::editor
{
    /**
     * @class EditorNodeGraph
     * @brief 노드 그래프 캔버스 호스트입니다. 패널 · 팝업 · 섹션이 멤버로 소유합니다.
     */
    class EditorNodeGraph
    {
    public:
        EditorNodeGraph();
        ~EditorNodeGraph();

        /** @brief 노드 에디터 컨텍스트를 해제합니다. */
        void shutdown();

        /** @brief 컨텍스트를 준비하고 캔버스를 엽니다. false 면 endCanvas 를 부르지 마십시오. */
        bool beginCanvas( const utf8* pCanvasId, const utf8* pSettingsFileName );
        /** @brief beginCanvas()와 짝을 이룹니다. */
        void endCanvas();

        /** @brief Begin 없이 현재 에디터만 바인딩합니다(위치 조회용). */
        bool bind() const;
        /** @brief bind()와 짝을 이룹니다. */
        void unbind() const;

        /** @brief 컨텍스트가 만들어져 있으면 true입니다. */
        bool isReady() const { return _pEditor != nullptr; }

        /** @brief 다음 Begin에서 노드 위치를 시드하고 뷰를 맞출지 여부입니다. */
        bool needsContentFit() const { return _bNeedsContentFit; }
        /** @brief 다음 캔버스에서 콘텐츠에 맞게 줌/팬하도록 요청합니다. */
        void requestContentFit() { _bNeedsContentFit = true; }
        /**
         * @brief 요청이 있으면 캔버스 안에서 한 번 NavigateToContent 를 호출합니다 — 캔버스 크기가 앞 프레임과 같을 때만(아니면 요청을 남겨 다음 프레임에).
         * @details 캔버스 크기가 막 정해진 프레임(처음 열기 · Reload 와 함께 옆 인스펙터가 붙거나 떨어짐)에 맞추면 노드 편집기가 옛 크기의 보이는
         *          사각형으로 맞춘다는 가설(패널 점검 D21 — Dialogue Graph 캔버스가 일부만 덮고 링크가 패널 밖에 그려짐)에 따른 것이다.
         */
        void applyContentFitIfNeeded();
        /** @brief 맞추기를 해도 되는 크기인지입니다 — 0 이 아니고 앞 프레임과 같다. */
        static bool isCanvasSizeSettled( const float2& previousSize, const float2& currentSize )
        {
            return currentSize._x > 0.0f && currentSize._y > 0.0f && previousSize._x == currentSize._x && previousSize._y == currentSize._y;
        }

    private:
        void ensureContext( const utf8* pSettingsFileName );
        void destroyContext();

        ax::NodeEditor::EditorContext* _pEditor;
        string                         _settingsPath;
        float2                         _previousCanvasSize; ///< 앞 프레임의 캔버스 크기(beginCanvas 가 잰다)
        float2                         _canvasSize;         ///< 이번 프레임의 캔버스 크기
        bool                           _bNeedsContentFit;
    };
} // namespace sw::editor
