#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphModel.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickNodeRegistry.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( GimmickGraphModelTest, FirstNodeGoesToTheOrigin )
{
    const GimmickNodeRegistry registry;
    const GimmickGraphModel   model( registry );
    const float2              position = model.computeNewNodePosition();
    SW_EXPECT_EQUAL( 0.0f, position._x );
    SW_EXPECT_EQUAL( 0.0f, position._y );
}
