#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/BitStream.h"

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

            static vector<uint8> encodeDevice( const PushDeviceRegistration& registration )
            {
                BitWriter writer;
                writer.writeBits( LiveOpsProtocol::kRecordFormat, 8 );
                LiveOpsProtocol::writeDevice( writer, registration );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeDevice( const vector<uint8>& bytes, PushDeviceRegistration& outRegistration )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                return reader.readBits( 8 ) == LiveOpsProtocol::kRecordFormat && LiveOpsProtocol::readDevice( reader, outRegistration );
            }

            /** @brief 계정의 기기 레코드를 모두 읽습니다(한도 + 1 — 넘친 것도 보이게). */
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
    /** @brief 계정의 기기 목록을 읽는 일입니다(알림 하나). */
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
                return; // 저장소가 아프다 — 이 알림은 버린다(최대 한 번)
            for ( const ServiceRecord& record : listRecord )
            {
                PushDeviceRegistration registration;
                if ( PushNotificationDispatcherInternal::decodeDevice( record._bytes, registration ) )
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
    /** @brief 기기 등록(덮기 + 넘친 가장 오래된 것 지우기) · 해지를 한 트랜잭션으로 쓰는 일입니다. */
    class PushDeviceWriteWork final : public IServiceStoreWork
    {
    public:
        PushDeviceRegistration      _registration;
        string                      _key;
        PushNotificationDispatcher* _pDispatcher;
        AccountID                   _accountID;
        uint64                      _requestTag;
        LiveOpsResult               _result;
        uint8                       _bErase;

        PushDeviceWriteWork()
            : _registration{}
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
                    if ( Internal::decodeDevice( record._bytes, registration ) == false )
                        continue;
                    if ( record._key != _key && ( pOldestKey == nullptr || registration._registeredMs < oldestMs ) )
                    {
                        pOldestKey = &record._key;
                        oldestMs   = registration._registeredMs;
                    }
                }
                if ( bReplacing == false && deviceCount >= PushLimit::kMaxDevicePerAccount && pOldestKey != nullptr )
                    transaction.erase( table, *pOldestKey ); // 한도 — 가장 오래 등록된 기기를 밀어낸다
                transaction.put( table, _key, Internal::encodeDevice( _registration ) );
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
        , _nextDeliveryID{ 1 }
        , _pendingWorkCount{ 0 }
    {
    }

    void PushNotificationDispatcher::initialize( IServiceStore* pStore, const PushDispatcherSettings& settings )
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

    string PushNotificationDispatcher::makeAccountPrefix( AccountID accountID ) { return ServiceKeyUtil::makeHex64( accountID ) + "/"; }

    string PushNotificationDispatcher::makeDeviceKey( AccountID accountID, string_view providerID, string_view token )
    {
        // 토큰은 키 규칙 밖 글자를 가질 수 있다 — 키에는 해시만, 토큰은 값에
        return makeAccountPrefix( accountID ) + string( providerID ) + "." + ServiceKeyUtil::makeHex64( StringUtil::computeHash64( token.data(), token.size(), false ) );
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

    void PushNotificationDispatcher::applyDevices( AccountID accountID, const PushNotificationMessage& message, vector<PushDeviceRegistration>&& listDevice, int64 nowMs )
    {
        --_pendingWorkCount;
        for ( PushDeviceRegistration& device : listDevice )
        {
            if ( findProvider( device._providerID ) == nullptr )
                continue; // 이 서버 빌드에 없는 제공자
            const uint64 deliveryID = _nextDeliveryID++;
            Delivery&    delivery   = _mapDelivery[deliveryID];
            delivery._message       = message;
            delivery._device        = std::move( device );
            delivery._accountID     = accountID;
            delivery._nextAttemptMs = nowMs;
            sendDelivery( deliveryID, delivery );
        }
    }

    void PushNotificationDispatcher::sendDelivery( uint64 deliveryID, Delivery& delivery )
    {
        delivery._bInFlight = SW_TRUE;
        ++_stats._sentCount;
        findProvider( delivery._device._providerID )->send( deliveryID, delivery._device._token, delivery._device._locale, delivery._message );
    }

    void PushNotificationDispatcher::tick( int64 nowMs )
    {
        for ( IPushNotificationProvider* pProvider : _listProvider )
        {
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
                        unregisterDevice( delivery._accountID, delivery._device._providerID, delivery._device._token, 0 ); // 꼬리표 0 — 완료 없음
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
                        SW_LOG_WARNING( "PushDispatcher: provider '%#' rejected a notification for account %#", delivery._device._providerID.c_str(), delivery._accountID );
                        _mapDelivery.erase( deliveryIt );
                        break;
                    }
                }
            }
        }
        for ( auto& [deliveryID, delivery] : _mapDelivery ) // 물러남이 끝난 것을 다시
        {
            if ( delivery._bInFlight == SW_FALSE && nowMs >= delivery._nextAttemptMs )
                sendDelivery( deliveryID, delivery );
        }
    }

    void PushNotificationDispatcher::registerDevice( AccountID accountID, const PushDeviceRegistration& registration, uint64 requestTag )
    {
        const bool bValid = accountID != kInvalidAccountID && PushLimit::isValidProviderID( registration._providerID ) && registration._token.empty() == false &&
                            registration._token.size() <= static_cast<size_t>( PushLimit::kMaxTokenSize ) &&
                            registration._locale.size() <= static_cast<size_t>( PushLimit::kMaxLocaleSize );
        if ( bValid == false || _pStore == nullptr )
        {
            if ( requestTag != 0 )
                _completionBuffer.push( PushDeviceCompletion{ requestTag, bValid ? LiveOpsResult::Unavailable : LiveOpsResult::Invalid } );
            return;
        }
        unique_ptr<PushDeviceWriteWork> work = make_unique<PushDeviceWriteWork>();
        work->_registration                  = registration;
        work->_key                           = makeDeviceKey( accountID, registration._providerID, registration._token );
        work->_pDispatcher                   = this;
        work->_accountID                     = accountID;
        work->_requestTag                    = requestTag;
        ++_pendingWorkCount;
        _pStore->submit( std::move( work ) );
    }

    void PushNotificationDispatcher::unregisterDevice( AccountID accountID, string_view providerID, string_view token, uint64 requestTag )
    {
        if ( PushLimit::isValidProviderID( providerID ) == false || token.empty() || _pStore == nullptr )
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
