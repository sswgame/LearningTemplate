/**
 * @file UICanvasPaintState.h
 * @brief 화면 UI 그리기 목록과 그 재사용 판정 상태, 레이어 숨김 요청 수입니다(`UISystem` 의 값 멤버).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Render/UIPaintPass.h"
#include "Engine/UI/Screen/UILayer.h"

namespace sw
{
    class WidgetTree;

    /**
     * @class UICanvasPaintState
     * @brief 이번 프레임 그리기 목록(`_canvas`)과 칠하는 중인 목록, 내용 번호, 지난 그리기의 트리 순서 · 숨김 레이어, 레이어마다의 숨김 요청 수를 듭니다.
     * @details 화면 스택 순회 · 포커스 테두리 · 앵커 내기는 `UISystem::paintScreens` 가 하고, 이 클래스는 "지난 목록을 그대로 쓸 수 있는가" 의 판정에 쓰는 값과
     *          목록 맞바꾸기만 맡는다. 판정 순서(프레임 조건 → 트리마다 `isPaintedTree` → `UIPaintPass::canReuseOutput`)는 `paintScreens` 본문이 기준이다.
     */
    class SW_API UICanvasPaintState
    {
    public:
        UICanvasPaintState();
        ~UICanvasPaintState();
        UICanvasPaintState( const UICanvasPaintState& )            = delete;
        UICanvasPaintState& operator=( const UICanvasPaintState& ) = delete;

        // --- 레이어 숨김 ---------------------------------------------------------------
        /** @brief @p layers 의 레이어마다 숨김 요청 수를 하나 올리고 숨긴 레이어 집합을 다시 만듭니다. */
        void addLayerHideRequest( UILayerMask layers );
        /** @brief @p layers 의 레이어마다 숨김 요청 수를 하나 내리고(0 아래로는 내리지 않는다) 숨긴 레이어 집합을 다시 만듭니다. */
        void        removeLayerHideRequest( UILayerMask layers );
        bool        isLayerHidden( UILayer layer ) const { return _arrLayerHideCount[static_cast<uint32>( layer )] > 0; }
        UILayerMask getHiddenLayers() const { return _hiddenLayers; }

        // --- 그리기 목록 ---------------------------------------------------------------
        /**
         * @brief 트리 순회 앞의 재사용 조건입니다. 지난 목록이 트리 출력만으로 이루어졌고 @p bReusableFrame(포커스 테두리 · 화면 막기 없음)이며
         *        숨긴 레이어 · 화면 수 · 대상 크기가 지난 그리기와 같으면 true 입니다.
         */
        [[nodiscard]] bool canReuseLastCanvas( bool bReusableFrame, size_t screenCount, const float2& targetSize ) const;
        /** @brief 지난 그리기의 @p index 번째 트리가 @p tree 이면 true 입니다(`canReuseLastCanvas` 가 화면 수를 맞춘 뒤에 부른다). */
        [[nodiscard]] bool isPaintedTree( size_t index, const WidgetTree& tree ) const { return _listPaintedTree[index] == &tree; }
        /** @brief 지난 목록을 그대로 쓴다. 미룬 위젯은 다시 무효화되므로 재사용한 프레임에는 미룬 글리프가 없다. */
        void markLastCanvasReused() { _deferredGlyphCount = 0; }
        /**
         * @brief 새로 칠할 준비를 합니다. 지난 트리 순서를 비우고(부르는 쪽이 `addPaintedTree` 로 채운다) 재사용 조건과 숨긴 레이어를 기록한 뒤,
         *        칠하는 중인 목록을 비워 대상 크기 @p targetSize 로 돌려줍니다.
         */
        CanvasDrawList& beginPaint( bool bReusableFrame, const float2& targetSize );
        void            addPaintedTree( const WidgetTree& tree ) { _listPaintedTree.push_back( &tree ); }
        /** @brief 칠하기를 끝냅니다. 칠한 목록이 지난 목록과 내용이 다를 때만 맞바꾸고(복사하지 않는다) 내용 번호를 올립니다. */
        void endPaint( uint32 deferredGlyphCount );
        /** @brief 칠하기 문맥(`UIPaintContext::_pScratch`)으로 넘기는 순회 스크래치입니다. */
        UIPaintScratch* getPaintScratch() const { return &_paintScratch; }
        /** @brief 기동 뒤 자란 스크래치를 놓습니다(종료 누수 보고가 이 상태의 소멸보다 먼저다). */
        void releasePaintScratch() { _paintScratch = UIPaintScratch{}; }

        const CanvasDrawList& getCanvas() const { return _canvas; }
        uint64                getCanvasRevision() const { return _canvasRevision; }
        uint32                getDeferredGlyphCount() const { return _deferredGlyphCount; }

    private:
        /** @brief 레이어마다의 숨김 요청 수에서 `_hiddenLayers` 를 다시 만듭니다. */
        void rebuildHiddenLayers();

    private:
        CanvasDrawList            _canvas;                           ///< 이번 프레임의 그리기 목록입니다.
        CanvasDrawList            _canvasScratch;                    ///< 칠하는 중인 목록입니다(같은 내용이면 버립니다).
        mutable UIPaintScratch    _paintScratch;                     ///< 그리기 순회 스크래치입니다. 칠하기 문맥으로 넘기고, 이 상태와 함께 사라집니다.
        uint64                    _canvasRevision;                   ///< `_canvas` 내용의 번호입니다.
        vector<const WidgetTree*> _listPaintedTree;                  ///< 지난 그리기의 화면 트리 순서입니다(같고 모두 재사용이면 이어 붙이기를 건너뛴다).
        uint32                    _deferredGlyphCount;               ///< 지난 그리기가 래스터화 예산 때문에 미룬 글리프 수입니다.
        uint16                    _arrLayerHideCount[kUILayerCount]; ///< 레이어마다 건 숨김 요청 수입니다.
        UILayerMask               _hiddenLayers;                     ///< 숨김 요청이 하나라도 걸린 레이어입니다(`_arrLayerHideCount` 에서 만든 값).
        UILayerMask               _paintedHiddenLayers;              ///< 지난 그리기가 따른 `_hiddenLayers` 입니다(달라지면 목록을 다시 만든다).
        uint8                     _bLastCanvasReusable;              ///< 지난 그리기 목록이 트리 출력만으로 이루어졌다(포커스 테두리 · 숨김 없음).
    };
} // namespace sw
