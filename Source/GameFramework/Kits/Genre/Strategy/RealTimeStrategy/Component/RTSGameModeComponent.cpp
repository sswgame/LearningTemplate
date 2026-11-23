#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/RTSGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"

namespace sw
{
    RTSGameModeComponent::RTSGameModeComponent()
        : _unitDataPath{}
        , _startMinerals{ 0 }
        , _pCatalog{}
    {
    }

    RTSGameModeComponent::~RTSGameModeComponent() = default;

    bool RTSGameModeComponent::acquireCatalog()
    {
        return acquireRequired( _unitDataPath, _pCatalog );
    }

    bool RTSGameModeComponent::hasUnits( std::initializer_list<const CatalogKey<RTSUnitDef>*> listUnitID ) const
    {
        bool bKnown = true;
        for ( const CatalogKey<RTSUnitDef>* pUnitID : listUnitID )
        {
            if ( _pCatalog->find( *pUnitID ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "RTSGameMode.UnitNotInCatalog", "%#: unit '%#' is empty or not in %# - the game cannot start", getTypeName().c_str(), pUnitID->c_str(), _unitDataPath.c_str() );
            bKnown = false;
        }
        return bKnown;
    }
} // namespace sw
