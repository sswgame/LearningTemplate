/**
 * @file ShaderBakeDriver.h
 * @brief 오프라인 셰이더 베이크의 **정책**입니다. 무엇을 구울지 모으고(레시피), 그것을 모두 굽습니다.
 * @details 언리얼에서 "어떤 셰이더를 컴파일하는가" 를 정하는 것은 머티리얼 · 버텍스 팩토리 · 렌더러이고,
 *          ShaderCore 는 한 장을 컴파일할 뿐입니다. 이 저장소도 같은 자리입니다: 한 장을 굽는 법과 이름 짓기 · 최신
 *          판정(`Shader/Compile/ShaderBaker`)은 메커니즘이고, 파이프라인 XML 과 패스 종류(`FrameRendererUtil`)를
 *          읽어 레시피를 만드는 것은 렌더러의 지식입니다. 예전에는 이 둘이 `ShaderBaker` 한 구조체에 있어
 *          `Shader/` 가 `Renderer/` 를 include 했고, 그 넉 줄이 Engine 코어의 마지막 강결합 묶음이었습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"

namespace sw
{
    /**
     * @struct ShaderBakeDriver
     * @brief 레시피 수집과 일괄 베이크입니다. `App --bake-shaders` 와 테스트가 부릅니다.
     */
    struct SW_API ShaderBakeDriver
    {
        /**
         * @brief 렌더 파이프라인 에셋(pipeline XML)과 엔진 부트스트랩 데이터(enginedata.xml)를 바탕으로
         *        게임 런타임에 실제로 필요한 셰이더와 퍼뮤테이션만 네 RHI 바이너리로 한꺼번에 굽습니다.
         * @param resourceRoot 리소스 루트 디렉터리(비어 있으면 ResourceUtil 기준으로 자동 탐색)
         * @param targetFormat 대상 포맷(Count 면 DX11, DX12, Vulkan, OpenGL 모두 굽기)
         * @param bForceAll true 면 내용 해시와 상관없이 모두 다시 컴파일
         * @return 성공적으로 구운 바이너리 파일 총 개수
         */
        static uint32 bakeAllShaders( string_view        resourceRoot = {},
                                      ShaderTargetFormat targetFormat = ShaderTargetFormat::Count,
                                      bool               bForceAll    = false );

        /**
         * @brief 이 리소스 트리가 구워야 할 레시피를 모두 모읍니다(파이프라인 XML + 머티리얼).
         * @details 런타임이 만드는 퍼뮤테이션과 여기서 만드는 레시피가 어긋나면 Shipping 이 매니페스트 미스로
         *          떨어집니다. 그래서 define 을 합치는 규칙과 패스 기본 셰이더를 고르는 규칙이 런타임과 같은 자리
         *          (`FrameRendererUtil`)를 봅니다. 그것이 이 함수가 렌더러 층에 있는 이유입니다.
         */
        static void collectAllRecipes( string_view rootDir, vector<ShaderBakeRecipe>& outListRecipe );
    };
} // namespace sw
