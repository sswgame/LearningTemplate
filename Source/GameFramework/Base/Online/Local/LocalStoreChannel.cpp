#include "pch.h"

#include "GameFramework/Base/Online/Local/LocalStoreChannel.h"

#include "Core/Concurrency/thread.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "GameFramework/Base/Online/Local/LocalDeviceKeyProvider.h"
#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"

namespace sw
{
    LocalStoreChannel::LocalStoreChannel()
        : _keyProvider{}
        , _sealContext{}
        , _store{}
        , _listPendingRequest{}
    {
    }

    LocalStoreChannel::LocalStoreChannel( unique_ptr<ILocalStore> store )
        : _keyProvider{}
        , _sealContext{}
        , _store{ std::move( store ) }
        , _listPendingRequest{}
    {
        SW_ASSERT( _store != nullptr );
    }

    // 구독 목록 칸이 맨 나중에 선언돼 먼저 지며 남은 구독을 끊고, 프런트가 봉인 문맥 · 키보다 먼저 진다(남은 쓰기를 마친다).
    LocalStoreChannel::~LocalStoreChannel() = default;

    bool LocalStoreChannel::open( string_view gameName, const LocalStoreLocation& location, INetSecurityProvider* pSecurityProvider, string& outError )
    {
        SW_ASSERT( _store == nullptr );
        if ( pSecurityProvider != nullptr )
        {
            _keyProvider = make_unique<LocalDeviceKeyProvider>( FileUtil::joinPath( UserDataPath::getDataDirectory( gameName ), LocalDeviceKeyProvider::kKeyFileName ), pSecurityProvider );
            _sealContext = LocalSealContext{ pSecurityProvider, _keyProvider.get(), nullptr };
        }
        const LocalStoreConfig config{ LocalStoreFactory::kFileBackendName, location };
        _store = LocalStoreFactory::createLocalStore( gameName, config, _sealContext, outError );
        if ( _store != nullptr )
            return true;
        _sealContext = LocalSealContext{};
        _keyProvider.reset();
        return false;
    }

    void LocalStoreChannel::drain()
    {
        // 완료 콜백이 잇는 쓰기(자동 저장의 슬롯 기록)까지 마치도록, 맡긴 일이 바닥날 때까지 완료를 넘기고 나서 닫는다. 종료 한 번의 대기라 폴링으로 둔다.
        while ( _store->getPendingCount() > 0 )
        {
            update();
            if ( _store->getPendingCount() > 0 )
                ThreadUtil::sleepForMilliseconds( 1 );
        }
        _store->shutdown();
    }

    void LocalStoreChannel::close()
    {
        _subscriptions.detachAll();
        _listPendingRequest.clear();
        _store.reset();
        _sealContext = LocalSealContext{};
        _keyProvider.reset();
    }

    Subscription LocalStoreChannel::submitRead( string_view slot, const CompletionFunction& onComplete )
    {
        return track( _store->submitRead( slot ), onComplete );
    }

    Subscription LocalStoreChannel::submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options, const CompletionFunction& onComplete )
    {
        return track( _store->submitWrite( slot, std::move( bytes ), options ), onComplete );
    }

    Subscription LocalStoreChannel::submitErase( string_view slot, const CompletionFunction& onComplete )
    {
        return track( _store->submitErase( slot ), onComplete );
    }

    Subscription LocalStoreChannel::track( uint64 requestID, const CompletionFunction& onComplete )
    {
        SW_ASSERT( requestID != 0 && onComplete.isBound() );
        _listPendingRequest.push_back( PendingRequest{ onComplete, requestID } );
        return _subscriptions.issue( requestID );
    }

    void LocalStoreChannel::releaseRequest( uint64 requestID )
    {
        (void)VectorUtil::removeIf( _listPendingRequest, [requestID]( const PendingRequest& request )
        { return request._requestID == requestID; } );
    }

    void LocalStoreChannel::update()
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
