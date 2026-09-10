#include "pch.h"

#include "Engine/Resource/ResourceManager.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourcePackManager.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ResourceManager" );

    ResourceManager::ResourceManager()
        : _assetDatabase{}
        , _assetFormatRegistry{}
        , _materialCache{ make_unique<MaterialCache>() }
        , _textureCache{ make_unique<TextureCache>() }
        , _prefabManager{ make_unique<PrefabManager>() }
        , _pPackManager{ make_unique<ResourcePackManager>() }
        , _resourceWatchHandle{}
        , _pReloadFileManager{ nullptr }
    {
    }

    ResourceManager::~ResourceManager() = default;

    bool ResourceManager::initialize()
    {
        if ( ResourceUtil::initialize() == false )
        {
            SW_LOG_ERROR( "Failed to initialize ResourceManager!" );
            return false;
        }

        mountStartupPacks();

        loadAssetRegistries();

        _assetFormatRegistry.ensureBuiltins();
        return true;
    }

    bool ResourceManager::mountStartupPacks()
    {
        if ( _pPackManager == nullptr )
            return false;

        // 팩 폴더는 **실행 파일 기준**으로 찾는다. 예전엔 "Bin/Packs" 라는 상대 경로 하나뿐이라
        // 작업 디렉터리가 프로젝트 루트일 때만 맞았다. EngineLoop 은 이미 실행 파일 기준으로
        // 찾고 있어서 App 은 멀쩡했지만, EngineLoop 을 거치지 않고 ResourceManager 만 직접
        // 세우는 쪽(테스트·툴)은 아무 것도 마운트하지 못한다 — Shipping 은 느슨한 Resource/ 가
        // 없으니 그대로 모든 리소스 로드 실패가 된다.
        const string exeDir         = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
        const string arrCandidate[] = { FileUtil::joinPath( exeDir, "Packs" ),
                                        "Packs",
                                        FileUtil::joinPath( ResourceUtil::getProjectFolderPath(), "Packs" ),
                                        "Bin/Packs" };
        for ( const string& packsDir : arrCandidate )
        {
            if ( packsDir.empty() )
                continue;
            if ( _pPackManager->scanAndMountPacks( packsDir, ResourceUtil::getSearchPriority() ) )
                return true;
        }
        return false;
    }

    uint32 ResourceManager::loadAssetRegistries()
    {
        // 에셋 식별자(GUID) 표를 시작 시점에 채운다. 예전엔 ensureMeta 를 거친 에셋만 알아서, 이름을 바꾼 프리팹의
        // GUID 복구가 "그 세션에서 먼저 로드됐을 때만" 동작했고 배포본은 .meta 를 싣지 않아 아예 빈 표였다.
        // 팩이면 쿠커가 만든 assetregistry.txt 를 도메인마다 읽고, 없으면(느슨한 트리) .meta 를 훑는다 —
        // 유니티의 GUID 표, 언리얼의 AssetRegistry 가 하는 일이다.
        uint32       registered{ 0 };
        const string gameRoot      = GameConfig::getActive()._packRoot;
        const string arrRegistry[] = { "engine/assetregistry.txt", "common/assetregistry.txt",
                                       gameRoot.empty() ? string{} : gameRoot + "/assetregistry.txt" };
        for ( const string& registryPath : arrRegistry )
        {
            if ( registryPath.empty() == false )
                registered += _assetDatabase.loadRegistry( registryPath );
        }
        // 어느 경로로 채웠는지 같이 적는다 — 팩과 느슨한 트리가 둘 다 있는 자리에서 "5 항목" 만으로는 구분이 안 된다.
        if ( registered > 0 )
        {
            SW_LOG_INFO( "에셋 레지스트리 %# 항목 (팩 assetregistry.txt)", registered );
        }
        else if ( ResourceUtil::getRootFolderPath().empty() == false )
        {
            registered = _assetDatabase.scanMetaFiles( ResourceUtil::getRootFolderPath() );
            SW_LOG_INFO( "에셋 레지스트리 %# 항목 (.meta 스캔)", registered );
        }
        return registered;
    }

    void ResourceManager::shutdown()
    {
        detachReloadFileManager();

        if ( _pPackManager != nullptr )
            _pPackManager->unmountAll();

        if ( _materialCache != nullptr )
            _materialCache->clear();
        if ( _textureCache != nullptr )
            _textureCache->clear();
        _assetDatabase.clear();
    }

    void ResourceManager::garbageCollectUnusedAssets()
    {
        engine::getAssetStreamingQueue().sweepUnusedCache();
        // Note: MaterialCache automatically cleans up materials with 0 refcount in release().
    }

    void ResourceManager::attachReloadFileManager( ReloadFileManager& reloadFileManager )
    {
        detachReloadFileManager();
        _pReloadFileManager = &reloadFileManager;

        vector<string>         listExtension{ ".mat", ".prefab", ".json", ".xml", ".glTF", ".gltf", ".obj" };
        FileWatchMatchDelegate fileWatchDelegate{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &ResourceManager::onResourceFileChanged, this ) };
        _resourceWatchHandle = _pReloadFileManager->registerWatch( "Resource/", listExtension, fileWatchDelegate );
    }

    void ResourceManager::detachReloadFileManager()
    {
        if ( _resourceWatchHandle.isValid() && _pReloadFileManager != nullptr )
        {
            _pReloadFileManager->unregisterWatch( _resourceWatchHandle );
            _resourceWatchHandle = {};
        }
        _pReloadFileManager = nullptr;
    }

    void ResourceManager::onResourceFileChanged( const FileChangeEvent& changeEvent )
    {
        if ( changeEvent._action != FileWatcherAction::Modified )
            return;

        string relPath{};
        if ( FileUtil::makePathRelative( ResourceUtil::getRootFolderPath(), FileUtil::joinPath( changeEvent._directory, changeEvent._filename ), relPath ) == false )
            return;

        if ( relPath.empty() )
            return;

        SW_LOG_INFO( "Hot-Reloading asset: %#", relPath.c_str() );

        const string extension{ FileUtil::getExtension( changeEvent._filename ) };
        if ( extension == ".mat" )
        {
            // Try to reload from cache
            _materialCache->reload( relPath );
        }
        else if ( extension == ".prefab" )
        {
            // Future expansion for prefabs if needed.
        }
    }

    MaterialCache& ResourceManager::getMaterialManager()
    {
        return *_materialCache;
    }

    const MaterialCache& ResourceManager::getMaterialManager() const
    {
        return *_materialCache;
    }

    TextureCache& ResourceManager::getTextureManager()
    {
        return *_textureCache;
    }

    const TextureCache& ResourceManager::getTextureManager() const
    {
        return *_textureCache;
    }

    PrefabManager& ResourceManager::getPrefabManager()
    {
        return *_prefabManager;
    }

    const PrefabManager& ResourceManager::getPrefabManager() const
    {
        return *_prefabManager;
    }

    ResourcePackManager& ResourceManager::getPackManager()
    {
        return *_pPackManager;
    }

    const ResourcePackManager& ResourceManager::getPackManager() const
    {
        return *_pPackManager;
    }
} // namespace sw
