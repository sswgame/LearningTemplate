#include "pch.h"

#include "App/Module/LiveReloadManager.h"

#include "TestFramework/TestFramework.h"

namespace
{
    // 컴파일러가 널임을 증명하지 못하게 전역에 둔다. 증명하면 쓰기를 트랩 명령으로 바꿔 버린다(그것도 잡히긴 한다).
    uintptr_t s_faultAddress{ 0 };
    int32     s_guardProbeValue{ 0 };

    /** @brief 널 가까운 주소에 써서 접근 위반을 냅니다. */
    void writeThroughNull()
    {
        *reinterpret_cast<volatile int32*>( s_faultAddress ) = 7;
    }
} // namespace

/**
 * @brief [ModuleCallGuardTest] 호출 안의 접근 위반은 그 호출만 실패시키고, 그 뒤의 호출은 평소대로 돈다
 * @details 핫 리로드가 새 모듈 코드를 처음 부르는 자리(onAfterReload)를 이것으로 지킨다. 결함 코드가 남고, 가드를 빠져나온 뒤에도
 *          처리기가 제자리로 돌아와 다음 호출이 정상으로 끝나는지 본다.
 */
SW_TEST_CASE( ModuleCallGuardTest, FaultInsideTheCallIsContainedAndReported )
{
    uint32 faultCode{ 0 };
    SW_EXPECT_FALSE( sw::ModuleCallGuard::run( SW_DELEGATE_FUNCTION( sw::Delegate<void()>, writeThroughNull ), faultCode ) );
    SW_EXPECT_TRUE( faultCode != 0 );

    s_guardProbeValue = 0;
    SW_EXPECT_TRUE( sw::ModuleCallGuard::run( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []()
    { s_guardProbeValue += 5; } ),
                                              faultCode ) );
    SW_EXPECT_EQUAL( 0u, faultCode );
    SW_EXPECT_EQUAL( 5, s_guardProbeValue );
}

/**
 * @brief [ModuleCallGuardTest] 겹친 가드는 안쪽 결함을 안쪽에서 받고, 바깥 가드는 그 뒤에도 자기 결함을 받는다
 * @details 리눅스의 시그널 처리기는 프로세스 전체에 걸리므로 바깥 가드만 설치 · 해제한다. 안쪽이 빠져나올 때 처리기를 내려 버리면
 *          바깥의 두 번째 결함이 프로세스를 내린다.
 */
SW_TEST_CASE( ModuleCallGuardTest, NestedGuardsEachCatchTheirOwnFault )
{
    s_guardProbeValue = 0;
    uint32     outerFaultCode{ 0 };
    const bool bOuterCompleted = sw::ModuleCallGuard::run( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []()
    {
        uint32 innerFaultCode{ 0 };
        if ( sw::ModuleCallGuard::run( SW_DELEGATE_FUNCTION( sw::Delegate<void()>, writeThroughNull ), innerFaultCode ) == false && innerFaultCode != 0 )
            s_guardProbeValue += 10;
        writeThroughNull();
        s_guardProbeValue += 100;
    } ),
                                                           outerFaultCode );
    SW_EXPECT_FALSE( bOuterCompleted );
    SW_EXPECT_TRUE( outerFaultCode != 0 );
    SW_EXPECT_EQUAL( 10, s_guardProbeValue );
}
