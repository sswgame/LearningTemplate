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
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Resource/Pack/ResourcePakTypes.h"

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
     * @brief 팩 페이로드 배치(`ResourcePakWriter` 의 `PackLayoutMode`)와 슬롯 배치의 빈 자리 한도입니다.
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
        bool _bRecursive{ false }; ///< 참이면 어느 깊이의 폴더든, 거짓이면 도메인 바로 아래 폴더만 뺀다
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
    };
} // namespace sw
