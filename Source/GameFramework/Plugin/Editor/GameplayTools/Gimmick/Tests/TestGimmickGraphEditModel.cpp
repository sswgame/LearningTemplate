#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphEditModel.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Circuit/GimmickNodeRegistry.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( GimmickGraphEditModelTest, FirstNodeGoesToTheOrigin )
{
    const GimmickNodeRegistry   registry;
    const GimmickGraphEditModel model( registry );
    const float2                position = model.computeNewNodePosition();
    SW_EXPECT_EQUAL( 0.0f, position._x );
    SW_EXPECT_EQUAL( 0.0f, position._y );
}
