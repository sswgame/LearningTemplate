#include "pch.h"

#include "Engine/Scene/SceneDocument.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Uuid/Uuid.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct SceneDocumentInternal
        {
            static constexpr const utf8* kRoot          = "Scene";
            static constexpr const utf8* kName          = "name";
            static constexpr const utf8* kEntities      = "entities";
            static constexpr const utf8* kEntity        = "entity";
            static constexpr const utf8* kFileId        = "id";
            static constexpr const utf8* kPrefab        = "prefab";
            static constexpr const utf8* kGameObject    = "GameObject";
            static constexpr const utf8* kDefaultEntity = "Entity";
            static constexpr uint32      kSceneBinMagic = 0x53434E31u; // 'SCN1'
            // v1 부터 엔티티마다 **바이너리 상태**가 한 필드 더 붙는다(비어 있을 수 있다). v0 은
            // XML 문자열만 실려 있었고, 그 파일도 계속 읽는다 — 아래 읽기가 버전으로 갈린다.
            // v2 부터 엔티티의 파일 id(부착이 부모를 가리키는 값)가 끝에 붙는다.
            // v3 부터 프리팹 인스턴스의 덮어쓴 것(`_prefabOverrideXml`)이 끝에 붙는다.
            static constexpr uint32 kSceneBinVersion = 3;

            /** @brief 속성으로 먼저 찾고, 없으면 같은 이름의 자식 텍스트를 봅니다(저작본의 두 모양을 다 읽습니다). 없으면 nullptr 입니다. */
            static const utf8* findAttributeOrChildText( const XmlNode& node, const utf8* pKey )
            {
                const utf8* pValue = node.findAttribute( pKey );
                return ( pValue != nullptr ) ? pValue : node.findChildText( pKey );
            }

            /**
             * @brief 프리팹 GUID 로 경로를 다시 풉니다. 파일 이동 · 이름 변경을 자동으로 따라갑니다.
             * @details XML 로더와 바이너리 로더가 같은 아홉 줄을 각자 들고 있었습니다. 한쪽만 고치면 그 포맷으로 읽은 씬만
             *          옮긴 프리팹을 못 찾습니다.
             */
            static void resolvePrefabPathByGuid( SceneDocument::EntityNode& node )
            {
                if ( node._prefabGuid.empty() || engine::areEngineServicesBound() == false )
                    return;

                Uuid guid{};
                if ( Uuid::tryParse( node._prefabGuid, guid ) == false || guid.isNull() )
                    return;

                string resolved;
                if ( engine::getResourceManager().getAssetDatabase().tryGetPath( guid, resolved ) && resolved.empty() == false )
                    node._prefab = std::move( resolved );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SceneDocument" );

    bool SceneDocument::loadXml( string_view path )
    {
        *this       = {};
        _sourcePath = path;

        XmlDocument doc;
        string      absPath;
        if ( doc.loadPath( path, &absPath ) == false )
        {
            // 없는 파일과 깨진 파일을 가른다 — 예전에는 구문 오류도 "File not found" 였다.
            SW_LOG_ERROR( "Scene not loaded - %#", doc.getLastError() );
            return false;
        }

        XmlNode root = doc.getRoot( SceneDocumentInternal::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing root <Scene>: %#", absPath );
            return false;
        }

        if ( AssetFormatRegistry::upgradeXmlWithActiveRegistry( AssetKind::Scene, doc, root, AssetFormatVersions::kScene ) == false )
        {
            SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
            return false;
        }

        const utf8* pSceneName = root.findAttribute( "name" );
        if ( pSceneName == nullptr )
            pSceneName = root.findChildText( SceneDocumentInternal::kName );

        if ( pSceneName != nullptr )
            _name = pSceneName;
        else
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

        XmlNode entities = root.findChild( SceneDocumentInternal::kEntities );

        if ( entities.isValid() )
        {
            for ( XmlNode entityNode = entities.findChild( SceneDocumentInternal::kEntity ); entityNode.isValid();
                  entityNode         = entityNode.findNextSibling( SceneDocumentInternal::kEntity ) )
            {
                EntityNode  node{};
                const utf8* pName = SceneDocumentInternal::findAttributeOrChildText( entityNode, SceneDocumentInternal::kName );
                if ( pName != nullptr )
                    node._name = pName;

                // 파일 id 를 못 읽으면 0(없음)으로 두고 알린다 — 그 엔티티를 가리키는 부착은 풀리지 않고 남는다.
                const utf8* pFileId = entityNode.findAttribute( SceneDocumentInternal::kFileId );
                if ( pFileId != nullptr && StringUtil::parseUint64( pFileId, node._fileId ) == false )
                    SW_LOG_WARNING( "Entity '%#' has an unreadable id '%#' in %# - its children may stay unattached", node._name, pFileId, absPath );

                const utf8* pPrefabGuid = SceneDocumentInternal::findAttributeOrChildText( entityNode, "prefabGuid" );
                if ( pPrefabGuid != nullptr )
                    node._prefabGuid = pPrefabGuid;

                const utf8* pPrefab = SceneDocumentInternal::findAttributeOrChildText( entityNode, SceneDocumentInternal::kPrefab );
                if ( pPrefab != nullptr )
                    node._prefab = pPrefab;

                SceneDocumentInternal::resolvePrefabPathByGuid( node );

                const XmlNode overrideNode = entityNode.findChild( PrefabOverrides::kRootName );
                if ( overrideNode.isValid() )
                    node._prefabOverrideXml = overrideNode.toString();

                XmlNode stateNode = entityNode.findChild( SceneDocumentInternal::kGameObject );
                // 서브트리는 XML 문서가 쓴다(`XmlNode::toString`). 예전에는 여기서 손으로 다시 썼는데, 속성 값의 줄바꿈을 그대로 적어 다시
                // 읽을 때 공백이 됐다(XML 속성 값 정규화 — 여러 줄 대사 · 설명이 한 줄로) — 쓰는 규칙이 두 벌이었다.
                if ( stateNode.isValid() )
                    node._embeddedXml = stateNode.toString();

                if ( node._name.empty() )
                    node._name = SceneDocumentInternal::kDefaultEntity;
                _listEntityNode.push_back( std::move( node ) );
            }
        }

        _bValid = true;
        SW_LOG_INFO( "Loaded '%#' (%# entities) from %#",
                     _name, static_cast<uint32>( _listEntityNode.size() ), absPath );
        return true;
    }

    bool SceneDocument::saveXml( string_view path ) const
    {
        XmlDocument xmlDoc;
        XmlNode     root = xmlDoc.appendRoot( SceneDocumentInternal::kRoot );
        root.appendAttribute( "formatVersion", static_cast<uint32>( AssetFormatVersions::kScene ) );
        root.appendAttribute( "name", _name );
        XmlNode entities = root.appendChild( SceneDocumentInternal::kEntities );

        for ( const EntityNode& entity : _listEntityNode )
        {
            XmlNode entityNode = entities.appendChild( SceneDocumentInternal::kEntity );
            if ( entity._fileId != 0 )
            {
                utf8         arrFileIdText[constant::kMaxBuffer32]{};
                const uint32 fileIdLength = StringUtil::formatNumber( arrFileIdText, constant::kMaxBuffer32, entity._fileId, 10 );
                entityNode.appendAttribute( SceneDocumentInternal::kFileId, string_view( arrFileIdText, fileIdLength ) );
            }
            entityNode.appendAttribute( SceneDocumentInternal::kName, entity._name );
            if ( entity._prefab.empty() == false )
                entityNode.appendAttribute( SceneDocumentInternal::kPrefab, entity._prefab );
            if ( entity._prefabGuid.empty() == false )
            {
                entityNode.appendAttribute( "prefabGuid", entity._prefabGuid );
            }
            else if ( entity._prefab.empty() == false && engine::areEngineServicesBound() )
            {
                Uuid prefabGuid{};
                if ( engine::getResourceManager().getAssetDatabase().tryGetGuid( entity._prefab, prefabGuid ) && prefabGuid.isNull() == false )
                    entityNode.appendAttribute( "prefabGuid", prefabGuid.toString() );
            }
            if ( entity._prefabOverrideXml.empty() == false )
            {
                XmlDocument overrideDoc;
                if ( overrideDoc.parse( entity._prefabOverrideXml ) && overrideDoc.getRoot().isValid() )
                    entityNode.appendClone( overrideDoc.getRoot() );
                else
                    SW_LOG_ERROR( "Entity '%#' has prefab overrides that are not XML - they are not written", entity._name );
            }
            if ( entity._embeddedXml.empty() == false )
            {
                XmlDocument goDoc;
                if ( goDoc.parse( entity._embeddedXml ) )
                {
                    XmlNode goRoot = goDoc.getRoot();
                    if ( goRoot.isValid() )
                        entityNode.appendClone( goRoot );
                }
            }
        }

        const string absPath = ResourceUtil::getWritePath( path );
        if ( absPath.empty() )
        {
            SW_LOG_ERROR( "Cannot resolve save path: %#", path );
            return false;
        }
        FileUtil::ensureParentDirectoryExists( absPath );
        if ( xmlDoc.saveFile( absPath ) == false )
        {
            SW_LOG_ERROR( "Failed to write: %#", absPath );
            return false;
        }
        SW_LOG_INFO( "Saved '%#' (%# entities) -> %#",
                     _name, static_cast<uint32>( _listEntityNode.size() ), absPath );
        return true;
    }

    bool SceneDocument::loadBinary( string_view path )
    {
        *this       = {};
        _sourcePath = path;

        vector<uint8> bytes;
        string        absPath;
        Archive       arch;

        if ( ResourceUtil::readBinaryResource( path, bytes ) )
        {
            arch    = Archive( bytes.data(), bytes.size() );
            absPath = path;
        }
        else
        {
            if ( FileUtil::isAbsolutePath( path ) )
                absPath = FileUtil::normalizeSeparators( path );
            else
            {
                absPath = ResourceUtil::getResourcePath( path );
                if ( absPath.empty() )
                    absPath = path;
            }
            arch = Archive( absPath, true );
        }
        if ( arch.getSize() < 12 )
        {
            SW_LOG_ERROR( "Binary read failed or too small: %#", absPath );
            return false;
        }

        uint32 magic{ 0 };
        arch >> magic;
        if ( magic != SceneDocumentInternal::kSceneBinMagic )
        {
            SW_LOG_ERROR( "Bad binary magic: %#", absPath );
            return false;
        }

        uint32 version{ 0 };
        arch >> version;
        if ( version > SceneDocumentInternal::kSceneBinVersion )
        {
            SW_LOG_ERROR( "Unsupported binary version %# in %#", version, absPath );
            return false;
        }

        arch >> _name;

        uint32 entityCount{ 0 };
        arch >> entityCount;

        // **파일이 말한 개수를 그대로 잡아 두지 않는다.** 엔티티 하나는 길이 앞머리(4바이트)를 쓰는
        // 필드 넷(v1 부터는 다섯, v3 부터는 여섯)이므로, 남은 바이트를 그 최소치로 나눈 것보다 많은 엔티티는 있을 수
        // 없다. 손상된 씬 하나가 수백 기가짜리 `reserve` 가 되는 것을 여기서 막는다. 읽기는 어차피
        // 아래에서 실패하지만, 그 전에 할당이 먼저 터진다.
        const uint64 kMinBytesPerEntity = ( version >= 3 ? 6u : ( version >= 1 ? 5u : 4u ) ) * sizeof( uint32 ) + ( version >= 2 ? sizeof( uint64 ) : 0u );
        const uint64 maxPossibleEntity  = arch.getRemainingBytes() / kMinBytesPerEntity;
        if ( static_cast<uint64>( entityCount ) > maxPossibleEntity )
        {
            SW_LOG_ERROR( "Binary scene claims %# entities but only %# can fit in %# remaining bytes: %#",
                          entityCount, maxPossibleEntity, arch.getRemainingBytes(), absPath );
            return false;
        }

        _listEntityNode.reserve( entityCount );
        for ( uint32 entityIndex = 0; entityIndex < entityCount; ++entityIndex )
        {
            EntityNode node{};
            arch >> node._name >> node._prefab >> node._prefabGuid >> node._embeddedXml;
            if ( version >= 1 )
                arch >> node._embeddedStateBytes;
            if ( version >= 2 )
                arch >> node._fileId;
            if ( version >= 3 )
                arch >> node._prefabOverrideXml;
            // 잘린 파일에서 남은 횟수를 마저 도는 것은 빈 노드를 쌓는 일일 뿐이다.
            if ( arch.isError() )
                break;

            SceneDocumentInternal::resolvePrefabPathByGuid( node );

            _listEntityNode.push_back( std::move( node ) );
        }

        if ( arch.isError() )
        {
            SW_LOG_ERROR( "Binary scene stream corrupted in %#", absPath );
            _bValid = false;
            return false;
        }

        _bValid = true;
        SW_LOG_INFO( "Loaded '%#' (%# entities) from binary %#",
                     _name, static_cast<uint32>( _listEntityNode.size() ), absPath );
        return true;
    }

    bool SceneDocument::saveBinary( string_view path ) const
    {
        Archive arch;
        arch << SceneDocumentInternal::kSceneBinMagic;
        arch << SceneDocumentInternal::kSceneBinVersion;
        arch << _name;
        arch << static_cast<uint32>( _listEntityNode.size() );

        for ( const EntityNode& entity : _listEntityNode )
        {
            string prefabGuid = entity._prefabGuid;
            if ( prefabGuid.empty() && entity._prefab.empty() == false && engine::areEngineServicesBound() )
            {
                Uuid resolvedGuid{};
                if ( engine::getResourceManager().getAssetDatabase().tryGetGuid( entity._prefab, resolvedGuid ) && resolvedGuid.isNull() == false )
                    prefabGuid = resolvedGuid.toString();
            }

            arch << entity._name;
            arch << entity._prefab;
            arch << prefabGuid;
            arch << entity._embeddedXml;
            arch << entity._embeddedStateBytes;
            arch << entity._fileId;
            arch << entity._prefabOverrideXml;
        }

        const string absPath = ResourceUtil::getWritePath( path );
        if ( absPath.empty() )
        {
            SW_LOG_ERROR( "Cannot resolve save path: %#", path );
            return false;
        }
        FileUtil::ensureParentDirectoryExists( absPath );
        const bool bOk = arch.saveFile( absPath );
        if ( bOk )
            SW_LOG_INFO( "Saved binary '%#' (%# entities) -> %#",
                         _name, static_cast<uint32>( _listEntityNode.size() ), absPath );
        return bOk;
    }

    bool SceneDocument::load( string_view path )
    {
        // 쿠킹본 이름은 쿠커와 같은 규칙 하나다(`AssetCookPath`). 씬 이름(`.scene.xml`)이 아니면 쿠킹본이 없다 — 쿠커가 굽지 않는다.
        const string binPath = AssetCookPath::toCookedPath( path );

#if defined( SW_SHIPPING )
        if ( binPath.empty() == false && loadBinary( binPath ) )
            return true;
        SW_LOG_ERROR( "Shipping requires cooked binary scene: %# (cooked name '%#')", path, binPath );
        return false;
#else
        if ( FileUtil::hasExtension( path, ".bin" ) )
            return loadBinary( path );

        if ( binPath.empty() == false && ResourceUtil::hasResource( binPath ) && loadBinary( binPath ) )
            return true;

        return loadXml( path );
#endif
    }

    void SceneDocument::assignMissingFileIds()
    {
        uint64 nextFileId = getMaxFileId() + 1;
        for ( EntityNode& entity : _listEntityNode )
        {
            if ( entity._fileId == 0 )
                entity._fileId = nextFileId++;
        }
    }

    uint64 SceneDocument::getMaxFileId() const
    {
        uint64 maxFileId = 0;
        for ( const EntityNode& entity : _listEntityNode )
            maxFileId = MathUtil::max( maxFileId, entity._fileId );
        return maxFileId;
    }
} // namespace sw
