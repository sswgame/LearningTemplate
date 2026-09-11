#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

namespace sw::editor
{
    namespace
    {
        enum class MatchMode : uint8
        {
            EndsWith = 0,
            Extension,
            Scene
        };

        // 여기 적힌 확장자는 **이 저장소가 실제로 읽거나 쓰는 것만** 둔다. 2026-09-12 에
        // 죽은 것을 걷어냈다 — `._material`(오타로 보인다) · `.mat`(파일도 코드도 없다) ·
        // `.pfb`(loadPrefab 이 모르는 이름) · `.glsl` `.vert` `.frag`(엔진은 HLSL 전용) ·
        // `.csv`(참조 0) · `.mp3` `.ogg`(디코더가 없다. XAudio2 는 `.wav` 만 읽는다).
        // `.spv` 도 뺐다 — 그것은 **구운 산출물**이라(`Resource/common/shaders/bin/`) 셰이더
        // 소스로 세면 콘텐츠 브라우저가 빌드 출력 178개를 에셋으로 보여 준다.
        // 대신 `.hlsli` 를 넣었다 — 공유 헤더는 진짜 셰이더 소스인데 빠져 있었다.
        constexpr string_view kArrPrefabSuffix[]    = { ".prefab.xml", ".prefab.json", ".prefab.bin", ".prefab" };
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
         * @brief 에셋 종류 하나에 대해 **에디터가 아는 전부**를 한 줄에 담습니다.
         *
         * @details 예전에는 표가 여섯이었다 — 매칭 규칙 · 패널 제목 · 도구 패널 목록 · 패널 접미사
         *          매핑 · 브라우저 필터 · "Other" 제외 목록. 종류를 하나 고치려면 여섯 군데를
         *          찾아야 했고, 접미사는 그중 **두 곳에 따로** 적혀 있어서 실제로 어긋났다
         *          (`._material` · `.mat` 은 양쪽에 있었고 `.hlsli` 는 양쪽에 없었다).
         *          지금은 이 표 하나가 정본이고 나머지는 전부 여기서 만들어진다.
         *
         *          한 종류가 줄을 둘 이상 가질 수 있다 — 매칭 방식이 다를 때다(SpriteClip 은
         *          문서 접미사와 이미지 확장자를 함께 쓴다). 보여 주는 정보(제목·라벨)는
         *          **그 종류의 첫 줄**에 적는다.
         */
        struct AssetKindRow
        {
            EditorAssetKind    _kind;
            MatchMode          _mode;
            const string_view* _pSuffix;
            uint32             _suffixCount;
            const utf8*        _pPanelTitle;    ///< 전용 도구 패널 제목. nullptr 이면 패널이 없다
            const utf8*        _pBrowserLabel;  ///< 콘텐츠 브라우저 필터 라벨. nullptr 이면 필터에 없다
            bool               _bOtherExcluded; ///< "Other" 필터에서 뺄지
        };

        template <size_t N>
        constexpr uint32 countOf( const string_view ( & )[N] )
        {
            return static_cast<uint32>( N );
        }

        // 줄 순서가 곧 **브라우저 필터와 도구 패널의 표시 순서**다.
        const AssetKindRow kArrAssetKind[] = {
            {         EditorAssetKind::Scene,     MatchMode::Scene,             nullptr,                              0,           nullptr,    "Scenes",  true},
            {        EditorAssetKind::Prefab,  MatchMode::EndsWith,    kArrPrefabSuffix,    countOf( kArrPrefabSuffix ),   "Prefab Editor",   "Prefabs",  true},
            {       EditorAssetKind::Texture, MatchMode::Extension,      kArrTextureExt,      countOf( kArrTextureExt ),           nullptr,  "Textures",  true},
            {        EditorAssetKind::Shader, MatchMode::Extension,       kArrShaderExt,       countOf( kArrShaderExt ),           nullptr,   "Shaders",  true},
            {      EditorAssetKind::Material, MatchMode::Extension,     kArrMaterialExt,     countOf( kArrMaterialExt ),        "Material", "Materials",  true},
            {         EditorAssetKind::Audio, MatchMode::Extension,        kArrAudioExt,        countOf( kArrAudioExt ),           nullptr,     "Audio",  true},
            {EditorAssetKind::AnimationGraph,  MatchMode::EndsWith,      kArrAnimSuffix,      countOf( kArrAnimSuffix ), "Animation Graph",      "Anim",  true},
            { EditorAssetKind::DialogueGraph,  MatchMode::EndsWith,  kArrDialogueSuffix,  countOf( kArrDialogueSuffix ),  "Dialogue Graph",  "Dialogue",  true},
            {    EditorAssetKind::SpriteClip,  MatchMode::EndsWith, kArrSpriteDocSuffix, countOf( kArrSpriteDocSuffix ),     "Sprite Clip",    "Sprite",  true},
            {    EditorAssetKind::SpriteClip, MatchMode::Extension,  kArrSpriteImageExt,  countOf( kArrSpriteImageExt ),           nullptr,     nullptr,  true},
            {       EditorAssetKind::TileMap,  MatchMode::EndsWith,   kArrTileMapSuffix,   countOf( kArrTileMapSuffix ),   "Tile Map Tool",  "Tile Map",  true},
            {      EditorAssetKind::Sequence,  MatchMode::EndsWith,  kArrSequenceSuffix,  countOf( kArrSequenceSuffix ),       "Sequencer",       "Seq",  true},
            {          EditorAssetKind::Data, MatchMode::Extension,         kArrDataExt,         countOf( kArrDataExt ),           nullptr,      "Data", false},
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

            static bool matchScene( string_view path )
            {
                if ( path.empty() )
                    return false;
                if ( FileUtil::hasExtension( path, ".scene" ) )
                    return true;
                if ( FileUtil::hasExtension( path, ".xml" ) )
                {
                    const string pathStr{ path };
                    if ( StringUtil::stristr( pathStr.c_str(), ".scene" ) != nullptr )
                        return true;
                }
                return StringUtil::endsWith( path, "_scene.xml", true );
            }

            static bool matchRow( const AssetKindRow& row, string_view path )
            {
                if ( row._mode == MatchMode::Scene )
                    return matchScene( path );
                return matchSuffixList( path, row._pSuffix, row._suffixCount, row._mode );
            }

            /** @brief 전용 패널이 있는 종류의 접미사를 펼쳐 패널 매핑을 만듭니다. */
            static vector<EditorAssetPanelMapping> buildPanelMappings()
            {
                vector<EditorAssetPanelMapping> listMapping{};
                for ( const AssetKindRow& row : kArrAssetKind )
                {
                    if ( getPanelTitleOf( row._kind ) == nullptr || row._pSuffix == nullptr )
                        continue;
                    for ( uint32 index = 0; index < row._suffixCount; ++index )
                        listMapping.push_back( EditorAssetPanelMapping{ row._kind, row._pSuffix[index] } );
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

            /** @brief 라벨이 있는 종류로 브라우저 필터를 만듭니다. 앞뒤의 All/Other 는 종류가 아니다. */
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

            /** @brief 종류의 패널 제목. 없으면 nullptr — `getPanelTitle` 의 빈 문자열과 구분한다. */
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

        // 가장 긴 접미사가 이긴다 — `.anim.json` 이 `.json` 보다 구체적이다.
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
        // 전용 패널이 있는 종류의 접미사를 그대로 펼친다 — 접미사를 두 번 적지 않기 위해서다.
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

    void EditorAssetTypeRegistry::appendSuffixes( EditorAssetKind kind, vector<string>& outListSuffix )
    {
        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._kind != kind || row._pSuffix == nullptr )
                continue;
            for ( uint32 index = 0; index < row._suffixCount; ++index )
            {
                const string_view suffix = row._pSuffix[index];
                if ( EditorAssetTypeInternal::containsSuffix( outListSuffix, suffix ) )
                    continue;
                outListSuffix.push_back( string{ suffix } );
            }
        }
    }

    void EditorAssetTypeRegistry::appendImportExtensions( vector<string>& outListExtension )
    {
        for ( const AssetKindRow& row : kArrAssetKind )
        {
            if ( row._kind == EditorAssetKind::Data || row._kind == EditorAssetKind::Scene )
                continue;
            if ( row._pSuffix == nullptr )
                continue;
            for ( uint32 index = 0; index < row._suffixCount; ++index )
            {
                const string_view suffix = row._pSuffix[index];
                if ( EditorAssetTypeInternal::containsSuffix( outListExtension, suffix ) )
                    continue;
                outListExtension.push_back( string{ suffix } );
            }
        }
        if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".json" ) == false )
            outListExtension.push_back( ".json" );
        if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".txt" ) == false )
            outListExtension.push_back( ".txt" );
    }
} // namespace sw::editor
