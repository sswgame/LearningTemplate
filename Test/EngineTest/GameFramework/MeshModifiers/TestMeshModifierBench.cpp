#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( MeshModifierBenchTest, "100k vertices through eight mesh modifiers" );

// ------------------------------------------------------------------------------
// MeshModifierBenchTest — 정점 약 10 만 개에 모디파이어 8 개를 거는 CPU 정본 경로의 비용. 숫자는 로그로만 남기고 기준값은 두지 않는다.
// Release 에서 측정한다: EngineTest.exe --test_filter=MeshModifierBenchTest.*
// ------------------------------------------------------------------------------

using namespace sw;
using namespace sw::editor;

namespace
{
    struct MeshModifierBenchTestInternal
    {
        /** @brief 6 면 · 52² 칸 · 6 정점 = 97344 정점입니다. */
        static constexpr uint32 kSubdivision = 52;
    };
} // namespace

/**
 * @brief [MeshModifierBenchTest] 휘기 · 비틀기 · 테이퍼 · 늘이기 · 물결 · 노이즈 · 녹이기 · 격자(4x4x4) 여덟 개를 한 번 거는 시간
 * @details 테스트는 결과가 유한한지와 정점 수만 본다.
 */
SW_TEST_CASE( MeshModifierBenchTest, HundredThousandVerticesEightModifiers )
{
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( MeshModifierBenchTestInternal::kSubdivision );
    MeshModifierStack      stack;
    const utf8*            arrID[] = { "bend", "twist", "taper", "stretch", "wave", "noise", "melt", "ffd4" };
    uint32                 salt    = 11;
    for ( const utf8* pID : arrID )
    {
        const int32 index = stack.addModifier( pID );
        SW_ASSERT_TRUE( index >= 0 );
        stack.findModifier( index )->fitToBounds( MeshModifierBounds::makeFromPoints( source._listPosition ) );
        test::MeshModifierTestUtil::fillParams( *stack.findModifier( index ), salt++ );
    }

    MeshModifierMesh             mesh = source;
    const MeshModifierEvaluation evaluation{};
    const Stopwatch              stopwatch;
    stack.apply( mesh, evaluation );
    [[maybe_unused]] const int64 elapsedMicro = stopwatch.getElapsedMicroseconds();

    SW_EXPECT_EQUAL( source._listPosition.size(), mesh._listPosition.size() );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isFinite( mesh._listPosition ) );
    SW_LOG_INFO( "[MeshModifierBench] %# vertices x %# modifiers: %# us (%# ns per vertex-modifier)", mesh._listPosition.size(), stack.getModifierCount(), elapsedMicro,
                 static_cast<float32>( elapsedMicro ) * 1000.0f / static_cast<float32>( mesh._listPosition.size() * static_cast<size_t>( stack.getModifierCount() ) ) );
}
