#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"

namespace
{
    struct PlatformerMotorTuningTestUtil
    {
        static sw::PlatformerSettings makeSettings( int32 extraJumps, int32 airDashes )
        {
            sw::PlatformerSettings settings;
            settings._extraJumpCount = extraJumps;
            settings._airDashCount   = airDashes;
            return settings;
        }

        /** @brief 모터의 상태 바이트입니다. 남은 공중 점프 · 대시가 여기에 실린다. */
        static sw::vector<uint8> readBytes( const sw::PlatformerMotor2D& motor )
        {
            sw::Archive archive;
            motor.writeState( archive );
            const uint8* pData = static_cast<const uint8*>( archive.getData() );
            return sw::vector<uint8>( pData, pData + archive.getSize() );
        }
    };
} // namespace

// 결함: 활공을 켜고 끌 때마다 `setSettings` 가 남은 공중 점프 · 대시를 채워 활공으로 횟수를 되살렸다 - 수치만 바꾸는 입구를 나눴다.
SW_TEST_CASE( PlatformerMotorTuningTest, ChangingTuningKeepsWhatIsLeft )
{
    sw::PlatformerMotor2D refilled;
    refilled.setSettings( PlatformerMotorTuningTestUtil::makeSettings( 2, 1 ) );

    sw::PlatformerMotor2D tuned;
    tuned.setSettings( PlatformerMotorTuningTestUtil::makeSettings( 2, 1 ) );
    tuned.changeTuning( PlatformerMotorTuningTestUtil::makeSettings( 5, 4 ) );
    SW_EXPECT_EQUAL( tuned.getSettings()._extraJumpCount, 5 );
    SW_EXPECT_TRUE( PlatformerMotorTuningTestUtil::readBytes( tuned ) == PlatformerMotorTuningTestUtil::readBytes( refilled ) ); // 상한이 올라도 채우지 않는다
}

SW_TEST_CASE( PlatformerMotorTuningTest, LoweringTuningClampsWhatIsLeft )
{
    sw::PlatformerMotor2D expected;
    expected.setSettings( PlatformerMotorTuningTestUtil::makeSettings( 1, 0 ) );

    sw::PlatformerMotor2D tuned;
    tuned.setSettings( PlatformerMotorTuningTestUtil::makeSettings( 3, 2 ) );
    tuned.changeTuning( PlatformerMotorTuningTestUtil::makeSettings( 1, 0 ) );
    SW_EXPECT_TRUE( PlatformerMotorTuningTestUtil::readBytes( tuned ) == PlatformerMotorTuningTestUtil::readBytes( expected ) );
}
