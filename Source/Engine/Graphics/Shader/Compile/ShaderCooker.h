/**
 * @file ShaderCooker.h
 * @brief HLSL 소스를 타깃 백엔드(DXIL, SPIR-V, DXBC) 바이트코드로 미리 컴파일(쿠킹)하는 유틸리티입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
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
         * @brief `bin/<rhi>/cook.stamp` 를 지금 소스의 내용 해시와, 소스마다 컴파일이 연 헤더(와 그 내용 해시)로 다시 씁니다(쿠킹이 끝난 뒤). 다른 include 루트 도메인(`collectIncludeRootDomains`)은 @p resourceRoot 아래에서 @p listPriority 로 찾습니다.
         * @param pFailedSource 이번 쿠킹에서 컴파일에 실패한 소스(정규화한 절대 경로)입니다. 스탬프에서 빠져 다음 쿠킹이 다시 시도합니다.
         *                      실패한 셰이더의 옛 바이너리 위에 지금 소스의 해시를 기록하면 그 셰이더가 영영 최신으로 판정되기 때문입니다.
         * @param mapIncludedFile 소스(정규화한 절대 경로)마다 그 소스의 산출물을 만든 컴파일이 연 헤더(절대 경로)입니다. 포맷 · 퍼뮤테이션 전부의 합집합이며
         *                        이번에 다시 컴파일하지 않은 요청의 몫은 `isCookedOutputCurrent` 가 돌려준 목록으로 채운다. 여기 없는 소스는 헤더에 기대지 않는 것으로 적히므로,
         *                        컴파일한 소스를 빼먹으면 그 소스의 헤더 변경이 낡음으로 잡히지 않는다.
         * @return 스탬프를 썼으면 true 입니다. 도메인 설정이 틀렸거나 가려진 헤더가 있거나 헤더를 읽지 못하거나 shaders 폴더 밖이거나 파일을 쓰지 못했으면 false 이며, 호출자(쿠킹)는 그것을 실패로 센다 — 스탬프 없이 "성공" 으로 끝나면 다음 판정이 그제야 낡았다고 알린다.
         * @details 루트와 우선순위를 전역(`AssetPaths`)에서 읽지 않고 받는다. 테스트는 임시 트리의 루트와 **자기 우선순위**를 넘겨
         *          실제 `Content/` 도, 같은 프로세스의 앞선 케이스가 바꿔 둔 전역 우선순위도 보지 않는다(우선순위가 비면 스탬프를 쓰지 않아 판정이 흔들린다).
         *          스탬프를 쓰면 그 폴더의 스탬프 읽기 캐시를 버린다.
         */
        [[nodiscard]] static bool writeCookStamp( string_view resourceRoot, const vector<string>& listPriority, string_view binDirectory, const unordered_set<string>* pFailedSource,
                                                  const unordered_map<string, vector<string>>& mapIncludedFile );

        /**
         * @brief @p binDirectory 의 셰이더 바이너리(`.dxil` · `.dxbc` · `.spv`) 중 파일 이름이 @p uniqueKeepFileName 에 없는 것을 지웁니다. 스탬프 · 매니페스트 같은 다른 파일은 건드리지 않습니다.
         * @param uniqueKeepFileName 이번 쿠킹이 쓴 리플렉션 매니페스트의 키(= 바이너리 파일 이름, `computeBinaryFileName`)입니다.
         * @param outRemovedCount 지운 파일 수입니다.
         * @return 지워야 할 파일을 모두 지웠으면 true 입니다. 하나라도 못 지웠으면 false 입니다(그 파일이 남아 판정이 진다).
         * @details 퍼뮤테이션 축이 늘거나 셰이더 · 목록 줄이 사라지면 요청의 파일 이름이 바뀌어 옛 바이너리가 매니페스트 밖에 남는다. 그대로 두면 `verifyCookedShaders` 가
         *          "매니페스트에 없는 바이너리" 로 지고 배포 팩에 실린다. 쿠커가 새 매니페스트를 쓴 자리에서 스스로 치우게 해 손으로 지울 일을 없앤다.
         */
        [[nodiscard]] static bool removeUnlistedBinaries( string_view binDirectory, const unordered_set<string>& uniqueKeepFileName, uint32& outRemovedCount );

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
         * @brief 소스와 **그 소스가 실제로 연 헤더들**, 도구 지문을 합친 지문입니다. 산출물 · 캐시 항목이 최신인지 판단하는 **내용 기준**입니다.
         * @param absShaderPath 주 소스(`.hlsl`)의 절대 경로
         * @param listIncludedFile 컴파일이 연 헤더(`ShaderCompileResult::_listIncludedFile`). 모르면 빈 목록이며 그러면 소스와 도구 지문만 본다 — 헤더 변경은 잡히지 않는다.
         * @return 소스나 헤더 하나라도 읽지 못하면 0 입니다(같은 지문이 나올 수 없다). 헤더 순서와 무관하다.
         * @details 예전에는 `.hlsli` **전부**의 해시를 섞어 헤더 하나를 고치면 모든 셰이더가 낡았다. 어느 헤더를 읽었는지는 컴파일러가 소스를 읽는 순간 include 처리기가 안다
         *          (바이트코드 리플렉션은 바인딩 정보라 담지 못한다). 그래서 그 목록을 컴파일 결과에서 받아 이 셰이더의 헤더만 본다.
         *
         *          **파일 시간이 아니라 내용이다.** 이 저장소는 쿠킹된 바이너리까지 커밋하므로 `git pull`
         *          이 소스와 산출물의 mtime 을 임의의 순서로 덮어쓴다. 소스가 바뀌었는데도 "산출물이
         *          더 새것"으로 판정되어 그대로 넘어가고, 한 백엔드만 다른 이미지를 내면 백엔드 버그로 보인다.
         */
        static uint64 computeSourceFingerprint( string_view absShaderPath, const vector<string>& listIncludedFile );

        /** @brief 의존 기록 파일의 접미사입니다(캐시 파일 이름 뒤에 붙입니다). */
        inline static constexpr const utf8* kIncludeRecordSuffix = ".deps";

        /**
         * @brief 컴파일이 연 헤더와 그 내용 해시를 @p recordPath 에 적습니다(컴파일 캐시 항목 옆의 의존 기록).
         * @details 캐시 키는 컴파일 전에 필요하고 헤더 목록은 컴파일 뒤에야 알아서(순환), 키에 헤더를 넣지 않고 캐시 항목 옆에 이 기록을 둔다.
         *          맞출 때 `readIncludeRecord` 가 기록된 헤더가 지금도 같은지 본다. 목록이 비어도 기록은 쓴다(헤더에 기대지 않는 셰이더다).
         * @return 헤더를 읽지 못하거나 파일을 쓰지 못했으면 false 입니다.
         */
        [[nodiscard]] static bool writeIncludeRecord( string_view recordPath, const vector<string>& listIncludedFile );

        /**
         * @brief @p recordPath 의 의존 기록을 읽어, 기록된 헤더가 **모두 지금도 같은 내용**이면 그 목록을 @p outListIncludedFile 에 담고 true 를 돌려줍니다.
         * @return 기록이 없거나 형식이 틀리거나 헤더 하나라도 없어졌거나 읽지 못하거나 내용이 바뀌었으면 false 입니다(캐시 미스로 다룬다). 그때 목록은 비웁니다.
         */
        [[nodiscard]] static bool readIncludeRecord( string_view recordPath, vector<string>& outListIncludedFile );

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
         * @brief 셰이더를 가질 수 있는 도메인(`engine` · `common` · `game/<팩>` · 그 밖에 우선순위에 적힌 것)을 @p listPriority 에서 발견합니다.
         * @param listPriority `EngineConfig._listResourcePriority`(`AssetPaths::getSearchPriority()`) — 도메인 목록의 기준은 이 설정 데이터다.
         *                     `game` 토큰은 @p resourceRoot 의 `game/` 아래 폴더를 훑어 `game/<이름>` 으로 펼친다(엔진은 게임 이름을 모른다). 나머지 토큰은 그대로 도메인이다.
         * @return 목록이 비었거나 include 루트 종류(`engine` · `common`)가 빠졌으면 오류를 남기고 false 입니다(기본 목록으로 넘어가지 않는다).
         */
        [[nodiscard]] static bool collectShaderDomains( string_view resourceRoot, const vector<string>& listPriority, vector<string>& outListDomain );

        /**
         * @brief 셰이더 `#include` 가 자기 폴더 다음으로 찾는 도메인 **종류**(`engine`, `common` 순)를 돌려줍니다(`<도메인>/shaders`).
         * @details 컴파일러의 검색 경로와 쿠킹 스탬프가 같은 함수를 쓴다. 게임 셰이더의 include 루트는 자기 도메인 + 이 둘이며 다른 게임은 들어가지 않는다.
         *          스탬프는 자기 도메인의 소스에 더해 이 도메인들의 `.hlsli` 를 `<도메인>:<상대 경로>` 키로 기록한다. common 셰이더가 include 하는
         *          engine 헤더를 고치면 common 산출물도 낡은 것이 되어야 한다.
         * @return @p listPriority 에 두 종류 중 하나라도 없으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool collectIncludeRootDomains( const vector<string>& listPriority, vector<string>& outListDomain );

        /**
         * @brief @p absShaderPath 가 속한 도메인의 헤더가 include 루트(engine · common)의 헤더와 같은 상대 경로를 가지면 그 쌍을 @p outListProblem 에 적습니다.
         * @details include 검색은 자기 폴더가 먼저라 같은 이름의 헤더가 엔진 헤더를 조용히 가린다. 가림은 고르지 않고 오류로 멈춘다.
         *          도메인 밖의 파일(임시 폴더의 소스)은 검사할 것이 없어 true 입니다. 컴파일(`ShaderCompiler`)과 `verifyCookedShaders` 가 부른다.
         * @return 문제가 없으면 true 입니다. 설정이 잘못됐을 때도 false 입니다(오류를 남김).
         */
        [[nodiscard]] static bool findShadowedHeaders( string_view resourceRoot, const vector<string>& listPriority, string_view absShaderPath, vector<string>& outListProblem );

        /**
         * @brief 쿠킹된 산출물이 지금 소스에서 나온 것인지 `bin/<rhi>/cook.stamp` 의 내용 해시로 확인합니다.
         * @param binDirectory `<domain>/shaders/bin/<rhi>`(스탬프가 있는 폴더)입니다.
         * @param absShaderPath 원본 `.hlsl` 의 절대 경로입니다.
         * @param pOutListIncludedFile 최신이면 그 소스의 스탬프에 적힌 헤더(절대 경로)를 담습니다(선택). 최신이 아니면 비웁니다. 컴파일을 건너뛴 요청의 몫을 `writeCookStamp` 에 넘길 때 씁니다.
         * @details 최신이려면 소스 해시가 같고 **스탬프에 적힌 이 소스의 헤더가 모두 지금도 같은 내용**이어야 한다. 이 소스가 열지 않은 헤더는 보지 않는다.
         *          헤더가 없어졌거나 읽지 못하면 낡음이다. 옛 판의 스탬프도 낡음이다.
         */
        static bool isCookedOutputCurrent( string_view binDirectory, string_view absShaderPath, vector<string>* pOutListIncludedFile = nullptr );
        /** @brief `isCookedOutputCurrent` 의 본체 — 다른 include 루트 도메인의 헤더를 @p resourceRoot 아래에서 @p listPriority 로 찾아 봅니다(`writeCookStamp` 와 같은 짝). */
        static bool isCookedOutputCurrent( string_view resourceRoot, const vector<string>& listPriority, string_view binDirectory, string_view absShaderPath, vector<string>* pOutListIncludedFile = nullptr );

        /**
         * @brief 도메인(`engine` · `common` · `game/<팩>`)마다 커밋된 @p shaderFolder 바이너리가 지금 소스에서 나온 것인지 보고 문제를 @p outListProblem 에 더합니다.
         * @details 판정은 스탬프를 쓰는 쪽(`writeCookStamp`)과 같은 수집 규칙 · 같은 해시다 — 규칙이 한 언어에만 있으므로 "두 언어가 같은 규칙인가" 를 볼 게이트가 없다.
         *          보는 것: 바이너리 폴더 · 스탬프 · 스탬프 판 · 도구 지문(스탬프의 `kToolchainKey` 줄), 스탬프에 빠진 · 바뀐 · 남은 소스, 소스마다 스탬프에 적힌 헤더가 지금도 같은지,
         *          바이너리가 하나라도 있는지, 리플렉션 매니페스트가 있는지와 매니페스트에 없는 바이너리(쿠커 레시피가 늘었는데 다시 베이크하지 않은 것). 소스가 없는 도메인은 건너뛴다.
         *          shortcut: 헤더는 컴파일러가 **먼저 찾은 하나**만 적힌다. 소스가 그대로인데 앞 순위 루트(자기 폴더 → engine → common)에 같은 이름의 헤더가 새로 생겨 가려지는 경우는
         *          이 판정이 못 잡는다 — `findShadowedHeaders` 가 같은 이름을 오류로 막아 두므로 거기서 걸린다.
         *          `App --verify-shaders`(커밋 훅 · 배포 빌드)와 `ShaderCookStampTest` 가 부릅니다.
         */
        static void verifyCookedShaders( string_view resourceRoot, string_view shaderFolder, vector<string>& outListProblem );
        /** @brief `verifyCookedShaders` 의 본체 — 도메인 발견의 기준인 우선순위 설정을 직접 받습니다(위 판은 `AssetPaths::getSearchPriority()` 를 넘긴다). */
        static void verifyCookedShaders( string_view resourceRoot, const vector<string>& listPriority, string_view shaderFolder, vector<string>& outListProblem );

        /**
         * @brief 스탬프 읽기 결과와 파일 내용 해시 캐시(`ContentHash`)를 버려, 다음 조회가 다시 읽게 합니다.
         * @details 이름은 헤더 해시를 따로 캐시하던 때의 것이다. 지금은 헤더를 열 때마다 내용 해시를 보므로(크기 · 쓰기 시각이 같으면 캐시) 실행 중 `.hlsli` 를 고치면 다음 조회가 스스로 알아챈다.
         *          그래도 수동 리로드와 쿠킹 직후는 "전부 다시 본다" 는 약속이라 부른다 — 같은 시각에 같은 크기로 다시 쓴 파일(테스트)을 놓치지 않는다.
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
