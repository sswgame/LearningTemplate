#include "pch.h"

#include "Engine/Automation/Scenario/AutomationCaptureUtil.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Presentation/IEngineRenderDriver.h"

namespace sw
{
    bool AutomationCaptureUtil::isReadyForCapture( const GameObjectManager* pManager )
    {
        return AutomationStepRegistry::findCaptureBlocker().isWaiting() == false && countPendingJobs( pManager ) == 0;
    }

    uint32 AutomationCaptureUtil::countPendingJobs( const GameObjectManager* pManager )
    {
        const IEngineRenderDriver* const pRenderDriver = engine::findSubsystem<IEngineRenderDriver>();
        return pRenderDriver != nullptr ? pRenderDriver->countPendingCaptureJobs( pManager ) : 0u;
    }
} // namespace sw
