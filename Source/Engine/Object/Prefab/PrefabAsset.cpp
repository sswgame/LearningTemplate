#include "pch.h"

#include "Engine/Object/Prefab/PrefabAsset.h"

#include "Core/Uuid/Uuid.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct PrefabAssetInternal
        {
            /**
             * @brief 프리팹 참조(경로 또는 GUID 문자열)를 경로로 풉니다. GUID 가 아니거나 데이터베이스에 없으면 받은 그대로 반환합니다.
             * @details 로드와 스폰이 이 열두 줄을 각자 들고 있었습니다. 한쪽만 GUID 를 풀면 같은 참조가 로드는 되고 스폰은 안 됩니다.
             */
            static string resolvePrefabPath( string_view assetReference )
            {
                if ( engine::areEngineServicesBound() == false )
                    return string{ assetReference };

                Uuid   guid{};
                string resolvedPath{ assetReference };
                if ( Uuid::tryParse( assetReference, guid ) == false || guid.isNull() )
                    return resolvedPath;

                string mappedPath;
                if ( engine::getResourceManager().getAssetDatabase().tryGetPath( guid, mappedPath ) && mappedPath.empty() == false )
                    resolvedPath = std::move( mappedPath );
                return resolvedPath;
            }

            static constexpr const utf8* kRoot             = "Prefab";
            static constexpr const utf8* kName             = "name";
            static constexpr const utf8* kGameObject       = "GameObject";
            static constexpr const utf8* kDefaultInstance  = "PrefabInstance";
            static constexpr uint32      kPrefabBinMagic2  = 0x50464232u; // 'PFB2'
            static constexpr uint32      kPrefabBinVersion = 0;

            static string makePrefabCacheKey( string_view assetRelativePath )
            {
                string key = FileUtil::normalizePath( assetRelativePath );

                // 확장자 길이를 손으로 쓰지 않는다. 예전에는 `.bin`/`.xml` 은 -4, `.json` 은 -5 로
                // 따로 적어서, 확장자를 하나 더 넣을 때 길이를 같이 고쳐야 했다. 숫자와 문자열이
                // 떨어져 있으면 어긋난다.
                for ( const string_view extension : { ".bin", ".xml", ".json" } )
                {
                    if ( FileUtil::hasExtension( key, extension ) )
                    {
                        key.resize( key.size() - extension.size() );
                        break;
                    }
                }
                return key;
            }

            static void collectPrefabRefsFromText( const utf8* pText, vector<string>& outListPath )
            {
                if ( StringUtil::isNullOrEmpty( pText ) )
                    return;
                if ( StringUtil::stristr( pText, ".prefab" ) == nullptr )
                    return;
                outListPath.push_back( pText );
            }

            static void collectPrefabRefsFromXml( XmlNode node, vector<string>& outListPath )
            {
                if ( node.isValid() == false )
                    return;
                collectPrefabRefsFromText( node.getText(), outListPath );
                for ( XmlAttribute attr = node.getFirstAttribute(); attr; attr = attr.getNext() )
                    collectPrefabRefsFromText( attr.getValue(), outListPath );
                for ( XmlNode childNode = node.findChild(); childNode; childNode = childNode.findNextSibling() )
                    collectPrefabRefsFromXml( childNode, outListPath );
            }

            static void collectPrefabRefsFromJson( JsonValue value, vector<string>& outListPath )
            {
                if ( value.isValid() == false )
                    return;
                if ( value.isString() )
                {
                    const string text = value.asString();
                    collectPrefabRefsFromText( text.c_str(), outListPath );
                    return;
                }
                if ( value.isObject() )
                {
                    const vector<string> listKey = value.getMemberNames();
                    for ( const string& key : listKey )
                        collectPrefabRefsFromJson( value.get( key ), outListPath );
                    return;
                }
                if ( value.isArray() )
                {
                    const size_t count = value.size();
                    for ( size_t index = 0; index < count; ++index )
                        collectPrefabRefsFromJson( value.at( index ), outListPath );
                }
            }

            /** @brief 형식을 모르는 본문(쿠킹한 바이너리 · 직접 준 텍스트)의 형식입니다. 읽을 때 **한 번만** 부릅니다. */
            static PrefabStateFormat detectStateFormat( string_view stateData )
            {
                const string_view trimmed = StringUtil::trim( stateData );
                return ( trimmed.empty() == false && trimmed.front() == '{' ) ? PrefabStateFormat::Json : PrefabStateFormat::Xml;
            }

            static bool upgradePrefabXmlBody( string& xmlBody, PrefabStateFormat format )
            {
                if ( xmlBody.empty() )
                    return false;
                if ( format == PrefabStateFormat::Json )
                    return true; // JSON 본문은 XML 업그레이드 대상이 아니다

                string wrapped = "<Prefab>";
                wrapped += xmlBody;
                wrapped += "</Prefab>";
                XmlDocument wrapDoc;
                if ( wrapDoc.parse( wrapped ) == false )
                    return false;
                XmlNode wrapRoot = wrapDoc.getRoot( kRoot );
                if ( wrapRoot.isValid() == false )
                    return false;
                if ( AssetFormatRegistry::upgradeXmlWithActiveRegistry( AssetKind::Prefab, wrapDoc, wrapRoot, AssetFormatVersions::kPrefab ) == false )
                    return false;
                XmlNode bodyNode = wrapRoot.findChild( kGameObject );
                if ( bodyNode.isValid() == false )
                    return false;
                xmlBody = bodyNode.toString();
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "PrefabAsset" );

    PrefabAsset::PrefabAsset()
        : _name{}
        , _stateData{}
        , _stateFormat{ PrefabStateFormat::Xml }
        , _bValid{ SW_FALSE }
        , _reserved{ 0 } {}

    bool PrefabAsset::loadFromXmlFile( string_view assetRelativePath )
    {
        _bValid = SW_FALSE;
        string      absPath;
        XmlDocument doc;
        if ( doc.loadPath( assetRelativePath, &absPath ) == false )
        {
            SW_LOG_ERROR( "Prefab not loaded - %#", doc.getLastError() );
            return false;
        }

        XmlNode root = doc.getRoot( PrefabAssetInternal::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <Prefab> root: %#", absPath );
            return false;
        }
        if ( AssetFormatRegistry::upgradeXmlWithActiveRegistry( AssetKind::Prefab, doc, root, AssetFormatVersions::kPrefab ) == false )
        {
            SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
            return false;
        }

        const utf8* pNameAttr = root.findAttribute( PrefabAssetInternal::kName );
        if ( pNameAttr != nullptr )
            _name = pNameAttr;

        XmlNode bodyNode = root.findChild( PrefabAssetInternal::kGameObject );
        if ( bodyNode.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <GameObject> in <Prefab>: %#", absPath );
            return false;
        }
        _stateData = bodyNode.toString();

        if ( _name.empty() )
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

        _stateFormat = PrefabStateFormat::Xml;
        _bValid      = SW_TRUE;
        SW_LOG_INFO( "Loaded '%#' from %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::loadFromJsonFile( string_view assetRelativePath )
    {
        _bValid = SW_FALSE;
        string       absPath;
        JsonDocument doc;
        if ( doc.loadPath( assetRelativePath, &absPath ) == false )
        {
            SW_LOG_ERROR( "Prefab not loaded - %#", doc.getLastError() );
            return false;
        }

        // 저장기가 쓰는 모양 하나 — GameObject 상태 JSON 그대로다(이름은 그 `_name`).
        const JsonValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR( "Prefab JSON is not a GameObject state object: %#", absPath );
            return false;
        }
        _name      = root.get( "_name" ).asString();
        _stateData = doc.dump( 0 );

        if ( _name.empty() )
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

        _stateFormat = PrefabStateFormat::Json;
        _bValid      = SW_TRUE;
        return true;
    }

    bool PrefabAsset::loadFromBinaryFile( string_view assetRelativePath )
    {
        _bValid        = SW_FALSE;
        string absPath = ResourceUtil::getResourcePath( assetRelativePath );
        if ( absPath.empty() )
            absPath = assetRelativePath;

        Archive arch( absPath, true );
        if ( arch.getSize() < 12 )
        {
            SW_LOG_ERROR( "Binary read failed or too small: %#", absPath );
            return false;
        }

        uint32 magic{ 0 };
        arch >> magic;
        if ( magic != PrefabAssetInternal::kPrefabBinMagic2 )
        {
            SW_LOG_ERROR( "Bad binary magic: %#", absPath );
            return false;
        }

        uint32 version{ 0 };
        arch >> version;
        if ( version > PrefabAssetInternal::kPrefabBinVersion )
        {
            SW_LOG_ERROR( "Unsupported binary version %# in %#", version, absPath );
            return false;
        }

        arch >> _name >> _stateData;
        if ( arch.isError() )
        {
            SW_LOG_ERROR( "Binary prefab stream corrupted in %#", absPath );
            return false;
        }

        _stateFormat = PrefabAssetInternal::detectStateFormat( _stateData );
        if ( PrefabAssetInternal::upgradePrefabXmlBody( _stateData, _stateFormat ) == false )
        {
            SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
            return false;
        }

        _bValid = SW_TRUE;
        SW_LOG_INFO( "Loaded '%#' from binary %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::saveToXmlFile( string_view assetRelativePath ) const
    {
        const string absPath = ResourceUtil::getWritePath( assetRelativePath );

        const string xmlBody = ( _stateFormat == PrefabStateFormat::Xml ) ? _stateData : convertState( PrefabStateFormat::Xml );

        XmlDocument xmlDoc;
        XmlNode     root = xmlDoc.appendRoot( PrefabAssetInternal::kRoot );
        root.appendAttribute( "formatVersion", static_cast<uint32>( AssetFormatVersions::kPrefab ) );
        root.appendAttribute( PrefabAssetInternal::kName, _name );
        if ( xmlBody.empty() == false )
        {
            XmlDocument bodyDoc;
            const bool  bBodyParsed = bodyDoc.parse( xmlBody );
            XmlNode     bodyRoot    = bBodyParsed ? bodyDoc.getRoot() : XmlNode{};
            if ( bodyRoot.isValid() == false )
            {
                SW_LOG_ERROR( "Prefab '%#' has a state that cannot be written as XML - '%#' is left as it was", _name, assetRelativePath );
                return false;
            }
            root.appendClone( bodyRoot );
        }
        else if ( _stateData.empty() == false )
        {
            // 상태가 있는데 XML 로 옮기지 못했다(타입이 빠진 JSON 본문 등). 예전에는 빈 `<Prefab>` 을 쓰고 성공이라 했다 — 파일의 내용이 사라졌다.
            SW_LOG_ERROR( "Prefab '%#' could not be converted to XML - '%#' is left as it was", _name, assetRelativePath );
            return false;
        }

        // 상위 폴더가 없으면 쓰기가 실패한다. 로더는 실패를 모두 로그하는데 세이버는 조용히 false 만
        // 반환하고 있었다. 새 폴더에 프리팹을 저장하면 아무 메시지도 없이 아무 일도 일어나지 않았다.
        // SceneDocument::saveXml 과 같은 형태로 맞춘다.
        FileUtil::ensureParentDirectoryExists( absPath );

        const bool writeOk = xmlDoc.saveFile( absPath );
        if ( writeOk == false )
        {
            SW_LOG_ERROR( "Failed to write prefab XML: %#", absPath );
            return false;
        }

        if ( engine::areEngineServicesBound() )
            engine::getResourceManager().getAssetDatabase().ensureMeta( assetRelativePath );
        SW_LOG_INFO( "Saved '%#' -> %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::saveToJsonFile( string_view assetRelativePath ) const
    {
        const string absPath = ResourceUtil::getWritePath( assetRelativePath );

        string jsonStr = ( _stateFormat == PrefabStateFormat::Json ) ? _stateData : convertState( PrefabStateFormat::Json );
        if ( jsonStr.empty() )
        {
            // 상태를 읽지 못했다(타입이 빠진 본문 등). 본문을 그대로 싸서 잃지 않는다.
            JsonDocument doc;
            JsonValue    root = doc.makeObject();
            root.set( "formatVersion" ).setInt( static_cast<int32>( AssetFormatVersions::kPrefab ) );
            root.set( "name" ).setString( _name );
            root.set( "xmlBody" ).setString( _stateData );
            jsonStr = doc.dump( 1 );
        }

        FileUtil::ensureParentDirectoryExists( absPath );

        const bool writeOk = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( jsonStr.data() ),
                                                  jsonStr.size() );
        if ( writeOk == false )
        {
            SW_LOG_ERROR( "Failed to write prefab JSON: %#", absPath );
            return false;
        }

        if ( engine::areEngineServicesBound() )
            engine::getResourceManager().getAssetDatabase().ensureMeta( assetRelativePath );
        SW_LOG_INFO( "Saved '%#' JSON %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::saveToFile( string_view assetRelativePath ) const
    {
        // 형식은 경로가 정한다 — 쿠커(`cookAllPrefabs`)와 로더(`loadPrefab`)가 읽는 규칙과 같다. 예전에는 "Apply to Prefab" 이 늘 XML 로 써서
        // `.prefab.json` 에 XML 이 들어가 그 프리팹이 다시는 읽히지 않았고, 프리팹이 아닌 경로(씬 · 머티리얼)도 그대로 덮었다.
        if ( StringUtil::endsWith( assetRelativePath, ".prefab.json", true ) )
            return saveToJsonFile( assetRelativePath );
        if ( StringUtil::endsWith( assetRelativePath, ".prefab.xml", true ) )
            return saveToXmlFile( assetRelativePath );
        SW_LOG_ERROR( "'%#' is not a prefab source path (.prefab.xml / .prefab.json) - nothing was written", assetRelativePath );
        return false;
    }

    bool PrefabAsset::saveToBinaryFile( string_view assetRelativePath ) const
    {
        const string absPath = ResourceUtil::getWritePath( assetRelativePath );

        Archive arch;
        arch << PrefabAssetInternal::kPrefabBinMagic2;
        arch << PrefabAssetInternal::kPrefabBinVersion;
        arch << _name;
        arch << _stateData;

        FileUtil::ensureParentDirectoryExists( absPath );
        if ( arch.saveFile( absPath ) == false )
        {
            SW_LOG_ERROR( "Failed to write prefab binary: %#", absPath );
            return false;
        }
        return true;
    }

    void PrefabAsset::setFromGameObject( const GameObject* pGameObject )
    {
        if ( pGameObject == nullptr )
        {
            _bValid = SW_FALSE;
            return;
        }
        // 프리팹 루트에는 부모가 없다 — 자식 인스턴스로 프리팹을 만들어도 옛 부모를 싣지 않는다. 예전에는 실어서, 그 프리팹을 스폰할 때마다
        // 그 이름의 오브젝트에 붙었다(언리얼 · 유니티의 프리팹 루트도 부모를 들지 않는다).
        ObjectSaveOptions options{};
        options._bOmitExternalParent = true;
        _name                        = pGameObject->getName().c_str();
        _stateData                   = ObjectStateSerializer::saveToXmlString( pGameObject, options );
        _stateFormat                 = PrefabStateFormat::Xml;
        _bValid                      = _stateData.empty() == false ? SW_TRUE : SW_FALSE;
    }

    bool PrefabAsset::applyStateTo( GameObject* pTarget, const ObjectIdentity* pIdentity ) const
    {
        if ( pTarget == nullptr || _stateData.empty() )
            return false;
        // 옛 프리팹에 남은 다른 오브젝트로의 부착은 읽지 않는다(위 `setFromGameObject`). 오브젝트 안의 부착은 그대로 잇는다.
        ObjectLoadContext context{};
        context._pIdentity              = pIdentity;
        context._bExternalParentAllowed = false;
        return ( _stateFormat == PrefabStateFormat::Json ) ? ObjectStateSerializer::loadFromJsonString( pTarget, _stateData, context )
                                                           : ObjectStateSerializer::loadFromXmlString( pTarget, _stateData, context );
    }

    string PrefabAsset::convertState( PrefabStateFormat targetFormat ) const
    {
        // 컴포넌트는 매니저가 이름으로 만든다(역직렬화 팩토리). 쓰고 버리는 매니저 안에서 읽고 다른 형식으로 쓴다 — 씬 쿠커와 같은 방법이다.
        GameObjectManager scratch;
        GameObject*       pTemp = scratch.createGameObject( hashed_string( _name.c_str() ) );
        if ( applyStateTo( pTemp ) == false )
            return {};
        return ( targetFormat == PrefabStateFormat::Json ) ? ObjectStateSerializer::saveToJsonString( pTemp ) : ObjectStateSerializer::saveToXmlString( pTemp );
    }

    void PrefabAsset::collectReferencedPrefabPaths( vector<string>& outListPath ) const
    {
        outListPath.clear();
        const string trimmed{ StringUtil::trim( _stateData ) };
        if ( trimmed.empty() )
            return;
        if ( _stateFormat == PrefabStateFormat::Json )
        {
            JsonDocument doc;
            if ( doc.parse( trimmed ) == false )
                return;
            PrefabAssetInternal::collectPrefabRefsFromJson( doc.getRoot(), outListPath );
            return;
        }
        XmlDocument doc;
        if ( doc.parse( trimmed ) == false )
            return;
        PrefabAssetInternal::collectPrefabRefsFromXml( doc.getRoot(), outListPath );
    }

    void PrefabManager::reload( string_view assetRelativePath, IRHIDevice* )
    {
        if ( assetRelativePath.empty() )
            return;

        const string cacheKey = PrefabAssetInternal::makePrefabCacheKey( assetRelativePath );

        std::unique_lock<std::shared_mutex> writeLock{ _mapCacheMutex };
        _mapCache.erase( cacheKey );
    }

    bool PrefabManager::isCached( string_view assetRelativePath ) const
    {
        if ( assetRelativePath.empty() )
            return false;

        const string                        cacheKey = PrefabAssetInternal::makePrefabCacheKey( assetRelativePath );
        std::shared_lock<std::shared_mutex> readLock{ _mapCacheMutex };
        return _mapCache.find( cacheKey ) != _mapCache.end();
    }

    size_t PrefabManager::getCachedCount() const
    {
        std::shared_lock<std::shared_mutex> readLock{ _mapCacheMutex };
        return _mapCache.size();
    }

    void PrefabManager::clear()
    {
        std::unique_lock<std::shared_mutex> writeLock{ _mapCacheMutex };
        _mapCache.clear();
    }
    PrefabAsset* PrefabManager::loadPrefab( string_view assetRelativePath )
    {
        const string resolvedPath = PrefabAssetInternal::resolvePrefabPath( assetRelativePath );

        const string cacheKey = PrefabAssetInternal::makePrefabCacheKey( resolvedPath );
        {
            std::shared_lock<std::shared_mutex> readLock{ _mapCacheMutex };
            const auto                          cacheIt = _mapCache.find( cacheKey );
            if ( cacheIt != _mapCache.end() )
                return cacheIt->second.get();
        }

        unique_ptr<PrefabAsset> asset = make_unique<PrefabAsset>();

        // 쿠킹본 이름은 쿠커(`cookAllPrefabs`)와 같은 규칙 하나다(`AssetCookPath`).
        string binPath = AssetCookPath::toCookedPath( resolvedPath );
        if ( binPath.empty() )
            binPath = resolvedPath;

#if defined( SW_SHIPPING )
        if ( asset->loadFromBinaryFile( binPath ) == false )
        {
            SW_LOG_ERROR( "Shipping requires cooked binary: %#", binPath );
            return nullptr;
        }
#else
        const bool bJson         = FileUtil::hasExtension( resolvedPath, ".json" );
        const bool bSourceLoaded = bJson ? asset->loadFromJsonFile( resolvedPath ) : asset->loadFromXmlFile( resolvedPath );
        if ( bSourceLoaded == false )
        {
            if ( asset->loadFromBinaryFile( binPath ) == false )
                return nullptr;
            // Dev 는 소스(XML/JSON)가 기준이다. 여기로 왔다는 것은 소스가 옮겨졌거나 지워졌는데 낡은 쿠킹 산출물
            // (.gitignore 된 .bin)이 소스 트리에 남아 있다는 뜻이다. 조용히 쓰면 실패가 가려진다(프리팹을 옮기는
            // 실험에서 옛 .bin 이 "Not found" 를 그대로 삼켰다). 언리얼 · 유니티의 에디터는 쿠킹 데이터를 아예 보지 않는다.
            // 여기는 폴백을 남기되 두 경로를 다 적어 왜 그 내용이 나왔는지 바로 보이게 한다.
            SW_LOG_WARNING( "Source prefab missing - loaded stale cooked binary instead: %# (source %#)", binPath, resolvedPath );
        }
#endif

        std::unique_lock<std::shared_mutex> writeLock{ _mapCacheMutex };
        const auto                          cacheIt = _mapCache.find( cacheKey );
        if ( cacheIt != _mapCache.end() )
            return cacheIt->second.get();

        PrefabAsset* pCached = asset.get();
        _mapCache.emplace( cacheKey, std::move( asset ) );
        return pCached;
    }

    bool PrefabManager::revertInstance( GameObject* pInstance, string_view assetRelativePath )
    {
        if ( pInstance == nullptr )
            return false;
        const PrefabAsset* pAsset = loadPrefab( assetRelativePath );
        if ( pAsset == nullptr || pAsset->isValid() == false )
            return false;

        // 인스턴스의 자리를 적어 둔다. 상태를 읽으면 컴포넌트가 모두 새로 만들어지고 이름 · 부착 · 트랜스폼이 프리팹의 것이 된다.
        // 부모는 오브젝트가 아니라 **붙어 있던 컴포넌트**(소켓일 수 있다)를 핸들로 적는다 — 예전에는 부모 오브젝트의 primary 에 다시 붙여,
        // 소켓에 달린 무기가 튀었다. 컴포넌트 id 도 되살린다 — 이 인스턴스의 컴포넌트를 가리키던 핸들(활성 카메라 · 게임 코드)이 이어지게.
        const hashed_string   name         = pInstance->getName();
        const SceneComponent* pOldRoot     = pInstance->getPrimarySceneComponent();
        const bool            bHadRoot     = pOldRoot != nullptr;
        const ComponentHandle parentHandle = ( bHadRoot && pOldRoot->getParent() != nullptr ) ? pOldRoot->getParent()->getHandle() : ComponentHandle{};
        const float3          position     = bHadRoot ? pOldRoot->getLocalPosition() : float3{};
        const float3          rotation     = bHadRoot ? pOldRoot->getLocalRotation() : float3{};
        const ObjectIdentity  identity     = ObjectStateSerializer::captureIdentity( pInstance );

        if ( pAsset->applyStateTo( pInstance, &identity ) == false )
            return false;

        pInstance->setName( name );
        SceneComponent* pNewRoot = pInstance->getPrimarySceneComponent();
        if ( pNewRoot != nullptr )
        {
            GameObjectManager* pManager = pInstance->getManager();
            Component*         pParent  = ( pManager != nullptr && parentHandle.isValid() ) ? pManager->resolveComponent( parentHandle ) : nullptr;
            if ( pParent != nullptr && pParent->isSceneComponent() && pNewRoot->getParent() != pParent &&
                 pNewRoot->attachToComponent( static_cast<SceneComponent*>( pParent ) ) == false )
                SW_LOG_WARNING( "Revert of '%#' could not re-attach it to its parent - it stays at the root", name.c_str() );
            if ( bHadRoot )
            {
                pNewRoot->setLocalPosition( position );
                pNewRoot->setLocalRotation( rotation );
            }
        }
        return true;
    }

    GameObject* PrefabManager::spawn( GameObjectManager* pGameObjectManager, string_view assetRelativePath, const utf8* pInstanceName )
    {
        // 이 함수는 나머지 포인터를 모두 검사한다(`pAsset` · `pGameObject` · `pInstanceName`).
        // 매니저만 빠져 있었다. 활성 씬이 없을 때 `getObjectManager()` 는 nullptr 를 반환한다.
        if ( pGameObjectManager == nullptr )
        {
            SW_LOG_WARNING( "Cannot spawn prefab '%#' without a GameObjectManager.", assetRelativePath );
            return nullptr;
        }

        const string resolvedPath = PrefabAssetInternal::resolvePrefabPath( assetRelativePath );

        const string                       cacheKey = PrefabAssetInternal::makePrefabCacheKey( resolvedPath );
        const hashed_string                pathKey( cacheKey.data(), static_cast<uint32>( cacheKey.size() ) );
        thread_local vector<hashed_string> t_listSpawnStack;

        if ( std::find( t_listSpawnStack.begin(), t_listSpawnStack.end(), pathKey ) != t_listSpawnStack.end() )
        {
            SW_LOG_ERROR( "Circular prefab reference detected for '%#' — spawn aborted to prevent recursion overflow",
                          assetRelativePath );
            return nullptr;
        }

        t_listSpawnStack.push_back( pathKey );
        struct StackGuard
        {
            vector<hashed_string>& _stack;
            ~StackGuard()
            {
                _stack.pop_back();
            }
        } guard{ t_listSpawnStack };

        PrefabAsset* pAsset = loadPrefab( assetRelativePath );
        if ( pAsset == nullptr || pAsset->isValid() == false )
            return nullptr;

        const utf8* pInstanceNameUtf8 = pInstanceName != nullptr ? pInstanceName : pAsset->getName().c_str();
        if ( StringUtil::isNullOrEmpty( pInstanceNameUtf8 ) )
            pInstanceNameUtf8 = PrefabAssetInternal::kDefaultInstance;

        GameObject* pGameObject = pGameObjectManager->createGameObject( hashed_string( pInstanceNameUtf8 ) );
        if ( pGameObject == nullptr )
            return nullptr;

        // 컴포넌트 틱 중이면 오브젝트는 지금 만들어 돌려주고(부르는 쪽이 핸들을 든다), 상태는 틱 직후 구조 변경 큐에서 채운다. 예전에는 상태 읽기가
        // 그 자리에서 돌며 컴포넌트 추가가 모두 미뤄져(nullptr) **프리팹의 값이 버려졌다** — 빈 오브젝트만 남았다.
        if ( pGameObjectManager->isStructuralMutationFrozen() )
        {
            const uint64 objectId = pGameObject->getObjectId();
            const string instanceName( pInstanceNameUtf8 );
            // 캐시의 프리팹은 그 사이 다시 읽힐 수 있다(에디터 핫 리로드) — 포인터가 아니라 경로를 들고 그때 다시 찾는다.
            pGameObjectManager->deferStructuralChange( [this, pGameObjectManager, objectId, resolvedPath, instanceName]()
            {
                GameObject*  pSpawned      = pGameObjectManager->findGameObjectById( objectId );
                PrefabAsset* pLaterAsset   = ( pSpawned != nullptr ) ? loadPrefab( resolvedPath ) : nullptr;
                const bool   bStateWritten = pLaterAsset != nullptr && applySpawnState( pSpawned, *pLaterAsset, instanceName );
                if ( pSpawned != nullptr && bStateWritten == false )
                    pGameObjectManager->destroyObject( pSpawned );
            } );
            return pGameObject;
        }

        if ( applySpawnState( pGameObject, *pAsset, pInstanceNameUtf8 ) == false )
        {
            pGameObjectManager->destroyObject( pGameObject );
            return nullptr;
        }
        return pGameObject;
    }

    bool PrefabManager::applySpawnState( GameObject* pGameObject, const PrefabAsset& asset, string_view instanceName )
    {
        if ( StringUtil::trim( asset.getStateData() ).empty() == false )
        {
            if ( asset.applyStateTo( pGameObject ) == false )
            {
                SW_LOG_ERROR( "ObjectState apply failed for '%#' — spawn aborted", instanceName );
                return false;
            }
            pGameObject->setName( hashed_string( string( instanceName ).c_str() ) );
        }
        return true;
    }

    uint32 PrefabManager::cookAllPrefabs( string_view sourceRoot, string_view cookedDir, uint32& outFailedCount )
    {
        outFailedCount = 0;
        if ( sourceRoot.empty() || cookedDir.empty() )
        {
            SW_LOG_ERROR( "Prefab cook needs a source root and an output directory (--cooked-dir)." );
            return 0;
        }

        vector<string> listFile;
        FileUtil::collectFiles( sourceRoot, {}, listFile, true );
        // 순서를 정한다 — 같은 쿠킹본을 쓰는 두 소스 가운데 어느 것이 먼저인지가 실행마다 같아야 한다.
        std::sort( listFile.begin(), listFile.end() );

        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( sourceRoot ) );
        string       out  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( cookedDir ) );
        out += '/';

        vector<string> listWrittenKey;
        uint32         writtenCount = 0;
        for ( const string& filePath : listFile )
        {
            const string normalized = FileUtil::normalizeSeparators( filePath );
            const bool   bXml       = StringUtil::endsWith( normalized, ".prefab.xml", true );
            const bool   bJson      = StringUtil::endsWith( normalized, ".prefab.json", true );
            if ( ( bXml || bJson ) == false || normalized.size() <= root.size() + 1 )
                continue;

            // 출력은 `<cookedDir>/<소스 루트 기준 상대 경로>` 의 쿠킹본 이름이다 — 런타임 `loadPrefab` 과 같은 규칙 하나(`AssetCookPath`).
            const string outputPath = AssetCookPath::toCookedPath( out + normalized.substr( root.size() + 1 ) );
            const string outputKey  = StringUtil::toLower( outputPath.c_str() );
            if ( std::find( listWrittenKey.begin(), listWrittenKey.end(), outputKey ) != listWrittenKey.end() )
            {
                SW_LOG_WARNING( "Prefab cook: '%#' would overwrite the cooked file another source already wrote ('%#') - skipped", normalized, outputPath );
                ++outFailedCount;
                continue;
            }

            PrefabAsset asset;
            const bool  bLoaded = bJson ? asset.loadFromJsonFile( normalized ) : asset.loadFromXmlFile( normalized );
            if ( bLoaded == false || asset.isValid() == false || asset.saveToBinaryFile( outputPath ) == false )
            {
                SW_LOG_WARNING( "Prefab cook failed for '%#'", normalized );
                ++outFailedCount;
                continue;
            }
            listWrittenKey.push_back( outputKey );
            ++writtenCount;
        }
        return writtenCount;
    }
} // namespace sw
