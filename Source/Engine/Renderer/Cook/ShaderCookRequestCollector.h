/**
 * @file ShaderCookRequestCollector.h
 * @brief **무엇을 쿠킹할지** 정합니다. 파이프라인 XML 과 머티리얼을 훑어 (셰이더 · 진입점 · define) 요청 목록을 만듭니다.
 * @details 쿠킹하는 일(`ShaderCookDriver` · `Shader/Compile/ShaderCooker`)과 나누는 이유는 입력이 다르기 때문입니다. 여기 입력은 **에셋**(파이프라인 · 머티리얼)이고
 *          저쪽 입력은 요청 목록입니다(언리얼도 머티리얼 · 버텍스 팩토리가 셰이더 맵을 정하고 컴파일 관리자는 받은 작업만 돈다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    /**
     * @struct ShaderCookRequestCollector
     * @brief 리소스 트리가 쿠킹해야 할 셰이더 요청을 모읍니다. `ShaderCookDriver::cookAllShaders` 가 부릅니다.
     */
    struct SW_API ShaderCookRequestCollector
    {
        /**
         * @brief 이 리소스 트리가 쿠킹해야 할 요청을 모두 모읍니다(파이프라인 XML + 머티리얼).
         * @details 런타임이 만드는 퍼뮤테이션과 여기서 만드는 요청이 어긋나면 Shipping 이 매니페스트 미스로
         *          떨어집니다. 그래서 define 을 합치는 규칙과 패스 기본 셰이더를 고르는 규칙이 런타임과 같은 코드
         *          (`RenderPassTypeInfo` 테이블, `selectRenderPassShader`, `FrameRendererUtil`)를 봅니다. 그것이 이 함수가 렌더러 층에 있는 이유입니다.
         * @return 엔진 기본 에셋(`EngineDefaultAssets`)을 읽지 못하면 false 입니다. 엔진 셰이더 경로가 그 데이터에만 있기 때문입니다.
         */
        [[nodiscard]] static bool collectAllRequests( string_view rootDir, vector<ShaderCookRequest>& outListRequest );
    };
} // namespace sw
