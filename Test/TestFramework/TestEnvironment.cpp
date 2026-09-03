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

        sw::engine::getSceneManager().cancelPendingAsyncLoads();
        sw::engine::getTaskManager().waitAll();
        sw::engine::getTaskManager().clear();
    }
} // namespace test
