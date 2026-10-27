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
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/XML/XMLDocument.h"

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
            static constexpr const utf8* kFileID        = "id";
            static constexpr const utf8* kPrefab        = "prefab";
            static constexpr const utf8* kGameObject    = "GameObject";
            static constexpr const utf8* kDefaultEntity = "Entity";
            static constexpr uint32      kBinMagic      = FourCcUtil::make( "SCN1" );
            // 배치: 이름 · 월드 설정(리플렉션 바이너리) · 기본 엔티티 목록 · 셀 목록(셀 좌표 x · z, 그 셀의 엔티티 목록).
            // 엔티티 하나: 이름 · 프리팹 · GUID · XML 상태 · 바이너리 상태 · 파일 id · 덮어쓴 것(`_prefabOverrideXML`).
            // 쿠킹본은 쿠커가 매번 다시 쿠킹하는 산출물이라 이 판만 읽는다 — 배치를 바꾸면 판을 올린다.
            static constexpr uint32 kBinVersion = 4;
            // 엔티티 하나는 길이 앞머리(4바이트)를 쓰는 필드 여섯과 파일 id(8바이트)다 — 파일이 말한 개수를 그대로 잡지 않는 데 쓴다.
            static constexpr uint64 kMinBytesPerEntity = 6u * sizeof( uint32 ) + sizeof( uint64 );
            // 셀 하나는 좌표 둘과 엔티티 수다.
            static constexpr uint64 kMinBytesPerCell = 2u * sizeof( int32 ) + sizeof( uint32 );

            /** @brief 엔티티 목록을 씁니다(수, 엔티티마다 필드). 프리팹 GUID 가 비었으면 에셋 DB 에서 풀어 적습니다. */
            static void writeEntities( Archive& arch, const vector<SceneDocument::SceneObjectNode>& listNode )
            {
                arch << static_cast<uint32>( listNode.size() );
                for ( const SceneDocument::SceneObjectNode& entity : listNode )
                {
                    string     prefabGuid         = entity._prefabGuid;
                    const bool bResolvePrefabGuid = prefabGuid.empty() && entity._prefab.empty() == false && engine::areEngineServicesBound();
                    if ( bResolvePrefabGuid )
                    {
                        UUID resolvedGuid{};
                        if ( engine::getSubsystem<AssetManager>().getAssetDatabase().tryGetGuid( entity._prefab, resolvedGuid ) && resolvedGuid.isNull() == false )
                            prefabGuid = resolvedGuid.toString();
                    }

                    arch << entity._name;
                    arch << entity._prefab;
                    arch << prefabGuid;
                    arch << entity._embeddedXML;
                    arch << entity._embeddedStateBytes;
                    arch << entity._fileID;
                    arch << entity._prefabOverrideXML;
                }
            }

            /**
             * @brief 엔티티 목록을 읽습니다(`writeEntities` 의 짝). 실패하면 오류를 남기고 false 입니다.
             * @details **파일이 말한 개수를 그대로 잡아 두지 않는다.** 남은 바이트를 엔티티 최소 크기로 나눈 것보다 많은 엔티티는 있을 수 없다 —
             *          손상된 씬 하나가 수백 기가짜리 `reserve` 가 되는 것을 여기서 막는다.
             */
            [[nodiscard]] static bool readEntities( Archive& arch, vector<SceneDocument::SceneObjectNode>& outListNode, string_view absPath )
            {
                uint32 entityCount{ 0 };
                arch >> entityCount;
                const uint64 maxPossibleEntity = arch.getRemainingBytes() / kMinBytesPerEntity;
                if ( arch.isError() || static_cast<uint64>( entityCount ) > maxPossibleEntity )
                {
                    SW_LOG_ERROR( "Binary scene claims %# entities but only %# can fit in %# remaining bytes: %#", entityCount, maxPossibleEntity,
                                  arch.getRemainingBytes(), absPath );
                    return false;
                }

                outListNode.reserve( entityCount );
                for ( uint32 entityIndex = 0; entityIndex < entityCount; ++entityIndex )
                {
                    SceneDocument::SceneObjectNode node{};
                    arch >> node._name >> node._prefab >> node._prefabGuid >> node._embeddedXML >> node._embeddedStateBytes >> node._fileID >> node._prefabOverrideXML;
                    // 잘린 파일에서 남은 횟수를 마저 도는 것은 빈 노드를 쌓는 일일 뿐이다.
                    if ( arch.isError() )
                    {
                        SW_LOG_ERROR( "Binary scene stream corrupted in %#", absPath );
                        return false;
                    }
                    resolvePrefabPathByGuid( node );
                    outListNode.push_back( std::move( node ) );
                }
                return true;
            }

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
                if ( engine::getSubsystem<AssetManager>().getAssetDatabase().tryGetPath( guid, resolved ) && resolved.empty() == false )
                    node._prefab = std::move( resolved );
            }

            /**
             * @brief 월드 설정은 판 2부터 필수다 — 없으면 스트리밍 여부를 지어내지 않고 읽지 않는다. 읽고 검증해 @p outSettings 에 담는다.
             * @return 요소가 없거나 읽지 못하거나 쓸 수 없으면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool readWorldSettings( const XMLNode& root, string_view sceneName, string_view absPath, WorldSettings& outSettings )
            {
                const XMLNode worldSettingsNode = root.findChild( WorldSettings::kElementName );
                if ( worldSettingsNode.isValid() == false )
                {
                    SW_LOG_ERROR( "Scene has no <%#> in %# - the scene is not loaded", WorldSettings::kElementName, absPath );
                    return false;
                }
                const bool bWorldSettingsRead = XMLSerializer::deserialize( &outSettings, *WorldSettings::StaticType(), worldSettingsNode.toString() );
                if ( bWorldSettingsRead == false || outSettings.validate( sceneName ) == false )
                {
                    SW_LOG_ERROR( "Scene <%#> could not be read or is not usable in %# - the scene is not loaded", WorldSettings::kElementName, absPath );
                    return false;
                }
                return true;
            }

            /**
             * @brief `<Entity>` 요소 하나를 읽습니다. 엔티티마다 0 이 아닌 파일 id 가 있다(`Scene::serializeToDocument` 가 늘 적는다) — 없거나 못 읽는 문서는 받지 않는다.
             *        부착 · 핸들이 이 값으로 부모를 가리키고, 쿠커는 이 값으로 엔티티를 찾는다.
             * @return 파일 id 가 없거나 틀리면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool readEntityNode( const XMLNode& sceneObjectNode, string_view absPath, SceneDocument::SceneObjectNode& outNode )
            {
                const utf8* pName = sceneObjectNode.findAttribute( kName );
                if ( pName != nullptr )
                    outNode._name = pName;

                const utf8* pFileID        = sceneObjectNode.findAttribute( kFileID );
                const bool  bInvalidFileID = pFileID == nullptr || StringUtil::parseUint64( pFileID, outNode._fileID ) == false || outNode._fileID == 0;
                if ( bInvalidFileID )
                {
                    SW_LOG_ERROR( "Entity '%#' has no valid id ('%#') in %# - the scene is not loaded", outNode._name, pFileID != nullptr ? pFileID : "", absPath );
                    return false;
                }

                const utf8* pPrefabGuid = sceneObjectNode.findAttribute( "prefabGuid" );
                if ( pPrefabGuid != nullptr )
                    outNode._prefabGuid = pPrefabGuid;

                const utf8* pPrefab = sceneObjectNode.findAttribute( kPrefab );
                if ( pPrefab != nullptr )
                    outNode._prefab = pPrefab;

                resolvePrefabPathByGuid( outNode );

                const XMLNode overrideNode = sceneObjectNode.findChild( PrefabOverrides::kRootName );
                if ( overrideNode.isValid() )
                    outNode._prefabOverrideXML = overrideNode.toString();

                XMLNode stateNode = sceneObjectNode.findChild( kGameObject );
                // 서브트리는 XML 문서가 쓴다(`XMLNode::toString`) — 손으로 쓰면 속성 값의 줄바꿈이 그대로 적혀 다시 읽을 때 공백이 된다
                // (XML 속성 값 정규화 — 여러 줄 대사 · 설명이 한 줄로).
                if ( stateNode.isValid() )
                    outNode._embeddedXML = stateNode.toString();

                if ( outNode._name.empty() )
                    outNode._name = kDefaultEntity;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SceneDocument" );

    SW_ASSET_TYPE( Scene, SceneDocument::kAssetType );

    bool SceneDocument::loadXML( string_view path )
    {
        return loadXMLSource( path, ReadSource::Resource );
    }

    bool SceneDocument::loadXMLFromDiskFile( string_view diskPath )
    {
        return loadXMLSource( diskPath, ReadSource::DiskFile );
    }

    bool SceneDocument::loadXMLSource( string_view path, ReadSource source )
    {
        *this       = {};
        _sourcePath = path;

        AssetLoadScope loadScope( "Scene", path );
        XMLDocument    doc;
        string         absPath;
        // 디스크 파일은 부르는 쪽이 이름(`loadXMLFromDiskFile`)으로 고른 것이다 — 리소스 id 읽기가 실패한 뒤 넘어오는 길이 아니다.
        const bool bRead = source == ReadSource::DiskFile ? doc.loadFile( path ) : doc.loadResource( path, &absPath );
        if ( source == ReadSource::DiskFile )
            absPath = string( path );
        if ( bRead == false )
        {
            // 없는 파일과 깨진 파일을 가른다(구문 오류를 "File not found" 로 알리지 않는다).
            if ( doc.isLastErrorNotFound() )
                SW_LOG_ERROR_ID( "Scene.FileNotFound", "Scene not loaded - %#", doc.getLastError() );
            else
                SW_LOG_ERROR_ID( "Scene.FileUnreadable", "Scene not loaded - %#", doc.getLastError() );
            return false;
        }
        loadScope.beginPhase( AssetLoadPhase::Decode );

        XMLNode root = doc.getRoot( SceneDocumentInternal::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing root <Scene>: %#", absPath );
            return false;
        }

        if ( AssetXMLFormat::isCurrentXMLVersion( kAssetType, root ) == false )
        {
            SW_LOG_ERROR( "formatVersion mismatch: %#", absPath );
            return false;
        }

        // 씬 · 엔티티의 값은 속성에만 있다(`saveXML` 이 쓰는 모양).
        const utf8* pSceneName = root.findAttribute( SceneDocumentInternal::kName );

        if ( pSceneName != nullptr )
            _name = pSceneName;
        else
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

        if ( SceneDocumentInternal::readWorldSettings( root, _name, absPath, _worldSettings ) == false )
        {
            *this = {};
            return false;
        }

        const XMLNode entities = root.findChild( SceneDocumentInternal::kEntities );
        if ( entities.isValid() )
        {
            for ( XMLNode sceneObjectNode = entities.findChild( SceneDocumentInternal::kEntity ); sceneObjectNode.isValid();
                  sceneObjectNode         = sceneObjectNode.findNextSibling( SceneDocumentInternal::kEntity ) )
            {
                SceneObjectNode node{};
                if ( SceneDocumentInternal::readEntityNode( sceneObjectNode, absPath, node ) == false )
                {
                    *this = {};
                    return false;
                }
                _listSceneObjectNode.push_back( std::move( node ) );
            }
        }

        _bValid = true;
        loadScope.setSucceeded();
        SW_LOG_INFO( "Loaded '%#' (%# entities) from %#",
                     _name, static_cast<uint32>( _listSceneObjectNode.size() ), absPath );
        return true;
    }

    bool SceneDocument::saveXML( string_view path ) const
    {
        // 셀로 나눈 문서는 쿠킹본이다. XML 에는 셀이 없으므로 쓰면 셀의 엔티티가 사라진다.
        if ( _listStreamingCell.empty() == false )
        {
            SW_LOG_ERROR( "Scene '%#' is split into %# streaming cells (a cooked scene) - it is not saved as XML", _name,
                          static_cast<uint32>( _listStreamingCell.size() ) );
            return false;
        }
        // 읽는 쪽이 받지 않는 월드 설정은 쓰지 않는다.
        if ( _worldSettings.validate( _name ) == false )
            return false;

        XMLDocument xmlDoc;
        XMLNode     root = xmlDoc.appendRoot( SceneDocumentInternal::kRoot );
        root.appendAttribute( "formatVersion", static_cast<uint32>( kAssetType._formatVersion ) );
        root.appendAttribute( "name", _name );
        {
            XMLDocument worldSettingsDoc;
            if ( worldSettingsDoc.parse( XMLSerializer::serialize( &_worldSettings, *WorldSettings::StaticType() ) ) == false ||
                 worldSettingsDoc.getRoot().isValid() == false )
            {
                SW_LOG_ERROR( "Scene '%#' world settings could not be written - the scene is not saved", _name );
                return false;
            }
            root.appendClone( worldSettingsDoc.getRoot() );
        }
        XMLNode entities = root.appendChild( SceneDocumentInternal::kEntities );

        for ( const SceneObjectNode& entity : _listSceneObjectNode )
        {
            // 읽는 쪽(`loadXML`)이 받지 않는 모양은 쓰지 않는다.
            if ( entity._fileID == 0 )
            {
                SW_LOG_ERROR( "Entity '%#' has no id - scene '%#' is not saved", entity._name, _name );
                return false;
            }
            XMLNode      sceneObjectNode = entities.appendChild( SceneDocumentInternal::kEntity );
            utf8         arrFileIDText[constant::kMaxBuffer32]{};
            const uint32 fileIDLength = StringUtil::formatNumber( arrFileIDText, constant::kMaxBuffer32, entity._fileID, 10 );
            sceneObjectNode.appendAttribute( SceneDocumentInternal::kFileID, string_view( arrFileIDText, fileIDLength ) );
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
                if ( engine::getSubsystem<AssetManager>().getAssetDatabase().tryGetGuid( entity._prefab, prefabGuid ) && prefabGuid.isNull() == false )
                    sceneObjectNode.appendAttribute( "prefabGuid", prefabGuid.toString() );
            }
            if ( entity._prefabOverrideXML.empty() == false )
            {
                XMLDocument overrideDoc;
                if ( overrideDoc.parse( entity._prefabOverrideXML ) && overrideDoc.getRoot().isValid() )
                    sceneObjectNode.appendClone( overrideDoc.getRoot() );
                else
                    SW_LOG_ERROR( "Entity '%#' has prefab overrides that are not XML - they are not written", entity._name );
            }
            if ( entity._embeddedXML.empty() == false )
            {
                XMLDocument goDoc;
                if ( goDoc.parse( entity._embeddedXML ) )
                {
                    XMLNode goRoot = goDoc.getRoot();
                    if ( goRoot.isValid() )
                        sceneObjectNode.appendClone( goRoot );
                }
            }
        }

        const string absPath = ResourcePaths::getWritePath( path );
        if ( absPath.empty() )
        {
            SW_LOG_ERROR( "Cannot resolve save path: %#", path );
            return false;
        }
        if ( FileUtil::ensureParentDirectoryExists( absPath ) == false || xmlDoc.saveFile( absPath ) == false )
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

        // 낱개 파일 허용 정책(OS 절대 경로 · 작업 경로 포함)은 `ResourceIO` 한 곳이 본다 — 실패 뒤 디스크를 다시 열지 않는다.
        vector<uint8> bytes;
        const string  absPath{ path };
        Archive       arch;
        if ( ResourceIO::readBinaryResource( ResourceID{ path }, bytes ) )
            arch = Archive( bytes.data(), bytes.size() );
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
            SW_LOG_ERROR_ID( "Scene.UnsupportedBinaryVersion", "Unsupported binary version %# in %# (this build reads %#) - cook the scene again", version, absPath,
                             SceneDocumentInternal::kBinVersion );
            return false;
        }

        arch >> _name;
        if ( BinarySerializer::deserialize( &_worldSettings, *WorldSettings::StaticType(), arch ) == false || _worldSettings.validate( _name ) == false )
        {
            SW_LOG_ERROR( "Binary scene world settings could not be read or are not usable: %#", absPath );
            *this = {};
            return false;
        }

        if ( SceneDocumentInternal::readEntities( arch, _listSceneObjectNode, absPath ) == false )
        {
            *this = {};
            return false;
        }

        uint32 cellCount{ 0 };
        arch >> cellCount;
        const uint64 maxPossibleCell = arch.getRemainingBytes() / SceneDocumentInternal::kMinBytesPerCell;
        if ( arch.isError() || static_cast<uint64>( cellCount ) > maxPossibleCell )
        {
            SW_LOG_ERROR( "Binary scene claims %# streaming cells but only %# can fit: %#", cellCount, maxPossibleCell, absPath );
            *this = {};
            return false;
        }
        // 셀은 스트리밍하는 씬에만 있다 — 스트리밍하지 않는 씬에 셀이 있으면 그 엔티티는 아무도 올리지 않는다.
        if ( cellCount > 0 && _worldSettings._bStreaming == false )
        {
            SW_LOG_ERROR( "Binary scene has %# streaming cells but does not stream: %#", cellCount, absPath );
            *this = {};
            return false;
        }
        _listStreamingCell.resize( cellCount );
        for ( StreamingCell& cell : _listStreamingCell )
        {
            arch >> cell._cell._x >> cell._cell._z;
            if ( SceneDocumentInternal::readEntities( arch, cell._listSceneObjectNode, absPath ) == false )
            {
                *this = {};
                return false;
            }
        }

        if ( arch.isError() )
        {
            SW_LOG_ERROR( "Binary scene stream corrupted in %#", absPath );
            *this = {};
            return false;
        }

        _bValid = true;
        // 이 줄의 모양은 `Scripts/qa/SceneLoadBench.py` 가 읽는다 — 셀 수는 따로 적는다.
        SW_LOG_INFO( "Loaded '%#' (%# entities) from binary %#", _name, static_cast<uint32>( computeTotalEntityCount() ), absPath );
        if ( cellCount > 0 )
            SW_LOG_INFO( "Scene '%#' has %# streaming cells", _name, cellCount );
        return true;
    }

    bool SceneDocument::saveBinary( string_view path ) const
    {
        // 읽는 쪽이 받지 않는 월드 설정 · 셀은 쓰지 않는다.
        if ( _worldSettings.validate( _name ) == false )
            return false;
        if ( _listStreamingCell.empty() == false && _worldSettings._bStreaming == false )
        {
            SW_LOG_ERROR( "Scene '%#' has streaming cells but does not stream - not saved", _name );
            return false;
        }

        Archive arch;
        arch << SceneDocumentInternal::kBinMagic;
        arch << SceneDocumentInternal::kBinVersion;
        arch << _name;
        if ( BinarySerializer::serialize( &_worldSettings, *WorldSettings::StaticType(), arch ) == false )
        {
            SW_LOG_ERROR( "Scene '%#' world settings could not be written - not saved", _name );
            return false;
        }
        SceneDocumentInternal::writeEntities( arch, _listSceneObjectNode );
        arch << static_cast<uint32>( _listStreamingCell.size() );
        for ( const StreamingCell& cell : _listStreamingCell )
        {
            arch << cell._cell._x << cell._cell._z;
            SceneDocumentInternal::writeEntities( arch, cell._listSceneObjectNode );
        }

        const string absPath = ResourcePaths::getWritePath( path );
        if ( absPath.empty() )
        {
            SW_LOG_ERROR( "Cannot resolve save path: %#", path );
            return false;
        }
        const bool bOk = FileUtil::ensureParentDirectoryExists( absPath ) && arch.saveFile( absPath );
        if ( bOk )
            SW_LOG_INFO( "Saved binary '%#' (%# entities, %# streaming cells) -> %#", _name, static_cast<uint32>( computeTotalEntityCount() ),
                         static_cast<uint32>( _listStreamingCell.size() ), absPath );
        return bOk;
    }

    size_t SceneDocument::computeTotalEntityCount() const
    {
        size_t count = _listSceneObjectNode.size();
        for ( const StreamingCell& cell : _listStreamingCell )
        {
            count += cell._listSceneObjectNode.size();
        }
        return count;
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

        const bool bBinaryLoaded = binPath.empty() == false && ResourceIO::hasResource( ResourceID{ binPath } ) && loadBinary( binPath );
        if ( bBinaryLoaded )
            return true;

        return loadXML( path );
#endif
    }

} // namespace sw
