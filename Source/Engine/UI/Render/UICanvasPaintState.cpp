#include "pch.h"

#include "Engine/UI/Render/UICanvasPaintState.h"

namespace sw
{
    UICanvasPaintState::UICanvasPaintState()
        : _pCanvas{ make_shared<CanvasDrawList>() }
        , _pCanvasSpare{}
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
               _pCanvas->_targetSize == targetSize;
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
        if ( _canvasScratch.isSameContent( *_pCanvas ) )
            return;
        // 바뀐 목록은 새 공유 목록으로 낸다 — 패킷(렌더 스레드)이 지난 목록을 쥐고 있을 수 있어 제자리에 쓰지 않는다.
        // 그 전 목록을 아무도 쥐지 않았으면(use_count 1 — 새 참조는 여기서만 생긴다) 그것을 맞바꿔 저장소를 다시 쓴다.
        shared_ptr<CanvasDrawList> pNext = ( _pCanvasSpare != nullptr && _pCanvasSpare.use_count() == 1 ) ? std::move( _pCanvasSpare ) : make_shared<CanvasDrawList>();
        std::swap( *pNext, _canvasScratch );
        _pCanvasSpare = std::const_pointer_cast<CanvasDrawList>( _pCanvas );
        _pCanvas      = std::move( pNext );
        ++_canvasRevision;
    }
} // namespace sw
