#include "pch.h"

#include "GameFramework/Base/Online/Platform/SteamPlatformServices.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "SteamPlatformServices" );

    unique_ptr<SteamPlatformServices> SteamPlatformServices::create( unique_ptr<ISteamApiBridge> pBridge, string& outError )
    {
        if ( pBridge == nullptr )
        {
            outError = "no Steam API bridge was given (the plugin that links the Steam SDK provides it)";
            SW_LOG_ERROR( "Steam platform services need an ISteamApiBridge - none was given" );
            return nullptr;
        }
        if ( pBridge->initialize( outError ) == false )
        {
            SW_LOG_ERROR( "Steam SDK initialization failed: %#", outError.c_str() );
            return nullptr;
        }
        return unique_ptr<SteamPlatformServices>( new SteamPlatformServices( std::move( pBridge ) ) );
    }

    SteamPlatformServices::SteamPlatformServices( unique_ptr<ISteamApiBridge> pBridge )
        : _pBridge{ std::move( pBridge ) }
    {
    }

    SteamPlatformServices::~SteamPlatformServices()
    {
        _pBridge->shutdown();
    }
} // namespace sw
