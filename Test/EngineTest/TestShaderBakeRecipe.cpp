/**
 * @file TestShaderBakeRecipe.cpp
 * @brief 베이커가 모으는 레시피가 **런타임이 실제로 요구하는 퍼뮤테이션**을 덮는지 본다.
 * @details GPU 도 셰이더 컴파일러도 필요 없다 — 파이프라인 XML 과 머티리얼을 읽어 (셰이더 · 진입점 ·
 *          define) 목록을 만들고 해시를 비교할 뿐이다. 그래서 nogpu 라벨의 `EngineTest_NoGPU` 에 든다.
 *
 *          이 파일이 있는 이유: 베이커는 패스의 define 을 **파이프라인 XML 의 `_listPermutation`** 에서만
 *          읽었는데, 런타임은 G버퍼 패스에 `SW_PASS_GBUFFER=1` 을 **C++ 에서** 얹었다. 두 자리가 어긋나자
 *          런타임이 찾는 해시를 아무도 굽지 않았고, Shipping 은 런타임 컴파일이 없으므로 G버퍼 드로우가
 *          통째로 사라졌다 — 디퍼드 화면이 한 색으로 남고 SSAO 는 가림을 하나도 내지 않았다.
 *          그 증상은 GPU 스위트(`RenderPassGpuTest`)에서만 보였고, 그 스위트는 CI 가 돌리지 않는다.
 *          여기서는 그림을 그리지 않고 **목록만** 대조하므로 CI 가 잡는다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 이 스템(예: "gbuffer")·스테이지·해시를 가진 레시피가 목록에 있는가. */
    bool hasRecipeInternal( const sw::vector<sw::ShaderBakeRecipe>& listRecipe,
                            sw::string_view                         stemLower,
                            sw::ShaderStage                         stage,
                            uint64                                  permHash )
    {
        for ( const sw::ShaderBakeRecipe& recipe : listRecipe )
        {
            if ( recipe._stage != stage || recipe._permHash != permHash )
                continue;
            if ( sw::ShaderBaker::getStemLower( recipe._shaderPath ) == stemLower )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief [ShaderBakeRecipeTest] 패스가 C++ 에서 얹는 define 까지 레시피에 든다
 * @details `FrameRendererUtil::getPassDefine` 이 런타임과 베이커가 함께 보는 **유일한 정본**이다.
 *          베이커가 그것을 안 보면(예전이 그랬다) 런타임이 요청하는 해시가 목록에 없다.
 */
SW_TEST_CASE( ShaderBakeRecipeTest, PassDefineReachesBakedRecipes )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::vector<sw::ShaderBakeRecipe> listRecipe;
    sw::ShaderBaker::collectAllRecipes( sw::ResourceUtil::getRootFolderPath(), listRecipe );
    SW_EXPECT_TRUE_MSG( listRecipe.empty() == false, "레시피를 하나도 모으지 못했다 — 리소스 루트를 못 찾았을 수 있다" );
    SW_ASSERT_TRUE( listRecipe.empty() == false );

    // 런타임이 G버퍼 패스에 얹는 define 집합 — FrameRendererResources 가 PSO 를 만들 때 쓰는 그것이다.
    const sw::vector<sw::string> listGbufferDefine = sw::FrameRendererUtil::getPassDefine( sw::RenderPassType::GBuffer );
    SW_EXPECT_TRUE_MSG( listGbufferDefine.empty() == false,
                        "G버퍼 패스가 얹는 define 이 사라졌다 — 이 테스트가 지키려던 축이 없어졌다" );
    SW_ASSERT_TRUE( listGbufferDefine.empty() == false );

    const uint64 gbufferPermHash = sw::ShaderBaker::computePermutationHash( listGbufferDefine );
    SW_EXPECT_TRUE_MSG( hasRecipeInternal( listRecipe, "gbuffer", sw::ShaderStage::Vertex, gbufferPermHash ),
                        "G버퍼 패스 define 이 든 VS 레시피가 없다 — Shipping 에서 G버퍼 드로우가 통째로 사라진다" );
    SW_EXPECT_TRUE_MSG( hasRecipeInternal( listRecipe, "gbuffer", sw::ShaderStage::Pixel, gbufferPermHash ),
                        "G버퍼 패스 define 이 든 PS 레시피가 없다 — Shipping 에서 G버퍼 드로우가 통째로 사라진다" );
}

/**
 * @brief [ShaderBakeRecipeTest] 레시피 목록에 같은 (셰이더·진입점·스테이지·해시) 가 두 번 들지 않는다
 * @details `appendRecipeUnique` 의 계약이다. 중복은 그 자체로 치명적이진 않지만 같은 것을 네 번 굽게
 *          만들고, 무엇보다 "런타임 요청 = 레시피" 대조를 흐린다.
 */
SW_TEST_CASE( ShaderBakeRecipeTest, RecipesAreUnique )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::vector<sw::ShaderBakeRecipe> listRecipe;
    sw::ShaderBaker::collectAllRecipes( sw::ResourceUtil::getRootFolderPath(), listRecipe );
    SW_ASSERT_TRUE( listRecipe.empty() == false );

    uint32 duplicateCount = 0;
    for ( size_t outer = 0; outer < listRecipe.size(); ++outer )
    {
        for ( size_t inner = outer + 1; inner < listRecipe.size(); ++inner )
        {
            const bool bSame = listRecipe[outer]._stage == listRecipe[inner]._stage &&
                               listRecipe[outer]._permHash == listRecipe[inner]._permHash &&
                               listRecipe[outer]._entryPoint == listRecipe[inner]._entryPoint &&
                               listRecipe[outer]._shaderPath == listRecipe[inner]._shaderPath;
            if ( bSame )
                ++duplicateCount;
        }
    }

    SW_EXPECT_EQUAL( 0u, duplicateCount );
}
