/**
 * @file ShaderBakeDriver.h
 * @brief 오프라인 셰이더 베이크의 **정책** — 무엇을 구울지 모으고(레시피), 그것을 전부 굽는다.
 * @details 언리얼에서 "어떤 셰이더를 컴파일하는가" 를 정하는 것은 머티리얼·버텍스 팩토리·렌더러이고,
 *          ShaderCore 는 한 장을 컴파일할 뿐이다. 이 저장소도 같은 자리다: 한 장을 굽는 법과 이름 짓기·최신
 *          판정(`Shader/Compile/ShaderBaker`)은 메커니즘이고, 파이프라인 XML 과 패스 종류(`FrameRendererUtil`)를
 *          읽어 레시피를 만드는 것은 렌더러의 지식이다. 예전에는 이 둘이 `ShaderBaker` 한 구조체에 있어
 *          `Shader/` 가 `Renderer/` 를 include 했고, 그 넉 줄이 Engine 코어의 마지막 강결합 묶음이었다.
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
     * @brief 레시피 수집과 일괄 베이크. `App --bake-shaders` 와 테스트가 부릅니다.
     */
    struct SW_API ShaderBakeDriver
    {
        /**
         * @brief 렌더 파이프라인 에셋(pipeline XML) 및 엔진 부트스트랩 데이터(enginedata.xml)를 기반으로
         *        실제 게임 런타임에 필요한 셰이더와 순열(Permutations)만을 4대 RHI 바이너리로 일괄 베이킹합니다.
         * @param resourceRoot 리소스 루트 디렉터리 (비어있으면 ResourceUtil 기준 자동 탐색)
         * @param targetFormat 대상 포맷 (Count이면 DX11, DX12, Vulkan, OpenGL 전체 베이킹)
         * @param bForceAll true이면 내용 해시와 무관하게 전면 재컴파일
         * @return 성공적으로 베이킹된 바이너리 파일 총 개수
         */
        static uint32 bakeAllShaders( string_view        resourceRoot = {},
                                      ShaderTargetFormat targetFormat = ShaderTargetFormat::Count,
                                      bool               bForceAll    = false );

        /**
         * @brief 이 리소스 트리가 구워야 할 레시피를 전부 모읍니다 (파이프라인 XML + 머티리얼).
         * @details 런타임이 만드는 퍼뮤테이션과 여기서 만드는 레시피가 어긋나면 Shipping 이 매니페스트 미스로
         *          떨어진다. 그래서 define 을 합치는 규칙과 패스 기본 셰이더를 고르는 규칙이 런타임과 같은 자리
         *          (`FrameRendererUtil`)를 본다 — 그것이 이 함수가 렌더러 층에 있는 이유다.
         */
        static void collectAllRecipes( string_view rootDir, vector<ShaderBakeRecipe>& outListRecipe );
    };
} // namespace sw
