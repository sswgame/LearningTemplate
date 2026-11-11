/**
 * @file PackReachResult.h
 * @brief 쓰는 것만 담는 쿠킹의 고르기입니다 — 뿌리에서 참조를 따라 닿는 파일을 모읍니다. 팩 쿠커(`PackCooker`)가 도메인 팩마다 이 집합의 파일만 담는다.
 * @details 언리얼 쿠커가 맵 · Primary Asset(규칙 `AlwaysCook`)에서 시작해 참조된 패키지만 쿠킹하는 것과 같다(기억에 의존한 비교 — Primary Asset Rules 의 `CookRule`).
 *          - 뿌리: 엔진 코드가 상수로 읽는 에셋(`SW_ASSET_COOK_ROOT` — 엔진 기본 에셋 표가 그 하나라 엔진 쪽은 그 칸들로 닿는다), 항상 담기 묶음(`PackCookRules::_listAlwaysCook` —
 *            게임 · 키트 코드가 이름으로 읽는 것, 게임 데이터 · 맵), 게임 설정(`_gameSettingsPath`)의 `<RequiredData kind pack>` 이 가리키는 다른 팩의 데이터(`_listRequiredData` 의 자리).
 *          - 따라가기: 글 에셋의 참조 칸(`AssetReferenceFields` — 생성 표 + 손 표, `_base` 부모 포함)과 곁 파일(`AssetCompanionRegistry`). 임포트 산출물(머티리얼 · 셋)도 따라간다.
 *            폴더를 가리키는 참조는 그 아래 전부다(런타임이 폴더를 훑는다). 따라가지 않는 칸(`_listUnfollowedField`)은 런타임이 읽지 않는 도구 입력이다.
 *          - 빠진 참조: 담을 수 있는 파일도, 제외 규칙이 뺀 원본(셰이더 원본 — 쿠킹본은 스테이징에 있다)도 아니면 오류다. 조용히 빼지 않는다. 검사하지 않는 도메인(`scratch/`)은 보지 않는다.
 *          - 항목 단위: 큰 카탈로그(`_listEntryCatalog` — 외형 · 아이템)는 쓰는 항목만 따라간다. 쓰는 항목은 닿은 글의 id 칸(`_listEntryIDField`)이 부른 것과 그 항목이 다시 부른 것이다.
 *            폴더 참조 · 항상 담기 · `#` 없는 파일 참조로 닿으면 전부 쓰인다(캐릭터 편집 화면처럼 표 전체를 보여 주는 쪽). 언리얼은 애셋 단위라 이런 판정이 없다 — 데이터 테이블은 통째로 참조를 끈다(기억).
 *          - 스테이징 산출물: `PackStagedSelection` — 원본이 쓰일 때만 담는다.
 *          - 읽기는 `IPackReachSource` 로 받는다 — 쿠커는 디스크 입구(`AssetIO`)로, 테스트는 메모리로 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct PackCookRules;

    class AssetReferenceFields;
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
        vector<string>        _listGameDomain;                                ///< 굽는 게임 팩 도메인(`game/empty`) — 그 게임 설정이 필수 데이터 뿌리를 더한다
        PackAutomationContent _automation{ PackAutomationContent::Excluded }; ///< 자동화 전용 묶음을 뿌리로 쓰는가
        vector<string>        _listCodeRoot;                                  ///< 코드가 상수로 읽는 에셋(`AssetCookRootRegistry` — 쿠커가 채운다). 어디에도 없으면 빠진 참조와 같은 오류다
    };

    /** @struct SW_API PackReachResult @brief 고르기 결과입니다. */
    struct PackReachResult
    {
        unordered_set<string> _setIncluded;           ///< 담을 파일(`_setPackable` 의 부분 집합)
        unordered_set<string> _setReferencedExcluded; ///< 닿았지만 제외 규칙이 뺀 원본(셰이더 원본) — 그 쿠킹본(스테이징)만 담는다
        uint32                _rootCount{ 0 };        ///< 뿌리 수(항상 담기 + 필수 데이터)

        // 파일 머리말 참고.
        /**
         * @brief 뿌리에서 닿는 파일을 모읍니다.
         * @return 빠진 참조 · 읽지 못한 글 · 모르는 필수 데이터 종류가 하나라도 있으면(모두 오류로 남긴다) false 입니다. 그때도 끝까지 훑어 문제를 한 번에 알린다.
         */
        [[nodiscard]] static bool collect( const AssetReferenceFields& params, const PackCookRules& rules, const PackReachFiles& files, const IPackReachSource& source,
                                           PackReachResult& outResult );
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
    class SW_API PackStagedSelection
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
