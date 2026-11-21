#include "pch.h"

#include "Programs/OnlineLoadBot/LoadBotLocalServer.h"

#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Config/ServerConfig.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Mail/ServiceMailCampaign.h"
#include "GameFramework/Runtime/Online/Server/IOnlineStoreDriver.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStoreTracker.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStoreUtil.h"

namespace sw
{
    namespace
    {
        struct LoadBotLocalServerInternal
        {
            static constexpr uint16      kGameServerPort = 7777;
            static constexpr const utf8* kAccountKit     = "account";
            // 기동 때 넣는 전체 우편 — 봇 시나리오의 `mailbox_claim_all` 이 받을 첨부(원장 발행). 자산은 `LoadBot.online.json` 의 화폐 카탈로그에 있다.
            static constexpr const utf8* kCampaignAsset      = "cur.gold";
            static constexpr int64       kCampaignAmount     = 100;
            static constexpr int64       kCampaignLeadMs     = 60000;   ///< 시작 시각을 조금 앞에 둔다 — 서버 시각과 같은 밀리초에 받을 수 없는 일이 없게
            static constexpr int64       kCampaignLifetimeMs = 3600000; ///< 한 시간 — 남는 저장소(PostgreSQL)에 쌓이지 않게 짧게, 우편함 `_endedCampaignTTL` 이 치운다

            /**
             * @brief @p run 을 저장소 스레드에 맡기고 완료(@p complete)가 불릴 때까지 완료를 거둡니다. 저장소는 일마다 완료를 정확히 한 번 부른다 — 시한 없이 기다려도 끝난다.
             * @param kind 스냅샷 읽기면 @p run 의 모든 읽기가 한 시점이다(원장 감사)
             */
            template <typename TRun, typename TComplete>
            static void runStoreJob( ServiceStore& store, ServiceStoreJobKind kind, TRun&& run, TComplete&& complete )
            {
                ServiceStoreTracker tracker;
                tracker.bindStore( &store );
                if ( kind == ServiceStoreJobKind::SnapshotRead )
                    tracker.submitSnapshotRead( std::forward<TRun>( run ), std::forward<TComplete>( complete ) );
                else
                    tracker.submit( std::forward<TRun>( run ), std::forward<TComplete>( complete ) );
                while ( tracker.isIdle() == false )
                {
                    if ( store.pollCompletions() == 0 )
                        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + ServiceStoreUtil::kIdlePollSleepNanoseconds );
                }
            }

            /** @brief 전체 우편 하나를 넣습니다. id 는 시작 시각이라 실행마다 새 캠페인이다(남는 저장소에서도 봇마다 한 번씩 받는다). */
            [[nodiscard]] static expected<void> seedCampaign( ServiceStore& store, UnixMilliseconds startMs )
            {
                ServiceMailCampaign campaign;
                campaign._campaignID              = static_cast<uint64>( startMs.count() );
                campaign._titleKey                = "loadbot.campaign";
                campaign._senderName              = "loadbot";
                campaign._startMs                 = startMs.count() - kCampaignLeadMs;
                campaign._endMs                   = startMs.count() + kCampaignLifetimeMs;
                ServiceMailAttachment& attachment = campaign._listAttachment.emplace_back();
                attachment._assetID               = kCampaignAsset;
                attachment._amount                = kCampaignAmount;
                ServiceStoreResult result         = ServiceStoreResult::Unavailable;
                runStoreJob(
                    store, ServiceStoreJobKind::ReadWrite, [campaign]( IServiceStoreConnection& connection )
                {
                    ServiceTransaction transaction;
                    if ( ServiceMailCampaign::stageCreate( campaign, transaction ) == false )
                        return ServiceStoreResult::Invalid;
                    return connection.commit( transaction );
                }, [&result]( ServiceStoreResult committed )
                { result = committed; } );
                if ( result != ServiceStoreResult::Ok )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "the load mail campaign could not be written to the store" } );
                return {};
            }

            /**
             * @brief 파일에서 읽은 설정 위에 부하에 딸린 값을 얹습니다. 키트 목록 · 옵션은 파일이 정본이다(코드에 다시 적지 않는다).
             * @return 가벼운 해시를 켜야 하는데 파일에 계정 키트가 없으면 그 이유입니다.
             */
            [[nodiscard]] static expected<void> applyLoad( const LoadBotLocalServerSettings& settings, OnlineServerConfig& inoutConfig )
            {
                inoutConfig._serverID                = static_cast<int32>( settings._serverID );
                inoutConfig._region                  = settings._region;
                inoutConfig._maxConnections          = settings._maxConnections;
                inoutConfig._maxConnectionsPerRemote = settings._maxConnections; // 봇은 모두 루프백 한 주소에서 붙는다
                if ( settings._bLightPasswordHash == SW_FALSE )
                    return {};
                for ( OnlineServerKitEntry& kit : inoutConfig._listKit )
                {
                    if ( kit._name != kAccountKit )
                        continue;
                    // 설정 글은 키트 모듈의 형식이라 봇이 모른다. 부하 값 하나만 글 위에 얹는다.
                    JSONDocument document;
                    if ( document.parse( kit._settings.empty() ? string_view( "{}" ) : string_view( kit._settings ) ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "the account kit settings are not JSON", document.getLastError() } );
                    document.getRoot().set( "_bLightPasswordHash" ).setBool( true );
                    const expected<string> settingsText = document.dump();
                    if ( settingsText.has_value() == false )
                        return unexpected( settingsText.error() );
                    kit._settings = *settingsText;
                    return {};
                }
                return unexpected( Error{ ErrorKind::NotFound, "--light-hash needs the account kit in the online config", kAccountKit } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoadBotLocalServer::LoadBotLocalServer()
        : _runtime{ make_unique<OnlineServerRuntime>() }
    {
    }

    LoadBotLocalServer::~LoadBotLocalServer()
    {
        shutdown();
    }

    expected<void> LoadBotLocalServer::initialize( IStreamTransport* pTransport, const LoadBotLocalServerSettings& settings )
    {
        using Internal = LoadBotLocalServerInternal;
        OnlineServerRuntimeConfig runtimeSettings;
        runtimeSettings._config     = settings._config;
        const expected<void> loaded = Internal::applyLoad( settings, runtimeSettings._config );
        if ( loaded.has_value() == false )
            return loaded;
        runtimeSettings._listenAddress = NetAddress::makeLoopback( settings._port );
        runtimeSettings._pTransport    = pTransport;
        runtimeSettings._gamePort      = Internal::kGameServerPort;
        runtimeSettings._bTLS          = SW_FALSE; // 봇과 테스트는 평문 루프백이다
        runtimeSettings._pClock        = settings._pClock;
        if ( settings._pStoreConfig != nullptr )
        {
            // 전용 서버와 같은 규칙으로 고른다 — 이름이 비거나 목록에 없으면 오류(메모리로 대신하지 않는다)
            const expected<void> resolved = OnlineServerRuntime::resolveStores( *settings._pStoreConfig, runtimeSettings._config, runtimeSettings );
            if ( resolved.has_value() == false )
                return resolved;
        }
        else
        {
            runtimeSettings._store._driver = kMemoryStoreDriverName; // 봇 서버는 부하를 재고 사라진다 - 저장을 남기지 않는다
            runtimeSettings._cache._driver = kMemoryStoreDriverName;
        }
        const UnixMilliseconds startMs = settings._startMs != UnixMilliseconds{ 0 } ? settings._startMs : WallClock::nowUnixMilliseconds();
        const expected<void>   started = _runtime->initialize( std::move( runtimeSettings ), startMs );
        if ( started.has_value() == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "the online server runtime did not start", started.error().message() }.appendDetail( started.error().detail() ) );
        // 우편함이 첫 틱에 캠페인을 읽는다 — 그 전에 넣는다
        return Internal::seedCampaign( *_runtime->getContext()._pStore, startMs );
    }

    void LoadBotLocalServer::shutdown()
    {
        _runtime->shutdown();
    }

    void LoadBotLocalServer::update( UnixMilliseconds nowMs )
    {
        _runtime->update( nowMs );
    }

    OnlineServiceHost& LoadBotLocalServer::getHost()
    {
        return _runtime->getHost();
    }

    uint16 LoadBotLocalServer::getListenPort() const
    {
        return _runtime->getListenPort();
    }

    expected<LedgerAuditReport> LoadBotLocalServer::readLedgerAudit()
    {
        ServiceStore* const pStore = _runtime->isInitialized() ? _runtime->getContext()._pStore : nullptr;
        if ( pStore == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the local server is not running - nothing to audit" } );
        return runLedgerAudit( *pStore );
    }

    const ServiceStoreCommitStats* LoadBotLocalServer::findCommitStats() const
    {
        const ServiceStore* const pStore = _runtime->isInitialized() ? _runtime->getContext()._pStore : nullptr;
        return pStore != nullptr ? &pStore->getCommitStats() : nullptr;
    }

    expected<LedgerAuditReport> LoadBotLocalServer::runLedgerAudit( ServiceStore& store )
    {
        // 봇이 멈춘 뒤 남은 일(정산 · 수령의 완료)을 먼저 거둔다 — 그 완료가 새 일을 맡기면 그것도 거둔다(시한은 저장소 종료 대기와 같다)
        ServiceStoreUtil::flushUntilIdle( &store, [&store]()
        { return store.getPendingCount() == 0; } );
        ServiceStoreRead<LedgerAuditReport> audit;
        // 한 시점에서 읽는다 — PostgreSQL 에서 쪽마다 다른 시점이면 그사이 서버 예약 작업(만료 반환 · 정산)이 끼어 거짓 불균형을 본다
        LoadBotLocalServerInternal::runStoreJob(
            store, ServiceStoreJobKind::SnapshotRead, []( IServiceStoreConnection& connection )
        {
            ServiceStoreRead<LedgerAuditReport> read;
            read._result = LedgerAuditReport::computeReport( connection, read._value );
            return read;
        }, [&audit]( ServiceStoreRead<LedgerAuditReport>& read )
        { audit = std::move( read ); } );
        if ( audit.isOk() == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "the ledger audit could not read the store" } );
        return std::move( audit._value );
    }

    LoadBotServerStats LoadBotLocalServer::makeServerStats( const ServiceStoreCommitStats& commitStats, const LedgerAuditReport& report )
    {
        LoadBotServerStats serverStats;
        serverStats._bRead                 = SW_TRUE;
        serverStats._commitCount           = commitStats._commitCount.load( std::memory_order_relaxed );
        serverStats._conflictCount         = commitStats._conflictCount.load( std::memory_order_relaxed );
        serverStats._bBalanced             = report.isBalanced() ? SW_TRUE : SW_FALSE;
        serverStats._assetCount            = static_cast<int32>( report._listAsset.size() );
        serverStats._journalCount          = report._journalCount;
        serverStats._negativeEscrowCount   = report._negativeEscrowCount;
        serverStats._unreadableRecordCount = report._unreadableRecordCount;
        for ( const LedgerAssetAudit& asset : report._listAsset )
        {
            serverStats._mismatchedAssetCount += asset.isBalanced() ? 0 : 1;
        }
        return serverStats;
    }
} // namespace sw
