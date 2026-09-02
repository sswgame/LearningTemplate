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
            static constexpr const utf8* kRoot            = "Scene";
            static constexpr const utf8* kName            = "name";
            static constexpr const utf8* kEntities        = "entities";
            static constexpr const utf8* kEntity          = "entity";
            static constexpr const utf8* kPrefab          = "prefab";
            static constexpr const utf8* kGameObject      = "GameObject";
            static constexpr const utf8* kDefaultEntity   = "Entity";
            static constexpr uint32      kSceneBinMagic   = 0x53434E31u; // 'SCN1'
            static constexpr uint32      kSceneBinVersion = 0;

            static string absoluteWritePath( string_view path )
            {
                string result = ResourceUtil::getResourcePath( path );
                if ( result.empty() )
                {
                    result = ResourceUtil::makeAbsolutePath( path );
                    if ( result.empty() )
                        result = path;
                }
                return result;
            }

            static void appendNodeXml( StringBuilder<constant::kMaxBuffer8192>& out, XmlNode node )
            {
                if ( node.isValid() == false )
                    return;

                const utf8* pNodeName = node.name();
                if ( StringUtil::isNullOrEmpty( pNodeName ) )
                    return;

                out.append( '<' ).append( pNodeName );
                for ( XmlAttribute attr = node.firstAttr(); attr; attr = attr.next() )
                {
                    out.append( ' ' ).append( attr.name() ).append( "=\"" ).append( XmlDocument::escapeString( attr.value() != nullptr ? attr.value() : "" ) ).append( '"' );
                }

                bool bHasElementChild = false;
                for ( XmlNode child = node.child(); child; child = child.next() )
                {
                    const utf8* pChildName = child.name();
                    if ( StringUtil::isNullOrEmpty( pChildName ) == false )
                    {
                        bHasElementChild = true;
                        break;
                    }
                }

                const bool bHasValue = StringUtil::isNullOrEmpty( node.text() ) == false;
                if ( bHasElementChild == false && bHasValue == false )
                {
                    out.append( "/>" );
                    return;
                }

                out.append( '>' );
                if ( bHasValue )
                    out.append( XmlDocument::escapeString( node.text() ) );

                for ( XmlNode child = node.child(); child; child = child.next() )
                {
                    const utf8* pChildName = child.name();
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
        if ( doc.loadResource( path, &absPath ) == false )
        {
            // Absolute fallback
            if ( doc.loadFile( path ) == false )
            {
                SW_LOG_ERROR( "File not found: %#", path );
                return false;
            }
            absPath = path;
        }

        XmlNode root = doc.root( SceneDocumentInternal::kRoot );
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

        const utf8* pSceneName = root.attr( "name" );
        if ( pSceneName == nullptr )
        {
            pSceneName = root.childText( SceneDocumentInternal::kName );
        }

        if ( pSceneName != nullptr )
        {
            _name = pSceneName;
        }
        else
        {
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );
        }

        XmlNode entities = root.child( SceneDocumentInternal::kEntities );

        if ( entities.isValid() )
        {
            for ( XmlNode entityNode = entities.child( SceneDocumentInternal::kEntity ); entityNode.isValid();
                  entityNode         = entityNode.next( SceneDocumentInternal::kEntity ) )
            {
                EntityNode  node{};
                const utf8* pName = entityNode.attr( SceneDocumentInternal::kName );
                if ( pName == nullptr )
                {
                    pName = entityNode.childText( SceneDocumentInternal::kName );
                }
                if ( pName != nullptr )
                {
                    node._name = pName;
                }

                const utf8* pPrefabGuid = entityNode.attr( "prefabGuid" );
                if ( pPrefabGuid == nullptr )
                {
                    pPrefabGuid = entityNode.childText( "prefabGuid" );
                }
                if ( pPrefabGuid != nullptr )
                {
                    node._prefabGuid = pPrefabGuid;
                }

                const utf8* pPrefab = entityNode.attr( SceneDocumentInternal::kPrefab );
                if ( pPrefab == nullptr )
                {
                    pPrefab = entityNode.childText( SceneDocumentInternal::kPrefab );
                }
                if ( pPrefab != nullptr )
                {
                    node._prefab = pPrefab;
                }

                // GUID 기반 경로 해석 (파일 이동/이름 변경에 대한 자동 복구)
                if ( node._prefabGuid.empty() == false && engine::areEngineServicesBound() )
                {
                    Uuid guid{};
                    if ( Uuid::tryParse( node._prefabGuid, guid ) && guid.isNull() == false )
                    {
                        const string* pResolved = engine::getResourceManager().getAssetDatabase().getPath( guid );
                        if ( pResolved != nullptr && pResolved->empty() == false )
                            node._prefab = *pResolved;
                    }
                }

                XmlNode stateNode = entityNode.child( SceneDocumentInternal::kGameObject );
                if ( stateNode.isValid() )
                {
                    StringBuilder<constant::kMaxBuffer8192> stateSb;
                    SceneDocumentInternal::appendNodeXml( stateSb, stateNode );
                    node._embeddedXml = stateSb.view();
                }

                if ( node._name.empty() )
                {
                    node._name = SceneDocumentInternal::kDefaultEntity;
                }
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
        root.appendAttr( "formatVersion", static_cast<uint32>( AssetFormatVersions::kScene ) );
        root.appendAttr( "name", _name );
        XmlNode entities = root.appendChild( SceneDocumentInternal::kEntities );

        for ( const EntityNode& entity : _listEntityNode )
        {
            XmlNode entityNode = entities.appendChild( SceneDocumentInternal::kEntity );
            entityNode.appendAttr( SceneDocumentInternal::kName, entity._name );
            if ( entity._prefab.empty() == false )
                entityNode.appendAttr( SceneDocumentInternal::kPrefab, entity._prefab );
            if ( entity._prefabGuid.empty() == false )
            {
                entityNode.appendAttr( "prefabGuid", entity._prefabGuid );
            }
            else if ( entity._prefab.empty() == false && engine::areEngineServicesBound() )
            {
                const Uuid* pGuid = engine::getResourceManager().getAssetDatabase().getGuid( entity._prefab );
                if ( pGuid != nullptr && pGuid->isNull() == false )
                    entityNode.appendAttr( "prefabGuid", pGuid->toString() );
            }
            if ( entity._embeddedXml.empty() == false )
            {
                XmlDocument goDoc;
                if ( goDoc.parse( entity._embeddedXml ) )
                {
                    XmlNode goRoot = goDoc.root();
                    if ( goRoot.isValid() )
                        entityNode.appendClone( goRoot );
                }
            }
        }

        const string absPath = SceneDocumentInternal::absoluteWritePath( path );
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

        string absPath = ResourceUtil::getResourcePath( path );
        if ( absPath.empty() )
            absPath = path;

        Archive arch( absPath, true );
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

        _listEntityNode.reserve( entityCount );
        for ( uint32 entityIndex = 0; entityIndex < entityCount; ++entityIndex )
        {
            EntityNode node{};
            arch >> node._name >> node._prefab >> node._prefabGuid >> node._embeddedXml;

            if ( node._prefabGuid.empty() == false && engine::areEngineServicesBound() )
            {
                Uuid guid{};
                if ( Uuid::tryParse( node._prefabGuid, guid ) && guid.isNull() == false )
                {
                    const string* pResolved = engine::getResourceManager().getAssetDatabase().getPath( guid );
                    if ( pResolved != nullptr && pResolved->empty() == false )
                        node._prefab = *pResolved;
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
                const Uuid* pGuid = engine::getResourceManager().getAssetDatabase().getGuid( entity._prefab );
                if ( pGuid != nullptr && pGuid->isNull() == false )
                    prefabGuid = pGuid->toString();
            }

            arch << entity._name;
            arch << entity._prefab;
            arch << prefabGuid;
            arch << entity._embeddedXml;
        }

        const string absPath = SceneDocumentInternal::absoluteWritePath( path );
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
