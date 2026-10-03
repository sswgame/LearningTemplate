#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Input/ActionMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/TestFramework.h"

// Resource/ 아래 데이터가 지금 코드의 이름만 쓰는지 — 모르는 키 · 타입 · 열거자가 하나도 없는지.

namespace
{
    struct ResourceDataSchemaInternal
    {
        /** @brief Resource/ 의 데이터 파일 한 종류와 그것을 읽는 실제 로더입니다. */
        struct DataKind
        {
            const utf8* _pLabel;
            bool ( *_pIsKind )( sw::string_view resourceId );
            bool ( *_pLoad )( const sw::string& resourceId );
        };

        static bool endsWith( sw::string_view resourceId, sw::string_view suffix ) { return sw::StringUtil::endsWith( resourceId, suffix, true ); }
        static bool startsWith( sw::string_view resourceId, sw::string_view prefix ) { return sw::StringUtil::startsWith( resourceId, prefix, true ); }

        static bool isScene( sw::string_view resourceId ) { return endsWith( resourceId, ".scene.xml" ); }
        static bool isPrefab( sw::string_view resourceId ) { return endsWith( resourceId, ".prefab.xml" ) || endsWith( resourceId, ".prefab.json" ); }
        static bool isPipeline( sw::string_view resourceId ) { return startsWith( resourceId, "engine/pipeline/" ) && endsWith( resourceId, ".xml" ); }
        static bool isRenderPass( sw::string_view resourceId ) { return startsWith( resourceId, "engine/renderpass/" ) && endsWith( resourceId, ".xml" ); }
        static bool isEngineData( sw::string_view resourceId ) { return endsWith( resourceId, "enginedata.xml" ); }
        static bool isInputMap( sw::string_view resourceId ) { return endsWith( resourceId, ".input.xml" ); }
        static bool isMaterial( sw::string_view resourceId ) { return endsWith( resourceId, ".material" ); }
        static bool isSpriteClip( sw::string_view resourceId ) { return endsWith( resourceId, ".sprite.json" ); }

        static bool loadScene( const sw::string& resourceId )
        {
            sw::SceneDocument doc;
            if ( doc.loadXml( resourceId ) == false )
                return false;
            sw::Scene scene{ "ResourceDataSchemaScene" };
            return scene.instantiate( doc );
        }

        static bool loadPrefab( const sw::string& resourceId )
        {
            sw::PrefabAsset prefab;
            const bool      bLoaded = endsWith( resourceId, ".json" ) ? prefab.loadFromJsonFile( resourceId ) : prefab.loadFromXmlFile( resourceId );
            if ( bLoaded == false )
                return false;
            sw::GameObjectManager manager;
            sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "ResourceDataSchemaPrefab" ) );
            return pObject != nullptr && prefab.applyStateTo( pObject );
        }

        static bool loadPipeline( const sw::string& resourceId )
        {
            sw::RenderPipelineResource pipeline;
            return pipeline.loadFromXmlFile( resourceId );
        }

        static bool loadRenderPass( const sw::string& resourceId )
        {
            sw::RenderPassResource pass;
            return pass.loadFromXmlFile( resourceId );
        }

        static bool loadEngineData( const sw::string& resourceId )
        {
            sw::EngineData data;
            return data.loadFromResource( resourceId );
        }

        static bool loadInputMap( const sw::string& resourceId )
        {
            sw::ActionMap map;
            return map.loadFromResource( resourceId );
        }

        static bool loadMaterial( const sw::string& resourceId )
        {
            const sw::shared_ptr<sw::Material> material = sw::Material::create();
            return material != nullptr && material->loadFromFile( resourceId );
        }

        static bool loadSpriteClip( const sw::string& resourceId )
        {
            sw::SpriteClipAsset clip;
            return clip.loadFromFile( resourceId );
        }

        /** @brief 데이터 종류 표입니다. 앞의 줄이 먼저 맞습니다. */
        static constexpr DataKind kArrDataKind[] = {
            {     "scene",      &isScene,      &loadScene},
            {    "prefab",     &isPrefab,     &loadPrefab},
            {  "pipeline",   &isPipeline,   &loadPipeline},
            {"renderpass", &isRenderPass, &loadRenderPass},
            {"enginedata", &isEngineData, &loadEngineData},
            {  "inputmap",   &isInputMap,   &loadInputMap},
            {  "material",   &isMaterial,   &loadMaterial},
            {"spriteclip", &isSpriteClip, &loadSpriteClip},
        };

        /** @brief 데이터로 보는 확장자입니다. 이 확장자인데 표의 어느 줄에도 맞지 않는 파일은 시험이 집니다(새 종류가 검사를 비켜 가지 않게). */
        static bool isDataFile( sw::string_view resourceId )
        {
            return endsWith( resourceId, ".xml" ) || endsWith( resourceId, ".json" ) || endsWith( resourceId, ".material" );
        }

        /** @brief @p resourceId 의 종류입니다. 표에 없으면 nullptr 입니다. */
        static const DataKind* findKind( sw::string_view resourceId )
        {
            for ( const DataKind& kind : kArrDataKind )
            {
                if ( kind._pIsKind( resourceId ) )
                    return &kind;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [ResourceDataSchemaTest] Resource/ 아래 데이터 파일은 모두 실제 로더로 읽히고, 모르는 키 · 타입 · 열거자 경고가 하나도 나지 않는다
 * @details 이름은 하나만 쓴다 — 코드의 이름을 바꾸면 데이터를 그 이름으로 다시 쓴다(별칭으로 옛 이름을 남기지 않는다). 텍스트 로더는 모르는 키를
 *          버리고 성공하므로(`SchemaOrphanPolicy::Ignore`) 데이터를 빠뜨려도 다른 시험은 초록이다. 이 시험은 그 버린 것을 경고로 잡는다.
 *          데이터 확장자(.xml · .json · .material)인데 종류 표에 없는 파일도 실패다.
 */
SW_TEST_CASE( ResourceDataSchemaTest, EveryResourceDataFileLoadsWithoutUnknownNames )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string& resourceRoot = sw::ResourceUtil::getRootFolderPath();
    SW_ASSERT_FALSE( resourceRoot.empty() );

    sw::vector<sw::string> listFilePath;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( resourceRoot, "", listFilePath, true ) );

    uint32 loadedCount{ 0 };
    for ( const sw::string& filePath : listFilePath )
    {
        const sw::string resourceId = sw::ResourceUtil::toResourceId( filePath );
        if ( ResourceDataSchemaInternal::isDataFile( resourceId ) == false )
            continue;

        const ResourceDataSchemaInternal::DataKind* pKind = ResourceDataSchemaInternal::findKind( resourceId );
        SW_EXPECT_TRUE_MSG( pKind != nullptr, ( "종류 표에 없는 데이터 파일입니다: " + resourceId ).c_str() );
        if ( pKind == nullptr )
            continue;

        test::ScopedLogCollector logs;
        const bool               bLoaded = pKind->_pLoad( resourceId );
        SW_EXPECT_TRUE_MSG( bLoaded, ( sw::string( pKind->_pLabel ) + " 를 읽지 못했습니다: " + resourceId + logs.joined() ).c_str() );
        SW_EXPECT_TRUE_MSG( logs.joined().empty(), ( resourceId + " 를 읽으며 경고가 났습니다:" + logs.joined() ).c_str() );
        ++loadedCount;
    }
    // 장면 · 프리팹 · 파이프라인 · 렌더 패스 · 엔진 데이터가 실제로 훑였는지 — 경로를 못 찾아 0 개면 이 시험은 아무것도 보지 않은 것이다.
    SW_EXPECT_TRUE_MSG( loadedCount >= 10u, ( "읽은 데이터 파일이 " + std::to_string( loadedCount ) + " 개뿐입니다" ).c_str() );
}

/**
 * @brief [ResourceDataSchemaTest] 컴포넌트 원소의 모르는 속성은 버려지고 이름으로 알린다 — 위 시험이 기대는 경고 경로
 * @details XML 은 루트 원소의 모르는 속성만 orphan 으로 올렸고, 안쪽 원소(컴포넌트 · 구조체 칸)의 모르는 속성은 말없이 버렸다. 그래서 이름을 바꾸고
 *          씬 데이터를 빠뜨려도 로드는 조용했다.
 */
SW_TEST_CASE( ResourceDataSchemaTest, UnknownComponentAttributeIsNamed )
{
    sw::GameObjectManager    manager;
    sw::GameObject*          pObject = manager.createGameObject( sw::hashed_string( "Probe" ) );
    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "a component attribute the type does not have" );
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString(
            pObject, "<GameObject _name=\"Probe\"><_listComponent><SceneComponent _localScale=\"2,2,2\" _noSuchField=\"1\" /></_listComponent></GameObject>" ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "SceneComponent._noSuchField" ) == 1, logs.joined().c_str() );
}
