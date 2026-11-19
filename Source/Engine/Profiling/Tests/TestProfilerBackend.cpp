// ProfilerBackendTest — 태스크 프로파일 훅: 외부 출력이 없으면 구간을 열지 않는다(nogpu).
#include "pch.h"

#include "Core/Task/TaskTypes.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Profiling/ProfilerBackend.h"

using namespace sw;

#if SW_PROFILER_BACKEND_COMPILED
/**
 * @brief [ProfilerBackendTest] 태스크 훅은 함수 한 쌍을 다 갖고, 활성 출력이 없는 동안에는 구간을 열지 않는다(연 출력이 nullptr — 닫을 것이 없다)
 * @details `TaskManager::bindProfileHook` 에 꽂는 그 훅이다. 테스트가 도는 동안 켜져 있던 출력은 끝에 되돌린다.
 */
SW_TEST_CASE( ProfilerBackendTest, TaskHookOpensNoZoneWithoutAnActiveBackend )
{
    const TaskProfileHook* pHook = ProfilerBackend::getTaskProfileHook();
    SW_REQUIRE_NOT_NULL( pHook );
    SW_REQUIRE_TRUE( pHook->_pBeginZone != nullptr && pHook->_pEndZone != nullptr );

    IProfilerBackend* const pPrevious = ProfilerBackend::getActiveBackend();
    ProfilerBackend::setActiveBackend( nullptr );
    const TaskProfileZone zone = pHook->_pBeginZone( "ProfilerBackendTest.Task", TaskProfileZoneKind::Execute );
    ProfilerBackend::setActiveBackend( pPrevious );
    SW_EXPECT_TRUE( zone._pContext == nullptr );
}
#endif
