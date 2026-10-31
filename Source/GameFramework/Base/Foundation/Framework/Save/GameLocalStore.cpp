#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Save/GameLocalStore.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Config/GameSelection.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Online/Local/LocalDeviceKeyProvider.h"
#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"

namespace sw
{
    SW_LOG_CALLER( "GameLocalStore" );

    GameLocalStore::GameLocalStore()
        : _keyProvider{}
        , _sealContext{}
        , _store{}
        , _listPendingRequest{}
    {
    }

    GameLocalStore::GameLocalStore( unique_ptr<ILocalStore> store )
        : _keyProvider{}
        , _sealContext{}
        , _store{ std::move( store ) }
        , _listPendingRequest{}
    {
        SW_ASSERT( _store != nullptr );
    }

    // 구독 목록 칸이 맨 나중에 선언돼 먼저 지며 남은 구독을 끊고, 프런트가 봉인 문맥 · 키보다 먼저 진다(남은 쓰기를 마친다).
    GameLocalStore::~GameLocalStore() = default;

    SubsystemInitResult GameLocalStore::initializeSubsystem()
    {
        SW_ASSERT( _store == nullptr ); // 이미 연 프런트(테스트)는 수명 함수를 거치지 않는다
        INetSecurityProvider* const pSecurityProvider = game::findSubsystem<INetSecurityProvider>();
        if ( pSecurityProvider == nullptr )
        {
            SW_LOG_ERROR( "GameLocalStore needs the INetSecurityProvider game service (device key and sealed slots) - saves are unavailable and the game does not start" );
            return SubsystemInitResult::Failed;
        }
        const string gameName = GameSelection::getName();
        _keyProvider          = make_unique<LocalDeviceKeyProvider>( FileUtil::joinPath( UserDataPath::getDataDirectory( gameName ), LocalDeviceKeyProvider::kKeyFileName ), pSecurityProvider );
        _sealContext          = LocalSealContext{ pSecurityProvider, _keyProvider.get(), nullptr };

        LocalStoreConfig config{ LocalStoreFactory::kFileBackendName, *LocalStoreLocation::tryUnderUserData( kSaveDirectory ) }; // 상수라 늘 풀린다
        string           error;
        _store = LocalStoreFactory::createLocalStore( gameName, config, _sealContext, error );
        if ( _store == nullptr )
        {
            SW_LOG_ERROR( "GameLocalStore could not open the local store: %# - the game does not start", error.c_str() );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void GameLocalStore::stopSubsystem()
    {
        _store->shutdown();
        update(); // 끝까지 마친 쓰기의 완료를 넘긴다(세이브 알림). 이 뒤에 맡긴 요청은 IOError 로 끝나고 넘기지 않는다
    }

    void GameLocalStore::shutdownSubsystem()
    {
        _subscriptions.detachAll();
        _listPendingRequest.clear();
        _store.reset();
        _sealContext = LocalSealContext{};
        _keyProvider.reset();
    }

    Subscription GameLocalStore::submitRead( string_view slot, const CompletionFunction& onComplete )
    {
        return track( _store->submitRead( slot ), onComplete );
    }

    Subscription GameLocalStore::submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options, const CompletionFunction& onComplete )
    {
        return track( _store->submitWrite( slot, std::move( bytes ), options ), onComplete );
    }

    Subscription GameLocalStore::track( uint64 requestID, const CompletionFunction& onComplete )
    {
        SW_ASSERT( requestID != 0 && onComplete.isBound() );
        _listPendingRequest.push_back( PendingRequest{ onComplete, requestID } );
        return _subscriptions.issue( requestID );
    }

    void GameLocalStore::releaseRequest( uint64 requestID )
    {
        (void)VectorUtil::removeIf( _listPendingRequest, [requestID]( const PendingRequest& request )
        { return request._requestID == requestID; } );
    }

    void GameLocalStore::update()
    {
        if ( _store == nullptr )
            return;
        // 콜백이 새 요청을 맡길 수 있으므로 회수한 완료는 지역 목록에 둔다.
        vector<LocalStoreCompletion> listCompletion;
        _store->pollCompletions( listCompletion );
        for ( const LocalStoreCompletion& completion : listCompletion )
        {
            for ( size_t index = 0; index < _listPendingRequest.size(); ++index )
            {
                if ( _listPendingRequest[index]._requestID != completion._requestID )
                    continue;
                const CompletionFunction onComplete = _listPendingRequest[index]._onComplete;
                _listPendingRequest.erase( _listPendingRequest.begin() + static_cast<ptrdiff_t>( index ) );
                _subscriptions.detach( completion._requestID ); // 끝난 요청 - 주인이 든 구독을 끈다(그 뒤 풀어도 아무것도 하지 않는다)
                onComplete( completion );
                break;
            }
            // 목록에 없으면 주인이 구독을 놓은 요청이다 - 완료를 버린다.
        }
    }
} // namespace sw
