/**
 * @file ShaderCookDriver.h
 * @brief 오프라인 셰이더 쿠킹의 일괄 실행입니다. 요청(`ShaderCookRequestCollector` 가 모은다)을 모두 쿠킹하고 매니페스트 · 스탬프를 씁니다.
 * @details 언리얼에서 "어떤 셰이더를 컴파일하는가"를 정하는 것은 머티리얼, 버텍스 팩토리, 렌더러이고,
 *          ShaderCore 는 셰이더 하나를 컴파일할 뿐입니다. 이 저장소도 같은 구조입니다: 셰이더 하나를 쿠킹하는 법과 이름 짓기, 최신
 *          판정(`Shader/Compile/ShaderCooker`)은 메커니즘이고, 파이프라인 XML 과 패스 종류(`FrameRendererUtil`)를
 *          읽어 요청을 만드는 것은 렌더러의 지식입니다. 둘을 한 곳에 두면 `Shader/` 가 `Renderer/` 를 include 하게
 *          됩니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/Shader/Compile/IShaderCookService.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    /**
     * @struct ShaderCookSummary
     * @brief 일괄 쿠킹이 한 일입니다. 실패나 계약 위반이 하나라도 있으면 `App --cook-shaders` 는 실패로 끝납니다.
     * @details 주의: 실패한 셰이더의 소스에도 스탬프를 찍으면 다음 쿠킹이 그것을 건너뛰어, 문법 오류가 든 셰이더의 옛 바이너리가
     *          종료 코드 0 으로 배포본에 실립니다. 실패는 세어 돌려주고 그 소스는 스탬프에서 뺍니다.
     */
    struct [[nodiscard]] ShaderCookSummary
    {
        uint32 _cookedCount{ 0 };            ///< 새로 쿠킹된 바이너리 수
        uint32 _failedCount{ 0 };            ///< 쿠킹하지 못한 것의 수. 컴파일 실패(그 소스는 스탬프에서 빠져 다음 쿠킹이 다시 시도한다), 소스 없는 요청, 쓰지 못한 매니페스트 · 스탬프, 지우지 못한 옛 바이너리를 센다
        uint32 _contractViolationCount{ 0 }; ///< 바인딩 계약 위반 수
        uint32 _removedCount{ 0 };           ///< 매니페스트에 없어 지운 옛 바이너리 수
        uint32 _compileMs{ 0 };              ///< 최신 판정과 컴파일에 쓴 시간(ms). 작업마다 잰 것의 합이라 병렬로 돌면 `_totalMs` 보다 길 수 있다(계측용)
        uint32 _reflectMs{ 0 };              ///< 바이너리 읽기 · 리플렉션 · 계약 검사에 쓴 시간(ms). 작업마다 잰 것의 합이다(계측용)
        uint32 _totalMs{ 0 };                ///< 쿠킹 전체의 벽시계 시간(ms). 요청 풀기 · 병렬 구간 · 매니페스트와 스탬프 쓰기를 모두 센다(계측용)
        /** @brief 실패도 위반도 없으면 true 입니다. */
        bool isClean() const { return _failedCount == 0 && _contractViolationCount == 0; }
    };

    /**
     * @enum ShaderCookSampleAxis
     * @brief 표본이 값마다 적어도 한 번 덮을 요청의 축입니다. 규칙 파일의 이름(`source` · `entryPoint` · `stage` · `permutationDefine`)과 짝입니다.
     * @details 굽기 경로의 갈래는 소스(include · 도메인 · 머티리얼 그래프 생성물) · 진입점 · 스테이지(그래픽 · 컴퓨트 · 지오메트리 · 테셀레이션) · 퍼뮤테이션 define(변형 키)이 정한다.
     *          바인드리스는 형식(SM6.6 의 DX12 · Vulkan)이 정하므로 축이 아니다. 언리얼 `-ShaderCompileWorker` 테스트가 셰이더 형식 · 퍼뮤테이션 대표만 굽는 것과 같은 자리다.
     */
    enum class ShaderCookSampleAxis : uint8
    {
        Source,
        EntryPoint,
        Stage,
        PermutationDefine,
    };

    /**
     * @struct ShaderCookDriver
     * @brief 요청 수집과 일괄 쿠킹입니다. `App --cook-shaders` 와 테스트가 부릅니다.
     */
    struct SW_API ShaderCookDriver
    {
        /** @brief 표본 규칙 파일(프로젝트 루트 기준, 개발 전용)입니다 — `{ "_listCoverAxis": [ "source", … ] }`. */
        static constexpr const utf8* kSampleRuleFile = "Config/Dev/ShaderCookSample.json";

        /** @brief 표본 규칙 JSON 글을 읽습니다. 칸이 없거나 비었거나 모르는 · 겹친 축이면 그 값이 대상인 오류입니다(기본값이 없다). */
        [[nodiscard]] static expected<vector<ShaderCookSampleAxis>> parseSampleRule( string_view jsonText, string_view sourceName );
        /** @brief 프로젝트의 표본 규칙 파일을 읽습니다(`parseSampleRule`). */
        [[nodiscard]] static expected<vector<ShaderCookSampleAxis>> loadSampleRule();
        /**
         * @brief 요청을 순서대로 보며 @p listAxis 의 어느 축에서든 아직 덮지 않은 값을 가진 요청만 고릅니다(퍼뮤테이션이 없는 요청은 빈 define 하나로 센다). 결정적이다 — 같은 요청 목록이면 같은 표본이다.
         * @details 표본은 모든 소스 · 진입점 · 스테이지 · define 을 한 번씩 컴파일 · 리플렉션 · 계약 검사에 지나게 한다. 전체 굽기는 배포 쿠킹(CookAssets — shipping-check)이 본다.
         */
        [[nodiscard]] static vector<ShaderCookRequest> selectSample( const vector<ShaderCookRequest>& listRequest, const vector<ShaderCookSampleAxis>& listAxis );

        /**
         * @brief 렌더 파이프라인 에셋(pipeline XML)과 엔진 기동 데이터(enginedefaultassets.xml)를 바탕으로
         *        게임 런타임에 실제로 필요한 셰이더와 퍼뮤테이션만 네 RHI 바이너리로 한꺼번에 쿠킹합니다.
         * @param cookedDir 쿠킹 출력의 루트(`--cooked-dir`). 바이너리는 `<여기>/<domain>/shaders/bin/<rhi>` 에 쓴다 — 씬 · 프리팹 쿠킹본과 같은 스테이징이고, 팩 쿠커가 거기서 담는다
         * @param resourceRoot 리소스 루트 디렉터리(비어 있으면 AssetPaths 기준으로 자동 탐색)
         * @param targetFormat 대상 포맷(Count 면 DX11, DX12, Vulkan, OpenGL 모두 쿠킹)
         * @param bForceAll true 면 내용 해시와 상관없이 모두 다시 컴파일
         * @param scope Sample 이면 표본(`selectSample`)만 굽는다. 쿠커는 요청 밖의 바이너리를 지우고 스탬프는 폴더의 소스 모두를 적으므로 표본은 빈(없는) 스테이징에만 굽는다 —
         *              채워진 스테이징이면 실패다(쿠킹본을 표본으로 덮지 않는다)
         * @return 쿠킹된 수, 실패 수, 계약 위반 수, 지운 옛 바이너리 수. 요청이 하나도 없어도 실패다
         */
        static ShaderCookSummary cookAllShaders( string_view        cookedDir,
                                                 string_view        resourceRoot = {},
                                                 ShaderTargetFormat targetFormat = ShaderTargetFormat::Count,
                                                 bool               bForceAll    = false,
                                                 ShaderCookScope    scope        = ShaderCookScope::All );

        /** @brief 요청 @p listRequest 가 쿠킹하는 도메인(`engine` · `game/<팩>`)을 처음 나온 순서로 모읍니다. `shaders` 폴더 칸이 없는 요청은 쿠킹도 실패하므로 여기서는 건너뜁니다. */
        static void collectCookedDomains( const vector<ShaderCookRequest>& listRequest, vector<string>& outListDomain );
        /**
         * @brief 이 리소스 트리의 요청이 굽는 도메인마다 쿠킹 스테이징(@p cookedDir)의 @p targetFormat 바이너리가 소스와 맞는지 판정해 문제를 @p outListProblem 에 적습니다.
         * @details 굽는 것(`collectAllRequests`)과 판정하는 것이 같은 목록을 본다 — 설정의 도메인을 따로 훑으면 굽지 않는 폴더(에디터가 실행 때 컴파일하는 `editor/shaders`)를 "쿠킹되지 않음" 으로 잡는다.
         */
        static void verifyCookedShaders( string_view cookedDir, ShaderTargetFormat targetFormat, vector<string>& outListProblem );
        /**
         * @brief 이 리소스 트리의 요청이 @p targetFormat 으로 만드는 바이너리의 스테이징 기준 id(`engine/shaders/bin/dx12/<파일>`, `FileUtil::normalizePath`)와 그 출처 묶음을 모읍니다.
         * @details 팩 쿠커가 "닿은 출처가 요청했는데 팩에서 버리는" 바이너리를 경고로 가른다(`PackCookParams::_mapRequestedShaderBinary`). 요청을 모으지 못하면 false 입니다.
         */
        [[nodiscard]] static bool collectRequestedBinaries( ShaderTargetFormat targetFormat, unordered_map<string, vector<vector<string>>>& outMapBinaryOrigin );

        /**
         * @brief 요청 목록을 모두 쿠킹하고, 리플렉션 매니페스트를 쓰고, 매니페스트에 없는 옛 바이너리를 지우고, 쿠킹 스탬프를 씁니다(`cookAllShaders` 의 본체).
         * @param cookedDir 쿠킹 출력의 루트(`cookAllShaders` 와 같다). 비면 실패다
         * @details 요청의 셰이더 경로는 리소스 경로다 — 소스 · include 는 컴파일러와 같은 해석기(`ShaderSourceResolver`, 엔진 `AssetPaths`)로 푼다.
         *          **소스가 없거나 `shaders` 폴더 밖인 요청도 실패로 셉니다.** 조용히 건너뛰면 요청이 어긋난 채 종료 코드 0 으로 끝난다.
         *          요청이 만들지 않는 바이너리는 지웁니다 — 요청 규칙이 바뀌면(퍼뮤테이션 해시 · 셰이더 삭제) 옛 파일이 매니페스트 밖에 남아 `--verify-shaders` 를 지게 한다.
         *          요청 × 포맷의 컴파일 · 리플렉션은 엔진의 병렬 for(`engine::runParallel`)로 나눠 돌린다. 산출물(바이너리 · 매니페스트 · 스탬프)은 직렬로 돌 때와 바이트 단위로 같다 —
         *          작업마다 자기 결과 칸에만 쓰고, 매니페스트 합치기 · 카운터 합산 · 실패 소스 기록 · 매니페스트와 스탬프 쓰기는 요청 순서대로 직렬로 한다.
         *          같은 바이너리 파일을 만드는 요청이 여럿이면 그 요청들은 한 작업 사슬로 순서대로 돈다(두 스레드가 같은 파일을 쓰지 않는다).
         *          엔진 서비스가 묶이지 않았거나 워커가 없으면 이 스레드가 요청 순서대로 모두 돈다.
         */
        static ShaderCookSummary cookRequests( string_view                      cookedDir,
                                               const vector<ShaderCookRequest>& listRequest,
                                               ShaderTargetFormat               targetFormat = ShaderTargetFormat::Count,
                                               bool                             bForceAll    = false );
    };
} // namespace sw
