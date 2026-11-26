#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Plugin/Editor/GameplayTools/LogicCircuit/Model/LogicCircuitGraphEditModel.h"
#include "GameFramework/Runtime/Gameplay/Mechanism/Circuit/LogicCircuitNodeRegistry.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( LogicCircuitGraphEditModelTest, FirstNodeGoesToTheOrigin )
{
    const LogicCircuitNodeRegistry   registry;
    const LogicCircuitGraphEditModel model( registry );
    const float2                     position = model.computeNewNodePosition();
    SW_EXPECT_EQUAL( 0.0f, position._x );
    SW_EXPECT_EQUAL( 0.0f, position._y );
}
