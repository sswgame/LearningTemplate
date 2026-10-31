/**
 * @file ShaderBytecodeProvider.h
 * @brief 셰이더 바이트코드 · 리플렉션을 어디서 얻는가(쿠킹본 정책은 팩, 소스 정책은 실행 중 컴파일)를 정하는 공급자입니다. 셰이더 캐시가 기동 때 하나만 겁니다.
 * @details 예전에는 같은 판정이 다섯 곳에 흩어져 있었다 — 캐시 로드 · 매니페스트 조회 · 리플렉션 폴백의 콘텐츠 정책 분기와 "엔진 서비스가 있으면 캐시, 없으면 컴파일러" 분기 둘.
 *          그래서 쿠킹본 정책의 메모리 캐시는 소스 경로가 없어 지문이 늘 0 이었고 한 번도 맞지 않았다. 정책은 실행 중 바뀌지 않으므로 기동 때 고른 구현이 답한다.
 *          상용 엔진: 언리얼은 쿠킹된 빌드가 `FShaderCodeLibrary`(셰이더 라이브러리)에서, 에디터가 셰이더 컴파일 매니저 · DDC 에서 바이트코드를 얻는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    class ShaderCache;
    struct ShaderReflectionData;

    /**
     * @class IShaderBytecodeProvider
     * @brief 바이트코드 공급자 계약입니다(데이터 없는 인터페이스). 구현은 `CookedShaderBytecodeProvider` · `LiveShaderBytecodeProvider` 둘이고 `ShaderCache::initialize` 가 콘텐츠 정책으로 고른다.
     * @details 스레드 계약: 로드 워커가 동시에 부른다 — 구현은 상태가 없다.
     */
    class IShaderBytecodeProvider
    {
    public:
        virtual ~IShaderBytecodeProvider() = default;

        /** @brief 메모리 캐시에 없는 요청 @p desc 의 바이트코드를 얻습니다. 실패해도 오류 글을 담은 결과를 돌려줍니다. */
        [[nodiscard]] virtual ShaderCompileResult load( const ShaderCompileDesc& desc ) const = 0;
        /** @brief 메모리 캐시 항목의 지문입니다. 넣을 때와 꺼낼 때 같은 값이면 맞다. 0 이면 맞을 수 없다(읽지 못한 소스). */
        [[nodiscard]] virtual uint64 computeFingerprint( const ShaderCompileResult& result ) const = 0;
        /** @brief 요청 @p desc 의 바인딩 리플렉션을 구합니다. 바이트코드와 같은 출처다(섞으면 레이아웃과 바이트코드가 어긋난다). */
        [[nodiscard]] virtual bool getReflection( ShaderCache& cache, const ShaderCompileDesc& desc, ShaderReflectionData& outReflection ) const = 0;
    };

    /**
     * @class CookedShaderBytecodeProvider
     * @brief 쿠킹본 정책(배포 · `-cooked-content`)입니다. 바이트코드는 팩의 쿠킹본(`ShaderBinaryLocation`), 리플렉션은 쿠킹된 매니페스트만 읽는다 — 컴파일러가 없다.
     * @details 팩은 실행 중 바뀌지 않으므로 메모리 캐시 항목은 늘 맞다(지문이 상수).
     */
    class CookedShaderBytecodeProvider final : public IShaderBytecodeProvider
    {
    public:
        [[nodiscard]] ShaderCompileResult load( const ShaderCompileDesc& desc ) const override;
        [[nodiscard]] uint64              computeFingerprint( const ShaderCompileResult& result ) const override;
        [[nodiscard]] bool                getReflection( ShaderCache& cache, const ShaderCompileDesc& desc, ShaderReflectionData& outReflection ) const override;
    };

    /**
     * @class LiveShaderBytecodeProvider
     * @brief 소스 정책(개발 실행)입니다. 바이트코드는 실행 중 컴파일(컴파일러가 셰이더 DDC 를 먼저 본다), 리플렉션은 그 바이트코드에서 만든다 — 쿠킹본을 읽지 않는다.
     * @details 메모리 캐시 지문은 소스와 그 컴파일이 연 헤더의 내용이다(`ShaderCooker::computeSourceFingerprint`) — 파일을 고치면 다음 요청이 다시 컴파일한다.
     */
    class LiveShaderBytecodeProvider final : public IShaderBytecodeProvider
    {
    public:
        [[nodiscard]] ShaderCompileResult load( const ShaderCompileDesc& desc ) const override;
        [[nodiscard]] uint64              computeFingerprint( const ShaderCompileResult& result ) const override;
        [[nodiscard]] bool                getReflection( ShaderCache& cache, const ShaderCompileDesc& desc, ShaderReflectionData& outReflection ) const override;
    };
} // namespace sw
