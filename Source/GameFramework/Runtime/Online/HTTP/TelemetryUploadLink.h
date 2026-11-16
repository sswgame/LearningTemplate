/**
 * @file TelemetryUploadLink.h
 * @brief 엔진의 텔레메트리 · 크래시 보고 업로더에 GameFramework 의 HTTP 클라이언트를 끼우는 게임 인스턴스 서브시스템입니다.
 * @details - 엔진은 업로더 계약(`ITelemetryUploader` · `ICrashReportUploader`)과 HTTP 창구 계약(`IHTTPClient`)만 갖고 실제 전송을 모른다(층). 이 서브시스템이
 *            팩 gamesettings 의 끝점(`GameSettings::_telemetryUploadEndpoint` · `_crashUploadEndpoint`)을 읽어 `BlockingHTTPClient` 를 업로더에 넘긴다.
 *          - 끝점을 적지 않으면 아무것도 하지 않는다 — 동의해도 기계 밖으로 나가지 않는다(엔진 기본). 끝점은 운영자의 값이라 저장소의 팩은 적지 않고,
 *            로컬 확인은 개발 구성의 `-gv_telemetryUploadEndpoint` · `-gv_crashUploadEndpoint`(게임 인스턴스가 gamesettings 에 넣는다 — `IGameSettingsOverride`)로
 *            루프백 수신기를 가리킨다(`Scripts/net/telemetry_local_run.py`).
 *          - 동의 · 표본 · 묶음 · 상한은 엔진 서비스가 지킨다. 텔레메트리는 서비스의 스풀 워커 스레드(`TelemetrySpool`)가 보내고, 크래시 보고는 기동 때 전용 스레드 하나가
 *            보낼 줄의 묶음을 올린다(`CrashReportService::runReporter`). 묶음은 지난 실행의 크래시를 이번 기동이 묶은 것이라 크래시 난 프로세스가 보낼 일이 없다.
 *          - 종료 때 텔레메트리 업로더를 떼며 지금 스풀 파일을 닫아 보낸다(`TelemetryService::releaseUploader` — 워커가 그 업로더로 보내는 중인 것이 끝날 때까지 기다린다) — 빌린 참조가 엔진 서비스에 남지 않는다.
 *          언리얼은 `IAnalyticsProvider` 를 설정(`[Analytics]`)으로 고르고 크래시는 `CrashReportClient` 가 `DataRouterUrl` 로 보낸다(기억에 의존). 끝점이 설정인 것은 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/DedicatedWorker.h"

#include "Engine/Telemetry/CrashReportUploader.h"
#include "Engine/Telemetry/TelemetryUploader.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/Runtime/Online/HTTP/BlockingHTTPClient.h"

namespace sw
{
    struct GameSettings;

    class TelemetryService;

    /**
     * @class TelemetryUploadLink
     * @brief 텔레메트리 · 크래시 업로드 연결 하나입니다. 게임 인스턴스가 소유하고 게임 스레드에서 서고 내립니다.
     */
    class SW_GF_API TelemetryUploadLink final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "TelemetryUploadLink";

        /** @param settings 팩 설정입니다. 게임 인스턴스의 칸이라 이 객체보다 오래 삽니다. */
        explicit TelemetryUploadLink( const GameSettings& settings );
        ~TelemetryUploadLink() override;

        TelemetryUploadLink( const TelemetryUploadLink& )            = delete;
        TelemetryUploadLink& operator=( const TelemetryUploadLink& ) = delete;

        // ISubsystem - 끝점이 없으면 아무 일도 하지 않고 성공이다. 끝점이 있는데 클라이언트를 세우지 못하면(TLS 신뢰 파일 · 키 환경 변수) 실패다.
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 텔레메트리 업로더를 엔진 서비스에 꽂았으면 true 입니다. */
        bool isTelemetryLinked() const { return _pTelemetry != nullptr; }
        /** @brief 크래시 보고를 보낼 전용 스레드가 돌고 있으면 true 입니다. */
        bool isCrashLinked() const { return _crashWorker.isRunning(); }

    private:
        /** @brief 크래시 보고 일 하나 — 묶음 폴더입니다. */
        struct CrashUploadJob
        {
            string _reportsFolder{};
        };

        [[nodiscard]] expected<void> linkTelemetry( const string& endpoint );
        [[nodiscard]] expected<void> linkCrashReports( const string& endpoint );
        /** @brief 전용 스레드에서 묶음을 올립니다. 정지 중에 꺼낸 일도 끝까지 보낸다(이미 받은 묶음이라 다음 기동까지 미룰 까닭이 없다). */
        void uploadCrashReports( CrashUploadJob& job, bool bStopping );

    private:
        const GameSettings&                 _settings;
        BlockingHTTPClient                  _telemetryClient; ///< 텔레메트리 스풀 워커 스레드가 쓴다
        BlockingHTTPClient                  _crashClient;     ///< 크래시 보고 전용 스레드가 쓴다(요청 하나가 덤프만큼 길어 텔레메트리와 나누지 않는다)
        unique_ptr<HTTPTelemetryUploader>   _pTelemetryUploader;
        unique_ptr<HTTPCrashReportUploader> _pCrashUploader;
        DedicatedWorker<CrashUploadJob>     _crashWorker; ///< 업로더보다 뒤에 선언해 먼저 멈춘다
        TelemetryService*                   _pTelemetry;  ///< 업로더를 꽂은 엔진 서비스(꽂지 않았으면 nullptr)
    };
} // namespace sw
