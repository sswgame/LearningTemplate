#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/TextureBaker.h"

#include "Engine/Resource/AssetFormat.h"

namespace sw::editor
{
    namespace
    {
        enum class MatchMode : uint8
        {
            EndsWith = 0,
            Extension,
            CookableSource ///< 엔진의 쿠킹 규칙(`AssetCookPath`) — 쿠커가 굽는 저작 소스만. 접미사는 엔진 표에서 온다
        };

        // 여기 적힌 확장자는 **이 저장소가 실제로 읽거나 쓰는 것만** 둔다. 2026-09-12 에
        // 쓰이지 않는 것을 걷어 냈다. `._material`(오타로 보인다) · `.mat`(파일도 코드도 없다) ·
        // `.pfb`(loadPrefab 이 모르는 이름) · `.glsl` `.vert` `.frag`(엔진은 HLSL 전용) ·
        // `.csv`(참조 0) · `.mp3` `.ogg`(디코더가 없다. XAudio2 는 `.wav` 만 읽는다).
        // `.spv` 도 뺐다. 그것은 **구운 산출물**이라(`Resource/common/shaders/bin/`) 셰이더
        // 소스로 세면 콘텐츠 브라우저가 빌드 출력 178개를 애셋으로 보여 준다.
        // 대신 `.hlsli` 를 넣었다. 공유 헤더는 진짜 셰이더 소스인데 빠져 있었다.
        //
        // 씬 · 프리팹은 여기 적지 않는다 — 쿠커가 굽는 이름이 곧 에디터가 여는 이름이다(`AssetCookPath`, 2026-10-03). 예전에는 여기 따로
        // 적어, 쿠커가 굽지 않는 `_scene.xml` · `.scene` 이 어디든 든 `.xml`(`forest.scenery.xml`) · 확장자 없는 `.scene` · `.prefab` 도
        // 씬 · 프리팹으로 열고 저장하고 퀵 런처에 띄웠다 — 에디터에서는 되고 배포본에는 없었다. 쿠킹본 `.prefab.bin` 은 프리팹 편집기로 열렸지만
        // 저장이 거절됐다(`PrefabAsset::saveToFile` 은 소스만 쓴다). 구운 산출물은 `.spv` 처럼 에셋이 아니다.
        constexpr string_view kArrTextureExt[]      = { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".hdr", ".bmp" };
        constexpr string_view kArrMaterialExt[]     = { ".material" };
        constexpr string_view kArrShaderExt[]       = { ".hlsl", ".hlsli" };
        constexpr string_view kArrAudioExt[]        = { ".wav" };
        constexpr string_view kArrDataExt[]         = { ".xml", ".json", ".ini", ".kv" };
        constexpr string_view kArrAnimSuffix[]      = { ".anim.json", ".anim" };
        constexpr string_view kArrDialogueSuffix[]  = { ".dialogue.json", ".dialogue" };
        constexpr string_view kArrSpriteDocSuffix[] = { ".sprite.json", ".sprite" };
        constexpr string_view kArrSpriteImageExt[]  = { ".png", ".jpg", ".jpeg", ".dds", ".tga" };
        constexpr string_view kArrSequenceSuffix[]  = { ".seq.json", ".seq" };
        constexpr string_view kArrTileMapSuffix[]   = { ".tilemap.xml", ".tilemap" };

        /**
         * @struct AssetKindRow
         * @brief 애셋 종류 하나에 대해 **에디터가 아는 전부**를 한 줄에 담습니다.
         *
         * @details 예전에는 표가 여섯이었습니다. 매칭 규칙 · 패널 제목 · 도구 패널 목록 · 패널 접미사 매핑 · 브라우저 필터 ·
         *          "Other" 제외 목록입니다. 종류를 하나 고치려면 여섯 군데를 찾아야 했고, 접미사는 그중 **두 곳에 따로**
         *          적혀 있어서 실제로 어긋났습니다(`._material` · `.mat` 은 양쪽에 있었고 `.hlsli` 는 양쪽에 없었습니다).
         *          지금은 이 표 하나가 정본이고 나머지는 모두 여기서 만들어집니다.
         *
         *          한 종류가 줄을 둘 이상 가질 수 있습니다. 매칭 방식이 다를 때입니다(SpriteClip 은 문서 접미사와 이미지
         *          확장자를 함께 씁니다). 보여 주는 정보(제목 · 라벨)는 **그 종류의 첫 줄**에 적습니다.
         */
        struct AssetKindRow
        {
            EditorAssetKind         _kind;
            MatchMode               _mode;
            const string_view*      _pSuffix;
            uint32                  _suffixCount;
            const utf8*             _pPanelTitle;     ///< 전용 도구 패널 제목. nullptr 이면 패널이 없습니다
            const utf8*             _pBrowserLabel;   ///< 콘텐츠 브라우저 필터 라벨. nullptr 이면 필터에 없습니다
            bool                    _bOtherExcluded;  ///< "Other" 필터에서 뺄지
            const utf8*             _pCacheKindName;  ///< 이 줄의 파일을 들고 있는 엔진 캐시(`IAssetCache::getAssetKindName`). nullptr 이면 핫 리로드 대상이 아닙니다
            AssetSourceImporterFunc _pfnImportSource; ///< 캐시가 읽기 전에 소스를 굽는 임포터. nullptr 이면 바로 캐시가 다시 읽습니다
        };

        template <size_t N>
        constexpr uint32 countOf( const string_view ( & )[N] )
        {
            return static_cast<uint32>( N );
        }

        // 줄 순서가 곧 **브라우저 필터와 도구 패널의 표시 순서**다.
        const AssetKindRow kArrAssetKind[] = {
            {         EditorAssetKind::Scene, MatchMode::CookableSource,             nullptr,                              0,           nullptr,    "Scenes",  true,      nullptr,                                 nullptr},
            {        EditorAssetKind::Prefab, MatchMode::CookableSource,             nullptr,                              0,   "Prefab Editor",   "Prefabs",  true,     "Prefab",                                 nullptr},
            {       EditorAssetKind::Texture,      MatchMode::Extension,      kArrTextureExt,      countOf( kArrTextureExt ),           nullptr,  "Textures",  true,    "Texture", &TextureBaker::importChangedSourceImage},
            {        EditorAssetKind::Shader,      MatchMode::Extension,       kArrShaderExt,       countOf( kArrShaderExt ),           nullptr,   "Shaders",  true,      nullptr,                                 nullptr},
            {      EditorAssetKind::Material,      MatchMode::Extension,     kArrMaterialExt,     countOf( kArrMaterialExt ),        "Material", "Materials",  true,   "Material",                                 nullptr},
            {         EditorAssetKind::Audio,      MatchMode::Extension,        kArrAudioExt,        countOf( kArrAudioExt ),           nullptr,     "Audio",  true,      nullptr,                                 nullptr},
            {EditorAssetKind::AnimationGraph,       MatchMode::EndsWith,      kArrAnimSuffix,      countOf( kArrAnimSuffix ), "Animation Graph",      "Anim",  true,      nullptr,                                 nullptr},
            { EditorAssetKind::DialogueGraph,       MatchMode::EndsWith,  kArrDialogueSuffix,  countOf( kArrDialogueSuffix ),  "Dialogue Graph",  "Dialogue",  true,      nullptr,                                 nullptr},
            {    EditorAssetKind::SpriteClip,       MatchMode::EndsWith, kArrSpriteDocSuffix, countOf( kArrSpriteDocSuffix ),     "Sprite Clip",    "Sprite",  true, "SpriteClip",                                 nullptr},
            {    EditorAssetKind::SpriteClip,      MatchMode::Extension,  kArrSpriteImageExt,  countOf( kArrSpriteImageExt ),           nullptr,     nullptr,  true,      nullptr,                                 nullptr},
            {       EditorAssetKind::TileMap,       MatchMode::EndsWith,   kArrTileMapSuffix,   countOf( kArrTileMapSuffix ),   "Tile Map Tool",  "Tile Map",  true,      nullptr,                                 nullptr},
            {      EditorAssetKind::Sequence,       MatchMode::EndsWith,  kArrSequenceSuffix,  countOf( kArrSequenceSuffix ),       "Sequencer",       "Seq",  true,      nullptr,                                 nullptr},
            {          EditorAssetKind::Data,      MatchMode::Extension,         kArrDataExt,         countOf( kArrDataExt ),           nullptr,      "Data", false,      nullptr,                                 nullptr},
        };

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
            static AssetKind getCookKind( EditorAssetKind kind )
            {
                if ( kind == EditorAssetKind::Scene )
                    return AssetKind::Scene;
                if ( kind == EditorAssetKind::Prefab )
                    return AssetKind::Prefab;
                return AssetKind::Count;
            }

            static bool matchRow( const AssetKindRow& row, string_view path )
            {
                if ( row._mode == MatchMode::CookableSource )
                    return AssetCookPath::isCookableSource( path, getCookKind( row._kind ) );
                return matchSuffixList( path, row._pSuffix, row._suffixCount, row._mode );
            }

            /** @brief 줄 하나의 접미사를 @p outListSuffix 에 더합니다. 쿠킹 규칙 줄은 엔진 표에서 가져옵니다(정적 문자열). */
            static void appendRowSuffixes( const AssetKindRow& row, vector<string_view>& outListSuffix )
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
                for ( const AssetKindRow& row : kArrAssetKind )
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

            /** @brief 전용 패널이 있는 종류를 표 순서대로 모읍니다(중복 제거). */
            static vector<EditorAssetKind> buildToolPanelKinds()
            {
                vector<EditorAssetKind> listKind{};
                for ( const AssetKindRow& row : kArrAssetKind )
                {
                    if ( row._pPanelTitle == nullptr )
                        continue;
                    if ( std::find( listKind.begin(), listKind.end(), row._kind ) != listKind.end() )
                        continue;
                    listKind.push_back( row._kind );
                }
                return listKind;
            }

            /** @brief 라벨이 있는 종류로 브라우저 필터를 만듭니다. 앞뒤의 All/Other 는 종류가 아닙니다. */
            static vector<EditorAssetBrowserFilter> buildBrowserFilters()
            {
                vector<EditorAssetBrowserFilter> listFilter{};
                listFilter.push_back( EditorAssetBrowserFilter{ "All", EditorAssetKind::Unknown, false } );
                for ( const AssetKindRow& row : kArrAssetKind )
                {
                    if ( row._pBrowserLabel == nullptr )
                        continue;
                    listFilter.push_back( EditorAssetBrowserFilter{ row._pBrowserLabel, row._kind, false } );
                }
                listFilter.push_back( EditorAssetBrowserFilter{ "Other", EditorAssetKind::Unknown, true } );
                return listFilter;
            }

            /** @brief 종류의 패널 제목입니다. 없으면 nullptr 이고, `getPanelTitle` 의 빈 문자열과 구분됩니다. */
            static const utf8* getPanelTitleOf( EditorAssetKind kind )
            {
                for ( const AssetKindRow& row : kArrAssetKind )
                {
                    if ( row._kind == kind && row._pPanelTitle != nullptr )
                        return row._pPanelTitle;
                }
                return nullptr;
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
            static void appendUniqueRowSuffixes( const AssetKindRow& row, vector<string>& outListSuffix )
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
    bool EditorAssetTypeRegistry::matches( EditorAssetKind kind, string_view path )
    {
        if ( kind == EditorAssetKind::Unknown || path.empty() )
            return false;

        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._kind != kind )
                continue;
            if ( EditorAssetTypeInternal::matchRow( row, path ) )
                return true;
        }
        return false;
    }

    bool EditorAssetTypeRegistry::matches( EditorAssetKind kind, const utf8* pPath )
    {
        if ( pPath == nullptr )
            return false;
        return matches( kind, string_view{ pPath } );
    }

    bool EditorAssetTypeRegistry::matchesAny( string_view path )
    {
        if ( path.empty() )
            return false;
        for ( const AssetKindRow& row : kArrAssetKind )
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
        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._bOtherExcluded && EditorAssetTypeInternal::matchRow( row, path ) )
                return false;
        }
        return true;
    }

    const utf8* EditorAssetTypeRegistry::getPanelTitle( EditorAssetKind kind )
    {
        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._kind == kind && row._pPanelTitle != nullptr )
                return row._pPanelTitle;
        }
        return "";
    }

    string_view EditorAssetTypeRegistry::findPanelTitleForPath( string_view assetPath )
    {
        if ( assetPath.empty() )
            return {};

        // 가장 긴 접미사가 이긴다. `.anim.json` 이 `.json` 보다 구체적이다.
        size_t                               bestLen{ 0 };
        EditorAssetKind                      bestKind{ EditorAssetKind::Unknown };
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
        if ( bestKind == EditorAssetKind::Unknown )
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

    const EditorAssetKind* EditorAssetTypeRegistry::getToolPanelKinds( uint32& outCount )
    {
        static const vector<EditorAssetKind> s_listKind = EditorAssetTypeInternal::buildToolPanelKinds();
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
        for ( const AssetKindRow& row : kArrAssetKind )
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
        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._pCacheKindName != nullptr )
                outListKindName.push_back( row._pCacheKindName );
        }
    }

    void EditorAssetTypeRegistry::appendReloadableSuffixes( vector<string>& outListSuffix )
    {
        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._pCacheKindName != nullptr )
                EditorAssetTypeInternal::appendUniqueRowSuffixes( row, outListSuffix );
        }
    }

    void EditorAssetTypeRegistry::appendSuffixes( EditorAssetKind kind, vector<string>& outListSuffix )
    {
        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._kind == kind )
                EditorAssetTypeInternal::appendUniqueRowSuffixes( row, outListSuffix );
        }
    }

    bool EditorAssetTypeRegistry::collectFiles( EditorAssetKind kind, string_view directory, vector<string>& outListFilePath )
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
        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._kind == EditorAssetKind::Data || row._kind == EditorAssetKind::Scene )
                continue;
            EditorAssetTypeInternal::appendUniqueRowSuffixes( row, outListExtension );
        }
        if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".json" ) == false )
            outListExtension.push_back( ".json" );
        if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".txt" ) == false )
            outListExtension.push_back( ".txt" );
    }
} // namespace sw::editor
