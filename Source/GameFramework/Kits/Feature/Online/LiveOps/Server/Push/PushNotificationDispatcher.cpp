#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsProtocol.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

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
    namespace
    {
        /** @brief 저장소 스레드에서 도는 푸시 일입니다. 서비스 멤버를 만지지 않고 인자로 받은 값만 씁니다. */
        struct PushStoreRun
        {
            /** @brief 계정의 기기 목록을 읽습니다. 저장소에 문제가 있거나 손상된 레코드가 있으면 빈 목록이다 — 이 알림은 버린다(배달은 최대 한 번이다). */
            static vector<PushDeviceRegistration> readDevices( IServiceStoreConnection& connection, AccountID accountID )
            {
                vector<PushDeviceRegistration> listDevice;
                vector<ServiceRecord>          listRecord;
                if ( PushNotificationDispatcherInternal::listDevices( connection, accountID, listRecord ) != ServiceStoreResult::Ok )
                    return listDevice;
                for ( const ServiceRecord& record : listRecord )
                {
                    PushDeviceRegistration registration;
                    if ( LiveOpsProtocol::decodeDevice( record._key, record._bytes, registration ) == false )
                    {
                        listDevice.clear();
                        return listDevice;
                    }
                    listDevice.push_back( std::move( registration ) );
                }
                return listDevice;
            }

            /** @brief 토픽 알림의 표를 "없어야 한다" 조건으로 씁니다. 쓰면 이 서버가 보낼 차례이고, 이미 있으면(Conflict) 다른 서버가 보냈다. */
            static ServiceStoreResult claimTopic( IServiceStoreConnection& connection, const string& claimKey )
            {
                ServiceTransaction transaction;
                transaction.put( PushNotificationDispatcherInternal::getTopicClaimTable(), claimKey, vector<uint8>{ 1 }, ServiceRecord::kAbsentVersion );
                return connection.commit( transaction );
            }

            /** @brief 기기 등록(덮어쓰기와 넘친 가장 오래된 기기 지우기)이나 해지(@p bytes 가 비었다)를 한 트랜잭션으로 씁니다. */
            static LiveOpsOutcome writeDevice( IServiceStoreConnection& connection, AccountID accountID, const string& key, const vector<uint8>& bytes )
            {
                using Internal             = PushNotificationDispatcherInternal;
                const hashed_string& table = Internal::getDeviceTable();
                ServiceTransaction   transaction;
                if ( bytes.empty() )
                {
                    ServiceRecord            current;
                    const ServiceStoreResult readResult = connection.readRecord( table, key, current );
                    if ( readResult == ServiceStoreResult::NotFound )
                        return LiveOpsResult::NotFound;
                    if ( readResult != ServiceStoreResult::Ok )
                        return LiveOpsOutcome::fail( OnlineError::kUnavailable );
                    transaction.erase( table, key, current._version );
                }
                else
                {
                    vector<ServiceRecord> listRecord;
                    if ( Internal::listDevices( connection, accountID, listRecord ) != ServiceStoreResult::Ok )
                        return LiveOpsOutcome::fail( OnlineError::kUnavailable );
                    bool          bReplacing  = false;
                    const string* pOldestKey  = nullptr;
                    int64         oldestMs    = 0;
                    int32         deviceCount = 0;
                    for ( const ServiceRecord& record : listRecord )
                    {
                        ++deviceCount;
                        bReplacing = bReplacing || record._key == key;
                        PushDeviceRegistration registration;
                        if ( LiveOpsProtocol::decodeDevice( record._key, record._bytes, registration ) == false )
                            return LiveOpsOutcome::fail( OnlineError::kUnavailable ); // 손상된 영속 레코드는 건너뛰지 않고 이 쓰기를 Unavailable 로 끝낸다
                        if ( record._key != key && ( pOldestKey == nullptr || registration._registeredMs < oldestMs ) )
                        {
                            pOldestKey = &record._key;
                            oldestMs   = registration._registeredMs;
                        }
                    }
                    const bool bEvictOldestDevice = bReplacing == false && deviceCount >= PushLimit::kMaxDevicePerAccount && pOldestKey != nullptr;
                    if ( bEvictOldestDevice )
                        transaction.erase( table, *pOldestKey ); // 한도를 넘었으므로 가장 오래 등록된 기기를 밀어낸다
                    transaction.put( table, key, bytes );
                }
                return connection.commit( transaction ) == ServiceStoreResult::Ok ? LiveOpsOutcome{ LiveOpsResult::Ok } : LiveOpsOutcome::fail( OnlineError::kUnavailable );
            }
        };
    } // namespace
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
        , _deliveryIDAllocator{}
        , _store{}
    {
    }

    void PushNotificationDispatcher::initialize( IServiceStore* pStore, const PushDispatcherConfig& settings )
    {
        SW_ASSERT( pStore != nullptr );
        _store.bindStore( pStore );
        _accountBucketMap.initialize( settings._accountBurst, settings._accountRefillIntervalMs, static_cast<size_t>( settings._maxTrackedAccountCount ) );
    }

    bool PushNotificationDispatcher::registerProvider( IPushNotificationProvider* pProvider )
    {
        if ( pProvider == nullptr || findProvider( pProvider->getProviderID() ) != nullptr )
        {
            SW_LOG_ERROR_ID( "PushNotificationDispatcher.PushDispatcherProviderNullRegistered", "PushDispatcher: provider is null or registered twice" );
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

    bool PushNotificationDispatcher::notifyAccount( AccountID accountID, const PushNotificationMessage& message, UnixMilliseconds nowMs )
    {
        int64 retryAfterMs = 0;
        if ( _store.hasStore() == false || _accountBucketMap.tryConsume( accountID, nowMs.count(), retryAfterMs ) == false )
        {
            ++_stats._rateLimitedCount;
            return false;
        }
        _store.submit( [accountID]( IServiceStoreConnection& connection )
        { return PushStoreRun::readDevices( connection, accountID ); },
                       [this, accountID, message, nowMs]( vector<PushDeviceRegistration>& listDevice )
        { applyDevices( accountID, message, std::move( listDevice ), nowMs ); } );
        return true;
    }

    int32 PushNotificationDispatcher::notifyTopic( string_view topic, const PushNotificationMessage& message, UnixMilliseconds nowMs )
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
            delivery._nextAttemptMs      = nowMs.count();
            delivery._bTopic             = SW_TRUE;
            sendDelivery( deliveryID, delivery, nowMs );
            ++handedCount;
        }
        return handedCount;
    }

    void PushNotificationDispatcher::notifyTopicOnce( string_view claimKey, string_view topic, const PushNotificationMessage& message, UnixMilliseconds nowMs )
    {
        if ( _store.hasStore() == false || ServiceTransaction::isValidKey( claimKey ) == false )
        {
            ++_stats._topicSkippedCount;
            return;
        }
        _store.submit( [key = string( claimKey )]( IServiceStoreConnection& connection )
        { return PushStoreRun::claimTopic( connection, key ); },
                       [this, topicName = string( topic ), message, nowMs]( ServiceStoreResult result )
        { applyTopicClaim( topicName, message, result, nowMs ); } );
    }

    void PushNotificationDispatcher::applyTopicClaim( const string& topic, const PushNotificationMessage& message, ServiceStoreResult result, UnixMilliseconds nowMs )
    {
        if ( result != ServiceStoreResult::Ok )
        {
            ++_stats._topicSkippedCount; // Conflict 는 다른 서버가 이미 보냈다는 뜻이다
            return;
        }
        ++_stats._topicClaimedCount;
        (void)notifyTopic( topic, message, nowMs );
    }

    void PushNotificationDispatcher::applyDevices( AccountID accountID, const PushNotificationMessage& message, vector<PushDeviceRegistration>&& listDevice, UnixMilliseconds nowMs )
    {
        for ( PushDeviceRegistration& device : listDevice )
        {
            if ( findProvider( device._providerID ) == nullptr )
                continue; // 이 서버 빌드에 없는 제공자이다
            const uint64 deliveryID = _deliveryIDAllocator.allocate();
            Delivery&    delivery   = _mapDelivery[deliveryID];
            delivery._message       = message;
            delivery._device        = std::move( device );
            delivery._accountID     = accountID;
            delivery._nextAttemptMs = nowMs.count();
            sendDelivery( deliveryID, delivery, nowMs );
        }
    }

    void PushNotificationDispatcher::sendDelivery( uint64 deliveryID, Delivery& delivery, UnixMilliseconds nowMs )
    {
        delivery._bInFlight = SW_TRUE;
        ++_stats._sentCount;
        IPushNotificationProvider* pProvider = findProvider( delivery._device._providerID );
        if ( delivery._bTopic != SW_FALSE )
            pProvider->sendToTopic( deliveryID, delivery._topic, delivery._message, nowMs );
        else
            pProvider->send( deliveryID, delivery._device._token, delivery._device._locale, delivery._message, nowMs );
    }

    void PushNotificationDispatcher::update( UnixMilliseconds nowMs )
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
                        delivery._nextAttemptMs = nowMs.count() + MathUtil::max( result._retryAfterMs, PushLimit::kFirstBackoffMs << ( delivery._attempt - 1 ) );
                        break;
                    }
                    case PushDeliveryStatus::Rejected:
                    {
                        ++_stats._droppedCount;
                        SW_LOG_WARNING_ID( "PushNotificationDispatcher.PushDispatcherProviderRejectedNotification", "PushDispatcher: provider '%#' rejected a notification (account %#, topic '%#')", delivery._device._providerID.c_str(), delivery._accountID,
                                           delivery._topic.c_str() );
                        _mapDelivery.erase( deliveryIt );
                        break;
                    }
                }
            }
        }
        for ( auto& [deliveryID, delivery] : _mapDelivery ) // 백오프가 끝난 배달을 다시 보낸다
        {
            if ( delivery._bInFlight == SW_FALSE && nowMs.count() >= delivery._nextAttemptMs )
                sendDelivery( deliveryID, delivery, nowMs );
        }
    }

    void PushNotificationDispatcher::registerDevice( AccountID accountID, const PushDeviceRegistration& registration, uint64 requestTag )
    {
        // 길이 상한(토큰 · 언어)은 `PushDeviceRegistration` 의 `PROPERTY( MaxCount )` 가 원본이다. 요청 본문 읽기(고정 배치형)와 레코드 쓰기(`encodeDevice`)가 막으므로 여기 다시 적지 않는다.
        const bool bValid = accountID != kInvalidAccountID && PushLimit::isValidProviderID( registration._providerID ) && registration._token.empty() == false;
        if ( bValid == false || _store.hasStore() == false )
        {
            if ( requestTag != 0 )
                _completionBuffer.push( PushDeviceCompletion{ requestTag, bValid ? LiveOpsOutcome::fail( OnlineError::kUnavailable ) : LiveOpsOutcome::fail( OnlineError::kInvalidRequest ) } );
            return;
        }
        vector<uint8> bytes;
        if ( LiveOpsProtocol::encodeDevice( registration, bytes ) == false )
        {
            if ( requestTag != 0 )
                _completionBuffer.push( PushDeviceCompletion{ requestTag, LiveOpsOutcome::fail( OnlineError::kInvalidRequest ) } ); // 쓰기가 오류 로그를 남겼다
            return;
        }
        submitWrite( accountID, makeDeviceKey( accountID, registration._providerID, registration._token ), std::move( bytes ), requestTag );
    }

    void PushNotificationDispatcher::unregisterDevice( AccountID accountID, string_view providerID, string_view token, uint64 requestTag )
    {
        const bool bInvalidUnregister = PushLimit::isValidProviderID( providerID ) == false || token.empty() || _store.hasStore() == false;
        if ( bInvalidUnregister )
        {
            if ( requestTag != 0 )
                _completionBuffer.push( PushDeviceCompletion{ requestTag, LiveOpsOutcome::fail( OnlineError::kInvalidRequest ) } );
            return;
        }
        submitWrite( accountID, makeDeviceKey( accountID, providerID, token ), vector<uint8>{}, requestTag ); // 빈 몸은 해지다
    }

    void PushNotificationDispatcher::submitWrite( AccountID accountID, string&& key, vector<uint8>&& bytes, uint64 requestTag )
    {
        _store.submit( [accountID, key = std::move( key ), bytes = std::move( bytes )]( IServiceStoreConnection& connection )
        { return PushStoreRun::writeDevice( connection, accountID, key, bytes ); },
                       [this, requestTag]( LiveOpsOutcome result )
        { applyWrite( requestTag, result ); } );
    }

    void PushNotificationDispatcher::applyWrite( uint64 requestTag, LiveOpsOutcome result )
    {
        if ( requestTag != 0 )
            _completionBuffer.push( PushDeviceCompletion{ requestTag, result } );
    }
} // namespace sw
