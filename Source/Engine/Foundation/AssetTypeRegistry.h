/**
 * @file AssetTypeRegistry.h
 * @brief 에셋 종류 하나의 정의(`AssetTypeDef`)와 그 등록표입니다. 종류를 더하는 일은 정의 하나와 등록 줄(`SW_ASSET_TYPE`) 하나입니다.
 * @details 한 종류의 사실(접미사 · 쿠킹 역할 · 판)을 여러 표에 따로 적으면 한쪽만 늘어난다 — 예전에는 `AssetKind` 열거 · 판 상수 표 ·
 *          쿠킹 표의 `cook_suffixes`(JSON) 가 같은 종류를 따로 적었다. 지금은 파생하는 쪽(쿠킹 경로 `AssetCookPath`,
 *          판 검사, 팩 쿠커의 잔재 판정)이 모두 이 표를 읽는다.
 *          - **접미사 우선순위는 이 표가 정렬로 가진다**: 가장 긴 접미사가 먼저, 길이가 같으면 등록 순이다(`.scene.xml` 이 `.scene` 보다 먼저). 표의 적은 순서에 기대지 않는다.
 *          - 종류의 열쇠는 이름(`_name`)이다. 정의는 헤더의 `static constexpr` 라 DLL 마다 사본이 있을 수 있어 주소로 비교하지 않는다.
 *          - 등록 줄은 정의를 가진 기능의 .cpp(캐시가 있으면 캐시 등록 옆)에 둔다. 모듈이 등록한 종류는 등록자가 사라질 때 표에서 빠진다.
 *          - 이름 · 접미사가 겹치는 등록은 표가 받지 않고 문제로 남긴다(`collectProblems` — `AssetTypeRegistryTest` 가 본다).
 *          - **에셋 파일 형식은 XML 이나 바이너리다**. JSON 접미사는 쿠킹 입력(`CookSource` — 쿠킹이 바이너리로 바꾼다)만 받고,
 *            그대로 읽는 접미사(`Plain` · `Cooked`)로는 올리지 못한다(`isSuffixFormatAllowed`). JSON 은 도구 · 설정 데이터의 형식이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 에셋 저작 XML 의 스키마 판(루트 속성 `formatVersion`)입니다. 없으면 세대 0 입니다. */
    using AssetFormatVersion = uint32;

    /** @brief 접미사 하나가 쿠킹에서 맡는 역할입니다. */
    enum class AssetSuffixRole : uint8
    {
        Plain,       ///< 쿠킹하지 않는 에셋 파일(그대로 팩에 든다)
        CookSource,  ///< 쿠커가 쿠킹하는 저작 소스(쿠킹본은 `_cookedSuffix`)
        Cooked,      ///< 쿠커가 만든 쿠킹본 — 스테이징에만 있고, 소스 트리에 있으면 낡은 잔재다
        CookedAlias, ///< 확장자 없는 이름(`.scene`) — 쿠킹본 이름(`.scene.bin`)으로 읽는다
    };

    /** @struct AssetSuffix @brief 종류의 접미사 하나(소문자, 점 포함)와 그 역할입니다. */
    struct AssetSuffix
    {
        string_view     _text;
        AssetSuffixRole _role;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AssetTypeDef
     * @brief 에셋 종류 하나의 정의입니다. 기능 헤더에 `static constexpr` 로 두고 그 .cpp 에서 `SW_ASSET_TYPE` 로 등록합니다.
     */
    struct AssetTypeDef
    {
        string_view        _name;          ///< 종류 이름 — 로그 · 에디터 레이블 키. 등록된 종류끼리 겹치지 않는다
        const AssetSuffix* _pSuffix;       ///< 접미사 목록(파일로 가르지 않는 종류는 nullptr)
        uint32             _suffixCount;   ///< `_pSuffix` 의 수
        string_view        _cookedSuffix;  ///< 쿠킹본 접미사(쿠킹하지 않는 종류는 빈 글)
        AssetFormatVersion _formatVersion; ///< 저작 XML 의 현재 판. 저장 형태가 호환되지 않게 바뀌면 올린다
        string_view        _excludeGroup;  ///< 빌드 타깃이 패키지에서 뺄 수 있는 묶음(`Texture` · `ShaderBinary` · `Audio`). 없으면 빈 글 — 쿠킹 표의 `target_excluded_asset_kinds` 가 이 이름을 가리킨다
    };
} // namespace sw

namespace sw
{
    /** @struct AssetSuffixMatch @brief 경로가 맞은 접미사입니다. 맞은 것이 없으면 둘 다 nullptr 입니다. */
    struct AssetSuffixMatch
    {
        const AssetTypeDef* _pType{ nullptr };
        const AssetSuffix*  _pSuffix{ nullptr };

        bool isMatched() const { return _pType != nullptr; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AssetTypeRegistry
     * @brief 등록된 에셋 종류의 표입니다. 파일 머리말 참고. 정적 초기화와 모듈 적재 · 해제에서 고치고, 그 밖에서는 읽기만 합니다.
     */
    class SW_API AssetTypeRegistry
    {
    public:
        /** @brief 종류를 올립니다. 이름이나 접미사가 이미 올라온 종류와 겹치면 올리지 않고 문제로 남깁니다. */
        static void registerType( const AssetTypeDef& def );
        /** @brief 종류를 내립니다(모듈 해제). 올라오지 않은 종류면 아무 일도 하지 않습니다. */
        static void unregisterType( const AssetTypeDef& def );

        /** @brief 이름이 @p name 인 종류입니다. 없으면 nullptr 입니다. */
        static const AssetTypeDef* findType( string_view name );
        /** @brief 올라온 종류들입니다(올린 순서, 사본). */
        static vector<const AssetTypeDef*> collectTypes();

        /** @brief @p path 끝에 맞는 접미사입니다 — 가장 긴 것이 먼저, 길이가 같으면 등록 순이다. 대소문자는 가리지 않습니다. */
        static AssetSuffixMatch findSuffix( string_view path );
        /** @brief @p path 의 접미사가 종류 @p def 의 것이면 true 입니다(이름으로 비교한다 — 정의는 DLL 마다 사본일 수 있다). 캐시가 "내 종류의 경로인가" 를 볼 때 씁니다. */
        static bool isPathOfType( string_view path, const AssetTypeDef& def );
        /** @brief @p path 가 맞은 종류의 제외 묶음(`_excludeGroup`)입니다. 맞은 종류가 없거나 묶음이 없으면 빈 글입니다. */
        static string_view findExcludeGroup( string_view path );
        /** @brief @p group 을 제외 묶음으로 가진 종류가 올라와 있으면 true 입니다(쿠킹 표가 모르는 묶음 이름을 가리키지 않는지 테스트가 본다). */
        static bool hasExcludeGroup( string_view group );

        /** @brief 올리지 못한 등록(겹친 이름 · 접미사 · JSON 으로 그대로 읽는 접미사)을 사람이 읽는 글로 @p outListProblem 에 더합니다. */
        static void collectProblems( vector<string>& outListProblem );

        /** @brief 에셋 접미사의 형식이 허용되는지입니다. `.json` 으로 끝나는 접미사는 쿠킹 입력(`CookSource`)일 때만 true 입니다. */
        [[nodiscard]] static bool isSuffixFormatAllowed( const AssetSuffix& suffix );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_ASSET_TYPE` 가 두는 정적 등록자입니다. 만들 때 올리고 사라질 때 내립니다. */
    class SW_API AssetTypeRegistrar
    {
    public:
        explicit AssetTypeRegistrar( const AssetTypeDef& def );
        ~AssetTypeRegistrar();
        AssetTypeRegistrar( const AssetTypeRegistrar& )            = delete;
        AssetTypeRegistrar& operator=( const AssetTypeRegistrar& ) = delete;

    private:
        const AssetTypeDef& _def;
    };
} // namespace sw

/**
 * @brief 에셋 종류 @p def 를 등록합니다(정의를 가진 기능의 .cpp, 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다.
 */
#define SW_ASSET_TYPE( id, def )                                     \
    static const ::sw::AssetTypeRegistrar sw_assetTypeRegistrar_##id \
    {                                                                \
        def                                                          \
    }
