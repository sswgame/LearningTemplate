#include "pch.h"

#include "Engine/Resource/AssetManager.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Destruction/FractureAssetCache.h"
#include "Engine/Graphics/2D/SpriteMeshBuilder.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/2D/SpriteRenderUtil.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AnimationAssetCache.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/IAssetCache.h"
#include "Engine/Resource/LocalizationReloadCache.h"
#include "Engine/Resource/ResourcePackManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Resource/SpriteClipCache.h"

namespace sw
{
    SW_LOG_CALLER( "AssetManager" );

    AssetManager::AssetManager()
        : _assetDatabase{}
        , _assetFormatRegistry{}
        , _materialCache{ make_unique<MaterialCache>() }
        , _textureCache{ make_unique<TextureCache>() }
        , _prefabCache{ make_unique<PrefabCache>() }
        , _spriteClipCache{ make_unique<SpriteClipCache>() }
        , _meshCache{ make_unique<MeshCache>() }
        , _skeletonCache{ make_unique<SkeletonCache>() }
        , _animClipCache{ make_unique<AnimClipCache>() }
        , _boneLodCache{ make_unique<SkeletonBoneLodCache>() }
        , _socketSetCache{ make_unique<SocketSetCache>() }
        , _notifyTableCache{ make_unique<AnimNotifyTableCache>() }
        , _physicsAssetCache{ make_unique<PhysicsAssetCache>() }
        , _rigAssetCache{ make_unique<RigAssetCache>() }
        , _localizationReloadCache{ make_unique<LocalizationReloadCache>() }
        , _fractureCache{ make_unique<FractureAssetCache>() }
        , _pPackManager{ make_unique<ResourcePackManager>() }
        , _listBuiltInAssetCache{}
        , _registeredAssetCache{}
        , _contentSource{ ContentSource::Cooked }
    {
        // 내장 캐시는 이 목록 하나에 적는다 — 등록 · "내장인가" 판정이 모두 이것을 본다. 내장 캐시도 **등록부를 통해서만** 훑는다.
        // 코드로 짓는 값 표(내장 도형 · 9-슬라이스 메시 · 스프라이트 텍스처 인스턴스)는 Engine.dll 의 정적 객체다 — 매니저보다 오래 산다.
        _listBuiltInAssetCache = { _materialCache.get(),
                                   _textureCache.get(),
                                   _prefabCache.get(),
                                   _spriteClipCache.get(),
                                   _meshCache.get(),
                                   _skeletonCache.get(),
                                   _animClipCache.get(),
                                   _boneLodCache.get(),
                                   _socketSetCache.get(),
                                   _notifyTableCache.get(),
                                   _physicsAssetCache.get(),
                                   _rigAssetCache.get(),
                                   _localizationReloadCache.get(),
                                   _fractureCache.get(),
                                   &MeshUtil::getPrimitiveCache(),
                                   &SpriteMeshBuilder::getSlicedMeshCache(),
                                   &SpriteRenderUtil::getTextureInstanceCache() };
        for ( IAssetCache* pCache : _listBuiltInAssetCache )
            registerAssetCache( pCache );
    }

    AssetManager::~AssetManager() = default;

    bool AssetManager::initialize()
    {
        if ( ResourceUtil::initialize() == false )
        {
            SW_LOG_ERROR( "Failed to initialize AssetManager!" );
            return false;
        }

        _assetFormatRegistry.ensureBuiltins();
        return true;
    }

    bool AssetManager::mountContent( const vector<string>& listSearchPriority, ContentSource source )
    {
        // 우선순위 적용과 마운트를 **여기서 붙여 둔다.** 둘을 호출자에게 맡기면 순서를 뒤집거나
        // 사이에 다른 것을 끼워 넣을 수 있고, 실제로 그래서 팩이 게임 도메인 없이 실린 적이 있다.
        if ( listSearchPriority.empty() == false )
            ResourceUtil::setSearchPriority( listSearchPriority );

        // 소스 트리를 읽는 쿠킹은 팩을 올리지 않는다 — 팩은 쿠킹의 산출물이다. 느슨한 파일을 배포 구성에서도 읽고, GUID 표는 `.meta` 를 훑는다.
        _contentSource = source;
        bool bMounted{ false };
        if ( source == ContentSource::SourceTree )
            _pPackManager->setAllowLooseFiles( true );
        else
            bMounted = mountStartupPacks();
        loadAssetRegistries();
        return bMounted;
    }

    bool AssetManager::mountStartupPacks()
    {
        if ( _pPackManager == nullptr )
            return false;

        // 팩 폴더는 **실행 파일 기준**으로 찾는다. 작업 디렉터리 기준 상대 경로("Bin/Packs")면 EngineLoop 을 거치지 않고
        // AssetManager 만 직접 세우는 쪽(테스트 · 툴)이 아무것도 마운트하지 못하고, Shipping 은 느슨한 Resource/ 가
        // 없으니 그대로 모든 리소스 로드 실패가 된다.
        const string exeDir         = FileUtil::getBinaryDirectory();
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

    uint32 AssetManager::loadAssetRegistries()
    {
        // 에셋 식별자(GUID) 표를 시작 시점에 채운다 — 이름을 바꾼 프리팹의 GUID 복구가 "그 세션에서 먼저 로드됐을 때만" 동작하지 않게.
        // 팩이면 쿠커가 만든 assetregistry.txt 를 도메인마다 읽고(배포본은 .meta 를 싣지 않는다), 없으면(느슨한 트리) .meta 를 훑는다.
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
        // 어느 경로로 채웠는지 같이 적는다. 팩과 느슨한 트리가 둘 다 있는 자리에서 "5 항목" 만으로는 구분이 안 된다.
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

    void AssetManager::shutdown()
    {
        if ( _pPackManager != nullptr )
            _pPackManager->unmountAll();

        clearAssetCaches();
        // 비운 **뒤에** 말한다. 여기까지 왔다는 것은 죽은 포인터를 아직 밟지 않았다는 뜻이고,
        // 다음 실행에서 같은 일이 반복되지 않게 이름을 남겨야 한다.
        warnAboutRemainingModuleCaches();
        _assetDatabase.clear();
    }

    void AssetManager::garbageCollectUnusedAssets()
    {
        engine::getAssetStreamingQueue().clearCompletionRecord();
        // 캐시 자체는 참조가 0 이 되는 자리에서 스스로 지운다(`MaterialCache::release`).
        // 여기서는 아직 할 일이 없다. 있게 되면 등록부를 훑는다.
    }

    void AssetManager::registerAssetCache( IAssetCache* pCache )
    {
        if ( pCache == nullptr )
            return;
        // 이름은 **지금** 복사해 둔다(`RegistrationList`). 모듈이 내리지 않고 사라지면 나중에는 물어볼 수 없다.
        const utf8*              pKindName = pCache->getAssetKindName();
        const string_view        kindName  = ( pKindName != nullptr ) ? string_view{ pKindName } : string_view{};
        const RegistrationResult result    = _registeredAssetCache.add( pCache, kindName );
        if ( result == RegistrationResult::DuplicateName )
            SW_LOG_WARNING( "Asset cache kind '%#' is already registered by another cache - keeping the first", kindName );
    }

    bool AssetManager::isBuiltInAssetCache( const IAssetCache* pCache ) const
    {
        for ( const IAssetCache* pBuiltIn : _listBuiltInAssetCache )
        {
            if ( pBuiltIn == pCache )
                return pCache != nullptr;
        }
        return false;
    }

    void AssetManager::unregisterAssetCache( const IAssetCache* pCache )
    {
        (void)_registeredAssetCache.remove( pCache ); // 올라 있지 않으면 할 일이 없다(멱등)
    }

    vector<IAssetCache*> AssetManager::getAllAssetCache() const
    {
        return _registeredAssetCache.getItems();
    }

    IAssetCache* AssetManager::findAssetCache( string_view assetKindName ) const
    {
        // 이름은 등록 시점 사본으로 맞춘다. 죽은 모듈의 가상 함수를 부르지 않는다.
        return _registeredAssetCache.findByName( assetKindName );
    }

    void AssetManager::clearAssetCaches()
    {
        for ( IAssetCache* pCache : _registeredAssetCache.getItems() )
            pCache->clear();
    }

    void AssetManager::warnAboutRemainingModuleCaches() const
    {
        for ( uint32 index = 0; index < _registeredAssetCache.getCount(); ++index )
        {
            if ( isBuiltInAssetCache( _registeredAssetCache.getAt( index ) ) )
                continue;

            // 이름은 사본이라 안전하다. 포인터는 이미 죽었을 수도 있어 **역참조하지 않는다.**
            SW_LOG_WARNING( "에셋 캐시 '%#' 가 등록된 채로 남아 있습니다 — 올린 쪽(모듈)이 내려가기 전에 "
                            "unregisterAssetCache 를 불러야 합니다. 그대로 두면 다음 비우기가 죽은 코드로 뜁니다.",
                            _registeredAssetCache.getNameAt( index ).c_str() );
        }
    }

    uint32 AssetManager::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        vector<IAssetCache*> listModuleCache;
        for ( IAssetCache* pCache : _registeredAssetCache.getItems() )
        {
            if ( isBuiltInAssetCache( pCache ) )
                continue;
            // 캐시 객체가 모듈의 정적 데이터이거나, 모듈 쪽 클래스라 vtable 이 그 이미지에 있으면 이미지와 함께 사라진다.
            const bool bObjectWithin = IModuleUnloadListener::isAddressWithin( pCache, pBegin, pEnd );
            const bool bVtableWithin = IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( pCache ), pBegin, pEnd );
            if ( bObjectWithin || bVtableWithin )
                listModuleCache.push_back( pCache );
        }
        uint32 releasedCount{ 0 };
        for ( const IAssetCache* pCache : listModuleCache )
        {
            if ( _registeredAssetCache.remove( pCache ) )
                ++releasedCount;
        }
        return releasedCount;
    }

    MaterialCache& AssetManager::getMaterialManager()
    {
        return *_materialCache;
    }

    const MaterialCache& AssetManager::getMaterialManager() const
    {
        return *_materialCache;
    }

    TextureCache& AssetManager::getTextureManager()
    {
        return *_textureCache;
    }

    const TextureCache& AssetManager::getTextureManager() const
    {
        return *_textureCache;
    }

    MeshCache& AssetManager::getMeshCache()
    {
        return *_meshCache;
    }

    const MeshCache& AssetManager::getMeshCache() const
    {
        return *_meshCache;
    }

    PrefabCache& AssetManager::getPrefabCache()
    {
        return *_prefabCache;
    }

    const PrefabCache& AssetManager::getPrefabCache() const
    {
        return *_prefabCache;
    }

    ResourcePackManager& AssetManager::getPackManager()
    {
        return *_pPackManager;
    }

    const ResourcePackManager& AssetManager::getPackManager() const
    {
        return *_pPackManager;
    }
} // namespace sw
