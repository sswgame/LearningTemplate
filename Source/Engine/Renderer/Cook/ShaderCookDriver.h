/**
 * @file ShaderCookDriver.h
 * @brief 오프라인 셰이더 쿠킹의 **정책**입니다. 무엇을 쿠킹할지 모으고(요청), 그것을 모두 쿠킹합니다.
 * @details 언리얼에서 "어떤 셰이더를 컴파일하는가" 를 정하는 것은 머티리얼 · 버텍스 팩토리 · 렌더러이고,
 *          ShaderCore 는 한 장을 컴파일할 뿐입니다. 이 저장소도 같은 자리입니다: 한 장을 쿠킹하는 법과 이름 짓기 · 최신
 *          판정(`Shader/Compile/ShaderCooker`)은 메커니즘이고, 파이프라인 XML 과 패스 종류(`FrameRendererUtil`)를
 *          읽어 요청을 만드는 것은 렌더러의 지식입니다. 둘을 한 곳에 두면 `Shader/` 가 `Renderer/` 를 include 하게
 *          됩니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    /**
     * @struct ShaderCookSummary
     * @brief 일괄 쿠킹이 한 일입니다. 실패 · 계약 위반이 하나라도 있으면 `App --cook-shaders` 는 실패로 끝납니다.
     * @details 주의: 실패한 셰이더의 소스에도 도장을 찍으면 다음 쿠킹이 그것을 건너뛰어, 문법 오류가 든 셰이더의 옛 바이너리가
     *          종료 코드 0 으로 배포본에 실립니다. 실패는 세어 돌려주고 그 소스는 도장에서 뺍니다.
     */
    struct [[nodiscard]] ShaderCookSummary
    {
        uint32 _cookedCount{ 0 };            ///< 새로 쿠킹된 바이너리 수
        uint32 _failedCount{ 0 };            ///< 컴파일에 실패한 (요청, 포맷) 수 — 그 소스는 도장에서 빠져 다음 쿠킹이 다시 시도한다
        uint32 _contractViolationCount{ 0 }; ///< 바인딩 계약 위반 수
        /** @brief 실패도 위반도 없으면 true 입니다. */
        bool isClean() const { return _failedCount == 0 && _contractViolationCount == 0; }
    };

    /**
     * @struct ShaderCookDriver
     * @brief 요청 수집과 일괄 쿠킹입니다. `App --cook-shaders` 와 테스트가 부릅니다.
     */
    struct SW_API ShaderCookDriver
    {
        /**
         * @brief 렌더 파이프라인 에셋(pipeline XML)과 엔진 부트스트랩 데이터(enginedefaultassets.xml)를 바탕으로
         *        게임 런타임에 실제로 필요한 셰이더와 퍼뮤테이션만 네 RHI 바이너리로 한꺼번에 쿠킹합니다.
         * @param resourceRoot 리소스 루트 디렉터리(비어 있으면 ResourceUtil 기준으로 자동 탐색)
         * @param targetFormat 대상 포맷(Count 면 DX11, DX12, Vulkan, OpenGL 모두 쿠킹)
         * @param bForceAll true 면 내용 해시와 상관없이 모두 다시 컴파일
         * @return 쿠킹된 수 · 실패 수 · 계약 위반 수
         */
        static ShaderCookSummary cookAllShaders( string_view        resourceRoot = {},
                                                 ShaderTargetFormat targetFormat = ShaderTargetFormat::Count,
                                                 bool               bForceAll    = false );

        /**
         * @brief 이 리소스 트리가 쿠킹해야 할 요청을 모두 모읍니다(파이프라인 XML + 머티리얼).
         * @details 런타임이 만드는 퍼뮤테이션과 여기서 만드는 요청이 어긋나면 Shipping 이 매니페스트 미스로
         *          떨어집니다. 그래서 define 을 합치는 규칙과 패스 기본 셰이더를 고르는 규칙이 런타임과 같은 자리
         *          (`RenderPassTypeInfo` 표 · `selectRenderPassShader` · `FrameRendererUtil`)를 봅니다. 그것이 이 함수가 렌더러 층에 있는 이유입니다.
         */
        static void collectAllRequests( string_view rootDir, vector<ShaderCookRequest>& outListRequest );
    };
} // namespace sw
