#include "pch.h"

#include "Engine/UI/Screen/UIScreenMarkers.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    UIScreenMarkers::UIScreenMarkers()
        : _listAnchor{}
        , _listMarker{}
        , _markerView{}
        , _activeMarkerCount{ 0 }
        , _pGPUManager{ nullptr }
        , _listCanvasAnchor{}
        , _listFreeAnchor{}
        , _screen{ kInvalidUIScreenHandle }
    {
    }

    UIScreenMarkers::~UIScreenMarkers() = default;

    uint32 UIScreenMarkers::allocateAnchor()
    {
        uint32 anchor = static_cast<uint32>( _listAnchor.size() );
        if ( _listFreeAnchor.empty() == false )
        {
            anchor = _listFreeAnchor.back();
            _listFreeAnchor.pop_back();
            _listAnchor[anchor] = CanvasAnchor{};
        }
        else
        {
            _listAnchor.emplace_back();
        }
        return anchor;
    }

    void UIScreenMarkers::setAnchor( uint32 anchor, const float2& position, float32 scale )
    {
        SW_ASSERT( anchor < _listAnchor.size() ); // 마커 위젯이 살아 있는 동안만 부른다(번호는 `allocateAnchor` 가 줬다)
        if ( anchor >= _listAnchor.size() )
            return;
        CanvasAnchor& entry = _listAnchor[anchor];
        entry._position     = position; // UI 단위 — 물리 픽셀은 그리기 단계가 그 프레임 배율로 바꾼다(`publishCanvasAnchors`)
        entry._scale        = scale;
    }

    void UIScreenMarkers::setGPUMarker( uint32 anchor, const CanvasMarkerRecord& record )
    {
        SW_ASSERT( anchor < _listAnchor.size() ); // 마커 위젯이 살아 있는 동안만 부른다(번호는 `allocateAnchor` 가 줬다)
        if ( anchor >= _listAnchor.size() )
            return;
        if ( _listMarker.size() <= anchor )
            _listMarker.resize( _listAnchor.size() );
        CanvasMarkerRecord& entry  = _listMarker[anchor];
        const bool          bWasOn = entry._bActive != 0;
        entry                      = record;
        entry._bActive             = 1;
        if ( bWasOn == false )
            ++_activeMarkerCount;
    }

    void UIScreenMarkers::clearGPUMarker( uint32 anchor )
    {
        if ( anchor >= _listMarker.size() || _listMarker[anchor]._bActive == 0 )
            return;
        _listMarker[anchor]._bActive = 0;
        --_activeMarkerCount;
    }

    void UIScreenMarkers::publishCanvasAnchors( float32 uiScale )
    {
        // 앵커는 물리 픽셀이다(칠하기와 같은 배율). 정수로 맞춰야 앵커 기준으로 맞춘 사각형 가장자리가 픽셀에 남는다(글자가 흐려지지 않는다).
        _markerView._uiScale = uiScale; // GPU 투영도 같은 배율로 물리 픽셀 정수로 맞춘다
        _listCanvasAnchor.resize( _listAnchor.size() );
        for ( size_t index = 0; index < _listAnchor.size(); ++index )
        {
            const CanvasAnchor& source = _listAnchor[index];
            CanvasAnchor&       target = _listCanvasAnchor[index];
            target._position           = float2{ MathUtil::round( source._position._x * uiScale ), MathUtil::round( source._position._y * uiScale ) };
            // GPU 가 투영하는 마커는 컴퓨트가 덮어쓰기 전까지 접어 둔다 — 컴퓨트가 돌지 못하면(셰이더 없음 · 능력 없음) 왼쪽 위에 쌓여 보이지 않고 그려지지 않는다.
            const bool bGPUProjected = index < _listMarker.size() && _listMarker[index]._bActive != 0;
            target._scale            = bGPUProjected ? 0.0f : source._scale;
        }
    }

    void UIScreenMarkers::forgetScreen()
    {
        _screen = kInvalidUIScreenHandle;
        _listAnchor.clear();
        _listMarker.clear();
        _activeMarkerCount = 0;
        _listFreeAnchor.clear();
    }

    void UIScreenMarkers::clear()
    {
        forgetScreen();
        _listCanvasAnchor.clear();
    }
} // namespace sw
