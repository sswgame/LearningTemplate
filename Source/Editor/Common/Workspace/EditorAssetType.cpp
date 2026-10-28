#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/HeightfieldImporter.h"
#include "Editor/Common/Asset/ModelImporter.h"
#include "Editor/Common/Asset/TextureImporter.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Graph/AnimationBoneMask.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Scene/VertexAnimationCooker.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Asset/AssetFormat.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/LayerWeightsData.h"
#include "Engine/Graphics/Lighting/LightProbeGrid.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/LocalizationBinaryTable.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/TranslationMemory.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/TileMap/TileMapXML.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/VFX/VFXEffectDef.h"

namespace sw::editor
{
    namespace
    {
        enum class MatchMode : uint8
        {
            EndsWith = 0,
            Extension,
            EngineType,     ///< 엔진 에셋 종류 정의(`AssetTypeDef`)의 접미사를 그대로 쓴다. 접미사 철자를 에디터가 따로 적지 않는다
            CookableSource, ///< 엔진의 쿠킹 규칙(`AssetCookPath`)을 따른다. 쿠커가 쿠킹하는 저작 소스만 해당하며, 접미사는 엔진 테이블에서 온다
            RawHeightfield  ///< `heightfields_raw/` 아래의 높이장 원본이다. 같은 `.png` 라도 텍스처 원본이 아니다(`HeightfieldImporter::isRawHeightfieldPath`)
        };

        // 이 테이블이 확장자 → 에디터 종류의 원본이다. 엔진이 읽는 포맷의 철자는 그 포맷의 에셋 종류 정의(`AssetTypeDef`)를 가리키고(문자열을 다시 적지 않는다),
        // 씬 · 프리팹은 쿠킹 규칙(`AssetCookPath`)을, 카탈로그 데이터는 `.xml` · `.json` 을 따른다. 계약은 `EditorAssetTypeTest` 의
        // `EveryEngineFormatMapsToItsKind` · `EveryAssetDataKindIsData` · `EveryEngineAssetTypeIsAccountedFor` 가 지킨다. 옛 맨 접미사(`.seq` · `.sprite` · `.dialogue` · `.tilemap`)는 두지 않는다.
        // 여기 적는 확장자는 이 저장소가 실제로 읽거나 쓰는 것만 둔다. 엔진은 HLSL 전용이므로 `.glsl` 등은 없다. 셰이더는 공유 헤더 `.hlsli` 도 소스다.
        // `.spv` 는 넣지 않는다. 그것은 쿠킹된 산출물이므로(`Content/common/shaders/bin/`) 셰이더
        // 소스로 세면 콘텐츠 브라우저가 빌드 출력 수백 개를 애셋으로 보여 준다.
        //
        // 씬과 프리팹은 여기 적지 않는다. 쿠커가 쿠킹하는 이름이 곧 에디터가 여는 이름이다(`AssetCookPath`). 여기 따로 적으면
        // 쿠커가 쿠킹하지 않는 이름(`_scene.xml`, 아무 `.xml`, 확장자 없는 `.scene` 등)도 씬이나 프리팹으로 열고 저장하게 되어 에디터에서는 되고
        // 배포본에는 없다. 쿠킹된 산출물(`.prefab.bin`)은 `.spv` 처럼 에셋이 아니다(`PrefabAsset::saveToFile` 은 소스만 쓴다).
        //
        // 엔진이 읽는 포맷의 접미사는 에디터가 적지 않는다 — 그 포맷의 에셋 종류 정의(`X::kAssetType`)를 가리킨다(`MatchMode::EngineType`).
        // 엔진이 접미사를 더하거나 바꾸면(예: 소리 형식, JSON 에서 XML 로) 에디터 판정도 같이 바뀐다. 예전에는 에디터가 `.wav` 만 적어 엔진이 읽는 `.ogg` 가 소리로 안 보였다.
        // 정의에 없는 접미사만 아래에 적는다: 엔진이 읽지 않는 저작 원본(이미지 · 셰이더 · 모델), 카탈로그 확장자, 모듈이 읽는 테이블 접미사다.
        constexpr string_view kArrTextureSourceExt[]  = { ".png", ".jpg", ".jpeg", ".tga", ".hdr", ".bmp" };
        constexpr string_view kArrShaderExt[]         = { ".hlsl", ".hlsli" };
        constexpr string_view kArrDataExt[]           = { ".xml", ".json" };
        constexpr string_view kArrSpriteImageExt[]    = { ".png", ".jpg", ".jpeg", ".dds", ".tga" };
        constexpr string_view kArrThemeTokenSuffix[]  = { EditorThemeTokens::kExtension };
        constexpr string_view kArrModelSourceExt[]    = { ".glb", ".gltf", ".vrm" };              ///< `models_raw/` 의 원본이다. 임포트하는 것이 리로드다
        constexpr string_view kArrHeightfieldRawExt[] = { ".png", ".r16" };                       ///< `heightfields_raw/` 의 원본과 구멍 마스크다. 임포트하는 것이 리로드다
        constexpr string_view kArrModuleDataSuffix[]  = { ".interactions.xml", ".elements.xml" }; ///< 모듈이 로드한 데이터 테이블 캐시다(GameFramework 의 상호작용 테이블과 원소 규칙 테이블)

        /**
         * @brief 에디터가 종류 표에 올리지 않는 엔진 에셋 종류의 이름입니다(`X::kAssetType._name` 으로 적어 종류 이름이 바뀌면 컴파일이 깨진다).
         * @details 엔진에 에셋 종류가 새로 등록되면 규칙 표(`kArrAssetMatch`)에 올리거나 여기에 적어야 한다 — 둘 다 없으면 `EditorAssetTypeTest.EveryEngineAssetTypeIsAccountedFor` 가 막는다.
         *          여기 있는 종류는 두 가지다: ① 저작 문서지만 전용 에디터 종류가 없어 `.xml` · `.json` 규칙(Data)으로 보이거나 전용 창이 직접 연다(`VFX` · 페어드 애니메이션 · 얼굴 · 리타깃 등),
         *          ② 쿠킹 산출물 · 에디터가 열지 않는 바이너리(셰이더 바이너리 · 내비 메시 · 라이트 프로브 · 지역화 바이너리)다.
         *          이름을 쓰는 것은 정의의 주소가 상수식이 아니라서다(정의는 엔진 DLL 에서 오는 `static constexpr` 멤버라 주소를 constexpr 표에 담을 수 없다).
         */
        constexpr string_view kArrUnlistedType[] = {
            AnimationBoneMaskDef::kAssetType._name,
            FacialRig::kAssetType._name,
            VisemeTrack::kAssetType._name,
            RetargetProfile::kAssetType._name,
            SkeletonBoneLOD::kAssetType._name,
            PairedAnimationDef::kAssetType._name,
            VFXEffectDef::kAssetType._name,
            MeshVertexAnimation::kAssetType._name,
            CultureTable::kAssetType._name,
            TranslationMemory::kAssetType._name,
            MaterialInstance::kAssetType._name,
            VertexAnimationCookList::kAssetType._name,
            LightProbeGrid::kAssetType._name,
            NavMeshAsset::kAssetType._name,
            ShaderCooker::kAssetType._name,
            LocalizationBinaryTable::kAssetType._name,
        };

        /**
         * @struct AssetMatchRow
         * @brief 경로 판정 규칙 하나입니다. 어느 종류인지와 핫 리로드가 그 파일을 어떻게 다시 읽는지를 담습니다.
         * @details 한 종류가 규칙을 둘 이상 가질 수 있다. 매칭 방식이 다를 때다. SpriteClip 은 문서 접미사와 이미지 확장자를 함께 쓴다.
         *          종류를 보여 주는 정보(이름, 아이콘, 색, 패널, 필터)는 종류 테이블(`kArrKindInfo`)에 종류마다 항목 하나로 둔다.
         */
        struct AssetMatchRow
        {
            EditorAssetType         _kind;
            MatchMode               _mode;
            const string_view*      _pSuffix; ///< `EndsWith` · `Extension` 의 접미사. 그 밖의 방식은 nullptr
            uint32                  _suffixCount;
            string_view             _engineTypeName;                            ///< `EngineType` 의 엔진 에셋 종류 이름(`X::kAssetType._name`). 그 밖의 방식은 빈 글. 접미사는 이 종류의 정의에서 온다
            const utf8*             _pCacheKindName;                            ///< 이 규칙의 파일을 가진 엔진 캐시(`IAssetCache::getAssetKindName`). nullptr 이면 핫 리로드 대상이 아니다
            AssetSourceImporterFunc _pfnImportSource;                           ///< 캐시가 읽기 전에 소스를 임포트하는 임포터. nullptr 이면 바로 캐시가 다시 읽는다
            AssetImportThread       _importThread{ AssetImportThread::Editor }; ///< 임포터가 도는 스레드. 긴 임포트만 `Worker` 로 적는다
        };

        // 규칙 순서가 곧 판정 우선순위다. 처음 맞는 규칙이 이긴다(`findKind`, `findReloadRoute`).
        constexpr AssetMatchRow kArrAssetMatch[] = {
            { EditorAssetType::Scene, MatchMode::CookableSource, nullptr, 0, {}, nullptr, nullptr },
            { EditorAssetType::Prefab, MatchMode::CookableSource, nullptr, 0, {}, "Prefab", nullptr },
            { EditorAssetType::Heightfield, MatchMode::RawHeightfield, kArrHeightfieldRawExt, SW_COUNT_OF( kArrHeightfieldRawExt ), {}, nullptr, &HeightfieldImporter::importChangedSourceHeightfield, AssetImportThread::Worker },
            { EditorAssetType::Texture, MatchMode::Extension, kArrTextureSourceExt, SW_COUNT_OF( kArrTextureSourceExt ), {}, "Texture", &TextureImporter::importChangedSourceImage, AssetImportThread::Worker },
            { EditorAssetType::Texture, MatchMode::EngineType, nullptr, 0, Texture2D::kAssetType._name, "Texture", nullptr },
            { EditorAssetType::Shader, MatchMode::Extension, kArrShaderExt, SW_COUNT_OF( kArrShaderExt ), {}, nullptr, nullptr },
            { EditorAssetType::Material, MatchMode::EngineType, nullptr, 0, Material::kAssetType._name, "Material", nullptr },
            { EditorAssetType::Audio, MatchMode::EngineType, nullptr, 0, AudioClipData::kAssetType._name, nullptr, nullptr },
            { EditorAssetType::Audio, MatchMode::EngineType, nullptr, 0, AudioGraphDef::kAssetType._name, AssetReloadRoute::kAnyCacheHoldingPath, nullptr }, // 소리 그래프다. 클립 캐시가 읽어 둔 것을 오디오 서비스가 다시 굳힌다(엔진 캐시 `AudioData`)
            { EditorAssetType::AnimationGraph, MatchMode::EngineType, nullptr, 0, AnimationGraphAsset::kAssetType._name, nullptr, nullptr },
            { EditorAssetType::DialogueGraph, MatchMode::EngineType, nullptr, 0, DialogueGraphAsset::kAssetType._name, nullptr, nullptr },
            { EditorAssetType::SpriteClip, MatchMode::EngineType, nullptr, 0, SpriteClipAsset::kAssetType._name, "SpriteClip", nullptr },
            { EditorAssetType::SpriteClip, MatchMode::Extension, kArrSpriteImageExt, SW_COUNT_OF( kArrSpriteImageExt ), {}, nullptr, nullptr },
            { EditorAssetType::TileMap, MatchMode::EngineType, nullptr, 0, TileMapXMLData::kAssetType._name, nullptr, nullptr },
            { EditorAssetType::Sequence, MatchMode::EngineType, nullptr, 0, SequenceAsset::kAssetType._name, nullptr, nullptr },
            { EditorAssetType::Mesh, MatchMode::EngineType, nullptr, 0, MeshAssetFormat::kAssetType._name, "Mesh", nullptr },
            { EditorAssetType::Mesh, MatchMode::Extension, kArrModelSourceExt, SW_COUNT_OF( kArrModelSourceExt ), {}, "Mesh", &ModelImporter::importChangedSourceModel, AssetImportThread::Worker },
            { EditorAssetType::Skeleton, MatchMode::EngineType, nullptr, 0, Skeleton::kAssetType._name, "Skeleton", nullptr },
            { EditorAssetType::Rig, MatchMode::EngineType, nullptr, 0, RigAsset::kAssetType._name, "Rig", nullptr },
            { EditorAssetType::AnimationClip, MatchMode::EngineType, nullptr, 0, AnimationClip::kAssetType._name, "AnimationClip", nullptr },
            { EditorAssetType::Fracture, MatchMode::EngineType, nullptr, 0, FractureAsset::kAssetType._name, "Fracture", nullptr },
            { EditorAssetType::Heightfield, MatchMode::EngineType, nullptr, 0, HeightfieldData::kAssetType._name, nullptr, nullptr },
            { EditorAssetType::LayerWeights, MatchMode::EngineType, nullptr, 0, LayerWeightsData::kAssetType._name, nullptr, nullptr }, // 지형이 읽을 때만 읽는다 — 엔진 캐시가 없어 핫 리로드 대상이 아니다(`TerrainComponent::reloadTerrain`)
            { EditorAssetType::Data, MatchMode::EndsWith, kArrThemeTokenSuffix, SW_COUNT_OF( kArrThemeTokenSuffix ), {}, nullptr, &EditorTheme::importChangedTokenFile },
            { EditorAssetType::Data, MatchMode::EngineType, nullptr, 0, SourceStringTable::kAssetType._name, "StringTable", nullptr }, // 로드한 프로젝트를 다시 읽는다(`LocalizationManager`)
            { EditorAssetType::Data, MatchMode::EngineType, nullptr, 0, TranslationTable::kAssetType._name, "StringTable", nullptr },
            { EditorAssetType::Data, MatchMode::EngineType, nullptr, 0, LocalizationProject::kAssetType._name, "StringTable", nullptr },
            { EditorAssetType::UIDocument, MatchMode::EngineType, nullptr, 0, UIDocumentAsset::kAssetType._name, AssetReloadRoute::kAnyCacheHoldingPath, nullptr }, // UI 문서다. 그 문서로 연 화면을 다시 만든다(`UISystem`)
            { EditorAssetType::Data, MatchMode::EngineType, nullptr, 0, UIStyleSheetAsset::kAssetType._name, AssetReloadRoute::kAnyCacheHoldingPath, nullptr }, // UI 스타일 시트다. 쓰는 화면을 다시 맞춘다
            { EditorAssetType::Data, MatchMode::EndsWith, kArrModuleDataSuffix, SW_COUNT_OF( kArrModuleDataSuffix ), {}, AssetReloadRoute::kAnyCacheHoldingPath, nullptr },
            { EditorAssetType::Data, MatchMode::EngineType, nullptr, 0, AudioMixerDesc::kAssetType._name, AssetReloadRoute::kAnyCacheHoldingPath, nullptr }, // 오디오 서비스가 쓰는 믹서 그래프 · 이벤트 라이브러리다(엔진 캐시 `AudioData` 가 지금 쓰는 파일만 다시 읽는다)
            { EditorAssetType::Data, MatchMode::EngineType, nullptr, 0, AudioEventLibrary::kAssetType._name, AssetReloadRoute::kAnyCacheHoldingPath, nullptr },
            { EditorAssetType::Data, MatchMode::Extension, kArrDataExt, SW_COUNT_OF( kArrDataExt ), {}, nullptr, nullptr },
        };

        // 종류마다 항목 하나다. 항목 순서가 곧 브라우저 필터, 도구 패널, 리소스 카탈로그의 표시 순서다.
        // 필드는 종류, 이름(단수), 브라우저 라벨, 패널 제목, 아이콘, 색, 액센트 색, Other 제외, 임포트 순서다
        constexpr EditorAssetTypeInfo kArrKindInfo[] = {
            {         EditorAssetType::Scene,          "Scene",        "Scenes",           nullptr,          editoricon::kScene,                       Color{},  true,  true, false},
            {        EditorAssetType::Prefab,         "Prefab",       "Prefabs",   "Prefab Window",         editoricon::kPrefab, { 0.35f, 0.70f, 1.00f, 1.0f }, false,  true,  true},
            {       EditorAssetType::Texture,        "Texture",      "Textures",           nullptr,        editoricon::kTexture, { 0.35f, 0.85f, 0.45f, 1.0f }, false,  true,  true},
            {        EditorAssetType::Shader,         "Shader",       "Shaders",           nullptr,         editoricon::kShader, { 0.95f, 0.45f, 0.35f, 1.0f }, false,  true,  true},
            {      EditorAssetType::Material,       "Material",     "Materials",        "Material",       editoricon::kMaterial, { 0.80f, 0.45f, 0.95f, 1.0f }, false,  true,  true},
            {         EditorAssetType::Audio,          "Audio",         "Audio",           nullptr,          editoricon::kAudio, { 0.95f, 0.85f, 0.25f, 1.0f }, false,  true,  true},
            {EditorAssetType::AnimationGraph, "AnimationGraph",     "Animation", "Animation Graph", editoricon::kAnimationGraph, { 1.00f, 0.60f, 0.20f, 1.0f }, false,  true,  true},
            { EditorAssetType::DialogueGraph,       "Dialogue",      "Dialogue",  "Dialogue Graph",       editoricon::kDialogue, { 0.40f, 0.75f, 1.00f, 1.0f }, false,  true,  true},
            {    EditorAssetType::SpriteClip,     "SpriteClip",        "Sprite",     "Sprite Clip",         editoricon::kSprite, { 1.00f, 0.60f, 0.20f, 1.0f }, false,  true,  true},
            {       EditorAssetType::TileMap,        "TileMap",      "Tile Map",   "Tile Map Tool",        editoricon::kTileMap, { 0.45f, 0.85f, 0.50f, 1.0f }, false,  true,  true},
            {      EditorAssetType::Sequence,       "Sequence",           "Seq",       "Sequencer",       editoricon::kSequence, { 0.85f, 0.55f, 0.85f, 1.0f }, false,  true,  true},
            {          EditorAssetType::Mesh,           "Mesh",        "Meshes",           nullptr,           editoricon::kCube, { 0.55f, 0.80f, 0.80f, 1.0f }, false,  true,  true},
            {      EditorAssetType::Skeleton,       "Skeleton",     "Skeletons",           nullptr,       editoricon::kSkeleton, { 0.90f, 0.85f, 0.70f, 1.0f }, false,  true,  true},
            { EditorAssetType::AnimationClip,  "AnimationClip",         "Clips",           nullptr,  editoricon::kAnimationClip, { 1.00f, 0.70f, 0.35f, 1.0f }, false,  true,  true},
            {           EditorAssetType::Rig,            "Rig",          "Rigs",           nullptr,            editoricon::kRig, { 0.95f, 0.75f, 0.55f, 1.0f }, false,  true,  true},
            {   EditorAssetType::Heightfield,    "Heightfield",  "Heightfields",           nullptr,    editoricon::kHeightfield, { 0.65f, 0.60f, 0.45f, 1.0f }, false,  true, false},
            {  EditorAssetType::LayerWeights,   "LayerWeights", "Layer Weights",           nullptr,        editoricon::kPalette, { 0.55f, 0.75f, 0.40f, 1.0f }, false,  true, false},
            {      EditorAssetType::Fracture,       "Fracture",     "Fractures",           nullptr,       editoricon::kFracture, { 0.85f, 0.50f, 0.40f, 1.0f }, false,  true,  true},
            {    EditorAssetType::UIDocument,     "UIDocument",  "UI Documents",      "UI Preview",         editoricon::kWidget, { 0.95f, 0.55f, 0.75f, 1.0f }, false,  true, false},
            {          EditorAssetType::Data,           "Data",          "Data",           nullptr,          editoricon::kTable, { 0.60f, 0.75f, 0.95f, 1.0f }, false, false, false},
        };

        /** @brief 종류 테이블이 `Unknown` 을 뺀 모든 종류를 한 번씩 담고, 이름, 라벨, 아이콘 필드가 비어 있지 않은지 검사합니다. */
        constexpr bool isKindTableComplete()
        {
            for ( uint32 kindValue = 1; kindValue < static_cast<uint32>( EditorAssetType::Count ); ++kindValue )
            {
                uint32 rowCount{ 0 };
                for ( const EditorAssetTypeInfo& info : kArrKindInfo )
                {
                    if ( static_cast<uint32>( info._kind ) != kindValue )
                        continue;
                    ++rowCount;
                    const bool bMissingText = info._pDisplayName == nullptr || info._pBrowserLabel == nullptr || info._pIcon == nullptr;
                    if ( bMissingText )
                        return false;
                }
                if ( rowCount != 1 )
                    return false;
            }
            return true;
        }

        /** @brief 모든 종류가 판정 규칙을 하나 이상 가졌는지 검사합니다. 규칙이 없는 종류는 어떤 경로에도 해당하지 않습니다. */
        constexpr bool isEveryKindMatchable()
        {
            for ( uint32 kindValue = 1; kindValue < static_cast<uint32>( EditorAssetType::Count ); ++kindValue )
            {
                bool bFound{ false };
                for ( const AssetMatchRow& row : kArrAssetMatch )
                {
                    if ( static_cast<uint32>( row._kind ) == kindValue )
                        bFound = true;
                }
                if ( bFound == false )
                    return false;
            }
            return true;
        }

        static_assert( sizeof( kArrKindInfo ) / sizeof( kArrKindInfo[0] ) + 1 == static_cast<size_t>( EditorAssetType::Count ),
                       "kArrKindInfo needs exactly one row per EditorAssetType" );
        static_assert( isKindTableComplete(), "every EditorAssetType needs one kArrKindInfo row with a name, a browser label and an icon" );
        static_assert( isEveryKindMatchable(), "every EditorAssetType needs at least one kArrAssetMatch row" );

        struct EditorAssetTypeInternal
        {
            static bool matchSuffixList( string_view path, const string_view* pSuffix, uint32 count, MatchMode mode )
            {
                if ( pSuffix == nullptr || count == 0 )
                    return false;
                if ( mode == MatchMode::EndsWith )
                {
                    for ( uint32 index = 0; index < count; ++index )
                    {
                        if ( StringUtil::endsWith( path, pSuffix[index], true ) )
                            return true;
                    }
                    return false;
                }

                for ( uint32 index = 0; index < count; ++index )
                {
                    if ( FileUtil::hasExtension( path, pSuffix[index] ) )
                        return true;
                }
                return false;
            }

            /** @brief 엔진 쿠킹 규칙의 종류(정의)를 반환합니다. 씬이나 프리팹이 아니면 nullptr 입니다. */
            static const AssetTypeDef* findCookType( EditorAssetType kind )
            {
                if ( kind == EditorAssetType::Scene )
                    return &SceneDocument::kAssetType;
                if ( kind == EditorAssetType::Prefab )
                    return &PrefabAsset::kAssetType;
                return nullptr;
            }

            static bool matchRow( const AssetMatchRow& row, string_view path )
            {
                if ( row._mode == MatchMode::CookableSource )
                {
                    const AssetTypeDef* pCookType = findCookType( row._kind );
                    return pCookType != nullptr && AssetCookPath::isCookableSource( path, *pCookType );
                }
                if ( row._mode == MatchMode::RawHeightfield )
                    return HeightfieldImporter::isRawHeightfieldPath( path );
                if ( row._mode == MatchMode::EngineType )
                {
                    const AssetTypeDef* pType = findRowType( row );
                    if ( pType == nullptr )
                        return false;
                    for ( uint32 suffixIndex = 0; suffixIndex < pType->_suffixCount; ++suffixIndex )
                    {
                        if ( isOpenableRole( pType->_pSuffix[suffixIndex]._role ) && StringUtil::endsWith( path, pType->_pSuffix[suffixIndex]._text, true ) )
                            return true;
                    }
                    return false;
                }
                return matchSuffixList( path, row._pSuffix, row._suffixCount, row._mode );
            }

            /**
             * @brief `EngineType` 규칙의 엔진 에셋 종류 정의를 반환합니다. 규칙이 그 방식이 아니거나 엔진에 등록되지 않은 종류면 nullptr 입니다(테스트가 모든 규칙이 찾아지는지 본다).
             * @details 정의는 이름으로 찾아 처음 쓸 때 한 번 모은다(규칙 표와 같은 순서) — 이름마다 엔진 표를 훑는 값을 경로마다 치르지 않는다.
             */
            static const AssetTypeDef* findRowType( const AssetMatchRow& row )
            {
                static const vector<const AssetTypeDef*> s_listRowType = []
                {
                    vector<const AssetTypeDef*> listType{};
                    for ( const AssetMatchRow& each : kArrAssetMatch )
                    {
                        listType.push_back( each._mode == MatchMode::EngineType ? AssetTypeRegistry::findType( each._engineTypeName ) : nullptr );
                    }
                    return listType;
                }();
                return s_listRowType[static_cast<size_t>( &row - kArrAssetMatch )];
            }

            /** @brief 에디터가 여는 접미사의 역할인지 반환합니다. 쿠킹본 이름(`Cooked` · `CookedAlias`)은 에셋으로 열지 않습니다. */
            static bool isOpenableRole( AssetSuffixRole role ) { return role == AssetSuffixRole::Plain || role == AssetSuffixRole::CookSource; }

            /** @brief 규칙 하나의 접미사를 @p outListSuffix 에 추가합니다. 쿠킹 규칙은 엔진 테이블에서 가져옵니다(정적 문자열). */
            static void appendRowSuffixes( const AssetMatchRow& row, vector<string_view>& outListSuffix )
            {
                if ( row._mode == MatchMode::CookableSource )
                {
                    const AssetTypeDef* pCookType = findCookType( row._kind );
                    if ( pCookType != nullptr )
                        AssetCookPath::appendSourceSuffixes( *pCookType, outListSuffix );
                    return;
                }
                if ( row._mode == MatchMode::EngineType )
                {
                    const AssetTypeDef* pType = findRowType( row );
                    if ( pType == nullptr )
                        return;
                    for ( uint32 suffixIndex = 0; suffixIndex < pType->_suffixCount; ++suffixIndex )
                    {
                        if ( isOpenableRole( pType->_pSuffix[suffixIndex]._role ) )
                            outListSuffix.push_back( pType->_pSuffix[suffixIndex]._text );
                    }
                    return;
                }
                if ( row._pSuffix == nullptr )
                    return;
                for ( uint32 index = 0; index < row._suffixCount; ++index )
                {
                    outListSuffix.push_back( row._pSuffix[index] );
                }
            }

            /** @brief 전용 패널이 있는 종류의 접미사를 펼쳐 패널 매핑을 만듭니다. */
            static vector<EditorAssetPanelMapping> buildPanelMappings()
            {
                vector<EditorAssetPanelMapping> listMapping{};
                vector<string_view>             listSuffix{};
                for ( const AssetMatchRow& row : kArrAssetMatch )
                {
                    if ( getPanelTitleOf( row._kind ) == nullptr )
                        continue;
                    listSuffix.clear();
                    appendRowSuffixes( row, listSuffix );
                    for ( const string_view suffix : listSuffix )
                    {
                        listMapping.push_back( EditorAssetPanelMapping{ row._kind, suffix } );
                    }
                }
                return listMapping;
            }

            /** @brief 전용 패널이 있는 종류를 종류 테이블 순서대로 모읍니다. */
            static vector<EditorAssetType> buildToolPanelKinds()
            {
                vector<EditorAssetType> listKind{};
                for ( const EditorAssetTypeInfo& info : kArrKindInfo )
                {
                    if ( info._pPanelTitle != nullptr )
                        listKind.push_back( info._kind );
                }
                return listKind;
            }

            /** @brief 라벨이 있는 종류로 브라우저 필터를 만듭니다. 앞뒤의 All 과 Other 는 종류가 아닙니다. */
            static vector<EditorAssetBrowserFilter> buildBrowserFilters()
            {
                vector<EditorAssetBrowserFilter> listFilter{};
                listFilter.push_back( EditorAssetBrowserFilter{ "All", EditorAssetType::Unknown, false } );
                for ( const EditorAssetTypeInfo& info : kArrKindInfo )
                {
                    listFilter.push_back( EditorAssetBrowserFilter{ info._pBrowserLabel, info._kind, false } );
                }
                listFilter.push_back( EditorAssetBrowserFilter{ "Other", EditorAssetType::Unknown, true } );
                return listFilter;
            }

            /** @brief 종류의 패널 제목을 반환합니다. 없으면 nullptr 이며, `getPanelTitle` 의 빈 문자열과 구분됩니다. */
            static const utf8* getPanelTitleOf( EditorAssetType kind )
            {
                const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
                return pInfo != nullptr ? pInfo->_pPanelTitle : nullptr;
            }

            /** @brief 종류가 브라우저 "Other" 필터에서 빠지면 true 입니다. */
            static bool isOtherExcluded( EditorAssetType kind )
            {
                const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
                return pInfo != nullptr && pInfo->_bOtherExcluded;
            }

            /** @brief 종류의 접미사가 임포트 대화상자에 들어가면 true 입니다. */
            static bool isImportable( EditorAssetType kind )
            {
                const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
                return pInfo != nullptr && pInfo->_bImportable;
            }

            static bool containsSuffix( const vector<string>& listSuffix, string_view suffix )
            {
                return VectorUtil::containsIf( listSuffix, [&]( const string& existing )
                { return StringUtil::equals( existing, suffix, true ); } );
            }

            /** @brief 규칙 하나의 접미사를 중복 없이 @p outListSuffix 에 추가합니다. */
            static void appendUniqueRowSuffixes( const AssetMatchRow& row, vector<string>& outListSuffix )
            {
                vector<string_view> listRowSuffix{};
                appendRowSuffixes( row, listRowSuffix );
                for ( const string_view suffix : listRowSuffix )
                {
                    if ( containsSuffix( outListSuffix, suffix ) )
                        continue;
                    outListSuffix.push_back( string{ suffix } );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorAssetTypeRegistry::matches( EditorAssetType kind, string_view path )
    {
        if ( kind == EditorAssetType::Unknown || path.empty() )
            return false;

        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( row._kind != kind )
                continue;
            if ( EditorAssetTypeInternal::matchRow( row, path ) )
                return true;
        }
        return false;
    }

    bool EditorAssetTypeRegistry::matches( EditorAssetType kind, const utf8* pPath )
    {
        if ( pPath == nullptr )
            return false;
        return matches( kind, string_view{ pPath } );
    }

    bool EditorAssetTypeRegistry::matchesAny( string_view path )
    {
        if ( path.empty() )
            return false;
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( EditorAssetTypeInternal::matchRow( row, path ) )
                return true;
        }
        return false;
    }

    bool EditorAssetTypeRegistry::matchesOther( string_view path )
    {
        if ( path.empty() )
            return false;
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( EditorAssetTypeInternal::isOtherExcluded( row._kind ) && EditorAssetTypeInternal::matchRow( row, path ) )
                return false;
        }
        return true;
    }

    EditorAssetType EditorAssetTypeRegistry::findKind( string_view path )
    {
        if ( path.empty() )
            return EditorAssetType::Unknown;
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( EditorAssetTypeInternal::matchRow( row, path ) )
                return row._kind;
        }
        return EditorAssetType::Unknown;
    }

    EditorAssetType EditorAssetTypeRegistry::findKindOfEngineType( string_view typeName )
    {
        if ( typeName == SceneDocument::kAssetType._name )
            return EditorAssetType::Scene;
        if ( typeName == PrefabAsset::kAssetType._name )
            return EditorAssetType::Prefab;
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( row._mode == MatchMode::EngineType && row._engineTypeName == typeName )
                return row._kind;
        }
        return EditorAssetType::Unknown;
    }

    bool EditorAssetTypeRegistry::isEngineTypeAccounted( string_view typeName )
    {
        if ( findKindOfEngineType( typeName ) != EditorAssetType::Unknown )
            return true;
        for ( const string_view unlistedName : kArrUnlistedType )
        {
            if ( unlistedName == typeName )
                return true;
        }
        return false;
    }

    bool EditorAssetTypeRegistry::areEngineTypeRowsResolved()
    {
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( row._mode == MatchMode::EngineType && EditorAssetTypeInternal::findRowType( row ) == nullptr )
                return false;
        }
        return true;
    }

    const EditorAssetTypeInfo* EditorAssetTypeRegistry::findKindInfo( EditorAssetType kind )
    {
        for ( const EditorAssetTypeInfo& info : kArrKindInfo )
        {
            if ( info._kind == kind )
                return &info;
        }
        return nullptr;
    }

    const EditorAssetTypeInfo* EditorAssetTypeRegistry::getKindInfos( uint32& outCount )
    {
        outCount = static_cast<uint32>( sizeof( kArrKindInfo ) / sizeof( kArrKindInfo[0] ) );
        return kArrKindInfo;
    }

    const utf8* EditorAssetTypeRegistry::getPanelTitle( EditorAssetType kind )
    {
        const utf8* pTitle = EditorAssetTypeInternal::getPanelTitleOf( kind );
        return pTitle != nullptr ? pTitle : "";
    }

    EditorAssetType EditorAssetTypeRegistry::findPanelKindForPath( string_view assetPath )
    {
        if ( assetPath.empty() )
            return EditorAssetType::Unknown;

        // 가장 긴 접미사가 이긴다. `.animgraph.xml` 이 `.json` 보다 구체적이다.
        size_t                               bestLen{ 0 };
        EditorAssetType                      bestKind{ EditorAssetType::Unknown };
        uint32                               mappingCount{ 0 };
        const EditorAssetPanelMapping* const pMapping = getPanelMappings( mappingCount );
        for ( uint32 index = 0; index < mappingCount; ++index )
        {
            const EditorAssetPanelMapping& mapping = pMapping[index];
            if ( StringUtil::endsWith( assetPath, mapping._suffix, true ) == false )
                continue;
            if ( mapping._suffix.size() <= bestLen )
                continue;
            bestLen  = mapping._suffix.size();
            bestKind = mapping._kind;
        }
        return bestKind;
    }

    const EditorAssetPanelMapping* EditorAssetTypeRegistry::getPanelMappings( uint32& outCount )
    {
        // 전용 패널이 있는 종류의 접미사를 그대로 펼친다. 접미사를 두 번 적지 않기 위해서다.
        static const vector<EditorAssetPanelMapping> s_listMapping = EditorAssetTypeInternal::buildPanelMappings();
        outCount                                                   = static_cast<uint32>( s_listMapping.size() );
        return s_listMapping.data();
    }

    const EditorAssetType* EditorAssetTypeRegistry::getToolPanelKinds( uint32& outCount )
    {
        static const vector<EditorAssetType> s_listKind = EditorAssetTypeInternal::buildToolPanelKinds();
        outCount                                        = static_cast<uint32>( s_listKind.size() );
        return s_listKind.data();
    }

    const EditorAssetBrowserFilter* EditorAssetTypeRegistry::getBrowserFilters( uint32& outCount )
    {
        static const vector<EditorAssetBrowserFilter> s_listFilter = EditorAssetTypeInternal::buildBrowserFilters();
        outCount                                                   = static_cast<uint32>( s_listFilter.size() );
        return s_listFilter.data();
    }

    AssetReloadRoute EditorAssetTypeRegistry::findReloadRoute( string_view path )
    {
        AssetReloadRoute route{};
        if ( path.empty() )
            return route;
        // 테이블 순서대로 첫 일치 규칙이 이긴다. 이미지는 SpriteClip 이미지 규칙보다 앞선 Texture 규칙으로 간다.
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( EditorAssetTypeInternal::matchRow( row, path ) == false )
                continue;
            route._pCacheKindName  = row._pCacheKindName;
            route._pfnImportSource = row._pfnImportSource;
            route._importThread    = row._importThread;
            return route;
        }
        return route;
    }

    void EditorAssetTypeRegistry::appendReloadCacheKindNames( vector<string_view>& outListKindName )
    {
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( row._pCacheKindName != nullptr && row._pCacheKindName[0] != '\0' )
                outListKindName.push_back( row._pCacheKindName );
        }
    }

    void EditorAssetTypeRegistry::appendReloadableSuffixes( vector<string>& outListSuffix )
    {
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( row._pCacheKindName != nullptr || row._pfnImportSource != nullptr )
                EditorAssetTypeInternal::appendUniqueRowSuffixes( row, outListSuffix );
        }
    }

    void EditorAssetTypeRegistry::appendSuffixes( EditorAssetType kind, vector<string>& outListSuffix )
    {
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( row._kind == kind )
                EditorAssetTypeInternal::appendUniqueRowSuffixes( row, outListSuffix );
        }
    }

    bool EditorAssetTypeRegistry::collectFiles( EditorAssetType kind, string_view directory, vector<string>& outListFilePath )
    {
        vector<string> listFile{};
        if ( FileUtil::collectFiles( directory, "", listFile, true ) == false )
            return false;
        for ( string& filePath : listFile )
        {
            if ( matches( kind, string_view{ filePath } ) )
                outListFilePath.push_back( std::move( filePath ) );
        }
        return true;
    }

    void EditorAssetTypeRegistry::appendImportExtensions( vector<string>& outListExtension )
    {
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( EditorAssetTypeInternal::isImportable( row._kind ) == false )
                continue;
            EditorAssetTypeInternal::appendUniqueRowSuffixes( row, outListExtension );
        }
        if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".json" ) == false )
            outListExtension.push_back( ".json" );
        if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".txt" ) == false )
            outListExtension.push_back( ".txt" );
    }
} // namespace sw::editor
