#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsProtocol.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct PushNotificationDispatcherInternal
        {
            static const hashed_string& getDeviceTable()
            {
                static const hashed_string s_table{ "liveops_device" };
                return s_table;
            }

            static const hashed_string& getTopicClaimTable()
            {
                static const hashed_string s_table{ "liveops_push" };
                return s_table;
            }

            /** @brief 계정의 기기 레코드를 모두 읽습니다. 한도보다 하나 더 읽어서 넘친 것도 보이게 합니다. */
            static ServiceStoreResult listDevices( IServiceStoreConnection& connection, AccountID accountID, vector<ServiceRecord>& outListRecord )
            {
                return connection.listRecords( getDeviceTable(), PushNotificationDispatcher::makeAccountPrefix( accountID ), "", PushLimit::kMaxDevicePerAccount + 1, false,
                                               outListRecord );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 계정의 기기 목록을 읽는 작업입니다. 알림 하나에 해당합니다. */
    class PushDeviceReadWork final : public IServiceStoreWork
    {
    public:
        PushDeviceReadWork( PushNotificationDispatcher* pDispatcher, AccountID accountID, const PushNotificationMessage& message, int64 nowMs )
            : _listDevice{}
            , _message{ message }
            , _pDispatcher{ pDispatcher }
            , _accountID{ accountID }
            , _nowMs{ nowMs }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            vector<ServiceRecord> listRecord;
            if ( PushNotificationDispatcherInternal::listDevices( connection, _accountID, listRecord ) != ServiceStoreResult::Ok )
                return; // 저장소에 문제가 있으므로 이 알림은 버린다. 배달은 최대 한 번이다
            for ( const ServiceRecord& record : listRecord )
            {
                PushDeviceRegistration registration;
                if ( LiveOpsProtocol::decodeDevice( record._key, record._bytes, registration ) == false )
                {
                    _listDevice.clear(); // 손상된 영속 레코드는 건너뛰지 않는다. 저장소 문제와 같이 이 알림을 버린다
                    return;
                }
                _listDevice.push_back( std::move( registration ) );
            }
        }

        void complete() override { _pDispatcher->applyDevices( _accountID, _message, std::move( _listDevice ), _nowMs ); }

    private:
        vector<PushDeviceRegistration> _listDevice;
        PushNotificationMessage        _message;
        PushNotificationDispatcher*    _pDispatcher;
        AccountID                      _accountID;
        int64                          _nowMs;
    };
} // namespace sw

namespace sw
{
    /** @brief 토픽 알림의 표를 "없어야 한다" 조건으로 쓰는 작업입니다. 쓰면 이 서버가 보낼 차례이고, 이미 있으면(Conflict) 다른 서버가 보냈다. */
    class PushTopicClaimWork final : public IServiceStoreWork
    {
    public:
        PushTopicClaimWork( PushNotificationDispatcher* pDispatcher, string claimKey, string topic, const PushNotificationMessage& message, int64 nowMs )
            : _claimKey{ std::move( claimKey ) }
            , _topic{ std::move( topic ) }
            , _message{ message }
            , _pDispatcher{ pDispatcher }
            , _nowMs{ nowMs }
            , _result{ ServiceStoreResult::Unavailable }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            ServiceTransaction transaction;
            transaction.put( PushNotificationDispatcherInternal::getTopicClaimTable(), _claimKey, vector<uint8>{ 1 }, ServiceRecord::kAbsentVersion );
            _result = connection.commit( transaction );
        }

        void complete() override { _pDispatcher->applyTopicClaim( _topic, _message, _result, _nowMs ); }

    private:
        string                      _claimKey;
        string                      _topic;
        PushNotificationMessage     _message;
        PushNotificationDispatcher* _pDispatcher;
        int64                       _nowMs;
        ServiceStoreResult          _result;
    };
} // namespace sw

namespace sw
{
    /** @brief 기기 등록(덮어쓰기와 넘친 가장 오래된 기기 지우기)과 해지를 한 트랜잭션으로 쓰는 작업입니다. */
    class PushDeviceWriteWork final : public IServiceStoreWork
    {
    public:
        vector<uint8>               _bytes; ///< 등록할 기기 레코드이다. 해지일 때는 비어 있다
        string                      _key;
        PushNotificationDispatcher* _pDispatcher;
        AccountID                   _accountID;
        uint64                      _requestTag;
        LiveOpsResult               _result;
        uint8                       _bErase;

        PushDeviceWriteWork()
            : _bytes{}
            , _key{}
            , _pDispatcher{ nullptr }
            , _accountID{ kInvalidAccountID }
            , _requestTag{ 0 }
            , _result{ LiveOpsResult::Unavailable }
            , _bErase{ SW_FALSE }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            using Internal             = PushNotificationDispatcherInternal;
            const hashed_string& table = Internal::getDeviceTable();
            ServiceTransaction   transaction;
            if ( _bErase != SW_FALSE )
            {
                ServiceRecord            current;
                const ServiceStoreResult readResult = connection.readRecord( table, _key, current );
                if ( readResult == ServiceStoreResult::NotFound )
                {
                    _result = LiveOpsResult::NotFound;
                    return;
                }
                if ( readResult != ServiceStoreResult::Ok )
                    return;
                transaction.erase( table, _key, current._version );
            }
            else
            {
                vector<ServiceRecord> listRecord;
                if ( Internal::listDevices( connection, _accountID, listRecord ) != ServiceStoreResult::Ok )
                    return;
                bool          bReplacing  = false;
                const string* pOldestKey  = nullptr;
                int64         oldestMs    = 0;
                int32         deviceCount = 0;
                for ( const ServiceRecord& record : listRecord )
                {
                    ++deviceCount;
                    bReplacing = bReplacing || record._key == _key;
                    PushDeviceRegistration registration;
                    if ( LiveOpsProtocol::decodeDevice( record._key, record._bytes, registration ) == false )
                        return; // 손상된 영속 레코드는 건너뛰지 않고 이 쓰기를 Unavailable 로 끝낸다
                    if ( record._key != _key && ( pOldestKey == nullptr || registration._registeredMs < oldestMs ) )
                    {
                        pOldestKey = &record._key;
                        oldestMs   = registration._registeredMs;
                    }
                }
                const bool bEvictOldestDevice = bReplacing == false && deviceCount >= PushLimit::kMaxDevicePerAccount && pOldestKey != nullptr;
                if ( bEvictOldestDevice )
                    transaction.erase( table, *pOldestKey ); // 한도를 넘었으므로 가장 오래 등록된 기기를 밀어낸다
                transaction.put( table, _key, _bytes );
            }
            _result = connection.commit( transaction ) == ServiceStoreResult::Ok ? LiveOpsResult::Ok : LiveOpsResult::Unavailable;
        }

        void complete() override { _pDispatcher->applyWrite( _requestTag, _result ); }
    };
} // namespace sw

namespace sw
{
    PushNotificationDispatcher::PushNotificationDispatcher()
        : _mapDelivery{}
        , _listProvider{}
        , _listResultScratch{}
        , _completionBuffer{}
        , _accountBucketMap{}
        , _stats{}
        , _pStore{ nullptr }
        , _deliveryIDAllocator{}
        , _pendingWorkCount{ 0 }
    {
    }

    void PushNotificationDispatcher::initialize( IServiceStore* pStore, const PushDispatcherConfig& settings )
    {
        SW_ASSERT( pStore != nullptr );
        _pStore = pStore;
        _accountBucketMap.initialize( settings._accountBurst, settings._accountRefillIntervalMs, static_cast<size_t>( settings._maxTrackedAccountCount ) );
    }

    bool PushNotificationDispatcher::registerProvider( IPushNotificationProvider* pProvider )
    {
        if ( pProvider == nullptr || findProvider( pProvider->getProviderID() ) != nullptr )
        {
            SW_LOG_ERROR( "PushDispatcher: provider is null or registered twice" );
            return false;
        }
        _listProvider.push_back( pProvider );
        return true;
    }

    string PushNotificationDispatcher::makeAccountPrefix( AccountID accountID )
    {
        return ServiceKeyUtil::makeHex64( accountID ) + "/";
    }

    string PushNotificationDispatcher::makeDeviceKey( AccountID accountID, string_view providerID, string_view token )
    {
        // 토큰은 키 규칙 밖의 글자를 가질 수 있다. 그래서 키에는 해시만 쓰고 토큰은 값에 둔다
        return makeAccountPrefix( accountID ) + string( providerID ) + "." + ServiceKeyUtil::makeHex64( HashUtil::computeStringHash64( token.data(), token.size(), false ) );
    }

    IPushNotificationProvider* PushNotificationDispatcher::findProvider( string_view providerID ) const
    {
        for ( IPushNotificationProvider* pProvider : _listProvider )
        {
            if ( pProvider->getProviderID() == providerID )
                return pProvider;
        }
        return nullptr;
    }

    bool PushNotificationDispatcher::notifyAccount( AccountID accountID, const PushNotificationMessage& message, int64 nowMs )
    {
        int64 retryAfterMs = 0;
        if ( _pStore == nullptr || _accountBucketMap.tryConsume( accountID, nowMs, retryAfterMs ) == false )
        {
            ++_stats._rateLimitedCount;
            return false;
        }
        ++_pendingWorkCount;
        _pStore->submit( make_unique<PushDeviceReadWork>( this, accountID, message, nowMs ) );
        return true;
    }

    int32 PushNotificationDispatcher::notifyTopic( string_view topic, const PushNotificationMessage& message, int64 nowMs )
    {
        int32 handedCount = 0;
        for ( IPushNotificationProvider* pProvider : _listProvider )
        {
            if ( pProvider->supportsTopics() == false )
                continue;
            const uint64 deliveryID      = _deliveryIDAllocator.allocate();
            Delivery&    delivery        = _mapDelivery[deliveryID];
            delivery._message            = message;
            delivery._topic              = string( topic );
            delivery._device._providerID = string( pProvider->getProviderID() );
            delivery._nextAttemptMs      = nowMs;
            delivery._bTopic             = SW_TRUE;
            sendDelivery( deliveryID, delivery, nowMs );
            ++handedCount;
        }
        return handedCount;
    }

    void PushNotificationDispatcher::notifyTopicOnce( string_view claimKey, string_view topic, const PushNotificationMessage& message, int64 nowMs )
    {
        if ( _pStore == nullptr || ServiceTransaction::isValidKey( claimKey ) == false )
        {
            ++_stats._topicSkippedCount;
            return;
        }
        ++_pendingWorkCount;
        _pStore->submit( make_unique<PushTopicClaimWork>( this, string( claimKey ), string( topic ), message, nowMs ) );
    }

    void PushNotificationDispatcher::applyTopicClaim( const string& topic, const PushNotificationMessage& message, ServiceStoreResult result, int64 nowMs )
    {
        --_pendingWorkCount;
        if ( result != ServiceStoreResult::Ok )
        {
            ++_stats._topicSkippedCount; // Conflict 는 다른 서버가 이미 보냈다는 뜻이다
            return;
        }
        ++_stats._topicClaimedCount;
        (void)notifyTopic( topic, message, nowMs );
    }

    void PushNotificationDispatcher::applyDevices( AccountID accountID, const PushNotificationMessage& message, vector<PushDeviceRegistration>&& listDevice, int64 nowMs )
    {
        --_pendingWorkCount;
        for ( PushDeviceRegistration& device : listDevice )
        {
            if ( findProvider( device._providerID ) == nullptr )
                continue; // 이 서버 빌드에 없는 제공자이다
            const uint64 deliveryID = _deliveryIDAllocator.allocate();
            Delivery&    delivery   = _mapDelivery[deliveryID];
            delivery._message       = message;
            delivery._device        = std::move( device );
            delivery._accountID     = accountID;
            delivery._nextAttemptMs = nowMs;
            sendDelivery( deliveryID, delivery, nowMs );
        }
    }

    void PushNotificationDispatcher::sendDelivery( uint64 deliveryID, Delivery& delivery, int64 nowMs )
    {
        delivery._bInFlight = SW_TRUE;
        ++_stats._sentCount;
        IPushNotificationProvider* pProvider = findProvider( delivery._device._providerID );
        if ( delivery._bTopic != SW_FALSE )
            pProvider->sendToTopic( deliveryID, delivery._topic, delivery._message, nowMs );
        else
            pProvider->send( deliveryID, delivery._device._token, delivery._device._locale, delivery._message, nowMs );
    }

    void PushNotificationDispatcher::update( int64 nowMs )
    {
        for ( IPushNotificationProvider* pProvider : _listProvider )
        {
            pProvider->update( nowMs );
            _listResultScratch.clear();
            (void)pProvider->pollResults( _listResultScratch );
            for ( const PushDeliveryResult& result : _listResultScratch )
            {
                const auto deliveryIt = _mapDelivery.find( result._deliveryID );
                if ( deliveryIt == _mapDelivery.end() )
                    continue;
                Delivery& delivery  = deliveryIt->second;
                delivery._bInFlight = SW_FALSE;
                switch ( result._status )
                {
                    case PushDeliveryStatus::Delivered:
                    {
                        ++_stats._deliveredCount;
                        _mapDelivery.erase( deliveryIt );
                        break;
                    }
                    case PushDeliveryStatus::InvalidToken:
                    {
                        ++_stats._invalidTokenCount;
                        if ( delivery._bTopic == SW_FALSE )
                            unregisterDevice( delivery._accountID, delivery._device._providerID, delivery._device._token, 0 ); // 요청 태그 0은 완료가 없다
                        _mapDelivery.erase( deliveryIt );
                        break;
                    }
                    case PushDeliveryStatus::Transient:
                    case PushDeliveryStatus::RateLimited:
                    {
                        if ( ++delivery._attempt >= PushLimit::kMaxRetry )
                        {
                            ++_stats._droppedCount;
                            _mapDelivery.erase( deliveryIt );
                            break;
                        }
                        delivery._nextAttemptMs = nowMs + std::max( result._retryAfterMs, PushLimit::kFirstBackoffMs << ( delivery._attempt - 1 ) );
                        break;
                    }
                    case PushDeliveryStatus::Rejected:
                    {
                        ++_stats._droppedCount;
                        SW_LOG_WARNING( "PushDispatcher: provider '%#' rejected a notification (account %#, topic '%#')", delivery._device._providerID.c_str(), delivery._accountID,
                                        delivery._topic.c_str() );
                        _mapDelivery.erase( deliveryIt );
                        break;
                    }
                }
            }
        }
        for ( auto& [deliveryID, delivery] : _mapDelivery ) // 백오프가 끝난 배달을 다시 보낸다
        {
            if ( delivery._bInFlight == SW_FALSE && nowMs >= delivery._nextAttemptMs )
                sendDelivery( deliveryID, delivery, nowMs );
        }
    }

    void PushNotificationDispatcher::registerDevice( AccountID accountID, const PushDeviceRegistration& registration, uint64 requestTag )
    {
        // 길이 상한(토큰 · 언어)은 `PushDeviceRegistration` 의 `PROPERTY( MaxCount )` 가 원본이다. 요청 본문 읽기(고정 배치형)와 레코드 쓰기(`encodeDevice`)가 막으므로 여기 다시 적지 않는다.
        const bool bValid = accountID != kInvalidAccountID && PushLimit::isValidProviderID( registration._providerID ) && registration._token.empty() == false;
        if ( bValid == false || _pStore == nullptr )
        {
            if ( requestTag != 0 )
                _completionBuffer.push( PushDeviceCompletion{ requestTag, bValid ? LiveOpsResult::Unavailable : LiveOpsResult::Invalid } );
            return;
        }
        unique_ptr<PushDeviceWriteWork> work = make_unique<PushDeviceWriteWork>();
        if ( LiveOpsProtocol::encodeDevice( registration, work->_bytes ) == false )
        {
            if ( requestTag != 0 )
                _completionBuffer.push( PushDeviceCompletion{ requestTag, LiveOpsResult::Invalid } ); // 쓰기가 오류 로그를 남겼다
            return;
        }
        work->_key         = makeDeviceKey( accountID, registration._providerID, registration._token );
        work->_pDispatcher = this;
        work->_accountID   = accountID;
        work->_requestTag  = requestTag;
        ++_pendingWorkCount;
        _pStore->submit( std::move( work ) );
    }

    void PushNotificationDispatcher::unregisterDevice( AccountID accountID, string_view providerID, string_view token, uint64 requestTag )
    {
        const bool bInvalidUnregister = PushLimit::isValidProviderID( providerID ) == false || token.empty() || _pStore == nullptr;
        if ( bInvalidUnregister )
        {
            if ( requestTag != 0 )
                _completionBuffer.push( PushDeviceCompletion{ requestTag, LiveOpsResult::Invalid } );
            return;
        }
        unique_ptr<PushDeviceWriteWork> work = make_unique<PushDeviceWriteWork>();
        work->_key                           = makeDeviceKey( accountID, providerID, token );
        work->_pDispatcher                   = this;
        work->_accountID                     = accountID;
        work->_requestTag                    = requestTag;
        work->_bErase                        = SW_TRUE;
        ++_pendingWorkCount;
        _pStore->submit( std::move( work ) );
    }

    void PushNotificationDispatcher::applyWrite( uint64 requestTag, LiveOpsResult result )
    {
        --_pendingWorkCount;
        if ( requestTag != 0 )
            _completionBuffer.push( PushDeviceCompletion{ requestTag, result } );
    }
} // namespace sw
