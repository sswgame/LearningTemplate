/**
 * @file EditorAssetType.h
 * @brief 에디터 애셋 종류·패널 제목·접미사 SSOT
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"

namespace sw::editor
{
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
         * @brief 콤보에 그대로 넘기는 라벨. **널 종단 문자열이다.**
         * @details 예전에는 `string_view` 였는데 쓰는 쪽이 셋 다 곧바로 `.data()` 를 ImGui 로
         *          넘겼다 — ImGui 는 널 종단을 요구하지만 `string_view` 는 그것을 보장하지 않는다.
         *          지금 표가 전부 리터럴이라 우연히 맞을 뿐, 누가 부분 문자열을 넣으면 조용히
         *          범위를 넘어 읽는다. 타입으로 계약을 적어 둔다.
         */
        const utf8*     _label;
        EditorAssetKind _kind;
        bool            _bOther;
    };

    /**
     * @class EditorAssetTypeRegistry
     * @brief 종류 판별, 패널 제목, 브라우저 필터의 단일 정의
     */
    class EditorAssetTypeRegistry
    {
    public:
        /** @brief 경로가 지정 종류와 맞는지 여부를 반환합니다. */
        static bool matches( EditorAssetKind kind, string_view path );
        /** @brief nullptr이면 false입니다. */
        static bool matches( EditorAssetKind kind, const utf8* pPath );
        /** @brief 알려진 애셋 종류 중 하나라도 맞으면 true입니다. */
        static bool matchesAny( string_view path );
        /** @brief 브라우저 Other 필터 — 전용 종류에 안 걸리면 true입니다. */
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
         * @details 호출부가 개수를 받아 인덱스로 돌고 빈 제목을 걸러내는 대여섯 줄을 매번 다시
         *          쓰고 있었다(도킹 레이아웃·메뉴바). 순회는 레지스트리의 일이고, 쓰는 쪽은
         *          유효한 제목만 받으면 된다.
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
        /** @brief 임포트 대화상자용 접미사를 outListExtension에 추가합니다. */
        static void appendImportExtensions( vector<string>& outListExtension );

        /** @brief 워크스페이스 포커스가 지정 종류이면 그 경로, 아니면 empty입니다. */
        static string_view matchingFocusedPath( EditorAssetKind kind );
        /** @brief 포커스 경로·extraToken이 바뀌었으면 ioLastKey를 갱신하고 true입니다. */
        static bool consumeWorkspaceFocusKey( string& ioLastKey, uint64 extraToken );
    };
} // namespace sw::editor
