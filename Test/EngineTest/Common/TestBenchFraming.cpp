/**
 * @file TestBenchFraming.cpp
 * @brief 벤치 카메라 맞춤의 순수 구조 — 입력이 달라지면 다시 맞추고 같으면 맞추지 않는 비교, 보기 모드 우선순위.
 */
#include "pch.h"

#include "Games/Empty/BenchFraming.h"

#include "TestFramework/TestFramework.h"

SW_TEST_CASE( BenchFramingTest, SameInputIsEqual )
{
    sw::FramingInput first;
    first.mode              = sw::FramingMode::Character;
    first.characters        = 6;
    sw::FramingInput second = first;
    SW_EXPECT_TRUE( first == second );
    SW_EXPECT_FALSE( first != second );
}

SW_TEST_CASE( BenchFramingTest, EveryFieldChangeIsDetected )
{
    const sw::FramingInput base;

    sw::FramingInput changed = base;
    changed.mode             = sw::FramingMode::Face;
    SW_EXPECT_TRUE( changed != base );

    changed              = base;
    changed.cameraSerial = 1;
    SW_EXPECT_TRUE( changed != base );

    changed          = base;
    changed.gridSide = 3;
    SW_EXPECT_TRUE( changed != base );

    changed            = base;
    changed.characters = 2;
    SW_EXPECT_TRUE( changed != base );

    changed         = base;
    changed.columns = 2;
    SW_EXPECT_TRUE( changed != base );

    changed       = base;
    changed.faces = 2;
    SW_EXPECT_TRUE( changed != base );

    changed          = base;
    changed.rigCount = 2;
    SW_EXPECT_TRUE( changed != base );

    changed         = base;
    changed.rigView = 2;
    SW_EXPECT_TRUE( changed != base );
}

SW_TEST_CASE( BenchFramingTest, ModePriorityFaceRigRagdollCharacterGrid )
{
    using sw::FramingMode;
    using sw::selectFramingMode;
    // 높은 쪽이 이긴다. 낮은 스폰이 나중에 와도 덮어쓰지 않는다.
    SW_EXPECT_TRUE( selectFramingMode( FramingMode::Grid, FramingMode::Character ) == FramingMode::Character );
    SW_EXPECT_TRUE( selectFramingMode( FramingMode::Character, FramingMode::Grid ) == FramingMode::Character );
    SW_EXPECT_TRUE( selectFramingMode( FramingMode::Character, FramingMode::Ragdoll ) == FramingMode::Ragdoll );
    SW_EXPECT_TRUE( selectFramingMode( FramingMode::Ragdoll, FramingMode::Rig ) == FramingMode::Rig );
    SW_EXPECT_TRUE( selectFramingMode( FramingMode::Rig, FramingMode::Face ) == FramingMode::Face );
    SW_EXPECT_TRUE( selectFramingMode( FramingMode::Face, FramingMode::Rig ) == FramingMode::Face );
    SW_EXPECT_TRUE( selectFramingMode( FramingMode::Face, FramingMode::Grid ) == FramingMode::Face );
    SW_EXPECT_TRUE( selectFramingMode( FramingMode::Combat, FramingMode::Character ) == FramingMode::Combat );
}
