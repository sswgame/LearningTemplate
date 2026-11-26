// AnimationNotifyCrossingTest — 클립 커서의 알림 문턱(공용 구간 `PlaybackSegment` 규칙): 반복 경계 · 끝 시각 · 큰 dt 에서 알림이 지날 때마다 한 번(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/AnimationPlayback.h"

using namespace sw;

namespace
{
    struct AnimationNotifyCrossingTestUtil
    {
        /** @brief 0 · 0.5 · 1(끝) 초에 알림이 있는 1 초 클립의 트랙입니다. */
        static AnimationNotifyTrack makeTrack()
        {
            AnimationNotifyTrack track;
            for ( const float32 time : { 0.0f, 0.5f, 1.0f } )
            {
                AnimationNotifyEvent event{};
                event._name = hashed_string( time == 0.0f ? "Start" : ( time == 0.5f ? "Middle" : "End" ) );
                event._time = time;
                track.addEvent( event );
            }
            return track;
        }

        static uint32 countNamed( const vector<AnimationFiredNotify>& listFired, const utf8* pName )
        {
            uint32 count = 0;
            for ( const AnimationFiredNotify& fired : listFired )
                count += fired._name == hashed_string( pName ) ? 1u : 0u;
            return count;
        }
    };
} // namespace

/** @brief [AnimationNotifyCrossingTest] 60 Hz 로 2.83 초 반복 재생하면 처음 알림은 시작 + 바퀴마다, 가운데는 지날 때마다, 끝 알림은 되감을 때마다 한 번이다(누적 시각이 경계 위 · 아래여도) */
SW_TEST_CASE( AnimationNotifyCrossingTest, LoopFiresEachNotifyOncePerPass )
{
    using Util                       = AnimationNotifyCrossingTestUtil;
    const AnimationNotifyTrack track = Util::makeTrack();
    AnimationClipCursor        cursor;
    cursor.reset();
    vector<AnimationFiredNotify> listFired;
    for ( int32 stepIndex = 0; stepIndex < 170; ++stepIndex )
        track.collectFired( cursor.advance( 1.0f / 60.0f, 1.0f, true ), 1.0f, 1.0f, nullptr, listFired );
    SW_EXPECT_EQUAL( 3u, Util::countNamed( listFired, "Start" ) );
    SW_EXPECT_EQUAL( 3u, Util::countNamed( listFired, "Middle" ) );
    SW_EXPECT_EQUAL( 2u, Util::countNamed( listFired, "End" ) );
}

/** @brief [AnimationNotifyCrossingTest] 반복하지 않는 재생은 끝 알림을 끝에 닿는 걸음에 한 번 쏘고, 끝에 선 뒤의 걸음은 아무것도 지나지 않는다 */
SW_TEST_CASE( AnimationNotifyCrossingTest, NonLoopingEndFiresOnce )
{
    using Util                       = AnimationNotifyCrossingTestUtil;
    const AnimationNotifyTrack track = Util::makeTrack();
    AnimationClipCursor        cursor;
    cursor.reset();
    vector<AnimationFiredNotify> listFired;
    for ( int32 stepIndex = 0; stepIndex < 90; ++stepIndex )
        track.collectFired( cursor.advance( 1.0f / 60.0f, 1.0f, false ), 1.0f, 1.0f, nullptr, listFired );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( listFired, "Start" ) );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( listFired, "Middle" ) );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( listFired, "End" ) );
}

/** @brief [AnimationNotifyCrossingTest] 큰 dt 한 걸음이 두 바퀴를 넘으면 지난 바퀴마다 센다 — 가운데 알림은 0.25 → 2.75 초 동안 세 번 지났다 */
SW_TEST_CASE( AnimationNotifyCrossingTest, LargeStepCountsEveryPassedLoop )
{
    using Util                       = AnimationNotifyCrossingTestUtil;
    const AnimationNotifyTrack track = Util::makeTrack();
    AnimationClipCursor        cursor;
    cursor.reset( 0.25f );
    vector<AnimationFiredNotify> listFired;
    track.collectFired( cursor.advance( 0.0f, 1.0f, true ), 1.0f, 1.0f, nullptr, listFired ); // 첫 걸음(시작 시각 포함) — 0.25 에는 알림이 없다
    SW_EXPECT_TRUE( listFired.empty() );
    track.collectFired( cursor.advance( 2.5f, 1.0f, true ), 1.0f, 1.0f, nullptr, listFired );
    SW_EXPECT_EQUAL( 3u, Util::countNamed( listFired, "Middle" ) );
    SW_EXPECT_EQUAL( 2u, Util::countNamed( listFired, "End" ) );
    SW_EXPECT_EQUAL( 2u, Util::countNamed( listFired, "Start" ) );
}
