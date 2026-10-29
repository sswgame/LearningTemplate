// 동작(썸네일 · 열기 · 드롭)이 없는 코어 애셋 종류의 등록 줄입니다. 동작이 있는 종류는 그 동작 파일(`<종류>AssetTypeActions.cpp`)이 등록한다.
// 줄마다 독립된 등록이다 — 순서는 `_order` 가 정하고, 경로가 여러 종류에 맞을 때는 규칙의 우선순위(`EditorAssetMatchPriority`)가 정한다.
// 엔진이 읽는 포맷의 접미사는 적지 않는다 — 그 포맷의 엔진 정의(`X::kAssetType`)를 가리킨다. 엔진이 접미사를 더하거나 바꾸면 에디터 판정도 같이 바뀐다.
// 정의에 없는 접미사만 적는다: 엔진이 읽지 않는 저작 원본(높이장 · 스프라이트 이미지), 카탈로그 확장자, 모듈이 읽는 테이블 접미사다.
// 옛 맨 접미사(`.seq` · `.sprite` · `.dialogue` · `.tilemap`)는 두지 않는다.
#include "pch.h"

#include "Editor/Common/Asset/Texture/HeightfieldImporter.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/LayerWeightsAsset.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/TileMap/TileMapXML.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Style/UIStyleSheet.h"

namespace sw::editor
{
    namespace
    {
        struct BuiltinAssetTypesInternal
        {
            /** @brief 스프라이트 시트로 쓰는 이미지입니다. 텍스처가 먼저 가져가고(`Fallback`), 스프라이트 클립 패널이 시트로 열 때만 이 종류로 본다. */
            static constexpr string_view kArrSpriteImageExt[] = { ".png", ".jpg", ".jpeg", ".dds", ".tga" };
            /** @brief `heightfields_raw/` 의 높이장 원본과 구멍 마스크입니다. 같은 `.png` 라도 텍스처 원본이 아니다. 임포트하는 것이 리로드다. */
            static constexpr string_view kArrHeightfieldRawExt[] = { ".png", ".r16" };
            /** @brief 에디터 테마 토큰 파일입니다. 바뀌면 그 자리에서 테마를 다시 적용한다. */
            static constexpr string_view kArrThemeTokenSuffix[] = { EditorThemeTokens::kExtension };
            /** @brief 모듈이 로드한 데이터 테이블 캐시입니다(GameFramework 의 상호작용 테이블과 원소 규칙 테이블). 에디터는 GameFramework 를 모른다. */
            static constexpr string_view kArrModuleDataSuffix[] = { ".interactions.xml", ".elements.xml" };
            /** @brief 아무 종류도 아닌 카탈로그 데이터입니다. 모든 종류가 먼저 가져간다(`Catchall`). */
            static constexpr string_view kArrDataExt[] = { ".xml", ".json" };

            /** @brief 높이장 원본 규칙입니다. `heightfields_raw/` 아래에서 임포트 결과 경로가 나오는 원본만 높이장이다(`HeightfieldImporter::isRawHeightfieldPath`). */
            static EditorAssetMatchRule makeRawHeightfieldRule()
            {
                EditorAssetMatchRule rule = EditorAssetMatchRule::suffixes( EditorAssetMatchMode::Extension, kArrHeightfieldRawExt, SW_COUNT_OF( kArrHeightfieldRawExt ), EditorAssetMatchPriority::Path,
                                                                            { nullptr, &HeightfieldImporter::importChangedSourceHeightfield, AssetImportThread::Worker } );
                rule._pfnAcceptPath       = &HeightfieldImporter::isRawHeightfieldPath;
                return rule;
            }
        };

        const EditorAssetMatchRule kArrAnimationGraphRule[] = { EditorAssetMatchRule::engineType( AnimationGraphAsset::kAssetType ) };
        const EditorAssetMatchRule kArrDialogueGraphRule[]  = { EditorAssetMatchRule::engineType( DialogueGraphAsset::kAssetType ) };
        const EditorAssetMatchRule kArrSpriteClipRule[]     = {
            EditorAssetMatchRule::engineType( SpriteClipAsset::kAssetType, { "SpriteClip" } ),
            EditorAssetMatchRule::suffixes( EditorAssetMatchMode::Extension, BuiltinAssetTypesInternal::kArrSpriteImageExt, SW_COUNT_OF( BuiltinAssetTypesInternal::kArrSpriteImageExt ),
                                                EditorAssetMatchPriority::Fallback ),
        };
        const EditorAssetMatchRule kArrTileMapRule[]       = { EditorAssetMatchRule::engineType( TileMapXMLData::kAssetType ) };
        const EditorAssetMatchRule kArrSequenceRule[]      = { EditorAssetMatchRule::engineType( SequenceAsset::kAssetType ) };
        const EditorAssetMatchRule kArrSkeletonRule[]      = { EditorAssetMatchRule::engineType( Skeleton::kAssetType, { "Skeleton" } ) };
        const EditorAssetMatchRule kArrAnimationClipRule[] = { EditorAssetMatchRule::engineType( AnimationClip::kAssetType, { "AnimationClip" } ) };
        const EditorAssetMatchRule kArrRigRule[]           = { EditorAssetMatchRule::engineType( RigAsset::kAssetType, { "Rig" } ) };

        const EditorAssetMatchRule kArrHeightfieldRule[] = { BuiltinAssetTypesInternal::makeRawHeightfieldRule(), EditorAssetMatchRule::engineType( HeightfieldAsset::kAssetType ) };
        /** @brief 지형이 읽을 때만 읽는다 — 엔진 캐시가 없어 핫 리로드 대상이 아니다(`TerrainComponent::reloadTerrain`). 지형 브러시가 칠해 저장한다. */
        const EditorAssetMatchRule kArrLayerWeightsRule[] = { EditorAssetMatchRule::engineType( LayerWeightsAsset::kAssetType ) };
        const EditorAssetMatchRule kArrFractureRule[]     = { EditorAssetMatchRule::engineType( FractureAsset::kAssetType, { "Fracture" } ) };
        /** @brief UI 문서다. 그 문서로 연 화면을 다시 만든다(`UISystem`). */
        const EditorAssetMatchRule kArrUIDocumentRule[] = { EditorAssetMatchRule::engineType( UIDocumentAsset::kAssetType, { AssetReloadRoute::kAnyCacheHoldingPath } ) };

        /**
         * @brief 카탈로그 데이터와 전용 종류가 없는 데이터 문서입니다.
         * @details 지역화 문서는 로드한 프로젝트를 다시 읽는다(`LocalizationManager`). UI 스타일 시트는 쓰는 화면을 다시 맞춘다. 믹서 그래프 · 이벤트 라이브러리는
         *          오디오 서비스가 지금 쓰는 파일만 다시 읽는다(엔진 캐시 `AudioData`).
         */
        const EditorAssetMatchRule kArrDataRule[] = {
            EditorAssetMatchRule::suffixes( EditorAssetMatchMode::EndsWith, BuiltinAssetTypesInternal::kArrThemeTokenSuffix, SW_COUNT_OF( BuiltinAssetTypesInternal::kArrThemeTokenSuffix ),
                                            EditorAssetMatchPriority::Suffix, { nullptr, &EditorTheme::importChangedTokenFile } ),
            EditorAssetMatchRule::engineType( SourceStringTable::kAssetType, { "StringTable" } ),
            EditorAssetMatchRule::engineType( TranslationTable::kAssetType, { "StringTable" } ),
            EditorAssetMatchRule::engineType( LocalizationProject::kAssetType, { "StringTable" } ),
            EditorAssetMatchRule::engineType( UIStyleSheetAsset::kAssetType, { AssetReloadRoute::kAnyCacheHoldingPath } ),
            EditorAssetMatchRule::suffixes( EditorAssetMatchMode::EndsWith, BuiltinAssetTypesInternal::kArrModuleDataSuffix, SW_COUNT_OF( BuiltinAssetTypesInternal::kArrModuleDataSuffix ),
                                            EditorAssetMatchPriority::Suffix, { AssetReloadRoute::kAnyCacheHoldingPath } ),
            EditorAssetMatchRule::engineType( AudioMixerDesc::kAssetType, { AssetReloadRoute::kAnyCacheHoldingPath } ),
            EditorAssetMatchRule::engineType( AudioEventLibrary::kAssetType, { AssetReloadRoute::kAnyCacheHoldingPath } ),
            EditorAssetMatchRule::suffixes( EditorAssetMatchMode::Extension, BuiltinAssetTypesInternal::kArrDataExt, SW_COUNT_OF( BuiltinAssetTypesInternal::kArrDataExt ),
                                            EditorAssetMatchPriority::Catchall ),
        };
    } // namespace

    // 필드는 표시 이름, 브라우저 라벨, 패널 제목, 아이콘, 색, 액센트 색, Other 제외, 임포트, 규칙, 규칙 수, 새 에셋 틀, 동작 순서다.
    SW_EDITOR_ASSET_TYPE( AnimationGraph, AnimationGraphAsset::kAssetType._name.data(), 700, "AnimationGraph", "Animation", "Animation Graph", editoricon::kAnimationGraph,
                          { 1.00f, 0.60f, 0.20f, 1.0f }, false, true, true, kArrAnimationGraphRule, SW_COUNT_OF( kArrAnimationGraphRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( DialogueGraph, DialogueGraphAsset::kAssetType._name.data(), 800, "Dialogue", "Dialogue", "Dialogue Graph", editoricon::kDialogue, { 0.40f, 0.75f, 1.00f, 1.0f },
                          false, true, true, kArrDialogueGraphRule, SW_COUNT_OF( kArrDialogueGraphRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( SpriteClip, SpriteClipAsset::kAssetType._name.data(), 900, "SpriteClip", "Sprite", "Sprite Clip", editoricon::kSprite, { 1.00f, 0.60f, 0.20f, 1.0f }, false, true,
                          true, kArrSpriteClipRule, SW_COUNT_OF( kArrSpriteClipRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( TileMap, TileMapXMLData::kAssetType._name.data(), 1000, "TileMap", "Tile Map", "Tile Map Tool", editoricon::kTileMap, { 0.45f, 0.85f, 0.50f, 1.0f }, false, true,
                          true, kArrTileMapRule, SW_COUNT_OF( kArrTileMapRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( Sequence, SequenceAsset::kAssetType._name.data(), 1100, "Sequence", "Seq", "Sequencer", editoricon::kSequence, { 0.85f, 0.55f, 0.85f, 1.0f }, false, true, true,
                          kArrSequenceRule, SW_COUNT_OF( kArrSequenceRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( Skeleton, Skeleton::kAssetType._name.data(), 1300, "Skeleton", "Skeletons", nullptr, editoricon::kSkeleton, { 0.90f, 0.85f, 0.70f, 1.0f }, false, true, true,
                          kArrSkeletonRule, SW_COUNT_OF( kArrSkeletonRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( AnimationClip, AnimationClip::kAssetType._name.data(), 1400, "AnimationClip", "Clips", nullptr, editoricon::kAnimationClip, { 1.00f, 0.70f, 0.35f, 1.0f }, false,
                          true, true, kArrAnimationClipRule, SW_COUNT_OF( kArrAnimationClipRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( Rig, RigAsset::kAssetType._name.data(), 1500, "Rig", "Rigs", nullptr, editoricon::kRig, { 0.95f, 0.75f, 0.55f, 1.0f }, false, true, true, kArrRigRule,
                          SW_COUNT_OF( kArrRigRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( Heightfield, HeightfieldAsset::kAssetType._name.data(), 1600, "Heightfield", "Heightfields", nullptr, editoricon::kHeightfield, { 0.65f, 0.60f, 0.45f, 1.0f },
                          false, true, false, kArrHeightfieldRule, SW_COUNT_OF( kArrHeightfieldRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( LayerWeights, LayerWeightsAsset::kAssetType._name.data(), 1700, "LayerWeights", "Layer Weights", nullptr, editoricon::kPalette, { 0.55f, 0.75f, 0.40f, 1.0f },
                          false, true, false, kArrLayerWeightsRule, SW_COUNT_OF( kArrLayerWeightsRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( Fracture, FractureAsset::kAssetType._name.data(), 1800, "Fracture", "Fractures", nullptr, editoricon::kFracture, { 0.85f, 0.50f, 0.40f, 1.0f }, false, true, true,
                          kArrFractureRule, SW_COUNT_OF( kArrFractureRule ), nullptr, nullptr );
    SW_EDITOR_ASSET_TYPE( UIDocument, UIDocumentAsset::kAssetType._name.data(), 1900, "UIDocument", "UI Documents", "UI Preview", editoricon::kWidget, { 0.95f, 0.55f, 0.75f, 1.0f }, false,
                          true, false, kArrUIDocumentRule, SW_COUNT_OF( kArrUIDocumentRule ), nullptr, nullptr );
    // Data 는 맨 뒤다 — 플러그인 종류는 그 앞(< 9000)에 둔다.
    SW_EDITOR_ASSET_TYPE( Data, EditorAssetTypeIDs::kData, 9000, "Data", "Data", nullptr, editoricon::kTable, { 0.60f, 0.75f, 0.95f, 1.0f }, false, false, false, kArrDataRule,
                          SW_COUNT_OF( kArrDataRule ), nullptr, nullptr );
} // namespace sw::editor
