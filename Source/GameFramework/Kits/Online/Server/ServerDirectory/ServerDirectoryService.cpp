#include "pch.h"

#include "GameFramework/Kits/Online/Server/ServerDirectory/ServerDirectoryService.h"

#include "Core/Common/HashUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Online/ServerDirectory/ServerDirectoryProtocol.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ServerDirectoryServiceInternal
        {
            static constexpr uint8 kRecordFormat      = 1;
            static constexpr int32 kMaxMaintenanceRow = 64;
            static constexpr int32 kPercentScale      = 100;

            static const hashed_string& getMaintenanceTable()
            {
                static const hashed_string s_table{ "sd_maintenance" };
                return s_table;
            }

            static const hashed_string& getNoticeTable()
            {
                static const hashed_string s_table{ "sd_notice" };
                return s_table;
            }

            static vector<uint8> encodeMaintenance( const MaintenanceWindow& window )
            {
                BitWriter writer;
                writer.writeBits( kRecordFormat, 8 );
                ServerDirectoryProtocol::writeMaintenance( writer, window, true );
                return writer.releaseBytes();
            }

            static vector<uint8> encodeNotice( const ServiceNotice& notice )
            {
                BitWriter writer;
                writer.writeBits( kRecordFormat, 8 );
                ServerDirectoryProtocol::writeNotice( writer, notice );
                return writer.releaseBytes();
            }

            static uint64 combineText( uint64 hash, string_view text ) { return HashUtil::combine( hash, StringUtil::computeHash64( text.data(), text.size(), false ) ); }

            static string makeActor( AccountId actorId ) { return actorId == kInvalidAccountId ? string( "system" ) : "gm." + ServiceKeyUtil::makeHex64( actorId ); }

            /** @brief 공지 정렬 — 우선순위 내림차순, 같으면 시작 시각 내림차순, 같으면 id 오름차순(결정적). */
            struct NoticeOrder
            {
                bool operator()( const ServiceNotice& left, const ServiceNotice& right ) const
                {
                    if ( left._priority != right._priority )
                        return left._priority > right._priority;
                    if ( left._startMs != right._startMs )
                        return left._startMs > right._startMs;
                    return left._noticeId < right._noticeId;
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 점검 · 공지 두 표를 읽는 일입니다. */
    class ServerDirectoryReloadWork final : public IServiceStoreWork
    {
    public:
        ServerDirectoryReloadWork( ServerDirectoryService* pService, int64 nowMs )
            : _listMaintenance{}
            , _listNotice{}
            , _pService{ pService }
            , _nowMs{ nowMs }
            , _bReadOk{ SW_FALSE }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            using Internal = ServerDirectoryServiceInternal;
            vector<ServiceRecord> listRecord;
            if ( connection.listRecords( Internal::getMaintenanceTable(), "", "", Internal::kMaxMaintenanceRow, false, listRecord ) != ServiceStoreResult::Ok )
                return;
            for ( const ServiceRecord& record : listRecord )
            {
                BitReader         reader( record._bytes.data(), static_cast<int32>( record._bytes.size() ) );
                MaintenanceWindow window;
                if ( reader.readBits( 8 ) == Internal::kRecordFormat && ServerDirectoryProtocol::readMaintenance( reader, true, window ) )
                    _listMaintenance.push_back( std::move( window ) );
            }
            listRecord.clear();
            if ( connection.listRecords( Internal::getNoticeTable(), "", "", ServerDirectoryLimit::kMaxNoticeCount * 4, false, listRecord ) != ServiceStoreResult::Ok )
                return;
            for ( const ServiceRecord& record : listRecord )
            {
                BitReader     reader( record._bytes.data(), static_cast<int32>( record._bytes.size() ) );
                ServiceNotice notice;
                if ( reader.readBits( 8 ) == Internal::kRecordFormat && ServerDirectoryProtocol::readNotice( reader, notice ) )
                    _listNotice.push_back( std::move( notice ) );
            }
            _bReadOk = SW_TRUE;
        }

        void complete() override { _pService->applyReload( std::move( _listMaintenance ), std::move( _listNotice ), _bReadOk == SW_TRUE, _nowMs ); }

    private:
        vector<MaintenanceWindow> _listMaintenance;
        vector<ServiceNotice>     _listNotice;
        ServerDirectoryService*   _pService;
        int64                     _nowMs;
        uint8                     _bReadOk;
    };
} // namespace sw

namespace sw
{
    /** @brief 점검 · 공지 바꾸기 하나 — 레코드 쓰기(또는 지우기) + 감사 줄을 한 트랜잭션으로 씁니다(판 조건 — 그새 바뀌었으면 Unavailable, 다시 하면 된다). */
    class ServerDirectoryWriteWork final : public IServiceStoreWork
    {
    public:
        ServiceAuditEntry       _auditEntry;
        vector<uint8>           _bytes; ///< 비면 지우기
        string                  _key;
        hashed_string           _table;
        NetIdempotencyKey       _auditUnique;
        ServerDirectoryService* _pService;
        uint64                  _requestTag;
        ServerDirectoryResult   _result;

        ServerDirectoryWriteWork()
            : _auditEntry{}
            , _bytes{}
            , _key{}
            , _table{}
            , _auditUnique{ NetIdempotencyKey::makeRandom() }
            , _pService{ nullptr }
            , _requestTag{ 0 }
            , _result{ ServerDirectoryResult::Unavailable }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            ServiceRecord            current;
            const ServiceStoreResult readResult = connection.readRecord( _table, _key, current );
            if ( readResult == ServiceStoreResult::Unavailable || readResult == ServiceStoreResult::Invalid )
                return;
            if ( _bytes.empty() && readResult == ServiceStoreResult::NotFound )
            {
                _result = ServerDirectoryResult::NotFound;
                return;
            }
            ServiceTransaction transaction;
            if ( _bytes.empty() )
                transaction.erase( _table, _key, current._version );
            else
                transaction.put( _table, _key, _bytes, current._version );
            ServiceAuditLog::stageEntry( transaction, _auditEntry, _auditUnique._high, _auditUnique._low );
            const ServiceStoreResult commitResult = connection.commit( transaction );
            _result                               = commitResult == ServiceStoreResult::Ok ? ServerDirectoryResult::Ok : ServerDirectoryResult::Unavailable; // Conflict 도 "다시 하라"
        }

        void complete() override { _pService->applyWrite( _requestTag, _result ); }
    };
} // namespace sw

namespace sw
{
    ServerDirectoryService::ServerDirectoryService()
        : _listReader{}
        , _listMaintenance{}
        , _listNotice{}
        , _completionBuffer{}
        , _settings{}
        , _dependencies{}
        , _lastReloadMs{ 0 }
        , _lastVisibleHash{ 0 }
        , _statusRevision{ 0 }
        , _pendingCount{ 0 }
        , _bReloading{ SW_FALSE }
        , _bReloadRequested{ SW_TRUE }
        , _bStatusChanged{ SW_FALSE }
    {
    }

    ServerDirectoryService::~ServerDirectoryService() { shutdown(); }

    void ServerDirectoryService::initialize( const ServerDirectoryDependencies& dependencies, const ServerDirectorySettings& settings )
    {
        SW_ASSERT( dependencies._pStore != nullptr && dependencies._pRouter != nullptr );
        _dependencies = dependencies;
        _settings     = settings;
        for ( const string& kind : settings._listServerKind )
        {
            unique_ptr<ServerRegistryReader>& reader = _listReader.emplace_back( make_unique<ServerRegistryReader>() );
            reader->initialize( dependencies._pRouter, kind, settings._registryRefreshPeriodMs );
        }
        _bReloadRequested = SW_TRUE;
        _lastVisibleHash  = computeVisibleHash( 0 ); // 빈 상태는 알리지 않는다 — 처음 읽은 내용이 있을 때부터
    }

    void ServerDirectoryService::shutdown()
    {
        for ( unique_ptr<ServerRegistryReader>& reader : _listReader )
        {
            reader->shutdown();
        }
        _listReader.clear();
        _dependencies._pRouter = nullptr;
        _dependencies._pBus    = nullptr;
    }

    void ServerDirectoryService::tick( int64 nowMs )
    {
        if ( _dependencies._pStore == nullptr || _dependencies._pRouter == nullptr )
            return;
        for ( unique_ptr<ServerRegistryReader>& reader : _listReader )
        {
            reader->tick( nowMs );
        }
        const bool bPeriodic = nowMs - _lastReloadMs >= _settings._reloadPeriodMs;
        if ( _bReloading == SW_FALSE && ( _bReloadRequested == SW_TRUE || bPeriodic ) )
            startReload( nowMs );
        markStatusIfChanged( nowMs ); // 기간 경계(공지 시작 · 끝, 점검 시작 · 끝)는 다시 읽지 않아도 보이는 것이 바뀐다
    }

    void ServerDirectoryService::startReload( int64 nowMs )
    {
        _bReloadRequested = SW_FALSE;
        _bReloading       = SW_TRUE;
        _lastReloadMs     = nowMs;
        ++_pendingCount;
        _dependencies._pStore->submit( make_unique<ServerDirectoryReloadWork>( this, nowMs ) );
    }

    void ServerDirectoryService::applyReload( vector<MaintenanceWindow>&& listMaintenance, vector<ServiceNotice>&& listNotice, bool bReadOk, int64 nowMs )
    {
        --_pendingCount;
        _bReloading = SW_FALSE;
        if ( bReadOk == false )
            return; // 저장소가 아프다 — 옛 것을 두고 다음 주기에
        _listMaintenance = std::move( listMaintenance );
        _listNotice      = std::move( listNotice );
        std::sort( _listNotice.begin(), _listNotice.end(), ServerDirectoryServiceInternal::NoticeOrder{} );
        markStatusIfChanged( nowMs );
    }

    void ServerDirectoryService::notifyChanged() { _bReloadRequested = SW_TRUE; }

    bool ServerDirectoryService::takeStatusChange()
    {
        const bool bChanged = _bStatusChanged == SW_TRUE;
        _bStatusChanged     = SW_FALSE;
        return bChanged;
    }

    uint64 ServerDirectoryService::computeVisibleHash( int64 nowMs ) const
    {
        using Internal = ServerDirectoryServiceInternal;
        uint64 hash    = HashUtil::kFnvOffset64;
        for ( const MaintenanceWindow& window : _listMaintenance )
        {
            if ( window.isActive( nowMs ) == false )
                continue;
            hash = Internal::combineText( hash, window._scope );
            hash = Internal::combineText( hash, window._messageKey );
            hash = HashUtil::combine( hash, static_cast<uint64>( window._endMs ) );
        }
        for ( const ServiceNotice& notice : _listNotice )
        {
            if ( notice.isActive( nowMs ) == false )
                continue;
            hash = HashUtil::combine( hash, notice._noticeId );
            hash = Internal::combineText( hash, notice._text );
            hash = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( notice._priority ) ) );
        }
        return hash;
    }

    void ServerDirectoryService::markStatusIfChanged( int64 nowMs )
    {
        const uint64 hash = computeVisibleHash( nowMs );
        if ( hash == _lastVisibleHash )
            return;
        _lastVisibleHash = hash;
        _bStatusChanged  = SW_TRUE; // 바인딩이 takeStatusChange 로 보고 모두에게 알린다
        ++_statusRevision;
    }

    ServerDirectoryStatus ServerDirectoryService::makeStatus( int64 nowMs ) const
    {
        ServerDirectoryStatus status;
        for ( const MaintenanceWindow& window : _listMaintenance )
        {
            if ( window.isActive( nowMs ) == false || static_cast<int32>( status._listMaintenance.size() ) >= ServerDirectoryLimit::kMaxWindowCount )
                continue;
            MaintenanceWindow& visible = status._listMaintenance.emplace_back( window );
            visible._listAllowedAccount.clear(); // 허용 목록은 보내지 않는다
        }
        for ( const ServiceNotice& notice : _listNotice )
        {
            if ( notice.isActive( nowMs ) && static_cast<int32>( status._listNotice.size() ) < ServerDirectoryLimit::kMaxNoticeCount )
                status._listNotice.push_back( notice );
        }
        return status;
    }

    bool ServerDirectoryService::isBlockedByMaintenance( string_view kind, AccountId accountId, int64 nowMs, MaintenanceWindow* pOutWindow ) const
    {
        for ( const MaintenanceWindow& window : _listMaintenance )
        {
            const bool bBlocks = window.isActive( nowMs ) && window.appliesTo( kind ) && window.allowsAccount( accountId ) == false;
            if ( bBlocks == false )
                continue;
            if ( pOutWindow != nullptr )
                *pOutWindow = window;
            return true;
        }
        return false;
    }

    ServerAssignment ServerDirectoryService::assignServer( AccountId accountId, const ServerAssignmentRequest& request, int64 nowMs )
    {
        ServerAssignment      assignment;
        ServerRegistryReader* pReader = findReader( request._kind );
        if ( pReader == nullptr || ServerRecord::isValidName( request._region ) == false )
        {
            assignment._result = ServerDirectoryResult::Invalid;
            return assignment;
        }
        MaintenanceWindow window;
        if ( isBlockedByMaintenance( request._kind, accountId, nowMs, &window ) )
        {
            assignment._result           = ServerDirectoryResult::Maintenance;
            assignment._maintenanceEndMs = window._endMs;
            assignment._messageKey       = window._messageKey;
            return assignment;
        }
        // 막히지 않았는데 이 종류에 점검이 걸려 있다 — 허용 계정이다. 점검 상태 서버도 후보.
        bool bAllowedDuringMaintenance = false;
        for ( const MaintenanceWindow& active : _listMaintenance )
        {
            bAllowedDuringMaintenance = bAllowedDuringMaintenance || ( active.isActive( nowMs ) && active.appliesTo( request._kind ) );
        }

        ServerSelectionQuery query;
        query._kind                = request._kind;
        query._region              = request._region;
        query._buildVersion        = request._buildVersion;
        query._seatCount           = request._seatCount;
        query._staleMs             = _settings._staleMs;
        query._bIncludeMaintenance = bAllowedDuringMaintenance ? SW_TRUE : SW_FALSE;
        ServerStatus picked;
        if ( pReader->pickServer( query, nowMs, picked ) == false )
        {
            assignment._result = ServerDirectoryResult::NoServer;
            return assignment;
        }
        assignment._result   = ServerDirectoryResult::Ok;
        assignment._serverId = picked._descriptor._serverId;
        assignment._address  = picked._descriptor._address;
        assignment._port     = picked._descriptor._port;
        return assignment;
    }

    ServerDirectoryResult ServerDirectoryService::listServers( string_view kind, int64 nowMs, vector<ServerListEntry>& outListEntry ) const
    {
        const ServerRegistryReader* pReader = findReader( kind );
        if ( pReader == nullptr )
            return ServerDirectoryResult::Invalid;
        for ( const ServerStatus& status : pReader->getSnapshot() )
        {
            const bool bStale = nowMs - status._heartbeatMs > _settings._staleMs;
            if ( bStale || status._state == ServerState::Starting || static_cast<int32>( outListEntry.size() ) >= ServerDirectoryLimit::kMaxServerListCount )
                continue;
            ServerListEntry& entry = outListEntry.emplace_back();
            entry._serverId        = status._descriptor._serverId;
            entry._region          = status._descriptor._region;
            entry._address         = status._descriptor._address;
            entry._port            = status._descriptor._port;
            entry._state           = status._state;
            const int32 capacity   = std::max( 1, status._descriptor._capacity );
            entry._fillPercent     = static_cast<uint8>( std::min( ServerDirectoryServiceInternal::kPercentScale,
                                                                   status._load * ServerDirectoryServiceInternal::kPercentScale / capacity ) );
        }
        return ServerDirectoryResult::Ok;
    }

    ServerRegistryReader* ServerDirectoryService::findReader( string_view kind ) const
    {
        for ( const unique_ptr<ServerRegistryReader>& reader : _listReader )
        {
            if ( reader->getKind() == kind )
                return reader.get();
        }
        return nullptr;
    }

    void ServerDirectoryService::setMaintenance( const MaintenanceWindow& window, AccountId actorId, string_view memo, int64 nowMs, uint64 requestTag )
    {
        using Internal      = ServerDirectoryServiceInternal;
        const bool bScopeOk = window._scope == MaintenanceWindow::kScopeAll || ServerRecord::isValidName( window._scope );
        const bool bSizesOk = static_cast<int32>( window._listAllowedAccount.size() ) <= ServerDirectoryLimit::kMaxAllowedAccountCount &&
                              static_cast<int32>( window._messageKey.size() ) <= ServerDirectoryLimit::kMaxMessageKeySize;
        const bool bWindowOk = window._endMs == 0 || window._startMs < window._endMs;
        if ( bScopeOk == false || bSizesOk == false || bWindowOk == false )
        {
            _completionBuffer.push( ServerDirectoryCompletion{ requestTag, ServerDirectoryResult::Invalid } );
            return;
        }
        unique_ptr<ServerDirectoryWriteWork> work = make_unique<ServerDirectoryWriteWork>();
        work->_table                              = Internal::getMaintenanceTable();
        work->_key                                = window._scope;
        work->_bytes                              = Internal::encodeMaintenance( window );
        work->_auditEntry._actor                  = Internal::makeActor( actorId );
        work->_auditEntry._action                 = "sd.maintenance.set";
        work->_auditEntry._subject                = "sd/" + window._scope;
        work->_auditEntry._memo                   = string( memo );
        work->_auditEntry._timeMs                 = nowMs;
        work->_requestTag                         = requestTag;
        submitWrite( std::move( work ) );
    }

    void ServerDirectoryService::clearMaintenance( string_view scope, AccountId actorId, string_view memo, int64 nowMs, uint64 requestTag )
    {
        using Internal = ServerDirectoryServiceInternal;
        if ( scope != MaintenanceWindow::kScopeAll && ServerRecord::isValidName( scope ) == false )
        {
            _completionBuffer.push( ServerDirectoryCompletion{ requestTag, ServerDirectoryResult::Invalid } );
            return;
        }
        unique_ptr<ServerDirectoryWriteWork> work = make_unique<ServerDirectoryWriteWork>();
        work->_table                              = Internal::getMaintenanceTable();
        work->_key                                = string( scope );
        work->_auditEntry._actor                  = Internal::makeActor( actorId );
        work->_auditEntry._action                 = "sd.maintenance.clear";
        work->_auditEntry._subject                = "sd/" + string( scope );
        work->_auditEntry._memo                   = string( memo );
        work->_auditEntry._timeMs                 = nowMs;
        work->_requestTag                         = requestTag;
        submitWrite( std::move( work ) );
    }

    void ServerDirectoryService::postNotice( const ServiceNotice& notice, AccountId actorId, int64 nowMs, uint64 requestTag )
    {
        using Internal       = ServerDirectoryServiceInternal;
        const bool bTextOk   = notice._text.empty() == false && static_cast<int32>( notice._text.size() ) <= ServerDirectoryLimit::kMaxNoticeTextSize;
        const bool bWindowOk = notice._endMs == 0 || notice._startMs < notice._endMs;
        if ( notice._noticeId == 0 || bTextOk == false || bWindowOk == false )
        {
            _completionBuffer.push( ServerDirectoryCompletion{ requestTag, ServerDirectoryResult::Invalid } );
            return;
        }
        unique_ptr<ServerDirectoryWriteWork> work = make_unique<ServerDirectoryWriteWork>();
        work->_table                              = Internal::getNoticeTable();
        work->_key                                = ServiceKeyUtil::makeHex64( notice._noticeId );
        work->_bytes                              = Internal::encodeNotice( notice );
        work->_auditEntry._actor                  = Internal::makeActor( actorId );
        work->_auditEntry._action                 = "sd.notice.post";
        work->_auditEntry._subject                = "sd/notice/" + work->_key;
        work->_auditEntry._after                  = notice._text;
        work->_auditEntry._timeMs                 = nowMs;
        work->_requestTag                         = requestTag;
        submitWrite( std::move( work ) );
    }

    void ServerDirectoryService::removeNotice( uint64 noticeId, AccountId actorId, int64 nowMs, uint64 requestTag )
    {
        using Internal                            = ServerDirectoryServiceInternal;
        unique_ptr<ServerDirectoryWriteWork> work = make_unique<ServerDirectoryWriteWork>();
        work->_table                              = Internal::getNoticeTable();
        work->_key                                = ServiceKeyUtil::makeHex64( noticeId );
        work->_auditEntry._actor                  = Internal::makeActor( actorId );
        work->_auditEntry._action                 = "sd.notice.remove";
        work->_auditEntry._subject                = "sd/notice/" + work->_key;
        work->_auditEntry._timeMs                 = nowMs;
        work->_requestTag                         = requestTag;
        submitWrite( std::move( work ) );
    }

    void ServerDirectoryService::submitWrite( unique_ptr<ServerDirectoryWriteWork> work )
    {
        work->_pService = this;
        ++_pendingCount;
        _dependencies._pStore->submit( std::move( work ) );
    }

    void ServerDirectoryService::applyWrite( uint64 requestTag, ServerDirectoryResult result )
    {
        --_pendingCount;
        _completionBuffer.push( ServerDirectoryCompletion{ requestTag, result } );
        if ( result != ServerDirectoryResult::Ok )
            return;
        _bReloadRequested = SW_TRUE; // 이 서버는 다음 tick 에 다시 읽고 알린다
        if ( _dependencies._pBus != nullptr )
            _dependencies._pBus->publish( ServerDirectoryBus::kChangedTopic, nullptr, 0 );
    }
} // namespace sw
