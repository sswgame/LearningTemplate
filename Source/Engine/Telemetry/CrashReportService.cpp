#include "pch.h"

#include "Engine/Telemetry/CrashReportService.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/pair.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/Json/JsonDocument.h"
#include "Engine/Telemetry/CrashReportUploader.h"
#include "Engine/UserSettings/HardwareProbe.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include <algorithm>
#include <cstdio>

namespace sw
{
    SW_LOG_CALLER( "CrashReport" );

    namespace
    {
        struct CrashReportServiceInternal
        {
            static constexpr const utf8* kArrConsentName[] = { "local", "ask", "send" };
            static constexpr const utf8* kArrStateName[]   = { "local", "awaitingDecision", "queued", "sent", "declined" };
            static constexpr uint32      kStateCount       = 5;

            /** @brief 크래시 파일(`crash_<세션>.<확장자>`)과 묶음 안 이름입니다. 컨텍스트 파일이 있어야 크래시로 친다. */
            struct CrashFileKind
            {
                const utf8* _pExtension;
                const utf8* _pBundleName;
            };
            static constexpr CrashFileKind kArrCrashFile[] = {
                {            "txt",             "crash.txt"},
                {            "dmp",             "crash.dmp"},
                {      "stack.txt",       "crash.stack.txt"},
                {"breadcrumbs.txt", "crash.breadcrumbs.txt"},
            };

            [[nodiscard]] static bool parseState( string_view text, CrashReportState& outState )
            {
                for ( uint32 index = 0; index < kStateCount; ++index )
                {
                    if ( StringUtil::equals( text, kArrStateName[index], true ) )
                    {
                        outState = static_cast<CrashReportState>( index );
                        return true;
                    }
                }
                return false;
            }

            static string makeCrashFilePath( const string& folder, string_view sessionID, const utf8* pExtension )
            {
                StringBuilder<constant::kMaxBuffer256> name;
                name.appendFormat( "crash_%#.%#", sessionID, pExtension );
                return FileUtil::joinPath( folder, name.c_str() );
            }

            /** @brief 컨텍스트 파일 이름이면 세션 id 를 꺼냅니다(`crash_<세션>.txt` — `.stack.txt` · `.breadcrumbs.txt` 는 아니다). */
            static bool findSessionOfContextFile( string_view fileName, string& outSessionID )
            {
                const bool bPrefix  = StringUtil::startsWith( fileName, CrashReportService::kBundlePrefix );
                const bool bContext = StringUtil::endsWith( fileName, ".txt" ) && StringUtil::endsWith( fileName, ".stack.txt" ) == false &&
                                      StringUtil::endsWith( fileName, ".breadcrumbs.txt" ) == false;
                if ( bPrefix == false || bContext == false )
                    return false;
                const size_t prefixLength = StringUtil::strlen( CrashReportService::kBundlePrefix );
                outSessionID.assign( fileName.data() + prefixLength, fileName.size() - prefixLength - 4 );
                return outSessionID.empty() == false;
            }

            /** @brief 컨텍스트 파일(`key : value` 줄)을 읽어 @p outRoot 에 넣습니다. */
            static void readContextInto( string_view text, const JsonValue& outContext )
            {
                size_t lineStart = 0;
                while ( lineStart < text.size() )
                {
                    size_t lineEnd = text.find( '\n', lineStart );
                    if ( lineEnd == string_view::npos )
                        lineEnd = text.size();
                    const string_view line  = text.substr( lineStart, lineEnd - lineStart );
                    const size_t      colon = line.find( ':' );
                    if ( colon != string_view::npos )
                    {
                        const string_view key   = StringUtil::trim( line.substr( 0, colon ) );
                        const string_view value = StringUtil::trim( line.substr( colon + 1 ) );
                        if ( key.empty() == false )
                            outContext.set( key, false ).setString( value );
                    }
                    lineStart = lineEnd + 1;
                }
            }

            struct CrashTimeLess
            {
                bool operator()( const CrashReportSummary& lhs, const CrashReportSummary& rhs ) const { return lhs._crashTime < rhs._crashTime; }
            };

            static uint32 countLines( string_view text )
            {
                uint32 count = 0;
                for ( const utf8 character : text )
                {
                    count += character == '\n' ? 1u : 0u;
                }
                return count;
            }

            [[nodiscard]] static bool loadManifest( const string& bundleFolder, JsonDocument& outDocument )
            {
                return outDocument.tryParse( readText( FileUtil::joinPath( bundleFolder, CrashReportService::kManifestFileName ) ) );
            }

            [[nodiscard]] static bool saveManifest( const string& bundleFolder, const JsonDocument& document )
            {
                return document.saveFile( FileUtil::joinPath( bundleFolder, CrashReportService::kManifestFileName ), 2 );
            }

            static string readText( const string& path )
            {
                string text;
                if ( FileUtil::readTextFile( path, text ) == false )
                    text.clear();
                return text;
            }

            static CrashReportState getState( const JsonDocument& manifest )
            {
                CrashReportState state = CrashReportState::Local;
                (void)parseState( manifest.getRoot().get( "state" ).asString(), state ); // 모르는 이름이면 Local 로 둔다 — 보내지 않는 쪽이 안전하다
                return state;
            }

            static void setState( const JsonDocument& manifest, CrashReportState state ) { manifest.getRoot().set( "state" ).setString( toString( state ) ); }

            /** @brief 묶음 폴더들(이름 `crash_*`)입니다. */
            static void collectBundleFolders( const string& reportsFolder, vector<string>& outListFolder )
            {
                outListFolder.clear();
                vector<string> listFolder;
                if ( FileUtil::isDirectory( reportsFolder ) == false || FileUtil::collectFolders( reportsFolder, listFolder, false ) == false )
                    return;
                for ( const string& folder : listFolder )
                {
                    if ( StringUtil::startsWith( FileUtil::getFileNamePart( folder ), CrashReportService::kBundlePrefix ) )
                        outListFolder.push_back( folder );
                }
                std::sort( outListFolder.begin(), outListFolder.end() );
            }

            /** @brief 그 세션의 로그(`*_<세션>.txt`)를 이름 순으로 이어 끝 @p maxBytes 만 남깁니다. */
            static string collectSessionLog( const string& crashFolder, string_view sessionID, uint64 maxBytes )
            {
                vector<string> listFile;
                string         log;
                if ( FileUtil::collectFiles( crashFolder, "", listFile, false ) == false )
                    return log;
                std::sort( listFile.begin(), listFile.end() );
                string suffix( "_" );
                suffix += sessionID;
                suffix += ".txt";
                for ( const string& path : listFile )
                {
                    const string name = FileUtil::getFileNamePart( path );
                    if ( StringUtil::startsWith( name, CrashReportService::kBundlePrefix ) || StringUtil::endsWith( name, suffix ) == false )
                        continue;
                    log += readText( path );
                }
                if ( log.size() <= maxBytes )
                    return log;
                StringBuilder<constant::kMaxBuffer128> header;
                header.appendFormat( "[... %# earlier bytes dropped ...]\n", static_cast<uint64>( log.size() - maxBytes ) );
                return string( header.c_str() ) + log.substr( log.size() - static_cast<size_t>( maxBytes ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( CrashReportConsent consent )
    {
        return CrashReportServiceInternal::kArrConsentName[static_cast<uint32>( consent )];
    }

    const utf8* toString( CrashReportState state )
    {
        return CrashReportServiceInternal::kArrStateName[static_cast<uint32>( state )];
    }

    bool parseCrashReportConsent( string_view text, CrashReportConsent& outConsent )
    {
        for ( uint32 index = 0; index < 3; ++index )
        {
            if ( StringUtil::equals( text, CrashReportServiceInternal::kArrConsentName[index], true ) )
            {
                outConsent = static_cast<CrashReportConsent>( index );
                return true;
            }
        }
        return false;
    }

    CrashReportService::CrashReportService()
        : _crashFolder{}
        , _reportsFolder{}
        , _currentSessionID{}
        , _reporterExecutable{}
        , _settingHandle{}
        , _pSettings{ nullptr }
        , _consent{ CrashReportConsent::Local }
        , _bInitialized{ SW_FALSE }
    {
    }

    CrashReportService::~CrashReportService()
    {
        shutdown();
    }

    void CrashReportService::initialize( string_view crashFolder, string_view reportsFolder, string_view currentSessionID )
    {
        _crashFolder      = FileUtil::trimTrailingSlashes( crashFolder );
        _reportsFolder    = FileUtil::trimTrailingSlashes( reportsFolder );
        _currentSessionID = string( currentSessionID );
        _bInitialized     = SW_TRUE;
    }

    void CrashReportService::shutdown()
    {
        if ( _pSettings != nullptr )
        {
            _pSettings->unregisterEventListener( _settingHandle );
            _pSettings     = nullptr;
            _settingHandle = DelegateHandle{};
        }
        _bInitialized = SW_FALSE;
    }

    void CrashReportService::setConsent( CrashReportConsent consent )
    {
        _consent = consent;
        if ( _bInitialized == SW_TRUE )
            applyConsentToExisting();
    }

    void CrashReportService::bindConsentSetting( UserSettingsManager& settings )
    {
        if ( _pSettings != nullptr )
            _pSettings->unregisterEventListener( _settingHandle );
        _pSettings     = &settings;
        _settingHandle = settings.registerEventListener( SW_DELEGATE_METHOD( UserSettingEventListener, &CrashReportService::onSettingEvent, this ) );
        refreshConsentFromSetting();
    }

    void CrashReportService::onSettingEvent( const UserSettingEvent& event )
    {
        const bool bSettled = event._kind != UserSettingEventKind::PendingChanged && event._kind != UserSettingEventKind::ConfirmStarted;
        const bool bOurs    = event._settingID.empty() || event._settingID == hashed_string( kConsentSettingID );
        if ( bSettled && bOurs )
            refreshConsentFromSetting();
    }

    void CrashReportService::refreshConsentFromSetting()
    {
        if ( _pSettings == nullptr )
            return;
        CrashReportConsent consent = CrashReportConsent::Local;
        if ( parseCrashReportConsent( _pSettings->getAppliedValue( hashed_string( kConsentSettingID ) ), consent ) == false )
            consent = CrashReportConsent::Local; // 모르는 값은 가장 좁은 쪽
        if ( consent != _consent )
            setConsent( consent );
    }

    uint32 CrashReportService::collectNewCrashes()
    {
        using Internal = CrashReportServiceInternal;
        if ( _bInitialized == SW_FALSE || _crashFolder.empty() || FileUtil::isDirectory( _crashFolder ) == false )
            return 0;
        vector<string> listFile;
        if ( FileUtil::collectFiles( _crashFolder, "", listFile, false ) == false )
            return 0;
        uint32 createdCount = 0;
        for ( const string& path : listFile )
        {
            string sessionID;
            if ( Internal::findSessionOfContextFile( FileUtil::getFileNamePart( path ), sessionID ) == false || sessionID == _currentSessionID )
                continue;
            if ( createBundle( sessionID, path ) )
                ++createdCount;
        }
        if ( createdCount > 0 )
            enforceReportCap();
        return createdCount;
    }

    bool CrashReportService::createBundle( string_view sessionID, const string& contextPath )
    {
        using Internal            = CrashReportServiceInternal;
        const string bundleFolder = FileUtil::joinPath( _reportsFolder, string( kBundlePrefix ) + string( sessionID ) );
        if ( FileUtil::isDirectory( bundleFolder ) || FileUtil::ensureDirectoryExists( bundleFolder ) == false )
            return false;

        JsonDocument    manifest;
        const JsonValue root = manifest.makeObject();
        root.set( "schema" ).setInt( 1 );
        root.set( "session" ).setString( sessionID );
        root.set( "crashTime" ).setUint( FileUtil::getFileTimestamp( contextPath ) );
        root.set( "bundledBy" ).setString( _currentSessionID );
        const string    contextText = Internal::readText( contextPath );
        const JsonValue context     = root.set( "context" );
        context.setObject();
        Internal::readContextInto( contextText, context );
        // 자주 보는 칸은 맨 위에 — 받는 쪽 목록 화면이 컨텍스트를 풀지 않고 보인다.
        // 값을 먼저 꺼낸다 — 루트에 칸을 더하면 루트의 멤버 저장소가 옮겨 가 앞서 받은 핸들(context)이 낡는다.
        const string reason   = context.get( "reason" ).asString();
        const string buildID  = context.get( "BuildId" ).asString();
        const string build    = context.get( "Build" ).asString();
        const string platform = context.get( "Platform" ).asString();
        const string rhi      = context.get( "RHI" ).asString();
        const string gpu      = context.get( "GPU" ).asString();
        root.set( "reason" ).setString( reason );
        root.set( "buildId" ).setString( buildID );
        root.set( "build" ).setString( build );
        root.set( "platform" ).setString( platform );
        root.set( "rhi" ).setString( rhi );
        root.set( "gpu" ).setString( gpu );
        // 시스템 정보는 묶는 지금 읽는다 — 같은 기계의 다음 실행이다.
        const HardwareProbeResult hardware = HardwareProbe::probe();
        const JsonValue           system   = root.set( "system" );
        system.setObject();
        system.set( "logicalCores" ).setUint( hardware._logicalCoreCount );
        system.set( "memoryMb" ).setUint( hardware._systemMemoryMb );

        // 크래시 파일은 묶음으로 옮긴다(다시 묶지 않게). 로그는 복사한다 — 로그 폴더의 것은 로그 정리가 맡는다.
        const JsonValue files = root.set( "files" );
        files.setArray();
        uint32 breadcrumbCount = 0;
        for ( const Internal::CrashFileKind& kind : Internal::kArrCrashFile )
        {
            const string source = Internal::makeCrashFilePath( _crashFolder, sessionID, kind._pExtension );
            if ( FileUtil::exists( source ) == false )
                continue;
            const string target = FileUtil::joinPath( bundleFolder, kind._pBundleName );
            if ( FileUtil::copyFile( source, target ) == false )
            {
                SW_LOG_WARNING( "Crash report: '%#' could not be copied into the bundle", source.c_str() );
                continue;
            }
            if ( StringUtil::equals( kind._pExtension, "breadcrumbs.txt" ) )
                breadcrumbCount = Internal::countLines( Internal::readText( target ) );
            (void)FileUtil::removeFile( source ); // 실패는 removeFile 이 경고로 남긴다 — 사본은 이미 묶음에 있다
            const JsonValue entry = files.pushBack();
            entry.setObject();
            entry.set( "name" ).setString( kind._pBundleName );
            entry.set( "bytes" ).setUint( FileUtil::getFileSize( target ) );
        }
        const string log = Internal::collectSessionLog( _crashFolder, sessionID, kMaxLogBytes );
        if ( log.empty() == false && FileUtil::writeTextFile( FileUtil::joinPath( bundleFolder, "last.log" ), log ) )
        {
            const JsonValue entry = files.pushBack();
            entry.setObject();
            entry.set( "name" ).setString( "last.log" );
            entry.set( "bytes" ).setUint( log.size() );
        }
        root.set( "breadcrumbCount" ).setUint( breadcrumbCount );
        root.set( "attempts" ).setUint( 0 );
        CrashReportState state = CrashReportState::Local;
        if ( _consent == CrashReportConsent::Ask )
            state = CrashReportState::AwaitingDecision;
        else if ( _consent == CrashReportConsent::Send )
            state = CrashReportState::Queued;
        Internal::setState( manifest, state );
        if ( Internal::saveManifest( bundleFolder, manifest ) == false )
        {
            SW_LOG_WARNING( "Crash report: manifest of '%#' could not be written", bundleFolder.c_str() );
            return false;
        }
        SW_LOG_INFO( "Crash report: bundled session %# (%#) into '%#' - %#", sessionID, reason.c_str(), bundleFolder.c_str(),
                     toString( state ) );
        return true;
    }

    void CrashReportService::applyConsentToExisting()
    {
        using Internal = CrashReportServiceInternal;
        vector<string> listFolder;
        Internal::collectBundleFolders( _reportsFolder, listFolder );
        for ( const string& folder : listFolder )
        {
            JsonDocument manifest;
            if ( Internal::loadManifest( folder, manifest ) == false )
                continue;
            const CrashReportState state     = Internal::getState( manifest );
            CrashReportState       nextState = state;
            const bool             bWaiting  = state == CrashReportState::AwaitingDecision || state == CrashReportState::Queued;
            if ( _consent == CrashReportConsent::Local && bWaiting )
                nextState = CrashReportState::Local; // 철회 — 보내지 않은 것은 기계 안으로
            else if ( _consent == CrashReportConsent::Send && state == CrashReportState::AwaitingDecision )
                nextState = CrashReportState::Queued;
            if ( nextState == state )
                continue;
            Internal::setState( manifest, nextState );
            (void)Internal::saveManifest( folder, manifest ); // 못 쓰면 옛 상태로 남아 다음 동기화가 같은 전환을 다시 한다
        }
    }

    void CrashReportService::enforceReportCap()
    {
        using Internal = CrashReportServiceInternal;
        vector<string> listFolder;
        Internal::collectBundleFolders( _reportsFolder, listFolder );
        if ( listFolder.size() <= kMaxReports )
            return;
        // 크래시 시각 순으로 — 오래된 것부터 지운다.
        vector<pair<uint64, string>> listByTime;
        for ( const string& folder : listFolder )
        {
            JsonDocument manifest;
            const uint64 crashTime = Internal::loadManifest( folder, manifest ) ? manifest.getRoot().get( "crashTime" ).asUint() : 0u;
            listByTime.push_back( { crashTime, folder } );
        }
        std::sort( listByTime.begin(), listByTime.end() );
        for ( size_t index = 0; index + kMaxReports < listByTime.size(); ++index )
        {
            (void)FileUtil::removeDirectory( listByTime[index].second ); // 실패는 removeDirectory 가 경고로 남기고, 다음 정리가 다시 지운다
        }
    }

    void CrashReportService::collectReports( vector<CrashReportSummary>& outListReport ) const
    {
        using Internal = CrashReportServiceInternal;
        outListReport.clear();
        vector<string> listFolder;
        Internal::collectBundleFolders( _reportsFolder, listFolder );
        for ( const string& folder : listFolder )
        {
            JsonDocument manifest;
            if ( Internal::loadManifest( folder, manifest ) == false )
                continue;
            const JsonValue    root = manifest.getRoot();
            CrashReportSummary summary;
            summary._sessionID = root.get( "session" ).asString();
            summary._folder    = folder;
            summary._reason    = root.get( "reason" ).asString();
            summary._buildID   = root.get( "buildId" ).asString();
            summary._crashTime = root.get( "crashTime" ).asUint();
            summary._attempts  = static_cast<uint32>( root.get( "attempts" ).asUint() );
            summary._state     = Internal::getState( manifest );
            outListReport.push_back( summary );
        }
        std::stable_sort( outListReport.begin(), outListReport.end(), Internal::CrashTimeLess{} );
    }

    bool CrashReportService::decide( string_view sessionID, bool bSend )
    {
        using Internal            = CrashReportServiceInternal;
        const string bundleFolder = FileUtil::joinPath( _reportsFolder, string( kBundlePrefix ) + string( sessionID ) );
        JsonDocument manifest;
        if ( Internal::loadManifest( bundleFolder, manifest ) == false || Internal::getState( manifest ) != CrashReportState::AwaitingDecision )
            return false;
        Internal::setState( manifest, bSend ? CrashReportState::Queued : CrashReportState::Declined );
        return Internal::saveManifest( bundleFolder, manifest );
    }

    uint32 CrashReportService::countPendingUploads() const
    {
        vector<CrashReportSummary> listReport;
        collectReports( listReport );
        uint32 count = 0;
        for ( const CrashReportSummary& report : listReport )
        {
            count += report._state == CrashReportState::Queued && report._attempts < kMaxAttempts ? 1u : 0u;
        }
        return count;
    }

    bool CrashReportService::launchReporterProcess() const
    {
        if ( _reporterExecutable.empty() || countPendingUploads() == 0 )
            return false;
        string command( "\"" );
        command += _reporterExecutable;
        command += "\" ";
        command += kReporterArgument;
        command += "=\"";
        command += _reportsFolder;
        command += "\"";
        const bool bLaunched = Process::launchDetached( command );
        SW_LOG_INFO( "Crash report: %# the reporter process for '%#'", bLaunched ? "launched" : "could not launch", _reportsFolder.c_str() );
        return bLaunched;
    }

    uint32 CrashReportService::runReporter( string_view reportsFolder, ICrashReportUploader& uploader )
    {
        using Internal = CrashReportServiceInternal;
        vector<string> listFolder;
        Internal::collectBundleFolders( FileUtil::trimTrailingSlashes( reportsFolder ), listFolder );
        uint32 sentCount = 0;
        for ( const string& folder : listFolder )
        {
            JsonDocument manifest;
            if ( Internal::loadManifest( folder, manifest ) == false || Internal::getState( manifest ) != CrashReportState::Queued )
                continue;
            const JsonValue root     = manifest.getRoot();
            const uint64    attempts = root.get( "attempts" ).asUint();
            if ( attempts >= kMaxAttempts )
                continue;
            CrashReportUploadBundle bundle;
            bundle._folder        = folder;
            bundle._sessionID     = root.get( "session" ).asString();
            bundle._manifest      = manifest.dump( -1 );
            const JsonValue files = root.get( "files" );
            for ( size_t index = 0; index < files.size(); ++index )
            {
                bundle._listFilePath.push_back( FileUtil::joinPath( folder, files.at( index ).get( "name" ).asString() ) );
            }
            const CrashReportUploadResult result = uploader.upload( bundle );
            if ( result == CrashReportUploadResult::Sent )
            {
                Internal::setState( manifest, CrashReportState::Sent );
                // 받는 쪽이 덤프를 가졌다 — 큰 파일만 지우고 매니페스트 · 글 파일은 "무엇을 보냈나" 로 남긴다.
                (void)FileUtil::removeFile( FileUtil::joinPath( folder, "crash.dmp" ) );
                ++sentCount;
            }
            else
            {
                root.set( "attempts" ).setUint( attempts + 1 );
            }
            (void)Internal::saveManifest( folder, manifest ); // 못 쓰면 옛 상태로 남아 다음 보내기가 다시 시도한다(최소 한 번 전송)
        }
        return sentCount;
    }

} // namespace sw
