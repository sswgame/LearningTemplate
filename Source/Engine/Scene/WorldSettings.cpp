#include "pch.h"

#include "Engine/Scene/WorldSettings.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "WorldSettings" );

    bool WorldSettings::validate( string_view sceneName ) const
    {
        if ( _bStreaming == false )
            return true;

        bool          bValid = true;
        WorldCellGrid grid;
        if ( WorldCellGrid::tryCreate( _cellGrid, grid ) == false )
        {
            SW_LOG_ERROR( "Scene '%#' streams but its cell grid is not usable (cell size %#, loading radius %#)", sceneName, _cellGrid._cellSize,
                          _cellGrid._loadingRadius );
            bValid = false;
        }
        if ( _maxCellLoadsPerFrame == 0 )
        {
            SW_LOG_ERROR( "Scene '%#' streams but loads no cell per frame (_maxCellLoadsPerFrame is 0)", sceneName );
            bValid = false;
        }
        if ( ( _unloadMargin >= 0.0f ) == false )
        {
            SW_LOG_ERROR( "Scene '%#' has a negative unload margin (%#)", sceneName, _unloadMargin );
            bValid = false;
        }
        return bValid;
    }
} // namespace sw
