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
        string                 _sourcePath;
        string                 _outputPath;
        string                 _entryPoint;
        ShaderStage            _stage;
        ShaderTargetFormat     _targetFormat;
        vector<uint8>          _bytecode; ///< 쓴(또는 이미 같은 내용인) 바이너리의 바이트 — 부르는 쪽이 다시 읽지 않고 리플렉션을 뽑는다
        uint8                  _bSuccess : 1;
        uint8                  _bWritten : 1; ///< 출력 파일이 다른 내용이라 새로 썼다(거짓이면 이미 같은 내용이다)
        [[maybe_unused]] uint8 _reserved : 6;

        ShaderCookResult()
            : _sourcePath{}
            , _outputPath{}
            , _entryPoint{}
            , _stage{ ShaderStage::Vertex }
            , _targetFormat{ ShaderTargetFormat::SPIRV_Vulkan }
            , _bytecode{}
            , _bSuccess{ SW_FALSE }
            , _bWritten{ SW_FALSE }
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
     * @struct ShaderBinaryNameCollision
     * @brief 같은 바이너리 이름에 정본 define 이 다른 두 쿠킹 요청입니다(`ShaderCooker::findBinaryNameCollisions`). 쿠킹은 이것이 하나라도 있으면 실패로 센다.
     */
    struct ShaderBinaryNameCollision
    {
        size_t _firstIndex{ 0 };  ///< 그 이름을 먼저 받은 요청(목록 안 위치)
        size_t _secondIndex{ 0 }; ///< 같은 이름에 다른 define 으로 온 요청 — 쿠킹하지 않는다
        string _fileName;         ///< 겹친 바이너리 이름(확장자 없음)
    };

    /**
     * @struct ShaderCooker
     * @brief HLSL 소스를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이너리로 미리 컴파일(쿠킹)하고 산출물 이름을 짓는 엔진 유틸리티입니다.
     */
    struct SW_API ShaderCooker
    {
        /** @brief 쿠킹한 셰이더 산출물의 접미사입니다(백엔드 바이너리 셋, 리플렉션 매니페스트). 쿠킹 스테이징에만 있다 — 소스 트리에 있으면 낡은 잔재라 팩에 넣지 않는다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            {              ".dxil", AssetSuffixRole::Cooked},
            {              ".dxbc", AssetSuffixRole::Cooked},
            {               ".spv", AssetSuffixRole::Cooked},
            {"reflection.manifest", AssetSuffixRole::Cooked},
        };

        /** @brief 셰이더 바이너리 에셋 종류입니다(등록은 `ShaderCooker.cpp` 의 `SW_ASSET_TYPE`). 전용 서버 패키지가 빼는 묶음 `ShaderBinary` 다. */
        static constexpr AssetTypeDef kAssetType{ "ShaderBinary", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, "ShaderBinary" };

        /**
         * @brief 쿠킹 요청 하나의 컴파일 요청입니다(굽기와 쿠킹본 판정이 같은 요청으로 기록을 찾는다).
         * @param enginePolicy 쿠킹 시작에 뜬 엔진 설정 사본
         */
        [[nodiscard]] static ShaderCompileDesc makeCompileDesc( const ShaderSourceID& source, string_view entryPoint, ShaderStage stage, ShaderTargetFormat targetFormat,
                                                                const ShaderEnginePolicy& enginePolicy, const vector<string>& listPermutation );
        /**
         * @brief 컴파일 요청 @p desc 를 컴파일해 @p outputPath 에 둡니다. 이미 같은 내용이면 다시 쓰지 않습니다.
         * @param outputPath 출력 바이너리 파일 경로
         * @param outResult 바이트 · 새로 썼는지
         * @return 성공하면 true 입니다.
         */
        [[nodiscard]] static bool cookShader( const ShaderCompileDesc& desc, string_view outputPath, ShaderCookResult& outResult );

        // "무엇을 쿠킹할지"와 "모두 쿠킹"은 여기 없다. 파이프라인 XML 과 패스 종류를 아는 렌더러의 정책이라 `Renderer/Cook/ShaderCookDriver` 에 있다.
        // 산출물이 최신인지는 컴파일의 쿠킹 기록(`ShaderCompiler::findCurrentRecord`) 하나로 본다 — 최신이면 컴파일은 DDC 적중이라 쿠커가 따로 판정하지 않는다.

        /** @brief 셰이더 파일의 스템을 소문자로 반환합니다. 쿠킹된 파일 이름의 앞부분입니다(`computeBinaryFileName` 의 짝). */
        static string getStemLower( string_view filePath );

        /** @brief 매크로 퍼뮤테이션(문자열 목록)으로 64비트 해시를 계산합니다(퍼뮤테이션이 없으면 0). */
        static uint64 computePermutationHash( const vector<string>& listPermutation );

        /** @brief 매크로 퍼뮤테이션(ShaderMacroDefine 목록)으로 64비트 해시를 계산합니다(퍼뮤테이션이 없으면 0). */
        static uint64 computePermutationHash( const vector<ShaderMacroDefine>& listDefine );

        /**
         * @brief define 집합의 정본 글 목록입니다 — `NAME=VALUE`(값 없으면 `=1`)를 정렬했고 이름 없는 줄은 뺍니다. 받은 차례와 무관합니다.
         * @details 쿠킹 파일 이름(`computePermutationHash`) · 메모리 캐시 키(`ShaderCache`) · 셰이더 DDC 키가 모두 이것으로 define 을 적는다 — 받은 차례로 적으면
         *          같은 조합이 차례만 달라 헛 미스가 나고 DDC 에 두 벌이 쌓인다. 언리얼도 셰이더 컴파일 환경의 정의를 정렬해 DDC 키를 만든다.
         */
        [[nodiscard]] static vector<string> makeCanonicalDefines( const vector<ShaderMacroDefine>& listDefine );
        /** @brief 글 목록 판 정본입니다(쿠킹 요청의 `_listPermutation` — `FOO` 는 `FOO=1`, 빈 줄은 뺀다, 정렬). 문자열 판 `computePermutationHash` 가 이것을 해시한다. */
        [[nodiscard]] static vector<string> makeCanonicalDefines( const vector<string>& listPermutation );

        /**
         * @brief 같은 바이너리 이름(경로 · 스테이지 · 진입점 · 퍼뮤테이션 해시 16 자리)에 정본 define 이 다른 요청 짝을 찾습니다. 같은 define 의 중복 요청은 짝이 아니다.
         * @details 검사는 쿠킹 드라이버 한 곳에서만 한다 — 팩은 실행 중 바뀌지 않아 로드 때 대조는 두지 않는다(로드마다 비용).
         */
        [[nodiscard]] static vector<ShaderBinaryNameCollision> findBinaryNameCollisions( const vector<ShaderCookRequest>& listRequest );

        /** @brief 셰이더 파일 이름, 스테이지, 진입점, 퍼뮤테이션 해시를 조합한 표준 바이너리 파일 이름을 만듭니다. */
        static string computeBinaryFileName( string_view stemLower, ShaderStage stage,
                                             string_view entryPoint, uint64 permutationHash, string_view ext );

        /** @brief 타깃 포맷의 바이너리 서브폴더 이름(쿠킹 테이블의 `shader_folder`)입니다. 모르는 값(`Count` · 범위 밖)은 `reportFatalError` 입니다. */
        static string_view getSubfolderForFormat( ShaderTargetFormat format );

        /** @brief 타깃 포맷에 해당하는 확장자(".dxbc", ".dxil", ".spv")를 반환합니다. 모르는 값은 `reportFatalError` 입니다. */
        static string_view getExtensionForFormat( ShaderTargetFormat format );

        /** @brief 쿠킹 테이블의 셰이더 폴더 이름(`dx11`, `dx12`, `vulkan`, `opengl`)으로 ShaderTargetFormat 을 역으로 구합니다. 모르면 Count 입니다. */
        static ShaderTargetFormat getFormatForSubfolder( string_view subfolder );
    };
} // namespace sw
