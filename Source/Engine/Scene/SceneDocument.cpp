#include "pch.h"

#include "Engine/Scene/SceneDocument.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/UUID/UUID.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Xml/XmlDocument.h"

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
            static constexpr uint32      kBinMagic      = FourCcUtil::make( "SCN1" );
            // 엔티티 하나: 이름 · 프리팹 · GUID · XML 상태 · 바이너리 상태 · 파일 id · 덮어쓴 것(`_prefabOverrideXml`).
            // 쿠킹본은 쿠커가 매번 다시 쿠킹하는 산출물이라 이 판만 읽는다 — 배치를 바꾸면 판을 올린다.
            static constexpr uint32 kBinVersion = 3;

            /**
             * @brief 프리팹 GUID 로 경로를 다시 풉니다. 파일 이동 · 이름 변경을 자동으로 따라갑니다.
             * @details XML 로더와 바이너리 로더가 함께 씁니다 — 한쪽만 고치면 그 포맷으로 읽은 씬만 옮긴 프리팹을 못 찾습니다.
             */
            static void resolvePrefabPathByGuid( SceneDocument::SceneObjectNode& node )
            {
                if ( node._prefabGuid.empty() || engine::areEngineServicesBound() == false )
                    return;

                UUID guid{};
                if ( UUID::tryParse( node._prefabGuid, guid ) == false || guid.isNull() )
                    return;

                string resolved;
                if ( engine::getAssetManager().getAssetDatabase().tryGetPath( guid, resolved ) && resolved.empty() == false )
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

        AssetLoadScope loadScope( "Scene", path );
        XmlDocument    doc;
        string         absPath;
        if ( doc.loadPath( path, &absPath ) == false )
        {
            // 없는 파일과 깨진 파일을 가른다(구문 오류를 "File not found" 로 알리지 않는다).
            SW_LOG_ERROR( "Scene not loaded - %#", doc.getLastError() );
            return false;
        }
        loadScope.beginPhase( AssetLoadPhase::Decode );

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

        // 씬 · 엔티티의 값은 속성에만 있다(`saveXml` 이 쓰는 모양).
        const utf8* pSceneName = root.findAttribute( SceneDocumentInternal::kName );

        if ( pSceneName != nullptr )
            _name = pSceneName;
        else
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

        XmlNode entities = root.findChild( SceneDocumentInternal::kEntities );

        if ( entities.isValid() )
        {
            for ( XmlNode sceneObjectNode = entities.findChild( SceneDocumentInternal::kEntity ); sceneObjectNode.isValid();
                  sceneObjectNode         = sceneObjectNode.findNextSibling( SceneDocumentInternal::kEntity ) )
            {
                SceneObjectNode node{};
                const utf8*     pName = sceneObjectNode.findAttribute( SceneDocumentInternal::kName );
                if ( pName != nullptr )
                    node._name = pName;

                // 엔티티마다 0 이 아닌 파일 id 가 있다(`Scene::serializeToDocument` 가 늘 적는다). 없거나 못 읽는 문서는 받지 않는다 —
                // 부착 · 핸들이 이 값으로 부모를 가리키고, 쿠커는 이 값으로 엔티티를 찾는다.
                const utf8* pFileId = sceneObjectNode.findAttribute( SceneDocumentInternal::kFileId );
                if ( pFileId == nullptr || StringUtil::parseUint64( pFileId, node._fileId ) == false || node._fileId == 0 )
                {
                    SW_LOG_ERROR( "Entity '%#' has no valid id ('%#') in %# - the scene is not loaded", node._name, pFileId != nullptr ? pFileId : "",
                                  absPath );
                    *this = {};
                    return false;
                }

                const utf8* pPrefabGuid = sceneObjectNode.findAttribute( "prefabGuid" );
                if ( pPrefabGuid != nullptr )
                    node._prefabGuid = pPrefabGuid;

                const utf8* pPrefab = sceneObjectNode.findAttribute( SceneDocumentInternal::kPrefab );
                if ( pPrefab != nullptr )
                    node._prefab = pPrefab;

                SceneDocumentInternal::resolvePrefabPathByGuid( node );

                const XmlNode overrideNode = sceneObjectNode.findChild( PrefabOverrides::kRootName );
                if ( overrideNode.isValid() )
                    node._prefabOverrideXml = overrideNode.toString();

                XmlNode stateNode = sceneObjectNode.findChild( SceneDocumentInternal::kGameObject );
                // 서브트리는 XML 문서가 쓴다(`XmlNode::toString`) — 손으로 쓰면 속성 값의 줄바꿈이 그대로 적혀 다시 읽을 때 공백이 된다
                // (XML 속성 값 정규화 — 여러 줄 대사 · 설명이 한 줄로).
                if ( stateNode.isValid() )
                    node._embeddedXml = stateNode.toString();

                if ( node._name.empty() )
                    node._name = SceneDocumentInternal::kDefaultEntity;
                _listSceneObjectNode.push_back( std::move( node ) );
            }
        }

        _bValid = true;
        loadScope.setSucceeded();
        SW_LOG_INFO( "Loaded '%#' (%# entities) from %#",
                     _name, static_cast<uint32>( _listSceneObjectNode.size() ), absPath );
        return true;
    }

    bool SceneDocument::saveXml( string_view path ) const
    {
        XmlDocument xmlDoc;
        XmlNode     root = xmlDoc.appendRoot( SceneDocumentInternal::kRoot );
        root.appendAttribute( "formatVersion", static_cast<uint32>( AssetFormatVersions::kScene ) );
        root.appendAttribute( "name", _name );
        XmlNode entities = root.appendChild( SceneDocumentInternal::kEntities );

        for ( const SceneObjectNode& entity : _listSceneObjectNode )
        {
            // 읽는 쪽(`loadXml`)이 받지 않는 모양은 쓰지 않는다.
            if ( entity._fileId == 0 )
            {
                SW_LOG_ERROR( "Entity '%#' has no id - scene '%#' is not saved", entity._name, _name );
                return false;
            }
            XmlNode      sceneObjectNode = entities.appendChild( SceneDocumentInternal::kEntity );
            utf8         arrFileIdText[constant::kMaxBuffer32]{};
            const uint32 fileIdLength = StringUtil::formatNumber( arrFileIdText, constant::kMaxBuffer32, entity._fileId, 10 );
            sceneObjectNode.appendAttribute( SceneDocumentInternal::kFileId, string_view( arrFileIdText, fileIdLength ) );
            sceneObjectNode.appendAttribute( SceneDocumentInternal::kName, entity._name );
            if ( entity._prefab.empty() == false )
                sceneObjectNode.appendAttribute( SceneDocumentInternal::kPrefab, entity._prefab );
            if ( entity._prefabGuid.empty() == false )
            {
                sceneObjectNode.appendAttribute( "prefabGuid", entity._prefabGuid );
            }
            else if ( entity._prefab.empty() == false && engine::areEngineServicesBound() )
            {
                UUID prefabGuid{};
                if ( engine::getAssetManager().getAssetDatabase().tryGetGuid( entity._prefab, prefabGuid ) && prefabGuid.isNull() == false )
                    sceneObjectNode.appendAttribute( "prefabGuid", prefabGuid.toString() );
            }
            if ( entity._prefabOverrideXml.empty() == false )
            {
                XmlDocument overrideDoc;
                if ( overrideDoc.parse( entity._prefabOverrideXml ) && overrideDoc.getRoot().isValid() )
                    sceneObjectNode.appendClone( overrideDoc.getRoot() );
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
                        sceneObjectNode.appendClone( goRoot );
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
                     _name, static_cast<uint32>( _listSceneObjectNode.size() ), absPath );
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
        if ( magic != SceneDocumentInternal::kBinMagic )
        {
            SW_LOG_ERROR( "Bad binary magic: %#", absPath );
            return false;
        }

        uint32 version{ 0 };
        arch >> version;
        if ( version != SceneDocumentInternal::kBinVersion )
        {
            SW_LOG_ERROR( "Unsupported binary version %# in %# (this build reads %#) - cook the scene again", version, absPath,
                          SceneDocumentInternal::kBinVersion );
            return false;
        }

        arch >> _name;

        uint32 entityCount{ 0 };
        arch >> entityCount;

        // **파일이 말한 개수를 그대로 잡아 두지 않는다.** 엔티티 하나는 길이 앞머리(4바이트)를 쓰는 필드 여섯과 파일 id(8바이트)이므로,
        // 남은 바이트를 그 최소치로 나눈 것보다 많은 엔티티는 있을 수 없다. 손상된 씬 하나가 수백 기가짜리 `reserve` 가 되는 것을
        // 여기서 막는다. 읽기는 어차피 아래에서 실패하지만, 그 전에 할당이 먼저 터진다.
        const uint64 kMinBytesPerEntity = 6u * sizeof( uint32 ) + sizeof( uint64 );
        const uint64 maxPossibleEntity  = arch.getRemainingBytes() / kMinBytesPerEntity;
        if ( static_cast<uint64>( entityCount ) > maxPossibleEntity )
        {
            SW_LOG_ERROR( "Binary scene claims %# entities but only %# can fit in %# remaining bytes: %#",
                          entityCount, maxPossibleEntity, arch.getRemainingBytes(), absPath );
            return false;
        }

        _listSceneObjectNode.reserve( entityCount );
        for ( uint32 entityIndex = 0; entityIndex < entityCount; ++entityIndex )
        {
            SceneObjectNode node{};
            arch >> node._name >> node._prefab >> node._prefabGuid >> node._embeddedXml >> node._embeddedStateBytes >> node._fileId >> node._prefabOverrideXml;
            // 잘린 파일에서 남은 횟수를 마저 도는 것은 빈 노드를 쌓는 일일 뿐이다.
            if ( arch.isError() )
                break;

            SceneDocumentInternal::resolvePrefabPathByGuid( node );

            _listSceneObjectNode.push_back( std::move( node ) );
        }

        if ( arch.isError() )
        {
            SW_LOG_ERROR( "Binary scene stream corrupted in %#", absPath );
            _bValid = false;
            return false;
        }

        _bValid = true;
        SW_LOG_INFO( "Loaded '%#' (%# entities) from binary %#",
                     _name, static_cast<uint32>( _listSceneObjectNode.size() ), absPath );
        return true;
    }

    bool SceneDocument::saveBinary( string_view path ) const
    {
        Archive arch;
        arch << SceneDocumentInternal::kBinMagic;
        arch << SceneDocumentInternal::kBinVersion;
        arch << _name;
        arch << static_cast<uint32>( _listSceneObjectNode.size() );

        for ( const SceneObjectNode& entity : _listSceneObjectNode )
        {
            string prefabGuid = entity._prefabGuid;
            if ( prefabGuid.empty() && entity._prefab.empty() == false && engine::areEngineServicesBound() )
            {
                UUID resolvedGuid{};
                if ( engine::getAssetManager().getAssetDatabase().tryGetGuid( entity._prefab, resolvedGuid ) && resolvedGuid.isNull() == false )
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
                         _name, static_cast<uint32>( _listSceneObjectNode.size() ), absPath );
        return bOk;
    }

    bool SceneDocument::load( string_view path )
    {
        // 쿠킹본 이름은 쿠커와 같은 규칙 하나다(`AssetCookPath`). 씬 이름(`.scene.xml`)이 아니면 쿠킹본이 없다 — 쿠커가 쿠킹하지 않는다.
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

} // namespace sw
