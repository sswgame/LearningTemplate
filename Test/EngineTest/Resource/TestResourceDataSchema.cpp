#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Animation/AnimationJSONUtil.h"
#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Facial/LiveLinkFace.h"
#include "Engine/Animation/Graph/AnimationBoneMask.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Automation/AutomationLogPolicy.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/FitPartData.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/PoseModifier/ReferencePoseOverride.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/TranslationMemory.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/AnimationLOD.h"
#include "Engine/Object/SceneAnimation/VertexAnimationCooker.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Telemetry/TelemetrySchema.h"
#include "Engine/Text/FontCatalog.h"
#include "Engine/TileMap/TileMapXML.h"
#include "Engine/TileMap/TileSetAsset.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/Style/UIStyleSheetCache.h"
#include "Engine/UI/Style/UITheme.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "EngineTest/HostTargetTestUtil.h"

#include "TestFramework/Harness/TestHostRuntime.h"
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
            bool ( *_pIsKind )( sw::string_view resourceID );
            bool ( *_pLoad )( const sw::string& resourceID );
        };

        static bool endsWith( sw::string_view resourceID, sw::string_view suffix ) { return sw::StringUtil::endsWith( resourceID, suffix, true ); }
        static bool startsWith( sw::string_view resourceID, sw::string_view prefix ) { return sw::StringUtil::startsWith( resourceID, prefix, true ); }

        static bool isScene( sw::string_view resourceID ) { return endsWith( resourceID, ".scene.xml" ); }
        static bool isPrefab( sw::string_view resourceID ) { return endsWith( resourceID, ".prefab.xml" ) || endsWith( resourceID, ".prefab.json" ); }
        static bool isPipeline( sw::string_view resourceID ) { return startsWith( resourceID, "engine/pipeline/" ) && endsWith( resourceID, ".xml" ); }
        static bool isRenderPass( sw::string_view resourceID ) { return startsWith( resourceID, "engine/renderpass/" ) && endsWith( resourceID, ".xml" ); }
        static bool isEngineDefaultAssets( sw::string_view resourceID ) { return endsWith( resourceID, "enginedefaultassets.xml" ); }
        static bool isInputMap( sw::string_view resourceID ) { return endsWith( resourceID, ".input.xml" ); }
        static bool isAutomationScenario( sw::string_view resourceID ) { return endsWith( resourceID, ".scenario.xml" ); }
        static bool isAutomationAllowList( sw::string_view resourceID ) { return resourceID == sw::AutomationLogPolicy::kAllowListPath; }
        static bool isMaterial( sw::string_view resourceID ) { return endsWith( resourceID, ".material" ); }
        static bool isSpriteClip( sw::string_view resourceID ) { return endsWith( resourceID, ".sprite.xml" ); }
        static bool isDialogueGraph( sw::string_view resourceID ) { return endsWith( resourceID, sw::DialogueGraphAsset::kExtension ); }
        static bool isTelemetrySchema( sw::string_view resourceID ) { return endsWith( resourceID, ".telemetry.xml" ); }
        static bool isUserSettingsSchema( sw::string_view resourceID ) { return endsWith( resourceID, ".settings.xml" ); }
        static bool isPhysicsSettings( sw::string_view resourceID ) { return endsWith( resourceID, "physicssettings.xml" ); }
        static bool isNavMeshSettings( sw::string_view resourceID ) { return endsWith( resourceID, "navmeshsettings.xml" ); }
        static bool isPhysicsAsset( sw::string_view resourceID ) { return endsWith( resourceID, ".physics.xml" ); }
        static bool isSkeleton( sw::string_view resourceID ) { return endsWith( resourceID, sw::Skeleton::kExtension ); }
        static bool isAnimationGraph( sw::string_view resourceID ) { return endsWith( resourceID, ".animgraph.xml" ); }
        /** @brief 애니메이션 그래프(상태 기계) — 모르는 조건 표기는 로드 오류, 노드가 하나도 없으면 빈 그래프다. */
        [[nodiscard]] static bool loadAnimationGraph( const sw::string& resourceID )
        {
            sw::AnimationGraphAsset graph;
            return graph.loadFromFile( resourceID ) && graph._listNode.empty() == false;
        }
        /** @brief 본 마스크 프리셋(그래프 레이어가 "file" 로 가리킨다) — 항목이 없거나 가중치가 범위 밖이면 로드 오류다. */
        static bool               isBoneMask( sw::string_view resourceID ) { return endsWith( resourceID, sw::AnimationBoneMaskDef::kExtension ); }
        [[nodiscard]] static bool loadBoneMask( const sw::string& resourceID )
        {
            sw::AnimationBoneMaskDef mask;
            return mask.loadFromFile( resourceID );
        }
        static bool isTileSet( sw::string_view resourceID ) { return endsWith( resourceID, ".tileset.xml" ); }
        static bool isTileMap( sw::string_view resourceID ) { return endsWith( resourceID, ".tilemap.xml" ); }
        static bool isRender2DSettings( sw::string_view resourceID ) { return endsWith( resourceID, "/data/render2d.xml" ); }
        static bool isFontCatalog( sw::string_view resourceID ) { return endsWith( resourceID, "fontcatalog.xml" ); }
        static bool isUIScale( sw::string_view resourceID ) { return endsWith( resourceID, "uiscale.xml" ); }
        static bool isUIDocument( sw::string_view resourceID ) { return endsWith( resourceID, sw::UIDocumentAsset::kExtension ); }
        static bool isUIStyleSheet( sw::string_view resourceID ) { return endsWith( resourceID, sw::UIStyleSheetAsset::kExtension ); }
        static bool isUIThemes( sw::string_view resourceID ) { return endsWith( resourceID, "uithemes.xml" ); }

        /** @brief 스타일 시트를 읽습니다(모르는 칸 · 변수 · 선택자 문법 · 읽지 못한 값). */
        [[nodiscard]] static bool loadUIStyleSheet( const sw::string& resourceID )
        {
            sw::UIStyleSheetCache cache;
            sw::string            error;
            if ( cache.findOrLoad( resourceID, error ) == nullptr )
            {
                SW_LOG_WARNING( "%#", error.c_str() );
                return false;
            }
            return true;
        }

        /** @brief UI 문서를 읽고 위젯 트리까지 짓습니다(모르는 타입 · 속성 · 열거자 · 조각). */
        [[nodiscard]] static bool loadUIDocument( const sw::string& resourceID )
        {
            sw::UIDocumentCache                             cache;
            sw::string                                      error;
            const sw::shared_ptr<const sw::UIDocumentAsset> document = cache.findOrLoad( resourceID, error );
            sw::vector<sw::UIBindingDesc>                   listBinding;
            if ( document == nullptr || sw::UIDocumentLoader::instantiate( *document, cache, listBinding, error ) == nullptr )
            {
                SW_LOG_WARNING( "%#", error.c_str() );
                return false;
            }
            return true;
        }
        static bool isCultureTable( sw::string_view resourceID ) { return endsWith( resourceID, sw::CultureTable::kExtension ); }
        static bool isLocalizationProject( sw::string_view resourceID ) { return endsWith( resourceID, sw::LocalizationProject::kExtension ); }
        static bool isSourceStringTable( sw::string_view resourceID ) { return endsWith( resourceID, sw::SourceStringTable::kExtension ); }
        static bool isTranslationTable( sw::string_view resourceID ) { return endsWith( resourceID, sw::TranslationTable::kExtension ); }
        static bool isTranslationMemory( sw::string_view resourceID ) { return endsWith( resourceID, sw::TranslationMemory::kExtension ); }

        [[nodiscard]] static bool loadScene( const sw::string& resourceID )
        {
            sw::SceneDocument doc;
            if ( doc.loadXML( resourceID ) == false )
                return false;
            sw::Scene scene{ "ResourceDataSchemaScene" };
            return scene.instantiate( doc );
        }

        [[nodiscard]] static bool loadPrefab( const sw::string& resourceID )
        {
            sw::PrefabAsset prefab;
            const bool      bLoaded = endsWith( resourceID, ".json" ) ? prefab.loadFromJSONFile( resourceID ) : prefab.loadFromXMLFile( resourceID );
            if ( bLoaded == false )
                return false;
            sw::GameObjectManager manager;
            sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "ResourceDataSchemaPrefab" ) );
            return pObject != nullptr && prefab.applyStateTo( pObject );
        }

        [[nodiscard]] static bool loadPipeline( const sw::string& resourceID )
        {
            sw::RenderPipelineAsset pipeline;
            return pipeline.loadFromXMLFile( resourceID );
        }

        [[nodiscard]] static bool loadRenderPass( const sw::string& resourceID )
        {
            sw::RenderPassAsset pass;
            return pass.loadFromXMLFile( resourceID );
        }

        [[nodiscard]] static bool loadEngineDefaultAssets( const sw::string& resourceID )
        {
            return sw::EngineDefaultAssets::load( resourceID ) != nullptr;
        }

        /** @brief 시나리오는 형식(루트 · 루트 속성 · <At>)만 읽는다 — 단계 종류 · 탐침은 그 게임 모듈이 올라온 실기동(AppScenarioTest)이 본다. */
        [[nodiscard]] static bool loadAutomationScenario( const sw::string& resourceID )
        {
            sw::AutomationScenario scenario;
            sw::string             error;
            const bool             bLoaded = scenario.loadFromPath( resourceID, error );
            if ( bLoaded == false )
                SW_LOG_WARNING( "%#", error.c_str() );
            return bLoaded;
        }

        /** @brief 자동화 로그 게이트의 허용 목록 — 항목마다 이유가 있어야 읽힌다. */
        [[nodiscard]] static bool loadAutomationAllowList( const sw::string& resourceID )
        {
            (void)resourceID; // 경로는 하나(`kAllowListPath`)뿐이다
            sw::AutomationLogPolicy policy;
            sw::string              error;
            const bool              bLoaded = policy.loadAllowList( error );
            if ( bLoaded == false )
                SW_LOG_WARNING( "%#", error.c_str() );
            return bLoaded;
        }

        [[nodiscard]] static bool loadInputMap( const sw::string& resourceID )
        {
            sw::InputMap map;
            return map.loadFromResource( resourceID );
        }

        [[nodiscard]] static bool loadMaterial( const sw::string& resourceID )
        {
            const sw::shared_ptr<sw::Material> material = sw::Material::create();
            return material != nullptr && material->loadFromFile( resourceID );
        }

        /**
         * @brief 사용자 설정 스키마 — 엔진 핸들러 이름 · 전역 변수 대상까지 검사한다. 게임 스키마는 엔진 스키마 위에 덧붙인다(기동과 같은 순서).
         */
        [[nodiscard]] static bool loadUserSettingsSchema( const sw::string& resourceID )
        {
            sw::UserSettingsManager settings;
            sw::UserSettingsTargets targets;
            targets._pGlobalVariableManager = &sw::engine::getSubsystem<sw::GlobalVariableManager>();
            settings.initialize( targets );
            const sw::string& engineSchema = test::getEngineDefaultAssets()._userSettingsSchema.get();
            if ( resourceID != engineSchema && settings.loadSchema( engineSchema ) == false )
                return false;
            return settings.loadSchema( resourceID );
        }

        [[nodiscard]] static bool loadPhysicsSettings( const sw::string& resourceID )
        {
            sw::PhysicsSettings settings;
            return settings.loadFromResource( resourceID );
        }

        [[nodiscard]] static bool loadPhysicsAsset( const sw::string& resourceID )
        {
            sw::PhysicsAsset asset;
            return asset.loadFromResource( resourceID );
        }

        /** @brief 타일 레이어가 있으면 그 타일셋도 읽고 팔레트의 이름이 모두 타일셋에 있는지 본다. */
        [[nodiscard]] static bool loadTileMap( const sw::string& resourceID )
        {
            sw::TileMapXMLData map;
            if ( map.load( resourceID ) == false )
                return false;
            if ( map._tileSetPath.empty() )
                return true;
            sw::TileSetAsset   tileSet;
            sw::vector<uint16> listBrushIndex;
            return tileSet.loadFromResource( map._tileSetPath ) && map.mapTileCells( tileSet, listBrushIndex );
        }

        [[nodiscard]] static bool loadTileSet( const sw::string& resourceID )
        {
            sw::TileSetAsset tileSet;
            return tileSet.loadFromResource( resourceID );
        }

        [[nodiscard]] static bool loadRender2DSettings( const sw::string& resourceID )
        {
            sw::Render2DSettings settings;
            return settings.loadFromResource( resourceID );
        }

        /** @brief 로컬라이제이션 XML 문서(`loadFromXMLText( text, name, &error )` 모양)를 읽습니다. 모르는 칸은 오류 글로 돌아온다. */
        template <typename TDocument>
        [[nodiscard]] static bool loadLocalizationDocument( const sw::string& resourceID )
        {
            sw::string text;
            if ( sw::ResourceIO::readTextResource( resourceID, text ) == false )
                return false;
            TDocument  document;
            sw::string error;
            if ( document.loadFromXMLText( text, resourceID, &error ) )
                return true;
            SW_LOG_WARNING( "%#", error.c_str() );
            return false;
        }

        static bool isSequence( sw::string_view resourceID ) { return endsWith( resourceID, ".seq.xml" ); }
        /** @brief 시퀀스 — 키 트랙은 모두 아는 종류여야 한다(모르는 종류 이름 · 채널 수가 틀린 트랙은 읽기 오류다). */
        [[nodiscard]] static bool loadSequence( const sw::string& resourceID )
        {
            sw::SequenceAsset sequence;
            if ( sequence.loadFromFile( resourceID ) == false )
                return false;
            for ( const sw::SequenceKeyTrack& track : sequence._listTrack )
            {
                if ( sw::SequenceKeyTrack::getChannelCount( track._kind ) == 0 )
                    return false;
            }
            return true;
        }

        [[nodiscard]] static bool loadSpriteClip( const sw::string& resourceID )
        {
            sw::SpriteClipAsset clip;
            return clip.loadFromFile( resourceID );
        }

        [[nodiscard]] static bool loadDialogueGraph( const sw::string& resourceID )
        {
            sw::DialogueGraphAsset graph;
            return graph.loadFromFile( resourceID );
        }

        // 캐릭터 데이터 — 소켓은 엔진 기본 종류 표에, 부품 피팅은 엔진 기본 피팅 표에 대조한다(모르는 이름은 로드 오류).
        static constexpr const utf8* kDefaultSocketKinds = "engine/character/default.socketkinds.xml";
        static constexpr const utf8* kDefaultFitTables   = "engine/character/default.fit.xml";
        static bool                  isSocketKinds( sw::string_view resourceID ) { return endsWith( resourceID, ".socketkinds.xml" ); }
        static bool                  isSockets( sw::string_view resourceID ) { return endsWith( resourceID, ".sockets.xml" ); }
        static bool                  isReferencePose( sw::string_view resourceID ) { return endsWith( resourceID, ".refpose.xml" ); }
        static bool                  isBodyShape( sw::string_view resourceID ) { return endsWith( resourceID, ".bodyshape.xml" ); }
        static bool                  isFitTables( sw::string_view resourceID ) { return endsWith( resourceID, ".fit.xml" ); }
        static bool                  isPartFit( sw::string_view resourceID ) { return endsWith( resourceID, ".partfit.xml" ); }
        static bool                  isSurfaceChannels( sw::string_view resourceID ) { return endsWith( resourceID, ".surfacechannels.xml" ); }
        static bool                  isDestructionProfile( sw::string_view resourceID ) { return endsWith( resourceID, ".destruction.xml" ); }
        static bool                  isNotifyTable( sw::string_view resourceID ) { return endsWith( resourceID, ".notifies.xml" ); }
        static bool                  isClipData( sw::string_view resourceID ) { return endsWith( resourceID, ".clips.json" ); }
        /** @brief 페어드 애니메이션(역할 · 정렬 · 공유 알림) — 모르는 속성 · 유한하지 않은 수 · 앵커 규칙 위반은 읽기 오류다. 클립은 세션 시작에서 본다. */
        static bool               isPairedAnimation( sw::string_view resourceID ) { return endsWith( resourceID, sw::PairedAnimationDef::kExtension ); }
        [[nodiscard]] static bool loadPairedAnimation( const sw::string& resourceID )
        {
            sw::PairedAnimationDef def;
            return def.loadFromResource( resourceID );
        }
        /** @brief 모델 임포트 곁 데이터(`<모델>.clips.json`) — 임포터(`ModelImporter::readClipData`)와 같은 키 규칙(모르는 키는 오류)으로 본다. */
        [[nodiscard]] static bool loadClipData( const sw::string& resourceID )
        {
            sw::JSONDocument document;
            // models_raw/ 는 팩에 실리지 않아 Shipping 에서는 리소스 id 로 못 찾는다 — 임포터처럼 원본 트리에서 파일로 읽는다.
            if ( document.loadFile( sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), resourceID ) ) == false )
                return false;
            const sw::JSONValue root = document.getRoot();
            if ( sw::AnimationJSONUtil::hasOnlyKnownKeys( root, { "clips" }, resourceID ) == false || root.get( "clips" ).isObject() == false )
                return false;
            const sw::JSONValue clips = root.get( "clips" );
            for ( const sw::string& clipName : clips.getMemberNames() )
            {
                const sw::JSONValue clip = clips.get( clipName, false );
                if ( sw::AnimationJSONUtil::hasOnlyKnownKeys( clip, { "loop", "notifies", "curves" }, resourceID ) == false )
                    return false;
                const sw::JSONValue notifies = clip.get( "notifies" );
                for ( size_t notifyIndex = 0; notifies.isArray() && notifyIndex < notifies.size(); ++notifyIndex )
                {
                    const sw::JSONValue notify         = notifies.at( notifyIndex );
                    const bool          bInvalidNotify = sw::AnimationJSONUtil::hasOnlyKnownKeys( notify, { "name", "time", "duration" }, resourceID ) == false || notify.get( "name" ).isString() == false ||
                                                notify.get( "time" ).isNumber() == false;
                    if ( bInvalidNotify )
                        return false;
                }
            }
            return true;
        }

        static bool isSourceManifest( sw::string_view resourceID ) { return endsWith( resourceID, ".sources.json" ); }
        /**
         * @brief 리소스 출처 목록(`common/audio.sources.json` · `engine/textures.sources.json`) — 도구(`Scripts/dev/MakeCommonAudio.py` · `MakeCc0Import.py`)가 읽고 쓰는 키 규칙(모르는 키는 오류)으로 본다.
         * @details 팩에 실리지 않는다(`PackConfig.json` 의 `*.sources.json`) — 원본 트리에서 파일로 읽는다.
         */
        [[nodiscard]] static bool loadSourceManifest( const sw::string& resourceID )
        {
            sw::JSONDocument document;
            if ( document.loadFile( sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), resourceID ) ) == false )
                return false;
            const sw::JSONValue root = document.getRoot();
            if ( sw::AnimationJSONUtil::hasOnlyKnownKeys( root, { "_note", "packs" }, resourceID ) == false || root.get( "packs" ).isArray() == false )
                return false;
            const sw::JSONValue packs = root.get( "packs" );
            for ( size_t packIndex = 0; packIndex < packs.size(); ++packIndex )
            {
                const sw::JSONValue pack     = packs.at( packIndex );
                const bool          bBadPack = sw::AnimationJSONUtil::hasOnlyKnownKeys(
                                          pack, { "folder", "page", "page_license_marker", "download", "download_sha256", "license", "license_source", "license_quote", "convert", "files" }, resourceID ) == false ||
                                      pack.get( "files" ).isArray() == false;
                if ( bBadPack )
                    return false;
                const sw::JSONValue convert = pack.get( "convert" );
                const bool          bBadConvert =
                    convert.isObject() &&
                    sw::AnimationJSONUtil::hasOnlyKnownKeys(
                        convert, { "format", "subtype", "encoder", "vorbis_quality", "compression_level", "libsndfile_version", "soundfile_version" }, resourceID ) == false;
                if ( bBadConvert )
                    return false;
                const sw::JSONValue files = pack.get( "files" );
                for ( size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex )
                {
                    if ( sw::AnimationJSONUtil::hasOnlyKnownKeys( files.at( fileIndex ), { "name", "member", "download", "sha256", "source_sha256" }, resourceID ) == false )
                        return false;
                }
            }
            return true;
        }
        /** @brief 알림 표 — 처리기 이름 · 인자를 엔진 기본 처리기 레지스트리에 대조한다. */
        [[nodiscard]] static bool loadNotifyTable( const sw::string& resourceID )
        {
            sw::AnimationNotifyTable table;
            return table.loadFromResource( resourceID, sw::AnimationNotifyHandlerRegistry::getDefault() );
        }
        [[nodiscard]] static bool loadSockets( const sw::string& resourceID )
        {
            sw::SocketKindTable kinds;
            sw::SocketSet       sockets;
            return kinds.loadFromResource( kDefaultSocketKinds ) && sockets.loadFromResource( resourceID, kinds );
        }
        [[nodiscard]] static bool loadFitTables( const sw::string& resourceID )
        {
            const sw::FitSolver solver;
            sw::FitTables       tables;
            return tables.loadFromResource( resourceID, solver.getOperatorRegistry() );
        }
        [[nodiscard]] static bool loadPartFit( const sw::string& resourceID )
        {
            const sw::FitSolver solver;
            sw::FitTables       tables;
            sw::FitPartData     data;
            sw::string          nameError;
            const bool          bLoaded = tables.loadFromResource( kDefaultFitTables, solver.getOperatorRegistry() ) && data.loadFromResource( resourceID );
            if ( bLoaded && data.validateNames( tables, &nameError ) == false )
                SW_LOG_WARNING( "'%#' %#", resourceID.c_str(), nameError.c_str() );
            return bLoaded && nameError.empty();
        }

        /** @brief 후처리 리그(대상 · 노드) — 모르는 노드 종류 · 키 · 겹친 이름은 로드 오류다. */
        static bool               isRig( sw::string_view resourceID ) { return endsWith( resourceID, sw::RigAsset::kExtension ); }
        [[nodiscard]] static bool loadRig( const sw::string& resourceID )
        {
            sw::RigAsset rig;
            return rig.loadFromResource( resourceID );
        }

        /** @brief 리타깃 프로필 — 모르는 키 · 이동 방법 · 겹친 사슬은 로드 오류이고, 적힌 두 스켈레톤에 프로필의 본이 모두 있어야 한다. */
        static bool               isRetargetProfile( sw::string_view resourceID ) { return endsWith( resourceID, sw::RetargetProfile::kExtension ); }
        [[nodiscard]] static bool loadRetargetProfile( const sw::string& resourceID )
        {
            sw::RetargetProfile profile;
            if ( profile.loadFromResource( resourceID ) == false )
                return false;
            sw::Skeleton source;
            sw::Skeleton target;
            if ( source.loadFromResource( profile.getSourceSkeletonPath() ) == false || target.loadFromResource( profile.getTargetSkeletonPath() ) == false )
                return false;
            sw::PoseRetargeter retargeter;
            return retargeter.initialize( profile, source, target, nullptr );
        }

        /** @brief 임포트가 쓴 스켈레톤(본 · 부착 표) — 모르는 키 · 없는 본 이름은 로드 오류다. */
        [[nodiscard]] static bool loadSkeleton( const sw::string& resourceID )
        {
            sw::Skeleton skeleton;
            return skeleton.loadFromResource( resourceID );
        }

        /** @brief 스켈레톤 곁 본 LOD 표 — 모르는 키 · 곁 스켈레톤에 없는 본 이름은 오류다. */
        static bool               isBoneLOD( sw::string_view resourceID ) { return endsWith( resourceID, sw::SkeletonBoneLOD::kExtension ); }
        [[nodiscard]] static bool loadBoneLOD( const sw::string& resourceID )
        {
            sw::string importedPath;
            sw::string siblingPath;
            sw::SkeletonBoneLOD::makeSkeletonCandidatePaths( resourceID, importedPath, siblingPath );
            const sw::string&             skeletonPath = sw::ResourceIO::hasResource( importedPath ) ? importedPath : siblingPath;
            sw::SkeletonBoneLOD           boneLOD;
            sw::Skeleton                  skeleton;
            sw::vector<sw::vector<uint8>> listMask;
            return boneLOD.loadFromResource( resourceID ) && skeleton.loadFromResource( skeletonPath ) && boneLOD.computeMasks( skeleton, listMask, resourceID );
        }

        /** @brief 애니메이션 LOD 표(주기 단계 · 예산). */
        static bool isAnimationLOD( sw::string_view resourceID ) { return resourceID == sw::AnimationLODSettings::kResourcePath; }
        /** @brief 군중 공유 표(변형 칸 수 · 묶음 유지 · VAT 프레임율). */
        static bool isAnimationCrowd( sw::string_view resourceID ) { return resourceID == sw::AnimationCrowdSettings::kResourcePath; }
        /** @brief VAT 쿠킹 목록 — 가리키는 메시 · 스켈레톤 · 클립 파일이 모두 있어야 한다. */
        static bool isVertexAnimationList( sw::string_view resourceID ) { return endsWith( resourceID, sw::VertexAnimationCookList::kExtension ); }
        /** @brief 립싱크 분석 표 · 비즘 트랙 · 얼굴 리그 — 모르는 키는 읽기 오류다(리그의 타깃 · 본 이름은 메시를 묶을 때 본다). */
        static bool isLipSync( sw::string_view resourceID ) { return resourceID == sw::LipSyncSettings::kResourcePath; }
        static bool isVisemeTrack( sw::string_view resourceID ) { return endsWith( resourceID, sw::VisemeTrack::kExtension ); }
        static bool isFacialRig( sw::string_view resourceID ) { return endsWith( resourceID, sw::FacialRig::kExtension ); }
        static bool isVrmAvatar( sw::string_view resourceID ) { return endsWith( resourceID, ".avatar.xml" ); }
        /** @brief Live Link Face 패킷 배치 표(`livelinkface.xml`)와 머리마다의 연결 표(`<머리>.livelinkface.xml`) — 연결 표는 가리키는 배치 표까지 읽어야 성공한다. */
        static bool               isLiveLinkFaceLayout( sw::string_view resourceID ) { return resourceID == "livelinkface.xml" || endsWith( resourceID, "/livelinkface.xml" ); }
        static bool               isLiveLinkFaceMapping( sw::string_view resourceID ) { return endsWith( resourceID, ".livelinkface.xml" ); }
        [[nodiscard]] static bool loadLiveLinkFaceLayout( const sw::string& resourceID )
        {
            sw::LiveLinkFaceLayout layout;
            return layout.loadFromResource( resourceID );
        }
        [[nodiscard]] static bool loadLiveLinkFaceMapping( const sw::string& resourceID )
        {
            sw::LiveLinkFaceMapping mapping;
            sw::LiveLinkFaceLayout  layout;
            return mapping.loadFromResource( resourceID, layout );
        }

        /** @brief VRM 아바타 임포트 산출물(`<모델>.avatar.xml`) — 런타임은 이 파일 대신 임포트가 옮긴 얼굴 리그 · 후처리 리그를 읽는다(VrmRuntimeAssets) — 여기는 humanoid · 1인칭 기록이다. 루트 `VrmAvatar` 에 `_version` 속성과 `_mapHumanBone` 요소가 있는지만 본다. */
        [[nodiscard]] static bool loadVrmAvatar( const sw::string& resourceID )
        {
            sw::XMLDocument document;
            if ( document.loadFile( sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), resourceID ) ) == false )
                return false;
            const sw::XMLNode root = document.getRoot( "VrmAvatar", false );
            return root.isValid() && root.findAttribute( "_version", false ) != nullptr && root.findChild( "_mapHumanBone", false ).isValid();
        }

        // 엔진 카탈로그(`XMLCatalog`) 로더입니다. 게임 데이터(키트 · 기반 카탈로그)는 각자의 `*DataKind.cpp` 가 `SW_RESOURCE_DATA_KIND` 로 올린 종류를 `ResourceDataKindRegistry` 에서 찾습니다.
        template <typename TCatalog>
        [[nodiscard]] static bool loadCatalog( const sw::string& resourceID )
        {
            return sw::ResourceDataKindUtil::loadCatalog<TCatalog>( resourceID );
        }

        /** @brief 엔진 데이터 종류 표입니다. 앞의 줄이 먼저 맞습니다. 게임 데이터 카탈로그는 이 표가 아니라 `ResourceDataKindRegistry` 에 있습니다. */
        static constexpr DataKind kArrDataKind[] = {
            {              "scene",               &isScene,                                         &loadScene},
            {             "prefab",              &isPrefab,                                        &loadPrefab},
            {           "pipeline",            &isPipeline,                                      &loadPipeline},
            {         "renderpass",          &isRenderPass,                                    &loadRenderPass},
            {"enginedefaultassets", &isEngineDefaultAssets,                           &loadEngineDefaultAssets},
            {           "inputmap",            &isInputMap,                                      &loadInputMap},
            {           "scenario",  &isAutomationScenario,                            &loadAutomationScenario},
            {     "automationlogs", &isAutomationAllowList,                           &loadAutomationAllowList},
            {           "material",            &isMaterial,                                      &loadMaterial},
            {         "spriteclip",          &isSpriteClip,                                    &loadSpriteClip},
            {      "dialoguegraph",       &isDialogueGraph,                                 &loadDialogueGraph},
            {           "sequence",            &isSequence,                                      &loadSequence},
            {           "render2d",    &isRender2DSettings,                              &loadRender2DSettings},
            {            "tileset",             &isTileSet,                                       &loadTileSet},
            {            "tilemap",             &isTileMap,                                       &loadTileMap},
            {    "physicssettings",     &isPhysicsSettings,                               &loadPhysicsSettings},
            {    "navmeshsettings",     &isNavMeshSettings,                  &loadCatalog<sw::NavMeshSettings>},
            {       "physicsasset",        &isPhysicsAsset,                                  &loadPhysicsAsset},
            {    "telemetryschema",     &isTelemetrySchema,                  &loadCatalog<sw::TelemetrySchema>},
            {       "usersettings",  &isUserSettingsSchema,                            &loadUserSettingsSchema},
            {        "socketkinds",         &isSocketKinds,                  &loadCatalog<sw::SocketKindTable>},
            {            "sockets",             &isSockets,                                       &loadSockets},
            {      "referencepose",       &isReferencePose,            &loadCatalog<sw::ReferencePoseOverride>},
            {          "bodyshape",           &isBodyShape,                     &loadCatalog<sw::BodyShapeSet>},
            {          "fittables",           &isFitTables,                                     &loadFitTables},
            {            "partfit",             &isPartFit,                                       &loadPartFit},
            {    "surfacechannels",     &isSurfaceChannels,              &loadCatalog<sw::SurfaceChannelTable>},
            {        "destruction",  &isDestructionProfile,               &loadCatalog<sw::DestructionProfile>},
            {           "skeleton",            &isSkeleton,                                      &loadSkeleton},
            {          "animgraph",      &isAnimationGraph,                                &loadAnimationGraph},
            {           "bonemask",            &isBoneMask,                                      &loadBoneMask},
            {        "fontcatalog",         &isFontCatalog,                  &loadCatalog<sw::FontCatalogDesc>},
            {            "uiscale",             &isUIScale,                  &loadCatalog<sw::UIScaleSettings>},
            {         "uidocument",          &isUIDocument,                                    &loadUIDocument},
            {       "uistylesheet",        &isUIStyleSheet,                                  &loadUIStyleSheet},
            {           "uithemes",            &isUIThemes,                   &loadCatalog<sw::UIThemeCatalog>},
            {       "culturetable",        &isCultureTable,        &loadLocalizationDocument<sw::CultureTable>},
            {"localizationproject", &isLocalizationProject, &loadLocalizationDocument<sw::LocalizationProject>},
            {        "stringtable",   &isSourceStringTable,   &loadLocalizationDocument<sw::SourceStringTable>},
            {   "translationtable",    &isTranslationTable,    &loadLocalizationDocument<sw::TranslationTable>},
            {  "translationmemory",   &isTranslationMemory,   &loadLocalizationDocument<sw::TranslationMemory>},
            {                "rig",                 &isRig,                                           &loadRig},
            {    "retargetprofile",     &isRetargetProfile,                               &loadRetargetProfile},
            {        "notifytable",         &isNotifyTable,                                   &loadNotifyTable},
            {    "pairedanimation",     &isPairedAnimation,                               &loadPairedAnimation},
            {           "clipdata",            &isClipData,                                      &loadClipData},
            {     "sourcemanifest",      &isSourceManifest,                                &loadSourceManifest},
            {            "bonelod",             &isBoneLOD,                                       &loadBoneLOD},
            {       "animationlod",        &isAnimationLOD,             &loadCatalog<sw::AnimationLODSettings>},
            {     "animationcrowd",      &isAnimationCrowd,           &loadCatalog<sw::AnimationCrowdSettings>},
            {    "vertexanimation", &isVertexAnimationList,          &loadCatalog<sw::VertexAnimationCookList>},
            {            "lipsync",             &isLipSync,                  &loadCatalog<sw::LipSyncSettings>},
            {            "visemes",         &isVisemeTrack,                      &loadCatalog<sw::VisemeTrack>},
            {             "facial",           &isFacialRig,                        &loadCatalog<sw::FacialRig>},
            {          "vrmavatar",           &isVrmAvatar,                                     &loadVrmAvatar},
            { "livelinkfacelayout",  &isLiveLinkFaceLayout,                            &loadLiveLinkFaceLayout},
            {"livelinkfacemapping", &isLiveLinkFaceMapping,                           &loadLiveLinkFaceMapping},
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
                    // 다음 `class` · `struct` 중 가까운 것이 이 REFLECT 의 타입이다(설정 구조체도 게임 모듈 타입이다).
                    const size_t classPos   = text.find( "class ", reflectPos );
                    const size_t structPos  = text.find( "struct ", reflectPos );
                    const bool   bStruct    = structPos != sw::string::npos && ( classPos == sw::string::npos || structPos < classPos );
                    const size_t keywordPos = bStruct ? structPos : classPos;
                    if ( keywordPos == sw::string::npos )
                        break;
                    size_t nameStart = keywordPos + ( bStruct ? 7 : 6 );
                    while ( nameStart < text.size() && text.compare( nameStart, 3, "SW_" ) == 0 )
                    {
                        // 내보내기 매크로(`SW_API` 등)를 건너뛴다.
                        nameStart = text.find( ' ', nameStart );
                        if ( nameStart == sw::string::npos )
                            break;
                        ++nameStart;
                    }
                    if ( nameStart == sw::string::npos )
                        break;
                    size_t nameEnd = nameStart;
                    while ( nameEnd < text.size() && ( std::isalnum( static_cast<uint8>( text[nameEnd] ) ) != 0 || text[nameEnd] == '_' ) )
                    {
                        ++nameEnd;
                    }
                    if ( nameEnd > nameStart )
                        outListTypeName.push_back( text.substr( nameStart, nameEnd - nameStart ) );
                }
            }
        }

        /**
         * @brief 게임 모듈이 등록하는 데이터 종류(`SW_RESOURCE_DATA_KIND` 줄)의 경로 끝(다섯째 인자)을 모읍니다.
         * @details 이 실행 파일은 게임 모듈을 올리지 않아 그 등록이 종류 표에 없다. 리플렉션 루트가 아닌 게임 데이터(원소 목록)도 게임 모듈이
         *          종류로 등록했으면 그 모듈의 것으로 친다 — 등록 줄이 없으면 여전히 "종류 표에 없는 파일" 이다.
         */
        static void collectGameModuleDataKindSuffixes( const sw::string& resourceRoot, sw::vector<sw::string>& outListSuffix )
        {
            outListSuffix.clear();
            const sw::string       repositoryRoot = sw::FileUtil::getDirectoryPart( sw::FileUtil::trimTrailingSlashes( resourceRoot ) );
            sw::vector<sw::string> listSource;
            if ( sw::FileUtil::collectFiles( sw::FileUtil::joinPath( repositoryRoot, "Source/Games" ), ".cpp", listSource, true ) == false )
                return;
            for ( const sw::string& sourcePath : listSource )
            {
                sw::string text;
                if ( sw::FileUtil::readTextFile( sourcePath, text ) == false )
                    continue;
                for ( size_t macroPos = text.find( "SW_RESOURCE_DATA_KIND(" ); macroPos != sw::string::npos;
                      macroPos        = text.find( "SW_RESOURCE_DATA_KIND(", macroPos + 1 ) )
                {
                    // 인자: 변수 이름, 라벨, 앞, 포함, 끝, 읽기 — 다섯째(쉼표 넷 뒤)가 경로 끝 문자열이다.
                    size_t argumentStart = macroPos;
                    for ( uint32 commaIndex = 0; commaIndex < 4 && argumentStart != sw::string::npos; ++commaIndex )
                    {
                        argumentStart = text.find( ',', argumentStart + 1 );
                    }
                    const size_t quoteStart = argumentStart != sw::string::npos ? text.find( '"', argumentStart ) : sw::string::npos;
                    const size_t quoteEnd   = quoteStart != sw::string::npos ? text.find( '"', quoteStart + 1 ) : sw::string::npos;
                    if ( quoteEnd != sw::string::npos )
                        outListSuffix.push_back( text.substr( quoteStart + 1, quoteEnd - quoteStart - 1 ) );
                }
            }
        }

        /** @brief @p resourceID 가 게임 모듈이 등록한 데이터 종류의 경로 끝으로 끝나면 true 입니다. */
        static bool hasGameModuleDataKind( const sw::string& resourceID, const sw::vector<sw::string>& listSuffix )
        {
            for ( const sw::string& suffix : listSuffix )
            {
                if ( endsWith( resourceID, suffix ) )
                    return true;
            }
            return false;
        }

        /** @brief XML 데이터의 루트 원소 이름이 게임 모듈 타입이면 true 입니다. */
        static bool hasGameModuleRoot( const sw::string& resourceID, const sw::vector<sw::string>& listGameTypeName )
        {
            if ( endsWith( resourceID, ".xml" ) == false )
                return false;
            sw::XMLDocument document;
            if ( document.loadResource( resourceID ) == false )
                return false;
            const sw::XMLNode root = document.getRoot( nullptr, false );
            if ( root.isValid() == false || root.getName() == nullptr )
                return false;
            for ( const sw::string& typeName : listGameTypeName )
            {
                if ( typeName == root.getName() )
                    return true;
            }
            return false;
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
                {
                    bGameModule = bGameModule || line.find( "of unknown type '" + typeName + "'" ) != sw::string::npos;
                }
                if ( bGameModule == false )
                    result += line;
                lineStart = lineEnd;
            }
            return result;
        }

        /**
         * @brief 검증 결과를 일부러 내는 테스트 데이터입니다(맵 검사 시나리오가 연다). 이 파일만 검증 경고 줄을 받고, 그 줄이 없으면 진다.
         * @details 검증 결과 말고 다른 경고(모르는 이름)는 다른 파일과 똑같이 진다.
         */
        // 검증 결과를 일부러 내는 씬 — 맵 검사 기대 오류, 베이크 시나리오가 구울 아직 베이크하지 않은 라이트 프로브 볼륨.
        static constexpr const utf8* kArrValidationFixture[] = { "engine/automation/editor/mapcheck/mapcheck_expect_error.scene.xml",
                                                                 "engine/automation/editor/lightprobebake/bake_cancel.scene.xml" };

        static bool isValidationFixture( sw::string_view resourceID )
        {
            for ( const utf8* pFixture : kArrValidationFixture )
            {
                if ( resourceID == pFixture )
                    return true;
            }
            return false;
        }

        /** @brief 로그 글에서 검증 결과 줄(`Validation error on` · `Validation warning on`)을 뺍니다. 뺀 줄 수를 @p outRemovedCount 에 둡니다. */
        static sw::string removeValidationLines( const sw::string& joined, uint32& outRemovedCount )
        {
            sw::string result;
            outRemovedCount  = 0;
            size_t lineStart = 0;
            while ( lineStart < joined.size() )
            {
                size_t lineEnd = joined.find( "\n  ", lineStart + 1 );
                if ( lineEnd == sw::string::npos )
                    lineEnd = joined.size();
                const sw::string line        = joined.substr( lineStart, lineEnd - lineStart );
                const bool       bValidation = line.find( "Validation error on" ) != sw::string::npos || line.find( "Validation warning on" ) != sw::string::npos;
                if ( bValidation )
                    ++outRemovedCount;
                else
                    result += line;
                lineStart = lineEnd;
            }
            return result;
        }

        /** @brief 데이터로 보는 확장자입니다. 이 확장자인데 표의 어느 줄에도 맞지 않는 파일은 테스트가 집니다(새 종류가 검사를 비켜 가지 않게). */
        static bool isDataFile( sw::string_view resourceID )
        {
            return endsWith( resourceID, ".xml" ) || endsWith( resourceID, ".json" ) || endsWith( resourceID, ".material" );
        }

        /** @brief 한 파일을 읽는 종류입니다. 엔진 표의 줄이거나 레지스트리에 올라온 카탈로그 종류입니다. */
        struct ResolvedKind
        {
            const utf8* _pLabel{ nullptr };
            bool ( *_pLoad )( const sw::string& resourceID ){ nullptr };
        };

        /**
         * @brief @p resourceID 의 종류를 찾습니다. 엔진 표는 앞의 줄이 먼저 맞고, 그 뒤에 레지스트리를 봅니다.
         * @details 한 파일은 한 종류여야 한다 — 엔진 표와 레지스트리가 같이 맞거나 레지스트리 종류가 둘 이상 맞으면 @p outConflict 에 이유를 적고 false 를 반환합니다.
         *          맞는 종류가 없으면 true 를 반환하며 @p outKind 의 `_pLoad` 가 nullptr 입니다.
         */
        static bool findKind( sw::string_view resourceID, ResolvedKind& outKind, sw::string& outConflict )
        {
            outKind = ResolvedKind{};
            outConflict.clear();
            sw::vector<const sw::ResourceDataKind*> listRegistered;
            sw::engine::getSubsystem<sw::ResourceDataKindRegistry>().collectMatches( resourceID, listRegistered );
            for ( const DataKind& kind : kArrDataKind )
            {
                if ( kind._pIsKind( resourceID ) == false )
                    continue;
                if ( listRegistered.empty() == false )
                {
                    outConflict = sw::string( "엔진 표의 '" ) + kind._pLabel + "' 와 등록된 종류 '" + listRegistered[0]->_pLabel + "' 가 같은 파일에 맞습니다";
                    return false;
                }
                outKind._pLabel = kind._pLabel;
                outKind._pLoad  = kind._pLoad;
                return true;
            }
            if ( listRegistered.size() > 1 )
            {
                outConflict = sw::string( "등록된 종류 '" ) + listRegistered[0]->_pLabel + "' 와 '" + listRegistered[1]->_pLabel + "' 가 같은 파일에 맞습니다";
                return false;
            }
            if ( listRegistered.size() == 1 )
            {
                outKind._pLabel = listRegistered[0]->_pLabel;
                outKind._pLoad  = listRegistered[0]->_pLoad;
            }
            return true;
        }
    };
} // namespace

/**
 * @brief [ResourceDataSchemaTest] Resource/ 아래 데이터 파일은 모두 실제 로더로 읽히고, 모르는 키 · 타입 · 열거자 경고가 하나도 나지 않는다
 * @details 이름은 하나만 쓴다 — 코드의 이름을 바꾸면 데이터를 그 이름으로 다시 쓴다(별칭으로 옛 이름을 남기지 않는다). 텍스트 로더는 모르는 키를
 *          버리고 성공하므로(`SchemaOrphanPolicy::Ignore`) 데이터를 빠뜨려도 다른 테스트는 초록이다. 이 테스트는 그 버린 것을 경고로 잡는다.
 *          데이터 확장자(.xml · .json · .material)인데 종류 표에 없는 파일도 실패다.
 */
SW_TEST_CASE( ResourceDataSchemaTest, EveryResourceDataFileLoadsWithoutUnknownNames )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string& resourceRoot = sw::ResourcePaths::getRootFolderPath();
    SW_ASSERT_FALSE( resourceRoot.empty() );

    sw::vector<sw::string> listFilePath;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( resourceRoot, "", listFilePath, true ) );

    sw::vector<sw::string> listGameTypeName;
    ResourceDataSchemaInternal::collectGameModuleTypeNames( resourceRoot, listGameTypeName );
    sw::vector<sw::string> listGameDataKindSuffix;
    ResourceDataSchemaInternal::collectGameModuleDataKindSuffixes( resourceRoot, listGameDataKindSuffix );

    // 전용 서버 빌드는 서버처럼 읽는다 — 패키지에 없는 종류(텍스처 · 오디오)를 장면이 가리켜도 없는 것으로 친다.
    sw::unique_ptr<test::ScopedHostTarget> pServerHost;
    if ( test::HostTargetTestUtil::isServerOnlyBuild() )
        pServerHost = sw::make_unique<test::ScopedHostTarget>( "Server" );

    uint32 loadedCount{ 0 };
    for ( const sw::string& filePath : listFilePath )
    {
        const sw::string resourceID = sw::ResourcePaths::toResourceID( filePath );
        if ( ResourceDataSchemaInternal::isDataFile( resourceID ) == false )
            continue;
#if defined( SW_SHIPPING )
        // 배포본은 마운트된 팩만 읽는다(낱개 파일 꺼짐) — 이 구성의 입력은 쿠킹이 팩에 넣은 파일이다. 디스크 목록을 폴더 이름(`editor/` · `rigs_raw/`)으로
        // 거르지 않고 팩에 있는가로 가른다. 이름 목록은 쿠킹 제외 목록(`Config/Engine/PackConfig.json`)의 사본이 되어, 데이터 확장자를 가진 원본 폴더가
        // 늘면 어긋난다. 팩에 없는 파일은 Dev 빌드의 이 테스트가 낱개로 읽는다.
        if ( sw::ResourceIO::hasResource( resourceID ) == false )
            continue;
#endif

        ResourceDataSchemaInternal::ResolvedKind kind;
        sw::string                               conflict;
        const bool                               bUnambiguous = ResourceDataSchemaInternal::findKind( resourceID, kind, conflict );
        SW_EXPECT_TRUE_MSG( bUnambiguous, ( "한 파일이 두 종류에 맞습니다: " + resourceID + " - " + conflict ).c_str() );
        // 루트가 게임 모듈 타입인 리플렉션 데이터(`benchscene.xml` 의 `BenchSceneSettings`)는 그 게임 모듈이 데이터 종류로 등록한다 — 이 실행 파일은 게임 모듈을 올리지 않는다.
        // 루트가 리플렉션 타입이 아닌 게임 데이터(`autoplan.xml` 의 원소 목록)도 그 게임 모듈이 종류로 등록했으면 같다.
        const bool bGameModuleData = kind._pLoad == nullptr && ( ResourceDataSchemaInternal::hasGameModuleRoot( resourceID, listGameTypeName ) ||
                                                                 ResourceDataSchemaInternal::hasGameModuleDataKind( resourceID, listGameDataKindSuffix ) );
        if ( bGameModuleData )
            continue;
        SW_EXPECT_TRUE_MSG( kind._pLoad != nullptr || bUnambiguous == false, ( "종류 표에 없는 데이터 파일입니다: " + resourceID ).c_str() );
        if ( bUnambiguous == false || kind._pLoad == nullptr )
            continue;

        test::ScopedLogCollector logs;
        const bool               bLoaded = kind._pLoad( resourceID );
        SW_EXPECT_TRUE_MSG( bLoaded, ( sw::string( kind._pLabel ) + " 를 읽지 못했습니다: " + resourceID + logs.joined() ).c_str() );
        sw::string warnings = ResourceDataSchemaInternal::removeGameModuleTypeWarnings( logs.joined(), listGameTypeName );
        if ( ResourceDataSchemaInternal::isValidationFixture( resourceID ) )
        {
            uint32 validationLineCount{ 0 };
            warnings = ResourceDataSchemaInternal::removeValidationLines( warnings, validationLineCount );
            SW_EXPECT_TRUE_MSG( validationLineCount > 0, ( resourceID + " 는 검증 결과를 내야 하는 테스트 데이터인데 결과가 없습니다" ).c_str() );
        }
        SW_EXPECT_TRUE_MSG( warnings.empty(), ( resourceID + " 를 읽으며 경고가 났습니다:" + warnings ).c_str() );
        ++loadedCount;
    }
    // 장면 · 프리팹 · 파이프라인 · 렌더 패스 · 엔진 데이터가 실제로 훑였는지 — 경로를 못 찾아 0 개면 이 테스트는 아무것도 보지 않은 것이다.
    SW_EXPECT_TRUE_MSG( loadedCount >= 10u, ( "읽은 데이터 파일이 " + std::to_string( loadedCount ) + " 개뿐입니다" ).c_str() );
}

/**
 * @brief [ResourceDataSchemaTest] 컴포넌트 원소의 모르는 속성은 버려지고 이름으로 알린다 — 위 테스트가 기대는 경고 경로
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
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pObject, "<GameObject _name=\"Probe\"><_listComponent><SceneComponent _localScale=\"2, 2, 2\" _noSuchField=\"1\" /></_listComponent></GameObject>", sw::ObjectLoadContext{ sw::SerializePurpose::Authoring } ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "SceneComponent._noSuchField" ) == 1, logs.joined().c_str() );
}
