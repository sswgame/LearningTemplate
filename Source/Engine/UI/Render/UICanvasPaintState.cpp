#include "pch.h"

#include "Engine/UI/Render/UICanvasPaintState.h"

namespace sw
{
    UICanvasPaintState::UICanvasPaintState()
        : _canvas{}
        , _canvasScratch{}
        , _paintScratch{}
        , _canvasRevision{ 1 }
        , _listPaintedTree{}
        , _deferredGlyphCount{ 0 }
        , _arrLayerHideCount{}
        , _hiddenLayers{ UILayerMask::None }
        , _paintedHiddenLayers{ UILayerMask::None }
        , _bLastCanvasReusable{ SW_FALSE }
    {
    }

    UICanvasPaintState::~UICanvasPaintState() = default;

    void UICanvasPaintState::addLayerHideRequest( UILayerMask layers )
    {
        for ( uint32 layerIndex = 0; layerIndex < kUILayerCount; ++layerIndex )
        {
            if ( UILayerUtil::contains( layers, static_cast<UILayer>( layerIndex ) ) )
                ++_arrLayerHideCount[layerIndex];
        }
        rebuildHiddenLayers();
    }

    void UICanvasPaintState::removeLayerHideRequest( UILayerMask layers )
    {
        for ( uint32 layerIndex = 0; layerIndex < kUILayerCount; ++layerIndex )
        {
            if ( UILayerUtil::contains( layers, static_cast<UILayer>( layerIndex ) ) == false )
                continue;
            SW_ASSERT( _arrLayerHideCount[layerIndex] > 0 );
            if ( _arrLayerHideCount[layerIndex] > 0 )
                --_arrLayerHideCount[layerIndex];
        }
        rebuildHiddenLayers();
    }

    void UICanvasPaintState::rebuildHiddenLayers()
    {
        UILayerMask hidden = UILayerMask::None;
        for ( uint32 layerIndex = 0; layerIndex < kUILayerCount; ++layerIndex )
        {
            if ( _arrLayerHideCount[layerIndex] > 0 )
                hidden = EnumUtil::setFlag( hidden, UILayerUtil::toMask( static_cast<UILayer>( layerIndex ) ) );
        }
        _hiddenLayers = hidden;
    }

    bool UICanvasPaintState::canReuseLastCanvas( bool bReusableFrame, size_t screenCount, const float2& targetSize ) const
    {
        return _bLastCanvasReusable == SW_TRUE && bReusableFrame && _paintedHiddenLayers == _hiddenLayers && _listPaintedTree.size() == screenCount &&
               _canvas._targetSize == targetSize;
    }

    CanvasDrawList& UICanvasPaintState::beginPaint( bool bReusableFrame, const float2& targetSize )
    {
        _listPaintedTree.clear();
        _bLastCanvasReusable = bReusableFrame ? SW_TRUE : SW_FALSE;
        _paintedHiddenLayers = _hiddenLayers;
        _canvasScratch.clear();
        _canvasScratch._targetSize = targetSize;
        return _canvasScratch;
    }

    void UICanvasPaintState::endPaint( uint32 deferredGlyphCount )
    {
        _deferredGlyphCount = deferredGlyphCount;
        if ( _canvasScratch.isSameContent( _canvas ) )
            return;
        // 바뀐 목록은 맞바꾼다(복사하지 않는다). 다음 프레임은 지난 목록을 비우고 칠하기 목록으로 쓴다.
        std::swap( _canvas, _canvasScratch );
        ++_canvasRevision;
    }
} // namespace sw
