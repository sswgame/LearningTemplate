/**
 * @file TestShaderBinaryName.cpp
 * @brief 쿠킹 셰이더 바이너리 이름이 퍼뮤테이션 해시 64 비트를 다 적고, 같은 이름에 다른 define 이 오면 쿠킹이 잡는가 · 인메모리 캐시 키가 define 경계를 잃지 않는가입니다(nogpu).
 */
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderBytecodeProvider.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

using namespace sw;

namespace
{
    /** @brief 같은 경로 · 스테이지 · 진입점에 해시와 define 만 다른 쿠킹 요청입니다. */
    ShaderCookRequest makeRequest( uint64 permutationHash, vector<string> listPermutation )
    {
        ShaderCookRequest request;
        request._shaderPath      = "engine/shaders/post_bloom.hlsl";
        request._entryPoint      = "PSMain";
        request._stage           = ShaderStage::Pixel;
        request._listPermutation = std::move( listPermutation );
        request._permutationHash = permutationHash;
        return request;
    }

    /** @brief 무엇을 달라 해도 같은 바이트로 답하고 읽은 수만 세는 공급자입니다. */
    class CountingProvider final : public IShaderBytecodeProvider
    {
    public:
        [[nodiscard]] ShaderCompileResult load( const ShaderCompileDesc& desc ) const override
        {
            (void)desc;
            ShaderCompileResult result;
            result._bytecode = { 0x44, 0x58, 0x42, 0x43 };
            result._bSuccess = true;
            return result;
        }
        [[nodiscard]] uint64 computeFingerprint( const ShaderCompileResult& result, const ShaderCompileDesc& desc ) const override { return _cooked.computeFingerprint( result, desc ); }
        [[nodiscard]] bool   getReflection( ShaderCache& cache, const ShaderCompileDesc& desc, ShaderReflectionData& outReflection ) const override
        {
            (void)cache;
            (void)desc;
            (void)outReflection;
            return false;
        }

    private:
        CookedShaderBytecodeProvider _cooked;
    };
} // namespace

/**
 * @brief [ShaderBinaryNameTest] 바이너리 이름은 퍼뮤테이션 해시를 16 자리로 다 적는다
 * @details 변이: 옛 32 비트 자르기로 되돌리면 이름 끝이 `_89abcdef` 가 되어 진다.
 */
SW_TEST_CASE( ShaderBinaryNameTest, FileNameCarriesTheFullPermutationHash )
{
    const string fileName = ShaderCooker::computeBinaryFileName( "post_bloom", ShaderStage::Pixel, "", 0x0123456789ABCDEFull, ".spv" );
    SW_EXPECT_TRUE( StringUtil::endsWith( fileName, "_0123456789abcdef.spv" ) );
}

/**
 * @brief [ShaderBinaryNameTest] 같은 이름에 정본 define 이 다른 요청 짝만 충돌이다 — 차례만 다르거나 `FOO` · `FOO=1` 처럼 같은 집합은 아니다
 * @details 변이: 검사를 빼거나 define 을 정본으로 맞추지 않고 비교하면 첫 단언(짝 하나) 또는 둘째 단언(짝 없음)이 진다.
 */
SW_TEST_CASE( ShaderBinaryNameTest, CollisionIsADifferentDefineSetUnderTheSameName )
{
    const vector<ShaderCookRequest> listRequest{
        makeRequest( 0x1111u, { "A=1", "B" } ),
        makeRequest( 0x1111u, { "B=1", "A=1" } ), // 같은 집합 — 중복 요청이지 충돌이 아니다
        makeRequest( 0x1111u, { "C=2" } ),        // 해시가 겹친 다른 집합 — 충돌
        makeRequest( 0x2222u, { "C=2" } ),
    };
    const vector<ShaderBinaryNameCollision> listCollision = ShaderCooker::findBinaryNameCollisions( listRequest );
    SW_REQUIRE_EQUAL( listCollision.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), listCollision[0]._firstIndex );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listCollision[0]._secondIndex );
    SW_EXPECT_TRUE( ShaderCooker::findBinaryNameCollisions( vector<ShaderCookRequest>{ listRequest[0], listRequest[1] } ).empty() );
}

/**
 * @brief [ShaderBinaryNameTest] 인메모리 캐시 키는 define 경계를 잃지 않는다 — 값 `1_B=1` 하나와 define 둘(`A=1`, `B=1`)은 다른 항목이다
 * @details 변이: 키 구분자를 `_` 로 되돌리면 두 요청이 같은 키가 되어 둘째가 메모리에서 답해 공급자 읽기가 1 로 남는다.
 */
SW_TEST_CASE( ShaderBinaryNameTest, CacheKeyKeepsDefineBoundaries )
{
    ShaderCache cache;
    SW_REQUIRE_TRUE( cache.initializeWithProvider( make_unique<CountingProvider>() ) );

    ShaderCompileDesc joined;
    joined._source          = ShaderSourceID{ "engine/shaders/forward_lit.hlsl" };
    joined._entryPoint      = "PSMain";
    joined._stage           = ShaderStage::Pixel;
    joined._targetFormat    = ShaderTargetFormat::SPIRV_Vulkan;
    ShaderCompileDesc split = joined;
    joined._listDefine.push_back( ShaderMacroDefine{ "A", "1_B=1" } );
    split._listDefine.push_back( ShaderMacroDefine{ "A", "1" } );
    split._listDefine.push_back( ShaderMacroDefine{ "B", "1" } );

    SW_REQUIRE_TRUE( cache.getOrCompile( joined )._bSuccess );
    SW_REQUIRE_TRUE( cache.getOrCompile( split )._bSuccess );
    SW_EXPECT_EQUAL( uint64( 2 ), cache.getProviderLoadCount().get() );
    SW_EXPECT_EQUAL( uint64( 0 ), cache.getMemoryHitCount() );
}
