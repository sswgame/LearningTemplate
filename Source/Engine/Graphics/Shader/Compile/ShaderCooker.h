/**
 * @file ShaderCooker.h
 * @brief HLSL 소스를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이트코드로 미리 컴파일(쿠킹)하는 유틸리티입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"

#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
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
        uint64                 _byteCodeSize{ 0 };
        uint8                  _bSuccess : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        ShaderCookResult()
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
} // namespace sw

namespace sw
{
    /**
     * @struct ShaderCookRequest
     * @brief 쿠킹할 대상 하나입니다(셰이더, 진입점, 스테이지, define).
     * @details "무엇을 쿠킹할지"는 파이프라인 XML 과 머티리얼에서 나오고(`ShaderCookRequest.cpp`), "어떻게 쿠킹하는지"는
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
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShaderCooker
     * @brief HLSL 소스를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이너리로 미리 컴파일(쿠킹)하는 엔진 유틸리티입니다.
     */
    struct SW_API ShaderCooker
    {
        /** @brief 쿠킹한 셰이더 산출물의 접미사입니다(백엔드 바이너리 셋, 리플렉션 매니페스트, 쿠킹 스탬프). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            {              ".dxil", AssetSuffixRole::Plain},
            {              ".dxbc", AssetSuffixRole::Plain},
            {               ".spv", AssetSuffixRole::Plain},
            {"reflection.manifest", AssetSuffixRole::Plain},
            {         "cook.stamp", AssetSuffixRole::Plain},
        };
        /** @brief 셰이더 바이너리 에셋 종류입니다(등록은 `ShaderCooker.cpp` 의 `SW_ASSET_TYPE`). 전용 서버 패키지가 빼는 묶음 `ShaderBinary` 다. */
        static constexpr AssetTypeDef kAssetType{ "ShaderBinary", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, "ShaderBinary" };

        /**
         * @brief 셰이더 하나를 지정한 포맷과 스테이지로 컴파일해 디스크에 바이너리로 저장합니다.
         * @param sourcePath 원본 HLSL 소스 파일 경로
         * @param outputPath 출력 바이너리 파일 경로
         * @param entryPoint 셰이더 진입점 함수 이름(예: "VSMain", "PSMain", "CSMain")
         * @param stage 셰이더 스테이지(Vertex, Pixel, Compute)
         * @param targetFormat 대상 포맷(DXIL_D3D12, SPIRV_Vulkan, DXBC_D3D11)
         * @param pOutResult 쿠킹 결과 상세 정보(선택)
         * @return 성공하면 true 입니다.
         */
        [[nodiscard]] static bool cookShader( string_view sourcePath, string_view outputPath, string_view entryPoint,
                                              ShaderStage stage, ShaderTargetFormat targetFormat,
                                              const vector<string>* pListPermutation = nullptr,
                                              ShaderCookResult*     pOutResult       = nullptr );

        // "무엇을 쿠킹할지"와 "모두 쿠킹"은 여기 없다. 파이프라인 XML 과 패스 종류를 아는 렌더러의 정책이라
        // `Renderer/Cook/ShaderCookDriver` 에 있다. 여기는 셰이더 하나를 쿠킹하는 법과 이름 짓기, 최신 판정만 둔다.

        /**
         * @brief `bin/<rhi>/cook.stamp` 를 지금 소스의 내용 해시로 다시 씁니다(쿠킹이 끝난 뒤).
         * @param pFailedSource 이번 쿠킹에서 컴파일에 실패한 소스(정규화한 절대 경로)입니다. 스탬프에서 빠져 다음 쿠킹이 다시 시도합니다.
         *                      실패한 셰이더의 옛 바이너리 위에 지금 소스의 해시를 기록하면 그 셰이더가 영영 최신으로 판정되기 때문입니다.
         */
        static void writeCookStamp( string_view binDirectory, const unordered_set<string>* pFailedSource = nullptr );
        /**
         * @brief `writeCookStamp` 의 본체 — 다른 include 루트 도메인(`kArrIncludeRootDomain`)을 @p resourceRoot 아래에서 찾습니다.
         * @details 루트를 받지 않는 판은 `ResourcePaths` 의 루트를 넘긴다. 테스트는 임시 트리의 루트를 넘겨 실제 `Resource/` 를 보지 않는다
         *          (병렬로 도는 다른 프로세스가 그 트리를 건드려도 판정이 흔들리지 않는다).
         */
        static void writeCookStamp( string_view resourceRoot, string_view binDirectory, const unordered_set<string>* pFailedSource );

        /** @brief 셰이더 파일의 스템을 소문자로 반환합니다. 쿠킹된 파일 이름의 앞부분입니다(`computeBinaryFileName` 의 짝). */
        static string getStemLower( string_view filePath );

        /** @brief 매크로 퍼뮤테이션(문자열 목록)으로 64비트 해시를 계산합니다(퍼뮤테이션이 없으면 0). */
        static uint64 computePermutationHash( const vector<string>& listPermutation );

        /** @brief 매크로 퍼뮤테이션(ShaderMacroDefine 목록)으로 64비트 해시를 계산합니다(퍼뮤테이션이 없으면 0). */
        static uint64 computePermutationHash( const vector<ShaderMacroDefine>& listDefine );

        /** @brief 셰이더 파일 이름, 스테이지, 진입점, 퍼뮤테이션 해시를 조합한 표준 바이너리 파일 이름을 만듭니다. */
        static string computeBinaryFileName( string_view stemLower, ShaderStage stage,
                                             string_view entryPoint, uint64 permutationHash, string_view ext );

        /**
         * @brief 소스와 **공유 헤더(.hlsli)** 를 합친 내용 해시입니다. 산출물이 최신인지 판단하는 **유일한 기준**입니다.
         * @details `.hlsl` 하나만 보면 `binding.hlsli` 같은 공유 헤더를 고쳐도 아무것도 다시 쿠킹하지 않아,
         *          바이너리와 리플렉션 매니페스트가 소스와 조용히 어긋난다(그 어긋남은 DX12 GPU 페이지
         *          폴트로 나타난 적이 있다). 어느 셰이더가 어떤 헤더를 include 하는지는 파싱하지 않고
         *          **모든 .hlsli** 를 넣어 넉넉하게 잡는다. 과하게 쿠킹하는 쪽이 안전하다.
         *
         *          **파일 시간이 아니라 내용이다.** 이 저장소는 쿠킹된 바이너리까지 커밋하므로 `git pull`
         *          이 소스와 산출물의 mtime 을 임의의 순서로 덮어쓴다. 소스가 바뀌었는데도 "산출물이
         *          더 새것"으로 판정되어 그대로 넘어가고, 한 백엔드만 다른 이미지를 내면 백엔드 버그로 보인다.
         */
        static uint64 computeEffectiveSourceHash( string_view absShaderPath );
        /** @brief `computeEffectiveSourceHash` 의 본체 — 공유 헤더를 @p resourceRoot 아래에서 모읍니다. */
        static uint64 computeEffectiveSourceHash( string_view resourceRoot, string_view absShaderPath );
        /** @brief Resource 아래 모든 .hlsli 의 경로와 내용을 합친 해시입니다(값을 캐시합니다). */
        static uint64 getSharedHeaderContentHash();
        /** @brief @p resourceRoot 아래 모든 .hlsli 의 경로와 내용을 합친 해시입니다(루트마다 캐시합니다). */
        static uint64 getSharedHeaderContentHash( string_view resourceRoot );

        /**
         * @brief 컴파일 옵션의 버전입니다. **`ShaderCompiler.cpp` 가 컴파일러에 넘기는 인자(플래그, 백엔드 define)를 바꾸면 올리세요.**
         * @details 이 값은 `getToolchainFingerprint` 에 들어가 쿠킹 스탬프와 디스크 캐시 키를 바꾼다. 올리지 않으면 소스가 그대로라
         *          옛 옵션으로 만든 바이너리가 "최신"으로 남는다.
         */
        inline static constexpr uint32 kCompileOptionsRevision = 1;

        /**
         * @brief 쿠킹에 쓰는 셰이더 컴파일러의 버전 식별자입니다 — vcpkg 포트 `directx-dxc` 의 버전(`build/vcpkg_installed/vcpkg/status`)과 같은 글이어야 한다.
         * @details 컴파일러 파일의 내용 해시가 아니라 버전 글을 지문에 넣는다. 쿠킹본은 Windows 에서 만들어 커밋하고 리눅스도 같은 쿠킹본을 쓰므로,
         *          OS 마다 다른 라이브러리 파일을 해시하면 다른 OS 에서 모든 스탬프가 낡은 것이 된다. 같은 vcpkg 기준선이면 두 OS 의 포트 버전이 같다.
         *          DXC 를 올리면 이 글을 고친다 — 어긋나면 `TestShaderCompilerVersionPin.py` 가 진다.
         */
        inline static constexpr const utf8* kCompilerVersion = "directx-dxc 2026-05-27";

        /**
         * @brief 컴파일러 도구의 지문입니다. 컴파일 옵션 버전, Vulkan 타깃 버전, 컴파일러 버전 식별자를 합칩니다(`makeToolchainFingerprint` 에 이 클래스의 상수를 넣은 값).
         * @details 소스 내용 해시만 보면 DXC 를 바꾸거나 옵션을 고쳐도 아무것도 다시 쿠킹하지 않는다. 슬롯 계약 숫자(시프트 포함)는
         *          `bindingslots.hlsli` 에 있고 그 파일은 include 루트 헤더라 이미 스탬프에 들어간다. 파일이나 OS 에 따라 달라지는 값은 넣지 않는다.
         */
        static uint64 getToolchainFingerprint();
        /** @brief 지문 계산의 본체입니다. 입력만으로 정해집니다(테스트가 상수만으로 같은 값이 나오는지 확인합니다). */
        static uint64 makeToolchainFingerprint( uint32 compileOptionsRevision, uint32 vulkanTargetVersion, string_view compilerVersion );

        /**
         * @brief 셰이더 `#include` 가 자기 폴더 다음으로 찾는 도메인입니다(`<도메인>/shaders`). 컴파일러의 검색 경로와 쿠킹 스탬프가 같은 테이블을 씁니다.
         * @details 스탬프는 자기 도메인의 소스에 더해 이 도메인들의 `.hlsli` 를 `<도메인>:<상대 경로>` 키로 기록한다. common 셰이더가 include 하는
         *          engine 헤더를 고치면 common 산출물도 낡은 것이 되어야 한다.
         */
        inline static constexpr const utf8* kArrIncludeRootDomain[] = { "engine", "common" };

        /**
         * @brief 쿠킹된 산출물이 지금 소스에서 나온 것인지 `bin/<rhi>/cook.stamp` 의 내용 해시로 확인합니다.
         * @param binDirectory `<domain>/shaders/bin/<rhi>`(스탬프가 있는 폴더)입니다.
         * @param absShaderPath 원본 `.hlsl` 의 절대 경로입니다.
         */
        static bool isCookedOutputCurrent( string_view binDirectory, string_view absShaderPath );
        /** @brief `isCookedOutputCurrent` 의 본체 — 다른 include 루트 도메인의 헤더를 @p resourceRoot 아래에서 봅니다. */
        static bool isCookedOutputCurrent( string_view resourceRoot, string_view binDirectory, string_view absShaderPath );

        /**
         * @brief 도메인(`engine` · `common` · `game/<팩>`)마다 커밋된 @p shaderFolder 바이너리가 지금 소스에서 나온 것인지 보고 문제를 @p outListProblem 에 더합니다.
         * @details 판정은 스탬프를 쓰는 쪽(`writeCookStamp`)과 같은 수집 규칙 · 같은 해시다 — 규칙이 한 언어에만 있으므로 "두 언어가 같은 규칙인가" 를 볼 게이트가 없다.
         *          보는 것: 바이너리 폴더 · 스탬프 · 스탬프 판 · 도구 지문(스탬프의 `kToolchainKey` 줄), 스탬프에 빠진 · 바뀐 · 남은 소스, 바이너리가 하나라도 있는지,
         *          리플렉션 매니페스트가 있는지와 매니페스트에 없는 바이너리(쿠커 레시피가 늘었는데 다시 굽지 않은 것). 소스가 없는 도메인은 건너뛴다.
         *          `App --verify-shaders`(커밋 훅 · 배포 빌드)와 `ShaderCookStampTest` 가 부릅니다.
         */
        static void verifyCookedShaders( string_view resourceRoot, string_view shaderFolder, vector<string>& outListProblem );

        /**
         * @brief 캐시된 공유 헤더 해시와 스탬프 읽기 결과를 버려, 다음 조회가 다시 스캔하게 합니다.
         * @details 실행 중 `.hlsli` 를 고치고 수동 리로드를 누르는 경로에서만 부른다. 이것을 부르지 않으면
         *          컴파일 캐시의 키가 그대로라 **바뀐 헤더가 반영되지 않는다.** 로그는 성공을 찍는데
         *          화면은 그대로인, 가장 조용한 종류의 어긋남이다.
         */
        static void invalidateSharedHeaderCache();

        /** @brief 타깃 포맷의 바이너리 서브폴더 이름(쿠킹 테이블의 `shader_folder`)입니다. 모르는 포맷은 기본 백엔드의 폴더를 반환합니다. */
        static string_view getSubfolderForFormat( ShaderTargetFormat format );

        /** @brief 타깃 포맷에 해당하는 확장자(".dxbc", ".dxil", ".spv")를 반환합니다. */
        static string_view getExtensionForFormat( ShaderTargetFormat format );

        /** @brief 쿠킹 테이블의 셰이더 폴더 이름(`dx11`, `dx12`, `vulkan`, `opengl`)으로 ShaderTargetFormat 을 역으로 구합니다. 모르면 Count 입니다. */
        static ShaderTargetFormat getFormatForSubfolder( string_view subfolder );
    };
} // namespace sw
