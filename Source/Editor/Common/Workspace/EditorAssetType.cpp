#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/HeightfieldImporter.h"
#include "Editor/Common/Asset/ModelImporter.h"
#include "Editor/Common/Asset/TextureImporter.h"
#include "Editor/Common/Gui/EditorIconGlyphs.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/UI/Document/UiDocument.h"
#include "Engine/UI/Style/UiStyleSheet.h"

namespace sw::editor
{
    namespace
    {
        enum class MatchMode : uint8
        {
            EndsWith = 0,
            Extension,
            CookableSource, ///< 엔진의 쿠킹 규칙(`AssetCookPath`) — 쿠커가 쿠킹하는 저작 소스만. 접미사는 엔진 표에서 온다
            RawHeightfield  ///< `heightfields_raw/` 아래의 높이장 원본 — 같은 `.png` 라도 텍스처 원본이 아니다(`HeightfieldImporter::isRawHeightfieldPath`)
        };

        // 여기 적힌 확장자는 **이 저장소가 실제로 읽거나 쓰는 것만** 둔다 — 엔진은 HLSL 전용이고(`.glsl` 등 없음),
        // 오디오는 XAudio2 가 읽는 `.wav` 뿐이다. 셰이더는 공유 헤더 `.hlsli` 도 소스다.
        // `.spv` 는 넣지 않는다. 그것은 **쿠킹된 산출물**이라(`Resource/common/shaders/bin/`) 셰이더
        // 소스로 세면 콘텐츠 브라우저가 빌드 출력 수백 개를 애셋으로 보여 준다.
        //
        // 씬 · 프리팹은 여기 적지 않는다 — 쿠커가 쿠킹하는 이름이 곧 에디터가 여는 이름이다(`AssetCookPath`). 여기 따로 적으면
        // 쿠커가 쿠킹하지 않는 이름(`_scene.xml` · 아무 `.xml` · 확장자 없는 `.scene` 등)도 씬 · 프리팹으로 열고 저장하게 되어 에디터에서는 되고
        // 배포본에는 없다. 쿠킹된 산출물(`.prefab.bin`)은 `.spv` 처럼 에셋이 아니다(`PrefabAsset::saveToFile` 은 소스만 쓴다).
        constexpr string_view kArrTextureExt[]         = { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".hdr", ".bmp" };
        constexpr string_view kArrMaterialExt[]        = { ".material" };
        constexpr string_view kArrShaderExt[]          = { ".hlsl", ".hlsli" };
        constexpr string_view kArrAudioExt[]           = { ".wav" };
        constexpr string_view kArrDataExt[]            = { ".xml", ".json", ".ini", ".kv" };
        constexpr string_view kArrAnimSuffix[]         = { ".anim.json", ".anim" };
        constexpr string_view kArrDialogueSuffix[]     = { ".dialogue.json", ".dialogue" };
        constexpr string_view kArrSpriteDocSuffix[]    = { ".sprite.json", ".sprite" };
        constexpr string_view kArrSpriteImageExt[]     = { ".png", ".jpg", ".jpeg", ".dds", ".tga" };
        constexpr string_view kArrSequenceSuffix[]     = { ".seq.json", ".seq" };
        constexpr string_view kArrTileMapSuffix[]      = { ".tilemap.xml", ".tilemap" };
        constexpr string_view kArrMeshExt[]            = { MeshAssetFormat::kExtension };
        constexpr string_view kArrSkeletonSuffix[]     = { Skeleton::kExtension };
        constexpr string_view kArrRigSuffix[]          = { RigAsset::kExtension };
        constexpr string_view kArrAnimClipExt[]        = { AnimClip::kExtension };
        constexpr string_view kArrFractureExt[]        = { FractureAsset::kExtension };
        constexpr string_view kArrModelSourceExt[]     = { ".glb", ".gltf", ".vrm" }; ///< `models_raw/` 의 원본 — 임포트하는 것이 리로드다
        constexpr string_view kArrHeightfieldRawExt[]  = { ".png", ".r16" };          ///< `heightfields_raw/` 의 원본 · 구멍 마스크 — 임포트하는 것이 리로드다
        constexpr string_view kArrHeightfieldExt[]     = { HeightfieldData::kExtension };
        constexpr string_view kArrLocalizationSuffix[] = { SourceStringTable::kExtension, TranslationTable::kExtension, LocalizationProject::kExtension }; ///< 올린 프로젝트를 다시 읽는다(`LocalizationManager`)
        constexpr string_view kArrUiDocumentSuffix[]   = { UiDocumentAsset::kExtension };                                                                  ///< UI 문서 — 그 문서로 연 화면을 다시 짓는다(`UiSystem`)
        constexpr string_view kArrUiStyleSuffix[]      = { UiStyleSheetAsset::kExtension };                                                                ///< UI 스타일 시트 — 쓰는 화면을 다시 맞춘다
        constexpr string_view kArrModuleDataSuffix[]   = { ".interactions.xml", ".elements.xml" };                                                         ///< 모듈이 올린 데이터 표 캐시(GameFramework 상호작용 · 원소 규칙 표)
        /**
         * @struct AssetMatchRow
         * @brief 경로 판정 규칙 한 줄입니다 — 어느 종류인지, 그리고 핫 리로드가 그 파일을 어떻게 다시 읽는지.
         * @details 한 종류가 줄을 둘 이상 가질 수 있다. 매칭 방식이 다를 때다(SpriteClip 은 문서 접미사와 이미지 확장자를 함께 쓴다).
         *          종류를 보여 주는 정보(이름 · 아이콘 · 색 · 패널 · 필터)는 종류 표(`kArrKindInfo`)에 종류마다 한 줄로 둔다.
         */
        struct AssetMatchRow
        {
            EditorAssetType         _kind;
            MatchMode               _mode;
            const string_view*      _pSuffix;
            uint32                  _suffixCount;
            const utf8*             _pCacheKindName;  ///< 이 줄의 파일을 들고 있는 엔진 캐시(`IAssetCache::getAssetKindName`). nullptr 이면 핫 리로드 대상이 아닙니다
            AssetSourceImporterFunc _pfnImportSource; ///< 캐시가 읽기 전에 소스를 임포트하는 임포터. nullptr 이면 바로 캐시가 다시 읽습니다
        };

        template <size_t N>
        constexpr uint32 countOf( const string_view ( & )[N] )
        {
            return static_cast<uint32>( N );
        }

        // 줄 순서가 곧 **판정 우선순위**다 — 처음 맞는 줄이 이긴다(`findKind` · `findReloadRoute`).
        constexpr AssetMatchRow kArrAssetMatch[] = {
            {        EditorAssetType::Scene, MatchMode::CookableSource,                nullptr,                                 0,                                nullptr,                                              nullptr},
            {       EditorAssetType::Prefab, MatchMode::CookableSource,                nullptr,                                 0,                               "Prefab",                                              nullptr},
            {  EditorAssetType::Heightfield, MatchMode::RawHeightfield,  kArrHeightfieldRawExt,  countOf( kArrHeightfieldRawExt ),                                nullptr, &HeightfieldImporter::importChangedSourceHeightfield},
            {      EditorAssetType::Texture,      MatchMode::Extension,         kArrTextureExt,         countOf( kArrTextureExt ),                              "Texture",           &TextureImporter::importChangedSourceImage},
            {       EditorAssetType::Shader,      MatchMode::Extension,          kArrShaderExt,          countOf( kArrShaderExt ),                                nullptr,                                              nullptr},
            {     EditorAssetType::Material,      MatchMode::Extension,        kArrMaterialExt,        countOf( kArrMaterialExt ),                             "Material",                                              nullptr},
            {        EditorAssetType::Audio,      MatchMode::Extension,           kArrAudioExt,           countOf( kArrAudioExt ),                                nullptr,                                              nullptr},
            {    EditorAssetType::AnimGraph,       MatchMode::EndsWith,         kArrAnimSuffix,         countOf( kArrAnimSuffix ),                                nullptr,                                              nullptr},
            {EditorAssetType::DialogueGraph,       MatchMode::EndsWith,     kArrDialogueSuffix,     countOf( kArrDialogueSuffix ),                                nullptr,                                              nullptr},
            {   EditorAssetType::SpriteClip,       MatchMode::EndsWith,    kArrSpriteDocSuffix,    countOf( kArrSpriteDocSuffix ),                           "SpriteClip",                                              nullptr},
            {   EditorAssetType::SpriteClip,      MatchMode::Extension,     kArrSpriteImageExt,     countOf( kArrSpriteImageExt ),                                nullptr,                                              nullptr},
            {      EditorAssetType::TileMap,       MatchMode::EndsWith,      kArrTileMapSuffix,      countOf( kArrTileMapSuffix ),                                nullptr,                                              nullptr},
            {     EditorAssetType::Sequence,       MatchMode::EndsWith,     kArrSequenceSuffix,     countOf( kArrSequenceSuffix ),                                nullptr,                                              nullptr},
            {         EditorAssetType::Mesh,      MatchMode::Extension,            kArrMeshExt,            countOf( kArrMeshExt ),                                 "Mesh",                                              nullptr},
            {         EditorAssetType::Mesh,      MatchMode::Extension,     kArrModelSourceExt,     countOf( kArrModelSourceExt ),                                 "Mesh",             &ModelImporter::importChangedSourceModel},
            {     EditorAssetType::Skeleton,       MatchMode::EndsWith,     kArrSkeletonSuffix,     countOf( kArrSkeletonSuffix ),                             "Skeleton",                                              nullptr},
            {          EditorAssetType::Rig,       MatchMode::EndsWith,          kArrRigSuffix,          countOf( kArrRigSuffix ),                                  "Rig",                                              nullptr},
            {     EditorAssetType::AnimClip,      MatchMode::Extension,        kArrAnimClipExt,        countOf( kArrAnimClipExt ),                             "AnimClip",                                              nullptr},
            {     EditorAssetType::Fracture,      MatchMode::Extension,        kArrFractureExt,        countOf( kArrFractureExt ),                             "Fracture",                                              nullptr},
            {  EditorAssetType::Heightfield,      MatchMode::Extension,     kArrHeightfieldExt,     countOf( kArrHeightfieldExt ),                                nullptr,                                              nullptr},
            {         EditorAssetType::Data,       MatchMode::EndsWith, kArrLocalizationSuffix, countOf( kArrLocalizationSuffix ),                          "StringTable",                                              nullptr},
            {         EditorAssetType::Data,       MatchMode::EndsWith,   kArrUiDocumentSuffix,   countOf( kArrUiDocumentSuffix ), AssetReloadRoute::kAnyCacheHoldingPath,                                              nullptr},
            {         EditorAssetType::Data,       MatchMode::EndsWith,      kArrUiStyleSuffix,      countOf( kArrUiStyleSuffix ), AssetReloadRoute::kAnyCacheHoldingPath,                                              nullptr},
            {         EditorAssetType::Data,       MatchMode::EndsWith,   kArrModuleDataSuffix,   countOf( kArrModuleDataSuffix ), AssetReloadRoute::kAnyCacheHoldingPath,                                              nullptr},
            {         EditorAssetType::Data,      MatchMode::Extension,            kArrDataExt,            countOf( kArrDataExt ),                                nullptr,                                              nullptr},
        };

        // 종류마다 한 줄. 줄 순서가 곧 **브라우저 필터 · 도구 패널 · 리소스 카탈로그의 표시 순서**다.
        // 칸: 종류 · 이름(단수) · 브라우저 라벨 · 패널 제목 · 아이콘 · 색 · 액센트 색 · Other 제외 · 임포트
        constexpr EditorAssetTypeInfo kArrKindInfo[] = {
            {        EditorAssetType::Scene,       "Scene",       "Scenes",           nullptr,       editoricon::kScene,                style::kAccent,  true,  true, false},
            {       EditorAssetType::Prefab,      "Prefab",      "Prefabs",   "Prefab Editor",      editoricon::kPrefab, { 0.35f, 0.70f, 1.00f, 1.0f }, false,  true,  true},
            {      EditorAssetType::Texture,     "Texture",     "Textures",           nullptr,     editoricon::kTexture, { 0.35f, 0.85f, 0.45f, 1.0f }, false,  true,  true},
            {       EditorAssetType::Shader,      "Shader",      "Shaders",           nullptr,      editoricon::kShader, { 0.95f, 0.45f, 0.35f, 1.0f }, false,  true,  true},
            {     EditorAssetType::Material,    "Material",    "Materials",        "Material",    editoricon::kMaterial, { 0.80f, 0.45f, 0.95f, 1.0f }, false,  true,  true},
            {        EditorAssetType::Audio,       "Audio",        "Audio",           nullptr,       editoricon::kAudio, { 0.95f, 0.85f, 0.25f, 1.0f }, false,  true,  true},
            {    EditorAssetType::AnimGraph,   "AnimGraph",         "Anim", "Animation Graph",   editoricon::kAnimGraph, { 1.00f, 0.60f, 0.20f, 1.0f }, false,  true,  true},
            {EditorAssetType::DialogueGraph,    "Dialogue",     "Dialogue",  "Dialogue Graph",    editoricon::kDialogue, { 0.40f, 0.75f, 1.00f, 1.0f }, false,  true,  true},
            {   EditorAssetType::SpriteClip,  "SpriteClip",       "Sprite",     "Sprite Clip",      editoricon::kSprite, { 1.00f, 0.60f, 0.20f, 1.0f }, false,  true,  true},
            {      EditorAssetType::TileMap,     "TileMap",     "Tile Map",   "Tile Map Tool",     editoricon::kTileMap, { 0.45f, 0.85f, 0.50f, 1.0f }, false,  true,  true},
            {     EditorAssetType::Sequence,    "Sequence",          "Seq",       "Sequencer",    editoricon::kSequence, { 0.85f, 0.55f, 0.85f, 1.0f }, false,  true,  true},
            {         EditorAssetType::Mesh,        "Mesh",       "Meshes",           nullptr,        editoricon::kCube, { 0.55f, 0.80f, 0.80f, 1.0f }, false,  true,  true},
            {     EditorAssetType::Skeleton,    "Skeleton",    "Skeletons",           nullptr,    editoricon::kSkeleton, { 0.90f, 0.85f, 0.70f, 1.0f }, false,  true,  true},
            {     EditorAssetType::AnimClip,    "AnimClip",        "Clips",           nullptr,    editoricon::kAnimClip, { 1.00f, 0.70f, 0.35f, 1.0f }, false,  true,  true},
            {          EditorAssetType::Rig,         "Rig",         "Rigs",           nullptr,         editoricon::kRig, { 0.95f, 0.75f, 0.55f, 1.0f }, false,  true,  true},
            {  EditorAssetType::Heightfield, "Heightfield", "Heightfields",           nullptr, editoricon::kHeightfield, { 0.65f, 0.60f, 0.45f, 1.0f }, false,  true, false},
            {     EditorAssetType::Fracture,    "Fracture",    "Fractures",           nullptr,    editoricon::kFracture, { 0.85f, 0.50f, 0.40f, 1.0f }, false,  true,  true},
            {         EditorAssetType::Data,        "Data",         "Data",           nullptr,       editoricon::kTable, { 0.60f, 0.75f, 0.95f, 1.0f }, false, false, false},
        };

        /** @brief 종류 표가 `Unknown` 을 뺀 모든 종류를 꼭 한 번씩 담고, 이름 · 라벨 · 아이콘 칸이 비지 않았는지입니다. */
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

        /** @brief 모든 종류가 판정 줄을 하나 이상 가졌는지입니다. 줄이 없는 종류는 어떤 경로에도 걸리지 않습니다. */
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

            /** @brief 엔진 쿠킹 규칙의 종류입니다. 씬 · 프리팹이 아니면 `AssetKind::Count` 입니다. */
            static AssetKind getCookKind( EditorAssetType kind )
            {
                if ( kind == EditorAssetType::Scene )
                    return AssetKind::Scene;
                if ( kind == EditorAssetType::Prefab )
                    return AssetKind::Prefab;
                return AssetKind::Count;
            }

            static bool matchRow( const AssetMatchRow& row, string_view path )
            {
                if ( row._mode == MatchMode::CookableSource )
                    return AssetCookPath::isCookableSource( path, getCookKind( row._kind ) );
                if ( row._mode == MatchMode::RawHeightfield )
                    return HeightfieldImporter::isRawHeightfieldPath( path );
                return matchSuffixList( path, row._pSuffix, row._suffixCount, row._mode );
            }

            /** @brief 줄 하나의 접미사를 @p outListSuffix 에 더합니다. 쿠킹 규칙 줄은 엔진 표에서 가져옵니다(정적 문자열). */
            static void appendRowSuffixes( const AssetMatchRow& row, vector<string_view>& outListSuffix )
            {
                if ( row._mode == MatchMode::CookableSource )
                {
                    AssetCookPath::appendSourceSuffixes( getCookKind( row._kind ), outListSuffix );
                    return;
                }
                if ( row._pSuffix == nullptr )
                    return;
                for ( uint32 index = 0; index < row._suffixCount; ++index )
                    outListSuffix.push_back( row._pSuffix[index] );
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
                        listMapping.push_back( EditorAssetPanelMapping{ row._kind, suffix } );
                }
                return listMapping;
            }

            /** @brief 전용 패널이 있는 종류를 종류 표 순서대로 모읍니다. */
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

            /** @brief 라벨이 있는 종류로 브라우저 필터를 만듭니다. 앞뒤의 All/Other 는 종류가 아닙니다. */
            static vector<EditorAssetBrowserFilter> buildBrowserFilters()
            {
                vector<EditorAssetBrowserFilter> listFilter{};
                listFilter.push_back( EditorAssetBrowserFilter{ "All", EditorAssetType::Unknown, false } );
                for ( const EditorAssetTypeInfo& info : kArrKindInfo )
                    listFilter.push_back( EditorAssetBrowserFilter{ info._pBrowserLabel, info._kind, false } );
                listFilter.push_back( EditorAssetBrowserFilter{ "Other", EditorAssetType::Unknown, true } );
                return listFilter;
            }

            /** @brief 종류의 패널 제목입니다. 없으면 nullptr 이고, `getPanelTitle` 의 빈 문자열과 구분됩니다. */
            static const utf8* getPanelTitleOf( EditorAssetType kind )
            {
                const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
                return pInfo != nullptr ? pInfo->_pPanelTitle : nullptr;
            }

            /** @brief 종류가 브라우저 "Other" 필터에서 빠지는지입니다. */
            static bool isOtherExcluded( EditorAssetType kind )
            {
                const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
                return pInfo != nullptr && pInfo->_bOtherExcluded;
            }

            /** @brief 종류의 접미사가 임포트 대화상자에 들어가는지입니다. */
            static bool isImportable( EditorAssetType kind )
            {
                const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
                return pInfo != nullptr && pInfo->_bImportable;
            }

            static bool containsSuffix( const vector<string>& listSuffix, string_view suffix )
            {
                for ( const string& existing : listSuffix )
                {
                    if ( StringUtil::equals( existing, suffix, true ) )
                        return true;
                }
                return false;
            }

            /** @brief 줄 하나의 접미사를 중복 없이 @p outListSuffix 에 더합니다. */
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

    string_view EditorAssetTypeRegistry::findPanelTitleForPath( string_view assetPath )
    {
        if ( assetPath.empty() )
            return {};

        // 가장 긴 접미사가 이긴다. `.anim.json` 이 `.json` 보다 구체적이다.
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
        if ( bestKind == EditorAssetType::Unknown )
            return {};
        return getPanelTitle( bestKind );
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
        // 표 순서대로 첫 일치 줄이 이긴다(이미지는 SpriteClip 이미지 줄보다 앞선 Texture 줄로 간다).
        for ( const AssetMatchRow& row : kArrAssetMatch )
        {
            if ( EditorAssetTypeInternal::matchRow( row, path ) == false )
                continue;
            route._pCacheKindName  = row._pCacheKindName;
            route._pfnImportSource = row._pfnImportSource;
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
