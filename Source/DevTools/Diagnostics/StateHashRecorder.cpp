#include "pch.h"

#include "DevTools/Diagnostics/StateHashRecorder.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Scene/SceneObjectHash.h"

namespace sw
{
    /**
     * @brief `-gv_stateHashHistory=N`: 틱마다 씬 상태 해시를 남기고 최근 N 틱을 보관합니다(0=끄기). 개발 명령 `state.hashes` 로 읽습니다.
     * @details 에디터 멀티 프로세스 재생이 전용 서버와 클라이언트 둘 다에 준다(`EditorNetPlaySession`).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_stateHashHistory, 0, "틱별 씬 상태 해시 이력 길이 (0=끄기)" );

    SubsystemInitResult StateHashRecorder::initializeSubsystem()
    {
        if ( gv_stateHashHistory <= 0 )
            return SubsystemInitResult::Succeeded;
        StateHashHistory* const pHistory = engine::findSubsystem<StateHashHistory>();
        if ( pHistory == nullptr )
        {
            // 켜 달라는 요청을 조용히 버리지 않는다 — 이 호스트는 이력 서비스를 세우지 않는다.
            SW_LOG_ERROR_ID( "StateHashRecorder.NoHistory", "-gv_stateHashHistory=%# asked for but this host has no StateHashHistory service", static_cast<int32>( gv_stateHashHistory ) );
            return SubsystemInitResult::Succeeded;
        }
        pHistory->setCapacity( static_cast<uint32>( gv_stateHashHistory ) );
        return SubsystemInitResult::Succeeded;
    }
} // namespace sw
