/**
 * @file PackReachResult.h
 * @brief 쓰는 것만 담는 쿠킹의 고르기입니다 — 뿌리에서 참조를 따라 닿는 파일을 모읍니다. 팩 쿠커(`PackCooker`)가 도메인 팩마다 이 집합의 파일만 담는다.
 * @details 언리얼 쿠커가 맵 · Primary Asset(규칙 `AlwaysCook`)에서 시작해 참조된 패키지만 쿠킹하는 것과 같다(기억에 의존한 비교 — Primary Asset Rules 의 `CookRule`).
 *          - 뿌리: 엔진 코드가 상수로 읽는 에셋(`SW_ASSET_COOK_ROOT` — 엔진 기본 에셋 표가 그 하나라 엔진 쪽은 그 칸들로 닿는다), 항상 담기 묶음(`PackCookRules::_listAlwaysCook` —
 *            게임 · 키트 코드가 이름으로 읽는 것, 게임 데이터 · 맵), 게임 설정(`_gameSettingsPath`)의 `<RequiredData kind pack>` 이 가리키는 다른 팩의 데이터(`_listRequiredData` 의 자리),
 *            도메인 뿌리의 리다이렉트 표(`AssetRedirect` — 런타임이 이름으로 읽고, 옛 id 를 적는 글이라 훑지 않는다).
 *          - 따라가기: 글 에셋의 참조 칸(`AssetReferenceFields` — 생성 표 + 손 표, `_base` 부모 포함)과 곁 파일(`AssetCompanionRule`). 임포트 산출물(머티리얼 · 셋)도 따라간다.
 *            폴더를 가리키는 참조는 그 아래 전부다(런타임이 폴더를 훑는다). 따라가지 않는 칸(`_listUnfollowedField`)은 런타임이 읽지 않는 도구 입력이다.
 *          - 빠진 참조: 담을 수 있는 파일도, 제외 규칙이 뺀 원본(셰이더 원본 — 쿠킹본은 스테이징에 있다)도 아니면 오류다. 조용히 빼지 않는다. 검사하지 않는 도메인(`scratch/`)은 보지 않는다.
 *          - 항목 단위: 큰 카탈로그(`_listEntryCatalog` — 외형 · 아이템)는 쓰는 항목만 따라간다. 쓰는 항목은 닿은 글의 id 칸(`_listEntryIDField`)이 부른 것과 그 항목이 다시 부른 것이다.
 *            폴더 참조 · 항상 담기 · `#` 없는 파일 참조로 닿으면 전부 쓰인다(캐릭터 편집 화면처럼 표 전체를 보여 주는 쪽). 언리얼은 애셋 단위라 이런 판정이 없다 — 데이터 테이블은 통째로 참조를 끈다(기억).
 *          - 스테이징 산출물: `PackStagedSelection` — 원본이 쓰일 때만 담는다.
 *          - 읽기는 `IPackReachSource` 로 받는다 — 쿠커는 디스크 입구(`AssetIO`)로, 테스트는 메모리로 읽는다.
 *          - 파일마다의 사실(참조 · 불리는 항목 · 곁 파일)은 쿠킹 하나에 그래프 하나(`PackReachGraph`)가 한 번만 뽑는다 — 고르기마다(기동 필수, 씬마다) 같은 글을 다시 읽고
 *            풀지 않는다. 언리얼 애셋 레지스트리의 의존 그래프 위를 청크마다 걷는 것과 같은 모양이다(기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct PackCookRules;
    struct PackEntryCatalog;
    struct PackEntryIDField;

    class AssetReferenceFields;
    class XMLNode;
} // namespace sw

namespace sw
{
    /** @brief 이 쿠킹이 자동화 콘텐츠(시나리오 — `PackAlwaysCookRule::_bAutomationOnly`)를 담는가입니다. 쿠킹하는 구성에 실행기(`IAutomationRuntime`)가 있는지로 정한다. */
    enum class PackAutomationContent : uint8
    {
        Excluded, ///< 실행기가 없는 구성(Shipping · 전용 서버) — 시나리오를 담지 않는다
        Included, ///< 실행기가 있는 구성 — 시나리오를 뿌리로 담는다
    };

    /** @brief 고르기가 글 에셋을 읽는 입구입니다. */
    class IPackReachSource
    {
    public:
        virtual ~IPackReachSource() = default;
        /** @brief 리소스 id(정규화 — 소문자 · `/`) @p assetID 의 글을 읽습니다. 읽지 못하면 false 입니다. */
        [[nodiscard]] virtual bool readText( const string& assetID, string& outText ) const = 0;
    };

    /** @struct PackReachFiles @brief 고르기의 파일 목록입니다. id 는 모두 정규화한 리소스 id(`FileUtil::normalizePath`)다. */
    struct PackReachFiles
    {
        unordered_set<string> _setPackable;                                   ///< 담을 수 있는 파일(굽는 도메인의 파일 중 제외 규칙을 지난 것)
        unordered_set<string> _setExisting;                                   ///< 있는 모든 파일(제외 규칙이 뺀 것 포함) — 빠진 참조와 뺀 원본을 가른다
        vector<string>        _listGameDomain;                                ///< 굽는 게임 팩 도메인(`game/playground`) — 그 게임 설정이 필수 데이터 뿌리를 더한다
        PackAutomationContent _automation{ PackAutomationContent::Excluded }; ///< 자동화 전용 묶음을 뿌리로 쓰는가
        vector<string>        _listCodeRoot;                                  ///< 코드가 상수로 읽는 에셋(`AssetCookRoot` — 쿠커가 채운다). 어디에도 없으면 빠진 참조와 같은 오류다
    };

    /** @brief 카탈로그 파일에 닿은 길입니다. */
    enum class PackCatalogReach : uint8
    {
        Whole, ///< 폴더 · 항상 담기 · `#` 없는 파일 참조 — 모든 항목이 쓰인다
        Named, ///< 필수 데이터 · `파일#id` — 이름이 불린 항목만 쓰인다
    };

    /** @brief 글(또는 항목)이 가리키는 참조 하나입니다. */
    struct PackReachReference
    {
        string           _path;  ///< 정규화한 리소스 id(파일 또는 폴더)
        string           _field; ///< 칸 이름(오류 문구용)
        PackCatalogReach _reach{ PackCatalogReach::Whole };
    };

    /** @brief 글(또는 항목) 하나에서 뽑은 것입니다 — 따라갈 참조와 부르는 항목의 열쇠(`<이름 공간>\t<id>`). */
    struct PackReachText
    {
        vector<PackReachReference> _listReference;
        vector<string>             _listNamedKey;
    };

    /** @brief 항목 단위 카탈로그의 항목 하나입니다. */
    struct PackReachEntry
    {
        string        _key; ///< `<이름 공간>\t<id>`
        PackReachText _text;
    };

    /** @brief 파일 하나의 사실입니다. 항목 단위 카탈로그면 `_listEntry` 가 항목들이고 `_text` 는 항목 밖의 글이다. */
    struct PackReachFile
    {
        string                 _assetID;
        PackReachText          _text;
        vector<PackReachEntry> _listEntry;
        vector<string>         _listCompanion; ///< 담을 수 있는 곁 파일
        uint8                  _bEntryCatalog : 1;
        uint8                  _bFailed       : 1; ///< 읽지 못한 글 · 깨진 카탈로그(처음 볼 때 오류를 남겼다). 닿은 고르기는 모두 실패다
        [[maybe_unused]] uint8 _reserved      : 6;

        PackReachFile()
            : _assetID{}
            , _text{}
            , _listEntry{}
            , _listCompanion{}
            , _bEntryCatalog{ SW_FALSE }
            , _bFailed{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @class PackReachGraph
     * @brief 쿠킹 하나의 참조 그래프입니다 — 파일마다의 사실을 처음 닿을 때 한 번 뽑아 두고, 고르기들(`PackReachResult`)이 함께 걷습니다.
     * @details 넘긴 표 · 목록 · 읽기 입구를 빌려 쓴다(그래프보다 오래 살아야 하고 그동안 바뀌지 않아야 한다). 한 스레드가 쓴다.
     */
    class PackReachGraph
    {
    public:
        PackReachGraph( const AssetReferenceFields& params, const PackCookRules& rules, const PackReachFiles& files, const IPackReachSource& source );
        ~PackReachGraph();

        PackReachGraph( const PackReachGraph& )            = delete;
        PackReachGraph& operator=( const PackReachGraph& ) = delete;

        /** @brief 담을 수 있는 파일 @p assetID 의 사실입니다. 처음이면 읽어 뽑는다. 돌려준 참조는 그래프가 사는 동안 그대로다. */
        const PackReachFile& getFile( const string& assetID );
        /** @brief 폴더 @p path 아래 담을 수 있는 파일입니다. 없으면 nullptr 입니다. */
        const vector<string>* findPackableUnder( const string& path ) const;
        /** @brief 있는 파일이 아래에 있는 폴더면 true 입니다. */
        bool isExistingFolder( const string& path ) const { return _setExistingFolder.contains( path ); }
        /** @brief @p assetID 가 항목 단위 카탈로그면 그 규칙, 아니면 nullptr 입니다. */
        const PackEntryCatalog* findCatalogRule( string_view assetID ) const;

        const PackCookRules&    getRules() const { return _rules; }
        const PackReachFiles&   getFiles() const { return _files; }
        const IPackReachSource& getSource() const { return _source; }

    private:
        void loadFile( const string& assetID, PackReachFile& outFile );
        void loadCatalog( const PackEntryCatalog& rule, const string& assetID, const string& text, PackReachFile& outFile );
        /** @brief 글 @p text 의 참조 칸을 모읍니다. `파일#id` 는 그 항목을 부르고, `#` 없는 파일 참조는 파일 전부를 쓴다. */
        void collectReferences( string_view text, const string& assetID, PackReachText& inoutText ) const;
        /** @brief @p node 와 그 아래의 id 칸(속성 · 요소 글 · `<item>` 목록)이 부르는 항목을 모읍니다. */
        void collectNamedEntries( XMLNode node, const string& assetID, PackReachText& inoutText ) const;
        /** @brief 파일 @p assetID 안의 칸 @p fieldName 이 id 칸이면 그 이름 공간, 아니면 nullptr 입니다 — 그 파일의 팩에 닿는 칸만 본다(`PackRuleScope`). */
        const string* findIDFieldKind( string_view fieldName, string_view assetID ) const;

    private:
        const AssetReferenceFields&                            _params;
        const PackCookRules&                                   _rules;
        const PackReachFiles&                                  _files;
        const IPackReachSource&                                _source;
        unordered_set<string>                                  _setUnfollowedField;
        unordered_map<string, vector<const PackEntryIDField*>> _mapIDFieldKind;    ///< id 칸 이름 → 그 이름의 칸들(팩마다 닿는 범위가 다를 수 있다 — `PackRuleScope`)
        unordered_map<string, vector<string>>                  _mapPackableUnder;  ///< 폴더 → 그 아래 담을 수 있는 파일(`_files._setPackable` 순서)
        unordered_set<string>                                  _setExistingFolder; ///< 있는 파일이 아래에 있는 폴더
        unordered_map<string, unique_ptr<PackReachFile>>       _mapFile;           ///< 뽑은 파일의 사실(주소가 고정된다)
    };

    /** @struct PackReachResult @brief 고르기 결과입니다. */
    struct PackReachResult
    {
        unordered_set<string> _setIncluded;           ///< 담을 파일(`_setPackable` 의 부분 집합)
        unordered_set<string> _setReferencedExcluded; ///< 닿았지만 제외 규칙이 뺀 원본(셰이더 원본) — 그 쿠킹본(스테이징)만 담는다
        uint32                _rootCount{ 0 };        ///< 뿌리 수(항상 담기 + 필수 데이터)

        // 파일 머리말 참고.
        /**
         * @brief 뿌리에서 닿는 파일을 모읍니다.
         * @param setDeferredRoot 항상 담기에 맞아도 뿌리로 쓰지 않는 파일(씬 청크로 미루는 씬 — 기동 필수 고르기). 다른 뿌리가 참조로 닿으면 담는다
         * @return 빠진 참조 · 읽지 못한 글 · 모르는 필수 데이터 종류가 하나라도 있으면(모두 오류로 남긴다) false 입니다. 그때도 끝까지 훑어 문제를 한 번에 알린다.
         */
        [[nodiscard]] static bool collect( PackReachGraph& graph, const unordered_set<string>& setDeferredRoot, PackReachResult& outResult );
        /**
         * @brief 뿌리 @p listRoot(정규화한 id — 담을 수 있는 파일)에서만 닿는 파일을 모읍니다. 씬 하나의 닿음(씬 청크 나누기 — `PackSceneChunkPlan`)에 쓴다.
         * @return `collect` 와 같은 실패 규칙입니다.
         */
        [[nodiscard]] static bool collectFromRoots( PackReachGraph& graph, const vector<string>& listRoot, PackReachResult& outResult );
    };

    /** @struct PackDomainSources @brief 도메인 하나의 있는 모든 원본 id 입니다(스테이징 산출물의 원본을 가린다). */
    struct PackDomainSources
    {
        string         _domain;
        vector<string> _listExistingID;
    };

    /**
     * @class PackStagedSelection
     * @brief 스테이징 산출물을 담을지 고릅니다. 산출물은 원본에서 나오므로 그 원본이 쓰일 때만 담는다.
     * @details 같은 폴더 · 같은 줄기(첫 `.` 앞)의 원본이 그 원본이다(`maps/a.scene.xml` → `maps/a.scene.bin` · `a.preload.json` · 내비, 프리팹 쿠킹본).
     *          셰이더 바이너리(`shaders/bin/<백엔드>/`)는 같은 도메인의 `.hlsl` 줄기가 원본이다(`<줄기>_<진입점>[_<순열>]` — 가장 긴 줄기).
     *          원본을 가릴 수 없는 산출물(스탬프 · 반사 목록 · 같은 자리에 원본이 없는 산출물)은 담는다 — 모르는 것을 빼지 않는다.
     */
    class PackStagedSelection
    {
    public:
        /** @brief 있는 원본과 고르기 결과로 표를 만듭니다. */
        void build( const vector<PackDomainSources>& listSources, const PackReachResult& reach );
        /** @brief 도메인 기준 @p relativePath 의 산출물을 담으면 true 입니다. */
        [[nodiscard]] bool isSelected( string_view domain, string_view relativePath ) const;

    private:
        unordered_map<string, bool> _mapSourceStem; ///< `<폴더 id>/<줄기>` → 그 줄기의 원본 가운데 쓰이는 것이 있는가
        unordered_map<string, bool> _mapShaderStem; ///< `<도메인>\t<셰이더 줄기>` → 그 셰이더 원본이 쓰이는가
    };
} // namespace sw
