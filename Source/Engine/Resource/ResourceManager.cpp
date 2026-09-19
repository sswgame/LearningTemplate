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
        , _listAssetCache{}
    {
        // 내장 캐시도 **등록부를 통해서만** 훑는다 - 이름을 따로 적는 경로를 남기면 그 경로가
        // 다시 어긋난다(종료가 프리팹을 잊고 있었다).
        registerAssetCache( _materialCache.get() );
        registerAssetCache( _textureCache.get() );
        registerAssetCache( _prefabManager.get() );
    }

    ResourceManager::~ResourceManager() = default;

    bool ResourceManager::initialize()
    {
        if ( ResourceUtil::initialize() == false )
        {
            SW_LOG_ERROR( "Failed to initialize ResourceManager!" );
            return false;
        }

        _assetFormatRegistry.ensureBuiltins();
        return true;
    }

    bool ResourceManager::mountContent( const vector<string>& listSearchPriority )
    {
        // 우선순위 적용과 마운트를 **여기서 붙여 둔다.** 둘을 호출자에게 맡기면 순서를 뒤집거나
        // 사이에 다른 것을 끼워 넣을 수 있고, 실제로 그래서 팩이 게임 도메인 없이 실린 적이 있다.
        if ( listSearchPriority.empty() == false )
            ResourceUtil::setSearchPriority( listSearchPriority );

        const bool bMounted = mountStartupPacks();
        loadAssetRegistries();
        return bMounted;
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
        if ( _pPackManager != nullptr )
            _pPackManager->unmountAll();

        clearAssetCaches();
        // 비운 **뒤에** 말한다 — 여기까지 왔다는 것은 죽은 포인터를 아직 밟지 않았다는 뜻이고,
        // 다음 실행에서 같은 일이 반복되지 않게 이름을 남겨야 한다.
        warnAboutLeftoverModuleCaches();
        _assetDatabase.clear();
    }

    void ResourceManager::garbageCollectUnusedAssets()
    {
        engine::getAssetStreamingQueue().clearCompletionRecord();
        // 캐시 자체는 참조가 0 이 되는 자리에서 스스로 지운다(`MaterialCache::release`).
        // 여기서는 아직 할 일이 없다 - 있게 되면 등록부를 훑는다.
    }

    void ResourceManager::registerAssetCache( IAssetCache* pCache )
    {
        if ( pCache == nullptr )
            return;
        for ( const RegisteredAssetCache& existing : _listAssetCache )
        {
            if ( existing._pCache == pCache )
                return;
        }

        RegisteredAssetCache entry{};
        entry._pCache = pCache;
        // 이름은 **지금** 복사해 둔다 — 모듈이 내리지 않고 사라지면 나중에는 물어볼 수 없다.
        const utf8* pKindName = pCache->getAssetKindName();
        if ( pKindName != nullptr )
            entry._kindName = pKindName;
        _listAssetCache.push_back( std::move( entry ) );
    }

    void ResourceManager::unregisterAssetCache( const IAssetCache* pCache )
    {
        if ( pCache == nullptr )
            return;
        for ( size_t slot = 0; slot < _listAssetCache.size(); ++slot )
        {
            if ( _listAssetCache[slot]._pCache != pCache )
                continue;

            _listAssetCache.erase( _listAssetCache.begin() + static_cast<ptrdiff_t>( slot ) );
            return;
        }
    }

    vector<IAssetCache*> ResourceManager::getAllAssetCache() const
    {
        vector<IAssetCache*> listCache;
        listCache.reserve( _listAssetCache.size() );
        for ( const RegisteredAssetCache& entry : _listAssetCache )
        {
            listCache.push_back( entry._pCache );
        }
        return listCache;
    }

    IAssetCache* ResourceManager::findAssetCache( string_view assetKindName ) const
    {
        if ( assetKindName.empty() )
            return nullptr;
        for ( const RegisteredAssetCache& entry : _listAssetCache )
        {
            // 이름은 등록 시점 사본으로 맞춘다 — 죽은 모듈의 가상 함수를 부르지 않는다.
            if ( entry._pCache != nullptr && assetKindName == entry._kindName )
                return entry._pCache;
        }
        return nullptr;
    }

    void ResourceManager::clearAssetCaches()
    {
        for ( const RegisteredAssetCache& entry : _listAssetCache )
        {
            if ( entry._pCache != nullptr )
                entry._pCache->clear();
        }
    }

    void ResourceManager::warnAboutLeftoverModuleCaches() const
    {
        for ( const RegisteredAssetCache& entry : _listAssetCache )
        {
            if ( entry._pCache == _materialCache.get() || entry._pCache == _textureCache.get() ||
                 entry._pCache == _prefabManager.get() )
                continue;

            // 이름은 사본이라 안전하다. 포인터는 이미 죽었을 수도 있어 **역참조하지 않는다.**
            SW_LOG_WARNING( "에셋 캐시 '%#' 가 등록된 채로 남아 있습니다 — 올린 쪽(모듈)이 내려가기 전에 "
                            "unregisterAssetCache 를 불러야 합니다. 그대로 두면 다음 비우기가 죽은 코드로 뜁니다.",
                            entry._kindName.c_str() );
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
