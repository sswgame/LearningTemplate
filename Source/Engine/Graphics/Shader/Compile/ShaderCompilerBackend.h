/**
 * @file ShaderCompilerBackend.h
 * @brief 바깥 셰이더 컴파일러(DXC · FXC · Tint)를 부르는 자리입니다. 바이트코드의 바이트를 정하는 인자 · 플래그 · 후처리가 모두 여기 있습니다.
 * @details 이 파일들이 셰이더 컴파일러의 코드 지문(`Config/Dev/CookCodeInputs.json` "ShaderCompiler")이다 — DDC 조회 · 묶음 · 쿠킹 순서를 고쳐도 셰이더를 다시 컴파일하지 않게
 *          요청을 다루는 `ShaderCompiler.cpp` 와 나눈다(언리얼이 셰이더 형식 모듈 `IShaderFormat::GetVersion` 과 `FShaderCompilingManager` 를 나눈 것과 같다).
 */
#pragma once
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    /** @brief 컴파일러를 부르는 단계입니다. 앞 둘이 보통 컴파일이고, 디버그 코드젠만 소스를 바로 컴파일한다(디버그 정보가 원본 소스를 가리켜야 한다). */
    enum class ShaderBackendStep : uint8
    {
        Preprocess,          ///< 소스를 전처리만 한다 — include · define 을 다 펼친 글 하나가 나온다
        CompilePreprocessed, ///< 전처리한 글을 컴파일한다 — define · include 를 더 넣지 않는다
        CompileSource,       ///< 소스를 define · include 와 함께 컴파일한다(디버그 코드젠)
    };

    /** @brief 컴파일러 호출 한 번의 결과입니다. */
    enum class ShaderBackendRunResult : uint8
    {
        Succeeded, ///< 글(전처리) 또는 바이트코드를 얻었다
        Rejected,  ///< 셰이더 오류로 실패했다(오류 글을 담았다)
        NoOutput,  ///< 컴파일러를 못 쓰거나 출력이 없었다
    };

    /**
     * @struct ShaderBackendCall
     * @brief 컴파일러 호출의 입력입니다 — 풀린 요청과 include 규칙.
     */
    struct ShaderBackendCall
    {
        ShaderCompileDesc            _desc; ///< 엔진 define · 단계 매크로까지 든 요청
        ShaderCompileInput           _input;
        const ShaderIncludeResolver* _pIncludeResolver{ nullptr }; ///< 부르는 쪽이 호출보다 오래 든다
        string                       _profile;

        /**
         * @brief 요청 형식의 컴파일러(FXC · DXC)로 @p step 을 합니다.
         * @param inoutText 전처리한 글 — `Preprocess` 가 쓰고 `CompilePreprocessed` 가 읽는다
         */
        [[nodiscard]] ShaderBackendRunResult run( ShaderBackendStep step, vector<uint8>& inoutText, ShaderCompileResult& outResult ) const;

        /**
         * @brief 셰이더 스테이지와 타깃 포맷에 맞는 프로파일 문자열을 반환합니다.
         * @details DX11 은 SM5.0, DX12 · Vulkan · OpenGL 은 네이티브 bindless 와 Descriptor Indexing 을 위해 SM6.6 입니다. 스테이지마다
         *          다른 것은 `ShaderStageInfo` 표의 줄이고, SM5 에 없는 스테이지(메시 · 앰플리피케이션)는 vs_5_0 으로 폴백합니다.
         */
        [[nodiscard]] static const utf8* getTargetProfile( ShaderStage stage, ShaderTargetFormat targetFormat );

#if defined( SW_PLATFORM_WINDOWS )
        /**
         * @brief FXC 판 — 이 프로세스가 실은 d3dcompiler(System32 — vcpkg 기록에 없고 OS 업데이트로 바뀐다)의 내용 해시입니다. 못 읽으면 0 이고 오류를 한 번 남긴다.
         * @details DXBC 캐시 키에만 든다(언리얼이 셰이더 형식마다 컴파일러 판을 셰이더 맵 키에 넣는 자리). 도구 지문(`getToolchainFingerprint`)에 넣지 않는 것은
         *          쿠킹 기록이 기계마다(OS 판마다) 갈려 모든 셰이더를 다시 굽지 않게다. d3dcompiler 는 일반 import 라 기동 때 이미 실려 있다.
         */
        [[nodiscard]] static uint64 getFxcLibraryHash();
#endif
    };
} // namespace sw
