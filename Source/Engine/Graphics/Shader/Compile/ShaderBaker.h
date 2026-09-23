/**
 * @file ShaderBaker.h
 * @brief HLSL 소스를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이트코드로 미리 컴파일(베이킹)하는 유틸리티입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    /**
     * @struct ShaderBakeResult
     * @brief 셰이더 하나를 컴파일 · 베이킹한 결과입니다.
     */
    struct ShaderBakeResult
    {
        string                 _sourcePath;
        string                 _outputPath;
        string                 _entryPoint;
        ShaderStage            _stage;
        ShaderTargetFormat     _targetFormat;
        uint64                 _byteCodeSize{ 0 };
        uint8                  _bSuccess : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        ShaderBakeResult()
            : _sourcePath{}
            , _outputPath{}
            , _entryPoint{}
            , _stage{ ShaderStage::Vertex }
            , _targetFormat{ ShaderTargetFormat::SPIRV_Vulkan }
            , _byteCodeSize{ 0 }
            , _bSuccess{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @struct ShaderBakeRecipe
     * @brief 구울 것 하나입니다(셰이더 · 진입점 · 스테이지 · define).
     * @details "무엇을 구울지" 는 파이프라인 XML 과 머티리얼에서 나오고(`ShaderBakeRecipe.cpp`), "어떻게 굽는지" 는
     *          그것을 받아 컴파일합니다(`ShaderBaker.cpp`). 그 둘 사이를 넘는 값이라 여기 있습니다. 예전에는 TU 로컬이라
     *          "이 빌드가 무엇을 구웠나" 를 밖에서 볼 길이 없었습니다.
     */
    struct ShaderBakeRecipe
    {
        string         _shaderPath;
        string         _entryPoint;
        ShaderStage    _stage{ ShaderStage::Vertex };
        vector<string> _listPermutation;
        /// @brief `_listPermutation` 의 해시입니다. 구운 파일 이름에 들어갑니다(순서 무관).
        uint64 _permHash{ 0 };
    };

    /**
     * @struct ShaderBaker
     * @brief HLSL 소스를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이너리로 미리 컴파일(베이킹)하는 엔진 유틸리티입니다.
     */
    struct SW_API ShaderBaker
    {
        /**
         * @brief 셰이더 하나를 지정한 포맷과 스테이지로 컴파일해 디스크에 바이너리로 저장합니다.
         * @param sourcePath 원본 HLSL 소스 파일 경로
         * @param outputPath 출력 바이너리 파일 경로
         * @param entryPoint 셰이더 진입점 함수 이름(예: "VSMain", "PSMain", "CSMain")
         * @param stage 셰이더 스테이지(Vertex, Pixel, Compute)
         * @param targetFormat 대상 포맷(DXIL_D3D12, SPIRV_Vulkan, DXBC_D3D11)
         * @param pOutResult 베이킹 결과 상세 정보(선택)
         * @return 성공하면 true 입니다.
         */
        static bool bakeShader( string_view sourcePath, string_view outputPath, string_view entryPoint,
                                ShaderStage stage, ShaderTargetFormat targetFormat,
                                const vector<string>* pListPermutation = nullptr,
                                ShaderBakeResult*     pOutResult       = nullptr );

        // "무엇을 구울지" 와 "모두 굽기" 는 여기 없다. 파이프라인 XML · 패스 종류를 아는 렌더러의 정책이라
        // `Renderer/Bake/ShaderBakeDriver` 에 있다. 여기는 한 장을 굽는 법과 이름 짓기 · 최신 판정만 든다.

        /** @brief `bin/<rhi>/bake.stamp` 를 지금 소스의 내용 해시로 다시 씁니다(베이크가 끝난 뒤). */
        static void writeBakeStamp( string_view binDirectory );

        /** @brief 셰이더 파일의 스템을 소문자로 반환합니다. 구운 파일 이름의 앞부분입니다(`computeBinaryFileName` 의 짝). */
        static string getStemLower( string_view filePath );

        /** @brief 매크로 퍼뮤테이션(문자열 목록)으로 64비트 해시를 계산합니다(퍼뮤테이션이 없으면 0). */
        static uint64 computePermutationHash( const vector<string>& listPermutation );

        /** @brief 매크로 퍼뮤테이션(ShaderMacroDefine 목록)으로 64비트 해시를 계산합니다(퍼뮤테이션이 없으면 0). */
        static uint64 computePermutationHash( const vector<ShaderMacroDefine>& listDefine );

        /** @brief 셰이더 파일 이름 · 스테이지 · 진입점 · 퍼뮤테이션 해시를 조합한 표준 바이너리 파일 이름을 만듭니다. */
        static string computeBinaryFileName( string_view stemLower, ShaderStage stage,
                                             string_view entryPoint, uint64 permHash, string_view ext );

        /**
         * @brief 소스와 **공유 헤더(.hlsli)** 를 합친 내용 해시입니다. 산출물이 최신인지 보는 **유일한 기준**입니다.
         * @details `.hlsl` 하나만 보면 `binding.hlsli` 같은 공유 헤더를 고쳐도 아무것도 다시 굽지 않아,
         *          바이너리와 리플렉션 매니페스트가 소스와 조용히 어긋납니다(그 어긋남은 DX12 GPU 페이지
         *          폴트로 나타난 적이 있습니다). 어느 셰이더가 어떤 헤더를 include 하는지는 파싱하지 않고
         *          **모든 .hlsli** 를 넣어 넉넉하게 잡습니다. 과하게 굽는 쪽이 안전합니다.
         *
         *          **파일 시간이 아니라 내용입니다.** 이 저장소는 구운 바이너리까지 커밋하므로 `git pull`
         *          이 소스와 산출물의 mtime 을 임의의 순서로 덮어씁니다. 소스가 바뀌었는데도 "산출물이
         *          더 새것" 으로 판정돼 그대로 넘어갑니다. 실제로 `forwardlit` 이 라이트 버퍼 이전
         *          바이너리로 커밋됐고, Vulkan 만 다른 그림을 내는 것을 백엔드 버그로 오인했습니다.
         */
        static uint64 computeEffectiveSourceHash( string_view absShaderPath );
        /** @brief Resource 아래 모든 .hlsli 의 경로 + 내용을 합친 해시입니다(값을 캐시합니다). */
        static uint64 getSharedHeaderContentHash();

        /**
         * @brief 구운 산출물이 지금 소스에서 나온 것인지 `bin/<rhi>/bake.stamp` 의 내용 해시로 봅니다.
         * @param binDirectory `<domain>/shaders/bin/<rhi>`(스탬프가 있는 폴더)
         * @param absShaderPath 원본 `.hlsl` 절대 경로
         */
        static bool isBakedOutputCurrent( string_view binDirectory, string_view absShaderPath );

        /**
         * @brief 캐시된 공유 헤더 해시와 스탬프 읽기 결과를 버려, 다음 조회가 다시 훑게 합니다.
         * @details 실행 중 `.hlsli` 를 고치고 수동 리로드를 누르는 경로에서만 부릅니다. 이것을 부르지 않으면
         *          컴파일 캐시의 키가 그대로라 **바뀐 헤더가 반영되지 않습니다.** 로그는 성공을 찍는데
         *          화면은 그대로인, 가장 조용한 종류의 어긋남입니다.
         */
        static void invalidateSharedHeaderCache();

        /** @brief 타깃 포맷에 해당하는 서브폴더 이름("dx11", "dx12", "vulkan", "opengl")을 반환합니다. */
        static string_view getSubfolderForFormat( ShaderTargetFormat format );

        /** @brief 타깃 포맷에 해당하는 확장자(".dxbc", ".dxil", ".spv")를 반환합니다. */
        static string_view getExtensionForFormat( ShaderTargetFormat format );

        /** @brief 서브폴더 이름으로 ShaderTargetFormat 을 거꾸로 구합니다. */
        static ShaderTargetFormat getFormatForSubfolder( string_view subfolder );
    };
} // namespace sw
