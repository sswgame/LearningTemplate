#include "pch.h"

#include "Editor/Common/Workspace/AssetHotReload.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
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

            /** @brief 시작 차단 조건 — 시나리오 사본 폴더를 새로 만든 뒤 파일 감시가 그 쓰기를 알릴 때까지 참입니다. 에디터가 없으면 막지 않습니다. */
            static bool isFenceBlocking()
            {
                EditorContext*  pContext   = EditorContext::get();
                AssetHotReload* pHotReload = pContext != nullptr ? pContext->findSubsystem<AssetHotReload>() : nullptr;
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
    }

    void AssetHotReload::onResourceTreeChanged( const FileChangeEvent& changeEvent )
    {
        ++_contentChangeSerial;
        if ( changeEvent._filename == AssetHotReloadInternal::kFenceFileName )
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
        const string markerPath = FileUtil::joinPath( FileUtil::joinPath( ResourcePaths::getRootFolderPath(), AutomationScratch::kScratchDomain ), Internal::kFenceFileName );
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

        // 바뀐 파일(에디터 저장 · 임포트 산출물 · 바깥 도구의 쓰기)마다 검증 규칙을 돌린다 — 결과는 로그로 남는다.
        if ( EditorContext* pContext = EditorContext::get(); pContext != nullptr )
            pContext->getSubsystem<EditorAssetValidation>().requestValidation( relPath );

        // 대상이 아닌 파일은 NotTracked, 등록되지 않은 캐시는 Failed(오류를 남겼다) — 어느 쪽이든 감시는 이어 간다.
        if ( reloadChangedAsset( relPath ) == ReloadOutcome::Failed )
            SW_LOG_WARNING( "Hot reload skipped %# - the running scene keeps the previous content", relPath.c_str() );
    }

    ReloadOutcome AssetHotReload::reloadChangedAsset( string_view relativePath )
    {
        const AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( relativePath );
        // 소스를 임포트하는 종류는 임포트하는 것이 리로드다 — 임포트된 결과의 쓰기가 다음 감시 이벤트로 온다(그 결과를 읽는 캐시가 없어도 임포트는 한다).
        if ( route._pfnImportSource != nullptr && route._pfnImportSource( relativePath ) )
            return ReloadOutcome::Reloaded;
        if ( route._pCacheKindName == nullptr )
            return ReloadOutcome::NotTracked;

        // 에디터는 `EngineServices.h` 를 볼 수 없다(모듈 경계). 호스트가 넘겨준 서비스를 쓴다. 에셋 관리자가 없는 실행(헤드리스 도구)은 가진 캐시가 없다.
        AssetManager* pResources = findSubsystem<AssetManager>();
        if ( pResources == nullptr )
            return ReloadOutcome::NotTracked;
        // 디바이스는 **지금 것을** 넘긴다 — 캐시가 마지막으로 본 디바이스는 백엔드 교체 뒤 사라졌을 수 있다.
        EditorContext* pContext = EditorContext::get();
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
} // namespace sw::editor
