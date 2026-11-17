// ShaderSourceGeneratorTest — 쿠킹 전 셰이더 소스 생성기 창구: 개발 구성에서 생성기가 없으면 쿠킹 실패, 실패한 생성기가 하나라도 있으면 쿠킹 실패(나머지는 돈다),
// 모두 맞추면 쓴 수를 모은다. 등록 표와 떼어 둔 본체(`refreshGenerators`)에 가짜 생성기를 넘긴다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderSourceGenerator.h"

using namespace sw;

namespace
{
    /** @brief 부른 수를 세고 정한 결과 · 쓴 수를 내는 가짜 생성기입니다. */
    class FakeShaderSourceGenerator final : public IShaderSourceGenerator
    {
    public:
        mutable uint32 _callCount{ 0 }; ///< 부른 수 — 생성기 창구는 const 다

        FakeShaderSourceGenerator( const utf8* pName, bool bSucceeds, uint32 writtenCount )
            : _pName{ pName }
            , _bSucceeds{ bSucceeds }
            , _writtenCount{ writtenCount }
        {
        }

        const utf8* getName() const override { return _pName; }
        bool        refreshAll( const AssetPaths& /*assetPaths*/, string_view /*contentRoot*/, uint32& outWrittenCount ) const override
        {
            ++_callCount;
            outWrittenCount += _writtenCount;
            return _bSucceeds;
        }

    private:
        const utf8* _pName;
        bool        _bSucceeds; ///< NOLINT(sw-bool-flag): 테스트 가짜의 고정 답이다
        uint32      _writtenCount;
    };
} // namespace

/** @brief [ShaderSourceGeneratorTest] 개발 구성에서 생성기가 하나도 없으면 쿠킹 실패다 — 낡은 생성물로 굽지 않는다. 배포 구성(필요 없음)은 통과다 */
SW_TEST_CASE( ShaderSourceGeneratorTest, NoGeneratorFailsTheCook )
{
    const AssetPaths& assetPaths = engine::getSubsystem<AssetPaths>();
    uint32            written    = 0;
    {
        SW_EXPECT_LOG( "ShaderCooker.NoSourceGenerator" );
        SW_EXPECT_FALSE( IShaderSourceGenerator::refreshGenerators( {}, true, assetPaths, "unused", written ) );
    }
    SW_EXPECT_TRUE( IShaderSourceGenerator::refreshGenerators( {}, false, assetPaths, "unused", written ) );
    SW_EXPECT_EQUAL( 0u, written );
}

/** @brief [ShaderSourceGeneratorTest] 실패한 생성기가 있으면 쿠킹 실패이고, 나머지 생성기도 돈다. 모두 맞추면 쓴 수를 모은다 */
SW_TEST_CASE( ShaderSourceGeneratorTest, FailedGeneratorFailsTheCook )
{
    const AssetPaths&         assetPaths = engine::getSubsystem<AssetPaths>();
    FakeShaderSourceGenerator broken{ "Broken", false, 0 };
    FakeShaderSourceGenerator working{ "Working", true, 3 };
    uint32                    written = 0;
    {
        SW_EXPECT_LOG( "ShaderCooker.SourceGeneratorFailed" );
        SW_EXPECT_FALSE( IShaderSourceGenerator::refreshGenerators( { &broken, &working }, true, assetPaths, "unused", written ) );
    }
    SW_EXPECT_EQUAL( 1u, broken._callCount );
    SW_EXPECT_EQUAL( 1u, working._callCount ); // 실패 뒤에도 나머지는 돈다
    SW_EXPECT_EQUAL( 3u, written );

    written = 0;
    SW_EXPECT_TRUE( IShaderSourceGenerator::refreshGenerators( { &working }, true, assetPaths, "unused", written ) );
    SW_EXPECT_EQUAL( 3u, written );
}
