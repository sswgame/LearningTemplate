#include "pch.h"

#include "Engine/Scene/SceneDocument.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Uuid/Uuid.h"

#include "Engine/Common/EngineServices.h"
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
            static constexpr const utf8* kPrefab        = "prefab";
            static constexpr const utf8* kGameObject    = "GameObject";
            static constexpr const utf8* kDefaultEntity = "Entity";
            static constexpr uint32      kSceneBinMagic = 0x53434E31u; // 'SCN1'
            // v1 부터 엔티티마다 **바이너리 상태**가 한 필드 더 붙는다(비어 있을 수 있다). v0 은
            // XML 문자열만 실려 있었고, 그 파일도 계속 읽는다 — 아래 읽기가 버전으로 갈린다.
            static constexpr uint32 kSceneBinVersion = 1;

            static void appendNodeXml( StringBuilder<constant::kMaxBuffer8192>& out, XmlNode node )
            {
                if ( node.isValid() == false )
                    return;

                const utf8* pNodeName = node.getName();
                if ( StringUtil::isNullOrEmpty( pNodeName ) )
                    return;

                out.append( '<' ).append( pNodeName );
                for ( XmlAttribute attr = node.getFirstAttribute(); attr; attr = attr.getNext() )
                {
                    out.append( ' ' ).append( attr.getName() ).append( "=\"" ).append( XmlDocument::escapeString( attr.getValue() != nullptr ? attr.getValue() : "" ) ).append( '"' );
                }

                bool bHasElementChild = false;
                for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pChildName = child.getName();
                    if ( StringUtil::isNullOrEmpty( pChildName ) == false )
                    {
                        bHasElementChild = true;
                        break;
                    }
                }

                const bool bHasValue = StringUtil::isNullOrEmpty( node.getText() ) == false;
                if ( bHasElementChild == false && bHasValue == false )
                {
                    out.append( "/>" );
                    return;
                }

                out.append( '>' );
                if ( bHasValue )
                    out.append( XmlDocument::escapeString( node.getText() ) );

                for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pChildName = child.getName();
                    if ( StringUtil::isNullOrEmpty( pChildName ) == false )
                        appendNodeXml( out, child );
                }

                out.append( "</" ).append( pNodeName ).append( '>' );
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
            SW_LOG_ERROR( "File not found: %#", path );
            return false;
        }

        XmlNode root = doc.getRoot( SceneDocumentInternal::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing root <Scene>: %#", absPath );
            return false;
        }

        if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::Scene, doc, root, AssetFormatVersions::kScene ) ==
             false )
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
                const utf8* pName = entityNode.findAttribute( SceneDocumentInternal::kName );
                if ( pName == nullptr )
                    pName = entityNode.findChildText( SceneDocumentInternal::kName );
                if ( pName != nullptr )
                    node._name = pName;

                const utf8* pPrefabGuid = entityNode.findAttribute( "prefabGuid" );
                if ( pPrefabGuid == nullptr )
                    pPrefabGuid = entityNode.findChildText( "prefabGuid" );
                if ( pPrefabGuid != nullptr )
                    node._prefabGuid = pPrefabGuid;

                const utf8* pPrefab = entityNode.findAttribute( SceneDocumentInternal::kPrefab );
                if ( pPrefab == nullptr )
                    pPrefab = entityNode.findChildText( SceneDocumentInternal::kPrefab );
                if ( pPrefab != nullptr )
                    node._prefab = pPrefab;

                // GUID 로 경로를 다시 푼다(파일 이동 · 이름 변경을 자동으로 따라간다)
                if ( node._prefabGuid.empty() == false && engine::areEngineServicesBound() )
                {
                    Uuid guid{};
                    if ( Uuid::tryParse( node._prefabGuid, guid ) && guid.isNull() == false )
                    {
                        string resolved;
                        if ( engine::getResourceManager().getAssetDatabase().tryGetPath( guid, resolved ) && resolved.empty() == false )
                            node._prefab = std::move( resolved );
                    }
                }

                XmlNode stateNode = entityNode.findChild( SceneDocumentInternal::kGameObject );
                if ( stateNode.isValid() )
                {
                    StringBuilder<constant::kMaxBuffer8192> stateSb;
                    SceneDocumentInternal::appendNodeXml( stateSb, stateNode );
                    node._embeddedXml = stateSb.view();
                }

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
        FileUtil::createParentDirectory( absPath );
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
        // 필드 넷(v1 부터는 다섯)이므로, 남은 바이트를 그 최소치로 나눈 것보다 많은 엔티티는 있을 수
        // 없다. 손상된 씬 하나가 수백 기가짜리 `reserve` 가 되는 것을 여기서 막는다. 읽기는 어차피
        // 아래에서 실패하지만, 그 전에 할당이 먼저 터진다.
        const uint64 kMinBytesPerEntity = ( version >= 1 ? 5u : 4u ) * sizeof( uint32 );
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
            // 잘린 파일에서 남은 횟수를 마저 도는 것은 빈 노드를 쌓는 일일 뿐이다.
            if ( arch.isError() )
                break;

            if ( node._prefabGuid.empty() == false && engine::areEngineServicesBound() )
            {
                Uuid guid{};
                if ( Uuid::tryParse( node._prefabGuid, guid ) && guid.isNull() == false )
                {
                    string resolved;
                    if ( engine::getResourceManager().getAssetDatabase().tryGetPath( guid, resolved ) && resolved.empty() == false )
                        node._prefab = std::move( resolved );
                }
            }

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
        }

        const string absPath = ResourceUtil::getWritePath( path );
        if ( absPath.empty() )
        {
            SW_LOG_ERROR( "Cannot resolve save path: %#", path );
            return false;
        }
        FileUtil::createParentDirectory( absPath );
        const bool bOk = arch.saveFile( absPath );
        if ( bOk )
            SW_LOG_INFO( "Saved binary '%#' (%# entities) -> %#",
                         _name, static_cast<uint32>( _listEntityNode.size() ), absPath );
        return bOk;
    }

    bool SceneDocument::load( string_view path )
    {
        string     binPath( path );
        const bool bXml = FileUtil::hasExtension( binPath, ".xml" );
        if ( bXml )
            binPath.replace( binPath.size() - 4, 4, ".bin" );
        else if ( binPath.find( ".scene" ) != string::npos && FileUtil::hasExtension( binPath, ".bin" ) == false )
            binPath += ".bin";

#if defined( SW_SHIPPING )
        if ( loadBinary( binPath ) )
            return true;
        SW_LOG_ERROR( "Shipping requires cooked binary scene: %#", binPath );
        return false;
#else
        if ( FileUtil::hasExtension( path, ".bin" ) )
            return loadBinary( path );

        if ( ResourceUtil::hasResource( binPath ) && loadBinary( binPath ) )
            return true;

        return loadXml( path );
#endif
    }
} // namespace sw
