#include "pch.h"

#include "Engine/Scene/SceneSettings.h"

#include "Core/Log/Logger.h"

namespace sw
{
    const SceneDataLayerDef* SceneSettings::findDataLayer( string_view name ) const
    {
        for ( const SceneDataLayerDef& layer : _listDataLayer )
        {
            if ( layer._name == name )
                return &layer;
        }
        return nullptr;
    }

    bool SceneSettings::validate( string_view sceneName ) const
    {
        bool bValid = true;
        for ( size_t layerIndex = 0; layerIndex < _listDataLayer.size(); ++layerIndex )
        {
            const string& name = _listDataLayer[layerIndex]._name;
            // 쉼표는 엔티티의 `layers` 칸을 가른다 — 이름에 들면 읽을 때 다른 레이어가 된다.
            const bool bBadName = name.empty() || name.find( ',' ) != string::npos;
            if ( bBadName || findDataLayer( name ) != &_listDataLayer[layerIndex] )
            {
                SW_LOG_ERROR_ID( "SceneSettings.DataLayerNameInvalid", "Scene '%#' declares data layer '%#' with an empty, comma-separated or repeated name", sceneName, name );
                bValid = false;
            }
        }
        if ( _bStreaming == false )
            return bValid;

        StreamingCellGrid grid;
        if ( StreamingCellGrid::tryCreate( _cellGrid, grid ) == false )
        {
            SW_LOG_ERROR_ID( "SceneSettings.SceneStreamsButCell", "Scene '%#' streams but its cell grid is not usable (cell size %#, loading radius %#)", sceneName, _cellGrid._cellSize,
                             _cellGrid._loadingRadius );
            bValid = false;
        }
        if ( _maxCellLoadsPerFrame == 0 )
        {
            SW_LOG_ERROR_ID( "SceneSettings.SceneStreamsButLoads", "Scene '%#' streams but loads no cell per frame (_maxCellLoadsPerFrame is 0)", sceneName );
            bValid = false;
        }
        if ( ( _unloadMargin >= 0.0f ) == false )
        {
            SW_LOG_ERROR_ID( "SceneSettings.SceneNegativeUnloadMargin", "Scene '%#' has a negative unload margin (%#)", sceneName, _unloadMargin );
            bValid = false;
        }
        return bValid;
    }
} // namespace sw
