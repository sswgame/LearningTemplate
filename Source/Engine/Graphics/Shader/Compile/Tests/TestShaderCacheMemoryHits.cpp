// 셰이더 메모리 캐시 — 쿠킹본 정책은 같은 요청의 둘째 로드부터 공급자(팩)를 다시 읽지 않고, 깨진 편집은 마지막 성공 판을 쓴다(nogpu · 팩 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderBytecodeProvider.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

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

namespace
{
    /**
     * @brief 편집 중인 소스를 흉내 내는 공급자입니다 — 지문은 판 번호, 읽기는 판 번호를 바이트로 내거나(`_bBroken` 이면) 컴파일 오류로 답합니다.
     */
    class EditableFakeProvider final : public IShaderBytecodeProvider
    {
    public:
        uint64 _revision;
        uint8  _bBroken : 1;

        EditableFakeProvider()
            : _revision{ 1 }
            , _bBroken{ SW_FALSE }
        {
        }

        [[nodiscard]] ShaderCompileResult load( const ShaderCompileDesc& desc ) const override
        {
            (void)desc;
            ShaderCompileResult result;
            if ( _bBroken )
            {
                result._bSuccess     = false;
                result._errorMessage = "syntax error";
                return result;
            }
            result._bytecode = { static_cast<uint8>( _revision ) };
            result._bSuccess = true;
            return result;
        }
        [[nodiscard]] uint64 computeFingerprint( const ShaderCompileResult& result, const ShaderCompileDesc& desc ) const override
        {
            (void)result;
            (void)desc;
            return _revision;
        }
        [[nodiscard]] bool getReflection( ShaderCache& cache, const ShaderCompileDesc& desc, ShaderReflectionData& outReflection ) const override
        {
            (void)cache;
            (void)desc;
            (void)outReflection;
            return false;
        }
    };
} // namespace

/**
 * @brief [ShaderCacheTest] 소스가 깨지면 마지막 성공 판을 돌려주고, 재컴파일 결과는 그 항목 하나만 바꾼다
 * @details 예전에는 재컴파일 묶음이 하나라도 바꾸면 `clearCache` 로 캐시를 통째로 비워, 같은 묶음에서 깨진 셰이더의 다음 요청이 깨진 소스를 다시 컴파일하다 졌다(이전 판이 없다).
 */
SW_TEST_CASE( ShaderCacheTest, BrokenEditKeepsLastGoodVersion )
{
    ShaderCache                 cache;
    auto                        pOwnedProvider = make_unique<EditableFakeProvider>();
    EditableFakeProvider* const pProvider      = pOwnedProvider.get();
    SW_REQUIRE_TRUE( cache.initializeWithProvider( std::move( pOwnedProvider ) ) );

    const ShaderCompileDesc first  = makeDesc( "PSMain" );
    const ShaderCompileDesc second = makeDesc( "PSOther" );
    SW_REQUIRE_TRUE( cache.getOrCompile( first )._bSuccess );
    SW_REQUIRE_TRUE( cache.getOrCompile( second )._bSuccess );

    // 편집이 소스를 깨뜨렸다 — 지문이 달라져 다시 읽지만 실패하니 판 1 을 그대로 쓴다.
    pProvider->_revision = 2;
    pProvider->_bBroken  = SW_TRUE;
    SW_EXPECT_LOG( "ShaderCache.KeptLastGoodShader" );
    const ShaderCompileResult kept = cache.getOrCompile( first );
    SW_REQUIRE_TRUE( kept._bSuccess );
    SW_EXPECT_TRUE( kept._bytecode == vector<uint8>{ 1 } );

    // 재컴파일기가 다른 셰이더 하나만 새로 냈다 — 그 항목만 바뀌고 깨진 셰이더의 판 1 은 남는다.
    ShaderCompileResult recompiled;
    recompiled._bytecode = { 2 };
    recompiled._bSuccess = true;
    SW_REQUIRE_TRUE( cache.replaceCompiled( second, recompiled ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( cache.findCachedBytecode( second, bytes ) );
    SW_EXPECT_TRUE( bytes == vector<uint8>{ 2 } );
    SW_REQUIRE_TRUE( cache.findCachedBytecode( first, bytes ) );
    SW_EXPECT_TRUE( bytes == vector<uint8>{ 1 } );

    // 실패한 결과로는 항목을 바꾸지 않는다.
    ShaderCompileResult failed;
    failed._bSuccess = false;
    SW_EXPECT_LOG( "ShaderCache.ReplaceWithFailedResult" );
    SW_EXPECT_FALSE( cache.replaceCompiled( first, failed ) );

    // 고친 소스는 새 판으로 바뀐다.
    pProvider->_bBroken             = SW_FALSE;
    const ShaderCompileResult fixed = cache.getOrCompile( first );
    SW_REQUIRE_TRUE( fixed._bSuccess );
    SW_EXPECT_TRUE( fixed._bytecode == vector<uint8>{ 2 } );
}

/**
 * @brief [ShaderCacheTest] define 차례만 다른 같은 조합은 같은 캐시 항목이다 — 둘째 요청이 메모리 캐시에 맞고 정본 목록 · 퍼뮤테이션 해시가 같다
 * @details 예전에는 메모리 캐시 키와 셰이더 DDC 키가 define 을 받은 차례로 적어, 같은 조합이 차례만 달라 헛 미스가 나고 DDC 에 두 벌이 쌓였다(쿠킹 파일 이름의 해시만 정렬했다).
 */
SW_TEST_CASE( ShaderCacheTest, DefineOrderDoesNotSplitEntries )
{
    ShaderCache cache;
    SW_REQUIRE_TRUE( cache.initializeWithProvider( make_unique<CookedRuleFakeProvider>() ) );

    ShaderCompileDesc forward = makeDesc( "PSMain" );
    forward._listDefine.push_back( ShaderMacroDefine::parse( "MATERIAL_WET" ) );
    forward._listDefine.push_back( ShaderMacroDefine::parse( "SW_FORWARD=1" ) );
    ShaderCompileDesc reversed = makeDesc( "PSMain" );
    reversed._listDefine.push_back( ShaderMacroDefine::parse( "SW_FORWARD=1" ) );
    reversed._listDefine.push_back( ShaderMacroDefine::parse( "MATERIAL_WET=1" ) );

    const vector<string> canonical = ShaderCooker::makeCanonicalDefines( forward._listDefine );
    SW_REQUIRE_EQUAL( size_t( 2 ), canonical.size() );
    SW_EXPECT_EQUAL( string( "MATERIAL_WET=1" ), canonical[0] );
    SW_EXPECT_EQUAL( string( "SW_FORWARD=1" ), canonical[1] );
    SW_EXPECT_TRUE( canonical == ShaderCooker::makeCanonicalDefines( reversed._listDefine ) );
    SW_EXPECT_EQUAL( ShaderCooker::computePermutationHash( forward._listDefine ), ShaderCooker::computePermutationHash( reversed._listDefine ) );

    SW_REQUIRE_TRUE( cache.getOrCompile( forward )._bSuccess );
    SW_REQUIRE_TRUE( cache.getOrCompile( reversed )._bSuccess );
    SW_EXPECT_EQUAL( uint64( 1 ), cache.getProviderLoadCount() );
    SW_EXPECT_EQUAL( uint64( 1 ), cache.getMemoryHitCount() );
}
