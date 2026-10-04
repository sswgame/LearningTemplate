#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/AnimGraphAsset.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Character/BodyShape.h"
#include "Engine/Character/FitPartData.h"
#include "Engine/Character/FitSolver.h"
#include "Engine/Character/FitTables.h"
#include "Engine/Character/ReferencePoseOverride.h"
#include "Engine/Character/SocketSet.h"
#include "Engine/Character/SurfaceState.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/TranslationMemory.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Physics/PhysicsAsset.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Telemetry/TelemetrySchema.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Utility/TileMap/TileSetAsset.h"
#include "Engine/Utility/Xml/TileMapXml.h"

#include "GameFramework/AI/Director/AiDirectorProfile.h"
#include "GameFramework/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/AI/SpawnDirector.h"
#include "GameFramework/Ability/AbilityCatalog.h"
#include "GameFramework/Appearance/AppearanceDatabase.h"
#include "GameFramework/Camera/CameraPreset.h"
#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/Data/GameSettings.h"
#include "GameFramework/Gimmick/ElementRuleTable.h"
#include "GameFramework/Interaction/InteractionCatalog.h"
#include "GameFramework/Inventory/ItemCatalog.h"
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
        static bool isAiDirector( sw::string_view resourceId ) { return endsWith( resourceId, ".director.xml" ); }
        static bool isSpawnTable( sw::string_view resourceId ) { return endsWith( resourceId, ".spawns.xml" ); }
        static bool isTelemetrySchema( sw::string_view resourceId ) { return endsWith( resourceId, ".telemetry.xml" ); }
        static bool isUserSettingsSchema( sw::string_view resourceId ) { return endsWith( resourceId, ".settings.xml" ); }
        static bool isPhysicsSettings( sw::string_view resourceId ) { return endsWith( resourceId, "physicssettings.xml" ); }
        static bool isPhysicsAsset( sw::string_view resourceId ) { return endsWith( resourceId, ".physics.xml" ); }
        static bool isSkeleton( sw::string_view resourceId ) { return endsWith( resourceId, sw::Skeleton::kExtension ); }
        static bool isAnimGraph( sw::string_view resourceId ) { return endsWith( resourceId, ".animgraph.json" ); }
        /** @brief 애니메이션 그래프(상태 기계) — 모르는 조건 표기는 로드 오류, 노드가 하나도 없으면 빈 그래프다. */
        static bool loadAnimGraph( const sw::string& resourceId )
        {
            sw::AnimGraphAsset graph;
            return graph.loadFromFile( resourceId ) && graph._listNode.empty() == false;
        }
        static bool isElementRules( sw::string_view resourceId ) { return endsWith( resourceId, ".elements.xml" ); }
        static bool isInteractions( sw::string_view resourceId ) { return endsWith( resourceId, ".interactions.xml" ); }
        static bool isTileSet( sw::string_view resourceId ) { return endsWith( resourceId, ".tileset.xml" ); }
        static bool isTileMap( sw::string_view resourceId ) { return endsWith( resourceId, ".tilemap.xml" ); }
        static bool isRender2DSettings( sw::string_view resourceId ) { return endsWith( resourceId, "/data/render2d.xml" ); }
        static bool isAudioMixer( sw::string_view resourceId ) { return endsWith( resourceId, ".audiomixer.xml" ); }
        static bool isAudioEvents( sw::string_view resourceId ) { return endsWith( resourceId, ".audioevents.xml" ); }
        static bool isAudioMusic( sw::string_view resourceId ) { return endsWith( resourceId, ".music.xml" ); }
        static bool isCultureTable( sw::string_view resourceId ) { return endsWith( resourceId, sw::CultureTable::kFileSuffix ); }
        static bool isLocalizationProject( sw::string_view resourceId ) { return endsWith( resourceId, sw::LocalizationProject::kFileSuffix ); }
        static bool isSourceStringTable( sw::string_view resourceId ) { return endsWith( resourceId, sw::SourceStringTable::kFileSuffix ); }
        static bool isTranslationTable( sw::string_view resourceId ) { return endsWith( resourceId, sw::TranslationTable::kFileSuffix ); }
        static bool isTranslationMemory( sw::string_view resourceId ) { return endsWith( resourceId, sw::TranslationMemory::kFileSuffix ); }

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

        static bool loadPhysicsSettings( const sw::string& resourceId )
        {
            sw::PhysicsSettings settings;
            return settings.loadFromResource( resourceId );
        }

        static bool loadPhysicsAsset( const sw::string& resourceId )
        {
            sw::PhysicsAsset asset;
            return asset.loadFromResource( resourceId );
        }

        /** @brief 타일 레이어가 있으면 그 타일셋도 읽고 팔레트의 이름이 모두 타일셋에 있는지 본다. */
        static bool loadTileMap( const sw::string& resourceId )
        {
            sw::TileMapXmlData map;
            if ( map.load( resourceId ) == false )
                return false;
            if ( map._tileSetPath.empty() )
                return true;
            sw::TileSetAsset   tileSet;
            sw::vector<uint16> listBrushIndex;
            return tileSet.loadFromResource( map._tileSetPath ) && map.mapTileCells( tileSet, listBrushIndex );
        }

        static bool loadTileSet( const sw::string& resourceId )
        {
            sw::TileSetAsset tileSet;
            return tileSet.loadFromResource( resourceId );
        }

        static bool loadRender2DSettings( const sw::string& resourceId )
        {
            sw::Render2DSettings settings;
            return settings.loadFromResource( resourceId );
        }

        /** @brief 로컬라이제이션 JSON 문서(`loadFromJsonText( text, name, &error )` 모양)를 읽습니다. 모르는 칸은 오류 글로 돌아온다. */
        template <typename TDocument>
        static bool loadLocalizationDocument( const sw::string& resourceId )
        {
            sw::string text;
            if ( sw::ResourceUtil::readTextResource( resourceId, text ) == false )
                return false;
            TDocument  document;
            sw::string error;
            if ( document.loadFromJsonText( text, resourceId, &error ) )
                return true;
            SW_LOG_WARNING( "%#", error.c_str() );
            return false;
        }

        static bool loadSpriteClip( const sw::string& resourceId )
        {
            sw::SpriteClipAsset clip;
            return clip.loadFromFile( resourceId );
        }

        // 캐릭터 데이터 — 소켓은 엔진 기본 종류 표에, 부품 피팅은 엔진 기본 피팅 표에 대조한다(모르는 이름은 로드 오류).
        static constexpr const utf8* kDefaultSocketKinds = "engine/character/default.socketkinds.xml";
        static constexpr const utf8* kDefaultFitTables   = "engine/character/default.fit.xml";
        static bool                  isSocketKinds( sw::string_view resourceId ) { return endsWith( resourceId, ".socketkinds.xml" ); }
        static bool                  isSockets( sw::string_view resourceId ) { return endsWith( resourceId, ".sockets.xml" ); }
        static bool                  isReferencePose( sw::string_view resourceId ) { return endsWith( resourceId, ".refpose.xml" ); }
        static bool                  isBodyShape( sw::string_view resourceId ) { return endsWith( resourceId, ".bodyshape.xml" ); }
        static bool                  isFitTables( sw::string_view resourceId ) { return endsWith( resourceId, ".fit.xml" ); }
        static bool                  isPartFit( sw::string_view resourceId ) { return endsWith( resourceId, ".partfit.xml" ); }
        static bool                  isSurfaceChannels( sw::string_view resourceId ) { return endsWith( resourceId, ".surfacechannels.xml" ); }
        static bool                  loadSockets( const sw::string& resourceId )
        {
            sw::SocketKindTable kinds;
            sw::SocketSet       sockets;
            return kinds.loadFromResource( kDefaultSocketKinds ) && sockets.loadFromResource( resourceId, kinds );
        }
        static bool loadFitTables( const sw::string& resourceId )
        {
            const sw::FitSolver solver;
            sw::FitTables       tables;
            return tables.loadFromResource( resourceId, solver.getOperatorRegistry() );
        }
        static bool loadPartFit( const sw::string& resourceId )
        {
            const sw::FitSolver solver;
            sw::FitTables       tables;
            sw::FitPartData     data;
            return tables.loadFromResource( kDefaultFitTables, solver.getOperatorRegistry() ) && data.loadFromResource( resourceId, tables );
        }

        /** @brief 임포트가 쓴 스켈레톤(본 · 부착 표) — 모르는 키 · 없는 본 이름은 로드 오류다. */
        static bool loadSkeleton( const sw::string& resourceId )
        {
            sw::Skeleton skeleton;
            return skeleton.loadFromResource( resourceId );
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
        static bool isItems( sw::string_view resourceId ) { return isGameData( resourceId, "/data/items.xml" ); }
        static bool isAppearanceData( sw::string_view resourceId )
        {
            return startsWith( resourceId, "game/" ) && resourceId.find( "/data/appearance/" ) != sw::string_view::npos && endsWith( resourceId, ".xml" );
        }
        /** @brief 외형 데이터는 폴더 한 벌로 읽고 서로 대조한다 — 아이템은 같은 게임의 `data/items.xml` 이다. 파일마다 폴더 전체를 읽는다(작다). */
        static bool loadAppearanceData( const sw::string& resourceId )
        {
            const sw::string folder    = sw::FileUtil::getDirectoryPart( resourceId );
            const sw::string itemsPath = sw::FileUtil::joinPath( sw::FileUtil::getDirectoryPart( sw::FileUtil::trimTrailingSlashes( folder ) ), "items.xml" );
            sw::ItemCatalog  items;
            if ( items.loadFromResource( itemsPath ) == false )
                return false;
            sw::AppearanceDatabase database;
            return database.loadFromFolder( folder, &items );
        }
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
            {              "scene",               &isScene,                                         &loadScene},
            {             "prefab",              &isPrefab,                                        &loadPrefab},
            {           "pipeline",            &isPipeline,                                      &loadPipeline},
            {         "renderpass",          &isRenderPass,                                    &loadRenderPass},
            {"enginedefaultassets", &isEngineDefaultAssets,                           &loadEngineDefaultAssets},
            {           "inputmap",            &isInputMap,                                      &loadInputMap},
            {           "material",            &isMaterial,                                      &loadMaterial},
            {         "spriteclip",          &isSpriteClip,                                    &loadSpriteClip},
            {      "camerapresets",       &isCameraPresets,              &loadCatalog<sw::CameraPresetCatalog>},
            {           "render2d",    &isRender2DSettings,                              &loadRender2DSettings},
            {            "tileset",             &isTileSet,                                       &loadTileSet},
            {            "tilemap",             &isTileMap,                                       &loadTileMap},
            {       "elementrules",        &isElementRules,                 &loadCatalog<sw::ElementRuleTable>},
            {       "interactions",        &isInteractions,               &loadCatalog<sw::InteractionCatalog>},
            {    "physicssettings",     &isPhysicsSettings,                               &loadPhysicsSettings},
            {       "physicsasset",        &isPhysicsAsset,                                  &loadPhysicsAsset},
            {          "schedules",           &isSchedules,                  &loadCatalog<sw::ScheduleCatalog>},
            {         "aidirector",          &isAiDirector,                &loadCatalog<sw::AiDirectorProfile>},
            {         "spawntable",          &isSpawnTable,                       &loadCatalog<sw::SpawnTable>},
            {    "telemetryschema",     &isTelemetrySchema,                  &loadCatalog<sw::TelemetrySchema>},
            {       "usersettings",  &isUserSettingsSchema,                            &loadUserSettingsSchema},
            {          "abilities",           &isAbilities,                   &loadCatalog<sw::AbilityCatalog>},
            {              "crops",               &isCrops,                      &loadCatalog<sw::CropCatalog>},
            {               "city",                &isCity,                      &loadCatalog<sw::CityCatalog>},
            {            "weapons",             &isWeapons,                    &loadCatalog<sw::WeaponCatalog>},
            {           "rtsunits",            &isRtsUnits,                       &loadCatalog<sw::RtsCatalog>},
            {        "voxelblocks",         &isVoxelBlocks,                &loadCatalog<sw::VoxelBlockCatalog>},
            {           "coasters",            &isCoasters,             &loadCatalog<sw::CoasterLayoutCatalog>},
            {         "parklayout",          &isParkLayout,                                    &loadParkLayout},
            {       "gamesettings",        &isGameSettings,                                  &loadGameSettings},
            {        "socketkinds",         &isSocketKinds,                  &loadCatalog<sw::SocketKindTable>},
            {            "sockets",             &isSockets,                                       &loadSockets},
            {      "referencepose",       &isReferencePose,            &loadCatalog<sw::ReferencePoseOverride>},
            {          "bodyshape",           &isBodyShape,                     &loadCatalog<sw::BodyShapeSet>},
            {          "fittables",           &isFitTables,                                     &loadFitTables},
            {            "partfit",             &isPartFit,                                       &loadPartFit},
            {    "surfacechannels",     &isSurfaceChannels,              &loadCatalog<sw::SurfaceChannelTable>},
            {              "items",               &isItems,                      &loadCatalog<sw::ItemCatalog>},
            {         "appearance",      &isAppearanceData,                                &loadAppearanceData},
            {           "skeleton",            &isSkeleton,                                      &loadSkeleton},
            {          "animgraph",           &isAnimGraph,                                     &loadAnimGraph},
            {         "audiomixer",          &isAudioMixer,                   &loadCatalog<sw::AudioMixerDesc>},
            {        "audioevents",         &isAudioEvents,                &loadCatalog<sw::AudioEventLibrary>},
            {         "audiomusic",          &isAudioMusic,                   &loadCatalog<sw::AudioMusicDesc>},
            {       "culturetable",        &isCultureTable,        &loadLocalizationDocument<sw::CultureTable>},
            {"localizationproject", &isLocalizationProject, &loadLocalizationDocument<sw::LocalizationProject>},
            {        "stringtable",   &isSourceStringTable,   &loadLocalizationDocument<sw::SourceStringTable>},
            {   "translationtable",    &isTranslationTable,    &loadLocalizationDocument<sw::TranslationTable>},
            {  "translationmemory",   &isTranslationMemory,   &loadLocalizationDocument<sw::TranslationMemory>},
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
