#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Ability/AbilityCatalog.h"
#include "GameFramework/Camera/CameraPreset.h"
#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/Data/GameSettings.h"
#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Simulation/ThemePark/ParkLayout.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelBlock.h"
#include "GameFramework/Kits/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsCatalog.h"

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
        static bool isEngineDefaultAssets( sw::string_view resourceId ) { return endsWith( resourceId, "enginedefaultassets.xml" ); }
        static bool isInputMap( sw::string_view resourceId ) { return endsWith( resourceId, ".input.xml" ); }
        static bool isMaterial( sw::string_view resourceId ) { return endsWith( resourceId, ".material" ); }
        static bool isSpriteClip( sw::string_view resourceId ) { return endsWith( resourceId, ".sprite.json" ); }
        static bool isCameraPresets( sw::string_view resourceId ) { return endsWith( resourceId, ".cameras.xml" ); }
        static bool isSchedules( sw::string_view resourceId ) { return endsWith( resourceId, ".schedules.xml" ); }
        static bool isUserSettingsSchema( sw::string_view resourceId ) { return endsWith( resourceId, ".settings.xml" ); }

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
            sw::RenderPipelineAsset pipeline;
            return pipeline.loadFromXmlFile( resourceId );
        }

        static bool loadRenderPass( const sw::string& resourceId )
        {
            sw::RenderPassAsset pass;
            return pass.loadFromXmlFile( resourceId );
        }

        static bool loadEngineDefaultAssets( const sw::string& resourceId )
        {
            sw::EngineDefaultAssets data;
            return data.loadFromResource( resourceId );
        }

        static bool loadInputMap( const sw::string& resourceId )
        {
            sw::InputMap map;
            return map.loadFromResource( resourceId );
        }

        static bool loadMaterial( const sw::string& resourceId )
        {
            const sw::shared_ptr<sw::Material> material = sw::Material::create();
            return material != nullptr && material->loadFromFile( resourceId );
        }

        /**
         * @brief 사용자 설정 스키마 — 엔진 적용기 이름 · 전역 변수 대상까지 검사한다. 게임 스키마는 엔진 스키마 위에 덧붙인다(기동과 같은 순서).
         */
        static bool loadUserSettingsSchema( const sw::string& resourceId )
        {
            sw::UserSettingsManager settings;
            sw::UserSettingsTargets targets;
            targets._pGlobalVariableManager = &sw::engine::getGlobalVariableManager();
            settings.initialize( targets );
            const sw::string& engineSchema = sw::engine::getEngineDefaultAssets()._userSettingsSchema;
            if ( resourceId != engineSchema && settings.loadSchema( engineSchema ) == false )
                return false;
            return settings.loadSchema( resourceId );
        }

        static bool loadSpriteClip( const sw::string& resourceId )
        {
            sw::SpriteClipAsset clip;
            return clip.loadFromFile( resourceId );
        }

        // 게임 데이터 — 키트 카탈로그가 읽는다(게임 모듈은 읽은 정의를 조립만 한다). 파일 이름은 게임이 여는 그대로다.
        template <typename TCatalog>
        static bool loadCatalog( const sw::string& resourceId )
        {
            TCatalog catalog;
            return catalog.loadFromResource( resourceId );
        }
        static bool isGameData( sw::string_view resourceId, sw::string_view fileName ) { return startsWith( resourceId, "game/" ) && endsWith( resourceId, fileName ); }
        static bool isAbilities( sw::string_view resourceId ) { return isGameData( resourceId, "/data/abilities.xml" ); }
        static bool isCrops( sw::string_view resourceId ) { return isGameData( resourceId, "/data/crops.xml" ); }
        static bool isCity( sw::string_view resourceId ) { return isGameData( resourceId, "/data/city.xml" ); }
        static bool isWeapons( sw::string_view resourceId ) { return isGameData( resourceId, "/data/weapons.xml" ); }
        static bool isRtsUnits( sw::string_view resourceId ) { return isGameData( resourceId, "/data/units.xml" ); }
        static bool isVoxelBlocks( sw::string_view resourceId ) { return isGameData( resourceId, "/data/blocks.xml" ); }
        static bool isCoasters( sw::string_view resourceId ) { return isGameData( resourceId, "/data/coasters.xml" ); }
        static bool isParkLayout( sw::string_view resourceId ) { return isGameData( resourceId, "/data/rides.xml" ); }
        static bool isGameSettings( sw::string_view resourceId ) { return isGameData( resourceId, "/data/gamesettings.xml" ); }
        static bool loadGameSettings( const sw::string& resourceId )
        {
            sw::GameSettings settings;
            return settings.loadFromResource( resourceId );
        }
        /** @brief 공원 배치는 같은 폴더의 코스터 레이아웃(`coasters.xml`)을 가리킨다 — 그것을 먼저 읽는다. */
        static bool loadParkLayout( const sw::string& resourceId )
        {
            const sw::string         folder = sw::FileUtil::getDirectoryPart( resourceId );
            sw::CoasterLayoutCatalog layouts;
            if ( layouts.loadFromResource( sw::FileUtil::joinPath( folder, "coasters.xml" ) ) == false )
                return false;
            sw::ParkLayout layout;
            return layout.loadFromResource( resourceId, layouts );
        }

        /** @brief 데이터 종류 표입니다. 앞의 줄이 먼저 맞습니다. */
        static constexpr DataKind kArrDataKind[] = {
            {              "scene",               &isScene,                             &loadScene},
            {             "prefab",              &isPrefab,                            &loadPrefab},
            {           "pipeline",            &isPipeline,                          &loadPipeline},
            {         "renderpass",          &isRenderPass,                        &loadRenderPass},
            {"enginedefaultassets", &isEngineDefaultAssets,               &loadEngineDefaultAssets},
            {           "inputmap",            &isInputMap,                          &loadInputMap},
            {           "material",            &isMaterial,                          &loadMaterial},
            {         "spriteclip",          &isSpriteClip,                        &loadSpriteClip},
            {      "camerapresets",       &isCameraPresets,  &loadCatalog<sw::CameraPresetCatalog>},
            {          "schedules",           &isSchedules,      &loadCatalog<sw::ScheduleCatalog>},
            {       "usersettings",  &isUserSettingsSchema,                &loadUserSettingsSchema},
            {          "abilities",           &isAbilities,       &loadCatalog<sw::AbilityCatalog>},
            {              "crops",               &isCrops,          &loadCatalog<sw::CropCatalog>},
            {               "city",                &isCity,          &loadCatalog<sw::CityCatalog>},
            {            "weapons",             &isWeapons,        &loadCatalog<sw::WeaponCatalog>},
            {           "rtsunits",            &isRtsUnits,           &loadCatalog<sw::RtsCatalog>},
            {        "voxelblocks",         &isVoxelBlocks,    &loadCatalog<sw::VoxelBlockCatalog>},
            {           "coasters",            &isCoasters, &loadCatalog<sw::CoasterLayoutCatalog>},
            {         "parklayout",          &isParkLayout,                        &loadParkLayout},
            {       "gamesettings",        &isGameSettings,                      &loadGameSettings},
        };

        /**
         * @brief 게임 모듈(`Source/Games` 아래 헤더)이 `REFLECT` 로 선언한 타입 이름을 모읍니다.
         * @details EngineTest 는 게임 모듈을 링크하지 않는다 — 게임 팩의 씬 · 프리팹에 놓인 게임 컴포넌트는 여기서 모르는 타입이다. 그 이름이 실제로
         *          게임 소스에 선언된 것일 때만 그 경고를 넘긴다(오타 · 지운 타입은 그대로 실패다). 게임 모듈의 타입은 게임 빌드의 쿠킹이 본다.
         */
        static void collectGameModuleTypeNames( const sw::string& resourceRoot, sw::vector<sw::string>& outListTypeName )
        {
            outListTypeName.clear();
            const sw::string       repositoryRoot = sw::FileUtil::getDirectoryPart( sw::FileUtil::trimTrailingSlashes( resourceRoot ) );
            sw::vector<sw::string> listHeader;
            if ( sw::FileUtil::collectFiles( sw::FileUtil::joinPath( repositoryRoot, "Source/Games" ), ".h", listHeader, true ) == false )
                return;
            for ( const sw::string& headerPath : listHeader )
            {
                sw::string text;
                if ( sw::FileUtil::readTextFile( headerPath, text ) == false )
                    continue;
                for ( size_t reflectPos = text.find( "REFLECT(" ); reflectPos != sw::string::npos; reflectPos = text.find( "REFLECT(", reflectPos + 1 ) )
                {
                    const size_t classPos = text.find( "class ", reflectPos );
                    if ( classPos == sw::string::npos )
                        break;
                    size_t nameStart = classPos + 6;
                    size_t nameEnd   = nameStart;
                    while ( nameEnd < text.size() && ( std::isalnum( static_cast<uint8>( text[nameEnd] ) ) != 0 || text[nameEnd] == '_' ) )
                        ++nameEnd;
                    if ( nameEnd > nameStart )
                        outListTypeName.push_back( text.substr( nameStart, nameEnd - nameStart ) );
                }
            }
        }

        /** @brief 모은 경고에서 게임 모듈 타입의 "모르는 타입" 줄을 뺀 나머지입니다. */
        static sw::string removeGameModuleTypeWarnings( const sw::string& joined, const sw::vector<sw::string>& listGameTypeName )
        {
            sw::string result;
            size_t     lineStart = 0;
            while ( lineStart < joined.size() )
            {
                size_t lineEnd = joined.find( "\n  ", lineStart + 1 );
                if ( lineEnd == sw::string::npos )
                    lineEnd = joined.size();
                const sw::string line        = joined.substr( lineStart, lineEnd - lineStart );
                bool             bGameModule = false;
                for ( const sw::string& typeName : listGameTypeName )
                    bGameModule = bGameModule || line.find( "of unknown type '" + typeName + "'" ) != sw::string::npos;
                if ( bGameModule == false )
                    result += line;
                lineStart = lineEnd;
            }
            return result;
        }

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

    sw::vector<sw::string> listGameTypeName;
    ResourceDataSchemaInternal::collectGameModuleTypeNames( resourceRoot, listGameTypeName );

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
        const sw::string warnings = ResourceDataSchemaInternal::removeGameModuleTypeWarnings( logs.joined(), listGameTypeName );
        SW_EXPECT_TRUE_MSG( warnings.empty(), ( resourceId + " 를 읽으며 경고가 났습니다:" + warnings ).c_str() );
        ++loadedCount;
    }
    // 장면 · 프리팹 · 파이프라인 · 렌더 패스 · 엔진 데이터가 실제로 훑였는지 — 경로를 못 찾아 0 개면 이 시험은 아무것도 보지 않은 것이다.
    SW_EXPECT_TRUE_MSG( loadedCount >= 10u, ( "읽은 데이터 파일이 " + std::to_string( loadedCount ) + " 개뿐입니다" ).c_str() );
}

/**
 * @brief [ResourceDataSchemaTest] 컴포넌트 원소의 모르는 속성은 버려지고 이름으로 알린다 — 위 시험이 기대는 경고 경로
 * @details XML 이 루트 원소의 모르는 속성만 orphan 으로 올리고 안쪽 원소(컴포넌트 · 구조체 칸)의 모르는 속성을 말없이 버리면, 이름을 바꾸고
 *          씬 데이터를 빠뜨려도 로드가 조용하다.
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
