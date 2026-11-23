#include "pch.h"

#include "Engine/Scene/SceneDocument.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/Serialization/GameObjectFormatUpgrade.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/DataInheritanceSchemaRegistry.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    namespace
    {
        struct SceneDocumentInternal
        {
            static constexpr const utf8* kRoot          = "Scene";
            static constexpr const utf8* kEntities      = "entities";
            static constexpr const utf8* kEntity        = "entity";
            static constexpr const utf8* kGameObject    = "GameObject";
            static constexpr const utf8* kDefaultEntity = "Entity";
            static constexpr uint32      kBinMagic      = FourCCUtil::make( "SCN1" );
            // 배치: 이름 · 월드 설정(리플렉션 바이너리) · 기본 엔티티 목록 · 셀 목록(셀 좌표 x · z, 그 셀의 엔티티 목록).
            // 엔티티 하나: 이름 · 프리팹 경로 · XML 상태 · 바이너리 상태 · 파일 id · 덮어쓴 것(`_prefabOverrideXML`) · id 범위 · 앵커 파일 id · 레이어 목록 · 인스턴스 덮어쓰기 목록.
            // 프리팹은 경로 하나로 가리킨다(판 5 에서 GUID 칸을 지웠다). 판 6 에서 조립 칸을 더했다. 판 7 부터 상태의 컴포넌트 키가 고정 지역 id(`_localID`, `ComponentStableKey` — 감사 A-2)다. 셀에는 레이어 목록이 붙는다.
            // 판 8 부터 셀 목록은 색인(좌표 · 레이어 · 내용 구간)이고 셀 내용(엔티티 목록)은 색인 뒤에 셀마다 이어 붙는다 — 씬을 열 때 색인만 풀고 내용은 셀을 올릴 때 구간으로 읽는다(감사 A-13).
            // 판 9 부터 매직 · 판 뒤에 머리 길이(이 고정 머리 포함) · 내용 길이가 온다 — 로더는 고정 머리와 머리만 구간으로 읽고 셀 내용은 읽지 않는다.
            // 구간의 시작은 머리 끝에서 센 값으로 적는다(머리 길이가 레이어 이름에 달려 쓰기 전에 절대 위치를 모른다).
            // 판 10 부터 셀 색인 뒤에 셀 엔티티의 가장 큰 파일 id(`_maxCellFileID`)가 온다 — 색인만 읽은 씬이 새 파일 id 를 내려간 셀과 겹치지 않게 고른다.
            // 쿠킹본은 쿠커가 매번 다시 쿠킹하는 산출물이라 이 판만 읽는다 — 배치를 바꾸면 판을 올린다.
            // 판 11 부터 셀 색인의 구간 뒤에 그 조각의 HLOD 대리 파일 id(`StreamingCell::_hlodFileID`, 0 이면 없음)가 온다.
            static constexpr uint32 kBinVersion = 11;
            // 고정 머리: 매직 · 판 · 머리 길이 · 내용 길이.
            static constexpr uint64 kFixedHeadBytes = 2u * sizeof( uint32 ) + 2u * sizeof( uint64 );
            // 엔티티 하나는 길이 앞머리(4바이트)를 쓰는 필드 다섯, 목록 수 둘, 파일 id · id 범위 · 앵커 id(8바이트씩)다 — 파일이 말한 개수를 그대로 잡지 않는 데 쓴다.
            static constexpr uint32 kMinBytesPerEntity = 7u * sizeof( uint32 ) + 3u * sizeof( uint64 );
            /** @brief 지운 칸입니다. 프리팹은 경로로만 가리킨다 — 이 속성이 남은 씬은 오류로 읽지 않는다(옛 GUID 를 말없이 버리면 그 엔티티가 무엇을 가리켰는지 모른다). */
            static constexpr const utf8* kRemovedPrefabGUID = "prefabGUID";
            // 셀 색인 한 줄은 좌표 둘 · 레이어 수 · 내용 구간(시작 · 길이)이다.
            static constexpr uint32 kMinBytesPerCell = 2u * sizeof( int32 ) + sizeof( uint32 ) + 2u * sizeof( uint64 );
            /** @brief 인스턴스 멤버의 이름에서 앵커 이름과 내부 이름을 가르는 글자입니다. 네트워크 씬 번호가 이름의 해시라(`NetObjectTable`) 멤버 이름은 인스턴스마다 달라야 한다. */
            static constexpr const utf8* kInstanceNameSeparator = "/";

            /** @brief 씬 파일의 상속 규칙입니다. 엔티티는 파일 id 가 열쇠이고, `<GameObject>` · `<PrefabOverrides>` 는 자식이 적으면 통째로 바뀐다(컴포넌트 목록의 열쇠가 칸 값이 아니다 — `DataInheritance.md`). */
            static DataInheritanceSchema makeInheritanceSchema()
            {
                DataInheritanceSchema schema{ kRoot };
                schema._listKeptAttribute.emplace_back( "formatVersion" );
                DataInheritanceSchema settings = DataInheritanceSchema::makeFromType( *SceneSettings::StaticType() );
                settings._name                 = SceneSettings::kElementName;
                schema._listField.push_back( std::move( settings ) );
                schema._listField.push_back( DataInheritanceSchema::makeKeyedList( kEntities, "id" ) );
                return schema;
            }

            /** @brief 글 목록을 씁니다(수, 글마다). */
            static void writeTextList( Archive& arch, const vector<string>& listText )
            {
                arch << static_cast<uint32>( listText.size() );
                for ( const string& text : listText )
                {
                    arch << text;
                }
            }

            /** @brief `writeTextList` 의 짝입니다. 파일이 말한 개수가 남은 바이트에 들어가지 않으면 false 입니다. */
            [[nodiscard]] static bool readTextList( Archive& arch, vector<string>& outListText )
            {
                uint32 count{ 0 };
                count = arch.readCount( sizeof( uint32 ) );
                if ( arch.isError() )
                    return false;
                outListText.resize( count );
                for ( string& text : outListText )
                {
                    arch >> text;
                }
                return arch.isError() == false;
            }

            /** @brief 레이어 칸(`Winter,Night`)을 정렬한 이름 목록으로 읽습니다. */
            static void parseDataLayers( string_view text, vector<string>& outListLayer )
            {
                outListLayer.clear();
                StringSplitOptions options{};
                options._bTrim      = true;
                options._bDropEmpty = true;
                for ( const string_view name : StringUtil::split( text, ",", options ) )
                {
                    outListLayer.emplace_back( name );
                }
                VectorUtil::sortUnique( outListLayer );
            }

            /** @brief 레이어 이름 목록을 칸 글로 씁니다. */
            static string joinDataLayers( const vector<string>& listLayer )
            {
                string text;
                for ( const string& name : listLayer )
                {
                    if ( text.empty() == false )
                        text += ',';
                    text += name;
                }
                return text;
            }

            /** @brief 엔티티가 쓰는 레이어를 씬이 모두 선언했는지 봅니다 — 이름을 잘못 적으면 그 엔티티가 영영 오르지 않는다. 없으면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool validateDataLayersDeclared( const SceneDocument& doc, string_view absPath )
            {
                for ( const SceneDocument::SceneObjectNode& node : doc._listSceneObjectNode )
                {
                    for ( const string& layer : node._listDataLayer )
                    {
                        if ( doc._sceneSettings.findDataLayer( layer ) != nullptr )
                            continue;
                        SW_LOG_ERROR_ID( "SceneDocument.DataLayerNotDeclared", "Entity '%#' is on data layer '%#' that <%#> does not declare in %# - the scene is not loaded", node._name, layer, SceneSettings::kElementName, absPath );
                        return false;
                    }
                }
                return true;
            }

            /** @brief 엔티티 목록을 씁니다(수, 엔티티마다 필드). */
            static void writeEntities( Archive& arch, const vector<SceneDocument::SceneObjectNode>& listNode )
            {
                arch << static_cast<uint32>( listNode.size() );
                for ( const SceneDocument::SceneObjectNode& entity : listNode )
                {
                    arch << entity._name;
                    arch << entity._prefab;
                    arch << entity._embeddedXML;
                    arch << entity._embeddedStateBytes;
                    arch << entity._fileID;
                    arch << entity._prefabOverrideXML;
                    arch << entity._idScope;
                    arch << entity._instanceAnchorFileID;
                    writeTextList( arch, entity._listDataLayer );
                    writeTextList( arch, entity._listInstanceOverrideXML );
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
                entityCount = arch.readCount( kMinBytesPerEntity );
                if ( arch.isError() )
                {
                    SW_LOG_ERROR_ID( "SceneDocument.BinarySceneClaimsEntities", "Binary scene claims more entities than can fit in its %# remaining bytes: %#", arch.getRemainingBytes(), absPath );
                    return false;
                }

                outListNode.reserve( entityCount );
                for ( uint32 entityIndex = 0; entityIndex < entityCount; ++entityIndex )
                {
                    SceneDocument::SceneObjectNode node{};
                    arch >> node._name >> node._prefab >> node._embeddedXML >> node._embeddedStateBytes >> node._fileID >> node._prefabOverrideXML >> node._idScope >> node._instanceAnchorFileID;
                    const bool bListsRead = readTextList( arch, node._listDataLayer ) && readTextList( arch, node._listInstanceOverrideXML );
                    // 잘린 파일에서 남은 횟수를 마저 도는 것은 빈 노드를 쌓는 일일 뿐이다.
                    if ( bListsRead == false || arch.isError() )
                    {
                        SW_LOG_ERROR_ID( "SceneDocument.BinarySceneStreamCorrupted", "Binary scene stream corrupted in %#", absPath );
                        return false;
                    }
                    outListNode.push_back( std::move( node ) );
                }
                return true;
            }

            /**
             * @brief 월드 설정은 판 2부터 필수다 — 없으면 스트리밍 여부를 지어내지 않고 읽지 않는다. 읽고 검증해 @p outSettings 에 담는다.
             * @return 요소가 없거나 읽지 못하거나 쓸 수 없으면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool readSceneSettings( const XMLNode& root, string_view sceneName, string_view absPath, SceneSettings& outSettings )
            {
                const XMLNode sceneSettingsNode = root.findChild( SceneSettings::kElementName );
                if ( sceneSettingsNode.isValid() == false )
                {
                    SW_LOG_ERROR_ID( "SceneDocument.SceneNoSceneNot", "Scene has no <%#> in %# - the scene is not loaded", SceneSettings::kElementName, absPath );
                    return false;
                }
                const bool bSceneSettingsRead = XMLSerializer::deserialize( &outSettings, *SceneSettings::StaticType(), sceneSettingsNode.toString() );
                if ( bSceneSettingsRead == false || outSettings.validate( sceneName ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneDocument.SceneCouldNotRead", "Scene <%#> could not be read or is not usable in %# - the scene is not loaded", SceneSettings::kElementName, absPath );
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
                // 지운 칸은 옮겨 갈 곳을 알려 준다(모르는 칸 오류보다 먼저).
                if ( sceneObjectNode.findAttribute( kRemovedPrefabGUID ) != nullptr )
                {
                    SW_LOG_ERROR_ID( "SceneDocument.EntityPrefabGUIDRemoved", "Entity '%#' still has '%#' in %# - prefabs are referenced by path only (put the prefab's path in 'prefab'); the scene is not loaded",
                                     sceneObjectNode.getAttributeText( "name" ), kRemovedPrefabGUID, absPath );
                    return false;
                }
                // 칸 · 0 이 아닌 id 는 SceneEntityParams 의 PROPERTY 가 본다.
                SceneEntityParams params;
                if ( XMLFields::readProperties( sceneObjectNode, params, absPath ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneDocument.EntityNoValidId", "Entity '%#' is not readable in %# - the scene is not loaded", params._name, absPath );
                    return false;
                }
                outNode._fileID        = params._id;
                outNode._name          = std::move( params._name );
                outNode._prefab        = params._prefab.path();
                outNode._sceneInstance = std::move( params._scene );
                parseDataLayers( params._layers, outNode._listDataLayer );

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
                if ( outNode._sceneInstance.empty() == false && ( outNode._prefab.empty() == false || outNode._embeddedXML.empty() ) )
                {
                    SW_LOG_ERROR_ID( "SceneDocument.InstanceAnchorShape", "Scene instance '%#' in %# must hold a <%#> with its transform and no prefab - the scene is not loaded", outNode._name, absPath, kGameObject );
                    return false;
                }
                // 상태가 실린 엔티티의 덮어쓰기는 씬 상속의 자식이 적은 차이다 — 인스턴스 멤버와 같이 상태 위에 얹는다.
                const bool bStateDelta = outNode._sceneInstance.empty() && outNode._prefab.empty() && outNode._embeddedXML.empty() == false && outNode._prefabOverrideXML.empty() == false;
                if ( bStateDelta )
                {
                    outNode._listInstanceOverrideXML.push_back( std::move( outNode._prefabOverrideXML ) );
                    outNode._prefabOverrideXML.clear();
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( Scene, SceneDocument::kAssetType );
    // 판 3: 형제 순서 칸 `_siblingIndex` → `_siblingOrder`(분수 색인 키, 설계 E-6)
    SW_ASSET_FORMAT_UPGRADE( sceneSiblingOrder, "Scene", 2, 3, &GameObjectFormatUpgrade::renameSiblingIndex );
    SW_DATA_INHERITANCE_SCHEMA( Scene, &SceneDocumentInternal::makeInheritanceSchema );

    bool SceneDocument::loadXML( const AssetID& assetID )
    {
        vector<string> listChain;
        return loadXMLChained( assetID, listChain );
    }

    bool SceneDocument::loadXMLChained( const AssetID& assetID, vector<string>& inoutListChain )
    {
        *this       = {};
        _sourcePath = assetID.str();
        AssetLoadScope loadScope( "Scene", assetID.view() );
        XMLDocument    doc;
        string         absPath;
        string         baseID; // 펼친 글에는 `_base` 가 없다 - 저장이 부모와의 차이로 쓰려고 원문에서 받는다
        const bool     bRead = doc.loadResourceAs( assetID, ResourceContext::forSchema( &SceneDocumentInternal::makeInheritanceSchema ), &absPath, &baseID );
        return readXMLSource( doc, bRead, absPath, baseID, loadScope, inoutListChain );
    }

    bool SceneDocument::loadXMLFromDiskFile( string_view diskPath )
    {
        // 디스크 파일은 부르는 쪽이 이름(`loadXMLFromDiskFile`)으로 고른 것이다 — 리소스 id 읽기가 실패한 뒤 넘어오는 길이 아니다.
        *this       = {};
        _sourcePath = diskPath;
        AssetLoadScope loadScope( "Scene", diskPath );
        XMLDocument    doc;
        const bool     bRead = doc.loadFromFile( diskPath );
        vector<string> listChain;
        return readXMLSource( doc, bRead, string( diskPath ), string{}, loadScope, listChain );
    }

    bool SceneDocument::readXMLSource( XMLDocument& doc, const bool bRead, const string& absPath, const string& flattenedBaseID, AssetLoadScope& loadScope, vector<string>& inoutListChain )
    {
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
            SW_LOG_ERROR_ID( "SceneDocument.MissingRootScene", "Missing root <Scene>: %#", absPath );
            return false;
        }

        // 씬 상속: 부모 위에 차이를 얹어 한 벌로 펼친 뒤 읽는다(Godot 씬 상속 · 유니티 Prefab Variant 와 같은 모델). 부모는 `AssetIO` 로 읽으므로 쿠킹 입력이 된다.
        // 리소스 id 읽기는 규칙을 골라 이미 펼쳤다(@p flattenedBaseID 가 부모다). 디스크 파일은 `parseData` 가 카탈로그 표에 없는 이 칸을 그대로 둔다("자기 입구가 펼친다").
        _baseScenePath = flattenedBaseID;
        const string basePath{ root.getAttributeText( DataInheritance::kBaseAttribute ) };
        if ( basePath.empty() == false )
        {
            string flatText;
            bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
            if ( ResourceComposer::compose( root.toString(), absPath, ResourceContext::forSchema( &SceneDocumentInternal::makeInheritanceSchema ), flatText, bComposed ) == false || doc.parse( flatText ) == false )
            {
                SW_LOG_ERROR_ID( "SceneDocument.BaseSceneNotFlattened", "Scene '%#' could not be merged onto its base '%#' - the scene is not loaded", absPath, basePath );
                *this = {};
                return false;
            }
            root           = doc.getRoot( SceneDocumentInternal::kRoot );
            _baseScenePath = basePath;
        }

        if ( AssetXMLFormat::upgradeToCurrentXMLVersion( kAssetType, root ) == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.FormatVersionMismatch", "formatVersion mismatch: %#", absPath );
            return false;
        }

        // 씬 · 엔티티의 값은 속성에만 있다(`saveToFile` 이 쓰는 모양).
        SceneRootParams rootParams;
        if ( XMLFields::readProperties( root, rootParams, absPath, { "formatVersion" } ) == false )
        {
            *this = {};
            return false;
        }
        _name = rootParams._name.empty() ? FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) ) : rootParams._name;

        if ( SceneDocumentInternal::readSceneSettings( root, _name, absPath, _sceneSettings ) == false )
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

        if ( expandSceneInstances( absPath, inoutListChain ) == false || SceneDocumentInternal::validateDataLayersDeclared( *this, absPath ) == false )
        {
            *this = {};
            return false;
        }

        _bValid = true;
        loadScope.setSucceeded();
        SW_LOG_INFO_ID( "SceneDocument.LoadedEntities", "Loaded '%#' (%# entities) from %#",
                        _name, static_cast<uint32>( _listSceneObjectNode.size() ), absPath );
        return true;
    }

    uint64 SceneDocument::computeInstanceIDScope( const uint64 anchorFileID )
    {
        // 값을 고루 퍼뜨리는 고정 섞기(splitmix64) — 작은 파일 id 와 겹치지 않게 넓은 값을 만든다. 저장본의 id 가 이 값에 달려 있으므로 바꾸면 안 된다.
        uint64 mixed = anchorFileID + 0x9E3779B97F4A7C15ull;
        mixed        = ( mixed ^ ( mixed >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
        mixed        = ( mixed ^ ( mixed >> 27 ) ) * 0x94D049BB133111EBull;
        mixed        = mixed ^ ( mixed >> 31 );
        return mixed != 0 ? mixed : 1;
    }

    bool SceneDocument::splitInstancePartPath( string_view partPath, uint64& outAnchorFileID, string_view& outRelativePath )
    {
        const size_t separator = partPath.find( '/' );
        if ( separator == string_view::npos )
            return false;
        outAnchorFileID = 0;
        for ( const char digit : partPath.substr( 0, separator ) )
        {
            if ( digit < '0' || digit > '9' )
                return false;
            outAnchorFileID = outAnchorFileID * 10u + static_cast<uint64>( digit - '0' );
        }
        outRelativePath = partPath.substr( separator + 1 );
        return outAnchorFileID != 0 && outRelativePath.empty() == false;
    }

    bool SceneDocument::expandSceneInstances( string_view absPath, vector<string>& inoutListChain )
    {
        bool bHasAnchor = false;
        for ( const SceneObjectNode& node : _listSceneObjectNode )
        {
            bHasAnchor = bHasAnchor || node._sceneInstance.empty() == false;
        }
        if ( bHasAnchor == false )
            return true;

        // 자기 포함은 사슬로 잡는다. 디스크 입구로 연 맨 위 씬은 경로 모양이 달라 한 겹 더 돈 뒤에 잡힌다.
        inoutListChain.push_back( FileUtil::normalizePath( _sourcePath ).str() );
        // 깊이 상한은 엔진의 사본이다(씬 로드 워커가 `EngineConfig` 를 직접 읽으면 설정 제자리 덮기와 겹친다). 없으면 대신 읽지 않고 진다.
        const optional<uint32> maxDepth = engine::getSubsystem<ResourceComposition>().findInheritanceMaxDepth();
        if ( maxDepth.has_value() == false )
        {
            DataInheritance::reportNoDepthLimit( absPath );
            return false;
        }
        if ( inoutListChain.size() > *maxDepth )
        {
            SW_LOG_ERROR_ID( "SceneDocument.InstanceTooDeep", "Scene instances in %# nest deeper than %# levels - the scene is not loaded", absPath, *maxDepth );
            return false;
        }

        vector<SceneObjectNode> listExpanded;
        listExpanded.reserve( _listSceneObjectNode.size() );
        vector<PrefabPartOverride> listPart;
        for ( const SceneObjectNode& anchor : _listSceneObjectNode )
        {
            listExpanded.push_back( anchor );
            if ( anchor._sceneInstance.empty() )
                continue;
            SceneObjectNode& expandedAnchor = listExpanded.back();

            const string instancePath = FileUtil::normalizePath( anchor._sceneInstance ).str();
            for ( const string& chainPath : inoutListChain )
            {
                if ( chainPath != instancePath )
                    continue;
                SW_LOG_ERROR_ID( "SceneDocument.InstanceCycle", "Scene instance '%#' in %# includes '%#', which already includes this scene - the scene is not loaded", anchor._name, absPath, anchor._sceneInstance );
                return false;
            }
            SceneDocument inner{};
            if ( inner.loadXMLChained( AssetID{ anchor._sceneInstance }, inoutListChain ) == false )
            {
                SW_LOG_ERROR_ID( "SceneDocument.InstanceNotLoaded", "Scene instance '%#' in %# could not load '%#' - the scene is not loaded", anchor._name, absPath, anchor._sceneInstance );
                return false;
            }
            if ( PrefabOverrides::collectPartOverrides( anchor._prefabOverrideXML, listPart ) == false )
                return false;
            expandedAnchor._instancePacking = inner._sceneSettings.usesStaticMeshPacking() ? SceneInstancePacking::StaticMeshes : SceneInstancePacking::Separate;

            // 레이어 이름은 전역 집합이다. 같은 이름을 바깥이 선언했으면 바깥의 처음 상태가 이긴다.
            for ( const SceneDataLayerDef& layer : inner._sceneSettings._listDataLayer )
            {
                if ( _sceneSettings.findDataLayer( layer._name ) == nullptr )
                    _sceneSettings._listDataLayer.push_back( layer );
            }

            const uint64  scope = computeInstanceIDScope( anchor._fileID );
            vector<uint8> listPartUsed( listPart.size(), SW_FALSE );
            for ( SceneObjectNode& member : inner._listSceneObjectNode )
            {
                // 내부 문서의 평평한 id · 범위에 이 앵커의 범위를 XOR 한다 — 글 상태의 내부 id 가 그대로 이 문서의 평평한 id 로 옮겨 간다.
                const string partPath        = member._instancePartPath.empty() ? sw::to_string( member._fileID ) : member._instancePartPath;
                member._instanceAnchorFileID = member._instanceAnchorFileID == 0 ? anchor._fileID : ( member._instanceAnchorFileID ^ scope );
                member._fileID ^= scope;
                member._idScope ^= scope;
                StringBuilder<constant::buffer::kMax256> memberName;
                memberName.appendFormat( "%#%#%#", anchor._name, SceneDocumentInternal::kInstanceNameSeparator, member._name );
                member._name             = string( memberName.view() );
                member._instancePartPath = PrefabPartOverride::joinPath( sw::to_string( anchor._fileID ), partPath );
                member._instanceTopOverrideXML.clear();
                for ( size_t partIndex = 0; partIndex < listPart.size(); ++partIndex )
                {
                    if ( listPart[partIndex]._partPath != partPath )
                        continue;
                    listPartUsed[partIndex]        = SW_TRUE;
                    member._instanceTopOverrideXML = listPart[partIndex]._overrideXML;
                    member._listInstanceOverrideXML.push_back( listPart[partIndex]._overrideXML );
                }
                member._listDataLayer.insert( member._listDataLayer.end(), anchor._listDataLayer.begin(), anchor._listDataLayer.end() );
                VectorUtil::sortUnique( member._listDataLayer );
                listExpanded.push_back( std::move( member ) );
            }
            // 프리팹에서 사라진 대상의 덮어쓴 값과 같다 — 내부 씬을 고쳤다고 바깥 씬이 읽히지 않으면 안 된다. 경고하고 다음 저장에서 빠진다.
            for ( size_t partIndex = 0; partIndex < listPart.size(); ++partIndex )
            {
                if ( listPartUsed[partIndex] == SW_FALSE )
                    SW_LOG_WARNING_ID( "SceneDocument.InstancePartGone", "Scene instance '%#' overrides entity '%#' that '%#' no longer has - the override is dropped", anchor._name, listPart[partIndex]._partPath, anchor._sceneInstance );
            }
        }
        inoutListChain.pop_back();

        unordered_set<uint64> uniqueFileID;
        uniqueFileID.reserve( listExpanded.size() );
        for ( const SceneObjectNode& node : listExpanded )
        {
            if ( node._fileID == 0 || uniqueFileID.insert( node._fileID ).second == false )
            {
                SW_LOG_ERROR_ID( "SceneDocument.InstanceIDCollision", "Entity '%#' in %# has the file id %#, which is zero or already taken - the scene is not loaded", node._name, absPath, node._fileID );
                return false;
            }
        }
        _listSceneObjectNode = std::move( listExpanded );
        return true;
    }

    void SceneDocument::stripAuthoringInstanceData()
    {
        const auto stripNodes = []( vector<SceneObjectNode>& listNode )
        {
            for ( SceneObjectNode& node : listNode )
            {
                if ( node._sceneInstance.empty() == false )
                    node._prefabOverrideXML.clear();
                node._sceneInstance.clear();
                node._instancePartPath.clear();
                node._instanceTopOverrideXML.clear();
            }
        };
        stripNodes( _listSceneObjectNode );
        for ( StreamingCell& cell : _listStreamingCell )
        {
            stripNodes( cell._listSceneObjectNode );
        }
        _baseScenePath.clear();
    }

    bool SceneDocument::saveToFile( const AbsolutePath& absPath ) const
    {
        XMLDocument xmlDoc;
        if ( buildXML( xmlDoc ) == false )
            return false;
        if ( FileUtil::ensureParentDirectoryExists( absPath ) == false || xmlDoc.saveCompleteToFile( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.FailedWrite", "Failed to write: %#", absPath.str() );
            return false;
        }
        SW_LOG_INFO_ID( "SceneDocument.SavedEntities", "Saved '%#' (%# entities) -> %#",
                        _name, static_cast<uint32>( _listSceneObjectNode.size() ), absPath.str() );
        return true;
    }

    bool SceneDocument::saveXMLDelta( const AbsolutePath& absPath, const SceneDocument& parent ) const
    {
        XMLDocument fullDoc;
        XMLDocument parentDoc;
        if ( _baseScenePath.empty() || buildXML( fullDoc ) == false || parent.buildXML( parentDoc ) == false )
            return false;
        if ( ResourceComposer::saveDelta( absPath.str(), fullDoc.getRoot().toString(), parentDoc.getRoot().toString(), SceneDocumentInternal::makeInheritanceSchema(), _baseScenePath ) == false )
            return false;
        SW_LOG_INFO_ID( "SceneDocument.SavedDelta", "Saved '%#' as a difference from '%#' -> %#", _name, _baseScenePath, absPath.str() );
        return true;
    }

    bool SceneDocument::buildXML( XMLDocument& xmlDoc ) const
    {
        // 셀로 나눈 문서는 쿠킹본이다. XML 에는 셀이 없으므로 쓰면 셀의 엔티티가 사라진다.
        if ( _listStreamingCell.empty() == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.SceneSplitStreamingCells", "Scene '%#' is split into %# streaming cells (a cooked scene) - it is not saved as XML", _name,
                             static_cast<uint32>( _listStreamingCell.size() ) );
            return false;
        }
        // 읽는 쪽이 받지 않는 월드 설정은 쓰지 않는다.
        if ( _sceneSettings.validate( _name ) == false )
            return false;

        XMLNode root = xmlDoc.appendRoot( SceneDocumentInternal::kRoot );
        root.appendAttribute( "formatVersion", static_cast<uint32>( kAssetType._formatVersion ) );
        XMLFields::writeProperties( root, SceneRootParams{ _name } );
        {
            XMLDocument sceneSettingsDoc;
            if ( sceneSettingsDoc.parse( XMLSerializer::serialize( &_sceneSettings, *SceneSettings::StaticType() ) ) == false ||
                 sceneSettingsDoc.getRoot().isValid() == false )
            {
                SW_LOG_ERROR_ID( "SceneDocument.SceneWorldSettingsCould", "Scene '%#' world settings could not be written - the scene is not saved", _name );
                return false;
            }
            root.appendClone( sceneSettingsDoc.getRoot() );
        }
        XMLNode entities = root.appendChild( SceneDocumentInternal::kEntities );

        for ( const SceneObjectNode& entity : _listSceneObjectNode )
        {
            // 읽는 쪽(`loadXML`)이 받지 않는 모양은 쓰지 않는다.
            if ( entity._fileID == 0 )
            {
                SW_LOG_ERROR_ID( "SceneDocument.EntityNoIdScene", "Entity '%#' has no id - scene '%#' is not saved", entity._name, _name );
                return false;
            }
            // 펼친 멤버는 파일의 엔티티가 아니다 — 앵커가 다시 펼친다. 펼친 문서를 그대로 쓰면 평평한 id 의 사본이 파일에 굳는다(`Scene::serializeToDocument` 는 앵커로 되돌려 쓴다).
            if ( entity._instanceAnchorFileID != 0 )
            {
                SW_LOG_ERROR_ID( "SceneDocument.ExpandedMemberNotSaved", "Entity '%#' is an expanded scene instance member - scene '%#' is not saved", entity._name, _name );
                return false;
            }
            XMLNode sceneObjectNode = entities.appendChild( SceneDocumentInternal::kEntity );
            XMLFields::writeProperties( sceneObjectNode, SceneEntityParams{ entity._fileID, entity._name, AssetRef<PrefabAsset>{ entity._prefab }, entity._sceneInstance, SceneDocumentInternal::joinDataLayers( entity._listDataLayer ) } );
            if ( entity._prefabOverrideXML.empty() == false )
            {
                XMLDocument overrideDoc;
                if ( overrideDoc.parse( entity._prefabOverrideXML ) && overrideDoc.getRoot().isValid() )
                    sceneObjectNode.appendClone( overrideDoc.getRoot() );
                else
                    SW_LOG_ERROR_ID( "SceneDocument.EntityPrefabOverridesNot", "Entity '%#' has prefab overrides that are not XML - they are not written", entity._name );
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

        return true;
    }

    bool SceneDocument::loadBinary( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        *this                  = {};
        _sourcePath            = path;

        // 낱개 파일 허용 정책(OS 절대 경로 · 작업 경로 포함)은 `AssetIO` 한 곳이 본다 — 실패 뒤 디스크를 다시 열지 않는다.
        // 고정 머리와 머리(이름 · 월드 설정 · 늘 올라 있는 엔티티 · 셀 색인)만 구간으로 읽는다 — 셀 내용은 스트리밍이 셀을 올릴 때 읽는다.
        const string  absPath{ path };
        vector<uint8> fixedHead;
        if ( AssetIO::readBinaryAssetRange( assetID, 0, SceneDocumentInternal::kFixedHeadBytes, fixedHead ) == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.BinaryReadFailedToo", "Binary read failed or too small: %#", absPath );
            return false;
        }
        Archive fixedArch( fixedHead.data(), fixedHead.size() );
        uint32  magic{ 0 };
        uint32  version{ 0 };
        uint64  headSize{ 0 };
        uint64  contentSize{ 0 };
        fixedArch >> magic >> version >> headSize >> contentSize;
        if ( magic != SceneDocumentInternal::kBinMagic )
        {
            SW_LOG_ERROR_ID( "SceneDocument.BadBinaryMagic", "Bad binary magic: %#", absPath );
            return false;
        }
        if ( version != SceneDocumentInternal::kBinVersion )
        {
            SW_LOG_ERROR_ID( "Scene.UnsupportedBinaryVersion", "Unsupported binary version %# in %# (this build reads %#) - cook the scene again", version, absPath,
                             SceneDocumentInternal::kBinVersion );
            return false;
        }
        vector<uint8> head;
        const bool    bHeadRead = headSize > SceneDocumentInternal::kFixedHeadBytes &&
                               AssetIO::readBinaryAssetRange( assetID, SceneDocumentInternal::kFixedHeadBytes, headSize - SceneDocumentInternal::kFixedHeadBytes, head );
        if ( fixedArch.isError() || bHeadRead == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.BinarySceneHeadUnreadable", "Binary scene head (%# bytes) could not be read: %#", headSize, absPath );
            return false;
        }
        Archive arch( head.data(), head.size() );

        arch >> _name;
        if ( BinarySerializer::deserialize( &_sceneSettings, *SceneSettings::StaticType(), arch ) == false || _sceneSettings.validate( _name ) == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.BinarySceneWorldSettings", "Binary scene world settings could not be read or are not usable: %#", absPath );
            *this = {};
            return false;
        }

        if ( SceneDocumentInternal::readEntities( arch, _listSceneObjectNode, absPath ) == false )
        {
            *this = {};
            return false;
        }

        uint32 cellCount{ 0 };
        cellCount = arch.readCount( SceneDocumentInternal::kMinBytesPerCell );
        if ( arch.isError() )
        {
            SW_LOG_ERROR_ID( "SceneDocument.BinarySceneClaimsStreaming", "Binary scene claims more streaming cells than can fit in its %# remaining bytes: %#", arch.getRemainingBytes(), absPath );
            *this = {};
            return false;
        }
        // 셀은 스트리밍하는 씬에만 있다 — 스트리밍하지 않는 씬에 셀이 있으면 그 엔티티는 아무도 올리지 않는다.
        if ( cellCount > 0 && _sceneSettings._bStreaming == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.BinarySceneStreamingCells", "Binary scene has %# streaming cells but does not stream: %#", cellCount, absPath );
            *this = {};
            return false;
        }
        _listStreamingCell.resize( cellCount );
        for ( StreamingCell& cell : _listStreamingCell )
        {
            arch >> cell._cell._x >> cell._cell._z;
            const bool bLayersRead = SceneDocumentInternal::readTextList( arch, cell._listDataLayer );
            arch >> cell._contentOffset >> cell._contentSize >> cell._hlodFileID;
            if ( bLayersRead == false || arch.isError() )
            {
                SW_LOG_ERROR_ID( "SceneDocument.BinarySceneStreamCorrupted", "Binary scene stream corrupted in %#", absPath );
                *this = {};
                return false;
            }
        }
        arch >> _maxCellFileID;
        // 셀 내용은 머리 바로 뒤부터다 — 구간을 파일 안 절대 위치로 바꾸고 내용 영역을 넘는 구간은 받지 않는다(런타임 읽기가 `OutOfRange` 로 지기 전에 여기서 멈춘다).
        const uint64 contentBase = headSize;
        for ( StreamingCell& cell : _listStreamingCell )
        {
            const bool bRangeOutside = cell._contentSize == 0 || cell._contentOffset > contentSize || cell._contentSize > contentSize - cell._contentOffset;
            if ( bRangeOutside )
            {
                SW_LOG_ERROR_ID( "SceneDocument.BinarySceneCellOutside", "Binary scene cell (%#, %#) content lies outside the file in %#", cell._cell._x, cell._cell._z, absPath );
                *this = {};
                return false;
            }
            cell._contentOffset += contentBase;
        }

        if ( arch.isError() )
        {
            SW_LOG_ERROR_ID( "SceneDocument.BinarySceneStreamCorrupted", "Binary scene stream corrupted in %#", absPath );
            *this = {};
            return false;
        }

        _bValid = true;
        SW_LOG_INFO_ID( "SceneDocument.LoadedEntitiesBinary", "Loaded '%#' (%# entities) from binary %#", _name, static_cast<uint32>( computeTotalEntityCount() ), absPath );
        if ( cellCount > 0 )
            SW_LOG_INFO_ID( "SceneDocument.SceneStreamingCells", "Scene '%#' has %# streaming cells", _name, cellCount );
        return true;
    }

    bool SceneDocument::saveBinary( const AbsolutePath& absPath ) const
    {
        // 읽는 쪽이 받지 않는 월드 설정 · 셀은 쓰지 않는다.
        if ( _sceneSettings.validate( _name ) == false )
            return false;
        if ( _listStreamingCell.empty() == false && _sceneSettings._bStreaming == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.SceneStreamingCellsBut", "Scene '%#' has streaming cells but does not stream - not saved", _name );
            return false;
        }

        // 머리(이름 · 월드 설정 · 늘 올라 있는 엔티티 · 셀 색인)와 셀 내용을 따로 짓고, 고정 머리에 둘의 길이를 적는다(판 9).
        Archive arch;
        arch << _name;
        if ( BinarySerializer::serialize( &_sceneSettings, *SceneSettings::StaticType(), arch ) == false )
        {
            SW_LOG_ERROR_ID( "SceneDocument.SceneWorldSettingsCould2", "Scene '%#' world settings could not be written - not saved", _name );
            return false;
        }
        SceneDocumentInternal::writeEntities( arch, _listSceneObjectNode );
        // 색인 뒤에 셀 내용을 셀마다 잇는다(판 8). 색인만 읽은 문서(내용이 파일에 남은 셀)는 다시 쓸 내용이 없다.
        Archive content;
        arch << static_cast<uint32>( _listStreamingCell.size() );
        for ( const StreamingCell& cell : _listStreamingCell )
        {
            if ( cell.hasFileContent() )
            {
                SW_LOG_ERROR_ID( "SceneDocument.SceneCellContentNotRead", "Scene '%#' cell (%#, %#) content was not read into memory - not saved", _name, cell._cell._x, cell._cell._z );
                return false;
            }
            const uint64 contentOffset = content.getSize();
            SceneDocumentInternal::writeEntities( content, cell._listSceneObjectNode );
            arch << cell._cell._x << cell._cell._z;
            SceneDocumentInternal::writeTextList( arch, cell._listDataLayer );
            arch << contentOffset << static_cast<uint64>( content.getSize() - contentOffset ) << cell._hlodFileID;
        }
        // 셀 엔티티의 가장 큰 파일 id(판 10) — 런타임은 셀 내용을 읽지 않고도 새 파일 id 를 이 위에서 고른다.
        uint64 maxCellFileID = 0;
        for ( const StreamingCell& cell : _listStreamingCell )
        {
            maxCellFileID = MathUtil::max( maxCellFileID, computeMaxFileID( cell._listSceneObjectNode ) );
        }
        arch << maxCellFileID;
        Archive file;
        file << SceneDocumentInternal::kBinMagic << SceneDocumentInternal::kBinVersion << static_cast<uint64>( SceneDocumentInternal::kFixedHeadBytes + arch.getSize() )
             << static_cast<uint64>( content.getSize() );
        file.writeBytes( arch.getData(), arch.getSize() );
        file.writeBytes( content.getData(), content.getSize() );

        const bool bOk = FileUtil::ensureParentDirectoryExists( absPath ) && file.saveToFile( absPath.str() );
        if ( bOk )
            SW_LOG_INFO_ID( "SceneDocument.SavedBinaryEntitiesStreaming", "Saved binary '%#' (%# entities, %# streaming cells) -> %#", _name, static_cast<uint32>( computeTotalEntityCount() ),
                            static_cast<uint32>( _listStreamingCell.size() ), absPath.str() );
        return bOk;
    }

    bool SceneDocument::readStreamingCellContent( const uint8* pData, uint64 size, vector<SceneObjectNode>& outListNode, string_view sourcePath )
    {
        outListNode.clear();
        Archive arch( pData, size );
        if ( pData == nullptr || SceneDocumentInternal::readEntities( arch, outListNode, sourcePath ) == false || arch.isError() )
        {
            SW_LOG_ERROR_ID( "SceneDocument.CellContentCorrupted", "Streaming cell content (%# bytes) could not be read from %#", size, sourcePath );
            outListNode.clear();
            return false;
        }
        return true;
    }

    void SceneDocument::writeStreamingCellContent( const vector<SceneObjectNode>& listNode, vector<uint8>& outBytes )
    {
        Archive arch;
        SceneDocumentInternal::writeEntities( arch, listNode );
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
    }

    void SceneDocument::writeSavedNodes( const vector<SceneObjectNode>& listNode, vector<uint8>& outBytes )
    {
        Archive arch;
        SceneDocumentInternal::writeEntities( arch, listNode );
        for ( const SceneObjectNode& node : listNode )
        {
            // 런타임 칸도 함께 싣는다 — 하위 레벨의 쿠킹 상태 범위, 상태가 적힌 월드 원점(`_worldOrigin`).
            arch << static_cast<uint8>( node._statePurpose ) << node._cookedStateIDScope << node._worldOrigin._x << node._worldOrigin._y << node._worldOrigin._z;
        }
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
    }

    bool SceneDocument::readSavedNodes( const uint8* pData, uint64 size, vector<SceneObjectNode>& outListNode, string_view sourcePath )
    {
        outListNode.clear();
        Archive arch( pData, size );
        if ( pData == nullptr || SceneDocumentInternal::readEntities( arch, outListNode, sourcePath ) == false )
        {
            outListNode.clear();
            return false;
        }
        for ( SceneObjectNode& node : outListNode )
        {
            uint8 purpose{ 0 };
            arch >> purpose >> node._cookedStateIDScope >> node._worldOrigin._x >> node._worldOrigin._y >> node._worldOrigin._z;
            if ( arch.isError() || purpose > static_cast<uint8>( SerializePurpose::SaveGame ) )
            {
                SW_LOG_ERROR_ID( "SceneDocument.SavedNodePurposeInvalid", "Saved entity '%#' has no readable state purpose (%#) in %# - it is not used", node._name, static_cast<uint32>( purpose ),
                                 sourcePath );
                outListNode.clear();
                return false;
            }
            node._statePurpose = static_cast<SerializePurpose>( purpose );
        }
        return true;
    }

    uint64 SceneDocument::computeMaxFileID( const vector<SceneObjectNode>& listNode )
    {
        uint64 maxFileID = 0;
        for ( const SceneObjectNode& node : listNode )
        {
            if ( node._instanceAnchorFileID == 0 )
                maxFileID = MathUtil::max( maxFileID, node._fileID );
        }
        return maxFileID;
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

    bool SceneDocument::load( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        // 쿠킹본 이름은 쿠커와 같은 규칙 하나다(`AssetCookPath`). 씬 이름(`.scene.xml`)이 아니면 쿠킹본이 없다 — 쿠커가 쿠킹하지 않는다.
        const string binPath = AssetCookPath::toCookedPath( path );

        // 쿠킹본 경로를 직접 받으면 그것을 읽는다. 그 밖은 정책 하나가 고르고, 고른 쪽이 없으면 다른 쪽을 대신 읽지 않는다 — 소스 실행이 옆에 남은
        // 쿠킹본을 읽으면 소스와 어긋난 씬을 모르고 쓴다(`ContentSource`).
        if ( FileUtil::hasExtension( path, ".bin" ) )
            return loadBinary( assetID );
        if ( AssetIO::usesSource() )
            return loadXML( AssetID{ path } );
        if ( binPath.empty() == false && loadBinary( AssetID{ binPath } ) )
            return true;
        SW_LOG_ERROR_ID( "SceneDocument.CookedContentRequiresCooked", "Cooked content requires the cooked scene: %# (cooked name '%#')", path, binPath );
        return false;
    }

} // namespace sw
