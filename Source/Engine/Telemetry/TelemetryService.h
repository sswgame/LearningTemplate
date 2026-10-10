/**
 * @file TelemetryService.h
 * @brief 텔레메트리 파이프라인 — 동의 · 스키마 대조 · 표본 · 묶음 · 디스크 스풀(JSON lines, 크기 상한 · 회전) · 올리기 · 장면별 프레임 시간 요약 · 크래시 빵부스러기.
 * @details **동의가 꺼져 있으면(기본) 아무것도 모으지 않고 쓰지 않고 보내지 않습니다** — 사건은 버려지고, 동의를 거두면 쓰지 않은 묶음과 스풀 파일을 지웁니다.
 *          동의는 사용자 설정 `telemetry.enabled`(개인 정보 카테고리, 기본 false)가 정합니다(`bindConsentSetting`). 동의해도 기본 업로더는 아무 데도
 *          보내지 않습니다(`NullTelemetryUploader`) — 실제로 보내려면 게임이 업로더와 HTTP 창구를 끼웁니다.
 *          참고: 언리얼 `FAnalytics` · `IAnalyticsProvider`(공급자 교체 · 세션 · 사건 속성), Unity Analytics(사건 스키마 · 세션 표본 · 오프라인 큐).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "Engine/Telemetry/TelemetrySchema.h"

namespace sw
{
    struct UserSettingEvent;

    class ITelemetryUploader;
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
} // namespace sw

namespace sw
{
    /** @brief `record` 의 결과입니다. */
    enum class TelemetryRecordResult : uint8
    {
        Recorded = 0,   ///< 묶음에 들어갔다
        NoConsent,      ///< 동의가 꺼져 버렸다(빵부스러기만 남는다)
        SampledOut,     ///< 표본에서 빠졌다(세션 · 사건)
        NotInitialized, ///< 서비스가 서지 않았다
        UnknownEvent,   ///< 스키마에 없는 사건
        InvalidField    ///< 모르는 필드 · 타입이 다름 · 필수 필드 빠짐
    };

    SW_API const utf8* toString( TelemetryRecordResult result );

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
     * @brief 엔진 서비스입니다(`engine::getTelemetryService`, 게임은 `game::getService<TelemetryService>()`). `record` 는 아무 스레드에서나 부릅니다(잠금 하나).
     * @details **흐름**: `record` → 스키마 대조(모르는 사건 · 필드 · 타입 → 거절) → 빵부스러기 → 동의 → 세션 표본 → 사건 표본 → 묶음. 묶음이 `batchEvents` 에
     *          닿거나 `flushSeconds` 가 지나면(`update`) 스풀 파일 `telemetry_<세션>_<번호>.jsonl` 에 덧붙입니다. 파일이 `maxFileBytes` 를 넘으면 닫고 다음
     *          번호로 회전하며, 닫힌 파일은 업로더에 넘겨 `Sent` 면 지웁니다. 스풀 전체가 `maxFiles` · `maxTotalBytes` 를 넘으면 가장 오래된 닫힌 파일을
     *          지웁니다(보내지 못한 것을 무한히 쌓지 않는다). 지난 실행이 남긴 파일도 닫힌 파일로 이어 받습니다.
     *          **표본**은 결정적입니다 — 세션은 세션 id 해시, 사건은 (세션 · 사건 · 그 사건의 번호) 해시. 줄의 `sample` 이 비율이라 받는 쪽이 무게를 다시 준다.
     *          **장면 요약**: `recordFrame` 이 장면별 프레임 시간을 모아, 장면이 바뀌거나 끝날 때 `perf.sceneSummary`(프레임 수 · 평균 · p50 · p99 · 최대)를 남깁니다.
     */
    class SW_API TelemetryService
    {
    public:
        static constexpr const utf8* kConsentSettingID  = "telemetry.enabled";
        static constexpr const utf8* kSceneSummaryEvent = "perf.sceneSummary";
        static constexpr const utf8* kSessionStartEvent = "session.start";
        static constexpr const utf8* kSessionEndEvent   = "session.end";
        static constexpr const utf8* kSpoolFilePrefix   = "telemetry_";
        static constexpr const utf8* kSpoolExtension    = ".jsonl";

        TelemetryService();
        ~TelemetryService();

        /** @brief 스풀 폴더 · 문맥을 정하고 섭니다. 동의는 꺼진 채로 시작합니다. 폴더의 지난 스풀 파일을 닫힌 파일로 이어 받습니다. */
        void initialize( string_view spoolFolder, const TelemetryContext& context );
        /** @brief 동의가 켜져 있으면 장면 요약 · `session.end` 를 남기고 쓰고, 지금 파일을 닫아 올립니다. 설정 구독을 뗍니다. */
        void shutdown();

        [[nodiscard]] bool loadSchema( string_view path );
        [[nodiscard]] bool loadSchemaText( string_view xmlText, string_view sourceName );

        /**
         * @brief 동의를 정합니다. 켜지면 `session.start` 를 남깁니다. **꺼지면** 쓰지 않은 묶음 · 장면 시간 · 스풀 파일을 모두 지웁니다(이미 꺼져 있어도
         *        남은 파일을 지운다 — 동의 없이 보낼 것이 남지 않는다).
         */
        void setConsent( bool bEnabled );
        bool hasConsent() const;
        /** @brief 사용자 설정 `kConsentSettingID` 를 동의로 씁니다 — 지금 값을 읽고, 적용 · 되돌리기 · 로드 통보마다 다시 읽습니다. */
        void bindConsentSetting( UserSettingsManager& settings );
        /** @brief 업로더(빌림)입니다. nullptr 이면 `NullTelemetryUploader` 입니다. */
        void setUploader( ITelemetryUploader* pUploader );

        TelemetryRecordResult record( const TelemetryEvent& event );
        /** @brief 세션 시계를 흘리고 flush 시간이 되면 씁니다(게임 스레드). */
        void update( float32 deltaSeconds );
        /** @brief 장면 @p sceneID(경로 · 이름)의 프레임 하나입니다. 동의가 꺼져 있으면 아무것도 하지 않습니다. */
        void recordFrame( string_view sceneID, float32 deltaSeconds );
        /** @brief 묶음을 쓰고 상한을 지키고 닫힌 파일을 올립니다. */
        void flush();

        /** @brief 최근 사건(크래시 보고에 붙는 빵부스러기, 오래된 것부터)입니다. 동의와 상관없이 기계 안에만 남습니다. */
        void collectBreadcrumbs( vector<string>& outListBreadcrumb ) const;
        /** @brief 스풀 폴더의 파일(닫힌 것 · 지금 것)입니다. */
        void collectSpoolFiles( vector<string>& outListFilePath ) const;

        bool                    isInitialized() const;
        bool                    isSessionSampled() const;
        TelemetryStats          getStats() const;
        const TelemetrySchema&  getSchema() const { return _schema; }
        const TelemetryContext& getContext() const { return _context; }
        const string&           getSpoolFolder() const { return _spoolFolder; }

    private:
        TelemetryRecordResult recordLocked( const TelemetryEvent& event );
        bool                  isEventSampledLocked( const TelemetryEventDef& def, uint32 occurrence ) const;
        string                makeEventLineLocked( const TelemetryEvent& event, const TelemetryEventDef& def ) const;
        string                makeContextLineLocked() const;
        string                makeSpoolFilePathLocked( uint32 fileIndex ) const;
        void                  pushBreadcrumbLocked( const TelemetryEvent& event );
        void                  flushLocked();
        void                  closeCurrentFileLocked();
        void                  enforceCapsLocked();
        void                  uploadClosedLocked();
        void                  purgeLocked();
        void                  emitSceneSummaryLocked();
        void                  onSettingEvent( const UserSettingEvent& event );
        void                  refreshConsentFromSetting();

        mutable mutex           _mutex;
        TelemetrySchema         _schema;
        TelemetryContext        _context;
        TelemetryStats          _stats;
        TelemetryFrameHistogram _frames;
        DelegateHandle          _settingHandle;
        vector<string>          _listPendingLine;
        vector<string>          _listBreadcrumb;      ///< 고리 — `_breadcrumbHead` 가 가장 오래된 자리
        vector<string>          _listClosedFile;      ///< 올릴 파일(오래된 것부터)
        vector<uint32>          _listEventOccurrence; ///< 스키마 사건마다 기록을 시도한 수(사건 표본의 열쇠)
        string                  _spoolFolder;
        string                  _currentFile;
        string                  _currentScene;
        UserSettingsManager*    _pSettings;
        ITelemetryUploader*     _pUploader;
        float64                 _sessionSeconds;
        float32                 _flushTimer;
        uint64                  _currentFileBytes;
        uint32                  _sequence;
        uint32                  _fileIndex;
        uint32                  _currentFileEvents;
        uint32                  _breadcrumbHead;
        uint8                   _bInitialized    : 1;
        uint8                   _bConsent        : 1;
        uint8                   _bSessionSampled : 1;
    };
} // namespace sw
