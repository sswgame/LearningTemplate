/**
 * @file ShaderBaker.h
 * @brief HLSL 소스 코드를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이너리 바이트코드로 사전 컴파일(베이킹)하는 유틸리티
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/ShaderCompiler.h"

namespace sw
{
    /**
     * @struct ShaderBakeResult
     * @brief 셰이더 단일 컴파일 및 베이킹 결과
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
     * @struct ShaderBaker
     * @brief HLSL 소스 코드를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이너리로 사전 컴파일(베이킹)하는 엔진 유틸리티
     */
    struct SW_API ShaderBaker
    {
        /**
         * @brief 단일 셰이더를 지정된 포맷과 스테이지로 컴파일하여 디스크에 바이너리로 저장합니다.
         * @param sourcePath 원본 HLSL 소스 파일 경로
         * @param outputPath 출력 바이너리 파일 경로
         * @param entryPoint 셰이더 진입점 함수 이름 (예: "VSMain", "PSMain", "CSMain")
         * @param stage 셰이더 스테이지 (Vertex, Pixel, Compute)
         * @param targetFormat 대상 포맷 (DXIL_D3D12, SPIRV_Vulkan, DXBC_D3D11)
         * @param pOutResult 베이킹 결과 상세 정보 (선택적)
         * @return 성공 시 true
         */
        static bool bakeShader( string_view sourcePath, string_view outputPath, string_view entryPoint,
                                ShaderStage stage, ShaderTargetFormat targetFormat,
                                const vector<string>* pListPermutation = nullptr,
                                ShaderBakeResult*     pOutResult       = nullptr );

        /**
         * @brief 렌더 파이프라인 에셋(pipeline XML) 및 엔진 부트스트랩 데이터(enginedata.xml)를 기반으로
         *        실제 게임 런타임에 필요한 셰이더와 순열(Permutations)만을 4대 RHI 바이너리로 일괄 베이킹합니다.
         * @param resourceRoot 리소스 루트 디렉터리 (비어있으면 ResourceUtil 기준 자동 탐색)
         * @param targetFormat 대상 포맷 (Count이면 DX11, DX12, Vulkan, OpenGL 전체 베이킹)
         * @param bForceAll true이면 타임스탬프와 무관하게 전면 재컴파일
         * @return 성공적으로 베이킹된 바이너리 파일 총 개수
         */
        static uint32 bakeAllShaders( string_view        resourceRoot = {},
                                      ShaderTargetFormat targetFormat = ShaderTargetFormat::Count,
                                      bool               bForceAll    = false );

        /** @brief 매크로 순열(문자열 목록)로부터 64비트 고유 해시값을 계산합니다. (순열 없으면 0 반환) */
        static uint64 computePermutationHash( const vector<string>& listPermutation );

        /** @brief 매크로 순열(ShaderMacroDefine 목록)로부터 64비트 고유 해시값을 계산합니다. (순열 없으면 0 반환) */
        static uint64 computePermutationHash( const vector<ShaderMacroDefine>& listDefine );

        /** @brief 셰이더 파일명, 스테이지, 진입점, 순열 해시를 조합한 표준 바이너리 파일명을 생성합니다. */
        static string computeBinaryFileName( string_view stemLower, ShaderStage stage,
                                             string_view entryPoint, uint64 permHash, string_view ext );

        /** @brief 셰이더 스테이지별 기본 진입점 이름(VSMain, PSMain, CSMain, GSMain 등)을 반환합니다. */
        static string_view getDefaultEntryPointForStage( ShaderStage stage );

        /** @brief 타깃 포맷에 해당하는 서브폴더 이름("dx11", "dx12", "vulkan", "opengl")을 반환합니다. */
        /**
         * @brief 소스와 **공유 헤더(.hlsli)** 중 가장 새로운 타임스탬프.
         * @details 베이크 산출물이 최신인지 판단하는 유일한 기준이다. `.hlsl` 하나만 보면
         *          `binding.hlsli` 같은 공유 헤더를 고쳐도 아무것도 다시 굽지 않아, 바이너리와
         *          리플렉션 매니페스트가 소스와 조용히 어긋난다(그 어긋남은 DX12 GPU 페이지 폴트로
         *          나타난 적이 있다). 어느 셰이더가 어떤 헤더를 include 하는지는 파싱하지 않고
         *          **모든 .hlsli 중 최신값**으로 넉넉하게 잡는다 — 과하게 굽는 쪽이 안전하다.
         */
        static uint64 computeEffectiveSourceTimestamp( string_view absShaderPath );
        /** @brief Resource 아래 모든 .hlsli 중 가장 새로운 타임스탬프 (프로세스당 한 번만 훑는다). */
        static uint64 getSharedHeaderTimestamp();

        static string_view getSubfolderForFormat( ShaderTargetFormat format );

        /** @brief 타깃 포맷에 해당하는 확장자(".dxbc", ".dxil", ".spv")를 반환합니다. */
        static string_view getExtensionForFormat( ShaderTargetFormat format );

        /** @brief 서브폴더 이름으로부터 ShaderTargetFormat을 역산출합니다. */
        static ShaderTargetFormat getFormatForSubfolder( string_view subfolder );

        /** @brief 셰이더 스테이지 축약 태그("vs", "ps", "cs", "gs", "hs", "ds", "ms", "as")를 반환합니다. */
        static string_view getStageTag( ShaderStage stage );
    };
} // namespace sw
