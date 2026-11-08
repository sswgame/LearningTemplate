// 쿠킹본 정책의 셰이더 메모리 캐시 — 같은 요청의 둘째 로드부터 공급자(팩)를 다시 읽지 않는다(nogpu · 팩 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderBytecodeProvider.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

using namespace sw;

namespace
{
    /**
     * @brief 쿠킹본 공급자의 지문 규칙(`CookedShaderBytecodeProvider::computeFingerprint` — 팩은 실행 중 바뀌지 않아 상수)을 그대로 쓰고, 읽기만 팩 대신 고정 바이트로 답하는 공급자입니다.
     * @details 지문 규칙은 진짜 쿠킹본 공급자에 맡긴다 — 테스트가 규칙을 따로 적으면 그 규칙이 지문 0(늘 빗나감)으로 돌아가도 모른다(예전 결함).
     */
    class CookedRuleFakeProvider final : public IShaderBytecodeProvider
    {
    public:
        [[nodiscard]] ShaderCompileResult load( const ShaderCompileDesc& desc ) const override
        {
            ShaderCompileResult result;
            result._bytecode = { 0x44, 0x58, 0x42, 0x43 };
            result._bSuccess = true;
            (void)desc;
            return result;
        }
        [[nodiscard]] uint64 computeFingerprint( const ShaderCompileResult& result ) const override { return _cooked.computeFingerprint( result ); }
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

    ShaderCompileDesc makeDesc( const utf8* pEntryPoint )
    {
        ShaderCompileDesc desc;
        desc._source       = ShaderSourceID{ "engine/shaders/forward_lit.hlsl" };
        desc._entryPoint   = pEntryPoint;
        desc._stage        = ShaderStage::Pixel;
        desc._targetFormat = ShaderTargetFormat::SPIRV_Vulkan;
        return desc;
    }
} // namespace

/**
 * @brief [ShaderCacheTest] 쿠킹본 정책은 같은 요청을 두 번째부터 메모리 캐시로 답한다 — 적중 수가 오르고 공급자 읽기는 요청 종류마다 한 번이다
 * @details 예전에는 쿠킹본 항목의 지문이 늘 0 이라 한 번도 맞지 않아 매번 팩을 다시 읽었다(`ShaderBytecodeProvider.h` 머리말).
 */
SW_TEST_CASE( ShaderCacheTest, CookedPolicyAnswersRepeatsFromMemory )
{
    ShaderCache cache;
    SW_REQUIRE_TRUE( cache.initializeWithProvider( make_unique<CookedRuleFakeProvider>() ) );

    const ShaderCompileDesc first  = makeDesc( "PSMain" );
    const ShaderCompileDesc second = makeDesc( "PSOther" );
    SW_REQUIRE_TRUE( cache.getOrCompile( first )._bSuccess );
    SW_EXPECT_EQUAL( uint64( 0 ), cache.getMemoryHitCount() );
    SW_EXPECT_EQUAL( uint64( 1 ), cache.getProviderLoadCount() );

    for ( int32 repeat = 0; repeat < 3; ++repeat )
    {
        SW_EXPECT_TRUE( cache.getOrCompile( first )._bSuccess );
    }
    SW_EXPECT_EQUAL( uint64( 3 ), cache.getMemoryHitCount() );
    SW_EXPECT_EQUAL( uint64( 1 ), cache.getProviderLoadCount() );

    // 다른 요청(진입점)은 다른 항목이다 — 공급자를 한 번 더 부른다.
    SW_REQUIRE_TRUE( cache.getOrCompile( second )._bSuccess );
    SW_EXPECT_EQUAL( uint64( 3 ), cache.getMemoryHitCount() );
    SW_EXPECT_EQUAL( uint64( 2 ), cache.getProviderLoadCount() );

    // 비우면 다시 공급자다.
    cache.clearCache();
    SW_REQUIRE_TRUE( cache.getOrCompile( first )._bSuccess );
    SW_EXPECT_EQUAL( uint64( 3 ), cache.getProviderLoadCount() );
}
