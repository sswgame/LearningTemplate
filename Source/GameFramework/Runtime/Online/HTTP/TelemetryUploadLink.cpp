#include "pch.h"

#include "GameFramework/Runtime/Online/HTTP/TelemetryUploadLink.h"

#include "Core/Process/ProcessEnvironment.h"

#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/TelemetryService.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace sw
{
    namespace
    {
        struct TelemetryUploadLinkInternal
        {
            static constexpr const utf8* kTrustOption = "telemetryUploadTrustFile in gamesettings";
        };
    } // namespace
} // namespace sw

namespace sw
{
    TelemetryUploadLink::TelemetryUploadLink( const GameSettings& settings )
        : _settings{ settings }
        , _telemetryClient{}
        , _crashClient{}
        , _pTelemetryUploader{}
        , _pCrashUploader{}
        , _crashWorker{}
        , _pTelemetry{ nullptr }
    {
    }

    TelemetryUploadLink::~TelemetryUploadLink()
    {
        shutdownSubsystem();
    }

    SubsystemInitResult TelemetryUploadLink::initializeSubsystem()
    {
        // 개발 구성의 실행 인자 덮어쓰기(로컬 수신기)는 게임 인스턴스가 이미 넣었다(`IGameSettingsOverride`).
        const string& telemetryAddress = _settings._telemetryUploadEndpoint;
        const string& crashAddress     = _settings._crashUploadEndpoint;
        string        error;
        if ( telemetryAddress.empty() == false && linkTelemetry( telemetryAddress, error ) == false )
        {
            SW_LOG_ERROR_ID( "TelemetryUploadLink.TelemetryNotLinked", "[TelemetryUpload] telemetry uploads to '%#' could not be set up - %#", telemetryAddress.c_str(), error.c_str() );
            return SubsystemInitResult::Failed;
        }
        if ( crashAddress.empty() == false && linkCrashReports( crashAddress, error ) == false )
        {
            SW_LOG_ERROR_ID( "TelemetryUploadLink.CrashNotLinked", "[TelemetryUpload] crash report uploads to '%#' could not be set up - %#", crashAddress.c_str(), error.c_str() );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void TelemetryUploadLink::shutdownSubsystem()
    {
        if ( _pTelemetry != nullptr )
        {
            _pTelemetry->releaseUploader( *_pTelemetryUploader ); // 지금 스풀 파일을 닫아 보낸 뒤 기본 업로더로 되돌린다
            _pTelemetry = nullptr;
        }
        _crashWorker.stop(); // 받은 묶음은 끝까지 보낸다(`uploadCrashReports`)
        _pTelemetryUploader.reset();
        _pCrashUploader.reset();
        _telemetryClient.shutdown();
        _crashClient.shutdown();
    }

    bool TelemetryUploadLink::linkTelemetry( const string& endpoint, string& outError )
    {
        TelemetryService* const pTelemetry = game::findSubsystem<TelemetryService>();
        if ( pTelemetry == nullptr )
        {
            // 텔레메트리는 클라이언트 호스트 서비스다(`TelemetryService::getSubsystemHost`). 전용 서버는 운영 지표를 따로 낸다(`ServerObservability`).
            SW_LOG_INFO_ID( "TelemetryUploadLink.NoTelemetryHost", "[TelemetryUpload] this host has no telemetry service - '%#' is not used", endpoint.c_str() );
            return true;
        }
        string apiKey;
        if ( _settings._telemetryUploadKeyEnvironment.empty() == false && ProcessEnvironment::tryReadNonEmpty( _settings._telemetryUploadKeyEnvironment, apiKey ) == false )
        {
            outError = "the API key environment variable '" + _settings._telemetryUploadKeyEnvironment + "' (telemetryUploadKeyEnvironment) is empty";
            return false;
        }
        if ( _telemetryClient.initialize() == false )
        {
            outError = "the HTTP client could not start";
            return false;
        }
        if ( _telemetryClient.registerHostTLS( endpoint, _settings._telemetryUploadTrustFile, TelemetryUploadLinkInternal::kTrustOption, outError ) == false )
            return false;
        _pTelemetryUploader = make_unique<HTTPTelemetryUploader>( _telemetryClient, endpoint, apiKey );
        pTelemetry->setUploader( *_pTelemetryUploader );
        _pTelemetry = pTelemetry;
        SW_LOG_INFO_ID( "TelemetryUploadLink.TelemetryLinked", "[TelemetryUpload] telemetry uploads go to '%#' (sent only with consent)", endpoint.c_str() );
        return true;
    }

    bool TelemetryUploadLink::linkCrashReports( const string& endpoint, string& outError )
    {
        const CrashReportService* const pCrashReports = game::findSubsystem<CrashReportService>();
        const string                    reportsFolder = pCrashReports != nullptr ? pCrashReports->getReportsFolder() : string{};
        if ( reportsFolder.empty() )
        {
            // 묶음 폴더는 텔레메트리 기동이 정한다(크래시 처리기가 있는 클라이언트). 없으면 보낼 묶음도 없다.
            SW_LOG_INFO_ID( "TelemetryUploadLink.NoCrashReports", "[TelemetryUpload] this host bundles no crash reports - '%#' is not used", endpoint.c_str() );
            return true;
        }
        if ( _crashClient.initialize() == false )
        {
            outError = "the HTTP client could not start";
            return false;
        }
        if ( _crashClient.registerHostTLS( endpoint, _settings._telemetryUploadTrustFile, TelemetryUploadLinkInternal::kTrustOption, outError ) == false )
            return false;
        _pCrashUploader = make_unique<HTTPCrashReportUploader>( _crashClient, endpoint );
        _crashWorker.start( "CrashUpload", DedicatedWorker<CrashUploadJob>::ProcessDelegate::create<&TelemetryUploadLink::uploadCrashReports>( this ) );
        CrashUploadJob job;
        job._reportsFolder = reportsFolder;
        if ( _crashWorker.submit( std::move( job ) ) == false )
        {
            outError = "the crash upload thread did not take the job";
            return false;
        }
        return true;
    }

    void TelemetryUploadLink::uploadCrashReports( CrashUploadJob& job, bool bStopping )
    {
        (void)bStopping; // 받은 묶음은 정지 중에도 보낸다 — 시도 수 상한과 상태는 매니페스트가 지킨다
        // 동의는 묶을 때 정해져 보낼 줄(Queued)에 선 묶음만 올라간다. 보고 프로세스(기본 업로더)와 같은 폴더를 보아도 그쪽은 매니페스트를 쓰지 않는다.
        const uint32 sentCount = CrashReportService::runReporter( job._reportsFolder, *_pCrashUploader );
        SW_LOG_INFO_ID( "TelemetryUploadLink.CrashReportsSent", "[TelemetryUpload] crash reports: %# sent from '%#'", sentCount, job._reportsFolder.c_str() );
    }
} // namespace sw
