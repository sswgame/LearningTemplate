#include "pch.h"

#include "GameFramework/Runtime/Online/Platform/SteamPlatformServices.h"

#include "Core/Log/Logger.h"

namespace sw
{
    expected<unique_ptr<SteamPlatformServices>> SteamPlatformServices::create( unique_ptr<ISteamAPIBridge> pBridge )
    {
        if ( pBridge == nullptr )
        {
            SW_LOG_ERROR_ID( "SteamPlatformServices.SteamPlatformServicesNeed", "Steam platform services need an ISteamAPIBridge - none was given" );
            return unexpected( Error{ ErrorKind::InvalidArgument, "no Steam API bridge was given (the plugin that links the Steam SDK provides it)" } );
        }
        if ( expected<void> initialized = pBridge->initialize(); initialized.has_value() == false )
        {
            SW_LOG_ERROR_ID( "SteamPlatformServices.SteamSDKInitializationFailed", "Steam SDK initialization failed: %# (%#)", initialized.error().message(), initialized.error().detail() );
            return unexpected( initialized.error() );
        }
        return unique_ptr<SteamPlatformServices>( new SteamPlatformServices( std::move( pBridge ) ) );
    }

    SteamPlatformServices::SteamPlatformServices( unique_ptr<ISteamAPIBridge> pBridge )
        : _pBridge{ std::move( pBridge ) }
    {
    }

    SteamPlatformServices::~SteamPlatformServices()
    {
        _pBridge->shutdown();
    }
} // namespace sw
