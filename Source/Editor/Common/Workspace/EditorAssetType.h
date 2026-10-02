/**
 * @file EditorAssetType.h
 * @brief 에디터 애셋 종류 · 패널 제목 · 접미사의 단일 정의(SSOT)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"

namespace sw::editor
{
    /**
     * @brief 도구 문서를 읽은 결과 — "새 문서" 와 "읽지 못한 파일" 을 가른다. 문서 패널은 이것으로 저장을 막는다(`EditorDocumentPanel::reloadDocument`).
     * @details 타입에 `[[nodiscard]]` 가 붙어 있다 — 이것을 돌려주는 로드의 결과를 버리면 컴파일러가 짚는다. 예전에는 다섯 로더 중 셋이 bool 이라
     *          "없음" 과 "깨짐" 이 섞였고, 두 패널은 그 bool 마저 버린 채 읽었다고 표시해 깨진 파일을 앞 문서로 덮었다.
     */
    enum class [[nodiscard]] ToolAssetLoadResult : uint8
    {
        Loaded,    ///< 읽었다
        Missing,   ///< 파일이 없다 — 새 문서(기본값으로 시작하고 저장하면 만든다)
        Malformed, ///< 파일은 있는데 읽지 못했다(깨졌거나 더 새 형식) — 덮으면 안 된다
    };

    /** @brief 에디터가 구분하는 애셋 종류. 한 경로가 여러 종류에 걸릴 수 있습니다. */
    enum class EditorAssetKind : uint8
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
        Sequence
    };

    /** @brief 소스 파일을 굽는 임포터입니다. 처리했으면(구웠거나 굽지 않는다고 알렸으면) true — 그 파일의 캐시 리로드는 하지 않습니다. */
    using AssetSourceImporterFunc = bool ( * )( string_view relativePath );

    /**
     * @struct AssetReloadRoute
     * @brief 핫 리로드가 바뀐 파일 하나를 처리하는 방법입니다(`EditorAssetTypeRegistry::findReloadRoute`).
     */
    struct AssetReloadRoute
    {
        const utf8*             _pCacheKindName{ nullptr };  ///< 다시 읽을 엔진 캐시(`IAssetCache::getAssetKindName`). nullptr 이면 핫 리로드 대상이 아닙니다
        AssetSourceImporterFunc _pfnImportSource{ nullptr }; ///< 캐시가 읽기 전에 돌리는 임포터. nullptr 이면 바로 캐시가 다시 읽습니다
    };

    /** @brief 확장자/접미사 → 도구 패널 종류 */
    struct EditorAssetPanelMapping
    {
        EditorAssetKind _kind;
        string_view     _suffix;
    };

    /** @brief 콘텐츠 브라우저 타입 필터 한 줄 */
    struct EditorAssetBrowserFilter
    {
        /**
         * @brief 콤보에 그대로 넘기는 라벨입니다. **널 종단 문자열입니다.**
         * @details 예전에는 `string_view` 였는데, 쓰는 쪽 셋이 모두 곧바로 `.data()` 를 ImGui 로 넘겼습니다. ImGui 는 널 종단을
         *          요구하지만 `string_view` 는 그것을 보장하지 않습니다. 지금 표가 모두 리터럴이라 우연히 맞을 뿐, 누가 부분
         *          문자열을 넣으면 조용히 범위를 넘어 읽습니다. 그래서 타입으로 계약을 적어 둡니다.
         */
        const utf8*     _pLabel;
        EditorAssetKind _kind;
        bool            _bOther;
    };

    /**
     * @class EditorAssetTypeRegistry
     * @brief 종류 판별, 패널 제목, 브라우저 필터의 단일 정의입니다.
     */
    class EditorAssetTypeRegistry
    {
    public:
        /** @brief 경로가 지정 종류와 맞는지 여부를 반환합니다. */
        static bool matches( EditorAssetKind kind, string_view path );
        /** @brief 경로가 nullptr 이면 false 입니다. */
        static bool matches( EditorAssetKind kind, const utf8* pPath );
        /** @brief 알려진 애셋 종류 중 하나라도 맞으면 true입니다. */
        static bool matchesAny( string_view path );
        /** @brief 브라우저의 Other 필터입니다. 전용 종류에 걸리지 않으면 true 입니다. */
        static bool matchesOther( string_view path );

        /** @brief 도구 패널 제목입니다. 전용 패널이 없으면 빈 문자열입니다. */
        static const utf8* getPanelTitle( EditorAssetKind kind );
        /** @brief 경로에 대응하는 도구 패널 제목입니다. 없으면 empty입니다. */
        static string_view findPanelTitleForPath( string_view assetPath );

        /** @brief 도구 패널을 여는 접미사 목록입니다. outCount에 개수를 씁니다. */
        static const EditorAssetPanelMapping* getPanelMappings( uint32& outCount );
        /** @brief Assets 메뉴·도킹에 쓸 도구 패널 종류 목록입니다. */
        static const EditorAssetKind* getToolPanelKinds( uint32& outCount );

        /**
         * @brief 도구 패널 제목을 하나씩 넘겨 줍니다 (제목이 비어 있는 종류는 건너뜁니다).
         * @details 호출부가 개수를 받아 인덱스로 돌고 빈 제목을 걸러 내는 대여섯 줄을 매번 다시 쓰고 있었습니다(도킹
         *          레이아웃 · 메뉴바). 순회는 레지스트리의 일이고, 쓰는 쪽은 유효한 제목만 받으면 됩니다.
         */
        template <typename Func>
        static void forEachToolPanelTitle( Func&& func )
        {
            uint32                       kindCount{ 0 };
            const EditorAssetKind* const pKind = getToolPanelKinds( kindCount );
            for ( uint32 index = 0; index < kindCount; ++index )
            {
                const utf8* pTitle = getPanelTitle( pKind[index] );
                if ( StringUtil::isNullOrEmpty( pTitle ) )
                    continue;
                func( pTitle );
            }
        }
        /** @brief 콘텐츠 브라우저 타입 필터 목록입니다. */
        static const EditorAssetBrowserFilter* getBrowserFilters( uint32& outCount );
        /**
         * @brief 지정 종류의 접미사를 outListSuffix 에 더합니다(중복은 건너뜁니다).
         * @details 파일 감시 필터처럼 "이 종류의 파일" 을 골라야 하는 쪽이 확장자 목록을 따로 적지 않게 합니다. 목록이 둘이면
         *          한쪽만 늘어납니다. 실제로 리소스 감시가 `.mat` 만 보고 있어서, 저장소의 `.material` 은 하나도 걸리지
         *          않았습니다.
         */
        static void appendSuffixes( EditorAssetKind kind, vector<string>& outListSuffix );
        /**
         * @brief 바뀐 파일 하나를 핫 리로드가 처리하는 방법(다시 읽을 캐시 · 먼저 돌릴 임포터)입니다. 대상이 아니면 둘 다 nullptr 입니다.
         * @details 새 에셋 종류는 엔진에 `IAssetCache` 를 등록하고 이 표의 줄에 그 이름을 적으면 핫 리로드됩니다.
         *          `AssetHotReload` 에는 종류별 코드가 없습니다 — 거기에 분기를 더하지 말 것.
         */
        static AssetReloadRoute findReloadRoute( string_view path );
        /** @brief 표에 적힌 핫 리로드 캐시 이름을 전부 @p outListKindName 에 더합니다(엔진 등록부와 맞는지 보는 테스트용). */
        static void appendReloadCacheKindNames( vector<string_view>& outListKindName );
        /** @brief 엔진 캐시가 있는 종류(핫 리로드 대상)의 접미사를 @p outListSuffix 에 더합니다(중복은 건너뜁니다). */
        static void appendReloadableSuffixes( vector<string>& outListSuffix );
        /**
         * @brief @p directory 아래(재귀)에서 지정 종류인 파일을 @p outListFilePath 에 더합니다. 폴더가 없으면 false 입니다.
         * @details 판정은 `matches` 그대로다. 예전에는 리소스 카탈로그가 종류마다 확장자 하나(`.prefab.xml` · `.png` · `.hlsl`)로 따로 셌다 —
         *          JSON 프리팹 · `.jpg` 텍스처 · `.hlsli` 는 세지 않았다.
         */
        [[nodiscard]] static bool collectFiles( EditorAssetKind kind, string_view directory, vector<string>& outListFilePath );

        /** @brief 임포트 대화상자용 접미사를 outListExtension에 추가합니다. */
        static void appendImportExtensions( vector<string>& outListExtension );

        /** @brief 워크스페이스 포커스가 지정 종류이면 그 경로, 아니면 empty입니다. */
        static string_view matchingFocusedPath( EditorAssetKind kind );
        /** @brief 포커스 경로·extraToken이 바뀌었으면 ioLastKey를 갱신하고 true입니다. */
        static bool consumeWorkspaceFocusKey( string& ioLastKey, uint64 extraToken );
    };
} // namespace sw::editor
