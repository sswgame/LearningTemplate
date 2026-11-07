/**
 * @file PackConfig.h
 * @brief 팩 쿠킹 설정(`Config/Engine/PackConfig.json`)의 형식입니다. 값은 모두 그 파일에 있고 이 헤더는 형식만 가집니다.
 * @details 팩 쿠커(`PackCooker`, `App --cook-packs`)만 읽습니다. 배포물에는 들어가지 않습니다(`Config/` 는 팩 대상이 아니다).
 *          모든 칸이 `Required` 입니다 — 빠진 칸은 기본값으로 대신하지 않고 로드 오류입니다(예전 파이썬 쿠커의 `KeyError` 를 형식이 한다).
 *          - `_compression._codec` 은 `PackCompressionType` 의 이름입니다(`Zlib` · `LZ4` · `Zstd` · `RLE` · `None`). 모든 코덱이 엔진 안에 있어 도구 설치와 관계없습니다.
 *            실측(1MB 모사 데이터): Zlib 기준 대비 LZ4 는 크지만 해제가 3배 빠르고, Zstd 는 가장 작다. `_level` 0 = 코덱 기본값.
 *          - 제외 패턴은 `StringUtil::matchesPathPattern`(에디터 임포트 규칙과 같은 마디 매칭: `/` 없는 패턴은 파일 이름에, 있으면 경로 마디별로, `*` 는 폴더를 넘지 않고 마디 `**` 만 넘는다. 대소문자 무시)으로 도메인 기준 경로에 맞춥니다. 제외 폴더는 경로 성분 이름입니다.
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Content/Pack/PackTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct PackCompressionConfig
     * @brief 팩 압축 코덱과 레벨입니다. 코덱은 팩 단위입니다(리더가 헤더의 코덱 하나로 모든 항목을 푼다).
     */
    REFLECT()
    struct SW_API PackCompressionConfig
    {
        REFLECT_BODY();

        PROPERTY( Required )
        PackCompressionType _codec{ PackCompressionType::None }; ///< 팩 코덱

        PROPERTY( Required, Min = 0 )
        int32 _level{ 0 }; ///< 코덱 레벨(0 = 코덱 기본값)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackLayoutConfig
     * @brief 팩 페이로드 배치(`PackWriter` 의 `PackLayoutMode`)와 슬롯 배치의 빈 자리 한도입니다.
     * @details `Slot` 은 같은 폴더에 앞 빌드의 팩이 있으면 그 팩을 앞 빌드로 읽어 안 바뀐 항목의 자리를 지킨다(쿠커가 덮어쓸 팩 경로를 앞 빌드로 준다).
     */
    REFLECT()
    struct SW_API PackLayoutConfig
    {
        REFLECT_BODY();

        PROPERTY( Required )
        PackLayoutMode _mode{ PackLayoutMode::Sorted }; ///< 페이로드 배치

        PROPERTY( Required, Min = 0, Max = 100 )
        int32 _maxHolePercent{ 25 }; ///< `Slot` 에서 페이로드 영역의 빈 자리가 이 비율(%)을 넘으면 앞 빌드를 버리고 촘촘히 다시 배치한다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackExcludeRule
     * @brief 이름 붙은 제외 규칙 하나입니다(폴더 · 패턴).
     */
    REFLECT()
    struct SW_API PackExcludeRule
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name; ///< 규칙 이름(로그 · 문서용)

        PROPERTY( Required )
        vector<string> _listExcludeDirectory; ///< 뺄 폴더 이름

        PROPERTY( Required )
        vector<string> _listExcludePattern; ///< 뺄 파일 패턴(빈 목록이면 패턴 제외 없음)

        PROPERTY( Required )
        bool _bRecursive{ SW_FALSE }; ///< 참이면 어느 깊이의 폴더든, 거짓이면 도메인 바로 아래 폴더만 뺀다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackAlwaysCookRule
     * @brief 참조가 없어도 담는 파일의 이름 붙은 묶음입니다(언리얼 Primary Asset Rules 의 `AlwaysCook` · Directories to Always Cook).
     * @details 코드가 이름으로 읽는 것(엔진 기본 에셋 · 설정, 시나리오, 종류 경로 규칙으로 찾는 데이터)처럼 참조 칸으로 닿지 않는 파일만 적는다. 이 묶음의 파일은 뿌리라 그 참조도 따라간다.
     *          패턴은 리소스 id(`engine/…` · `game/<팩>/…`)에 `StringUtil::matchesPathPattern` 으로 맞춘다.
     */
    REFLECT()
    struct SW_API PackAlwaysCookRule
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name; ///< 묶음 이름 — 왜 참조로 닿지 않는지를 이름으로 적는다(로그 · 문서용)

        PROPERTY( Required )
        vector<string> _listPattern; ///< 리소스 id 패턴(`game/*/maps/**`)
    };

    /**
     * @struct PackRequiredDataPath
     * @brief 게임 설정 `<RequiredData kind pack>` 의 종류 하나가 팩 안에서 차지하는 자리(`data/items.xml` · 폴더 `data/appearance`)입니다.
     * @details 런타임은 종류 등록 줄의 경로 규칙으로 자리를 만든다(`GamePackDataUtil::tryMakeKindPath`). 팩 쿠킹은 게임 · 키트 모듈을 올리기 전에 돌아 그 등록을 보지 못하므로
     *          같은 자리를 데이터로 적는다(`DataCatalogInheritance.json` 과 같은 까닭). 둘이 같은지는 `PackCookRulesTest.RequiredDataPathsMatchTheKindRegistry` 가 본다.
     */
    REFLECT()
    struct SW_API PackRequiredDataPath
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _kind; ///< 데이터 종류 이름(`SW_ASSET_TYPE` 의 이름 — `items`)

        PROPERTY( Required )
        string _path; ///< 팩 루트 기준 자리(파일이면 그 파일, 폴더면 그 아래 전부)
    };

    /**
     * @struct PackCookRules
     * @brief 무엇을 담을지의 뿌리 규칙입니다 — 쓰는 것만 담는다(`PackReachability`).
     * @details 담는 것은 뿌리(항상 담기 묶음 · 게임 설정의 필수 데이터)에서 참조 칸(`AssetReferenceFields`)과 곁 파일(`SW_ASSET_COMPANION`)을 따라 닿는 파일이다.
     *          언리얼 쿠커가 맵 · Primary Asset 에서 참조된 패키지만 쿠킹하는 것과 같다. 닿는 참조가 가리키는 파일이 없으면 쿠킹 오류다(조용히 빼지 않는다).
     */
    REFLECT()
    struct SW_API PackCookRules
    {
        REFLECT_BODY();

        PROPERTY( Required )
        vector<PackAlwaysCookRule> _listAlwaysCook; ///< 참조가 없어도 담는 묶음(뿌리)

        PROPERTY( Required )
        string _gameSettingsPath; ///< 게임 팩 루트 기준 게임 설정 파일(`data/gamesettings.xml`) — 그 `<RequiredData>` 가 다른 팩의 데이터를 뿌리로 더한다

        PROPERTY( Required )
        vector<PackRequiredDataPath> _listRequiredData; ///< 필수 데이터 종류의 팩 안 자리. 게임 설정이 이 표에 없는 종류를 적으면 쿠킹 오류다

        PROPERTY( Required )
        vector<string> _listUnfollowedField; ///< 따라가지 않는 경로 칸 — 런타임이 읽지 않는 개발 도구 입력(현지화 수집 뿌리 `assetRoots`)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackConfig
     * @brief `Config/Engine/PackConfig.json` 이 담는 팩 쿠킹 설정입니다. 파일 머리말 참고.
     * @details `ConfigBaseLayer` 라 파일이 모든 키를 적는다(`ConfigFileSchemaTest`).
     */
    REFLECT( Meta = "ConfigBaseLayer" )
    struct SW_API PackConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief 프로젝트 루트 기준 설정 파일 경로입니다. */
        static constexpr const utf8* kConfigPath = "Config/Engine/PackConfig.json";

        /**
         * @brief 셰이더 원본(`.hlsl` · `.hlsli`)인가입니다 — `_bExcludeRawShaderSource` 가 켜진 팩(배포)에는 없고 쿠킹한 바이너리만 있다.
         * @details 팩 쿠커가 뺄 때와 기본 에셋 검사가 "배포 팩에 없는 것이 맞다" 고 볼 때 같은 판정을 쓴다(확장자를 두 곳에 적지 않는다).
         */
        static bool isRawShaderSource( string_view path ) { return StringUtil::endsWith( path, ".hlsl", true ) || StringUtil::endsWith( path, ".hlsli", true ); }

        PROPERTY( Required )
        PackCompressionConfig _compression; ///< 팩 압축

        PROPERTY( Required )
        PackLayoutConfig _layout; ///< 페이로드 배치(항목 추가 · 삭제에 뒤 항목이 밀리지 않게 하는 슬롯 배치)

        PROPERTY( Required )
        bool _bExcludeRawShaderSource{ false }; ///< 셰이더 원본(`.hlsl` · `.hlsli`)을 팩에서 뺀다 — 배포물은 쿠킹한 바이너리만 읽는다

        PROPERTY( Required )
        vector<string> _listGlobalExcludeDirectory; ///< 어느 깊이에서든 뺄 폴더 이름

        PROPERTY( Required )
        vector<string> _listGlobalExcludePattern; ///< 뺄 파일 패턴

        PROPERTY( Required )
        vector<PackExcludeRule> _listRule; ///< 이름 붙은 제외 규칙

        PROPERTY( Required )
        PackCookRules _cookRules; ///< 담을 뿌리 규칙 — 제외를 지난 파일 가운데 뿌리에서 참조로 닿는 것만 담는다
    };
} // namespace sw
