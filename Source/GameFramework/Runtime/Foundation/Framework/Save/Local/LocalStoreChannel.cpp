#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalStoreChannel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/thread.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalDeviceKeyProvider.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalStoreBackend.h"

namespace sw
{
    /**
     * @struct LocalStoreChannelList
     * @brief 살아 있는 채널 목록의 머리입니다(게임 스레드 — 채널은 게임 스레드에서 만들고 부수고 묻는다).
     * @details GameFramework 의 모듈 부분이라 엔진 범위와 함께 생기고 사라진다(일정 시스템 목록 `ScheduleSystemRegistry` 와 같은 자리).
     */
    struct LocalStoreChannelList
    {
        static constexpr const utf8* kModulePartName = "LocalStoreChannelList";

        LocalStoreChannel* _pFirst{ nullptr };
    };

    SW_MODULE_PART( Engine, LocalStoreChannelList, LocalStoreChannelList );

    namespace
    {
        /** @brief 저장 · 썸네일 쓰기가 끝나기 전에 시나리오가 그 결과를 보거나 끝나지 않게 한다(에디터 자리 잡기 · `<Pass/>`). */
        bool isLocalStoreBusy()
        {
            return LocalStoreChannel::isAnyChannelBusy();
        }
    } // namespace

    SW_AUTOMATION_FINISH_BLOCKER( localStoreRequests, "LocalStoreRequests", &isLocalStoreBusy );

    LocalStoreChannel::LocalStoreChannel()
        : _keyProvider{}
        , _sealContext{}
        , _store{}
        , _listPendingRequest{}
    {
        linkLive();
    }

    LocalStoreChannel::LocalStoreChannel( unique_ptr<ILocalStore> store )
        : _keyProvider{}
        , _sealContext{}
        , _store{ std::move( store ) }
        , _listPendingRequest{}
    {
        SW_ASSERT( _store != nullptr );
        linkLive();
    }

    // 구독 목록 칸이 맨 나중에 선언돼 먼저 지며 남은 구독을 끊고, 프런트가 봉인 문맥 · 키보다 먼저 진다(남은 쓰기를 마친다).
    LocalStoreChannel::~LocalStoreChannel()
    {
        unlinkLive();
    }

    void LocalStoreChannel::linkLive()
    {
        _pLiveList = game::findSubsystem<LocalStoreChannelList>();
        if ( _pLiveList == nullptr )
            return; // 게임 서비스가 없다(테스트의 지역 채널) — 걸 목록이 없다
        _pNextLive = _pLiveList->_pFirst;
        if ( _pLiveList->_pFirst != nullptr )
            _pLiveList->_pFirst->_pPrevLive = this;
        _pLiveList->_pFirst = this;
    }

    void LocalStoreChannel::unlinkLive()
    {
        if ( _pLiveList == nullptr )
            return; // 걸지 않았다
        if ( _pPrevLive != nullptr )
            _pPrevLive->_pNextLive = _pNextLive;
        else
            _pLiveList->_pFirst = _pNextLive;
        if ( _pNextLive != nullptr )
            _pNextLive->_pPrevLive = _pPrevLive;
        _pPrevLive = nullptr;
        _pNextLive = nullptr;
        _pLiveList = nullptr;
    }

    bool LocalStoreChannel::isAnyChannelBusy()
    {
        const LocalStoreChannelList* const pList = game::findSubsystem<LocalStoreChannelList>();
        for ( const LocalStoreChannel* pChannel = pList != nullptr ? pList->_pFirst : nullptr; pChannel != nullptr; pChannel = pChannel->_pNextLive )
        {
            if ( pChannel->isBusy() )
                return true;
        }
        return false;
    }

    expected<void> LocalStoreChannel::open( string_view gameName, const LocalStoreLocation& location, INetSecurityProvider* pSecurityProvider )
    {
        SW_ASSERT( _store == nullptr );
        if ( pSecurityProvider != nullptr )
        {
            _keyProvider = make_unique<LocalDeviceKeyProvider>( FileUtil::joinPath( UserDataPath::getDataDirectory( gameName ), LocalDeviceKeyProvider::kKeyFileName ), pSecurityProvider );
            _sealContext = LocalSealContext{ pSecurityProvider, _keyProvider.get(), nullptr };
        }
        const LocalStoreConfig            config{ LocalStoreBackend::kFileBackendName, location };
        expected<unique_ptr<ILocalStore>> store = LocalStoreBackend::createLocalStore( gameName, config, _sealContext );
        if ( store.has_value() )
        {
            _store = std::move( *store );
            return {};
        }
        _sealContext = LocalSealContext{};
        _keyProvider.reset();
        return unexpected( store.error() );
    }

    void LocalStoreChannel::flush()
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
