/**
 * @file LocalStoreChannelDevTools.cpp
 * @brief `LocalStoreChannel` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/thread.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Runtime/Foundation/Save/Local/LocalDeviceKeyProvider.h"
#include "GameFramework/Runtime/Foundation/Save/Local/LocalStoreChannel.h"
#include "GameFramework/Runtime/Foundation/Save/Local/LocalStoreFactory.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        /** @brief 저장 · 썸네일 쓰기가 끝나기 전에 시나리오가 그 결과를 보거나 끝나지 않게 한다(에디터 자리 잡기 · `<Pass/>`). */
        AutomationReadiness readLocalStoreRequests()
        {
            return LocalStoreChannel::isAnyChannelBusy() ? AutomationReadiness::waiting( 0, "a local store write is in flight" ) : AutomationReadiness::ready();
        }
    } // namespace

    SW_AUTOMATION_FINISH_BLOCKER( localStoreRequests, "LocalStoreRequests", &readLocalStoreRequests );
} // namespace sw
