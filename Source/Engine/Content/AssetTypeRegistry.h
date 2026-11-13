/**
 * @file AssetTypeRegistry.h
 * @brief 에셋 종류 하나의 정의(`AssetTypeDef`)와 그 등록표입니다. 종류를 더하는 일은 등록 줄(`SW_ASSET_TYPE`) 하나입니다.
 * @details 한 종류의 사실(접미사 · 쿠킹 역할 · 판 · 경로 규칙 · 로더 · 다시 읽기 방식 · 메모리 태그)을 여러 표에 따로 적으면 한쪽만 늘어난다 — 예전에는 `AssetKind` 열거 · 판 상수 표 ·
 *          쿠킹 표의 `cook_suffixes`(JSON) · 데이터 종류 표(`AssetDataKind`) · 캐시 특성 표(이름 · 로더 · 다시 읽기 방식)가 같은 종류를 따로 적었다(캐시 이름 `Rig` 과 종류 이름 `RigAsset` 처럼 갈라졌다). 지금은 파생하는 쪽(쿠킹 경로 `AssetCookPath`,
 *          판 검사, 팩 쿠커의 잔재 판정, 카탈로그 편집기, 공유 캐시)이 모두 이 표를 읽는다. 언리얼 `UAssetManager` 의 Primary Asset Type 설정 · 유니티 `ScriptableObject` 타입 목록에 해당한다(기억에 의존).
 *          - **경로 규칙은 접미사 표와 다르다**: 접미사는 쿠킹 역할을 정하고, 경로 규칙(`AssetPathRule`)은 접미사로 가를 수 없는 데이터 파일(`/data/items.xml`)을 찾는다. 경로 규칙이 있는 종류만 카탈로그 편집기에 나온다.
 *          - **로더는 등록 줄의 사본에만 있다**: 헤더의 `kAssetType` 은 쿠킹이 쓰는 사실만 적고, .cpp 의 등록 줄이 `withLoader` · `withPathRule` 로 더한 사본을 표에 올린다(로더 함수는 기능의 .cpp 에 있다).
 *            로더가 필요한 쪽은 `findType` 으로 표에서 찾는다.
 *          - **접미사 우선순위는 이 표가 정렬로 가진다**: 가장 긴 접미사가 먼저, 길이가 같으면 등록 순이다(`.scene.xml` 이 `.scene` 보다 먼저). 표의 적은 순서에 기대지 않는다.
 *          - 종류의 열쇠는 이름(`_name`)이다. 정의는 헤더의 `static constexpr` 라 DLL 마다 사본이 있을 수 있어 주소로 비교하지 않는다.
 *          - 등록 줄은 정의를 가진 기능의 .cpp 에 둔다. 공유 캐시가 있는 종류는 캐시 등록 줄(`SW_SHARED_ASSET_CACHE` · `SW_MODULE_SHARED_ASSET_CACHE`)이 같은 정의로 이 등록을 대신한다 —
 *            캐시의 종류 이름 · 로더 · 다시 읽기 방식이 이 정의 한 벌에서 나온다. 모듈이 등록한 종류는 등록자가 사라질 때 표에서 빠진다.
 *          - 이름 · 접미사가 겹치는 등록은 표가 받지 않고 문제로 남긴다(`collectProblems` — `AssetTypeRegistryTest` 가 본다).
 *          - **에셋 파일 형식은 XML 이나 바이너리다**. JSON 접미사는 쿠킹 입력(`CookSource` — 쿠킹이 바이너리로 바꾼다)만 받고,
 *            그대로 읽는 접미사(`Plain` · `Cooked`)로는 올리지 못한다(`isSuffixFormatAllowed`). JSON 은 도구 · 설정 데이터의 형식이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/MemoryTag.h"

namespace sw
{
    class AssetID;

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
    /** @brief 파일이 바뀐 에셋을 쥔 쪽에 어떻게 보이는가입니다. */
    enum class SharedAssetReloadMode : uint8
    {
        InPlace, ///< 쥔 객체를 제자리에서 바꾼다(`SharedAssetTable::reloadShared`). 모두가 다음 읽기부터 새 내용을 본다
        Detach,  ///< 표에서 떼어 낸다(`SharedAssetTable::detachShared`). 쥔 쪽은 옛 객체를 끝까지 쥐고, 다시 `acquire` 하면 새 객체를 받는다
    };

    /**
     * @struct AssetPathRule
     * @brief 데이터 파일 종류가 어느 경로의 파일인지 적은 선언입니다(접두 · 포함 · 접미, 대소문자 무시). 빈 칸은 보지 않습니다.
     * @details 함수로 적으면 에디터가 규칙을 보일 수 없고 두 종류가 같은 파일에 맞는지 따질 수 없다. 한 파일은 정확히 한 종류에만 맞아야 한다(`AssetTypeRegistry::collectPathMatches` 가 둘 이상이면 테스트가 진다).
     */
    struct SW_API AssetPathRule
    {
        string_view _prefix{};   ///< 리소스 id 가 이것으로 시작해야 합니다(`game/`)
        string_view _contains{}; ///< 리소스 id 가 이것을 담아야 합니다(`/data/appearance/`)
        string_view _suffix{};   ///< 리소스 id 가 이것으로 끝나야 합니다(`/data/items.xml`)

        /** @brief 규칙이 하나도 없으면 true 입니다 — 어떤 파일에도 맞지 않습니다(카탈로그가 아닌 종류). */
        [[nodiscard]] constexpr bool isEmpty() const { return _prefix.empty() && _contains.empty() && _suffix.empty(); }
        /** @brief @p assetID 가 적힌 규칙에 모두 맞으면 true 입니다(대소문자 무시). */
        [[nodiscard]] bool matches( string_view assetID ) const;
    };

    /**
     * @struct AssetTypeLoader
     * @brief 종류의 진짜 로더와 읽는 방식입니다. 검증 전용 로더는 없다 — 스키마 테스트와 카탈로그 편집기의 검사(`AssetTypeRegistry::checkLoad`)도, 공유 캐시(`SharedAssetCache`)도 같은 함수를 부른다.
     * @details 표는 형식을 모르므로 함수를 지운 꼴(`void*`)로 든다. 형식을 아는 쪽은 `of` · `with` 를 고르는 등록 줄뿐이고, 캐시는 같은 이름으로 인스턴스화한 자기 형식의 칸을 넘긴다.
     */
    struct AssetTypeLoader
    {
        using LoadIntoFunction  = bool ( * )( const AssetID& assetID, void* pOutAsset );
        using CheckLoadFunction = bool ( * )( const AssetID& assetID );

        LoadIntoFunction      _pLoadInto{ nullptr };                         ///< 만들어 둔 에셋에 읽습니다(공유 캐시). 읽지 못하면 오류를 남기고 false 입니다
        CheckLoadFunction     _pCheckLoad{ nullptr };                        ///< 새 에셋에 읽어 보고 버립니다. 모르는 키 · 열거자 · 참조는 로더가 알리고 false 입니다
        MemoryTag             _memoryTag{ MemoryTag::Asset };                ///< 읽는 동안 거는 할당 태그
        SharedAssetReloadMode _reloadMode{ SharedAssetReloadMode::InPlace }; ///< 공유 캐시의 다시 읽기 방식

        /** @brief `bool TAsset::loadFromAsset( const AssetID& )` 로 읽는 종류입니다. */
        template <typename TAsset>
        [[nodiscard]] static constexpr AssetTypeLoader of( MemoryTag memoryTag = MemoryTag::Asset, SharedAssetReloadMode reloadMode = SharedAssetReloadMode::InPlace )
        {
            return with<TAsset, &loadFromAssetOf<TAsset>>( memoryTag, reloadMode );
        }

        /** @brief 문맥이 더 필요해 함수 @p pfnLoad 로 읽는 종류입니다. */
        template <typename TAsset, bool ( *pfnLoad )( const AssetID&, TAsset& )>
        [[nodiscard]] static constexpr AssetTypeLoader with( MemoryTag memoryTag = MemoryTag::Asset, SharedAssetReloadMode reloadMode = SharedAssetReloadMode::InPlace )
        {
            return AssetTypeLoader{ &loadInto<TAsset, pfnLoad>, &checkLoad<TAsset, pfnLoad>, memoryTag, reloadMode };
        }

    private:
        template <typename TAsset>
        [[nodiscard]] static bool loadFromAssetOf( const AssetID& assetID, TAsset& outAsset )
        {
            return outAsset.loadFromAsset( assetID );
        }

        template <typename TAsset, bool ( *pfnLoad )( const AssetID&, TAsset& )>
        [[nodiscard]] static bool loadInto( const AssetID& assetID, void* pOutAsset )
        {
            return pfnLoad( assetID, *static_cast<TAsset*>( pOutAsset ) );
        }

        template <typename TAsset, bool ( *pfnLoad )( const AssetID&, TAsset& )>
        [[nodiscard]] static bool checkLoad( const AssetID& assetID )
        {
            TAsset asset;
            return pfnLoad( assetID, asset );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AssetTypeDef
     * @brief 에셋 종류 하나의 정의입니다. 쿠킹이 쓰는 사실은 기능 헤더의 `static constexpr kAssetType` 에 두고, .cpp 의 `SW_ASSET_TYPE` 가 경로 규칙 · 로더를 더해 올립니다. 헤더 정의가 필요 없는 데이터 파일은 등록 줄에서 바로 만듭니다.
     */
    struct AssetTypeDef
    {
        string_view        _name{};                  ///< 종류 이름 — 로그 · 에디터 레이블 키 · 공유 캐시의 종류 이름. 등록된 종류끼리 겹치지 않는다(대소문자 무시)
        const AssetSuffix* _pSuffix{ nullptr };      ///< 접미사 목록(파일로 가르지 않는 종류는 nullptr)
        uint32             _suffixCount{ 0 };        ///< `_pSuffix` 의 수
        string_view        _cookedSuffix{};          ///< 쿠킹본 접미사(쿠킹하지 않는 종류는 빈 글)
        AssetFormatVersion _formatVersion{ 0 };      ///< 저작 XML 의 현재 판. 저장 형태가 호환되지 않게 바뀌면 올린다
        string_view        _excludeGroup{};          ///< 빌드 타깃이 패키지에서 뺄 수 있는 묶음(`Texture` · `ShaderBinary` · `Audio`). 없으면 빈 글 — 쿠킹 표의 `target_excluded_asset_kinds` 가 이 이름을 가리킨다
        AssetPathRule      _pathRule{};              ///< 접미사로 가를 수 없는 데이터 파일의 경로 규칙(`/data/items.xml`). 비어 있으면 카탈로그가 아니다
        AssetTypeLoader    _loader{};                ///< 진짜 로더와 읽는 방식(등록 줄이 넣는다)
        const string_view* _pBuiltinName{ nullptr }; ///< 파일이 아니라 엔진이 만드는 에셋의 이름(내장 도형 `Cube`). 없으면 nullptr
        uint32             _builtinNameCount{ 0 };   ///< `_pBuiltinName` 의 수

        /**
         * @brief @p rule 을 경로 규칙으로 가진 사본입니다. 카탈로그 종류는 헤더의 `kAssetType` 에 규칙을 적어(`makeDataFile`) 타입 있는 참조(`AssetRef<T>`)가 경로를 가른다.
         */
        [[nodiscard]] constexpr AssetTypeDef withPathRule( const AssetPathRule& rule ) const
        {
            AssetTypeDef copy = *this;
            copy._pathRule    = rule;
            return copy;
        }
        /**
         * @brief @p arrName 을 내장 에셋 이름으로 가진 사본입니다(헤더의 `kAssetType`).
         * @details 내장 이름은 철자 그대로만 맞는다(대소문자를 가리지 않으면 같은 에셋을 여러 철자로 적어 캐시 자리가 갈린다 — `MeshUtil`). 모르는 이름은 받지 않는다.
         *          언리얼은 내장 도형을 엔진 콘텐츠의 진짜 에셋(`/Engine/BasicShapes/Cube`)으로, Godot 은 `PrimitiveMesh` 리소스로 둔다(기억에 의존) — 여기는 파일이 없으므로 종류가 이름 목록을 가진다.
         */
        template <uint32 Count>
        [[nodiscard]] constexpr AssetTypeDef withBuiltinNames( const string_view ( &arrName )[Count] ) const
        {
            AssetTypeDef copy      = *this;
            copy._pBuiltinName     = arrName;
            copy._builtinNameCount = Count;
            return copy;
        }
        /** @brief @p name 이 이 종류의 내장 에셋 이름이면 true 입니다(철자 그대로). */
        [[nodiscard]] constexpr bool isBuiltinName( string_view name ) const
        {
            for ( uint32 index = 0; index < _builtinNameCount; ++index )
            {
                if ( _pBuiltinName[index] == name )
                    return true;
            }
            return false;
        }
        /** @brief 경로로 이 종류의 에셋인지 가를 수 있으면(접미사 · 경로 규칙 · 내장 이름 중 하나가 있으면) true 입니다. 가를 수 없는 종류는 타입 있는 참조의 대상이 될 수 없다. */
        [[nodiscard]] constexpr bool canTellPath() const { return _suffixCount > 0 || _pathRule.isEmpty() == false || _builtinNameCount > 0; }
        /** @brief @p loader 를 로더로 가진 사본입니다(등록 줄). */
        [[nodiscard]] constexpr AssetTypeDef withLoader( const AssetTypeLoader& loader ) const
        {
            AssetTypeDef copy = *this;
            copy._loader      = loader;
            return copy;
        }
        /** @brief 접미사 · 쿠킹이 없는 데이터 파일 종류입니다(등록 줄). */
        [[nodiscard]] static constexpr AssetTypeDef makeDataFile( string_view name, const AssetPathRule& rule, const AssetTypeLoader& loader )
        {
            AssetTypeDef def;
            def._name     = name;
            def._pathRule = rule;
            def._loader   = loader;
            return def;
        }
        /** @brief 파일 규칙 없이 공유 캐시로만 찾는 종류입니다(등록 줄). */
        [[nodiscard]] static constexpr AssetTypeDef makeCacheOnly( string_view name, const AssetTypeLoader& loader )
        {
            AssetTypeDef def;
            def._name   = name;
            def._loader = loader;
            return def;
        }
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

        /** @brief 이름이 @p name 인 종류입니다(대소문자 무시). 없으면 nullptr 입니다. 받은 정의는 그 종류를 올린 모듈이 내려가기 전까지 삽니다. */
        static const AssetTypeDef* findType( string_view name );
        /** @brief 경로 규칙이 @p assetID 에 맞는 종류를 모두 모읍니다(비우고 채웁니다). 정상이면 0 개(카탈로그가 아닌 파일)나 1 개입니다. */
        static void collectPathMatches( string_view assetID, vector<const AssetTypeDef*>& outListType );
        /**
         * @brief @p def 의 진짜 로더로 @p assetID 를 읽어 보고 버립니다(메모리 태그를 겁니다). 카탈로그 편집기의 검사와 스키마 테스트가 부릅니다.
         * @return 로더가 없는 종류이거나 읽지 못하면 false 입니다(읽기 오류는 로더가 남긴다).
         */
        [[nodiscard]] static bool checkLoad( const AssetTypeDef& def, const AssetID& assetID );
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
    /**
     * @brief `SW_ASSET_TYPE` 가 두는 정적 등록자입니다. 만들 때 올리고 사라질 때 내립니다. 정의는 사본으로 들어, 등록 줄이 더한 경로 규칙 · 로더가 등록자와 수명이 같습니다.
     * @details 표는 프로세스 값이고 게임 모듈을 올리기 전의 쿠킹(`HeadlessSubsystem::runCookTask`)은 게임 · 키트 종류를 보지 못한다 — 쿠킹이 알아야 하는 합성 규칙은 이 표가 아니라 데이터에 둔다(`Content/engine/settings/datacataloginheritance.xml`).
     */
    class SW_API AssetTypeRegistrar
    {
    public:
        explicit AssetTypeRegistrar( const AssetTypeDef& def );
        ~AssetTypeRegistrar();
        AssetTypeRegistrar( const AssetTypeRegistrar& )            = delete;
        AssetTypeRegistrar& operator=( const AssetTypeRegistrar& ) = delete;

    private:
        AssetTypeDef _def;
    };
} // namespace sw

/**
 * @brief 에셋 종류를 등록합니다(정의를 가진 기능의 .cpp, 파일 범위). @p id 는 정적 이름을 만드는 식별자이고, 나머지는 `AssetTypeDef` 값입니다(템플릿 인자의 쉼표가 들어가도 된다).
 * @details 모든 구성(Shipping 포함)에 남습니다 — Shipping 은 모듈을 통째로 링크합니다(`sw_link_whole_archive`). 예:
 * @code
 *     SW_ASSET_TYPE( Texture, Texture2D::kAssetType );
 *     SW_ASSET_TYPE( UIScale, AssetTypeDef::makeDataFile( "uiscale", AssetPathRule{ {}, {}, "uiscale.xml" }, AssetTypeLoader::of<UIScaleSettings>() ) );
 * @endcode
 *          공유 캐시가 있는 종류는 이 줄 대신 캐시 등록 줄을 쓴다(`SharedAssetCacheDefinition.h`).
 */
#define SW_ASSET_TYPE( id, ... )                                     \
    static const ::sw::AssetTypeRegistrar sw_assetTypeRegistrar_##id \
    {                                                                \
        __VA_ARGS__                                                  \
    }
