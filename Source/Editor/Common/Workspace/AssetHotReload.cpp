#include "pch.h"

#include "Editor/Common/Workspace/AssetHotReload.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorAssetImportJob.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/AutomationScratch.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentAssetPathUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cache/IAssetCache.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    span<const utf8* const> AssetHotReload::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorAssetValidation::kSubsystemName };
        return kArrDependency;
    }

    namespace
    {
        struct AssetHotReloadInternal
        {
            /** @brief 감시 울타리 표지 파일의 이름입니다(`Resource/scratch/` 아래, 저장소에 넣지 않는 폴더). */
            static constexpr const utf8* kFenceFileName = "contentfence.tmp";
            /** @brief 표지 사건을 기다리는 시한(밀리초)입니다. 감시는 보통 몇 ms 안에 알린다. */
            static constexpr int64 kFenceTimeoutMilliseconds = 5000;
            /** @brief 표지 사건 뒤에 사본이 일으킨 임포트가 끝나기를 기다리는 시한(밀리초)입니다. 텍스처 압축은 큰 이미지 하나에 수십 초 걸릴 수 있다. */
            static constexpr int64 kFenceImportTimeoutMilliseconds = 120000;

            /** @brief 에디터 컨텍스트가 없으면 nullptr 입니다(탐침은 에디터 없는 호스트에도 등록돼 있다). 컨텍스트가 있는데 서비스가 없으면 버그라 멈춘다. */
            static AssetHotReload* findHotReload()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? &pContext->getSubsystem<AssetHotReload>() : nullptr;
            }

            /** @brief 탐침 `Editor.AssetImportBusy` — 원본 임포트가 돌거나 기다리면 1 입니다. 시나리오가 임포트가 끝나기를 기다린다. */
            [[nodiscard]] static bool readImportBusy( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AssetHotReload* pHotReload = findHotReload();
                outValue                         = pHotReload != nullptr && pHotReload->isImportBusy() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 탐침 `Editor.AssetImportFinishedCount` — 끝난 원본 임포트의 누적 수입니다(`WaitUntil` 이 임포트가 시작돼 끝난 것을 알아본다). */
            [[nodiscard]] static bool readImportFinishedCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AssetHotReload* pHotReload = findHotReload();
                outValue                         = pHotReload != nullptr ? static_cast<float64>( pHotReload->getFinishedImportCount() ) : 0.0;
                return true;
            }

            /** @brief 울타리 표지 파일의 절대 경로입니다. */
            static string makeFenceMarkerPath() { return FileUtil::joinPath( FileUtil::joinPath( ResourcePaths::getRootFolderPath(), AutomationScratch::kScratchDomain ), kFenceFileName ); }

            /**
             * @brief 감시 사건이 울타리 표지 파일의 것인지 봅니다.
             * @details 사건의 `_directory` 는 감시 루트(리소스 루트)이고 `_filename` 은 그 아래 상대 경로(`scratch\contentfence.tmp`)다(`IFileWatcher::pushChange`).
             *          이름만 견주면 루트 아래 표지는 한 번도 맞지 않아 울타리가 5 초마다 시한 오류를 냈다. 전체 경로로 견준다.
             */
            static bool isFenceMarkerEvent( const FileChangeEvent& changeEvent )
            {
                string_view leafName{};
                FileUtil::getFileNamePart( changeEvent._filename, leafName );
                if ( leafName != kFenceFileName )
                    return false; // 값싼 이름 검사로 대부분의 사건을 거른다
                return FileUtil::normalizePath( FileUtil::joinPath( changeEvent._directory, changeEvent._filename ) ) == FileUtil::normalizePath( makeFenceMarkerPath() );
            }

            /** @brief 시작 차단 조건 — 시나리오 사본 폴더를 새로 만든 뒤 파일 감시가 그 쓰기를 알릴 때까지 참입니다. 에디터가 없으면 막지 않습니다. */
            static bool isFenceBlocking()
            {
                AssetHotReload* pHotReload = findHotReload(); // 에디터 컨텍스트가 없으면 nullptr — 막지 않는다
                return pHotReload != nullptr && pHotReload->pollContentWatchFence();
            }

            /**
             * @brief 실제로 감시할 확장자 목록을 정합니다.
             * @details `editortooldefaults.json` 의 `_listHotReloadExtension` 이 정책이고, 다시 읽을 캐시가 있는 종류(`EditorAssetTypeRegistry`)가 한계입니다.
             *          설정이 비어 있으면 그 종류 전부를 봅니다. 설정에 처리할 수 없는 확장자가 있으면 경고하고 뺍니다.
             */
            static vector<string> resolveWatchExtensions()
            {
                // 반환 객체는 하나다 — 두 지역 변수를 각각 return 하면 NRVO 가 빠진다(`-Wnrvo`).
                vector<string>        listExtension{};
                const vector<string>& listConfigured = getEditorToolDefaults()._listHotReloadExtension;
                if ( listConfigured.empty() )
                {
                    EditorAssetTypeRegistry::appendReloadableSuffixes( listExtension );
                    return listExtension;
                }

                vector<string> listHandled{};
                EditorAssetTypeRegistry::appendReloadableSuffixes( listHandled );
                listExtension.reserve( listConfigured.size() );
                for ( const string& extension : listConfigured )
                {
                    if ( std::any_of( listHandled.begin(), listHandled.end(), [&extension]( const string& handled )
                    { return StringUtil::equals( handled, extension, true ); } ) )
                    {
                        listExtension.push_back( extension );
                        continue;
                    }
                    SW_LOG_WARNING( "Ignoring hot-reload extension '%#' because it has no handler (editortooldefaults.json)", extension.c_str() );
                }
                return listExtension;
            }
        };
    } // namespace

    SW_LOG_CALLER( "AssetHotReload" );

    AssetHotReload::AssetHotReload()
        : _pFileWatchDispatcher{ nullptr }
        , _pImportJob{ make_unique<EditorAssetImportJob>() }
        , _listQueuedImport{}
        , _listRunningImport{}
        , _listDeferredFile{}
        , _uniqueSetQueuedImportKey{}
        , _uniqueSetRunningImportKey{}
        , _uniqueSetDeferredFile{}
        , _bImportInFlight{ false }
        , _finishedImportCount{ 0 }
        , _fenceImportDeadlineNanoseconds{ 0 }
        , _resourceWatchHandle{}
        , _contentWatchHandle{}
        , _contentChangeSerial{ 0 }
        , _listChangedPath{}
        , _bChangedPathsUnknown{ false }
        , _fencePrepareCount{ AutomationScratch::getPrepareCount() }
        , _fenceArmedCount{ 0 }
        , _fenceDeadlineNanoseconds{ 0 }
        , _bFenceWaiting{ false }
        , _bFenceSeen{ false }
    {
    }

    AssetHotReload::~AssetHotReload()
    {
        shutdown();
    }

    bool AssetHotReload::initialize()
    {
        shutdown();

        const vector<string> listExtension = AssetHotReloadInternal::resolveWatchExtensions();
        if ( listExtension.empty() )
        {
            SW_LOG_INFO( "Asset hot reload: no extensions to watch, not enabling it." );
            return false;
        }

        _pFileWatchDispatcher = make_unique<FileWatchDispatcher>();
        if ( _pFileWatchDispatcher->initialize() == false )
        {
            _pFileWatchDispatcher.reset();
            return false;
        }

        FileWatchMatchDelegate fileWatchDelegate{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &AssetHotReload::onResourceFileChanged, this ) };
        // 감시 접두어는 **절대 경로**여야 한다 — 이벤트의 `_directory` 는 감시자가 열어 둔 절대 경로(리소스 루트)이고,
        // 접두어 비교는 그 둘을 그대로 맞춰 본다(상대 경로를 주면 비교가 항상 실패한다).
        _resourceWatchHandle = _pFileWatchDispatcher->registerWatch( ResourcePaths::getRootFolderPath(), listExtension, fileWatchDelegate );
        // 디스크 목록을 가진 화면(콘텐츠 브라우저)이 다시 읽을 때를 알도록, 종류와 무관하게 모든 변경을 센다.
        FileWatchMatchDelegate treeWatchDelegate{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &AssetHotReload::onResourceTreeChanged, this ) };
        _contentWatchHandle = _pFileWatchDispatcher->registerWatch( ResourcePaths::getRootFolderPath(), {}, treeWatchDelegate );
        return _resourceWatchHandle.isValid();
    }

    void AssetHotReload::shutdown()
    {
        // 도는 임포트 잡은 모듈이 내려가기 전에 끝낸다 — 내려간 모듈의 코드를 도는 워커를 남기지 않는다. 진행 중인 원본 하나만 끝까지 가고 나머지는 건너뛴다.
        if ( _pImportJob != nullptr )
        {
            _pImportJob->cancel();
            EditorAssetImportResult discarded{};
            while ( _pImportJob->take( discarded ) == EditorTaskPoll::Running )
            {
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + constant::kNanosecondsPerMillisecond );
            }
        }
        _listQueuedImport.clear();
        _listRunningImport.clear();
        _uniqueSetQueuedImportKey.clear();
        _uniqueSetRunningImportKey.clear();
        _listDeferredFile.clear();
        _uniqueSetDeferredFile.clear();
        _bImportInFlight = false;

        if ( _pFileWatchDispatcher == nullptr )
            return;

        if ( _resourceWatchHandle.isValid() )
            _pFileWatchDispatcher->unregisterWatch( _resourceWatchHandle );
        _resourceWatchHandle = {};
        if ( _contentWatchHandle.isValid() )
            _pFileWatchDispatcher->unregisterWatch( _contentWatchHandle );
        _contentWatchHandle = {};

        _pFileWatchDispatcher->shutdown();
        _pFileWatchDispatcher.reset();
    }

    void AssetHotReload::update()
    {
        if ( _pFileWatchDispatcher != nullptr )
            _pFileWatchDispatcher->update();
        pumpImportJob();
    }

    void AssetHotReload::onResourceTreeChanged( const FileChangeEvent& changeEvent )
    {
        ++_contentChangeSerial;
        if ( AssetHotReloadInternal::isFenceMarkerEvent( changeEvent ) )
        {
            _bFenceSeen = true; // 울타리 표지 — 바뀐 파일로 세지 않는다
            return;
        }
        if ( _bChangedPathsUnknown )
            return;
        // 확장자가 없는 이름은 폴더로 본다. 폴더의 내용 변경(Modified)은 안의 파일 사건이 따로 오므로 버리고, 폴더가 생기거나 사라지거나 이름이 바뀌면 안의 파일을 모르므로 전체를 다시 읽는다.
        const string_view extension = FileUtil::getExtension( changeEvent._filename );
        if ( extension.empty() )
        {
            if ( changeEvent._action != FileWatcherAction::Modified )
                _bChangedPathsUnknown = true;
            return;
        }
        const string resourceID = StringUtil::toLower( ResourcePaths::toResourceID( FileUtil::joinPath( changeEvent._directory, changeEvent._filename ) ).c_str() );
        if ( resourceID.empty() || _listChangedPath.size() >= kMaxTrackedChangedPaths )
        {
            _bChangedPathsUnknown = true;
            return;
        }
        // 목록 크기가 `kMaxTrackedChangedPaths` 로 막혀 있어 선형 검색이 O(256) 으로 끝난다(넘으면 위에서 "모름" 으로 돌아간다).
        if ( std::find( _listChangedPath.begin(), _listChangedPath.end(), resourceID ) == _listChangedPath.end() )
            _listChangedPath.push_back( resourceID );
    }

    bool AssetHotReload::takeChangedPaths( vector<string>& outListPath )
    {
        outListPath.clear();
        const bool bKnown = _bChangedPathsUnknown == false;
        if ( bKnown )
            outListPath = std::move( _listChangedPath );
        _listChangedPath.clear();
        _bChangedPathsUnknown = false;
        return bKnown;
    }

    void AssetHotReload::discardChangedPaths()
    {
        _listChangedPath.clear();
        _bChangedPathsUnknown = false;
    }

    bool AssetHotReload::pollContentWatchFence()
    {
        using Internal = AssetHotReloadInternal;
        if ( _pFileWatchDispatcher == nullptr )
            return false;
        const string markerPath = Internal::makeFenceMarkerPath();
        if ( _bFenceWaiting == false )
        {
            const uint64 prepareCount = AutomationScratch::getPrepareCount();
            if ( prepareCount == _fencePrepareCount )
                return false;
            // 사본을 새로 만들었다 — 감시 트리에 표지를 쓰고 그 사건이 오기를 기다린다.
            _fenceArmedCount = prepareCount;
            _bFenceSeen      = false;
            if ( FileUtil::writeTextFile( markerPath, "fence" ) == false )
            {
                SW_LOG_ERROR( "[ContentWatchFence] could not write the fence marker %# - the content index may be read before the file watch reports the scratch copy", markerPath );
                _fencePrepareCount = prepareCount;
                return false;
            }
            _bFenceWaiting            = true;
            _fenceDeadlineNanoseconds = MonotonicClock::nowNanoseconds() + Internal::kFenceTimeoutMilliseconds * constant::kNanosecondsPerMillisecond;
            return true;
        }
        const bool bTimedOut = MonotonicClock::nowNanoseconds() >= _fenceDeadlineNanoseconds;
        if ( _bFenceSeen == false && bTimedOut == false )
            return true;
        if ( _bFenceSeen == false )
            SW_LOG_ERROR( "[ContentWatchFence] the file watch did not report the fence marker within %# ms - the scenario starts anyway", Internal::kFenceTimeoutMilliseconds );
        else if ( isImportBusy() )
        {
            // 표지 앞의 사본 쓰기가 일으킨 원본 임포트가 워커에서 돈다. 임포트가 끝나기 전에 시작하면 시나리오가 낡은 결과(옛 DDS · 메시)를 읽는다.
            const int64 nowNanoseconds = MonotonicClock::nowNanoseconds();
            if ( _fenceImportDeadlineNanoseconds == 0 )
                _fenceImportDeadlineNanoseconds = nowNanoseconds + Internal::kFenceImportTimeoutMilliseconds * constant::kNanosecondsPerMillisecond;
            if ( nowNanoseconds < _fenceImportDeadlineNanoseconds )
                return true;
            SW_LOG_ERROR( "[ContentWatchFence] the asset import did not finish within %# ms - the scenario starts anyway", Internal::kFenceImportTimeoutMilliseconds );
        }
        _fenceImportDeadlineNanoseconds = 0;
        (void)FileUtil::removeFile( markerPath ); // 표지는 한 번 쓰고 지운다 — 못 지워도 다음 울타리가 덮어쓴다
        _bFenceWaiting     = false;
        _fencePrepareCount = _fenceArmedCount;
        return false;
    }

    void AssetHotReload::onResourceFileChanged( const FileChangeEvent& changeEvent )
    {
        // 저장은 원자적이다(임시 파일 → 이름 바꿔 끼우기, `FileUtil::writeTextFile`) — 그 저장은 Modified 가 아니라 새 이름(RenamedNewName)으로 온다.
        // 바깥 편집기의 "안전한 저장" 도 같다. 새로 생긴 파일(Added)은 아직 가진 캐시가 없어 할 일이 없지만 검증은 돈다.
        const bool bContentChanged = changeEvent._action == FileWatcherAction::Modified || changeEvent._action == FileWatcherAction::RenamedNewName ||
                                     changeEvent._action == FileWatcherAction::Added;
        if ( bContentChanged == false )
            return;

        string relPath{};
        if ( FileUtil::makeRelativePath( ResourcePaths::getRootFolderPath(), FileUtil::joinPath( changeEvent._directory, changeEvent._filename ), relPath ) == false )
            return;

        if ( relPath.empty() )
            return;

        // 워커 임포터의 원본(모델 · 텍스처 · 높이장 원본)은 임포트 잡이 임포트한다 — 에디터 스레드에서 동기로 돌지 않는다. 원본은 임포트가 쓰는 파일이 아니라 바로 검증을 요청한다.
        const AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( relPath );
        if ( route._pfnImportSource != nullptr && route._importThread == AssetImportThread::Worker )
        {
            if ( EditorContext* pContext = editor::findSubsystem<EditorContext>(); pContext != nullptr )
                pContext->getSubsystem<EditorAssetValidation>().requestValidation( relPath );
            requestSourceImport( relPath, route._pfnImportSource );
            return;
        }

        // 임포트가 도는 동안 그 밖의 파일은 미룬다 — 임포트 결과는 여러 파일이라, 쓰는 도중에 읽으면 반쯤 쓴 DDS 나 옆 폴더가 비어 있는 메시를 읽는다.
        if ( isImportBusy() )
        {
            if ( _uniqueSetDeferredFile.insert( relPath ).second )
                _listDeferredFile.push_back( std::move( relPath ) );
            return;
        }
        handleChangedFile( relPath );
    }

    void AssetHotReload::handleChangedFile( const string& relativePath )
    {
        // 바뀐 파일(에디터 저장 · 임포트 산출물 · 바깥 도구의 쓰기)마다 검증 규칙을 돌린다 — 결과는 로그로 남는다.
        if ( EditorContext* pContext = editor::findSubsystem<EditorContext>(); pContext != nullptr )
            pContext->getSubsystem<EditorAssetValidation>().requestValidation( relativePath );

        // 대상이 아닌 파일은 NotTracked, 등록되지 않은 캐시는 Failed(오류를 남겼다) — 어느 쪽이든 감시는 이어 간다.
        if ( reloadChangedAsset( relativePath ) == ReloadOutcome::Failed )
            SW_LOG_WARNING( "Hot reload skipped %# - the running scene keeps the previous content", relativePath.c_str() );
    }

    void AssetHotReload::requestSourceImport( string_view relativePath, AssetSourceImporterFunc pfnImport )
    {
        const string path = FileUtil::normalizeSeparators( relativePath );
        // 같은 원본인지는 구분자를 맞추고 대소문자를 무시한 키로 해시 집합에서 본다. 체크아웃 한 번에 원본이 수천 개 올라오므로 목록을 훑으면 O(N^2) 이다.
        const string key = StringUtil::toLower( path.c_str() );

        // 같은 원본을 지금 임포트하는 중이면 그 잡이 읽은 내용은 낡았다 — 다음 원본 전에 멈추게 하고 이 원본은 아래에서 다시 올린다.
        // (진행 중인 원본 하나는 끝까지 간다. 그 결과 파일은 임포트가 모두 끝날 때까지 읽히지 않는다 — `onResourceFileChanged` 의 미룸.)
        if ( _bImportInFlight && _uniqueSetRunningImportKey.contains( key ) )
            _pImportJob->cancel();
        if ( _uniqueSetQueuedImportKey.insert( key ).second )
            _listQueuedImport.push_back( ImportRequest{ path, pfnImport } );
    }

    void AssetHotReload::pumpImportJob()
    {
        EditorAssetImportResult result{};
        const EditorTaskPoll    poll = _pImportJob->take( result );
        if ( poll == EditorTaskPoll::Running )
            return;
        if ( poll == EditorTaskPoll::Ready )
            finishImport( result );
        else if ( _bImportInFlight )
        {
            SW_LOG_ERROR( "Asset import job lost its result - dropping the %# running source(s)", _listRunningImport.size() );
            _listRunningImport.clear();
            _uniqueSetRunningImportKey.clear();
            _bImportInFlight = false;
        }

        if ( _listQueuedImport.empty() == false )
            startQueuedImport();
        else if ( _bImportInFlight == false )
            flushDeferredFiles();
    }

    void AssetHotReload::startQueuedImport()
    {
        vector<EditorAssetImportItem> listItem;
        listItem.reserve( _listQueuedImport.size() );
        for ( const ImportRequest& request : _listQueuedImport )
        {
            listItem.push_back( EditorAssetImportItem{ request._relativePath, request._pfnImport } );
        }
        const size_t sourceCount = listItem.size();
        if ( _pImportJob->request( std::move( listItem ) ) == false )
        {
            SW_LOG_ERROR( "Asset import job is still running - %# source(s) wait for the next frame", sourceCount );
            return;
        }
        _listRunningImport = std::move( _listQueuedImport );
        _listQueuedImport.clear();
        _uniqueSetRunningImportKey = std::move( _uniqueSetQueuedImportKey );
        _uniqueSetQueuedImportKey.clear();
        _bImportInFlight = true;
        SW_LOG_INFO( "Asset import started: %# changed source(s)", sourceCount );
    }

    void AssetHotReload::finishImport( EditorAssetImportResult& result )
    {
        _bImportInFlight = false;
        size_t skippedCount{ 0 };
        for ( size_t index = 0; index < result._listItem.size(); ++index )
        {
            const EditorAssetImportItemResult& itemResult = result._listItem[index];
            if ( itemResult._bRan == false )
            {
                // 취소로 건너뛴 원본 — 다시 올린다(그 사이 같은 원본이 또 올라왔으면 합쳐진다).
                ++skippedCount;
                if ( index < _listRunningImport.size() )
                    requestSourceImport( _listRunningImport[index]._relativePath, _listRunningImport[index]._pfnImport );
                continue;
            }
            ++_finishedImportCount;
            if ( itemResult._bHandled == false )
            {
                // 임포터가 원본으로 보지 않은 파일 — 옛 길처럼 캐시가 다시 읽는다. 표가 임포터를 달아 둔 파일에서만 일어난다.
                const AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( itemResult._relativePath );
                if ( reloadCaches( route, itemResult._relativePath ) == ReloadOutcome::Failed )
                    SW_LOG_WARNING( "Hot reload skipped %# - the running scene keeps the previous content", itemResult._relativePath.c_str() );
            }
        }
        SW_LOG_INFO( "Asset import finished: %# source(s)%#", result._listItem.size() - skippedCount, result._bCanceled ? " (canceled; the rest runs again)" : "" );
        _listRunningImport.clear();
        _uniqueSetRunningImportKey.clear();
    }

    void AssetHotReload::flushDeferredFiles()
    {
        if ( _listDeferredFile.empty() )
            return;
        vector<string> listFile = std::move( _listDeferredFile );
        _listDeferredFile.clear();
        _uniqueSetDeferredFile.clear();
        for ( const string& relativePath : listFile )
        {
            handleChangedFile( relativePath );
        }
    }

    ReloadOutcome AssetHotReload::reloadChangedAsset( string_view relativePath )
    {
        const AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( relativePath );
        if ( route._pfnImportSource != nullptr )
        {
            // 워커 임포터의 원본은 임포트 잡이 임포트한다(`onResourceFileChanged`). 에디터 스레드에서 동기로 돌리지 않는다.
            if ( route._importThread == AssetImportThread::Worker )
                return ReloadOutcome::NotTracked;
            // 소스를 임포트하는 종류는 임포트하는 것이 리로드다 — 임포트된 결과의 쓰기가 다음 감시 이벤트로 온다(그 결과를 읽는 캐시가 없어도 임포트는 한다).
            if ( route._pfnImportSource( relativePath ) )
                return ReloadOutcome::Reloaded;
        }
        return reloadCaches( route, relativePath );
    }

    ReloadOutcome AssetHotReload::reloadCaches( const AssetReloadRoute& route, string_view relativePath )
    {
        if ( route._pCacheKindName == nullptr )
            return ReloadOutcome::NotTracked;

        // 에디터는 `EngineServices.h` 를 볼 수 없다(모듈 경계). 호스트가 넘겨준 서비스를 쓴다. 에셋 관리자가 없는 실행(헤드리스 도구)은 가진 캐시가 없다.
        AssetManager* pResources = findSubsystem<AssetManager>();
        if ( pResources == nullptr )
            return ReloadOutcome::NotTracked;
        // 디바이스는 **지금 것을** 넘긴다 — 캐시가 마지막으로 본 디바이스는 백엔드 교체 뒤 사라졌을 수 있다.
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        IRHIDevice*    pDevice  = ( pContext != nullptr ) ? pContext->getSubsystem<EditorRenderViews>().getRHIDevice() : nullptr;
        if ( route._pCacheKindName[0] == '\0' )
        {
            // 모듈이 로드한 캐시 — 이름 대신 그 경로를 가진 캐시가 다시 읽는다. 아무도 읽지 않은 파일이면 할 일이 없다(다음에 읽는 쪽이 새 내용을 읽는다).
            if ( reloadInCachesHolding( *pResources, relativePath, pDevice ) == 0 )
                return ReloadOutcome::NotTracked;
        }
        else
        {
            IAssetCache* pCache = pResources->findAssetCache( route._pCacheKindName );
            if ( pCache == nullptr )
            {
                SW_LOG_ERROR( "Hot reload: no asset cache '%#' is registered for %#", route._pCacheKindName, relativePath );
                return ReloadOutcome::Failed;
            }
            SW_LOG_INFO( "Hot-Reloading asset: %#", relativePath );
            pCache->reload( relativePath, pDevice );
        }

        Scene*             pScene   = getActiveScene();
        GameObjectManager* pObjects = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        if ( pObjects != nullptr )
            (void)notifyAssetUsers( *pObjects, relativePath );
        return ReloadOutcome::Reloaded;
    }

    uint32 AssetHotReload::reloadInCachesHolding( AssetManager& resources, string_view relativePath, IRHIDevice* pDevice )
    {
        uint32 reloadedCount{ 0 };
        for ( IAssetCache* pCache : resources.getAllAssetCache() )
        {
            if ( pCache->isCached( relativePath ) == false )
                continue;
            SW_LOG_INFO( "Hot-Reloading asset: %# (%#)", relativePath, pCache->getAssetKindName() );
            pCache->reload( relativePath, pDevice );
            ++reloadedCount;
        }
        return reloadedCount;
    }

    uint32 AssetHotReload::notifyAssetUsers( GameObjectManager& objects, string_view relativePath )
    {
        const string reloaded = FileUtil::normalizeSeparators( relativePath );
        uint32       notified = 0;
        objects.forEachGameObject( [&reloaded, &notified]( GameObject* pObject )
        {
            for ( Component* pComponent : pObject->getComponents() )
            {
                if ( pComponent == nullptr || pComponent->isPendingDestroy() )
                    continue;
                const TypeInfo* pType = pComponent->getTypeInfo();
                if ( pType == nullptr )
                    continue;
                pType->forEachProperty( [pComponent, &reloaded, &notified]( const PropertyInfo& prop )
                {
                    const string_view path = ComponentAssetPathUtil::readAssetPath( prop, pComponent );
                    if ( path.empty() || StringUtil::equals( FileUtil::normalizeSeparators( path ), reloaded, true ) == false )
                        return;
                    pComponent->onPropertyChanged( prop._name );
                    ++notified;
                }, true );
            }
        } );
        return notified;
    }
} // namespace sw::editor

namespace sw::editor
{
    // 사본을 감시가 알리기 전에 시나리오가 콘텐츠 색인을 읽으면 낡은 색인이 맞다고 보인다 — 시작 전에 표지 사건으로 한 번 기다린다(ContentBrowserWatchAndScan CW1).
    SW_AUTOMATION_START_BLOCKER( contentWatchFence, "ContentWatchFence", &AssetHotReloadInternal::isFenceBlocking );

    // 원본 임포트는 워커에서 돈다 — 시나리오가 원본을 쓴 뒤 결과를 읽으려면 임포트가 끝난 것을 알아야 한다. `Busy` 는 0 이 될 때까지, `FinishedCount` 는 시작 값보다 커질 때까지 기다린다.
    SW_AUTOMATION_PROBE( editorAssetImportBusy, "Editor.AssetImportBusy", "1 while a changed source (model, texture, heightfield) is being imported or waits for the import",
                         &AssetHotReloadInternal::readImportBusy );
    SW_AUTOMATION_PROBE( editorAssetImportFinishedCount, "Editor.AssetImportFinishedCount", "Changed sources whose import finished (success or failure) in this editor session",
                         &AssetHotReloadInternal::readImportFinishedCount );
} // namespace sw::editor
