#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ServerDirectoryServiceInternal
        {
            static constexpr int32 kMaxMaintenanceRow     = 64;   ///< 점검 표를 한 번에 읽는 쪽 크기이다
            static constexpr int32 kMaxMaintenanceRowRead = 1024; ///< 점검 표를 다 읽는 상한이다. 넘으면 이전 상태를 둔다(뒤의 점검을 조용히 버리면 그 범위가 열린다)
            static constexpr int32 kPercentScale          = 100;

            static const ServiceRecordTable<MaintenanceWindow>& getMaintenanceTable()
            {
                static const ServiceRecordTable<MaintenanceWindow> s_table{ "sd_maintenance" };
                return s_table;
            }

            static const ServiceRecordTable<ServiceAnnouncement>& getAnnouncementTable()
            {
                static const ServiceRecordTable<ServiceAnnouncement> s_table{ "sd_notice" };
                return s_table;
            }

            /** @brief 공지의 끝 시각을 읽습니다. 끝이 없는 공지(0)는 false 라 지우지 않는다. */
            [[nodiscard]] static bool readAnnouncementEndMs( const ServiceRecord& record, int64& outTimeMs )
            {
                ServiceAnnouncement announcement;
                if ( ServerDirectoryServiceInternal::getAnnouncementTable().decode( record, announcement ) == false || announcement._endMs == 0 )
                    return false;
                outTimeMs = announcement._endMs;
                return true;
            }

            /** @brief 점검 창의 끝 시각을 읽습니다. 끝이 없는 점검(0)은 false 라 지우지 않는다. */
            [[nodiscard]] static bool readMaintenanceEndMs( const ServiceRecord& record, int64& outTimeMs )
            {
                MaintenanceWindow window;
                if ( ServerDirectoryServiceInternal::getMaintenanceTable().decode( record, window ) == false || window._endMs == 0 )
                    return false;
                outTimeMs = window._endMs;
                return true;
            }

            /** @brief 점검 창 규칙입니다. 운영 쓰기와 저장소에서 읽은 레코드(A 는 상한을 보지 않는다)가 같은 규칙을 지납니다. */
            static bool isValidWindow( const MaintenanceWindow& window )
            {
                const bool bScopeOk = window._scope == MaintenanceWindow::kScopeAll || ServerRecord::isValidName( window._scope );
                const bool bSizesOk = static_cast<int32>( window._listAllowedAccount.size() ) <= ServerDirectoryLimit::kMaxAllowedAccountCount &&
                                      static_cast<int32>( window._messageKey.size() ) <= ServerDirectoryLimit::kMaxMessageKeySize;
                const bool bWindowOk = window._endMs == 0 || window._startMs < window._endMs;
                return bScopeOk && bSizesOk && bWindowOk;
            }

            /** @brief 공지 규칙입니다. 운영 쓰기와 저장소에서 읽은 레코드가 같은 규칙을 지납니다. */
            static bool isValidAnnouncement( const ServiceAnnouncement& announcement )
            {
                const bool bTextOk   = announcement._text.empty() == false && static_cast<int32>( announcement._text.size() ) <= ServerDirectoryLimit::kMaxAnnouncementTextSize;
                const bool bWindowOk = announcement._endMs == 0 || announcement._startMs < announcement._endMs;
                return announcement._announcementID != 0 && bTextOk && bWindowOk;
            }

            static uint64 combineText( uint64 hash, string_view text ) { return HashUtil::combine( hash, HashUtil::computeStringHash64( text.data(), text.size(), false ) ); }

            /** @brief 공지 정렬 기준입니다. 우선순위 내림차순, 같으면 시작 시각 내림차순, 그래도 같으면 id 오름차순이며 결과가 결정적입니다. */
            struct AnnouncementOrder
            {
                bool operator()( const ServiceAnnouncement& left, const ServiceAnnouncement& right ) const
                {
                    if ( left._priority != right._priority )
                        return left._priority > right._priority;
                    if ( left._startMs != right._startMs )
                        return left._startMs > right._startMs;
                    return left._announcementID < right._announcementID;
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 점검과 공지 두 테이블을 읽은 값입니다. */
    struct ServerDirectoryTables
    {
        vector<MaintenanceWindow>   _listMaintenance{};
        vector<ServiceAnnouncement> _listAnnouncement{};
    };

    /** @brief 점검과 공지 두 테이블을 읽습니다. 손상된 영속 레코드는 건너뛰지 않는다 — 이 읽기를 실패로 두어 이전 값을 지키고 오류 로그를 남긴다. */
    struct ServerDirectoryStoreRun
    {
        static ServiceStoreRead<ServerDirectoryTables> readTables( IServiceStoreConnection& connection )
        {
            using Internal = ServerDirectoryServiceInternal;
            ServiceStoreRead<ServerDirectoryTables> read;
            vector<ServiceRecord>                   listRecord;
            // 점검은 막는 규칙이라 다 읽는다 — 첫 쪽만 읽으면 그 뒤 범위의 점검이 조용히 빠져 열렸다(감사 O-18 의 fail-open)
            const ServiceRecordRead maintenance =
                ServiceRecordReader::readAll( connection, Internal::getMaintenanceTable(), "", Internal::kMaxMaintenanceRow, Internal::kMaxMaintenanceRowRead, false, listRecord );
            read._result = maintenance._result;
            if ( read._result != ServiceStoreResult::Ok )
                return read;
            if ( maintenance._bTruncated == SW_TRUE )
            {
                SW_LOG_ERROR_ID( "ServerDirectoryService.ServerDirectoryMaintenanceTooMany", "Server directory has more than %# maintenance records - kept the previous state", Internal::kMaxMaintenanceRowRead );
                read._result = ServiceStoreResult::Invalid;
                return read;
            }
            for ( const ServiceRecord& record : listRecord )
            {
                MaintenanceWindow window;
                const bool        bDecoded = ServerDirectoryServiceInternal::getMaintenanceTable().decode( record, window );
                if ( bDecoded && Internal::isValidWindow( window ) == false )
                    SW_LOG_ERROR_ID( "ServerDirectoryService.ServerDirectoryMaintenanceRecord", "Server directory maintenance record '%#' breaks the write rules", record._key );
                if ( bDecoded == false || Internal::isValidWindow( window ) == false )
                {
                    read._result = ServiceStoreResult::Invalid;
                    return read;
                }
                read._value._listMaintenance.push_back( std::move( window ) );
            }
            listRecord.clear();
            const ServiceRecordRead announcementRead = ServiceRecordReader::readAll( connection, Internal::getAnnouncementTable(), "", ServerDirectoryLimit::kMaxAnnouncementCount,
                                                                                     ServerDirectoryLimit::kMaxAnnouncementCount * 4, false, listRecord );
            read._result                             = announcementRead._result;
            if ( read._result != ServiceStoreResult::Ok )
                return read;
            if ( announcementRead._bTruncated == SW_TRUE )
            {
                // 지난 공지가 쌓여 상한에 닿으면 새 공지가 조용히 빠졌다 — 이전 값을 두고 알린다(만료 정리가 지난 것을 지운다)
                SW_LOG_ERROR_ID( "ServerDirectoryService.ServerDirectoryAnnouncementTooMany", "Server directory has more than %# notice records - kept the previous state",
                                 ServerDirectoryLimit::kMaxAnnouncementCount * 4 );
                read._result = ServiceStoreResult::Invalid;
                return read;
            }
            for ( const ServiceRecord& record : listRecord )
            {
                ServiceAnnouncement announcement;
                const bool          bDecoded = ServerDirectoryServiceInternal::getAnnouncementTable().decode( record, announcement );
                if ( bDecoded && Internal::isValidAnnouncement( announcement ) == false )
                    SW_LOG_ERROR_ID( "ServerDirectoryService.ServerDirectoryAnnouncementRecord", "Server directory notice record '%#' breaks the write rules", record._key );
                if ( bDecoded == false || Internal::isValidAnnouncement( announcement ) == false )
                {
                    read._result = ServiceStoreResult::Invalid;
                    return read;
                }
                read._value._listAnnouncement.push_back( std::move( announcement ) );
            }
            return read;
        }
    };
} // namespace sw

namespace sw
{
    ServerDirectoryService::ServerDirectoryService()
        : _listReader{}
        , _listMaintenance{}
        , _listAnnouncement{}
        , _completionBuffer{}
        , _settings{}
        , _dependencies{}
        , _lastVisibleHash{ 0 }
        , _statusRevision{ 0 }
        , _reloadJob{}
        , _store{}
        , _bStatusChanged{ SW_FALSE }
    {
    }

    ServerDirectoryService::~ServerDirectoryService()
    {
        shutdown();
    }

    void ServerDirectoryService::initialize( const ServerDirectoryDependencies& dependencies, const ServerDirectoryServiceConfig& settings )
    {
        SW_ASSERT( dependencies._pStore != nullptr && dependencies._pRouter != nullptr );
        _dependencies = dependencies;
        _settings     = settings;
        for ( const string& kind : settings._listServerKind )
        {
            unique_ptr<ServerRegistryReader>& reader = _listReader.emplace_back( make_unique<ServerRegistryReader>() );
            reader->initialize( dependencies._pRouter, kind, settings._registryRefreshPeriodMs, settings._seatReservationTTLMs );
        }
        _store.bindStore( dependencies._pStore );
        _reloadJob.setPeriod( settings._reloadPeriodMs );               // 첫 틱에 한 번 읽는다
        _lastVisibleHash = computeVisibleHash( UnixMilliseconds{ 0 } ); // 빈 상태는 알리지 않는다. 처음 읽은 내용이 있을 때부터 알린다
    }

    void ServerDirectoryService::shutdown()
    {
        for ( unique_ptr<ServerRegistryReader>& reader : _listReader )
        {
            reader->shutdown();
        }
        _listReader.clear();
        _store.bindStore( nullptr ); // 남은 일은 조립이 저장소 드레인으로 거둔다(예전과 같이 여기서 기다리지 않는다)
        _dependencies._pRouter = nullptr;
        _dependencies._pBus    = nullptr;
    }

    void ServerDirectoryService::update( UnixMilliseconds nowMs )
    {
        if ( _store.hasStore() == false || _dependencies._pRouter == nullptr )
            return;
        for ( unique_ptr<ServerRegistryReader>& reader : _listReader )
        {
            reader->update( nowMs );
        }
        if ( _reloadJob.tryStart( nowMs ) )
            startReload( nowMs );
        markStatusIfChanged( nowMs ); // 기간 경계(공지 시작과 끝, 점검 시작과 끝)는 다시 읽지 않아도 보이는 내용이 바뀐다
    }

    void ServerDirectoryService::startReload( UnixMilliseconds nowMs )
    {
        _store.submit( []( IServiceStoreConnection& connection )
        { return ServerDirectoryStoreRun::readTables( connection ); },
                       [this, nowMs]( ServiceStoreRead<ServerDirectoryTables>& read )
        { applyReload( std::move( read._value._listMaintenance ), std::move( read._value._listAnnouncement ), read.isOk(), nowMs ); } );
    }

    void ServerDirectoryService::applyReload( vector<MaintenanceWindow>&& listMaintenance, vector<ServiceAnnouncement>&& listAnnouncement, bool bReadOk, UnixMilliseconds nowMs )
    {
        _reloadJob.finish();
        if ( bReadOk == false )
            return; // 저장소에 문제가 있으므로 이전 값을 두고 다음 주기에 다시 읽는다
        _listMaintenance  = std::move( listMaintenance );
        _listAnnouncement = std::move( listAnnouncement );
        VectorUtil::sort( _listAnnouncement, ServerDirectoryServiceInternal::AnnouncementOrder{} );
        markStatusIfChanged( nowMs );
    }

    void ServerDirectoryService::notifyChanged()
    {
        _reloadJob.request();
    }

    bool ServerDirectoryService::takeStatusChange()
    {
        const bool bChanged = _bStatusChanged == SW_TRUE;
        _bStatusChanged     = SW_FALSE;
        return bChanged;
    }

    uint64 ServerDirectoryService::computeVisibleHash( UnixMilliseconds nowMs ) const
    {
        using Internal = ServerDirectoryServiceInternal;
        uint64 hash    = HashUtil::kFNVOffset64;
        for ( const MaintenanceWindow& window : _listMaintenance )
        {
            if ( window.isActive( nowMs.count() ) == false )
                continue;
            hash = Internal::combineText( hash, window._scope );
            hash = Internal::combineText( hash, window._messageKey );
            hash = HashUtil::combine( hash, static_cast<uint64>( window._startMs ) );
            hash = HashUtil::combine( hash, static_cast<uint64>( window._endMs ) );
        }
        for ( const ServiceAnnouncement& announcement : _listAnnouncement )
        {
            if ( announcement.isActive( nowMs.count() ) == false )
                continue;
            hash = HashUtil::combine( hash, announcement._announcementID );
            hash = Internal::combineText( hash, announcement._text );
            hash = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( announcement._priority ) ) );
            hash = HashUtil::combine( hash, static_cast<uint64>( announcement._startMs ) );
            hash = HashUtil::combine( hash, static_cast<uint64>( announcement._endMs ) );
            hash = HashUtil::combine( hash, announcement._bLiteralText );
        }
        return hash;
    }

    void ServerDirectoryService::markStatusIfChanged( UnixMilliseconds nowMs )
    {
        const uint64 hash = computeVisibleHash( nowMs );
        if ( hash == _lastVisibleHash )
            return;
        _lastVisibleHash = hash;
        _bStatusChanged  = SW_TRUE; // 바인딩이 takeStatusChange 로 확인해 모든 클라이언트에 알린다
        ++_statusRevision;
    }

    ServerDirectoryStatus ServerDirectoryService::makeStatus( UnixMilliseconds nowMs ) const
    {
        ServerDirectoryStatus status;
        for ( const MaintenanceWindow& window : _listMaintenance )
        {
            if ( window.isActive( nowMs.count() ) == false || static_cast<int32>( status._listMaintenance.size() ) >= ServerDirectoryLimit::kMaxWindowCount )
                continue;
            status._listMaintenance.push_back( window.makeView() ); // 보이는 타입에는 허용 목록이 없다
        }
        for ( const ServiceAnnouncement& announcement : _listAnnouncement )
        {
            if ( announcement.isActive( nowMs.count() ) && static_cast<int32>( status._listAnnouncement.size() ) < ServerDirectoryLimit::kMaxAnnouncementCount )
                status._listAnnouncement.push_back( announcement );
        }
        return status;
    }

    bool ServerDirectoryService::isBlockedByMaintenance( string_view kind, AccountID accountID, UnixMilliseconds nowMs, MaintenanceWindow* pOutWindow ) const
    {
        for ( const MaintenanceWindow& window : _listMaintenance )
        {
            const bool bBlocks = window.isActive( nowMs.count() ) && window.isApplicableTo( kind ) && window.allowsAccount( accountID ) == false;
            if ( bBlocks == false )
                continue;
            if ( pOutWindow != nullptr )
                *pOutWindow = window;
            return true;
        }
        return false;
    }

    ServerDirectoryServiceResult ServerDirectoryService::assignServer( AccountID accountID, const ServerAssignmentRequest& request, UnixMilliseconds nowMs, ServerAssignment& outAssignment )
    {
        outAssignment = ServerAssignment{};
        if ( findReader( request._kind ) == nullptr || ServerRecord::isValidName( request._region ) == false )
            return ServerDirectoryServiceResult::fail( ServerDirectoryError::kUnknownKind );
        outAssignment = pickAssignment( accountID, request, nowMs );
        return outAssignment._result;
    }

    ServerAssignment ServerDirectoryService::pickAssignment( AccountID accountID, const ServerAssignmentRequest& request, UnixMilliseconds nowMs )
    {
        ServerAssignment      assignment;
        ServerRegistryReader* pReader = findReader( request._kind );
        MaintenanceWindow     window;
        if ( isBlockedByMaintenance( request._kind, accountID, nowMs, &window ) )
        {
            assignment._result           = ServerDirectoryResult::Maintenance;
            assignment._maintenanceEndMs = window._endMs;
            assignment._messageKey       = window._messageKey;
            return assignment;
        }
        // 막히지 않았는데 이 종류에 점검이 걸려 있다면 허용 계정이다. 이때는 점검 상태의 서버도 후보가 된다.
        bool bAllowedDuringMaintenance = false;
        for ( const MaintenanceWindow& active : _listMaintenance )
        {
            bAllowedDuringMaintenance = bAllowedDuringMaintenance || ( active.isActive( nowMs.count() ) && active.isApplicableTo( request._kind ) );
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
        assignment._serverID = picked._descriptor._serverID;
        assignment._address  = picked._descriptor._address;
        assignment._port     = picked._descriptor.pickPort( accountID ); // 포트 샤딩 — 계정마다 정해진 샤드의 포트
        return assignment;
    }

    ServerDirectoryServiceResult ServerDirectoryService::listServers( string_view kind, UnixMilliseconds nowMs, vector<ServerListEntry>& outListEntry ) const
    {
        const ServerRegistryReader* pReader = findReader( kind );
        if ( pReader == nullptr )
            return ServerDirectoryServiceResult::fail( ServerDirectoryError::kUnknownKind );
        for ( const ServerStatus& status : pReader->getSnapshot() )
        {
            const bool bStale      = nowMs.count() - status._heartbeatMs > _settings._staleMs;
            const bool bSkipServer = bStale || status._state == ServerState::Starting || static_cast<int32>( outListEntry.size() ) >= ServerDirectoryLimit::kMaxServerListCount;
            if ( bSkipServer )
                continue;
            ServerListEntry& entry = outListEntry.emplace_back();
            entry._serverID        = status._descriptor._serverID;
            entry._region          = status._descriptor._region;
            entry._address         = status._descriptor._address;
            entry._port            = status._descriptor._port;
            entry._state           = status._state;
            const int32 capacity   = MathUtil::max( 1, status._descriptor._capacity );
            entry._fillPercent     = static_cast<uint8>( MathUtil::min( ServerDirectoryServiceInternal::kPercentScale,
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

    void ServerDirectoryService::setMaintenance( const MaintenanceWindow& window, const ServiceOperator& actor, string_view memo, UnixMilliseconds nowMs, uint64 requestTag )
    {
        using Internal = ServerDirectoryServiceInternal;
        if ( Internal::isValidWindow( window ) == false )
        {
            _completionBuffer.push( ServerDirectoryCompletion{ requestTag, ServerDirectoryServiceResult::fail( OnlineError::kInvalidRequest ) } );
            return;
        }
        ServiceAuditedWrite pendingJob;
        pendingJob._change.put( Internal::getMaintenanceTable(), window._scope, window );
        if ( pendingJob._change.isWellFormed() == false )
        {
            _completionBuffer.push( ServerDirectoryCompletion{ requestTag, ServerDirectoryServiceResult::fail( OnlineError::kInvalidRequest ) } ); // 표가 오류 로그를 남겼다
            return;
        }
        StringBuilder<constant::buffer::kMax128> subject;
        subject.appendFormat( "sd/%#", window._scope );
        pendingJob.setOperatorAudit( actor, "sd.maintenance.set", subject.view(), nowMs, memo );
        submitWrite( std::move( pendingJob ), requestTag );
    }

    void ServerDirectoryService::clearMaintenance( string_view scope, const ServiceOperator& actor, string_view memo, UnixMilliseconds nowMs, uint64 requestTag )
    {
        using Internal = ServerDirectoryServiceInternal;
        if ( scope != MaintenanceWindow::kScopeAll && ServerRecord::isValidName( scope ) == false )
        {
            _completionBuffer.push( ServerDirectoryCompletion{ requestTag, ServerDirectoryServiceResult::fail( OnlineError::kInvalidRequest ) } );
            return;
        }
        ServiceAuditedWrite pendingJob;
        pendingJob._change.erase( Internal::getMaintenanceTable(), scope );
        StringBuilder<constant::buffer::kMax128> subject;
        subject.appendFormat( "sd/%#", scope );
        pendingJob.setOperatorAudit( actor, "sd.maintenance.clear", subject.view(), nowMs, memo );
        submitWrite( std::move( pendingJob ), requestTag );
    }

    void ServerDirectoryService::postAnnouncement( const ServiceAnnouncement& announcement, const ServiceOperator& actor, UnixMilliseconds nowMs, uint64 requestTag )
    {
        using Internal = ServerDirectoryServiceInternal;
        if ( Internal::isValidAnnouncement( announcement ) == false )
        {
            _completionBuffer.push( ServerDirectoryCompletion{ requestTag, ServerDirectoryServiceResult::fail( OnlineError::kInvalidRequest ) } );
            return;
        }
        const string        announcementKey = ServiceKeyUtil::makeHex64( announcement._announcementID );
        ServiceAuditedWrite pendingJob;
        pendingJob._change.put( Internal::getAnnouncementTable(), announcementKey, announcement );
        if ( pendingJob._change.isWellFormed() == false )
        {
            _completionBuffer.push( ServerDirectoryCompletion{ requestTag, ServerDirectoryServiceResult::fail( OnlineError::kInvalidRequest ) } ); // 표가 오류 로그를 남겼다
            return;
        }
        StringBuilder<constant::buffer::kMax128> subject;
        subject.appendFormat( "sd/notice/%#", announcementKey );
        pendingJob.setOperatorAudit( actor, "sd.notice.post", subject.view(), nowMs );
        pendingJob._auditEntry._after = announcement._text;
        submitWrite( std::move( pendingJob ), requestTag );
    }

    ServiceTTLRule ServerDirectoryService::makeAnnouncementTTLRule( int64 expireAfterMs )
    {
        // 표가 작아 색인 없이 커서로 훑는다(liveops_push 와 같다). 읽기가 앞 128 줄만 보므로, 끝난 공지가 쌓이면 새 공지가 목록에서 빠졌다
        ServiceTTLRule rule;
        rule._table         = ServerDirectoryServiceInternal::getAnnouncementTable();
        rule._expireAfterMs = expireAfterMs;
        rule._pfnReadTime   = &ServerDirectoryServiceInternal::readAnnouncementEndMs;
        return rule;
    }

    ServiceTTLRule ServerDirectoryService::makeMaintenanceTTLRule( int64 expireAfterMs )
    {
        ServiceTTLRule rule; // 읽기가 앞 kMaxMaintenanceRow 줄만 본다 - 끝난 점검이 쌓이면 새 점검이 빠진다
        rule._table         = ServerDirectoryServiceInternal::getMaintenanceTable();
        rule._expireAfterMs = expireAfterMs;
        rule._pfnReadTime   = &ServerDirectoryServiceInternal::readMaintenanceEndMs;
        return rule;
    }

    void ServerDirectoryService::removeAnnouncement( uint64 announcementID, const ServiceOperator& actor, UnixMilliseconds nowMs, uint64 requestTag )
    {
        using Internal                      = ServerDirectoryServiceInternal;
        const string        announcementKey = ServiceKeyUtil::makeHex64( announcementID );
        ServiceAuditedWrite pendingJob;
        pendingJob._change.erase( Internal::getAnnouncementTable(), announcementKey );
        StringBuilder<constant::buffer::kMax128> subject;
        subject.appendFormat( "sd/notice/%#", announcementKey );
        pendingJob.setOperatorAudit( actor, "sd.notice.remove", subject.view(), nowMs );
        submitWrite( std::move( pendingJob ), requestTag );
    }

    void ServerDirectoryService::setMaintenance( const ServerDirectoryMaintenanceChange& change, const ServiceOperator& actor, string_view memo, UnixMilliseconds nowMs, uint64 requestTag )
    {
        MaintenanceWindow window;
        window._listAllowedAccount = change._listAllowedAccount;
        window._scope              = change._scope;
        window._messageKey         = change._messageKey;
        window._startMs            = change._startMs;
        window._endMs              = change._endMs;
        setMaintenance( window, actor, memo, nowMs, requestTag );
    }

    void ServerDirectoryService::postAnnouncement( const ServerDirectoryAnnouncementChange& change, const ServiceOperator& actor, UnixMilliseconds nowMs, uint64 requestTag )
    {
        ServiceAnnouncement announcement;
        announcement._text           = change._text;
        announcement._announcementID = change._announcementID;
        announcement._startMs        = change._startMs;
        announcement._endMs          = change._endMs;
        announcement._priority       = change._priority;
        announcement._bLiteralText   = change._bLiteralText;
        postAnnouncement( announcement, actor, nowMs, requestTag );
    }

    void ServerDirectoryService::takeControlCompletions( vector<ServerDirectoryControlCompletion>& outListCompletion )
    {
        vector<ServerDirectoryCompletion> listCompletion;
        _completionBuffer.moveAllTo( listCompletion );
        for ( const ServerDirectoryCompletion& completion : listCompletion )
        {
            ServerDirectoryControlResult result = ServerDirectoryControlResult::Unavailable;
            if ( completion._result == ServerDirectoryResult::Ok )
                result = ServerDirectoryControlResult::Ok;
            else if ( completion._result.hasError( OnlineError::kInvalidRequest ) )
                result = ServerDirectoryControlResult::Invalid;
            else if ( completion._result.hasError( OnlineError::kForbidden ) )
                result = ServerDirectoryControlResult::Forbidden;
            else if ( completion._result == ServerDirectoryResult::NotFound )
                result = ServerDirectoryControlResult::NotFound;
            outListCompletion.push_back( ServerDirectoryControlCompletion{ completion._requestTag, result } );
        }
    }

    void ServerDirectoryService::submitWrite( ServiceAuditedWrite&& pendingJob, uint64 requestTag )
    {
        _store.submit( [pendingJob = std::move( pendingJob )]( IServiceStoreConnection& connection )
        { return pendingJob.commit( connection ); },
                       [this, requestTag]( ServiceAuditedWriteResult result )
        {
            const ServerDirectoryServiceResult serviceResult = result == ServiceAuditedWriteResult::Ok        ? ServerDirectoryServiceResult{ ServerDirectoryResult::Ok }
                                                             : result == ServiceAuditedWriteResult::NotFound  ? ServerDirectoryServiceResult{ ServerDirectoryResult::NotFound }
                                                             : result == ServiceAuditedWriteResult::Forbidden ? ServerDirectoryServiceResult::fail( OnlineError::kForbidden )
                                                             : result == ServiceAuditedWriteResult::Busy      ? ServerDirectoryServiceResult::fail( OnlineError::kBusy )
                                                                                                              : ServerDirectoryServiceResult::fail( OnlineError::kUnavailable );
            applyWrite( requestTag, serviceResult );
        } );
    }

    void ServerDirectoryService::applyWrite( uint64 requestTag, ServerDirectoryServiceResult result )
    {
        _completionBuffer.push( ServerDirectoryCompletion{ requestTag, result } );
        if ( result != ServerDirectoryResult::Ok )
            return;
        _reloadJob.request(); // 이 서버는 다음 tick 에 다시 읽고 알린다
        if ( _dependencies._pBus != nullptr )
            (void)ServerDirectoryBus::kChangedTopic.publish( *_dependencies._pBus, ServerDirectoryChanged{} ); // 실패는 발행이 로그로 남겼다. 다른 서버는 주기로 다시 읽는다
    }
} // namespace sw
