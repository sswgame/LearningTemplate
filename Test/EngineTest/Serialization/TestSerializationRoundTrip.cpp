/**
 * @file TestSerializationRoundTrip.cpp
 * @brief Resource/ 의 씬 · 프리팹을 실제로 읽어 세 형식(XML · JSON · 바이너리)으로 다시 쓴다.
 * @details 두 가지를 본다. (1) 고정점 — 쓴 것을 같은 컴포넌트에 다시 읽어 다시 쓰면 바이트가 같다. (2) 덤프 — `SW_SERIALIZATION_DUMP_DIR` 이 있으면
 *          오브젝트 상태를 세 형식으로 그 폴더에 쓴다. 직렬화기를 고치기 전과 후에 덤프해 `diff -r` 로 출력 바이트가 그대로인지 본다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

#include "TestFramework/TestFramework.h"

#include <cstdlib>

namespace
{
    struct SerializationRoundTripInternal
    {
        /** @brief 씬 · 프리팹 하나를 읽어 만든 오브젝트입니다. 씬은 `_scene` 이, 프리팹은 `_manager` 가 소유합니다. */
        struct LoadedObjects
        {
            sw::unique_ptr<sw::Scene>             _scene;
            sw::unique_ptr<sw::GameObjectManager> _manager;
            sw::vector<sw::GameObject*>           _listObject;
        };

        static bool isScene( sw::string_view resourceId ) { return sw::StringUtil::endsWith( resourceId, ".scene.xml", true ); }
        static bool isPrefab( sw::string_view resourceId )
        {
            return sw::StringUtil::endsWith( resourceId, ".prefab.xml", true ) || sw::StringUtil::endsWith( resourceId, ".prefab.json", true );
        }

        /** @brief Resource/ 의 씬 · 프리팹 리소스 id 입니다(같은 트리면 실행마다 같은 순서). */
        static sw::vector<sw::string> collectSceneAndPrefabIds()
        {
            sw::vector<sw::string> listFilePath;
            (void)sw::FileUtil::collectFiles( sw::ResourceUtil::getRootFolderPath(), "", listFilePath, true ); // 실패하면 빈 목록 — 부르는 쪽이 개수로 잡는다
            sw::vector<sw::string> listResourceId;
            for ( const sw::string& filePath : listFilePath )
            {
                const sw::string resourceId = sw::ResourceUtil::toResourceId( filePath );
                if ( isScene( resourceId ) || isPrefab( resourceId ) )
                    listResourceId.push_back( resourceId );
            }
            return listResourceId;
        }

        /** @brief 씬은 문서를 읽어 만들고, 프리팹은 오브젝트 하나에 적용합니다(ResourceDataSchemaTest 와 같은 로더). */
        [[nodiscard]] static bool loadObjects( const sw::string& resourceId, LoadedObjects& outLoaded )
        {
            if ( isScene( resourceId ) )
            {
                sw::SceneDocument doc;
                if ( doc.loadXml( resourceId ) == false )
                    return false;
                outLoaded._scene = sw::make_unique<sw::Scene>( "SerializationRoundTripScene" );
                if ( outLoaded._scene->instantiate( doc ) == false )
                    return false;
                outLoaded._scene->getObjectManager()->getAllGameObjects( outLoaded._listObject );
                return true;
            }
            sw::PrefabAsset prefab;
            const bool      bLoaded = sw::StringUtil::endsWith( resourceId, ".json", true ) ? prefab.loadFromJsonFile( resourceId ) : prefab.loadFromXmlFile( resourceId );
            if ( bLoaded == false )
                return false;
            outLoaded._manager      = sw::make_unique<sw::GameObjectManager>();
            sw::GameObject* pObject = outLoaded._manager->createGameObject( sw::hashed_string( "SerializationRoundTripPrefab" ) );
            if ( pObject == nullptr || prefab.applyStateTo( pObject ) == false )
                return false;
            outLoaded._manager->getAllGameObjects( outLoaded._listObject );
            return true;
        }

        /** @brief 소유 포인터 원소의 컨테이너가 있는 타입입니다 — 원소를 만드는 팩토리가 소유자(오브젝트 상태 직렬화기)의 문맥에만 있어 고정점을 보지 않습니다. */
        static bool hasOwnedPointerContainer( const sw::TypeInfo& typeInfo )
        {
            for ( const sw::PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
            {
                const bool bContainer      = prop._bIsContainer && prop.hasContainerWrapper();
                const bool bOwnedContainer = bContainer && sw::SerializerUtil::isOwnedPointerElementType( prop.getContainerShape()._elementTypeName );
                if ( bOwnedContainer )
                    return true;
            }
            return false;
        }

        /**
         * @brief @p pComponent 를 세 형식으로 쓰고, 쓴 것을 같은 컴포넌트에 다시 읽어 다시 씁니다. 두 번째 쓰기가 다르면 그 형식 이름, 같으면 빈 글입니다.
         * @details 같은 객체에 되읽으므로 id · 부착 · 핸들이 그대로다 — 바이트가 다르면 그 형식의 쓰기 · 읽기가 값을 바꾼 것이다.
         */
        static sw::string findRewriteMismatch( sw::Component* pComponent, const sw::TypeInfo& typeInfo )
        {
            const sw::string xml = sw::XmlSerializer::serialize( pComponent, typeInfo );
            if ( sw::XmlSerializer::deserialize( pComponent, typeInfo, xml ) == false )
                return "xml read";
            if ( sw::XmlSerializer::serialize( pComponent, typeInfo ) != xml )
                return "xml";

            const sw::string json = sw::JsonSerializer::serialize( pComponent, typeInfo );
            if ( sw::JsonSerializer::deserialize( pComponent, typeInfo, json ) == false )
                return "json read";
            if ( sw::JsonSerializer::serialize( pComponent, typeInfo ) != json )
                return "json";

            sw::vector<uint8> firstBytes;
            sw::BinarySerializer::serialize( pComponent, typeInfo, firstBytes );
            if ( sw::BinarySerializer::deserialize( pComponent, typeInfo, firstBytes.data(), firstBytes.size() ) == false )
                return "binary read";
            sw::vector<uint8> secondBytes;
            sw::BinarySerializer::serialize( pComponent, typeInfo, secondBytes );
            if ( secondBytes != firstBytes )
                return "binary";
            return {};
        }

        /** @brief 덤프 파일 하나를 씁니다(폴더는 만든다). */
        [[nodiscard]] static bool writeDumpFile( const sw::string& path, const uint8* pData, size_t size )
        {
            return sw::FileUtil::ensureParentDirectoryExists( path ) && sw::FileUtil::writeFile( path, pData, size );
        }

        [[nodiscard]] static bool writeDumpText( const sw::string& path, const sw::string& text )
        {
            return writeDumpFile( path, reinterpret_cast<const uint8*>( text.data() ), text.size() );
        }
    };
} // namespace

/**
 * @brief [SerializationRoundTripTest] Resource/ 의 씬 · 프리팹에서 만든 컴포넌트는 세 형식 모두 쓴 것을 다시 읽어 다시 쓰면 같은 바이트다
 * @details 손으로 만든 시험 타입만 왕복하면 실제 데이터가 지나는 모양(중첩 컨테이너 · 맵 · 고정 배열 · enum · 값 구조체)을 놓친다.
 *          소유 포인터 컨테이너가 있는 타입은 뺀다(`_listComponent` 는 덤프 케이스가 오브젝트 상태로 본다).
 */
SW_TEST_CASE( SerializationRoundTripTest, EveryResourceComponentRewritesToTheSameBytes )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::vector<sw::string> listResourceId = SerializationRoundTripInternal::collectSceneAndPrefabIds();
    SW_ASSERT_TRUE( listResourceId.size() >= 5u );

    test::ScopedLogSuppressor suppressor; // 게임 모듈 타입의 "모르는 타입" 경고 — ResourceDataSchemaTest 가 따로 본다
    uint32                    componentCount{ 0 };
    sw::string                mismatches;
    for ( const sw::string& resourceId : listResourceId )
    {
        SerializationRoundTripInternal::LoadedObjects loaded;
        SW_EXPECT_TRUE_MSG( SerializationRoundTripInternal::loadObjects( resourceId, loaded ), resourceId.c_str() );
        for ( sw::GameObject* pObject : loaded._listObject )
        {
            for ( sw::Component* pComponent : pObject->getComponents() )
            {
                const sw::TypeInfo* pType = pComponent->getTypeInfo();
                if ( pType == nullptr || SerializationRoundTripInternal::hasOwnedPointerContainer( *pType ) )
                    continue;
                // 맡아 둔 원문(`_originalText`)이 줄바꿈으로 끝난다 — XML 문자열 속성은 읽을 때 끝 공백을 잘라 고정점이 아니다(백로그 1-1).
                // 이 컴포넌트는 직렬화기가 아니라 오브젝트 상태 직렬화기가 원문을 그대로 다시 쓴다.
                if ( pType == sw::MissingComponent::StaticType() )
                    continue;
                ++componentCount;
                const sw::string mismatch = SerializationRoundTripInternal::findRewriteMismatch( pComponent, *pType );
                if ( mismatch.empty() == false )
                    mismatches += resourceId + " " + pType->_name.c_str() + ": " + mismatch + "\n";
            }
        }
    }
    SW_EXPECT_TRUE( componentCount >= 50u );
    SW_EXPECT_TRUE_MSG( mismatches.empty(), mismatches.c_str() );
}

/**
 * @brief [SerializationRoundTripTest] `SW_SERIALIZATION_DUMP_DIR` 이 있으면 씬 · 프리팹의 오브젝트 상태를 세 형식으로 그 폴더에 쓴다(직렬화기 변경 전후 비교용)
 * @details 고치기 전 바이너리로 `before`, 고친 뒤 `after` 를 덤프하고 `diff -r before after` 가 비어야 한다. 이 케이스 하나만 돌려야 런타임 id 가 같다.
 */
SW_TEST_CASE( SerializationRoundTripTest, DumpEveryResourceObjectState )
{
    const utf8* pDumpDir = std::getenv( "SW_SERIALIZATION_DUMP_DIR" );
    if ( pDumpDir == nullptr || pDumpDir[0] == 0 )
        SW_TEST_SKIP( "SW_SERIALIZATION_DUMP_DIR is not set" );
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    test::ScopedLogSuppressor suppressor;
    uint32                    fileCount{ 0 };
    for ( const sw::string& resourceId : SerializationRoundTripInternal::collectSceneAndPrefabIds() )
    {
        SerializationRoundTripInternal::LoadedObjects loaded;
        if ( SerializationRoundTripInternal::loadObjects( resourceId, loaded ) == false )
            continue;
        for ( size_t objectIndex = 0; objectIndex < loaded._listObject.size(); ++objectIndex )
        {
            const sw::GameObject* pObject = loaded._listObject[objectIndex];
            const sw::string      stem    = sw::string( pDumpDir ) + "/" + resourceId + "/" + sw::to_string( static_cast<uint32>( objectIndex ) );
            sw::vector<uint8>     bytes;
            SW_EXPECT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pObject, bytes ) );
            SW_EXPECT_TRUE( SerializationRoundTripInternal::writeDumpText( stem + ".xml", sw::ObjectStateSerializer::saveToXmlString( pObject ) ) );
            SW_EXPECT_TRUE( SerializationRoundTripInternal::writeDumpText( stem + ".json", sw::ObjectStateSerializer::saveToJsonString( pObject ) ) );
            SW_EXPECT_TRUE( SerializationRoundTripInternal::writeDumpFile( stem + ".bin", bytes.data(), bytes.size() ) );
            fileCount += 3;
        }
    }
    SW_EXPECT_TRUE( fileCount > 0u );
}
