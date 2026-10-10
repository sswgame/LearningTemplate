#include "pch.h"

#include "Engine/Telemetry/TelemetryService.h"

#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryUploader.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include <algorithm>
#include <cstdio>

namespace sw
{
    SW_LOG_CALLER( "Telemetry" );

    namespace
    {
        struct TelemetryServiceInternal
        {
            static constexpr const utf8* kArrRecordResultName[] = { "Recorded", "NoConsent", "SampledOut", "NotInitialized", "UnknownEvent", "InvalidField" };
            static constexpr uint32      kUnitMask              = 0xffffffu;

            /** @brief 해시를 [0, 1] 로 — 표본 비교에 씁니다. */
            static float32 toUnit( uint64 hash ) { return static_cast<float32>( static_cast<uint32>( hash ^ ( hash >> 32 ) ) & kUnitMask ) / static_cast<float32>( kUnitMask ); }

            static bool passes( float32 rate, float32 unit ) { return rate >= 1.0f || unit < rate; }

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
                    const uint64 lhsTime = FileUtil::getFileTimestamp( lhs );
                    const uint64 rhsTime = FileUtil::getFileTimestamp( rhs );
                    if ( lhsTime != rhsTime )
                        return lhsTime < rhsTime;
                    return lhs < rhs;
                }
            };

            static bool appendText( const string& path, string_view text )
            {
                FILE* pFile = PlatformFileUtil::openFile( path.c_str(), "ab" );
                if ( pFile == nullptr )
                    return false;
                const size_t written = std::fwrite( text.data(), 1, text.size(), pFile );
                std::fclose( pFile );
                return written == text.size();
            }

            static uint32 countLines( string_view text )
            {
                uint32 count = 0;
                for ( const utf8 character : text )
                {
                    count += character == '\n' ? 1u : 0u;
                }
                return count;
            }

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

            static void appendValueText( StringBuilder<constant::kMaxBuffer512>& text, const TelemetryValue& value )
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
    const utf8* toString( TelemetryRecordResult result )
    {
        return TelemetryServiceInternal::kArrRecordResultName[static_cast<uint32>( result )];
    }

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
        , _settingHandle{}
        , _listPendingLine{}
        , _listBreadcrumb{}
        , _listClosedFile{}
        , _listEventOccurrence{}
        , _spoolFolder{}
        , _currentFile{}
        , _currentScene{}
        , _pSettings{ nullptr }
        , _pUploader{ nullptr }
        , _sessionSeconds{ 0.0 }
        , _flushTimer{ 0.0f }
        , _currentFileBytes{ 0 }
        , _sequence{ 0 }
        , _fileIndex{ 0 }
        , _currentFileEvents{ 0 }
        , _breadcrumbHead{ 0 }
        , _bInitialized{ SW_FALSE }
        , _bConsent{ SW_FALSE }
        , _bSessionSampled{ SW_TRUE }
    {
    }

    TelemetryService::~TelemetryService()
    {
        shutdown();
    }

    void TelemetryService::initialize( string_view spoolFolder, const TelemetryContext& context )
    {
        using Internal = TelemetryServiceInternal;
        std::scoped_lock<mutex> lock{ _mutex };
        _spoolFolder = FileUtil::trimTrailingSlashes( spoolFolder );
        _context     = context;
        _stats       = TelemetryStats{};
        _listPendingLine.clear();
        _listBreadcrumb.clear();
        _listClosedFile.clear();
        _frames.reset();
        _currentScene.clear();
        _currentFile.clear();
        _currentFileBytes  = 0;
        _currentFileEvents = 0;
        _sequence          = 0;
        _fileIndex         = 0;
        _breadcrumbHead    = 0;
        _sessionSeconds    = 0.0;
        _flushTimer        = 0.0f;
        _bConsent          = SW_FALSE;
        _bInitialized      = SW_TRUE;
        _listEventOccurrence.assign( _schema.getEvents().size(), 0u );
        _bSessionSampled = Internal::passes( _schema.getSettings()._sessionSampleRate, Internal::toUnit( StringUtil::computeHash64( _context._sessionID ) ) ) ? SW_TRUE : SW_FALSE;

        // 지난 실행이 닫지 못하고 남긴 파일까지 이어 받는다 — 동의가 켜지면 올리고, 동의가 꺼지면 `setConsent( false )` 가 지운다.
        vector<string> listFile;
        if ( FileUtil::isDirectory( _spoolFolder ) && FileUtil::collectFiles( _spoolFolder, "", listFile, false ) )
        {
            for ( const string& path : listFile )
            {
                if ( Internal::isSpoolFile( path ) )
                    _listClosedFile.push_back( path );
            }
            std::sort( _listClosedFile.begin(), _listClosedFile.end(), Internal::SpoolFileLess{} );
        }
    }

    void TelemetryService::shutdown()
    {
        if ( _pSettings != nullptr )
        {
            _pSettings->unregisterEventListener( _settingHandle );
            _pSettings     = nullptr;
            _settingHandle = DelegateHandle{};
        }
        std::scoped_lock<mutex> lock{ _mutex };
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
            flushLocked();
            closeCurrentFileLocked();
            uploadClosedLocked();
        }
        _bInitialized = SW_FALSE;
    }

    bool TelemetryService::loadSchema( string_view path )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const bool              bLoaded = _schema.loadFromResource( path );
        _listEventOccurrence.resize( _schema.getEvents().size(), 0u );
        return bLoaded;
    }

    bool TelemetryService::loadSchemaText( string_view xmlText, string_view sourceName )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const bool              bLoaded = _schema.loadFromXMLText( xmlText, sourceName );
        _listEventOccurrence.resize( _schema.getEvents().size(), 0u );
        return bLoaded;
    }

    void TelemetryService::setConsent( bool bEnabled )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const bool              bWasEnabled = _bConsent == SW_TRUE;
        _bConsent                           = bEnabled ? SW_TRUE : SW_FALSE;
        if ( _bInitialized == SW_FALSE )
            return;
        if ( bEnabled == false )
        {
            purgeLocked();
            if ( bWasEnabled )
                SW_LOG_INFO( "Telemetry consent withdrawn - pending events and spool files deleted" );
            return;
        }
        if ( bWasEnabled )
            return;
        SW_LOG_INFO( "Telemetry consent given - session %# (%#)", _context._sessionID.c_str(), _bSessionSampled == SW_TRUE ? "sampled in" : "sampled out" );
        if ( _schema.findEvent( hashed_string( kSessionStartEvent ) ) != nullptr )
            (void)recordLocked( TelemetryEvent{ hashed_string( kSessionStartEvent ) } );
    }

    bool TelemetryService::hasConsent() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _bConsent == SW_TRUE;
    }

    void TelemetryService::bindConsentSetting( UserSettingsManager& settings )
    {
        if ( _pSettings != nullptr )
            _pSettings->unregisterEventListener( _settingHandle );
        _pSettings     = &settings;
        _settingHandle = settings.registerEventListener( SW_DELEGATE_METHOD( UserSettingEventListener, &TelemetryService::onSettingEvent, this ) );
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
        setConsent( StringUtil::parseBool( value, false ) );
    }

    void TelemetryService::setUploader( ITelemetryUploader* pUploader )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _pUploader = pUploader;
    }

    TelemetryRecordResult TelemetryService::record( const TelemetryEvent& event )
    {
        std::scoped_lock<mutex> lock{ _mutex };
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
            SW_LOG_WARNING( "Telemetry event '%#' is not in the schema - dropped", event.getID().c_str() );
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
            SW_LOG_WARNING( "Telemetry event '%#' field '%#' is unknown or not a %# - dropped", event.getID().c_str(), value._name.c_str(),
                            pField != nullptr ? toString( pField->_type ) : "declared field" );
            return TelemetryRecordResult::InvalidField;
        }
        for ( const TelemetryFieldDef& field : pDef->_listField )
        {
            if ( field._bRequired == SW_FALSE || event.findValue( field._name ) != nullptr )
                continue;
            ++_stats._rejected;
            SW_LOG_WARNING( "Telemetry event '%#' misses required field '%#' - dropped", event.getID().c_str(), field._name.c_str() );
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
            flushLocked();
        return TelemetryRecordResult::Recorded;
    }

    bool TelemetryService::isEventSampledLocked( const TelemetryEventDef& def, uint32 occurrence ) const
    {
        if ( def._sampleRate >= 1.0f )
            return true;
        uint64 hash = StringUtil::computeHash64( _context._sessionID );
        hash        = StringUtil::computeHash64( def._id.c_str(), def._id.size(), true, hash );
        hash        = StringUtil::computeHash64( reinterpret_cast<const utf8*>( &occurrence ), sizeof( occurrence ), false, hash );
        return TelemetryServiceInternal::passes( def._sampleRate, TelemetryServiceInternal::toUnit( hash ) );
    }

    string TelemetryService::makeEventLineLocked( const TelemetryEvent& event, const TelemetryEventDef& def ) const
    {
        JSONDocument    doc;
        const JSONValue root = doc.makeObject();
        root.set( "type" ).setString( "event" );
        root.set( "event" ).setString( def._id.c_str() );
        root.set( "seq" ).setUint( _sequence + static_cast<uint64>( _listPendingLine.size() ) );
        root.set( "t" ).setFloat( MathUtil::round( _sessionSeconds * 1000.0 ) / 1000.0 );
        root.set( "sample" ).setFloat( static_cast<float64>( def._sampleRate ) );
        const JSONValue fields = root.set( "fields" );
        fields.setObject();
        for ( const TelemetryValue& value : event.getValues() )
        {
            TelemetryServiceInternal::setValueJSON( fields.set( value._name.c_str(), false ), value );
        }
        return doc.dump( -1 );
    }

    string TelemetryService::makeContextLineLocked() const
    {
        JSONDocument    doc;
        const JSONValue root = doc.makeObject();
        root.set( "type" ).setString( "context" );
        root.set( "schema" ).setInt( _schema.getVersion() );
        root.set( "session" ).setString( _context._sessionID );
        root.set( "build" ).setString( _context._buildConfig );
        root.set( "platform" ).setString( _context._platform );
        root.set( "buildId" ).setString( _context._buildID );
        root.set( "game" ).setString( _context._game );
        root.set( "file" ).setUint( _fileIndex );
        return doc.dump( -1 );
    }

    string TelemetryService::makeSpoolFilePathLocked( uint32 fileIndex ) const
    {
        StringBuilder<constant::kMaxBuffer256> name;
        name.appendFormat( "%#%#_%#%#", kSpoolFilePrefix, _context._sessionID.c_str(), Fmt( fileIndex, Format().width( 4 ).zeroPad() ), kSpoolExtension );
        return FileUtil::joinPath( _spoolFolder, name.c_str() );
    }

    void TelemetryService::pushBreadcrumbLocked( const TelemetryEvent& event )
    {
        const uint32 capacity = _schema.getSettings()._breadcrumbCount;
        if ( capacity == 0 )
            return;
        StringBuilder<constant::kMaxBuffer512> text;
        text.appendFormat( "%.3f %#", _sessionSeconds, event.getID().c_str() );
        for ( const TelemetryValue& value : event.getValues() )
        {
            text.appendFormat( " %#=", value._name.c_str() );
            TelemetryServiceInternal::appendValueText( text, value );
        }
        // 크래시 때 덤프 옆에 쓰이는 고정 고리에도 — 크래시 보고가 묶는다.
        CrashHandler::addBreadcrumb( text.view() );
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
        std::scoped_lock<mutex> lock{ _mutex };
        outListBreadcrumb.clear();
        const size_t count = _listBreadcrumb.size();
        for ( size_t offset = 0; offset < count; ++offset )
        {
            outListBreadcrumb.push_back( _listBreadcrumb[( _breadcrumbHead + offset ) % count] );
        }
    }

    void TelemetryService::collectSpoolFiles( vector<string>& outListFilePath ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outListFilePath = _listClosedFile;
        if ( _currentFile.empty() == false )
            outListFilePath.push_back( _currentFile );
    }

    bool TelemetryService::isInitialized() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _bInitialized == SW_TRUE;
    }

    bool TelemetryService::isSessionSampled() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _bSessionSampled == SW_TRUE;
    }

    TelemetryStats TelemetryService::getStats() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _stats;
    }

    void TelemetryService::update( float32 deltaSeconds )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bInitialized == SW_FALSE || deltaSeconds <= 0.0f )
            return;
        _sessionSeconds += static_cast<float64>( deltaSeconds );
        if ( _bConsent == SW_FALSE )
            return;
        _flushTimer += deltaSeconds;
        if ( _flushTimer < _schema.getSettings()._flushSeconds )
            return;
        _flushTimer = 0.0f;
        flushLocked();
    }

    void TelemetryService::recordFrame( string_view sceneID, float32 deltaSeconds )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bInitialized == SW_FALSE || _bConsent == SW_FALSE || deltaSeconds <= 0.0f )
            return;
        if ( _currentScene != sceneID )
        {
            emitSceneSummaryLocked();
            _currentScene.assign( sceneID.data(), sceneID.size() );
        }
        _frames.add( deltaSeconds * 1000.0f );
    }

    void TelemetryService::emitSceneSummaryLocked()
    {
        if ( _frames.getCount() > 0 && _schema.findEvent( hashed_string( kSceneSummaryEvent ) ) != nullptr )
        {
            TelemetryEvent summary{ hashed_string( kSceneSummaryEvent ) };
            summary.setString( "scene", _currentScene.empty() ? string_view( "(none)" ) : string_view( _currentScene ) );
            summary.setInt( "frames", _frames.getCount() );
            summary.setFloat( "seconds", _frames.getTotalMs() / 1000.0 );
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
        std::scoped_lock<mutex> lock{ _mutex };
        flushLocked();
    }

    void TelemetryService::flushLocked()
    {
        using Internal = TelemetryServiceInternal;
        if ( _bInitialized == SW_FALSE || _bConsent == SW_FALSE )
            return;
        if ( _listPendingLine.empty() == false && FileUtil::ensureDirectoryExists( _spoolFolder ) == false )
        {
            SW_LOG_WARNING( "Telemetry spool folder '%#' cannot be created - %# events dropped", _spoolFolder.c_str(), static_cast<uint32>( _listPendingLine.size() ) );
            _listPendingLine.clear();
            return;
        }
        const uint64 maxFileBytes = _schema.getSettings()._maxFileBytes;
        string       chunk;
        for ( const string& line : _listPendingLine )
        {
            const uint64 lineBytes = line.size() + 1;
            const bool   bFull     = _currentFileEvents > 0 && _currentFileBytes + chunk.size() + lineBytes > maxFileBytes;
            if ( bFull )
            {
                // 이 줄을 넣으면 상한을 넘는다 — 모은 것을 쓰고 파일을 닫은 뒤 새 파일로.
                if ( chunk.empty() == false && Internal::appendText( _currentFile, chunk ) )
                    _currentFileBytes += chunk.size();
                chunk.clear();
                closeCurrentFileLocked();
                ++_stats._rotatedFiles;
            }
            if ( _currentFile.empty() )
            {
                _currentFile       = makeSpoolFilePathLocked( _fileIndex );
                _currentFileBytes  = 0;
                _currentFileEvents = 0;
                chunk += makeContextLineLocked();
                chunk += '\n';
            }
            chunk += line;
            chunk += '\n';
            ++_currentFileEvents;
            ++_stats._writtenEvents;
        }
        if ( chunk.empty() == false )
        {
            if ( Internal::appendText( _currentFile, chunk ) )
                _currentFileBytes += chunk.size();
            else
                SW_LOG_WARNING( "Telemetry spool file '%#' cannot be written", _currentFile.c_str() );
        }
        _sequence += static_cast<uint32>( _listPendingLine.size() );
        _listPendingLine.clear();
        enforceCapsLocked();
        uploadClosedLocked();
    }

    void TelemetryService::closeCurrentFileLocked()
    {
        if ( _currentFile.empty() )
            return;
        _listClosedFile.push_back( _currentFile );
        _currentFile.clear();
        _currentFileBytes  = 0;
        _currentFileEvents = 0;
        ++_fileIndex;
    }

    void TelemetryService::enforceCapsLocked()
    {
        const TelemetryPipelineSettings& settings   = _schema.getSettings();
        uint64                           totalBytes = _currentFileBytes;
        for ( const string& path : _listClosedFile )
        {
            totalBytes += FileUtil::getFileSize( path );
        }
        // 지금 파일은 지우지 않는다 — 상한을 넘는 만큼 가장 오래된 닫힌 파일부터(보내지 못한 채) 버린다.
        while ( _listClosedFile.empty() == false )
        {
            const uint32 fileCount = static_cast<uint32>( _listClosedFile.size() ) + ( _currentFile.empty() ? 0u : 1u );
            const bool   bOver     = fileCount > settings._maxFiles || totalBytes > settings._maxTotalBytes;
            if ( bOver == false )
                break;
            const string oldest = _listClosedFile.front();
            totalBytes -= MathUtil::min( totalBytes, FileUtil::getFileSize( oldest ) );
            (void)FileUtil::removeFile( oldest ); // 실패는 removeFile 이 경고로 남긴다
            _listClosedFile.erase( _listClosedFile.begin() );
            ++_stats._droppedFiles;
        }
    }

    void TelemetryService::uploadClosedLocked()
    {
        ITelemetryUploader& uploader = _pUploader != nullptr ? *_pUploader : static_cast<ITelemetryUploader&>( NullTelemetryUploader::get() );
        while ( _listClosedFile.empty() == false )
        {
            TelemetryUploadBatch batch;
            batch._filePath  = _listClosedFile.front();
            batch._sessionID = _context._sessionID;
            if ( FileUtil::readTextFile( batch._filePath, batch._content ) == false )
            {
                _listClosedFile.erase( _listClosedFile.begin() ); // 지워진 파일 — 목록에서만 뺀다
                continue;
            }
            const uint32 lineCount             = TelemetryServiceInternal::countLines( batch._content );
            batch._eventCount                  = lineCount > 0 ? lineCount - 1 : 0; // 첫 줄은 문맥
            const TelemetryUploadResult result = uploader.upload( batch );
            if ( result != TelemetryUploadResult::Sent )
                break;                                     // 보내지 못했다 — 순서를 지켜 다음 flush 에 이 파일부터
            (void)FileUtil::removeFile( batch._filePath ); // 실패는 removeFile 이 경고로 남긴다
            _listClosedFile.erase( _listClosedFile.begin() );
            ++_stats._uploadedFiles;
        }
    }

    void TelemetryService::purgeLocked()
    {
        _listPendingLine.clear();
        _frames.reset();
        _currentScene.clear();
        vector<string> listFile = _listClosedFile;
        if ( _currentFile.empty() == false )
            listFile.push_back( _currentFile );
        for ( const string& path : listFile )
        {
            if ( FileUtil::exists( path ) && FileUtil::removeFile( path ) )
                ++_stats._purgedFiles;
        }
        _listClosedFile.clear();
        if ( _currentFile.empty() == false )
            ++_fileIndex;
        _currentFile.clear();
        _currentFileBytes  = 0;
        _currentFileEvents = 0;
    }
} // namespace sw
