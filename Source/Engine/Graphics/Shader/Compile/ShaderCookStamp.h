/**
 * @file ShaderCookStamp.h
 * @brief 쿠킹된 셰이더 산출물이 최신인지 판정합니다 — 소스 · 실제로 연 헤더의 내용 해시와 도구 지문을 `cook.stamp` · `.deps` 에 적고 읽는다(파일 시간을 보지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw
{
    /**
     * @class ShaderCookStampCache
     * @brief 쿠킹 한 번이 `bin/<rhi>/cook.stamp` 를 폴더마다 한 번만 읽게 하는 읽기 캐시입니다. 쿠킹을 도는 쪽(`ShaderCookDriver` 의 쿠킹 상태)이 값으로 듭니다.
     * @details 여러 스레드가 함께 써도 됩니다(안에서 잠근다). 예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다 — 캐시가 쓸모 있는 기간이 쿠킹 한 번이라
     *          그 쿠킹이 든다(언리얼 쿡 커맨드렛이 쿡 한 번의 상태를 `UCookOnTheFlyServer` 에 두는 자리). 내용은 `ShaderCookStamp.cpp` 에만 있다.
     */
    class SW_API ShaderCookStampCache
    {
    public:
        struct State; ///< 폴더별 스탬프 해석 결과와 잠금(정의는 ShaderCookStamp.cpp)

        ShaderCookStampCache();
        ~ShaderCookStampCache();
        ShaderCookStampCache( const ShaderCookStampCache& )            = delete;
        ShaderCookStampCache& operator=( const ShaderCookStampCache& ) = delete;

        /** @brief 내용입니다. `ShaderCookStamp` 만 씁니다. */
        State& getState() { return *_pState; }

    private:
        unique_ptr<State> _pState;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShaderCookStamp
     * @brief 쿠킹된 산출물이 최신인지 판정하고, 쿠킹이 끝난 뒤 그 판정의 근거(스탬프 · 의존 기록)를 씁니다. 매니페스트 밖 옛 바이너리 치우기 · 쿠킹본 검증도 여기 있다.
     */
    struct SW_API ShaderCookStamp
    {
        /**
         * @brief `bin/<rhi>/cook.stamp` 를 지금 소스의 내용 해시와, 소스마다 컴파일이 연 헤더(와 그 내용 해시)로 다시 씁니다(쿠킹이 끝난 뒤). 다른 include 루트 도메인의 자리는 @p resolver 가 정합니다(컴파일러와 같은 규칙).
         * @param pFailedSource 이번 쿠킹에서 컴파일에 실패한 소스(정규화한 절대 경로)입니다. 스탬프에서 빠져 다음 쿠킹이 다시 시도합니다.
         *                      실패한 셰이더의 옛 바이너리 위에 지금 소스의 해시를 기록하면 그 셰이더가 영영 최신으로 판정되기 때문입니다.
         * @param mapIncludedFile 소스(정규화한 절대 경로)마다 그 소스의 산출물을 만든 컴파일이 연 헤더(절대 경로)입니다. 포맷 · 퍼뮤테이션 전부의 합집합이며
         *                        이번에 다시 컴파일하지 않은 요청의 몫은 `isCookedOutputCurrent` 가 돌려준 목록으로 채운다. 여기 없는 소스는 헤더에 기대지 않는 것으로 적히므로,
         *                        컴파일한 소스를 빼먹으면 그 소스의 헤더 변경이 낡음으로 잡히지 않는다.
         * @return 스탬프를 썼으면 true 입니다. 가려진 헤더가 있거나 헤더를 읽지 못하거나 include 루트 밖이거나 파일을 쓰지 못했으면 false 이며, 호출자(쿠킹)는 그것을 실패로 센다 — 스탬프 없이 "성공" 으로 끝나면 다음 판정이 그제야 낡았다고 알린다.
         * @details 스탬프를 쓰면 @p stampCache 를 비운다(다음 판정이 새 스탬프를 읽는다).
         */
        [[nodiscard]] static bool writeCookStamp( const ShaderIncludeResolver& resolver, string_view sourceShadersDir, string_view binDirectory,
                                                  const unordered_set<string>* pFailedSource, const unordered_map<string, vector<string>>& mapIncludedFile,
                                                  ShaderCookStampCache& stampCache );

        /**
         * @brief @p binDirectory 의 셰이더 바이너리(`.dxil` · `.dxbc` · `.spv`) 중 파일 이름이 @p uniqueKeepFileName 에 없는 것을 지웁니다. 스탬프 · 매니페스트 같은 다른 파일은 건드리지 않습니다.
         * @param uniqueKeepFileName 이번 쿠킹이 쓴 리플렉션 매니페스트의 키(= 바이너리 파일 이름, `computeBinaryFileName`)입니다.
         * @param outRemovedCount 지운 파일 수입니다.
         * @return 지워야 할 파일을 모두 지웠으면 true 입니다. 하나라도 못 지웠으면 false 입니다(그 파일이 남아 판정이 진다).
         * @details 퍼뮤테이션 축이 늘거나 셰이더 · 목록 줄이 사라지면 요청의 파일 이름이 바뀌어 옛 바이너리가 매니페스트 밖에 남는다. 그대로 두면 `verifyCookedShaders` 가
         *          "매니페스트에 없는 바이너리" 로 지고 배포 팩에 실린다. 쿠커가 새 매니페스트를 쓴 자리에서 스스로 치우게 해 손으로 지울 일을 없앤다.
         */
        [[nodiscard]] static bool removeUnlistedBinaries( string_view binDirectory, const unordered_set<string>& uniqueKeepFileName, uint32& outRemovedCount );

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
         *          기록은 짝 바이트코드의 내용 해시(@p bytecodeHash)를 싣는다 — 바이트코드와 기록은 따로 원자적으로 쓰이므로(여러 프로세스가 같은 항목을 함께 쓴다),
         *          짝이 맞지 않는 둘(한쪽만 바뀐 순간 · 쓰다 멈춤)을 읽는 쪽이 해시로 가려 다시 컴파일한다. 옛 기록을 먼저 지우는 단계(프로세스끼리 경쟁했다)가 필요 없다.
         * @return 헤더를 읽지 못하거나 파일을 쓰지 못했으면 false 입니다.
         */
        [[nodiscard]] static bool writeIncludeRecord( string_view recordPath, const vector<string>& listIncludedFile, uint64 bytecodeHash );

        /**
         * @brief @p recordPath 의 의존 기록을 읽어, 기록된 헤더가 **모두 지금도 같은 내용**이면 그 목록을 @p outListIncludedFile 에, 짝 바이트코드의 내용 해시를 @p outBytecodeHash 에 담고 true 를 돌려줍니다.
         * @return 기록이 없거나 형식이 틀리거나 헤더 하나라도 없어졌거나 읽지 못하거나 내용이 바뀌었으면 false 입니다(캐시 미스로 다룬다). 그때 목록은 비웁니다.
         */
        [[nodiscard]] static bool readIncludeRecord( string_view recordPath, vector<string>& outListIncludedFile, uint64& outBytecodeHash );

        /**
         * @brief 컴파일러 도구의 지문입니다. 컴파일러를 부르는 코드의 지문(`CookCodeFingerprint` "ShaderCompiler" — 인자 · 플래그를 고치면 저절로 바뀐다), Vulkan 타깃 버전, 설치된 컴파일러 버전(vcpkg 설치 기록)을 합칩니다(`makeToolchainFingerprint` 에 이 클래스의 상수를 넣은 값).
         * @details 소스 내용 해시만 보면 DXC 를 바꾸거나 옵션을 고쳐도 아무것도 다시 쿠킹하지 않는다. 슬롯 계약 숫자(시프트 포함)는
         *          `binding_slots.hlsli` 에 있고 그 파일은 include 루트 헤더라 이미 스탬프에 들어간다. 파일이나 OS 에 따라 달라지는 값은 넣지 않는다.
         */
        static uint64 getToolchainFingerprint();

        /** @brief 지문 계산의 본체입니다. 입력만으로 정해집니다(테스트가 상수만으로 같은 값이 나오는지 확인합니다). */
        static uint64 makeToolchainFingerprint( uint64 compilerCodeFingerprint, uint32 vulkanTargetVersion, string_view compilerVersion );

        /**
         * @brief 셰이더 `#include` 가 자기 폴더 다음으로 찾는 도메인 **종류**(`engine`, `common` 순)를 돌려줍니다(`<도메인>/shaders`).
         * @details 컴파일러의 검색 경로와 쿠킹 스탬프가 같은 함수를 쓴다. 게임 셰이더의 include 루트는 자기 도메인 + 이 둘이며 다른 게임은 들어가지 않는다.
         *          스탬프는 자기 도메인의 소스에 더해 이 도메인들의 `.hlsli` 를 `<도메인>:<상대 경로>` 키로 기록한다. common 셰이더가 include 하는
         *          engine 헤더를 고치면 common 산출물도 낡은 것이 되어야 한다.
         * @return @p listPriority 에 두 종류 중 하나라도 없으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool collectIncludeRootDomains( const vector<string>& listPriority, vector<string>& outListDomain );

        /**
         * @brief 소스 도메인의 `shaders` 폴더 @p shadersFolder 의 헤더가 include 루트(engine · common)의 헤더와 같은 상대 경로를 가지면 그 쌍을 @p outListProblem 에 적습니다.
         * @details include 검색은 자기 폴더가 먼저라 같은 이름의 헤더가 엔진 헤더를 조용히 가린다. 가림은 고르지 않고 오류로 멈춘다. 컴파일(`ShaderCompiler`)과 `verifyCookedShaders` 가 부른다.
         * @return 문제가 없으면 true 입니다. 헤더 목록을 다 얻지 못했을 때도 false 입니다.
         */
        [[nodiscard]] static bool findShadowedHeaders( const ShaderIncludeResolver& resolver, const AbsolutePath& shadersFolder, vector<string>& outListProblem );

        /**
         * @brief 쿠킹된 산출물이 지금 소스에서 나온 것인지 `bin/<rhi>/cook.stamp` 의 내용 해시로 확인합니다.
         * @param binDirectory 스탬프가 있는 폴더(`<쿠킹 스테이징>/<domain>/shaders/bin/<rhi>`)입니다.
         * @param absShaderPath 원본 `.hlsl` 의 절대 경로입니다.
         * @param sourceShadersDir 원본이 든 `shaders` 폴더(`ShaderCompileInput::_shadersFolder`)입니다 — 스탬프 키의 기준이다.
         * @param stampCache 이 쿠킹의 스탬프 읽기 캐시입니다(폴더마다 한 번 읽는다).
         * @param pOutListIncludedFile 최신이면 그 소스의 스탬프에 적힌 헤더(절대 경로)를 담습니다(선택). 최신이 아니면 비웁니다. 컴파일을 건너뛴 요청의 몫을 `writeCookStamp` 에 넘길 때 씁니다.
         * @details 최신이려면 소스 해시가 같고 **스탬프에 적힌 이 소스의 헤더가 모두 지금도 같은 내용**이어야 한다. 이 소스가 열지 않은 헤더는 보지 않는다.
         *          헤더가 없어졌거나 읽지 못하면 낡음이다. 옛 판의 스탬프도 낡음이다.
         */
        static bool isCookedOutputCurrent( const ShaderIncludeResolver& resolver, string_view binDirectory, string_view absShaderPath, string_view sourceShadersDir,
                                           ShaderCookStampCache& stampCache, vector<string>* pOutListIncludedFile = nullptr );

        /**
         * @brief 쿠커가 굽는 도메인 @p listDomain(`ShaderCookDriver::collectCookedDomains` — 굽지 않는 도메인은 판정할 쿠킹본이 없다)마다 쿠킹 스테이징(@p cookedDir)의 @p format 바이너리(`ShaderBinaryLocation::makeForDomain`)가 지금 소스에서 나온 것인지 보고 문제를 @p outListProblem 에 더합니다.
         * @details 판정은 스탬프를 쓰는 쪽(`writeCookStamp`)과 같은 수집 규칙 · 같은 해시다 — 규칙이 한 언어에만 있으므로 "두 언어가 같은 규칙인가" 를 볼 게이트가 없다.
         *          보는 것: 바이너리 폴더 · 스탬프 · 스탬프 판 · 도구 지문(스탬프의 `kToolchainKey` 줄), 스탬프에 빠진 · 바뀐 · 남은 소스, 소스마다 스탬프에 적힌 헤더가 지금도 같은지,
         *          바이너리가 하나라도 있는지, 리플렉션 매니페스트가 있는지와 매니페스트에 없는 바이너리(쿠커 레시피가 늘었는데 다시 베이크하지 않은 것). 소스가 없는 도메인은 건너뛴다.
         *          스탬프에는 컴파일러가 찾아봤지만 없던 앞자리 후보도 "없어야 한다" 로 적혀 있어(`ShaderCompileResult::_listIncludedFile`), 소스가 그대로인데 앞 순위 자리
         *          (포함한 파일의 폴더 → 자기 shaders → engine → common)에 같은 이름의 헤더가 새로 생겨 가리면 낡음이다. `findShadowedHeaders` 는 그 위에 같은 이름 자체를 오류로 막는다.
         *          `App --verify-shaders`(커밋 훅 · 배포 빌드)와 `ShaderCookStampTest` 가 부릅니다.
         */
        static void verifyCookedShaders( const AssetPaths& assetPaths, string_view cookedDir, ShaderTargetFormat format, const vector<string>& listDomain, vector<string>& outListProblem );

        /**
         * @brief 파일 내용 해시 캐시(`ContentHash`)를 버려, 다음 조회가 다시 읽게 합니다(스탬프 읽기 캐시는 쿠킹마다의 값 `ShaderCookStampCache` 다).
         * @details 이름은 헤더 해시를 따로 캐시하던 때의 것이다. 지금은 헤더를 열 때마다 내용 해시를 보므로(크기 · 쓰기 시각이 같으면 캐시) 실행 중 `.hlsli` 를 고치면 다음 조회가 스스로 알아챈다.
         *          그래도 수동 리로드와 쿠킹 직후는 "전부 다시 본다" 는 약속이라 부른다 — 같은 시각에 같은 크기로 다시 쓴 파일(테스트)을 놓치지 않는다.
         */
        static void invalidateSharedHeaderCache();
    };
} // namespace sw
