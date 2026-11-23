/**
 * @file ShaderRequestLog.h
 * @brief 실행이 쓴 셰이더 컴파일 요청을 대상 형식별로 중복 없이 모은 기록과, 그 기록만 다시 컴파일해 셰이더 DDC 를 데우는 길입니다.
 * @details 셰이더 · 공용 hlsli 를 고친 묶음은 다음 실행이 셰이더마다 캐시 미스로 다시 컴파일한다(헤더 의존 기록 — `CookRecord::isFresh`).
 *          시간을 재는 실행(묶음 검증의 시나리오 체인) 안에서 그 컴파일이 일어나지 않게, 지난 실행의 요청을 기록해 두고 다음 검증이 시간을 재기 전에 그것만 컴파일한다.
 *          전체 굽기(`--cook-shaders`)는 모든 퍼뮤테이션이라 수만 개지만 실행이 실제로 쓰는 것은 백엔드당 수백 개다.
 *          상용 엔진: 언리얼 Shader Pipeline Cache(실행이 PSO 를 기록하고 다음 실행 · 쿠킹이 미리 컴파일), 유니티 ShaderVariantCollection(쓴 변형을 모아 미리 워밍).
 */
#pragma once
#include "Core/Container/flat_set.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    class ShaderSourceResolver;

    /** @struct ShaderWarmSummary @brief 기록을 컴파일한 결과의 수입니다. */
    struct ShaderWarmSummary
    {
        uint32 _requestCount{ 0 };       ///< 기록의 요청 수
        uint32 _missingSourceCount{ 0 }; ///< 소스가 없어 건너뛴 요청 수
        uint32 _failedCount{ 0 };        ///< 컴파일이 진 요청 수
    };

    /**
     * @class ShaderRequestLog
     * @brief 셰이더 컴파일 요청의 기록입니다 — 캐시 칸을 다시 만들 수 있는 값(소스 · 진입점 · 스테이지 · define · 대상 형식)만 줄 하나로 적는다.
     * @details 줄은 `<형식 폴더>\t<스테이지 태그>\t<소스>\t<진입점>[\t<NAME=VALUE>…]`(형식 폴더는 `ShaderCooker::getSubfolderForFormat`, 태그는 `ShaderStageInfo::_pTag`)이고
     *          줄 글 자체가 중복 판정의 키다. define 순서는 그대로 둔다 — 셰이더 DDC 키가 define 을 적힌 순서로 섞는다. 파일은 머리 줄 하나 뒤에 줄을 정렬해 적는다(같은 요청 집합이면 같은 글).
     *          엔진 설정 사본 · 디버그 코드젠은 적지 않는다 — 데우는 쪽이 자기 사본과 최적화 코드젠(쿠킹 · 실시간 기본과 같은 바이트)으로 컴파일한다.
     *          잠금이 없다 — 여러 스레드가 쓰면 주인(`ShaderCache`)이 자기 잠금으로 지킨다.
     */
    class SW_API ShaderRequestLog
    {
    public:
        /** @brief 기록 파일의 확장자입니다. 데우기가 폴더에서 이 확장자의 파일만 읽는다. */
        static constexpr const utf8* kFileExtension = ".shaderrequests";
        /** @brief 기록 파일의 머리 줄(형식 판)입니다. 다르면 읽기가 오류다. */
        static constexpr const utf8* kHeaderLine = "SW shader request log 1";

        ShaderRequestLog();

        /** @brief 요청 @p desc 를 더합니다. 이미 있으면 false 입니다. */
        bool add( const ShaderCompileDesc& desc );
        /** @brief 모은 요청 수입니다. */
        [[nodiscard]] size_t size() const { return _setLine.size(); }
        /** @brief 파일 글입니다(머리 줄 + 정렬한 줄). */
        [[nodiscard]] string makeText() const;
        /** @brief 파일 글 @p text 를 읽어 더합니다(중복은 하나로). 머리 줄이 다르거나 줄 모양이 틀리면 그 줄이 대상인 오류이고 아무것도 더하지 않습니다. */
        [[nodiscard]] expected<void> mergeText( string_view text );
        /** @brief 모은 요청입니다(줄 순서). `_enginePolicy` 는 비어 있다 — 컴파일하는 쪽이 싣는다. */
        [[nodiscard]] vector<ShaderCompileDesc> collectDescs() const;

        /**
         * @brief 폴더 @p folder 바로 아래의 기록 파일(`kFileExtension`)을 모두 읽어 하나로 합칩니다. 백엔드 자식마다 파일이 따로라 읽을 때 합친다.
         * @details 폴더가 없거나 기록이 없으면 빈 기록이다(첫 실행 — 데울 것이 없다). 읽지 못한 파일 · 틀린 글은 오류다.
         */
        [[nodiscard]] static expected<ShaderRequestLog> readFolder( string_view folder );
        /**
         * @brief @p inoutList 에서 소스를 풀 수 없는 요청을 빼고 그 소스를 @p outListMissing 에 적습니다(중복 없이).
         * @details 기록은 지난 실행의 것이라 그 뒤에 소스가 지워지거나 옮겨졌을 수 있다 — 없는 소스는 기록의 성질이지 결함이 아니어서 빼고 넘어간다(폴백이 아니다: 대신 고르는 값이 없다).
         */
        static void removeMissingSources( vector<ShaderCompileDesc>& inoutList, const ShaderSourceResolver& resolver, vector<string>& outListMissing );
        /**
         * @brief 기록의 요청을 엔진 설정 사본 @p enginePolicy 로 병렬 컴파일합니다 — 결과는 버리고 셰이더 DDC(`ShaderCompiler::compileHLSL`)가 채워지는 것이 목적이다.
         * @details 소스가 없는 요청은 경고하고 건너뛴다(`removeMissingSources`). 요청은 줄 글이 달라 서로 다르다 — 컴파일러의 "같은 변형을 동시에 넣지 않는다" 계약을 지킨다.
         */
        [[nodiscard]] ShaderWarmSummary compileAll( const ShaderEnginePolicy& enginePolicy ) const;
        /**
         * @brief 폴더 @p folder 의 기록을 합쳐(`readFolder`) 엔진 설정에서 뜬 사본으로 컴파일하고(`compileAll`) 수를 한 줄로 남깁니다 — `App --cook-shaders -shader-request-dir=<폴더>`.
         * @details 게임 · 기동 스레드에서 부른다(엔진 설정 사본을 뜬다). 기록이 없으면 할 일이 없다고 남기고 true 다. 기록을 읽지 못했거나 컴파일이 하나라도 지면 false 입니다 —
         *          기록은 성공한 요청만 적으므로 지는 컴파일은 지금 소스의 오류다(시간을 재는 실행보다 먼저 드러난다).
         */
        [[nodiscard]] static bool warmFromFolder( string_view folder );

    private:
        /** @brief 요청 하나의 줄입니다. */
        [[nodiscard]] static string makeLine( const ShaderCompileDesc& desc );
        /** @brief 줄 하나를 요청으로 읽습니다. */
        [[nodiscard]] static expected<ShaderCompileDesc> parseLine( string_view line );

    private:
        flat_set<string> _setLine; ///< 요청 줄(정렬 — 파일 글이 결정적이다)
    };
} // namespace sw
