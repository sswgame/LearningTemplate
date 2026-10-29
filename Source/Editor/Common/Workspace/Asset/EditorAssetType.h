/**
 * @file EditorAssetType.h
 * @brief 에디터의 애셋 종류입니다. 종류는 닫힌 열거가 아니라 등록 줄(`SW_EDITOR_ASSET_TYPE`)이고, 열쇠는 엔진 에셋 종류 정의(`AssetTypeDef::_name`)입니다.
 * @details 언리얼의 `UAssetDefinition`(예전 `IAssetTypeActions`)과 같다 — 종류 하나의 에디터 쪽 사실(이름 · 아이콘 · 색 · 판정 규칙 · 도구 패널 ·
 *          새 에셋 틀 · 동작)을 그 종류의 파일 한 곳에서 등록한다. 플러그인(GameFramework 에디터 모듈)도 같은 줄로 종류를 더한다.
 *          예전에는 `enum class EditorAssetType` 와 표 두 개(EditorAssetType.cpp)가 엔진 `AssetTypeRegistry` 를 다시 적었고, 플러그인 패널은
 *          코어 열거 값(`Data`)을 빌려 써야 해 종류를 더할 수 없었다.
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

#include "Engine/Foundation/AssetTypeRegistry.h"

namespace sw
{
    class AssetPaths;
    struct EngineDefaultAssets;

    class DataPath;
} // namespace sw

namespace sw::editor
{
    class IEditorAssetTypeActions;

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

    /**
     * @class EditorAssetType
     * @brief 에디터 애셋 종류 하나를 가리키는 값입니다. 빈 값이 "알 수 없음" 입니다.
     * @details 열쇠는 종류 등록의 id 다. 엔진 포맷이 있는 종류는 엔진 정의의 이름(`AssetTypeDef::_name`)을 id 로 쓰고(`EditorAssetType{ Material::kAssetType }`),
     *          엔진 정의가 없는 에디터 전용 종류(저작 셰이더 · 카탈로그 데이터)만 `EditorAssetTypeIDs` 의 이름을 쓴다.
     *          id 는 글을 복사해 둔다(`fixed_string`). 퀵 런처 색인처럼 오래 사는 값이 종류를 등록한 플러그인이 내려간 뒤에도 그 DLL 의 글을 가리키지 않게 하려는 것이다.
     */
    class EditorAssetType
    {
    public:
        /** @brief id 의 최대 길이입니다. 엔진 종류 이름 중 가장 긴 것(`VertexAnimationCookList`)보다 넉넉하다. */
        static constexpr uint32 kMaxIDLength = 40;

        EditorAssetType() = default;
        /** @brief 엔진 에셋 종류 정의가 가리키는 종류입니다. */
        explicit EditorAssetType( const AssetTypeDef& def )
            : _id{ def._name }
        {
        }
        /** @brief id 로 가리킵니다. 엔진 정의가 없는 종류(`EditorAssetTypeIDs`)와 등록 줄의 id 에 씁니다. */
        explicit EditorAssetType( string_view id )
            : _id{ id }
        {
        }

        /** @brief 어떤 종류도 가리키지 않으면 true 입니다. */
        bool isUnknown() const { return _id.empty(); }
        /** @brief 종류 등록의 id 입니다. */
        string_view getID() const { return _id.view(); }

        bool operator==( const EditorAssetType& other ) const { return _id == other._id; }
        bool operator!=( const EditorAssetType& other ) const { return ( *this == other ) == false; }

    private:
        fixed_string<kMaxIDLength> _id;
    };

    /** @brief 엔진 에셋 종류 정의가 없는 에디터 전용 종류의 id 입니다. 엔진 종류 이름과 겹치지 않습니다(겹치면 등록이 거절된다). */
    struct EditorAssetTypeIDs
    {
        static constexpr const utf8* kShader = "Shader"; ///< 저작 셰이더 소스(`.hlsl` · `.hlsli`). 엔진 종류는 쿠킹된 바이너리(`ShaderBinary`)뿐이다
        static constexpr const utf8* kData   = "Data";   ///< 카탈로그 데이터(`.xml` · `.json`)와 전용 종류가 없는 데이터 문서. 전용 창이 접미사로 고르는 문서(`acceptsDocumentPath`)가 여기 든다
    };

    /**
     * @brief 소스 파일을 임포트하는 임포터입니다. 처리했으면 true 를 반환하며, 임포트했거나 임포트하지 않는다고 알린 경우가 모두 해당합니다. 이때는 그 파일의 캐시 리로드를 하지 않습니다.
     * @details 경로 상태 @p assetPaths 는 부른 쪽(에디터 스레드)이 찾아 넘긴다 — 워커 임포터는 에디터 서비스 범위를 읽지 않는다.
     */
    using AssetSourceImporterFunc = bool ( * )( const AssetPaths& assetPaths, string_view relativePath );

    /**
     * @enum AssetImportThread
     * @brief 소스 임포터(`AssetSourceImporterFunc`)가 도는 스레드입니다. 긴 일은 에디터 스레드에서 동기로 돌지 않는다.
     */
    enum class AssetImportThread : uint8
    {
        Editor, ///< 에디터 스레드에서 바로 부릅니다. 에디터 서브시스템을 건드리는 짧은 일(테마 토큰 표시)만 여기에 둡니다.
        Worker, ///< 백그라운드 잡(`EditorAssetImportJob`)이 워커에서 부릅니다. 임포터는 UI 와 공유 상태를 건드리지 않습니다. 결과 적용(캐시 리로드 · 씬 알림)은 에디터 스레드가 합니다.
    };

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

        const utf8*             _pCacheKindName{ nullptr };                 ///< 다시 읽을 엔진 캐시(`IAssetCache::getAssetKindName`). nullptr 이면 핫 리로드 대상이 아니고, 빈 문자열이면 `kAnyCacheHoldingPath` 이다
        AssetSourceImporterFunc _pfnImportSource{ nullptr };                ///< 캐시가 다시 읽기 전에 실행하는 임포터. nullptr 이면 바로 캐시가 다시 읽는다
        AssetImportThread       _importThread{ AssetImportThread::Editor }; ///< 임포터가 도는 스레드. 임포터가 있을 때만 뜻이 있다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 판정 규칙이 경로를 보는 방법입니다. */
    enum class EditorAssetMatchMode : uint8
    {
        EngineType,     ///< 엔진 에셋 종류 정의(`AssetTypeDef`)의 여는 접미사(`Plain` · `CookSource`)를 그대로 쓴다. 접미사 철자를 에디터가 따로 적지 않는다
        CookableSource, ///< 엔진의 쿠킹 규칙(`AssetCookPath::isCookableSource`)을 따른다. 쿠커가 쿠킹하는 저작 소스만 해당한다(씬 · 프리팹)
        EndsWith,       ///< 경로가 접미사 하나로 끝난다(`.theme.xml` 처럼 점이 둘 이상인 이름)
        Extension,      ///< 경로의 확장자가 목록 하나다(`.png`)
    };

    /**
     * @brief 한 경로에 규칙 여럿이 맞을 때 이기는 차례입니다. 작은 값이 이기고, 같으면 더 긴 접미사가, 그래도 같으면 종류 순서(`_order`)가 이깁니다.
     * @details 예전에는 한 표의 줄 순서가 우선순위였다. 종류가 파일마다 등록되면 줄 순서가 없으므로 규칙이 자기 자리를 적는다.
     */
    enum class EditorAssetMatchPriority : uint8
    {
        Path,     ///< 위치나 쿠킹 규칙까지 보는 규칙(씬 · 프리팹의 쿠킹 소스, `heightfields_raw/` 의 높이장 원본). 같은 `.png` 라도 텍스처보다 먼저다
        Suffix,   ///< 종류의 접미사(엔진 정의 · 저작 원본 확장자). 대부분의 규칙
        Fallback, ///< 다른 종류가 먼저 가져가는 확장자를 함께 받는 규칙(스프라이트 클립의 이미지)
        Catchall, ///< 아무 종류도 아닌 `.xml` · `.json` 을 받는 규칙(Data)
    };

    /**
     * @struct EditorAssetMatchRule
     * @brief 경로 판정 규칙 하나입니다. 어느 경로가 이 종류인지와 핫 리로드가 그 파일을 어떻게 다시 읽는지를 담습니다.
     * @details 한 종류가 규칙을 둘 이상 가질 수 있다. 텍스처는 저작 이미지 확장자와 엔진 정의를 함께 쓴다. 만드는 함수(`engineType` 등)로 채운다.
     *          엔진 정의를 가리키는 표는 `constexpr` 가 아니다 — 정의는 DLL 에서 오는 `static constexpr` 멤버라 주소가 상수식이 아니다. 등록 줄과 같은 파일에 두면 정적 초기화 순서가 맞다.
     */
    struct SW_EDITOR_API EditorAssetMatchRule
    {
        EditorAssetMatchMode     _mode{ EditorAssetMatchMode::EngineType };
        EditorAssetMatchPriority _priority{ EditorAssetMatchPriority::Suffix };
        const AssetTypeDef*      _pEngineType{ nullptr }; ///< `EngineType` · `CookableSource` 의 엔진 종류. 그 밖의 방식은 nullptr
        const string_view*       _pSuffix{ nullptr };     ///< `EndsWith` · `Extension` 의 접미사. 그 밖의 방식은 nullptr
        uint32                   _suffixCount{ 0 };
        bool ( *_pfnAcceptPath )( string_view path ){ nullptr }; ///< 접미사가 맞은 뒤 더 보는 조건(높이장 원본 위치). nullptr 이면 접미사만 본다
        AssetReloadRoute _reload{};                              ///< 이 규칙의 파일을 핫 리로드가 다시 읽는 길. 비면 핫 리로드 대상이 아니다

        /** @brief 엔진 종류 @p def 의 여는 접미사로 판정합니다. */
        static EditorAssetMatchRule engineType( const AssetTypeDef& def, const AssetReloadRoute& reload = {} );
        /** @brief 엔진 종류 @p def 의 쿠킹 소스로 판정합니다(`AssetCookPath`). 쿠커가 쿠킹하는 이름이 곧 에디터가 여는 이름이다. */
        static EditorAssetMatchRule cookableSource( const AssetTypeDef& def, const AssetReloadRoute& reload = {} );
        /** @brief 접미사 목록(@p pSuffix, @p count)으로 판정합니다. */
        static EditorAssetMatchRule suffixes( EditorAssetMatchMode mode, const string_view* pSuffix, uint32 count, EditorAssetMatchPriority priority, const AssetReloadRoute& reload = {} );
    };

    /**
     * @brief 콘텐츠 브라우저 Add 메뉴가 만드는 새 에셋 하나의 틀입니다. 종류 등록(`EditorAssetTypeRegistration::_pNewAssetTemplate`)이 가리킵니다.
     * @details 글은 `_pfnSourcePath`(베낄 엔진 기본 에셋 항목) 와 `_pText`(그대로 쓰는 글) 가운데 하나만 채웁니다.
     */
    struct EditorNewAssetTemplate
    {
        const utf8* _pID{ nullptr };     ///< 틀 사이에서 겹치지 않는 이름. Add 메뉴 표지는 `contentBrowser.add.<id>` 다
        const utf8* _pLabel{ nullptr };  ///< Add 메뉴 라벨
        const utf8* _pStem{ nullptr };   ///< 기본 이름 줄기(`new_material`)
        const utf8* _pSuffix{ nullptr }; ///< 붙는 확장자(`.material`)
        /** @brief 베낄 엔진 기본 에셋 항목을 고릅니다. 경로의 원본은 `enginedefaultassets.xml` 이다. 손으로 만든 글이 퍼뮤테이션 같은 항목을 빠뜨리기 쉬운 종류가 쓴다 */
        const DataPath& ( *_pfnSourcePath )(const EngineDefaultAssets&){ nullptr };
        const utf8* _pText{ nullptr };          ///< 그대로 쓰는 글
        const utf8* _pNameAttribute{ nullptr }; ///< 베낀 글의 이름 속성 전체(`name="DefaultMaterial"`). 새 에셋은 `name="<줄기>"` 로 바꾼다. nullptr 이면 그대로 둔다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorAssetTypeRegistration
     * @brief 애셋 종류 하나의 등록 줄입니다. 이름, 아이콘, 색, 패널, 필터, 임포트 여부, 판정 규칙, 새 에셋 틀, 동작을 담습니다.
     * @details `_pID` 가 종류의 열쇠(`EditorAssetType::getID`)이고 `_order` 가 브라우저 필터 · 도구 패널 · 카탈로그의 표시 순서다.
     *          종류를 더하려면 그 종류의 파일에 `SW_EDITOR_ASSET_TYPE` 한 줄을 둔다. 아이콘, 색, 퀵 런처 분류, 카탈로그를 종류별 분기로 따로 적지 않는다.
     *          문자열과 표는 등록이 살아 있는 동안 있어야 한다(리터럴 · 파일 범위 표).
     */
    struct EditorAssetTypeRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "AssetType";

        const utf8*                    _pDisplayName;      ///< 단수 이름. 퀵 런처 분류와 배지에 쓴다
        const utf8*                    _pBrowserLabel;     ///< 콘텐츠 브라우저 필터와 리소스 카탈로그의 라벨
        const utf8*                    _pPanelTitle;       ///< 전용 도구 패널 제목. nullptr 이면 패널이 없다
        const utf8*                    _pIcon;             ///< 에디터 아이콘 글리프(editoricon::k…, UTF-8)
        Color                          _color;             ///< 목록과 배지의 색. `_bAccentColor` 이면 쓰지 않는다
        bool                           _bAccentColor;      ///< 고정 색 대신 테마 액센트 색을 쓴다
        bool                           _bOtherExcluded;    ///< 브라우저 "Other" 필터에서 뺄지 정한다
        bool                           _bImportable;       ///< 임포트 대화상자의 확장자 목록에 넣을지 정한다
        const EditorAssetMatchRule*    _pRule;             ///< 판정 규칙. 하나 이상이어야 한다(없으면 어떤 경로에도 맞지 않는다)
        uint32                         _ruleCount;         ///< `_pRule` 의 수
        const EditorNewAssetTemplate*  _pNewAssetTemplate; ///< Add 메뉴의 새 에셋 틀. nullptr 이면 Add 메뉴에 없다
        const IEditorAssetTypeActions* _pActions;          ///< 썸네일 · 열기 · 뷰포트 드롭 동작. nullptr 이면 일반 동작이다

        /** @brief 이 종류를 가리키는 값입니다. */
        EditorAssetType getType() const { return EditorAssetType{ string_view{ _pID } }; }
    };
} // namespace sw::editor

/**
 * @brief 애셋 종류 하나를 등록합니다. 종류의 파일(보통 `Editor/AssetActions/<종류>AssetTypeActions.cpp`, 플러그인은 그 모듈 파일)에 한 줄 둡니다.
 * @param name 파일 안에서 유일한 식별자 조각
 * @param pID 종류의 id. 엔진 포맷이 있으면 `X::kAssetType._name.data()`, 없으면 `EditorAssetTypeIDs::k…`
 * @details 나머지 인자는 `EditorAssetTypeRegistration` 의 필드 순서다(표시 이름 · 브라우저 라벨 · 패널 제목 · 아이콘 · 색 · 액센트 · Other 제외 · 임포트 · 규칙 표 · 규칙 수 · 새 에셋 틀 · 동작).
 */
#define SW_EDITOR_ASSET_TYPE( name, pID, order, ... ) SW_EDITOR_REGISTER( ::sw::editor::EditorAssetTypeRegistration, AssetType_##name, { pID, order }, __VA_ARGS__ )

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
     * @brief 등록된 애셋 종류로 경로를 판정하고, 패널 제목과 브라우저 필터를 냅니다. 종류별 분기는 없습니다.
     * @details 종류는 모듈이 올라오고 내려갈 때 바뀐다(`getGeneration`). 목록을 만들어 두는 쪽은 세대가 바뀌면 다시 만든다.
     *          판정은 등록만 읽으므로 워커(퀵 런처 색인)에서도 부른다 — 등록은 모듈 적재 · 해제 때만 바뀌고, 그때는 비동기 작업을 먼저 멈춘다.
     */
    class SW_EDITOR_API EditorAssetTypeRegistry
    {
    public:
        /** @brief 경로가 지정한 종류의 규칙 하나에라도 맞으면 true 입니다. 우선순위는 보지 않습니다(텍스처이면서 스프라이트 이미지인 경로가 둘 다 맞다). */
        static bool matches( const EditorAssetType& kind, string_view path );
        /** @brief 경로가 nullptr 이면 false 입니다. */
        static bool matches( const EditorAssetType& kind, const utf8* pPath );
        /** @brief 등록된 종류 중 하나라도 해당하면 true 입니다. */
        static bool matchesAny( string_view path );
        /** @brief 브라우저의 Other 필터 조건입니다. 전용 종류에 해당하지 않으면 true 입니다. */
        static bool matchesOther( string_view path );

        /**
         * @brief 경로가 속한 종류의 등록을 반환합니다. 해당하는 종류가 없으면 nullptr 입니다.
         * @details 맞은 규칙 가운데 우선순위(`EditorAssetMatchPriority`)가 앞선 것, 같으면 더 긴 접미사, 그래도 같으면 종류 순서가 앞선 것이 이긴다.
         *          `.animgraph.xml` 은 Data 의 `.xml` 보다 AnimationGraph 로, 이미지는 SpriteClip 보다 Texture 로 판정한다.
         *          아이콘, 색, 퀵 런처 분류, 카탈로그, 썸네일, 열기, 드롭이 모두 이 함수 하나로 종류를 정한다.
         */
        static const EditorAssetTypeRegistration* findKindInfoForPath( string_view path );
        /** @brief 경로가 속한 종류입니다(`findKindInfoForPath`). 해당하는 종류가 없으면 빈 값(알 수 없음)입니다. */
        static EditorAssetType findKind( string_view path );

        /** @brief 종류의 등록을 반환합니다. 알 수 없음이거나 등록되지 않은 종류면 nullptr 입니다. */
        static const EditorAssetTypeRegistration* findKindInfo( const EditorAssetType& kind );
        /** @brief 등록된 종류의 수입니다. */
        static uint32 getKindCount();
        /** @brief 표시 순서로 @p index 번째 종류의 등록입니다. */
        static const EditorAssetTypeRegistration& getKindAt( uint32 index );
        /** @brief 종류 등록이 바뀔 때마다(모듈 적재 · 해제) 올라가는 세대입니다. 필터 목록처럼 만들어 두는 쪽이 비교합니다. */
        static uint32 getGeneration();

        /** @brief 도구 패널 제목입니다. 전용 패널이 없으면 빈 문자열입니다. */
        static const utf8* getPanelTitle( const EditorAssetType& kind );
        /** @brief 경로를 여는 도구 패널의 종류입니다(가장 긴 접미사가 이긴다). 전용 패널이 없으면 알 수 없음입니다. 여는 요청은 `EditorWorkspace::requestOpenDocumentPanel` 입니다. */
        static EditorAssetType findPanelKindForPath( string_view assetPath );

        /**
         * @brief 도구 패널이 있는 종류와 그 제목을 표시 순서대로 하나씩 콜백(`func( kind, pTitle )`)에 넘깁니다. 제목이 비어 있는 종류는 건너뜁니다.
         * @details 순회는 레지스트리가 맡고, 쓰는 쪽(메뉴바)은 유효한 제목만 받는다. 여는 요청은 제목이 아니라 종류로 한다(`requestOpenDocumentPanel`).
         */
        template <typename Func>
        static void forEachToolPanel( Func&& func )
        {
            const uint32 kindCount = getKindCount();
            for ( uint32 index = 0; index < kindCount; ++index )
            {
                const EditorAssetTypeRegistration& registration = getKindAt( index );
                if ( StringUtil::isNullOrEmpty( registration._pPanelTitle ) )
                    continue;
                func( registration.getType(), registration._pPanelTitle );
            }
        }
        /** @brief 콘텐츠 브라우저 타입 필터 목록(All · 종류마다 하나 · Other)을 @p outListFilter 에 씁니다. 등록이 바뀌면(`getGeneration`) 다시 부릅니다. */
        static void collectBrowserFilters( vector<EditorAssetBrowserFilter>& outListFilter );
        /** @brief Add 메뉴의 새 에셋 틀을 종류 순서대로 @p outListTemplate 에 씁니다. */
        static void collectNewAssetTemplates( vector<const EditorNewAssetTemplate*>& outListTemplate );
        /** @brief 경로가 속한 종류의 동작입니다. 종류가 없거나 동작을 등록하지 않았으면 nullptr 입니다. */
        static const IEditorAssetTypeActions* findActionsForPath( string_view path );
        /**
         * @brief 지정한 종류의 접미사를 outListSuffix 에 추가합니다. 이미 있는 접미사는 건너뜁니다.
         * @details 파일 감시 필터처럼 "이 종류의 파일" 을 골라야 하는 쪽이 확장자 목록을 따로 적지 않게 한다. 목록이 둘이면
         *          한쪽만 늘어나 감시가 일부 확장자를 놓친다. 예를 들어 감시가 `.mat` 만 보면 `.material` 파일은 하나도 감지하지 못한다.
         */
        static void appendSuffixes( const EditorAssetType& kind, vector<string>& outListSuffix );
        /**
         * @brief 변경된 파일 하나를 핫 리로드가 처리하는 방법을 찾습니다. 다시 읽을 캐시와 먼저 실행할 임포터가 결과입니다. 핫 리로드 대상이 아니면 둘 다 nullptr 입니다.
         * @details 새 에셋 종류는 엔진에 `IAssetCache` 를 등록하고 그 종류의 규칙(`EditorAssetMatchRule::_reload`)에 그 이름을 적으면 핫 리로드된다.
         *          `AssetHotReload` 에는 종류별 코드가 없으므로 거기에 분기를 추가하지 않는다.
         */
        static AssetReloadRoute findReloadRoute( string_view path );
        /** @brief 엔진 캐시가 있는 종류(핫 리로드 대상)의 접미사를 @p outListSuffix 에 추가합니다. 이미 있는 접미사는 건너뜁니다. */
        static void appendReloadableSuffixes( vector<string>& outListSuffix );

        /** @brief 임포트 대화상자용 접미사를 outListExtension 에 추가합니다. */
        static void appendImportExtensions( vector<string>& outListExtension );

        /** @brief 워크스페이스 포커스가 지정한 종류이면 그 경로를 반환하고, 아니면 빈 문자열을 반환합니다. */
        static string_view matchingFocusedPath( const EditorAssetType& kind );
        /** @brief 포커스 경로나 extraToken 이 바뀌었으면 ioLastKey 를 갱신하고 true 를 반환합니다. */
        static bool consumeWorkspaceFocusKey( string& ioLastKey, uint64 extraToken );
    };
} // namespace sw::editor
