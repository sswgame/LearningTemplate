#include "pch.h"

#include "Engine/Window/WindowMonitor.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include <utility>

namespace sw
{
    namespace
    {
        struct WindowMonitorInternal
        {
            static bool isResolutionBefore( const int2& left, const int2& right )
            {
                return left._x != right._x ? left._x < right._x : left._y < right._y;
            }

            static bool isTooSmall( const int2& resolution )
            {
                return resolution._x < WindowMonitor::kMinResolutionSide || resolution._y < WindowMonitor::kMinResolutionSide;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool WindowMonitor::tryParseNetWorkArea( const vector<int64>& listValue, int64 desktopIndex, WindowMonitorRect& outArea )
    {
        constexpr int64 kValuesPerDesktop = 4;
        const int64     first             = desktopIndex * kValuesPerDesktop;
        const bool      bInRange          = desktopIndex >= 0 && first + kValuesPerDesktop <= static_cast<int64>( listValue.size() );
        if ( bInRange == false )
            return false;
        const size_t index = static_cast<size_t>( first );
        outArea._position  = int2{ static_cast<int32>( listValue[index] ), static_cast<int32>( listValue[index + 1] ) };
        outArea._size      = int2{ static_cast<int32>( listValue[index + 2] ), static_cast<int32>( listValue[index + 3] ) };
        return outArea._size._x > 0 && outArea._size._y > 0;
    }

    void WindowMonitor::clipWorkArea( const WindowMonitorRect& desktopWorkArea )
    {
        const int32 left   = MathUtil::max( _area._position._x, desktopWorkArea._position._x );
        const int32 top    = MathUtil::max( _area._position._y, desktopWorkArea._position._y );
        const int32 right  = MathUtil::min( _area._position._x + _area._size._x, desktopWorkArea._position._x + desktopWorkArea._size._x );
        const int32 bottom = MathUtil::min( _area._position._y + _area._size._y, desktopWorkArea._position._y + desktopWorkArea._size._y );
        const bool  bEmpty = right <= left || bottom <= top;
        _workArea          = bEmpty ? _area : WindowMonitorRect{
                                         int2{        left,          top},
                                         int2{right - left, bottom - top}
        };
    }

    void WindowMonitor::appendModeResolutions( const vector<WindowMonitorMode>& listMode, const vector<uint64>& listModeId, bool bSwapAxes )
    {
        for ( const uint64 modeId : listModeId )
        {
            for ( const WindowMonitorMode& mode : listMode )
            {
                if ( mode._id == modeId )
                    _listResolution.push_back( bSwapAxes ? int2{ mode._size._y, mode._size._x } : mode._size );
            }
        }
    }

    void WindowMonitor::normalizeResolutions()
    {
        (void)VectorUtil::removeIf( _listResolution, &WindowMonitorInternal::isTooSmall );
        VectorUtil::sortUnique( _listResolution, &WindowMonitorInternal::isResolutionBefore );
    }

    void WindowMonitor::orderPrimaryFirst( vector<WindowMonitor>& inoutListMonitor )
    {
        if ( inoutListMonitor.empty() )
            return;
        for ( size_t index = 1; index < inoutListMonitor.size(); ++index )
        {
            if ( inoutListMonitor[index]._bPrimary )
            {
                std::swap( inoutListMonitor[0], inoutListMonitor[index] );
                break;
            }
        }
        inoutListMonitor[0]._bPrimary = true;
    }
} // namespace sw
