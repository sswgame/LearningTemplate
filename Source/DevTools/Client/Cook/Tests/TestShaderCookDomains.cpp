// ShaderCookDomainsTest — 쿠킹 검사가 보는 도메인은 쿠커의 요청에서 나온다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Client/Cook/ShaderCookDriver.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

using namespace sw;

/** @brief [ShaderCookDomainsTest] 요청이 있는 도메인만 처음 나온 순서로 모은다. 요청이 없는 폴더(editor)는 판정 대상이 아니다. shaders 칸이 없는 요청은 건너뛴다 */
SW_TEST_CASE( ShaderCookDomainsTest, DomainsComeFromTheRequests )
{
    vector<ShaderCookRequest> listRequest( 5 );
    listRequest[0]._shaderPath = "engine/shaders/forward_lit.hlsl";
    listRequest[1]._shaderPath = "game/playground/shaders/sub/demo.hlsl";
    listRequest[2]._shaderPath = "engine/shaders/sky.hlsl";
    listRequest[3]._shaderPath = "common/shaders/toon.hlsl";
    listRequest[4]._shaderPath = "not/a/shader/path.hlsl";

    vector<string> listDomain{ "stale" };
    ShaderCookDriver::collectCookedDomains( listRequest, listDomain );
    const vector<string> listExpected{ "engine", "game/playground", "common" };
    SW_EXPECT_TRUE( listDomain == listExpected );

    ShaderCookDriver::collectCookedDomains( {}, listDomain );
    SW_EXPECT_TRUE( listDomain.empty() );
}
