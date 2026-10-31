#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Save/GameLocalStore.h"

#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Config/GameSelection.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "GameLocalStore" );

    GameLocalStore::GameLocalStore()
        : _channel{}
    {
    }

    GameLocalStore::GameLocalStore( unique_ptr<ILocalStore> store )
        : _channel{ std::move( store ) }
    {
    }

    GameLocalStore::~GameLocalStore() = default;

    SubsystemInitResult GameLocalStore::initializeSubsystem()
    {
        SW_ASSERT( _channel.isOpen() == false ); // 이미 연 프런트(테스트)는 수명 함수를 거치지 않는다
        INetSecurityProvider* const pSecurityProvider = game::findSubsystem<INetSecurityProvider>();
        if ( pSecurityProvider == nullptr )
        {
            SW_LOG_ERROR( "GameLocalStore needs the INetSecurityProvider game service (device key and sealed slots) - saves are unavailable and the game does not start" );
            return SubsystemInitResult::Failed;
        }
        string error;
        if ( _channel.open( GameSelection::getName(), *LocalStoreLocation::tryUnderUserData( kSaveDirectory ), pSecurityProvider, error ) == false ) // 자리는 상수라 늘 풀린다
        {
            SW_LOG_ERROR( "GameLocalStore could not open the local store: %# - the game does not start", error.c_str() );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }
} // namespace sw
