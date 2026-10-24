#include "pch.h"

#include "Editor/Common/Workspace/AssetHotReload.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

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
        /**
         * @brief 실제로 감시할 확장자 목록을 정합니다.
         * @details `editortooldefaults.json` 의 `_listHotReloadExtension` 이 정책이고, 다시 읽을 캐시가 있는 종류(`EditorAssetTypeRegistry`)가 한계입니다.
         *          설정이 비어 있으면 그 종류 전부를 봅니다. 설정에 처리할 수 없는 확장자가 있으면 경고하고 뺍니다.
         */
        vector<string> resolveWatchExtensions()
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
    } // namespace

    SW_LOG_CALLER( "AssetHotReload" );

    AssetHotReload::AssetHotReload()
        : _pFileWatchDispatcher{ nullptr }
        , _resourceWatchHandle{}
        , _contentWatchHandle{}
        , _contentChangeSerial{ 0 }
    {
    }

    AssetHotReload::~AssetHotReload()
    {
        shutdown();
    }

    bool AssetHotReload::initialize()
    {
        shutdown();

        const vector<string> listExtension = resolveWatchExtensions();
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
        (void)changeEvent;
        ++_contentChangeSerial;
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
