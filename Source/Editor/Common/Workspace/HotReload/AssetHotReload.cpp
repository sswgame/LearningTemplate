#include "pch.h"

#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/AssetReload/ReloadTracker.h"
#include "DevTools/Client/Automation/Scenario/AutomationScratchWorkspace.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorSceneIterationAccess.h"
#include "Editor/Common/Workspace/Asset/EditorAssetImportJob.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/HotReload/AssetReferenceTracker.h"
#include "Editor/Common/Workspace/HotReload/AssetReloadCascade.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentAssetPathUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw::editor
{
    namespace
    {
        struct AssetHotReloadInternal
        {
            /** @brief 파일 감시가 리소스 루트 기준으로 만든 경로를 캐시 · 로더에 넘길 리소스 id 로 바꾸는 한 곳입니다(디스크 → id 경계). */
            [[nodiscard]] static AssetID toAssetID( string_view relativePath ) { return AssetID{ relativePath }; }

            /** @brief 감시 울타리 표지 파일 이름입니다(작업 도메인 폴더 바로 아래 — 이 프로세스의 실행 뿌리 안이라 다른 에디터와 겹치지 않는다). */
            static constexpr const utf8* kFenceFileName = "contentfence.tmp";
            /** @brief 표지 사건을 기다리는 시한(밀리초)입니다. 감시는 보통 몇 ms 안에 알린다. */
            static constexpr int64 kFenceTimeoutMilliseconds = 5000;
            /** @brief 표지 사건 뒤에 사본이 일으킨 임포트가 끝나기를 기다리는 시한(밀리초)입니다. 텍스처 압축은 큰 이미지 하나에 수십 초 걸릴 수 있다. */
            static constexpr int64 kFenceImportTimeoutMilliseconds = 120000;

            /** @brief 에디터 컨텍스트가 없으면 nullptr 입니다(감시 값은 에디터 없는 호스트에도 등록돼 있다). 컨텍스트가 있는데 서비스가 없으면 버그라 멈춘다. */
            static AssetHotReload* findHotReload()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? &pContext->getSubsystem<AssetHotReload>() : nullptr;
            }

            /** @brief 감시 값 `Editor.AssetImportBusy` — 원본 임포트가 돌거나 기다리면 1 입니다. 시나리오가 임포트가 끝나기를 기다린다. */
            [[nodiscard]] static bool readImportBusy( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AssetHotReload* pHotReload = findHotReload();
                outValue                         = pHotReload != nullptr && pHotReload->isImportBusy() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.AssetCascadeReloadCount` — 연쇄로 다시 읽은 에셋의 누적 수입니다(부모를 고친 뒤 자식이 다시 읽혔는지 시나리오가 기다린다). */
            [[nodiscard]] static bool readCascadeReloadCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AssetHotReload* pHotReload = findHotReload();
                outValue                         = pHotReload != nullptr ? static_cast<float64>( pHotReload->getCascadeReloadCount() ) : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.AssetImportFinishedCount` — 끝난 원본 임포트의 누적 수입니다(`WaitUntil` 이 임포트가 시작돼 끝난 것을 알아본다). */
            [[nodiscard]] static bool readImportFinishedCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AssetHotReload* pHotReload = findHotReload();
                outValue                         = pHotReload != nullptr ? static_cast<float64>( pHotReload->getFinishedImportCount() ) : 0.0;
                return true;
            }

            /**
             * @brief 디스크 경로 @p diskPath 의 리소스 id 를 디스크 표기(대소문자 그대로, `/` 구분)로 돌려줍니다 — 감시 사건과 저장이 같은 규칙으로 쓴다. 트리 밖이면 빈 글입니다.
             * @details 리소스 트리는 리소스 루트와 작업 도메인 폴더(실행 뿌리 아래 — `AssetPaths::getScratchFolderPath`) 둘이다. 리소스 루트 안의 `scratch/` 는 작업 도메인의 옛 자리에 남은
             *          찌꺼기라 빈 글이다(그 id 는 실행 뿌리의 사본을 가리킨다). 예전에는 같은 체크아웃의 모든 App 이 `Content/scratch/` 를 나눠 써 남의 사본 사건을 pid 꼬리로 걸렀다.
             */
            static string makeTreePath( string_view diskPath )
            {
                string relative;
                if ( FileUtil::tryMakePathUnderRoot( AssetPaths::getScratchFolderPath(), diskPath, relative ) )
                    return string( AssetPaths::kScratchDomain ) + "/" + relative;
                if ( FileUtil::tryMakePathUnderRoot( editor::getSubsystem<AssetPaths>().getRootFolderPath(), diskPath, relative ) == false ||
                     FileUtil::startsWithPathComponent( FileUtil::normalizePath( relative ).str(), AssetPaths::kScratchDomain ) )
                    return {};
                return relative;
            }

            /** @brief 트리 표기 @p treePath(`makeTreePath`)의 디스크 경로입니다 — 작업 도메인은 실행 뿌리 아래, 나머지는 리소스 루트 아래. */
            static string makeDiskPath( string_view treePath )
            {
                if ( FileUtil::startsWithPathComponent( treePath, AssetPaths::kScratchDomain ) )
                    return FileUtil::joinPath( AssetPaths::getScratchFolderPath(), FileUtil::suffixAfterPathComponent( treePath, AssetPaths::kScratchDomain ) );
                return FileUtil::joinPath( editor::getSubsystem<AssetPaths>().getRootFolderPath(), treePath );
            }

            /** @brief 감시 사건의 트리 표기(`makeTreePath`)입니다. 사건의 `_directory` 는 감시 루트(리소스 루트 · 작업 도메인 폴더)이고 `_filename` 은 그 아래 상대 경로다(`IFileWatcher::pushChange`). */
            static string makeEventPath( const FileChangeEvent& changeEvent ) { return makeTreePath( FileUtil::joinPath( changeEvent._directory, changeEvent._filename ) ); }

            /** @brief 울타리 표지 파일의 절대 경로입니다. */
            static string makeFenceMarkerPath() { return FileUtil::joinPath( AssetPaths::getScratchFolderPath(), kFenceFileName ); }

            /**
             * @brief 감시 사건이 울타리 표지 파일의 것이면 true 입니다 — 바뀐 파일로 세지 않는다(에셋이 아니다).
             * @details 이름만 견주면 다른 폴더의 같은 이름이 맞는다 — 전체 경로로 견준다(디스크 경로라 OS 규칙, 접은 키는 리눅스에서 대소문자만 다른 폴더를 같게 본다).
             */
            static bool isFenceMarkerEvent( const FileChangeEvent& changeEvent )
            {
                string_view leafName{};
                FileUtil::getFileNamePart( changeEvent._filename, leafName );
                return leafName == kFenceFileName && FileUtil::isSameDiskPath( FileUtil::joinPath( changeEvent._directory, changeEvent._filename ), makeFenceMarkerPath() );
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
                    if ( VectorUtil::containsIf( listHandled, [&extension]( const string& handled )
                    { return StringUtil::equals( handled, extension, true ); } ) )
                    {
                        listExtension.push_back( extension );
                        continue;
                    }
                    SW_LOG_WARNING_ID( "AssetHotReload.IgnoringHotReloadExtension", "Ignoring hot-reload extension '%#' because it has no handler (editortooldefaults.json)", extension.c_str() );
                }
                return listExtension;
            }
        };
    } // namespace

    AssetHotReload::AssetHotReload()
        : _pFileWatchDispatcher{ nullptr }
        , _pScratchWatchDispatcher{ nullptr }
        , _pReferenceTracker{ make_unique<AssetReferenceTracker>() }
        , _mapAppliedContentHash{}
        , _cascadeReloadCount{ 0 }
        , _pImportJob{ make_unique<EditorAssetImportJob>() }
        , _listQueuedImport{}
        , _listRunningImport{}
        , _listDeferredFile{}
        , _uniqueSetQueuedImportKey{}
        , _uniqueSetRunningImportKey{}
        , _uniqueSetDeferredFile{}
        , _bImportInFlight{ SW_FALSE }
        , _bChangedPathsUnknown{ SW_FALSE }
        , _bFenceWaiting{ SW_FALSE }
        , _bFenceSeen{ SW_FALSE }
        , _finishedImportCount{ 0 }
        , _fenceImportDeadlineNanoseconds{ 0 }
        , _resourceWatch{}
        , _contentWatch{}
        , _scratchResourceWatch{}
        , _scratchContentWatch{}
        , _contentChangeSerial{ 0 }
        , _listChangedPath{}
        , _pIdentityRules{}
        , _fencePrepareCount{ AutomationScratchWorkspace::getPrepareCount() }
        , _fenceArmedCount{ 0 }
        , _fenceDeadlineNanoseconds{ 0 }
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
            SW_LOG_INFO_ID( "AssetHotReload.AssetHotReloadNo", "Asset hot reload: no extensions to watch, not enabling it." );
            return false;
        }

        _pIdentityRules = make_unique<AssetIdentityRules>();
        if ( _pIdentityRules->load( ProcessRoots::get()._project.str() ) == false )
            _pIdentityRules.reset(); // 이유는 load 가 남겼다 — 색인은 기동 때 것 그대로다

        _pFileWatchDispatcher = make_unique<FileWatchDispatcher>();
        if ( _pFileWatchDispatcher->initialize() == false )
        {
            _pFileWatchDispatcher.reset();
            return false;
        }

        FileWatchMatchDelegate fileWatchDelegate{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &AssetHotReload::onResourceFileChanged, this ) };
        // 감시 접두어는 **절대 경로**여야 한다 — 이벤트의 `_directory` 는 감시자가 열어 둔 절대 경로(리소스 루트)이고,
        // 접두어 비교는 그 둘을 그대로 맞춰 본다(상대 경로를 주면 비교가 항상 실패한다).
        _resourceWatch = _pFileWatchDispatcher->registerWatch( editor::getSubsystem<AssetPaths>().getRootFolderPath(), listExtension, fileWatchDelegate );
        // 디스크 목록을 가진 화면(콘텐츠 브라우저)이 다시 읽을 때를 알도록, 종류와 무관하게 모든 변경을 센다.
        FileWatchMatchDelegate treeWatchDelegate{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &AssetHotReload::onResourceTreeChanged, this ) };
        _contentWatch = _pFileWatchDispatcher->registerWatch( editor::getSubsystem<AssetPaths>().getRootFolderPath(), {}, treeWatchDelegate );
        // 작업 도메인(시나리오 사본 · 가져오기 · 미리보기)은 리소스 루트 밖, 실행 뿌리 아래다 — 디스패처 하나는 루트 하나만 보므로 따로 건다. 감시하려면 폴더가 있어야 한다.
        const string& scratchFolder = AssetPaths::getScratchFolderPath();
        _pScratchWatchDispatcher    = make_unique<FileWatchDispatcher>();
        if ( FileUtil::ensureDirectoryExists( scratchFolder ) && _pScratchWatchDispatcher->initialize( scratchFolder ) )
        {
            _scratchResourceWatch = _pScratchWatchDispatcher->registerWatch( scratchFolder, listExtension, fileWatchDelegate );
            _scratchContentWatch  = _pScratchWatchDispatcher->registerWatch( scratchFolder, {}, treeWatchDelegate );
        }
        else
        {
            _pScratchWatchDispatcher.reset(); // 이유는 ensure · initialize 가 남겼다 — 작업 도메인의 바깥 변경만 다시 읽지 않는다
        }
        return _resourceWatch.isActive();
    }

    void AssetHotReload::shutdown()
    {
        // 도는 임포트 잡은 모듈이 내려가기 전에 끝낸다 — 내려간 모듈의 코드를 도는 워커를 남기지 않는다. 진행 중인 원본 하나만 끝까지 가고 나머지는 건너뛴다.
        if ( _pImportJob != nullptr )
        {
            _pImportJob->cancel();
            EditorAssetImportResult discarded{};
            (void)_pImportJob->waitAndTake( discarded ); // 결과는 버린다 — 끝나는 순간 돌아온다(1 ms 잠자기 폴링을 대신한다)
        }
        _listQueuedImport.clear();
        _listRunningImport.clear();
        _uniqueSetQueuedImportKey.clear();
        _uniqueSetRunningImportKey.clear();
        _listDeferredFile.clear();
        _uniqueSetDeferredFile.clear();
        _bImportInFlight = SW_FALSE;

        if ( _pFileWatchDispatcher == nullptr )
            return;

        _resourceWatch.reset();
        _contentWatch.reset();
        _scratchResourceWatch.reset();
        _scratchContentWatch.reset();

        if ( _pScratchWatchDispatcher != nullptr )
        {
            _pScratchWatchDispatcher->shutdown();
            _pScratchWatchDispatcher.reset();
        }
        _pFileWatchDispatcher->shutdown();
        _pFileWatchDispatcher.reset();
    }

    void AssetHotReload::update()
    {
        if ( _pFileWatchDispatcher != nullptr )
            _pFileWatchDispatcher->update();
        if ( _pScratchWatchDispatcher != nullptr )
            _pScratchWatchDispatcher->update();
        pollImportJob();
        // 감시 사건이 올린 변경 번호를 같은 프레임에 색인에 반영한다(바뀐 파일만 다시 읽거나 전체 스캔).
        _pReferenceTracker->update( _contentChangeSerial, *this );
    }

    const EditorAssetIndex& AssetHotReload::getReferenceIndex() const
    {
        return _pReferenceTracker->getIndex();
    }

    bool AssetHotReload::isReferenceIndexCurrent() const
    {
        return _pReferenceTracker->isCurrent( _contentChangeSerial );
    }

    EditorAssetIndex* AssetHotReload::findCurrentReferenceIndex()
    {
        return _pReferenceTracker->findCurrentIndex( _contentChangeSerial );
    }

    void AssetHotReload::restartPendingReferenceScan()
    {
        _pReferenceTracker->restartPendingScan( _contentChangeSerial, *this );
    }

    bool AssetHotReload::isReferenceScanPending() const
    {
        return _pReferenceTracker->isScanPending();
    }

    void AssetHotReload::onResourceTreeChanged( const FileChangeEvent& changeEvent )
    {
        const string treePath = AssetHotReloadInternal::makeEventPath( changeEvent );
        if ( treePath.empty() )
            return; // 트리 밖 — 리소스 루트 안의 옛 작업 도메인 자리(`Content/scratch/`)에 남은 찌꺼기다
        ++_contentChangeSerial;
        if ( AssetHotReloadInternal::isFenceMarkerEvent( changeEvent ) )
        {
            _bFenceSeen = SW_TRUE; // 울타리 표지 — 바뀐 파일로 세지 않는다
            return;
        }
        if ( _bChangedPathsUnknown )
            return;
        // 확장자가 없는 이름은 폴더로 본다. 폴더의 내용 변경(Modified)은 안의 파일 사건이 따로 오므로 버리고, 폴더가 생기거나 사라지거나 이름이 바뀌면 안의 파일을 모르므로 전체를 다시 읽는다.
        const string_view extension = FileUtil::getExtension( changeEvent._filename );
        if ( extension.empty() )
        {
            if ( changeEvent._action != FileWatcherAction::Modified )
                _bChangedPathsUnknown = SW_TRUE;
            return;
        }
        const string assetID = FileUtil::normalizePath( treePath ).str();
        if ( _listChangedPath.size() >= kMaxTrackedChangedPaths )
        {
            _bChangedPathsUnknown = SW_TRUE;
            return;
        }
        if ( changeEvent._action != FileWatcherAction::Removed && changeEvent._action != FileWatcherAction::RenamedOldName )
            recordIdentity( assetID );
        // 목록 크기가 `kMaxTrackedChangedPaths` 로 막혀 있어 선형 검색이 O(256) 으로 끝난다(넘으면 위에서 "모름" 으로 돌아간다).
        if ( std::find( _listChangedPath.begin(), _listChangedPath.end(), assetID ) == _listChangedPath.end() )
            _listChangedPath.push_back( assetID );
    }

    void AssetHotReload::recordIdentity( const string& assetID ) const
    {
        if ( _pIdentityRules == nullptr )
            return;
        // 원본 곁의 서술이 바뀌면 정체성은 원본의 것이다.
        const bool   bSidecar = StringUtil::endsWith( assetID, AssetMeta::kExtension );
        const string targetID = bSidecar ? assetID.substr( 0, assetID.size() - AssetMeta::kExtension.size() ) : assetID;
        const string absPath  = AssetHotReloadInternal::makeDiskPath( targetID );
        if ( FileUtil::isRegularFile( absPath ) == false )
            return;
        const optional<AssetGUID> guid = _pIdentityRules->readIdentity( absPath, targetID );
        if ( guid.has_value() )
            AssetRegistry::record( *guid, targetID );
    }

    bool AssetHotReload::takeChangedPaths( vector<string>& outListPath )
    {
        outListPath.clear();
        const bool bKnown = _bChangedPathsUnknown == false;
        if ( bKnown )
            outListPath = std::move( _listChangedPath );
        _listChangedPath.clear();
        _bChangedPathsUnknown = SW_FALSE;
        return bKnown;
    }

    void AssetHotReload::discardChangedPaths()
    {
        _listChangedPath.clear();
        _bChangedPathsUnknown = SW_FALSE;
    }

    bool AssetHotReload::pollContentWatchFence()
    {
        using Internal = AssetHotReloadInternal;
        if ( _pScratchWatchDispatcher == nullptr )
            return false; // 사본(작업 도메인)을 보는 감시가 없다 — 기다릴 사건이 오지 않는다
        const string markerPath = Internal::makeFenceMarkerPath();
        if ( _bFenceWaiting == false )
        {
            const uint64 prepareCount = AutomationScratchWorkspace::getPrepareCount();
            if ( prepareCount == _fencePrepareCount )
                return false;
            // 사본을 새로 만들었다 — 감시 트리에 표지를 쓰고 그 사건이 오기를 기다린다.
            _fenceArmedCount = prepareCount;
            _bFenceSeen      = SW_FALSE;
            if ( FileUtil::writeTextFile( markerPath, "fence" ) == false )
            {
                SW_LOG_ERROR_ID( "AssetHotReload.CouldNotWriteFence", "[ContentWatchFence] could not write the fence marker %# - the content index may be read before the file watch reports the scratch copy", markerPath );
                _fencePrepareCount = prepareCount;
                return false;
            }
            _bFenceWaiting            = SW_TRUE;
            _fenceDeadlineNanoseconds = MonotonicClock::nowNanoseconds() + Internal::kFenceTimeoutMilliseconds * constant::time::kNanosecondsPerMillisecond;
            return true;
        }
        const bool bTimedOut = MonotonicClock::nowNanoseconds() >= _fenceDeadlineNanoseconds;
        if ( _bFenceSeen == false && bTimedOut == false )
            return true;
        if ( _bFenceSeen == false )
            SW_LOG_ERROR_ID( "AssetHotReload.FileWatchDidNot", "[ContentWatchFence] the file watch did not report the fence marker within %# ms - the scenario starts anyway", Internal::kFenceTimeoutMilliseconds );
        else if ( isImportBusy() )
        {
            // 표지 앞의 사본 쓰기가 일으킨 원본 임포트가 워커에서 돈다. 임포트가 끝나기 전에 시작하면 시나리오가 낡은 결과(옛 DDS · 메시)를 읽는다.
            const int64 nowNanoseconds = MonotonicClock::nowNanoseconds();
            if ( _fenceImportDeadlineNanoseconds == 0 )
                _fenceImportDeadlineNanoseconds = nowNanoseconds + Internal::kFenceImportTimeoutMilliseconds * constant::time::kNanosecondsPerMillisecond;
            if ( nowNanoseconds < _fenceImportDeadlineNanoseconds )
                return true;
            SW_LOG_ERROR_ID( "AssetHotReload.AssetImportDidNot", "[ContentWatchFence] the asset import did not finish within %# ms - the scenario starts anyway", Internal::kFenceImportTimeoutMilliseconds );
        }
        _fenceImportDeadlineNanoseconds = 0;
        (void)FileUtil::removeFile( markerPath ); // 표지는 한 번 쓰고 지운다 — 못 지워도 다음 울타리가 덮어쓴다
        _bFenceWaiting     = SW_FALSE;
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

        string relPath = AssetHotReloadInternal::makeEventPath( changeEvent ); // 트리 밖(옛 작업 도메인 자리)이면 빈 글
        if ( relPath.empty() )
            return;

        // 워커 임포터의 원본(모델 · 텍스처 · 높이장 원본)은 임포트 잡이 임포트한다 — 에디터 스레드에서 동기로 돌지 않는다.
        const AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( relPath );
        if ( route._pfnImportSource != nullptr && route._importThread == AssetImportThread::Worker )
        {
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
        if ( isAlreadyApplied( relativePath ) )
            return; // 에디터가 저장하며 이미 반영했다(`applySavedAsset`) — 같은 저장의 늦은 사건이다
        applyChangedFile( relativePath );
    }

    void AssetHotReload::applyChangedFile( const string& relativePath )
    {
        // 사건을 꺼낸 때 파일이 이미 없으면(이름을 바꾸고 지운 사본 폴더의 늦은 "바뀜" 사건) 다시 읽을 내용이 없다 — 지운 사건이다.
        // 캐시의 다시 읽기는 파일을 디스크에서 읽어 `File.NotFound` 를 남겼다(시나리오 `assetmanage` 다음 단계에서 났다).
        if ( FileUtil::exists( AssetHotReloadInternal::makeDiskPath( relativePath ) ) == false )
        {
            SW_LOG_INFO_ID( "AssetHotReload.ChangedFileGone", "Hot reload skipped %# - the file was removed before its change was handled", relativePath.c_str() );
            return;
        }
        // 대상이 아닌 파일은 NotTracked, 등록되지 않은 캐시는 Failed(오류를 남겼다) — 어느 쪽이든 감시는 이어 간다.
        if ( reloadChangedAsset( AssetHotReloadInternal::toAssetID( relativePath ) ) == ReloadResult::Failed )
            SW_LOG_WARNING_ID( "AssetHotReload.HotReloadSkippedRunning", "Hot reload skipped %# - the running scene keeps the previous content", relativePath.c_str() );
        reloadDependents( relativePath );
    }

    void AssetHotReload::applySavedAsset( string_view assetID )
    {
        // 문서 경로는 리소스 id 이거나 디스크 절대 경로(시나리오의 `Saved/Automation/` 임시 문서)다 — 디스크 경로를 리소스 id 변환에 넘기면 오류(`AssetPaths.WriteNotResourceID`)다.
        if ( AbsolutePath::isStrictAbsolute( assetID ) )
        {
            (void)reloadChangedAsset( AssetID{ assetID } );
            return;
        }
        const optional<AbsolutePath> diskPath     = editor::getSubsystem<AssetPaths>().toDiskPath( assetID );
        const string                 relativePath = diskPath.has_value() ? AssetHotReloadInternal::makeTreePath( diskPath->str() ) : string{};
        if ( relativePath.empty() )
        {
            // 트리(리소스 루트 · 작업 도메인) 밖(시나리오의 `Saved/Automation/` 임시 문서 · 붙인 디스크 도메인)은 감시도 색인도 없다 — 그 캐시만 다시 읽는다(예전 패널의 저장 뒤 리로드와 같다).
            (void)reloadChangedAsset( AssetID{ assetID } );
            return;
        }
        const uint64 contentHash = ContentHash::computeFile( diskPath->str() );
        if ( contentHash != 0 )
            _mapAppliedContentHash[FileUtil::normalizePath( relativePath ).str()] = contentHash;
        applyChangedFile( relativePath );
    }

    bool AssetHotReload::isAlreadyApplied( const string& relativePath )
    {
        const auto itApplied = _mapAppliedContentHash.find( FileUtil::normalizePath( relativePath ).str() );
        if ( itApplied == _mapAppliedContentHash.end() )
            return false;
        const bool bSame = ContentHash::computeFile( AssetHotReloadInternal::makeDiskPath( relativePath ) ) == itApplied->second;
        if ( bSame == false )
            _mapAppliedContentHash.erase( itApplied ); // 그 뒤에 바깥에서 또 바뀌었다 — 반영한다
        return bSame;
    }

    void AssetHotReload::reloadDependents( const string& relativePath )
    {
        const string assetID = FileUtil::normalizePath( relativePath ).str();
        // 펼쳐 넣는 종류면 바뀐 파일의 참조를 먼저 맞춘다 — 부모를 바꿨으면 새 사슬로 순환을 본다(감시가 올린 번호의 증분 갱신은 다음 `update` 라 늦다).
        // 그 파일의 줄만 고치므로(`EditorAssetIndex::applyChanges`) 체크아웃 한 번에 수천 개여도 표 전체를 정렬하지 않는다. 새 파일도 넣어도 된다 — 대상의 있음은 물을 때 본다.
        if ( shouldInlineReferences( assetID ) )
            _pReferenceTracker->refreshFile( assetID );
        const EditorAssetIndex& index = _pReferenceTracker->getIndex();
        if ( index.isReady() == false )
        {
            SW_LOG_INFO_ID( "AssetHotReload.CascadeIndexNotReady", "Reference index is still scanning - assets that inline %# are not reloaded for this change", assetID.c_str() );
            return;
        }
        vector<string> listDependent;
        if ( AssetReloadCascade::planDependents( index, assetID, &AssetHotReload::shouldInlineReferences, listDependent ) == false )
            return; // 순환 — 계획이 오류를 남겼다
        for ( const string& dependentID : listDependent )
        {
            const AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( dependentID );
            const AssetID          dependentAssetID{ dependentID };
            if ( isHeldInCaches( route, dependentAssetID ) == false )
                continue; // 로드되지 않았다 — 다음에 읽는 쪽이 새 내용으로 펼친다
            SW_LOG_INFO_ID( "AssetHotReload.CascadeReload", "Cascade reload: %# (inlines %#)", dependentID.c_str(), assetID.c_str() );
            if ( reloadCaches( route, dependentAssetID ) == ReloadResult::Reloaded )
                ++_cascadeReloadCount;
        }
    }

    bool AssetHotReload::shouldInlineReferences( string_view assetID )
    {
        return EditorAssetTypeRegistry::findReloadRoute( assetID )._referenceUse == AssetReferenceUse::InlinedAtLoad;
    }

    bool AssetHotReload::isHeldInCaches( const AssetReloadRoute& route, const AssetID& assetID )
    {
        AssetManager* pResources = findSubsystem<AssetManager>();
        if ( route._pCacheKindName == nullptr || pResources == nullptr )
            return false;
        if ( route._pCacheKindName[0] != '\0' )
        {
            const AssetCacheBase* pCache = pResources->findAssetCache( route._pCacheKindName );
            return pCache != nullptr && pCache->isCached( assetID.view() );
        }
        for ( AssetCacheBase* pCache : pResources->getAllAssetCache() )
        {
            if ( pResources->findReloadFunction( pCache ) != nullptr && pCache->isCached( assetID.view() ) )
                return true;
        }
        return false;
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

    void AssetHotReload::pollImportJob()
    {
        EditorAssetImportResult result{};
        const EditorTaskPoll    poll = _pImportJob->take( result );
        if ( poll == EditorTaskPoll::Running )
            return;
        if ( poll == EditorTaskPoll::Ready )
            finishImport( result );
        else if ( _bImportInFlight )
        {
            SW_LOG_ERROR_ID( "AssetHotReload.AssetImportJobLost", "Asset import job lost its result - dropping the %# running source(s)", _listRunningImport.size() );
            _listRunningImport.clear();
            _uniqueSetRunningImportKey.clear();
            _bImportInFlight = SW_FALSE;
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
        if ( _pImportJob->request( std::move( listItem ), editor::getSubsystem<AssetPaths>() ) == false ) // 경로 상태는 여기(에디터 스레드)서 찾아 워커에 넘긴다
        {
            SW_LOG_ERROR_ID( "AssetHotReload.AssetImportJobStill", "Asset import job is still running - %# source(s) wait for the next frame", sourceCount );
            return;
        }
        _listRunningImport = std::move( _listQueuedImport );
        _listQueuedImport.clear();
        _uniqueSetRunningImportKey = std::move( _uniqueSetQueuedImportKey );
        _uniqueSetQueuedImportKey.clear();
        _bImportInFlight = SW_TRUE;
        SW_LOG_INFO_ID( "AssetHotReload.AssetImportStartedChanged", "Asset import started: %# changed source(s)", sourceCount );
    }

    void AssetHotReload::finishImport( EditorAssetImportResult& result )
    {
        _bImportInFlight = SW_FALSE;
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
                if ( reloadCaches( route, AssetHotReloadInternal::toAssetID( itemResult._relativePath ) ) == ReloadResult::Failed )
                    SW_LOG_WARNING_ID( "AssetHotReload.HotReloadSkippedRunning", "Hot reload skipped %# - the running scene keeps the previous content", itemResult._relativePath.c_str() );
            }
        }
        SW_LOG_INFO_ID( "AssetHotReload.AssetImportFinishedSource", "Asset import finished: %# source(s)%#", result._listItem.size() - skippedCount, result._bCanceled ? " (canceled; the rest runs again)" : "" );
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

    ReloadResult AssetHotReload::reloadChangedAsset( const AssetID& assetID )
    {
        const string_view      relativePath = assetID.view();
        const AssetReloadRoute route        = EditorAssetTypeRegistry::findReloadRoute( relativePath );
        if ( route._pfnImportSource != nullptr )
        {
            // 워커 임포터의 원본은 임포트 잡이 임포트한다(`onResourceFileChanged`). 에디터 스레드에서 동기로 돌리지 않는다.
            if ( route._importThread == AssetImportThread::Worker )
                return ReloadResult::NotTracked;
            // 소스를 임포트하는 종류는 임포트하는 것이 리로드다 — 임포트된 결과의 쓰기가 다음 감시 이벤트로 온다(그 결과를 읽는 캐시가 없어도 임포트는 한다).
            if ( route._pfnImportSource( editor::getSubsystem<AssetPaths>(), relativePath ) )
            {
                if ( ReloadTracker* const pTracker = findSubsystem<ReloadTracker>() )
                    pTracker->record( ReloadSource::Asset, relativePath, ReloadResult::Reloaded );
                return ReloadResult::Reloaded;
            }
        }
        return reloadCaches( route, assetID );
    }

    ReloadResult AssetHotReload::reloadCaches( const AssetReloadRoute& route, const AssetID& assetID )
    {
        const string_view relativePath = assetID.view();
        if ( route._pCacheKindName == nullptr )
            return ReloadResult::NotTracked;

        // 에디터는 `EngineServices.h` 를 볼 수 없다(모듈 경계). 호스트가 넘겨준 서비스를 쓴다. 에셋 관리자가 없는 실행(헤드리스 도구)은 가진 캐시가 없다.
        AssetManager* pResources = findSubsystem<AssetManager>();
        if ( pResources == nullptr )
            return ReloadResult::NotTracked;
        if ( route._pCacheKindName[0] == '\0' )
        {
            // 모듈이 로드한 캐시 — 이름 대신 그 경로를 가진 캐시가 다시 읽는다. 아무도 읽지 않은 파일이면 할 일이 없다(다음에 읽는 쪽이 새 내용을 읽는다).
            if ( reloadInCachesHolding( *pResources, assetID ) == 0 )
                return ReloadResult::NotTracked;
        }
        else
        {
            AssetCacheBase* pCache = pResources->findAssetCache( route._pCacheKindName );
            if ( pCache == nullptr )
            {
                SW_LOG_ERROR_ID( "AssetHotReload.HotReloadNoAsset", "Hot reload: no asset cache '%#' is registered for %#", route._pCacheKindName, relativePath );
                if ( ReloadTracker* const pTracker = findSubsystem<ReloadTracker>() )
                    pTracker->record( ReloadSource::Asset, relativePath, ReloadResult::Failed, "no asset cache is registered for its kind" );
                return ReloadResult::Failed;
            }
            const AssetCacheReloadFunction pReload = pResources->findReloadFunction( pCache );
            if ( pReload == nullptr )
            {
                SW_LOG_ERROR_ID( "AssetHotReload.HotReloadAssetCache", "Hot reload: asset cache '%#' has no reload function (registered without SW_ASSET_CACHE_RELOAD_FUNCTION) - %# is not reloaded", route._pCacheKindName, relativePath );
                if ( ReloadTracker* const pTracker = findSubsystem<ReloadTracker>() )
                    pTracker->record( ReloadSource::Asset, relativePath, ReloadResult::Failed, "its asset cache has no reload function" );
                return ReloadResult::Failed;
            }
            SW_LOG_INFO_ID( "AssetHotReload.HotReloadingAsset", "Hot-Reloading asset: %#", relativePath );
            pReload( *pCache, assetID );
        }

        Scene*             pScene   = getActiveScene();
        GameObjectManager* pObjects = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        if ( pObjects != nullptr )
            (void)notifyAssetUsers( *pObjects, relativePath );
        if ( ReloadTracker* const pTracker = findSubsystem<ReloadTracker>() )
            pTracker->record( ReloadSource::Asset, relativePath, ReloadResult::Reloaded );
        return ReloadResult::Reloaded;
    }

    uint32 AssetHotReload::reloadInCachesHolding( AssetManager& resources, const AssetID& assetID )
    {
        const string_view relativePath = assetID.view();
        uint32            reloadedCount{ 0 };
        for ( AssetCacheBase* pCache : resources.getAllAssetCache() )
        {
            const AssetCacheReloadFunction pReload = resources.findReloadFunction( pCache );
            if ( pReload == nullptr || pCache->isCached( relativePath ) == false )
                continue;
            SW_LOG_INFO_ID( "AssetHotReload.HotReloadingAsset2", "Hot-Reloading asset: %# (%#)", relativePath, pCache->getAssetKindName() );
            pReload( *pCache, assetID );
            ++reloadedCount;
        }
        return reloadedCount;
    }

    uint32 AssetHotReload::notifyAssetUsers( GameObjectManager& objects, string_view relativePath )
    {
        const string reloaded = FileUtil::normalizeSeparators( relativePath );
        uint32       notified = 0;
        objects.forEachGameObject( editor::EditorSceneIterationAccess::getKey(), [&reloaded, &notified]( GameObject* pObject )
        {
            for ( Component* pComponent : pObject->getComponents() )
            {
                if ( pComponent == nullptr || pComponent->isPendingDestroy() )
                    continue;
                ComponentAssetPathUtil::forEachAssetPath( *pComponent, SW_DELEGATE_LAMBDA( AssetPathVisitor, [pComponent, &reloaded, &notified]( const AssetPathValue& value )
                {
                    const string_view path = value.getPath();
                    if ( path.empty() || StringUtil::equals( FileUtil::normalizeSeparators( path ), reloaded, true ) == false )
                        return;
                    pComponent->onPropertyChanged( value._pTopProperty->_name ); // 중첩 구조체 안의 경로면 그것을 품은 맨 위 칸으로 알린다
                    ++notified;
                } ) );
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
    SW_AUTOMATION_WALL_WATCH( editorAssetImportBusy, "Editor.AssetImportBusy", "1 while a changed source (model, texture, heightfield) is being imported or waits for the import",
                              &AssetHotReloadInternal::readImportBusy );
    SW_AUTOMATION_WALL_WATCH( editorAssetImportFinishedCount, "Editor.AssetImportFinishedCount", "Changed sources whose import finished (success or failure) in this editor session",
                              &AssetHotReloadInternal::readImportFinishedCount );
    // 연쇄 리로드는 파일 감시가 부모 저장을 알린 뒤에 돈다 — 감시는 바깥 일(OS 파일 감시)이라 벽시계로 기다린다(`SW_AUTOMATION_WALL_WATCH`).
    SW_AUTOMATION_WALL_WATCH( editorAssetCascadeReloadCount, "Editor.AssetCascadeReloadCount", "Assets reloaded because an asset they inline at load (a _base parent) changed",
                              &AssetHotReloadInternal::readCascadeReloadCount );
} // namespace sw::editor
