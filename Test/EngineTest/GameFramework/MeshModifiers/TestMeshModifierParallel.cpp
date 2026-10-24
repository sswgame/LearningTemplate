#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// MeshModifierParallelTest — 작업 관리자를 건 병렬 적용이 직렬 결과와 비트 단위로 같고, Debug 레이스 검출기에 걸리지 않는지 본다.
// 기본 정점 일(`deformPoint` 경로)은 덩어리마다 다른 워커가 같은 정점 배열을 쓴다 — 배열의 비const 인덱싱은 레이스 검출기가 쓰기로 세어
// 두 워커가 겹치는 순간 멈춘다. 그래서 일은 생 포인터로 자기 덩어리만 쓴다.
// ------------------------------------------------------------------------------

using namespace sw;
using namespace sw::editor;

namespace
{
    struct MeshModifierParallelTestInternal
    {
        /** @brief 6 면 · 40² 칸 · 6 정점 = 57600 정점(덩어리 15 개)입니다. 워커 셋이 겹치기에 충분합니다. */
        static constexpr uint32 kSubdivision = 40;
    };
} // namespace

/**
 * @brief [MeshModifierParallelTest] 점 단위 모디파이어 여럿을 병렬로 건 결과가 직렬과 비트 단위로 같다
 * @details 법선 · UV · 색이 있는 메시라 야코비안 법선 경로까지 돈다. Debug 에서는 정점 배열 접근이 레이스 검출기를 지난다.
 */
SW_TEST_CASE( MeshModifierParallelTest, DeformJobMatchesSerialUnderTaskManager )
{
    TaskManager taskManager;
    SW_ASSERT_TRUE( taskManager.initialize( 3 ) );

    const MeshModifierMesh source  = test::MeshModifierTestUtil::makeBoxMesh( MeshModifierParallelTestInternal::kSubdivision );
    const utf8*            arrID[] = { "bend", "twist", "taper", "wave", "noise", "spherify" };
    MeshModifierStack      stack;
    uint32                 salt = 3;
    for ( const utf8* pID : arrID )
    {
        const int32 index = stack.addModifier( pID );
        SW_ASSERT_TRUE( index >= 0 );
        stack.findModifier( index )->fitToBounds( MeshModifierBounds::makeFromPoints( source._listPosition ) );
        test::MeshModifierTestUtil::fillParams( *stack.findModifier( index ), salt++ );
    }

    const MeshModifierEvaluation serialEvaluation{};
    MeshModifierMesh             serialMesh = source;
    stack.apply( serialMesh, serialEvaluation );

    MeshModifierEvaluation parallelEvaluation{};
    parallelEvaluation._pTaskManager = &taskManager;
    MeshModifierMesh parallelMesh    = source;
    stack.apply( parallelMesh, parallelEvaluation );
    taskManager.shutdown();

    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( serialMesh._listPosition, parallelMesh._listPosition ) );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( serialMesh._listNormal, parallelMesh._listNormal ) );
    // 결과가 원본과 같으면 비교가 빈 일끼리 맞은 것이다
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( source._listPosition, parallelMesh._listPosition ) > 0.01f );
}
