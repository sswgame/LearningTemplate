#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/GameLocalStore.h"

#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Config/GameSelection.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
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
        // 이미 연 프런트(넘겨받은 저장소 — 테스트의 메모리 저장소)는 다시 열지 않는다. 게임 인스턴스가 그것을 넘겨받아 서면 사용자 데이터 폴더를 쓰지 않는다.
        if ( _channel.isOpen() )
            return SubsystemInitResult::Succeeded;
        INetSecurityProvider* const pSecurityProvider = game::findSubsystem<INetSecurityProvider>();
        if ( pSecurityProvider == nullptr )
        {
            SW_LOG_ERROR_ID( "GameLocalStore.GameLocalStoreNeedsINetSecurityProviderGame", "GameLocalStore needs the INetSecurityProvider game service (device key and sealed slots) - saves are unavailable and the game does not start" );
            return SubsystemInitResult::Failed;
        }
        string error;
        if ( _channel.open( GameSelection::getName(), *LocalStoreLocation::tryUnderUserData( kSaveDirectory ), pSecurityProvider, error ) == false ) // 자리는 상수라 늘 풀린다
        {
            SW_LOG_ERROR_ID( "GameLocalStore.GameLocalStoreCouldNotOpen", "GameLocalStore could not open the local store: %# - the game does not start", error.c_str() );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }
} // namespace sw
