#include "pch.h"

#include "TestFramework/TestEnvironment.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Scene/SceneManager.h"

namespace test
{
    void TestEnvironment::tearDown() const
    {
        if ( sw::engine::areEngineServicesBound() == false )
            return;

        sw::engine::getSubsystem<sw::SceneManager>().cancelPendingAsyncLoads();
        sw::engine::getSubsystem<sw::TaskManager>().waitAll();
        sw::engine::getSubsystem<sw::TaskManager>().clear();
    }
} // namespace test
