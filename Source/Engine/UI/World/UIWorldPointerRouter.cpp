#include "pch.h"

#include "Engine/UI/World/UIWorldPointerRouter.h"

#include "Engine/UI/World/WidgetComponent.h"

namespace sw
{
    UIWorldPointerRouter::UIWorldPointerRouter()
        : _pHovered{ nullptr }
        , _pCaptured{ nullptr }
        , _lastPoint{}
    {
    }

    bool UIWorldPointerRouter::route( const vector<WidgetComponent*>& listComponent, const WorldWidgetRay& ray, UIPointerEvent event )
    {
        WidgetComponent* pTarget = nullptr;
        WorldWidgetHit   hit{};
        if ( _pCaptured != nullptr )
        {
            // 끌기 중이다. 광선이 사각형을 벗어나도 평면 위의 점을 그 위젯이 받는다.
            if ( _pCaptured->pickWorld( ray, false, hit ) )
                pTarget = _pCaptured;
        }
        else
        {
            float32 nearest = 0.0f;
            for ( WidgetComponent* pComponent : listComponent )
            {
                WorldWidgetHit candidate{};
                if ( pComponent->pickWorld( ray, true, candidate ) == false )
                    continue;
                if ( pTarget == nullptr || candidate._distance < nearest )
                {
                    pTarget = pComponent;
                    hit     = candidate;
                    nearest = candidate._distance;
                }
            }
        }

        if ( pTarget != _pHovered )
        {
            if ( _pHovered != nullptr && _pHovered != _pCaptured )
                _pHovered->clearWorldHover();
            _pHovered  = pTarget;
            _lastPoint = hit._point; // 새 위젯에 들어왔다 — 첫 이벤트의 변화량은 0
        }
        if ( pTarget == nullptr )
            return false;

        event._position              = hit._point;
        event._delta                 = float2{ hit._point._x - _lastPoint._x, hit._point._y - _lastPoint._y };
        _lastPoint                   = hit._point;
        const UIPointerResult result = pTarget->dispatchWorldPointer( event );
        _pCaptured                   = pTarget->isWorldPointerCaptured() ? pTarget : nullptr;
        return result._bHandled == SW_TRUE;
    }

    void UIWorldPointerRouter::releaseHover()
    {
        if ( _pHovered != nullptr && _pHovered != _pCaptured )
            _pHovered->clearWorldHover();
        _pHovered = _pCaptured;
    }

    void UIWorldPointerRouter::forgetComponent( const WidgetComponent& component )
    {
        if ( _pHovered == &component )
            _pHovered = nullptr;
        if ( _pCaptured == &component )
            _pCaptured = nullptr;
    }
} // namespace sw
