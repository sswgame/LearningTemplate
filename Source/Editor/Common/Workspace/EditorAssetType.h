/**
 * @file EditorAssetType.h
 * @brief 에디터의 애셋 종류, 패널 제목, 접미사를 한곳에서 정의합니다(SSOT).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorColor.h"

namespace sw::editor
{
    /**
     * @brief 도구 문서를 읽은 결과입니다. "새 문서" 와 "읽지 못한 파일" 을 구분하며, 문서 패널은 이 값으로 저장을 막습니다(`EditorDocumentPanel::reloadDocument`).
     * @details 타입에 `[[nodiscard]]` 가 붙어 있어 이 값을 반환하는 로드의 결과를 버리면 컴파일러가 경고한다. bool 로는 "없음" 과 "깨짐" 이
     *          섞이고, 결과를 버린 채 읽었다고 표시하면 깨진 파일을 이전 문서로 덮어쓴다.
     */
    enum class [[nodiscard]] ToolAssetLoadResult : uint8
    {
        Loaded,    ///< 읽었다
        Missing,   ///< 파일이 없다. 새 문서이며 기본값으로 시작하고 저장하면 파일을 만든다
        Malformed, ///< 파일은 있지만 읽지 못했다(깨졌거나 더 새로운 형식). 덮어쓰면 안 된다
    };

    /** @brief 에디터가 구분하는 애셋 종류입니다. 한 경로가 여러 종류에 해당할 수 있습니다. */
    enum class EditorAssetType : uint8
    {
        Unknown = 0,
        Scene,
        Prefab,
        Texture,
        Material,
        Shader,
        Audio,
        Data,
        AnimationGraph,
        DialogueGraph,
        SpriteClip,
        TileMap,
        Sequence,
        Mesh,
        Skeleton,      ///< 임포트가 만든 스켈레톤(`.skeleton.xml`). 스켈레톤 캐시가 핫 리로드한다
        AnimationClip, ///< 임포트가 만든 애니메이션 클립(`.animclip`). 클립 캐시가 핫 리로드한다
        Heightfield,   ///< 높이장. 원본(`heightfields_raw/` 의 `.png`, `.r16`)이 바뀌면 임포트하고, 결과는 `.heightfield` 이다
        LayerWeights,  ///< 지형 레이어 가중치(`.layerweights`). 원본 스플랫(`heightfields_raw/` 의 `.splat.png`)을 임포트한 결과이고, 지형 브러시가 칠해 저장한다
        Rig,           ///< 후처리 리그(`.rig.xml`). 리그 캐시가 핫 리로드하고 PoseModifierComponent 가 다시 바인딩한다
        Fracture,      ///< 임포트가 만든 파쇄 에셋(`.fracture`). 파쇄 캐시가 핫 리로드한다
        UIDocument,    ///< 런타임 UI 문서(`.ui.xml`). 열면 UI Preview 패널이 그 문서를 보여 주고, 문서 캐시가 핫 리로드한다
        Count          ///< 종류의 개수이며 종류가 아니다. 종류 테이블(EditorAssetType.cpp)이 모든 종류를 한 번씩 담는지 컴파일 타임에 검사한다
    };

    /** @brief 소스 파일을 임포트하는 임포터입니다. 처리했으면 true 를 반환하며, 임포트했거나 임포트하지 않는다고 알린 경우가 모두 해당합니다. 이때는 그 파일의 캐시 리로드를 하지 않습니다. */
    using AssetSourceImporterFunc = bool ( * )( string_view relativePath );

    /**
     * @struct AssetReloadRoute
     * @brief 핫 리로드가 변경된 파일 하나를 처리하는 방법입니다(`EditorAssetTypeRegistry::findReloadRoute`).
     */
    struct AssetReloadRoute
    {
        /**
         * @brief 모듈이 등록한 캐시라 에디터가 이름을 모르는 항목에 쓰는 캐시 이름입니다(빈 문자열). 그 경로를 가진 캐시(`IAssetCache::isCached`)가 모두 다시 읽습니다.
         * @details GameFramework 의 데이터 테이블 캐시(`GameDataCache`: 상호작용 테이블, 원소 규칙 테이블)는 게임 서비스가 연결될 때 레지스트리에 등록된다. 에디터는 GameFramework 를 모른다.
         */
        static constexpr const utf8* kAnyCacheHoldingPath = "";

        const utf8*             _pCacheKindName{ nullptr };  ///< 다시 읽을 엔진 캐시(`IAssetCache::getAssetKindName`). nullptr 이면 핫 리로드 대상이 아니고, 빈 문자열이면 `kAnyCacheHoldingPath` 이다
        AssetSourceImporterFunc _pfnImportSource{ nullptr }; ///< 캐시가 다시 읽기 전에 실행하는 임포터. nullptr 이면 바로 캐시가 다시 읽는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorAssetTypeInfo
     * @brief 애셋 종류 하나를 화면에 보여 주는 데 필요한 정보를 모두 담습니다. 이름, 아이콘, 색, 패널, 필터, 임포트 여부가 해당합니다.
     * @details 종류를 추가하려면 이 테이블에 항목 하나를 추가한다. 아이콘, 색, 퀵 런처 분류, 카탈로그를 종류별 분기로 따로 적지 않는다.
     *          빈 항목이 있으면 컴파일이 실패한다(EditorAssetType.cpp 의 static_assert).
     */
    struct EditorAssetTypeInfo
    {
        EditorAssetType _kind;
        const utf8*     _pDisplayName;   ///< 단수 이름. 퀵 런처 분류와 배지에 쓴다
        const utf8*     _pBrowserLabel;  ///< 콘텐츠 브라우저 필터와 리소스 카탈로그의 라벨
        const utf8*     _pPanelTitle;    ///< 전용 도구 패널 제목. nullptr 이면 패널이 없다
        const utf8*     _pIcon;          ///< 에디터 아이콘 글리프(editoricon::k…, UTF-8)
        Color           _color;          ///< 목록과 배지의 색. `_bAccentColor` 이면 쓰지 않는다
        bool            _bAccentColor;   ///< 고정 색 대신 테마 액센트 색을 쓴다
        bool            _bOtherExcluded; ///< 브라우저 "Other" 필터에서 뺄지 정한다
        bool            _bImportable;    ///< 임포트 대화상자의 확장자 목록에 넣을지 정한다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 확장자나 접미사에서 도구 패널 종류로의 대응입니다. */
    struct EditorAssetPanelMapping
    {
        EditorAssetType _kind;
        string_view     _suffix;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 콘텐츠 브라우저 타입 필터의 항목 하나입니다. */
    struct EditorAssetBrowserFilter
    {
        /**
         * @brief 콤보에 그대로 넘기는 라벨입니다. 널 종단 문자열입니다.
         * @details 쓰는 쪽이 이 값을 곧바로 ImGui 로 넘기는데 ImGui 는 널 종단을 요구한다. `string_view` 는 이를 보장하지 않아 누가 부분
         *          문자열을 넣으면 조용히 범위를 넘어 읽게 되므로, 타입으로 계약을 드러낸다.
         */
        const utf8*     _pLabel;
        EditorAssetType _kind;
        bool            _bOther;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorAssetTypeRegistry
     * @brief 애셋 종류 판별, 패널 제목, 브라우저 필터를 한곳에서 정의합니다.
     */
    class EditorAssetTypeRegistry
    {
    public:
        /** @brief 경로가 지정한 종류에 해당하면 true 입니다. */
        static bool matches( EditorAssetType kind, string_view path );
        /** @brief 경로가 nullptr 이면 false 입니다. */
        static bool matches( EditorAssetType kind, const utf8* pPath );
        /** @brief 알려진 애셋 종류 중 하나라도 해당하면 true 입니다. */
        static bool matchesAny( string_view path );
        /** @brief 브라우저의 Other 필터 조건입니다. 전용 종류에 해당하지 않으면 true 입니다. */
        static bool matchesOther( string_view path );

        /**
         * @brief 경로가 속한 종류 하나를 반환합니다. 해당하는 종류가 없으면 `Unknown` 입니다.
         * @details 판정 테이블의 순서대로 처음 맞는 항목이 이긴다. `.animgraph.xml` 은 Data 보다 앞선 AnimationGraph 로, 이미지는 SpriteClip 보다 앞선 Texture 로 판정한다.
         *          아이콘, 색, 퀵 런처 분류, 카탈로그, 썸네일, 열기, 드롭이 모두 이 함수 하나로 종류를 정한다.
         */
        static EditorAssetType findKind( string_view path );
        /**
         * @brief 엔진 에셋 종류(`AssetTypeDef::_name`)가 속한 에디터 종류를 반환합니다. 규칙 표에 오르지 않은 종류는 `Unknown` 입니다.
         * @details 접미사가 아니라 엔진의 종류 정의로 가리키는 규칙(씬 · 프리팹 포함)만 찾는다. 이름으로 견주는 것은 정의가 DLL 마다 사본이라 주소로 견줄 수 없어서다.
         */
        static EditorAssetType findKindOfEngineType( string_view typeName );
        /**
         * @brief 엔진 에셋 종류가 에디터 쪽에 알려져 있으면 true 입니다 — 규칙 표에 있거나, 에디터가 종류로 올리지 않는다고 명시한 목록에 있습니다.
         * @details 엔진에 종류가 새로 등록되고 에디터가 모르면 false 이며, `EditorAssetTypeTest.EveryEngineAssetTypeIsAccountedFor` 가 막는다(엔진은 에디터 헤더를 모르는 경계라 테스트가 잇는다).
         */
        static bool isEngineTypeAccounted( string_view typeName );
        /** @brief 규칙 표가 이름으로 가리키는 엔진 에셋 종류가 모두 엔진에 등록되어 있으면 true 입니다(등록이 링크에서 빠지면 그 규칙은 어떤 경로에도 맞지 않는다). */
        static bool areEngineTypeRowsResolved();

        /** @brief 종류의 표시 정보를 반환합니다. `Unknown` 이나 `Count` 이면 nullptr 입니다. */
        static const EditorAssetTypeInfo* findKindInfo( EditorAssetType kind );
        /** @brief 종류 테이블 전체를 표시 순서대로 반환합니다. outCount 에 개수를 씁니다. */
        static const EditorAssetTypeInfo* getKindInfos( uint32& outCount );

        /** @brief 도구 패널 제목입니다. 전용 패널이 없으면 빈 문자열입니다. */
        static const utf8* getPanelTitle( EditorAssetType kind );
        /** @brief 경로를 여는 도구 패널의 종류입니다(가장 긴 접미사가 이긴다). 전용 패널이 없으면 `Unknown` 입니다. 여는 요청은 `EditorWorkspace::requestOpenDocumentPanel` 입니다. */
        static EditorAssetType findPanelKindForPath( string_view assetPath );

        /** @brief 도구 패널을 여는 접미사 목록을 반환합니다. outCount 에 개수를 씁니다. */
        static const EditorAssetPanelMapping* getPanelMappings( uint32& outCount );
        /** @brief Assets 메뉴와 도킹에 쓰는 도구 패널 종류 목록을 반환합니다. */
        static const EditorAssetType* getToolPanelKinds( uint32& outCount );

        /**
         * @brief 도구 패널이 있는 종류와 그 제목을 하나씩 콜백(`func( kind, pTitle )`)에 넘깁니다. 제목이 비어 있는 종류는 건너뜁니다.
         * @details 순회는 레지스트리가 맡고, 쓰는 쪽(메뉴바)은 유효한 제목만 받는다. 여는 요청은 제목이 아니라 종류로 한다(`requestOpenDocumentPanel`).
         */
        template <typename Func>
        static void forEachToolPanel( Func&& func )
        {
            uint32                       kindCount{ 0 };
            const EditorAssetType* const pKind = getToolPanelKinds( kindCount );
            for ( uint32 index = 0; index < kindCount; ++index )
            {
                const utf8* pTitle = getPanelTitle( pKind[index] );
                if ( StringUtil::isNullOrEmpty( pTitle ) )
                    continue;
                func( pKind[index], pTitle );
            }
        }
        /** @brief 콘텐츠 브라우저 타입 필터 목록을 반환합니다. */
        static const EditorAssetBrowserFilter* getBrowserFilters( uint32& outCount );
        /**
         * @brief 지정한 종류의 접미사를 outListSuffix 에 추가합니다. 이미 있는 접미사는 건너뜁니다.
         * @details 파일 감시 필터처럼 "이 종류의 파일" 을 골라야 하는 쪽이 확장자 목록을 따로 적지 않게 한다. 목록이 둘이면
         *          한쪽만 늘어나 감시가 일부 확장자를 놓친다. 예를 들어 감시가 `.mat` 만 보면 `.material` 파일은 하나도 감지하지 못한다.
         */
        static void appendSuffixes( EditorAssetType kind, vector<string>& outListSuffix );
        /**
         * @brief 변경된 파일 하나를 핫 리로드가 처리하는 방법을 찾습니다. 다시 읽을 캐시와 먼저 실행할 임포터가 결과입니다. 핫 리로드 대상이 아니면 둘 다 nullptr 입니다.
         * @details 새 에셋 종류는 엔진에 `IAssetCache` 를 등록하고 이 테이블의 항목에 그 이름을 적으면 핫 리로드된다.
         *          `AssetHotReload` 에는 종류별 코드가 없으므로 거기에 분기를 추가하지 않는다.
         */
        static AssetReloadRoute findReloadRoute( string_view path );
        /** @brief 테이블에 적힌 핫 리로드 캐시 이름을 모두 @p outListKindName 에 추가합니다. 엔진 레지스트리와 일치하는지 검사하는 테스트에서 씁니다. */
        static void appendReloadCacheKindNames( vector<string_view>& outListKindName );
        /** @brief 엔진 캐시가 있는 종류(핫 리로드 대상)의 접미사를 @p outListSuffix 에 추가합니다. 이미 있는 접미사는 건너뜁니다. */
        static void appendReloadableSuffixes( vector<string>& outListSuffix );
        /**
         * @brief @p directory 아래를 재귀로 훑어 지정한 종류의 파일을 @p outListFilePath 에 추가합니다. 폴더가 없으면 false 입니다.
         * @details 판정은 `matches` 를 그대로 쓴다. 종류마다 확장자 하나로 따로 세면 JSON 프리팹, `.jpg` 텍스처, `.hlsli` 를 놓친다.
         */
        [[nodiscard]] static bool collectFiles( EditorAssetType kind, string_view directory, vector<string>& outListFilePath );

        /** @brief 임포트 대화상자용 접미사를 outListExtension 에 추가합니다. */
        static void appendImportExtensions( vector<string>& outListExtension );

        /** @brief 워크스페이스 포커스가 지정한 종류이면 그 경로를 반환하고, 아니면 빈 문자열을 반환합니다. */
        static string_view matchingFocusedPath( EditorAssetType kind );
        /** @brief 포커스 경로나 extraToken 이 바뀌었으면 ioLastKey 를 갱신하고 true 를 반환합니다. */
        static bool consumeWorkspaceFocusKey( string& ioLastKey, uint64 extraToken );
    };
} // namespace sw::editor
