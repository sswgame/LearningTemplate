/**
 * @file CrashReportService.h
 * @brief 크래시 보고 — 다음 실행이 지난 크래시의 파일(미니덤프 · 컨텍스트 · 스택 · 빵부스러기 · 그 세션의 로그)을 묶음 폴더 하나로 모으고, 동의에 따라
 *        두거나 · 묻거나 · 보고 프로세스(`App -crash-reporter=<폴더>`)에 넘겨 올립니다.
 * @details 참고: 언리얼 CrashReportClient(별도 프로세스가 덤프 · 로그 · 컨텍스트를 묶어 "보내기?" 를 묻고 올린다), Sentry · Backtrace(미니덤프 업로드 ·
 *          빵부스러기 · 빌드 id 로 심볼 매칭). 크래시 순간에는 아무것도 보내지 않습니다 — 죽어 가는 프로세스는 할당 없이 파일만 씁니다(`CrashHandler`).
 *          **기본은 기계 안에만**(`telemetry.crashReports` = `local`) — 묶음은 만들지만 보내지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    struct UserSettingEvent;

    class ICrashReportUploader;
    class UserSettingsManager;

    /** @brief 크래시 보고 동의 — 사용자 설정 `telemetry.crashReports` 의 선택지입니다. */
    enum class CrashReportConsent : uint8
    {
        Local = 0, ///< 묶음만 만들어 기계 안에 둔다(기본)
        Ask,       ///< 묶음마다 "보낼까요?" — 게임 UI 가 `decide` 로 답한다
        Send       ///< 묶자마자 보낼 줄에 세운다
    };

    /** @brief 묶음 하나의 상태(매니페스트의 `state`)입니다. */
    enum class CrashReportState : uint8
    {
        Local = 0,        ///< 보내지 않는다
        AwaitingDecision, ///< 플레이어의 답을 기다린다
        Queued,           ///< 보낼 줄 — 보고 프로세스가 올린다
        Sent,             ///< 받는 쪽이 받았다
        Declined          ///< 플레이어가 보내지 않기로 했다
    };

    SW_API const utf8* toString( CrashReportConsent consent );
    SW_API const utf8* toString( CrashReportState state );
    /** @brief 설정 값(`local` · `ask` · `send`)을 읽습니다. 모르는 값이면 false 입니다. */
    [[nodiscard]] SW_API bool parseCrashReportConsent( string_view text, CrashReportConsent& outConsent );

    /** @brief 묶음 하나의 요약 — 게임의 "보낼까요?" 창이 읽습니다. */
    struct CrashReportSummary
    {
        string           _sessionID{};
        string           _folder{};
        string           _reason{};
        string           _buildID{};
        uint64           _crashTime{ 0 }; ///< 크래시 파일 시각(초)
        uint32           _attempts{ 0 };
        CrashReportState _state{ CrashReportState::Local };
    };
} // namespace sw

namespace sw
{
    /**
     * @class CrashReportService
     * @brief 엔진 서비스입니다(`game::getService<CrashReportService>()`). 기동 단계 `Telemetry` 가 세우고 지난 크래시를 묶습니다.
     * @details **묶음** `<보고 폴더>/crash_<세션>/`: `crash.dmp` · `crash.txt`(컨텍스트 — 사유 · 주소 · 백엔드 · GPU · 빌드 id) · `crash.stack.txt` ·
     *          `crash.breadcrumbs.txt`(최근 사건) · `last.log`(그 세션의 로그, 끝 `kMaxLogBytes`) · `manifest.json`(세션 · 상태 · 사유 · 빌드 id · 시스템 정보 · 파일 목록).
     *          원래 크래시 파일은 묶음으로 옮기고(다시 묶지 않는다), 로그는 복사합니다. 묶음은 `kMaxReports` 개까지 — 넘으면 오래된 것부터 지웁니다.
     *          **동의**를 바꾸면 이미 있는 묶음에도 적용합니다: `local` 로 바꾸면 기다리던 · 보낼 줄의 묶음이 기계 안으로 돌아오고(철회), `send` 로 바꾸면
     *          답을 기다리던 묶음이 보낼 줄에 섭니다(이미 `local` 로 모인 묶음은 소급해 보내지 않는다).
     */
    class SW_API CrashReportService
    {
    public:
        static constexpr const utf8* kConsentSettingID = "telemetry.crashReports";
        /** @brief 보고 프로세스를 띄우는 명령줄 키입니다 — `ArgumentList.xxx` 의 CRASH_REPORTER 철자(`-crash-reporter=<폴더>`). */
        static constexpr const utf8* kReporterArgument  = "-crash-reporter";
        static constexpr const utf8* kManifestFileName  = "manifest.json";
        static constexpr const utf8* kBundlePrefix      = "crash_";
        static constexpr const utf8* kReportsFolderName = "CrashReports"; ///< 로그 폴더(Saved/Logs) 옆의 묶음 폴더 이름
        static constexpr uint32      kMaxAttempts       = 3;              ///< 보고 프로세스가 묶음 하나를 올려 보는 횟수 상한
        static constexpr uint32      kMaxReports        = 10;             ///< 남겨 두는 묶음 수
        static constexpr uint64      kMaxLogBytes       = 512u * 1024u;

        CrashReportService();
        ~CrashReportService();
        CrashReportService( const CrashReportService& )            = delete;
        CrashReportService& operator=( const CrashReportService& ) = delete;

        /**
         * @brief 크래시 파일 폴더(`getCrashReportFolder` — 로그 폴더), 묶음 폴더, 지금 세션 id 를 정합니다. 지금 세션의 파일은 묶지 않습니다(살아 있다).
         */
        void initialize( string_view crashFolder, string_view reportsFolder, string_view currentSessionID );
        void shutdown();

        /** @brief 동의를 정하고 이미 있는 묶음의 상태에 적용합니다(클래스 설명). */
        void               setConsent( CrashReportConsent consent );
        CrashReportConsent getConsent() const { return _consent; }
        /** @brief 사용자 설정 `kConsentSettingID` 를 동의로 씁니다 — 지금 값을 읽고, 적용 · 되돌리기 · 로드 통보마다 다시 읽습니다. */
        void bindConsentSetting( UserSettingsManager& settings );

        /** @brief 지난 실행의 크래시를 묶습니다. 새로 만든 묶음 수입니다. */
        uint32 collectNewCrashes();
        /** @brief 묶음들의 요약(크래시 시각 순)입니다. */
        void collectReports( vector<CrashReportSummary>& outListReport ) const;
        /** @brief 답을 기다리던 묶음에 답합니다(보낸다 → 보낼 줄, 아니다 → Declined). 그 상태가 아니면 false 입니다. */
        bool decide( string_view sessionID, bool bSend );
        /** @brief 보낼 줄에 있고 시도가 남은 묶음 수입니다. */
        uint32 countPendingUploads() const;
        /**
         * @brief 보고 프로세스로 띄울 실행 파일입니다(`kReporterArgument` 를 알아듣는 것 — App). 비면(기본) 띄우지 않습니다 — 시험 실행 파일 같은
         *        다른 호스트가 자기를 보고 프로세스로 다시 띄우지 않게, 띄울 쪽(EngineLoop)이 정합니다.
         */
        void setReporterExecutable( string_view executablePath ) { _reporterExecutable = string( executablePath ); }
        /** @brief 보낼 것이 있고 보고 실행 파일이 정해져 있으면 보고 프로세스(`<실행 파일> -crash-reporter="<묶음 폴더>"`)를 기다리지 않고 띄웁니다. 띄웠으면 true 입니다. */
        bool          launchReporterProcess() const;
        const string& getReportsFolder() const { return _reportsFolder; }

        /**
         * @brief 보고 프로세스의 일 — 보낼 줄의 묶음을 업로더로 올립니다. `Sent` 면 상태를 바꾸고 덤프를 지우며(매니페스트 · 작은 파일은 남긴다),
         *        아니면 시도 수만 올립니다. 올린 수입니다.
         */
        static uint32 runReporter( string_view reportsFolder, ICrashReportUploader& uploader );

    private:
        [[nodiscard]] bool createBundle( string_view sessionID, const string& contextPath );
        void               applyConsentToExisting();
        void               enforceReportCap();
        void               onSettingEvent( const UserSettingEvent& event );
        void               refreshConsentFromSetting();

        string               _crashFolder;
        string               _reportsFolder;
        string               _currentSessionID;
        string               _reporterExecutable;
        DelegateHandle       _settingHandle;
        UserSettingsManager* _pSettings;
        CrashReportConsent   _consent;
        uint8                _bInitialized;
    };
} // namespace sw
