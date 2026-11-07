#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSVision.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    void RTSVision::reset( int32 width, int32 height )
    {
        _topology = GridTopology{ width, height };
        _listTeamCell.clear();
        _repaintTimer.clear();
    }

    void RTSVision::ensureTeamCount( int32 teamCount )
    {
        while ( getTeamCount() < teamCount )
        {
            _listTeamCell.emplace_back( static_cast<size_t>( _topology.getCellCount() ), static_cast<uint8>( RTSVisibility::Unexplored ) );
        }
    }

    bool RTSVision::updateRepaintTimer( float32 deltaTime, float32 interval )
    {
        _repaintTimer.update( deltaTime );
        if ( _repaintTimer.isActive() )
            return false;
        _repaintTimer.start( interval );
        return true;
    }

    void RTSVision::beginRepaint()
    {
        for ( vector<uint8>& listCell : _listTeamCell )
        {
            for ( uint8& visibility : listCell )
            {
                if ( visibility == static_cast<uint8>( RTSVisibility::Visible ) )
                    visibility = static_cast<uint8>( RTSVisibility::Explored );
            }
        }
    }

    void RTSVision::reveal( int32 team, const int2& center, float32 sight )
    {
        vector<uint8>& listCell     = _listTeamCell[static_cast<size_t>( team )];
        const int32    radius       = static_cast<int32>( sight );
        const float32  sightSquared = sight * sight;
        for ( int32 y = MathUtil::max( 0, center._y - radius ); y <= MathUtil::min( _topology._height - 1, center._y + radius ); ++y )
        {
            for ( int32 x = MathUtil::max( 0, center._x - radius ); x <= MathUtil::min( _topology._width - 1, center._x + radius ); ++x )
            {
                const float32 dx = static_cast<float32>( x - center._x );
                const float32 dy = static_cast<float32>( y - center._y );
                if ( dx * dx + dy * dy <= sightSquared )
                    listCell[static_cast<size_t>( _topology.toIndex( x, y ) )] = static_cast<uint8>( RTSVisibility::Visible );
            }
        }
    }

    RTSVisibility RTSVision::get( int32 team, const int2& cell ) const
    {
        const bool bOutside = team < 0 || team >= getTeamCount() || cell._x < 0 || cell._y < 0 || cell._x >= _topology._width || cell._y >= _topology._height;
        if ( bOutside )
            return RTSVisibility::Unexplored;
        return static_cast<RTSVisibility>( _listTeamCell[static_cast<size_t>( team )][static_cast<size_t>( _topology.toIndex( cell ) )] );
    }

    void RTSVision::serializeState( StateIO& io )
    {
        io.list( _listTeamCell, sizeof( uint32 ), [&]( vector<uint8>& listCell )
        {
            io.value( listCell );
            if ( io.isWriting() )
                return;
            io.check( listCell.size() == static_cast<size_t>( _topology.getCellCount() ) );
            for ( const uint8 visibility : listCell )
            {
                io.check( visibility <= static_cast<uint8>( RTSVisibility::Visible ) );
            }
        } );
        io.value( _repaintTimer );
    }

} // namespace sw
