/**
 * @file ShaderCooker.h
 * @brief HLSL 소스를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이트코드로 미리 컴파일(쿠킹)하는 유틸리티입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    class AssetPaths;

    /**
     * @struct ShaderCookResult
     * @brief 셰이더 하나를 컴파일하고 쿠킹한 결과입니다.
     */
    struct ShaderCookResult
    {
        string             _sourcePath;
        string             _outputPath;
        string             _entryPoint;
        ShaderStage        _stage;
        ShaderTargetFormat _targetFormat;
        /// @brief 컴파일이 실제로 연 `#include` 파일입니다(`ShaderCompileResult::_listIncludedFile` 그대로). 성공했을 때만 믿는다. 쿠킹 스탬프가 소스별 의존 목록으로 적는다.
        vector<string>         _listIncludedFile;
        uint64                 _byteCodeSize{ 0 };
        uint8                  _bSuccess : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        ShaderCookResult()
            : _sourcePath{}
            , _outputPath{}
            , _entryPoint{}
            , _stage{ ShaderStage::Vertex }
            , _targetFormat{ ShaderTargetFormat::SPIRV_Vulkan }
            , _listIncludedFile{}
            , _byteCodeSize{ 0 }
            , _bSuccess{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShaderCookRequest
     * @brief 쿠킹할 대상 하나입니다(셰이더, 진입점, 스테이지, define).
     * @details "무엇을 쿠킹할지"는 파이프라인 XML 과 머티리얼에서 나오고(`ShaderCookRequestCollector`), "어떻게 쿠킹하는지"는
     *          그것을 받아 컴파일하는 쪽이 정한다(`ShaderCooker.cpp`). 그 둘 사이를 오가는 값이라 여기 있다(그래서
     *          "이 빌드가 무엇을 쿠킹했나"를 밖에서 볼 수 있다).
     */
    struct ShaderCookRequest
    {
        string         _shaderPath;
        string         _entryPoint;
        ShaderStage    _stage{ ShaderStage::Vertex };
        vector<string> _listPermutation;
        /// @brief `_listPermutation` 의 해시입니다. 쿠킹된 파일 이름에 들어갑니다(순서 무관).
        uint64 _permutationHash{ 0 };
        /**
         * @brief 이 요청을 낸 출처입니다 — 묶음마다 그 에셋 id(`Content/` 기준 정규화 경로 — 파이프라인 XML, 머티리얼, 둘 다)가 **모두** 팩에 닿으면 이 바이너리가 필요하다.
         * @details 빈 묶음은 늘 필요하다(패스 종류 표 · 엔진 셰이더). 굽기는 출처를 보지 않고 모두 굽는다(언리얼처럼 닿은 머티리얼만 굽는 일은 하지 않는다) — 팩 쿠커가 "닿은 출처가
         *          요청했는데 원본이 닿지 않아 버리는" 바이너리만 경고하는 데 쓴다. 닿지 않은 개발 파이프라인(`deferredpipeline_gi.xml`)의 셰이더를 버리는 것은 정상이다.
         */
        vector<vector<string>> _listOrigin;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShaderCooker
     * @brief HLSL 소스를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이너리로 미리 컴파일(쿠킹)하고 산출물 이름을 짓는 엔진 유틸리티입니다.
     */
    struct SW_API ShaderCooker
    {
        /** @brief 쿠킹한 셰이더 산출물의 접미사입니다(백엔드 바이너리 셋, 리플렉션 매니페스트, 쿠킹 스탬프). 쿠킹 스테이징에만 있다 — 소스 트리에 있으면 낡은 잔재라 팩에 넣지 않는다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            {              ".dxil", AssetSuffixRole::Cooked},
            {              ".dxbc", AssetSuffixRole::Cooked},
            {               ".spv", AssetSuffixRole::Cooked},
            {"reflection.manifest", AssetSuffixRole::Cooked},
            {         "cook.stamp", AssetSuffixRole::Cooked},
        };

        /** @brief 셰이더 바이너리 에셋 종류입니다(등록은 `ShaderCooker.cpp` 의 `SW_ASSET_TYPE`). 전용 서버 패키지가 빼는 묶음 `ShaderBinary` 다. */
        static constexpr AssetTypeDef kAssetType{ "ShaderBinary", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, "ShaderBinary" };

        /**
         * @brief 셰이더 하나를 지정한 포맷과 스테이지로 컴파일해 디스크에 바이너리로 저장합니다.
         * @param source 원본 HLSL 소스의 리소스 경로(컴파일러가 해석한다 — 디스크 경로를 넣지 않는다)
         * @param outputPath 출력 바이너리 파일 경로
         * @param entryPoint 셰이더 진입점 함수 이름(예: "VSMain", "PSMain", "CSMain")
         * @param stage 셰이더 스테이지(Vertex, Pixel, Compute)
         * @param targetFormat 대상 포맷(DXIL_D3D12, SPIRV_Vulkan, DXBC_D3D11)
         * @param pOutResult 쿠킹 결과 상세 정보(선택)
         * @return 성공하면 true 입니다.
         */
        [[nodiscard]] static bool cookShader( const ShaderSourceID& source, string_view outputPath, string_view entryPoint,
                                              ShaderStage stage, ShaderTargetFormat targetFormat,
                                              const vector<string>* pListPermutation = nullptr,
                                              ShaderCookResult*     pOutResult       = nullptr );

        // "무엇을 쿠킹할지"와 "모두 쿠킹"은 여기 없다. 파이프라인 XML 과 패스 종류를 아는 렌더러의 정책이라 `Renderer/Cook/ShaderCookDriver` 에 있다.
        // 산출물이 최신인지(스탬프 · 의존 기록 · 도구 지문)는 `ShaderCookStamp` 가 판정한다. 여기는 셰이더 하나를 쿠킹하는 법과 이름 짓기만 둔다.

        /** @brief 셰이더 파일의 스템을 소문자로 반환합니다. 쿠킹된 파일 이름의 앞부분입니다(`computeBinaryFileName` 의 짝). */
        static string getStemLower( string_view filePath );

        /** @brief 매크로 퍼뮤테이션(문자열 목록)으로 64비트 해시를 계산합니다(퍼뮤테이션이 없으면 0). */
        static uint64 computePermutationHash( const vector<string>& listPermutation );

        /** @brief 매크로 퍼뮤테이션(ShaderMacroDefine 목록)으로 64비트 해시를 계산합니다(퍼뮤테이션이 없으면 0). */
        static uint64 computePermutationHash( const vector<ShaderMacroDefine>& listDefine );

        /** @brief 셰이더 파일 이름, 스테이지, 진입점, 퍼뮤테이션 해시를 조합한 표준 바이너리 파일 이름을 만듭니다. */
        static string computeBinaryFileName( string_view stemLower, ShaderStage stage,
                                             string_view entryPoint, uint64 permutationHash, string_view ext );

        /** @brief 타깃 포맷의 바이너리 서브폴더 이름(쿠킹 테이블의 `shader_folder`)입니다. 모르는 값(`Count` · 범위 밖)은 `SW_FATAL` 입니다. */
        static string_view getSubfolderForFormat( ShaderTargetFormat format );

        /** @brief 타깃 포맷에 해당하는 확장자(".dxbc", ".dxil", ".spv")를 반환합니다. 모르는 값은 `SW_FATAL` 입니다. */
        static string_view getExtensionForFormat( ShaderTargetFormat format );

        /** @brief 쿠킹 테이블의 셰이더 폴더 이름(`dx11`, `dx12`, `vulkan`, `opengl`)으로 ShaderTargetFormat 을 역으로 구합니다. 모르면 Count 입니다. */
        static ShaderTargetFormat getFormatForSubfolder( string_view subfolder );
    };
} // namespace sw
