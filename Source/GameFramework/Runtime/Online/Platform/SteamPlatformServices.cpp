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
        // 생성자가 private 라 createWith 의 람다(이 멤버 함수 문맥)에서 만든다 — 맨 new 는 sw default_delete(Memory::free)와 힙이 어긋난다.
        return make_unique<SteamPlatformServices>.createWith( [&pBridge]( void* pMem )
        { return sw_placement_new( pMem ) SteamPlatformServices( std::move( pBridge ) ); } );
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
