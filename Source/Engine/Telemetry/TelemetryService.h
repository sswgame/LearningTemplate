/**
 * @file TelemetryService.h
 * @brief 텔레메트리 파이프라인 — 동의 · 스키마 대조 · 표본 · 묶음 · 디스크 스풀(JSON lines, 크기 상한 · 회전) · 올리기 · 장면별 프레임 시간 요약 · 크래시 빵부스러기.
 * @details **동의가 꺼져 있으면(기본) 아무것도 모으지 않고 쓰지 않고 보내지 않습니다** — 사건은 버려지고, 동의를 거두면 쓰지 않은 묶음과 스풀 파일을 지웁니다.
 *          동의는 사용자 설정 `telemetry.enabled`(개인 정보 카테고리, 기본 false)가 정합니다(`bindConsentSetting`). 동의해도 기본 업로더는 아무 데도
 *          보내지 않습니다(`NullTelemetryUploader`) — 실제로 보내려면 게임이 업로더와 HTTP 창구를 끼웁니다.
 *          참고: 언리얼 `FAnalytics` · `IAnalyticsProvider`(공급자 교체 · 세션 · 사건 속성), Unity Analytics(사건 스키마 · 세션 표본 · 오프라인 큐).
 */
#pragma once
#include "Core/Concurrency/OnceInit.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Task/DedicatedWorker.h"
#include "Core/Time/ExponentialBackoff.h"

#include "Engine/Content/AssetID.h"
#include "Engine/EngineFrameHooks.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Telemetry/TelemetrySchema.h"
#include "Engine/Telemetry/TelemetryUploader.h"

#include <condition_variable>

namespace sw
{
    struct UserSettingEvent;

    class TelemetryEvent;
    class UserSettingsManager;

    /** @brief 스풀 파일마다 첫 줄로 적는 이 실행의 문맥입니다. */
    struct TelemetryContext
    {
        string _sessionID{};   ///< 크래시 보고 · 로그 파일과 같은 세션 id
        string _buildConfig{}; ///< Debug · Release · Shipping
        string _platform{};
        string _buildID{}; ///< 실행 파일의 빌드 id(PDB 서명 · GNU build-id — `ModuleBuildID`)
        string _game{};
    };

    /** @brief 스풀 워커가 할 일의 종류입니다. */
    enum class TelemetrySpoolJobKind : uint8
    {
        Write = 0, ///< 줄을 덧붙이고 상한을 지키고 닫힌 파일을 올린다
        Close,     ///< Write 뒤 지금 파일을 닫아 올린다(종료 · 업로더 떼기)
        Purge      ///< 동의를 거뒀다 — 스풀 파일을 모두 지운다
    };

    /** @brief 게임 쪽이 잠금 아래에서 만들어 스풀 워커에 넘기는 일 하나입니다(워커는 게임 쪽 상태를 읽지 않는다 — 필요한 값을 담아 간다). */
    struct TelemetrySpoolJob
    {
        vector<string>          _listLine{};
        TelemetryPipelineConfig _settings{};
        float64                 _nowSeconds{ 0.0 }; ///< 세션 시계 — 올리기 물러서기의 기준
        uint64                  _jobID{ 0 };
        int32                   _schemaVersion{ 0 };
        TelemetrySpoolJobKind   _kind{ TelemetrySpoolJobKind::Write };
    };
} // namespace sw

namespace sw
{
    /** @brief `record` 의 결과입니다. */
    ENUM()
    enum class TelemetryRecordResult : uint8
    {
        Recorded = 0,   ///< 묶음에 들어갔다
        NoConsent,      ///< 동의가 꺼져 버렸다(빵부스러기만 남는다)
        SampledOut,     ///< 표본에서 빠졌다(세션 · 사건)
        NotInitialized, ///< 서비스가 서지 않았다
        UnknownEvent,   ///< 스키마에 없는 사건
        InvalidField    ///< 모르는 필드 · 타입이 다름 · 필수 필드 빠짐
    };

    /** @brief 파이프라인 계수입니다. */
    struct TelemetryStats
    {
        uint32 _recorded{ 0 };
        uint32 _blockedByConsent{ 0 };
        uint32 _sampledOut{ 0 };
        uint32 _rejected{ 0 };
        uint32 _writtenEvents{ 0 };
        uint32 _rotatedFiles{ 0 }; ///< 크기 상한으로 닫은 파일
        uint32 _droppedFiles{ 0 }; ///< 수 · 전체 크기 상한으로 지운 파일(보내지 못한 채)
        uint32 _uploadedFiles{ 0 };
        uint32 _purgedFiles{ 0 }; ///< 동의를 거둬 지운 파일
    };
} // namespace sw

namespace sw
{
    /**
     * @class TelemetryFrameHistogram
     * @brief 프레임 시간 히스토그램입니다(0.1 ms 칸 2500 개 — 250 ms 넘는 것은 넘침 칸). 백분위는 칸 가운데 값이라 오차가 0.05 ms 안입니다.
     */
    class SW_API TelemetryFrameHistogram
    {
    public:
        static constexpr float32 kBucketMs    = 0.1f;
        static constexpr uint32  kBucketCount = 2500;

        TelemetryFrameHistogram();

        void    add( float32 milliseconds );
        void    reset();
        uint32  getCount() const { return _count; }
        float32 getMaxMs() const { return _maxMs; }
        float64 getTotalMs() const { return _totalMs; }
        /** @brief @p fraction(0..1) 백분위의 프레임 시간(ms)입니다. 표본이 없으면 0 입니다. */
        float32 computePercentileMs( float32 fraction ) const;

    private:
        vector<uint32> _listBucket;
        float64        _totalMs;
        float32        _maxMs;
        uint32         _count;
        uint32         _overflowCount;
    };
} // namespace sw

namespace sw
{
    /**
     * @class TelemetryService
     * @brief 엔진 서비스입니다(`engine::getTelemetryService`, 게임은 `game::findSubsystem<TelemetryService>()`). `record` 는 아무 스레드에서나 부릅니다(잠금 하나).
     * @details **흐름**: `record` → 스키마 대조(모르는 사건 · 필드 · 타입 → 거절) → 빵부스러기 → 동의 → 세션 표본 → 사건 표본 → 묶음. 묶음이 `batchEvents` 에
     *          닿거나 `flushSeconds` 가 지나면(`update`) 스풀 파일 `telemetry_<세션>_<번호>.jsonl` 에 덧붙입니다. 파일이 `maxFileBytes` 를 넘으면 닫고 다음
     *          번호로 회전하며, 닫힌 파일은 업로더에 넘겨 `Sent` 면 지웁니다. 스풀 전체가 `maxFiles` · `maxTotalBytes` 를 넘으면 가장 오래된 닫힌 파일을
     *          지웁니다(보내지 못한 것을 무한히 쌓지 않는다). 지난 실행이 남긴 파일도 닫힌 파일로 이어 받습니다.
     *          **표본**은 결정적입니다 — 세션은 세션 id 해시, 사건은 (세션 · 사건 · 그 사건의 번호) 해시. 줄의 `sample` 이 비율이라 받는 쪽이 무게를 다시 준다.
     *          **장면 요약**: `recordFrame` 이 장면별 프레임 시간을 모아, 장면이 바뀌거나 끝날 때 `perf.sceneSummary`(프레임 수 · 평균 · p50 · p99 · 최대)를 남깁니다.
     *          **프레임 관찰자**: 엔진 루프가 기동 뒤 서비스를 관찰자로 붙입니다(서브시스템이 섰을 때만). 씬 틱 뒤(`onAfterSceneTick`)에 활성 씬의 프레임 하나를
     *          기록하고 세션 시계를 흘립니다(`recordFrame` → `update`).
     *          **스레드**: 게임 쪽 잠금(`_mutex`)은 메모리(묶음 · 빵부스러기 · 시계)만 지킨다. 스풀 쓰기 · 회전 · 상한 · 올리기는 전용 스레드 `TelemetrySpool`
     *          (`DedicatedWorker`)가 받은 순서대로 하고, 그 상태는 스풀 잠금(`_spoolMutex`)이 지킨다. 네트워크 올리기는 어느 잠금도 쥐지 않는다 — 끝점이 느려도
     *          프레임 · 다른 스레드의 `record` 가 기다리지 않는다(감사 N-6). 올리기가 `Failed` 면 지수 물러서기(`uploadRetrySeconds` ~ `uploadRetryMaxSeconds`,
     *          지터)만큼 세션 시계가 흐를 때까지 다시 보내지 않는다. 언리얼 `FHttpModule`(비동기) + 분석 공급자의 배치 플러시와 같은 자리다.
     *          **서브시스템** `Telemetry`(Client 대상): 엔진 · 게임 스키마를 읽고 스풀 폴더(사용자 설정 파일 옆 `telemetry/`)로
     *          서며 동의를 사용자 설정에 묶습니다. 같은 동의를 쓰는 크래시 보고(`CrashReportService`)의 지난 크래시 묶기도 이 단계가 세우고 내립니다.
     */
    class SW_API TelemetryService : public ISubsystem
    {
    public:
        static constexpr const utf8* kConsentSettingID  = "telemetry.enabled";
        static constexpr const utf8* kSceneSummaryEvent = "perf.sceneSummary";
        static constexpr const utf8* kSessionStartEvent = "session.start";
        static constexpr const utf8* kSessionEndEvent   = "session.end";
        static constexpr const utf8* kSpoolFilePrefix   = "telemetry_";
        static constexpr const utf8* kSpoolExtension    = ".jsonl";

        // ISubsystem — 기동 단계 `Telemetry`. 스키마가 틀려도 기동은 멈추지 않는다(스키마에 없는 사건은 기록되지 않을 뿐이다).
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Telemetry";

        TelemetryService();
        ~TelemetryService() override;

        // 프레임 훅 — 씬 틱 뒤만 쓴다(엔진이 `EngineFrameHooks::_onAfterSceneTick` 에 붙인다).
        /** @brief 활성 씬(`_activeSceneID`)의 프레임 하나를 기록하고 세션 시계를 흘립니다(`recordFrame` → `update`). 동의가 없으면 둘 다 아무것도 하지 않습니다. */
        void onAfterSceneTick( const EngineFrameContext& context );

        /** @brief 스풀 폴더 · 문맥을 정하고 섭니다. 동의는 꺼진 채로 시작합니다. 폴더의 지난 스풀 파일을 닫힌 파일로 이어 받습니다(부른 스레드에서 폴더를 읽는다). */
        void initialize( string_view spoolFolder, const TelemetryContext& context );
        /** @brief 동의가 켜져 있으면 장면 요약 · `session.end` 를 남기고 쓰고, 지금 파일을 닫아 올립니다. 스풀 워커가 그 일을 마칠 때까지 기다립니다. 설정 구독을 뗍니다. */
        void shutdown();

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        /** @brief 동의는 사용자 설정이다. 종료가 맨 처음 쪽이라 마지막 장면 요약 · 묶음을 쓴다. */
        span<const utf8* const> getSubsystemDependencies() const override;
        /** @brief 서버 크래시 묶기 · 텔레메트리는 후속이다(동의가 플레이어 설정이다). */
        SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        [[nodiscard]] bool loadSchema( const AssetID& assetID );
        [[nodiscard]] bool loadSchemaText( string_view xmlText, string_view sourceName );

        /**
         * @brief 동의를 정합니다. 켜지면 `session.start` 를 남깁니다. **꺼지면** 쓰지 않은 묶음 · 장면 시간을 지우고 스풀 파일 지우기를 스풀 워커에 넘깁니다
         *        (이미 꺼져 있어도 남은 파일을 지운다 — 동의 없이 보낼 것이 남지 않는다). 앞서 넘긴 쓰기가 먼저 끝나고 지우기가 그 뒤에 온다.
         */
        void setConsent( bool bEnabled );
        bool hasConsent() const;
        /** @brief 사용자 설정 `kConsentSettingID` 를 동의로 씁니다 — 지금 값을 읽고, 적용 · 되돌리기 · 로드 통보마다 다시 읽습니다. */
        void bindConsentSetting( UserSettingsManager& settings );
        /**
         * @brief 업로더(빌림)를 바꿉니다. 처음에는 서비스가 값으로 든 `NullTelemetryUploader`(보내지 않는다)가 꽂혀 있어 포인터가 비는 때가 없습니다.
         * @details 참조로 받아 "없음" 을 넘길 수 없습니다 — 업로더는 항상 하나이고, 호출부는 따로 널 검사 · 기본값 처리를 하지 않습니다.
         */
        void setUploader( ITelemetryUploader& uploader );
        /**
         * @brief @p uploader 가 지금 업로더면 쓰지 않은 묶음과 지금 스풀 파일을 닫아 그 업로더로 보낸 뒤 기본 업로더(`NullTelemetryUploader`)로 되돌립니다.
         * @details 업로더를 빌려 준 쪽이 사라지기 전에 부릅니다 — 스풀 워커가 그 일을 마치고 그 업로더로 보내는 중인 것이 없을 때까지 기다리므로, 돌아온 뒤
         *          빌린 참조가 남지 않는다. 다른 업로더가 꽂혀 있으면 아무것도 하지 않습니다. 물러서기 중이면 보내지 않고 닫은 파일은 다음 실행이 이어 받습니다.
         *          이 뒤에 남는 사건(`session.end`)도 다음 실행이 닫힌 파일로 이어 받아 보냅니다.
         */
        void releaseUploader( ITelemetryUploader& uploader );

        TelemetryRecordResult record( const TelemetryEvent& event );
        /** @brief 세션 시계를 흘리고 flush 시간이 되면 씁니다(게임 스레드). */
        void update( float32 deltaSeconds );
        /** @brief 장면 @p sceneID(경로 · 이름)의 프레임 하나입니다. 동의가 꺼져 있으면 아무것도 하지 않습니다. */
        void recordFrame( string_view sceneID, float32 deltaSeconds );
        /**
         * @brief 묶음을 스풀 워커에 넘기고(동의가 있을 때), 그때까지 넘긴 일이 모두 끝날 때까지 기다립니다 — 쓰기 · 상한 · 올리기까지.
         * @details 디스크 · 끝점을 기다리므로 게임 루프에서 부르지 않습니다(루프는 `update` 가 기다리지 않고 넘긴다). 종료 앞 · 테스트가 부릅니다.
         */
        void flush();

        /** @brief 최근 사건(크래시 보고에 붙는 빵부스러기, 오래된 것부터)입니다. 동의와 상관없이 기계 안에만 남습니다. */
        void collectBreadcrumbs( vector<string>& outListBreadcrumb ) const;
        /** @brief 스풀 폴더의 파일(닫힌 것 · 지금 것)입니다. 스풀 워커가 아직 처리하지 않은 일은 반영되지 않습니다(`flush`). */
        void collectSpoolFiles( vector<string>& outListFilePath ) const;

        bool                    isInitialized() const;
        bool                    isSessionSampled() const;
        TelemetryStats          getStats() const;
        const TelemetrySchema&  getSchema() const { return _schema; }
        const TelemetryContext& getContext() const { return _context.get(); }
        const string&           getSpoolFolder() const { return _spoolFolder.get(); }

    private:
        TelemetryRecordResult recordLocked( const TelemetryEvent& event ) SW_REQUIRES_LOCK( _mutex );
        bool                  isEventSampledLocked( const TelemetryEventDef& def, uint32 occurrence ) const SW_REQUIRES_LOCK( _mutex );
        string                makeEventLineLocked( const TelemetryEvent& event, const TelemetryEventDef& def ) const SW_REQUIRES_LOCK( _mutex );
        void                  pushBreadcrumbLocked( const TelemetryEvent& event ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 묶음을 담은 일을 스풀 워커에 넘깁니다. 넘긴 일의 id 입니다. */
        uint64 submitSpoolJobLocked( TelemetrySpoolJobKind kind ) SW_REQUIRES_LOCK( _mutex );
        void   emitSceneSummaryLocked() SW_REQUIRES_LOCK( _mutex );
        /**
         * @brief 스풀 워커가 @p jobID 까지 마칠 때까지 기다립니다. @p pReleasedUploader 가 있으면 그것으로 보내는 중인 것도 끝나길 기다려, 같은 잠금 안에서
         *        기본 업로더로 되돌립니다(다음 올리기는 기본 업로더를 본다).
         */
        void waitSpoolJob( uint64 jobID, const ITelemetryUploader* pReleasedUploader ) SW_EXCLUDES_LOCK( _mutex, _spoolMutex );

        // 스풀 워커 스레드(`TelemetrySpool`)
        void   processSpoolJob( TelemetrySpoolJob& job, bool bStopping );
        void   writeLinesSpoolLocked( const TelemetrySpoolJob& job ) SW_REQUIRES_LOCK( _spoolMutex );
        string makeContextLineSpoolLocked( int32 schemaVersion ) const SW_REQUIRES_LOCK( _spoolMutex );
        string makeSpoolFilePathSpoolLocked( uint32 fileIndex ) const SW_REQUIRES_LOCK( _spoolMutex );
        void   closeCurrentFileSpoolLocked() SW_REQUIRES_LOCK( _spoolMutex );
        void   enforceCapsSpoolLocked( const TelemetryPipelineConfig& settings ) SW_REQUIRES_LOCK( _spoolMutex );
        void   purgeSpoolLocked() SW_REQUIRES_LOCK( _spoolMutex );
        /** @brief 닫힌 파일을 오래된 것부터 올립니다. 파일 읽기 · 보내기 동안은 스풀 잠금을 놓는다. */
        void uploadClosed( const TelemetrySpoolJob& job ) SW_EXCLUDES_LOCK( _spoolMutex );
        void onSettingEvent( const UserSettingEvent& event );
        void refreshConsentFromSetting();

    private:
        mutable mutex                       _mutex; ///< 게임 쪽 — 메모리 상태만 지킨다(쥔 채 I/O 를 하지 않는다)
        TelemetrySchema                     _schema;
        OnceInit<TelemetryContext>          _context;                ///< initialize 가 한 번 정한다(shutdown 이 비운다). 스풀 워커도 읽는다.
        TelemetryStats _stats               SW_GUARDED_BY( _mutex ); ///< 기록 쪽 계수(recorded · blockedByConsent · sampledOut · rejected)
        TelemetryFrameHistogram _frames     SW_GUARDED_BY( _mutex );
        Subscription                        _settingSubscription; ///< 동의 설정의 변경 통보 구독(`bindConsentSetting` ~ `shutdown`)
        vector<string> _listPendingLine     SW_GUARDED_BY( _mutex );
        vector<string> _listBreadcrumb      SW_GUARDED_BY( _mutex ); ///< 고리 — `_breadcrumbHead` 가 가장 오래된 자리
        vector<uint32> _listEventOccurrence SW_GUARDED_BY( _mutex ); ///< 스키마 사건마다 기록을 시도한 수(사건 표본의 열쇠)
        OnceInit<string>                    _spoolFolder;            ///< initialize 가 한 번 정한다(shutdown 이 비운다). 스풀 워커도 읽는다.
        string _currentScene                SW_GUARDED_BY( _mutex );
        UserSettingsManager*                _pSettings;
        float64 _sessionSeconds             SW_GUARDED_BY( _mutex );
        uint64 _lastSubmittedJob            SW_GUARDED_BY( _mutex ); ///< 스풀 워커에 마지막으로 넘긴 일의 id(1 부터)
        float32 _flushTimer                 SW_GUARDED_BY( _mutex );
        uint32 _sequence                    SW_GUARDED_BY( _mutex );
        uint32 _breadcrumbHead              SW_GUARDED_BY( _mutex );
        uint8                               _bInitialized    : 1 SW_GUARDED_BY( _mutex );
        uint8                               _bConsent        : 1 SW_GUARDED_BY( _mutex );
        uint8                               _bSessionSampled : 1 SW_GUARDED_BY( _mutex );

        // 스풀 쪽 — 스풀 워커가 쓰고 조회가 읽는다. 잠금 순서는 `_mutex` → `_spoolMutex`(워커는 `_mutex` 를 잡지 않는다).
        mutable mutex                       _spoolMutex;
        std::condition_variable_any         _spoolProgress;               ///< 일 하나를 마치거나 올리기 하나가 끝날 때 깨운다(`waitSpoolJob`)
        TelemetryStats _spoolStats          SW_GUARDED_BY( _spoolMutex ); ///< 스풀 쪽 계수(written · rotated · dropped · uploaded · purged)
        vector<string> _listClosedFile      SW_GUARDED_BY( _spoolMutex ); ///< 올릴 파일(오래된 것부터)
        string _currentFile                 SW_GUARDED_BY( _spoolMutex );
        NullTelemetryUploader               _nullUploader;                ///< 기본 업로더(값) — 아무 데도 보내지 않는다
        ITelemetryUploader* _pUploader      SW_GUARDED_BY( _spoolMutex ); ///< 빌린 업로더. 처음에는 `_nullUploader` 를 가리키고 비는 때가 없다
        ITelemetryUploader* _pUploadingWith SW_GUARDED_BY( _spoolMutex ); ///< 지금 잠금 밖에서 보내고 있는 업로더(없으면 nullptr)
        ExponentialBackoff _uploadBackoff   SW_GUARDED_BY( _spoolMutex ); ///< 연속 실패의 물러서기 — 실패가 시작될 때 그 일의 설정 값으로 다시 만든다
        float64 _uploadRetryAtSeconds       SW_GUARDED_BY( _spoolMutex ); ///< 이 세션 시각 전에는 올리지 않는다
        uint64 _currentFileBytes            SW_GUARDED_BY( _spoolMutex );
        uint64 _completedJob                SW_GUARDED_BY( _spoolMutex ); ///< 스풀 워커가 마친 마지막 일의 id
        uint32 _fileIndex                   SW_GUARDED_BY( _spoolMutex );
        uint32 _currentFileEvents           SW_GUARDED_BY( _spoolMutex );
        DedicatedWorker<TelemetrySpoolJob>  _spoolWorker; ///< 맨 뒤 — 생성자가 띄우고 소멸자가 멈춘다(처리 함수가 위 멤버를 쓴다)
    };

    SW_SERVICE_SLOT( TelemetryService, Required );
} // namespace sw
