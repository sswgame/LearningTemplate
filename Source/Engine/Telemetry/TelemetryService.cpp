#include "pch.h"

#include "Engine/Telemetry/TelemetryService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Module/ModuleBuildID.h"
#include "Core/Process/ProcessSession.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/PackEngineSettingsSubsystem.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryUploader.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include <algorithm>
#include <cstdio>

namespace sw
{
    namespace
    {
        struct TelemetryServiceInternal
        {
            static constexpr uint32 kUnitMask = 0xffffffu;

            /** @brief 해시를 [0, 1] 로 — 표본 비교에 씁니다. */
            static float32 toUnit( uint64 hash ) { return static_cast<float32>( static_cast<uint32>( hash ^ ( hash >> 32 ) ) & kUnitMask ) / static_cast<float32>( kUnitMask ); }

            static bool isSampled( float32 rate, float32 unit ) { return rate >= 1.0f || unit < rate; }

            static bool isSpoolFile( string_view path )
            {
                const string name = FileUtil::getFileNamePart( path );
                return StringUtil::startsWith( name, TelemetryService::kSpoolFilePrefix ) && StringUtil::endsWith( name, TelemetryService::kSpoolExtension );
            }

            /** @brief 지난 실행이 남긴 파일의 순서 — 시각(초), 같으면 이름. */
            struct SpoolFileLess
            {
                bool operator()( const string& lhs, const string& rhs ) const
                {
                    // 그 사이 지워진 파일은 시각 0 으로 맨 앞에 선다 - 다음 정리가 목록에서 뺀다.
                    uint64 lhsTime = 0;
                    uint64 rhsTime = 0;
                    (void)FileUtil::tryGetFileTimestamp( lhs, lhsTime );
                    (void)FileUtil::tryGetFileTimestamp( rhs, rhsTime );
                    if ( lhsTime != rhsTime )
                        return lhsTime < rhsTime;
                    return lhs < rhs;
                }
            };

            static void setValueJSON( const JSONValue& target, const TelemetryValue& value )
            {
                switch ( value._type )
                {
                    case TelemetryFieldType::Bool:
                    {
                        target.setBool( value._bValue == SW_TRUE );
                        break;
                    }
                    case TelemetryFieldType::Int:
                    {
                        target.setInt( value._integer );
                        break;
                    }
                    case TelemetryFieldType::Float:
                    {
                        target.setFloat( value._number );
                        break;
                    }
                    case TelemetryFieldType::String:
                    {
                        target.setString( value._text );
                        break;
                    }
                }
            }

            static void appendValueText( StringBuilder<constant::buffer::kMax512>& text, const TelemetryValue& value )
            {
                switch ( value._type )
                {
                    case TelemetryFieldType::Bool:
                    {
                        text.append( value._bValue == SW_TRUE ? "true" : "false" );
                        break;
                    }
                    case TelemetryFieldType::Int:
                    {
                        text.appendFormat( "%#", value._integer );
                        break;
                    }
                    case TelemetryFieldType::Float:
                    {
                        text.appendFormat( "%.3f", value._number );
                        break;
                    }
                    case TelemetryFieldType::String:
                    {
                        text.append( value._text.c_str() );
                        break;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TelemetryFrameHistogram::TelemetryFrameHistogram()
        : _listBucket{}
        , _totalMs{ 0.0 }
        , _maxMs{ 0.0f }
        , _count{ 0 }
        , _overflowCount{ 0 }
    {
        _listBucket.assign( kBucketCount, 0u );
    }

    void TelemetryFrameHistogram::add( float32 milliseconds )
    {
        const float32 clamped     = MathUtil::max( 0.0f, milliseconds );
        const uint32  bucketIndex = static_cast<uint32>( clamped / kBucketMs );
        if ( bucketIndex < kBucketCount )
            ++_listBucket[bucketIndex];
        else
            ++_overflowCount;
        _totalMs += static_cast<float64>( clamped );
        _maxMs = MathUtil::max( _maxMs, clamped );
        ++_count;
    }

    void TelemetryFrameHistogram::reset()
    {
        std::fill( _listBucket.begin(), _listBucket.end(), 0u );
        _totalMs       = 0.0;
        _maxMs         = 0.0f;
        _count         = 0;
        _overflowCount = 0;
    }

    float32 TelemetryFrameHistogram::computePercentileMs( float32 fraction ) const
    {
        if ( _count == 0 )
            return 0.0f;
        // 넓은 쪽 순위 — 100 프레임의 p99 는 99 번째(위에서 둘째), p50 은 50 번째다.
        const uint32 rank       = MathUtil::max( 1u, static_cast<uint32>( MathUtil::ceil( MathUtil::clamp( fraction, 0.0f, 1.0f ) * static_cast<float32>( _count ) ) ) );
        uint32       cumulative = 0;
        for ( uint32 bucketIndex = 0; bucketIndex < kBucketCount; ++bucketIndex )
        {
            cumulative += _listBucket[bucketIndex];
            if ( cumulative >= rank )
                return ( static_cast<float32>( bucketIndex ) + 0.5f ) * kBucketMs;
        }
        return _maxMs; // 넘침 칸 — 가장 긴 프레임으로 답한다
    }

    TelemetryService::TelemetryService()
        : _mutex{}
        , _schema{}
        , _context{}
        , _stats{}
        , _frames{}
        , _settingSubscription{}
        , _listPendingLine{}
        , _listBreadcrumb{}
        , _listEventOccurrence{}
        , _spoolFolder{}
        , _currentScene{}
        , _pSettings{ nullptr }
        , _sessionSeconds{ 0.0 }
        , _lastSubmittedJob{ 0 }
        , _flushTimer{ 0.0f }
        , _sequence{ 0 }
        , _breadcrumbHead{ 0 }
        , _bInitialized{ SW_FALSE }
        , _bConsent{ SW_FALSE }
        , _bSessionSampled{ SW_TRUE }
        , _spoolMutex{}
        , _spoolProgress{}
        , _spoolStats{}
        , _listClosedFile{}
        , _currentFile{}
        , _nullUploader{}
        , _pUploader{ &_nullUploader }
        , _pUploadingWith{ nullptr }
        , _uploadBackoff{ ExponentialBackoff::createWithRandomSeed( 0 ) } // 이 값은 쓰이지 않는다 — 실패가 시작될 때 그 일의 설정 값으로 다시 만든다
        , _uploadRetryAtSeconds{ 0.0 }
        , _currentFileBytes{ 0 }
        , _completedJob{ 0 }
        , _fileIndex{ 0 }
        , _currentFileEvents{ 0 }
        , _spoolWorker{}
    {
        // 스풀 쓰기 · 올리기는 디스크 · 망에서 막히므로 작업 풀이 아니라 전용 스레드다(docs/agents/CppStyle.md — 크래시 보고 올리기와 같은 자리).
        _spoolWorker.start( "TelemetrySpool", DedicatedWorker<TelemetrySpoolJob>::ProcessDelegate::create<&TelemetryService::processSpoolJob>( this ) );
    }

    TelemetryService::~TelemetryService()
    {
        shutdown();
        _spoolWorker.stop(); // shutdown 이 넘긴 일을 모두 기다렸다 — 남은 일이 없다
    }

    void TelemetryService::initialize( string_view spoolFolder, const TelemetryContext& context )
    {
        using Internal          = TelemetryServiceInternal;
        const string folderPath = FileUtil::trimTrailingSlashes( spoolFolder );

        // 지난 실행이 닫지 못하고 남긴 파일까지 이어 받는다 — 동의가 켜지면 올리고, 동의가 꺼지면 `setConsent( false )` 가 지운다. 폴더 읽기는 잠금 밖이다.
        vector<string> listLeftover;
        vector<string> listFile;
        if ( FileUtil::isDirectory( folderPath ) && FileUtil::collectFiles( folderPath, "", listFile, false ) )
        {
            for ( const string& path : listFile )
            {
                if ( Internal::isSpoolFile( path ) )
                    listLeftover.push_back( path );
            }
            VectorUtil::sort( listLeftover, Internal::SpoolFileLess{} );
        }

        ScopedLock<mutex> lock{ _mutex };
        _spoolFolder.set( folderPath );
        _context.set( context );
        _stats = TelemetryStats{};
        _listPendingLine.clear();
        _listBreadcrumb.clear();
        _frames.reset();
        _currentScene.clear();
        _sequence       = 0;
        _breadcrumbHead = 0;
        _sessionSeconds = 0.0;
        _flushTimer     = 0.0f;
        _bConsent       = SW_FALSE;
        _bInitialized   = SW_TRUE;
        _listEventOccurrence.assign( _schema.getEvents().size(), 0u );
        _bSessionSampled = Internal::isSampled( _schema.getSettings()._sessionSampleRate, Internal::toUnit( HashUtil::computeStringHash64( _context->_sessionID ) ) ) ? SW_TRUE : SW_FALSE;

        // 스풀 워커는 쉬고 있다 — 처음이거나, 앞 shutdown 이 넘긴 일을 모두 기다렸다.
        ScopedLock<mutex> spoolLock{ _spoolMutex };
        _spoolStats = TelemetryStats{};
        _listClosedFile.swap( listLeftover );
        _currentFile.clear();
        _currentFileBytes  = 0;
        _currentFileEvents = 0;
        _fileIndex         = 0;
        _uploadBackoff.reset();
        _uploadRetryAtSeconds = 0.0;
    }

    void TelemetryService::shutdown()
    {
        _settingSubscription.reset();
        _pSettings     = nullptr;
        uint64 lastJob = 0;
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _bInitialized == SW_FALSE )
                return;
            if ( _bConsent == SW_TRUE )
            {
                emitSceneSummaryLocked();
                if ( _schema.findEvent( hashed_string( kSessionEndEvent ) ) != nullptr )
                {
                    TelemetryEvent end{ hashed_string( kSessionEndEvent ) };
                    end.setFloat( "seconds", _sessionSeconds );
                    (void)recordLocked( end );
                }
                (void)submitSpoolJobLocked( TelemetrySpoolJobKind::Close );
            }
            _bInitialized = SW_FALSE; // 이 뒤로는 일을 넘기지 않는다
            lastJob       = _lastSubmittedJob;
        }
        // 종료는 마지막 묶음이 디스크 · 끝점에 닿기를 기다린다(기다림은 HTTP 시한까지 — 물러서기 중이면 보내지 않고 다음 실행이 이어 받는다).
        waitSpoolJob( lastJob, nullptr );
        ScopedLock<mutex> lock{ _mutex };
        if ( _bInitialized == SW_FALSE )
        {
            _spoolFolder.reset(); // initialize 가 한 번 정한다 — 다시 시작할 수 있게 비운다
            _context.reset();
        }
    }

    span<const utf8* const> TelemetryService::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { PackEngineSettingsSubsystem::kSubsystemName, UserSettingsManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult TelemetryService::initializeSubsystem()
    {
        // 문맥은 파일마다 첫 줄 — 크래시 보고 · 로그와 같은 세션 id, 심볼과 짝지을 빌드 id.
        const PackEngineSettings& packSettings = engine::getSubsystem<PackEngineSettings>();
        UserSettingsManager&      userSettings = engine::getSubsystem<UserSettingsManager>();
        const string&             engineSchema = engine::getSubsystem<EngineDefaultAssets>()._telemetrySchema.get();
        TelemetryContext          context;
        context._sessionID   = ProcessSession::getID();
        context._buildConfig = build::kConfigName;
        context._platform    = build::kPlatformName;
        context._buildID     = ModuleBuildID::find( nullptr )._id;
        context._game        = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( engine::getSubsystem<GameConfig>()._packRoot.get() ) );
        // 스풀은 사용자 설정 파일 옆이다 — 자동화가 설정 파일 자리를 옮기면(무인 실행 · `-gv_userSettingsFile`) 같이 옮겨 간다.
        const string folder = FileUtil::joinPath( FileUtil::getDirectoryPart( userSettings.getUserFilePath() ), "telemetry" );

        // 스키마가 틀려도 기동은 멈추지 않는다 — 스키마에 없는 사건은 기록되지 않을 뿐이다.
        if ( loadSchema( AssetID{ engineSchema } ) == false )
            SW_LOG_ERROR_ID( "Telemetry.EngineTelemetrySchemaNot", "Engine telemetry schema '%#' is not loaded", engineSchema.c_str() );
        if ( packSettings._telemetrySchema.isSet() )
        {
            if ( loadSchema( AssetID{ packSettings._telemetrySchema.get() } ) == false )
                SW_LOG_ERROR_ID( "Telemetry.GameTelemetrySchemaNot", "Game telemetry schema '%#' is not loaded", packSettings._telemetrySchema.c_str() );
        }
        initialize( folder, context );
        // 동의는 플레이어 옵션(기본 꺼짐)이다. 꺼져 있으면 지난 실행이 남긴 스풀까지 지운다.
        bindConsentSetting( userSettings );

        // 지난 실행의 크래시를 묶는다(덤프 · 로그 폴더 옆의 Saved/CrashReports). 기본 동의(local)는 묶기만 하고, 보낼 것이 있을 때만 보고 프로세스를 띄운다.
        const string crashFolder = engine::getSubsystem<CrashHandler>().getReportFolder();
        if ( crashFolder.empty() == false )
        {
            CrashReportService& crashReports = engine::getSubsystem<CrashReportService>();
            const string        reportsFolder =
                FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( crashFolder ) ), CrashReportService::kReportsFolderName );
            crashReports.initialize( crashFolder, reportsFolder, ProcessSession::getID() );
            crashReports.bindConsentSetting( userSettings );
            crashReports.setReporterExecutable( FileUtil::getExecutablePath() ); // App 이 -crash-reporter 를 알아듣는다(App::initialize 헤드리스 분기)
            (void)crashReports.collectNewCrashes();                              // 모은 수는 쓰지 않는다 — 보고 프로세스가 폴더를 다시 본다
            (void)crashReports.launchReporterProcess();
        }
        return SubsystemInitResult::Succeeded;
    }

    void TelemetryService::shutdownSubsystem()
    {
        engine::getSubsystem<CrashReportService>().shutdown();
        shutdown();
    }

    bool TelemetryService::loadSchema( const AssetID& assetID )
    {
        ScopedLock<mutex> lock{ _mutex };
        const bool        bLoaded = _schema.loadFromAsset( assetID );
        _listEventOccurrence.resize( _schema.getEvents().size(), 0u );
        return bLoaded;
    }

    bool TelemetryService::loadSchemaText( string_view xmlText, string_view sourceName )
    {
        ScopedLock<mutex> lock{ _mutex };
        const bool        bLoaded = _schema.loadFromXMLText( xmlText, sourceName );
        _listEventOccurrence.resize( _schema.getEvents().size(), 0u );
        return bLoaded;
    }

    void TelemetryService::setConsent( bool bEnabled )
    {
        ScopedLock<mutex> lock{ _mutex };
        const bool        bWasEnabled = _bConsent == SW_TRUE;
        _bConsent                     = bEnabled ? SW_TRUE : SW_FALSE;
        if ( _bInitialized == SW_FALSE )
            return;
        if ( bEnabled == false )
        {
            _listPendingLine.clear();
            _frames.reset();
            _currentScene.clear();
            (void)submitSpoolJobLocked( TelemetrySpoolJobKind::Purge ); // 앞서 넘긴 쓰기 뒤에 온다 — 그것들이 남긴 파일까지 지운다
            if ( bWasEnabled )
                SW_LOG_INFO_ID( "Telemetry.TelemetryConsentWithdrawnPending", "Telemetry consent withdrawn - pending events and spool files deleted" );
            return;
        }
        if ( bWasEnabled )
            return;
        SW_LOG_INFO_ID( "Telemetry.TelemetryConsentGivenSession", "Telemetry consent given - session %# (%#)", _context->_sessionID.c_str(), _bSessionSampled == SW_TRUE ? "sampled in" : "sampled out" );
        if ( _schema.findEvent( hashed_string( kSessionStartEvent ) ) != nullptr )
            (void)recordLocked( TelemetryEvent{ hashed_string( kSessionStartEvent ) } );
    }

    bool TelemetryService::hasConsent() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _bConsent == SW_TRUE;
    }

    void TelemetryService::bindConsentSetting( UserSettingsManager& settings )
    {
        _pSettings           = &settings;
        _settingSubscription = settings.registerEventListener( SW_DELEGATE_METHOD( UserSettingEventListener, &TelemetryService::onSettingEvent, this ) ); // 앞 구독은 대입이 푼다
        refreshConsentFromSetting();
    }

    void TelemetryService::onSettingEvent( const UserSettingEvent& event )
    {
        // 보류 값(메뉴에서 바꾸는 중)은 동의가 아니다 — 적용 · 되돌리기 · 로드 뒤의 확정 값만 본다.
        const bool bSettled = event._kind != UserSettingEventKind::PendingChanged && event._kind != UserSettingEventKind::ConfirmStarted;
        const bool bOurs    = event._settingID.empty() || event._settingID == hashed_string( kConsentSettingID );
        if ( bSettled && bOurs )
            refreshConsentFromSetting();
    }

    void TelemetryService::refreshConsentFromSetting()
    {
        if ( _pSettings == nullptr )
            return;
        const string_view value = _pSettings->getAppliedValue( hashed_string( kConsentSettingID ) );
        bool              bConsent{ false };
        setConsent( StringUtil::tryParseBool( value, bConsent ) && bConsent ); // 읽지 못한 값은 동의가 아니다
    }

    void TelemetryService::setUploader( ITelemetryUploader& uploader )
    {
        ScopedLock<mutex> spoolLock{ _spoolMutex };
        _pUploader = &uploader;
    }

    void TelemetryService::releaseUploader( ITelemetryUploader& uploader )
    {
        {
            ScopedLock<mutex> spoolLock{ _spoolMutex };
            if ( _pUploader != &uploader )
                return;
        }
        uint64 lastJob = 0;
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _bInitialized == SW_TRUE && _bConsent == SW_TRUE )
                (void)submitSpoolJobLocked( TelemetrySpoolJobKind::Close );
            lastJob = _lastSubmittedJob;
        }
        waitSpoolJob( lastJob, &uploader );
    }

    void TelemetryService::waitSpoolJob( uint64 jobID, const ITelemetryUploader* pReleasedUploader )
    {
        UniqueLock<mutex> spoolLock{ _spoolMutex };
        while ( _completedJob < jobID || ( pReleasedUploader != nullptr && _pUploadingWith == pReleasedUploader ) )
        {
            _spoolProgress.wait( spoolLock );
        }
        // 기다림과 바꾸기를 한 잠금 안에서 — 그 사이 다른 일이 이 업로더로 보내기 시작할 수 없다.
        if ( pReleasedUploader != nullptr && _pUploader == pReleasedUploader )
            _pUploader = &_nullUploader;
    }

    TelemetryRecordResult TelemetryService::record( const TelemetryEvent& event )
    {
        ScopedLock<mutex> lock{ _mutex };
        return recordLocked( event );
    }

    TelemetryRecordResult TelemetryService::recordLocked( const TelemetryEvent& event )
    {
        if ( _bInitialized == SW_FALSE )
            return TelemetryRecordResult::NotInitialized;
        const TelemetryEventDef* pDef = _schema.findEvent( event.getID() );
        if ( pDef == nullptr )
        {
            ++_stats._rejected;
            SW_LOG_WARNING_ID( "Telemetry.EventNotInSchema", "Telemetry event '%#' is not in the schema - dropped", event.getID().c_str() );
            return TelemetryRecordResult::UnknownEvent;
        }
        // 스키마 대조 — 받는 쪽 테이블과 어긋나는 줄은 쓰지 않는다.
        for ( const TelemetryValue& value : event.getValues() )
        {
            const TelemetryFieldDef* pField   = pDef->findField( value._name );
            const bool               bIntOk   = pField != nullptr && pField->_type == TelemetryFieldType::Float && value._type == TelemetryFieldType::Int;
            const bool               bMatches = pField != nullptr && ( pField->_type == value._type || bIntOk );
            if ( bMatches )
                continue;
            ++_stats._rejected;
            SW_LOG_WARNING_ID( "Telemetry.TelemetryEventFieldUnknown", "Telemetry event '%#' field '%#' is unknown or not a %# - dropped", event.getID().c_str(), value._name.c_str(),
                               pField != nullptr ? toString( pField->_type ) : "declared field" );
            return TelemetryRecordResult::InvalidField;
        }
        for ( const TelemetryFieldDef& field : pDef->_listField )
        {
            if ( field._bRequired == SW_FALSE || event.findValue( field._name ) != nullptr )
                continue;
            ++_stats._rejected;
            SW_LOG_WARNING_ID( "Telemetry.RequiredFieldMissing", "Telemetry event '%#' misses required field '%#' - dropped", event.getID().c_str(), field._name.c_str() );
            return TelemetryRecordResult::InvalidField;
        }

        pushBreadcrumbLocked( event );
        if ( _bConsent == SW_FALSE )
        {
            ++_stats._blockedByConsent;
            return TelemetryRecordResult::NoConsent;
        }
        const size_t defIndex   = static_cast<size_t>( pDef - _schema.getEvents().data() );
        const uint32 occurrence = _listEventOccurrence[defIndex]++;
        if ( _bSessionSampled == SW_FALSE || isEventSampledLocked( *pDef, occurrence ) == false )
        {
            ++_stats._sampledOut;
            return TelemetryRecordResult::SampledOut;
        }
        _listPendingLine.push_back( makeEventLineLocked( event, *pDef ) );
        ++_stats._recorded;
        if ( _listPendingLine.size() >= _schema.getSettings()._batchEvents )
            (void)submitSpoolJobLocked( TelemetrySpoolJobKind::Write );
        return TelemetryRecordResult::Recorded;
    }

    bool TelemetryService::isEventSampledLocked( const TelemetryEventDef& def, uint32 occurrence ) const
    {
        if ( def._sampleRate >= 1.0f )
            return true;
        uint64 hash = HashUtil::computeStringHash64( _context->_sessionID );
        hash        = HashUtil::computeStringHash64( def._id.c_str(), def._id.size(), true, hash );
        hash        = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( &occurrence ), sizeof( occurrence ), false, hash );
        return TelemetryServiceInternal::isSampled( def._sampleRate, TelemetryServiceInternal::toUnit( hash ) );
    }

    string TelemetryService::makeEventLineLocked( const TelemetryEvent& event, const TelemetryEventDef& def ) const
    {
        JSONDocument    doc;
        const JSONValue root = doc.makeObject();
        root.set( "type" ).setString( "event" );
        root.set( "event" ).setString( def._id.c_str() );
        root.set( "seq" ).setUint( _sequence + static_cast<uint64>( _listPendingLine.size() ) );
        root.set( "t" ).setFloat( MathUtil::round( _sessionSeconds * constant::time::kMillisecondsPerSecond ) / constant::time::kMillisecondsPerSecond );
        root.set( "sample" ).setFloat( static_cast<float64>( def._sampleRate ) );
        const JSONValue params = root.set( "fields" );
        params.setObject();
        for ( const TelemetryValue& value : event.getValues() )
        {
            TelemetryServiceInternal::setValueJSON( params.set( value._name.c_str() ), value );
        }
        // 텔레메트리 줄은 구조화 로그와 같은 규칙이다(`JSONWriter`) — 잘못된 UTF-8 은 U+FFFD 로 바꾸고 줄을 잃지 않는다.
        return root.describe();
    }

    void TelemetryService::pushBreadcrumbLocked( const TelemetryEvent& event )
    {
        const uint32 capacity = _schema.getSettings()._breadcrumbCount;
        if ( capacity == 0 )
            return;
        StringBuilder<constant::buffer::kMax512> text;
        text.appendFormat( "%.3f %#", _sessionSeconds, event.getID().c_str() );
        for ( const TelemetryValue& value : event.getValues() )
        {
            text.appendFormat( " %#=", value._name.c_str() );
            TelemetryServiceInternal::appendValueText( text, value );
        }
        // 크래시 때 덤프 옆에 쓰이는 고정 고리에도 — 크래시 보고가 묶는다.
        engine::getSubsystem<CrashHandler>().addBreadcrumb( text.view() );
        if ( _listBreadcrumb.size() < capacity )
        {
            _listBreadcrumb.push_back( text.c_str() );
            return;
        }
        _listBreadcrumb[_breadcrumbHead] = text.c_str();
        _breadcrumbHead                  = ( _breadcrumbHead + 1 ) % static_cast<uint32>( _listBreadcrumb.size() );
    }

    void TelemetryService::collectBreadcrumbs( vector<string>& outListBreadcrumb ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outListBreadcrumb.clear();
        const size_t count = _listBreadcrumb.size();
        for ( size_t offset = 0; offset < count; ++offset )
        {
            outListBreadcrumb.push_back( _listBreadcrumb[( _breadcrumbHead + offset ) % count] );
        }
    }

    void TelemetryService::collectSpoolFiles( vector<string>& outListFilePath ) const
    {
        ScopedLock<mutex> spoolLock{ _spoolMutex };
        outListFilePath = _listClosedFile;
        if ( _currentFile.empty() == false )
            outListFilePath.push_back( _currentFile );
    }

    bool TelemetryService::isInitialized() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _bInitialized == SW_TRUE;
    }

    bool TelemetryService::isSessionSampled() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _bSessionSampled == SW_TRUE;
    }

    TelemetryStats TelemetryService::getStats() const
    {
        TelemetryStats stats;
        {
            ScopedLock<mutex> lock{ _mutex };
            stats = _stats;
        }
        ScopedLock<mutex> spoolLock{ _spoolMutex };
        stats._writtenEvents = _spoolStats._writtenEvents;
        stats._rotatedFiles  = _spoolStats._rotatedFiles;
        stats._droppedFiles  = _spoolStats._droppedFiles;
        stats._uploadedFiles = _spoolStats._uploadedFiles;
        stats._purgedFiles   = _spoolStats._purgedFiles;
        return stats;
    }

    void TelemetryService::update( float32 deltaSeconds )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bInitialized == SW_FALSE || deltaSeconds <= 0.0f )
            return;
        _sessionSeconds += static_cast<float64>( deltaSeconds );
        if ( _bConsent == SW_FALSE )
            return;
        _flushTimer += deltaSeconds;
        if ( _flushTimer < _schema.getSettings()._flushSeconds )
            return;
        _flushTimer = 0.0f;
        (void)submitSpoolJobLocked( TelemetrySpoolJobKind::Write ); // 줄이 없어도 넘긴다 — 실패했던 올리기를 물러서기가 끝나면 다시 한다
    }

    void TelemetryService::recordFrame( string_view sceneID, float32 deltaSeconds )
    {
        ScopedLock<mutex> lock{ _mutex };
        const bool        bTelemetryDisabled = _bInitialized == SW_FALSE || _bConsent == SW_FALSE || deltaSeconds <= 0.0f;
        if ( bTelemetryDisabled )
            return;
        if ( _currentScene != sceneID )
        {
            emitSceneSummaryLocked();
            _currentScene.assign( sceneID.data(), sceneID.size() );
        }
        _frames.add( deltaSeconds * constant::time::kMillisecondsPerSecond );
    }

    void TelemetryService::onAfterSceneTick( const EngineFrameContext& context )
    {
        // 장면별 프레임 시간을 모으고 flush 시간이 되면 스풀 워커에 넘긴다(기록 → 시계 순서). 게임 스레드는 디스크 · 망을 기다리지 않는다.
        recordFrame( context._activeSceneID, context._deltaSeconds );
        update( context._deltaSeconds );
    }

    void TelemetryService::emitSceneSummaryLocked()
    {
        if ( _frames.getCount() > 0 && _schema.findEvent( hashed_string( kSceneSummaryEvent ) ) != nullptr )
        {
            TelemetryEvent summary{ hashed_string( kSceneSummaryEvent ) };
            summary.setString( "scene", _currentScene.empty() ? string_view( "(none)" ) : string_view( _currentScene ) );
            summary.setInt( "frames", _frames.getCount() );
            summary.setFloat( "seconds", _frames.getTotalMs() / constant::time::kMillisecondsPerSecond );
            summary.setFloat( "avgMs", _frames.getTotalMs() / static_cast<float64>( _frames.getCount() ) );
            summary.setFloat( "p50Ms", static_cast<float64>( _frames.computePercentileMs( 0.5f ) ) );
            summary.setFloat( "p99Ms", static_cast<float64>( _frames.computePercentileMs( 0.99f ) ) );
            summary.setFloat( "maxMs", static_cast<float64>( _frames.getMaxMs() ) );
            (void)recordLocked( summary );
        }
        _frames.reset();
    }

    void TelemetryService::flush()
    {
        uint64 lastJob = 0;
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _bInitialized == SW_TRUE && _bConsent == SW_TRUE )
                (void)submitSpoolJobLocked( TelemetrySpoolJobKind::Write );
            lastJob = _lastSubmittedJob;
        }
        waitSpoolJob( lastJob, nullptr );
    }

    uint64 TelemetryService::submitSpoolJobLocked( TelemetrySpoolJobKind kind )
    {
        TelemetrySpoolJob job;
        job._kind          = kind;
        job._jobID         = _lastSubmittedJob + 1;
        job._settings      = _schema.getSettings();
        job._schemaVersion = _schema.getVersion();
        job._nowSeconds    = _sessionSeconds;
        job._listLine.swap( _listPendingLine );
        const uint32 lineCount = static_cast<uint32>( job._listLine.size() );
        // 넘기기는 워커 큐의 잠금 한 번이다 — `_mutex` 를 쥔 채 넘겨 일 id 순서와 큐 순서가 같다.
        if ( _spoolWorker.submit( std::move( job ) ) == false )
        {
            // 워커는 생성자에서 소멸자까지 돈다 — 받지 않았다면 소멸 중의 호출이다(계약 위반).
            SW_LOG_ERROR_ID( "Telemetry.SpoolWorkerRefusedJob", "Telemetry spool worker is not running - %# events dropped", lineCount );
            return _lastSubmittedJob;
        }
        _sequence += lineCount;
        _lastSubmittedJob += 1;
        return _lastSubmittedJob;
    }

    void TelemetryService::processSpoolJob( TelemetrySpoolJob& job, bool bStopping )
    {
        (void)bStopping; // 정지 중에 꺼낸 일도 끝까지 한다(받은 사건을 잃지 않는다) — 소멸자의 stop 앞에서 shutdown 이 이미 기다려 실제로는 남은 일이 없다
        {
            // 디스크 쓰기는 스풀 잠금 아래다 — 이 잠금은 워커와 조회만 잡고 게임 루프(`record` · `update`)는 잡지 않는다.
            ScopedLock<mutex> spoolLock{ _spoolMutex };
            if ( job._kind == TelemetrySpoolJobKind::Purge )
            {
                purgeSpoolLocked();
            }
            else
            {
                writeLinesSpoolLocked( job );
                if ( job._kind == TelemetrySpoolJobKind::Close )
                    closeCurrentFileSpoolLocked();
                enforceLimitsSpoolLocked( job._settings );
            }
        }
        if ( job._kind != TelemetrySpoolJobKind::Purge )
            uploadClosedFiles( job );
        {
            ScopedLock<mutex> spoolLock{ _spoolMutex };
            _completedJob = job._jobID;
        }
        _spoolProgress.notify_all();
    }

    string TelemetryService::makeContextLineSpoolLocked( int32 schemaVersion ) const
    {
        JSONDocument    doc;
        const JSONValue root = doc.makeObject();
        root.set( "type" ).setString( "context" );
        root.set( "schema" ).setInt( schemaVersion );
        root.set( "session" ).setString( _context->_sessionID );
        root.set( "build" ).setString( _context->_buildConfig );
        root.set( "platform" ).setString( _context->_platform );
        root.set( "buildID" ).setString( _context->_buildID );
        root.set( "game" ).setString( _context->_game );
        root.set( "file" ).setUint( _fileIndex );
        return root.describe(); // 이벤트 줄과 같은 규칙(U+FFFD)
    }

    string TelemetryService::makeFilePathSpoolLocked( uint32 fileIndex ) const
    {
        StringBuilder<constant::buffer::kMax256> name;
        name.appendFormat( "%#%#_%#%#", kSpoolFilePrefix, _context->_sessionID.c_str(), Fmt( fileIndex, Format().width( 4 ).zeroPad() ), kSpoolExtension );
        return FileUtil::joinPath( _spoolFolder.get(), name.c_str() );
    }

    void TelemetryService::writeLinesSpoolLocked( const TelemetrySpoolJob& job )
    {
        if ( job._listLine.empty() )
            return;
        if ( FileUtil::ensureDirectoryExists( _spoolFolder.get() ) == false )
        {
            SW_LOG_WARNING_ID( "Telemetry.TelemetrySpoolFolderCannot", "Telemetry spool folder '%#' cannot be created - %# events dropped", _spoolFolder.get().c_str(), static_cast<uint32>( job._listLine.size() ) );
            return;
        }
        const uint64 maxFileBytes = job._settings._maxFileBytes;
        string       chunk;
        for ( const string& line : job._listLine )
        {
            const uint64 lineBytes = line.size() + 1;
            const bool   bFull     = _currentFileEvents > 0 && _currentFileBytes + chunk.size() + lineBytes > maxFileBytes;
            if ( bFull )
            {
                // 이 줄을 넣으면 상한을 넘는다 — 모은 것을 쓰고 파일을 닫은 뒤 새 파일로.
                if ( chunk.empty() == false && FileUtil::appendTextFile( _currentFile, chunk ) )
                    _currentFileBytes += chunk.size();
                chunk.clear();
                closeCurrentFileSpoolLocked();
                ++_spoolStats._rotatedFiles;
            }
            if ( _currentFile.empty() )
            {
                _currentFile       = makeFilePathSpoolLocked( _fileIndex );
                _currentFileBytes  = 0;
                _currentFileEvents = 0;
                chunk += makeContextLineSpoolLocked( job._schemaVersion );
                chunk += '\n';
            }
            chunk += line;
            chunk += '\n';
            ++_currentFileEvents;
            ++_spoolStats._writtenEvents;
        }
        if ( chunk.empty() == false )
        {
            if ( FileUtil::appendTextFile( _currentFile, chunk ) )
                _currentFileBytes += chunk.size();
            else
                SW_LOG_WARNING_ID( "Telemetry.TelemetrySpoolFileCannot", "Telemetry spool file '%#' cannot be written", _currentFile.c_str() );
        }
    }

    void TelemetryService::closeCurrentFileSpoolLocked()
    {
        if ( _currentFile.empty() )
            return;
        _listClosedFile.push_back( _currentFile );
        _currentFile.clear();
        _currentFileBytes  = 0;
        _currentFileEvents = 0;
        ++_fileIndex;
    }

    void TelemetryService::enforceLimitsSpoolLocked( const TelemetryPipelineConfig& settings )
    {
        uint64 totalBytes = _currentFileBytes;
        for ( const string& path : _listClosedFile )
        {
            uint64 byteCount = 0;
            (void)FileUtil::tryGetFileSize( path, byteCount ); // 그 사이 지워진 파일은 차지하는 바이트가 없다
            totalBytes += byteCount;
        }
        // 지금 파일은 지우지 않는다 — 상한을 넘는 만큼 가장 오래된 닫힌 파일부터(보내지 못한 채) 버린다.
        while ( _listClosedFile.empty() == false )
        {
            const uint32 fileCount = static_cast<uint32>( _listClosedFile.size() ) + ( _currentFile.empty() ? 0u : 1u );
            const bool   bOver     = fileCount > settings._maxFiles || totalBytes > settings._maxTotalBytes;
            if ( bOver == false )
                break;
            const string oldest      = _listClosedFile.front();
            uint64       oldestBytes = 0;
            (void)FileUtil::tryGetFileSize( oldest, oldestBytes ); // 그 사이 지워졌으면 뺄 것이 없다
            totalBytes -= MathUtil::min( totalBytes, oldestBytes );
            (void)FileUtil::removeFile( oldest ); // 실패는 removeFile 이 경고로 남긴다
            _listClosedFile.erase( _listClosedFile.begin() );
            ++_spoolStats._droppedFiles;
        }
    }

    void TelemetryService::uploadClosedFiles( const TelemetrySpoolJob& job )
    {
        for ( ;; )
        {
            TelemetryUploadBatch batch;
            ITelemetryUploader*  pUploader = nullptr;
            {
                ScopedLock<mutex> spoolLock{ _spoolMutex };
                if ( _listClosedFile.empty() || job._nowSeconds < _uploadRetryAtSeconds )
                    return; // 보낼 것이 없거나 물러서는 중이다
                batch._filePath = _listClosedFile.front();
                pUploader       = _pUploader;
                _pUploadingWith = pUploader; // `releaseUploader` 가 이 보내기가 끝나길 기다린다
            }
            // 잠금 밖 — 파일 읽기와 보내기. 끝점이 느려도 게임 스레드는 기다리지 않는다(목록을 바꾸는 것은 이 스레드뿐이라 맨 앞 파일은 그대로다).
            batch._sessionID             = _context->_sessionID;
            const bool            bRead  = FileUtil::readTextFile( batch._filePath, batch._content );
            TelemetryUploadResult result = TelemetryUploadResult::Kept;
            if ( bRead )
            {
                const uint32 lineCount = StringUtil::countChar( batch._content, '\n' );
                batch._eventCount      = lineCount > 0 ? lineCount - 1 : 0; // 첫 줄은 문맥
                result                 = pUploader->upload( batch );
            }
            bool bNext = false;
            {
                ScopedLock<mutex> spoolLock{ _spoolMutex };
                _pUploadingWith = nullptr;
                if ( bRead == false )
                {
                    _listClosedFile.erase( _listClosedFile.begin() ); // 지워진 파일 — 목록에서만 뺀다
                    bNext = true;
                }
                else if ( result == TelemetryUploadResult::Sent )
                {
                    _uploadBackoff.reset();
                    (void)FileUtil::removeFile( batch._filePath ); // 실패는 removeFile 이 경고로 남긴다
                    _listClosedFile.erase( _listClosedFile.begin() );
                    ++_spoolStats._uploadedFiles;
                    bNext = true;
                }
                else if ( result == TelemetryUploadResult::Failed )
                {
                    // 실패가 이어지는 동안 두 배씩 물러선다(지터) — 끝점이 죽었을 때 flush 마다 시한만큼 매달리지 않고, 복구 직후 클라이언트가 한꺼번에 몰리지 않는다.
                    const float64 msPerSecond = static_cast<float64>( constant::time::kMillisecondsPerSecond );
                    if ( _uploadBackoff.getNextDelayMs() == 0 )
                        _uploadBackoff = ExponentialBackoff::createWithRandomSeed( static_cast<int64>( static_cast<float64>( job._settings._uploadRetrySeconds ) * msPerSecond ) );
                    const int64 delayMs   = _uploadBackoff.takeDelayMs( static_cast<int64>( static_cast<float64>( job._settings._uploadRetryMaxSeconds ) * msPerSecond ) );
                    _uploadRetryAtSeconds = job._nowSeconds + static_cast<float64>( delayMs ) / msPerSecond;
                }
                // Kept(기본 업로더) — 보내지 않고 둔다. 순서를 지켜 다음 일에 이 파일부터.
            }
            _spoolProgress.notify_all();
            if ( bNext == false )
                return;
        }
    }

    void TelemetryService::purgeSpoolLocked()
    {
        vector<string> listFile = _listClosedFile;
        if ( _currentFile.empty() == false )
            listFile.push_back( _currentFile );
        for ( const string& path : listFile )
        {
            if ( FileUtil::exists( path ) && FileUtil::removeFile( path ) )
                ++_spoolStats._purgedFiles;
        }
        _listClosedFile.clear();
        if ( _currentFile.empty() == false )
            ++_fileIndex;
        _currentFile.clear();
        _currentFileBytes  = 0;
        _currentFileEvents = 0;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( TelemetryService, GameVisible, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        build._pStartupParts->_pTelemetry = &scope.emplaceSubsystem<TelemetryService>();
    } );
} // namespace sw
