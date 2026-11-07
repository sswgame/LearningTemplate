// UIAnimationPlayerTest — UI 애니메이션 재생기: 키 보간, 반복, 거꾸로, 멈춤 · 찾아가기, 이벤트(시각 · 곡선 지나감), 트윈 대체, 움직임 줄이기, 잘못된 트랙(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/Tests/UITestWidgets.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

using namespace sw;

namespace
{
    constexpr float32 kTolerance = 1.0e-3f;

    struct CommandLog
    {
        vector<string> _listCommand{};

        void onCommand( const hashed_string& command, Widget& /*source*/ ) { _listCommand.push_back( command.c_str() ); }
    };

    /** @brief 잎 "leaf" 하나를 가진 화면과 그 재생기입니다. 명령은 "tick" · "up" · "down" 을 받아 적습니다. */
    struct AnimationRig
    {
        UIScreen             _screen;
        Widget*              _pLeaf;
        CommandLog           _log;
        vector<Subscription> _listSubscription;

        AnimationRig()
            : _screen{ UIScreenDef{}, make_unique<CanvasPanel>() }
            , _pLeaf{ nullptr }
            , _log{}
            , _listSubscription{}
        {
            UITestLeaf& leaf = UITestUtil::addLeaf( *castTo<PanelWidget>( _screen.getTree().getRoot() ), float2{ 10.0f, 10.0f } );
            leaf.setName( "leaf" );
            _pLeaf = &leaf;
            for ( const utf8* pCommand : { "tick", "up", "down" } )
            {
                _listSubscription.push_back( _screen.registerCommand( hashed_string( pCommand ), UICommandDelegate::create<&CommandLog::onCommand>( &_log ) ) );
            }
        }

        UIAnimationPlayer& player() { return _screen.getAnimationPlayer(); }

        /** @brief 불투명도를 0 에서 1 까지 1 초 동안 직선으로 올리는 애니메이션입니다. 0.5 초에 "tick" 을 보냅니다. */
        static UIAnimation makeFade()
        {
            UIAnimation animation;
            animation._name = "fade";
            UIAnimationTrack track;
            track._widget   = "leaf";
            track._property = "_opacity";
            UIAnimationKey first;
            first._time  = 0.0f;
            first._value = "0";
            UIAnimationKey second;
            second._time   = 1.0f;
            second._value  = "1";
            second._curve  = BlendCurve::Linear;
            track._listKey = { first, second };
            animation._listTrack.push_back( track );
            UIAnimationEvent event;
            event._time    = 0.5f;
            event._command = "tick";
            animation._listEvent.push_back( event );
            return animation;
        }

        float32 opacity() const { return _pLeaf->getOpacity(); }
    };

    /** @brief 움직임 줄이기 설정을 끄고 시험한 뒤 되돌립니다. */
    struct ScopedMotion
    {
        bool _bSaved{ gv_uiReduceMotion }; ///< NOLINT(sw-bool-flag): 집합체 기본값으로 지금 값을 담는다

        explicit ScopedMotion( bool bReduce ) { gv_uiReduceMotion = bReduce; }
        ~ScopedMotion() { gv_uiReduceMotion = _bSaved; }
    };
} // namespace

/** @brief [UIAnimationPlayerTest] 재생: 첫 값을 바로 쓰고, 키 사이를 곡선으로 옮기며, 끝나면 끝 값을 두고 빠진다. 없는 이름은 경고하고 false 다 */
SW_TEST_CASE( UIAnimationPlayerTest, PlaysAndFinishes )
{
    SW_EXPECT_LOG( "UIAnimationPlayer.AnimationNotScreen" );
    const ScopedMotion motion( false );
    AnimationRig       rig;
    rig._pLeaf->setOpacity( 0.25f );
    rig.player().setAnimations( { AnimationRig::makeFade() } );
    SW_EXPECT_FALSE( rig.player().play( "nothing" ) );

    SW_REQUIRE_TRUE( rig.player().play( "fade" ) );
    SW_EXPECT_NEAR( 0.0f, rig.opacity(), kTolerance );
    SW_EXPECT_TRUE( rig.player().isPlaying( "fade" ) );

    rig.player().update( 0.25f );
    SW_EXPECT_NEAR( 0.25f, rig.opacity(), kTolerance );
    float32 time     = 0.0f;
    float32 duration = 0.0f;
    SW_REQUIRE_TRUE( rig.player().getPlaybackTime( "fade", time, duration ) );
    SW_EXPECT_NEAR( 0.25f, time, kTolerance );
    SW_EXPECT_NEAR( 1.0f, duration, kTolerance );

    rig.player().update( 2.0f );
    SW_EXPECT_NEAR( 1.0f, rig.opacity(), kTolerance );
    SW_EXPECT_FALSE( rig.player().isPlaying( "fade" ) );
    SW_EXPECT_FALSE( rig.player().isAnyPlaying() );
}

/** @brief [UIAnimationPlayerTest] 이벤트: 지나간 시각에 한 번씩 나가고, 한 틱에 여러 바퀴를 넘으면 바퀴마다 나간다. 반복 0 은 멈출 때까지다 */
SW_TEST_CASE( UIAnimationPlayerTest, EventsFireOncePerPass )
{
    const ScopedMotion motion( false );
    AnimationRig       rig;
    rig.player().setAnimations( { AnimationRig::makeFade() } );
    SW_REQUIRE_TRUE( rig.player().play( "fade" ) );
    rig.player().update( 0.4f );
    SW_EXPECT_TRUE( rig._log._listCommand.empty() );
    rig.player().update( 0.1f ); // 0.5 에 정확히 닿았다 - 끝을 넣지 않으므로 다음 틱에 나간다
    SW_EXPECT_TRUE( rig._log._listCommand.empty() );
    rig.player().update( 0.1f );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._log._listCommand.size() );
    rig.player().update( 0.1f );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._log._listCommand.size() );

    rig._log._listCommand.clear();
    SW_REQUIRE_TRUE( rig.player().play( "fade", 1.0f, 3 ) );
    rig.player().update( 2.5f ); // 세 바퀴째의 중간
    SW_EXPECT_EQUAL( size_t( 2 ), rig._log._listCommand.size() );
    SW_EXPECT_TRUE( rig.player().isPlaying( "fade" ) );
    rig.player().update( 1.0f );
    SW_EXPECT_EQUAL( size_t( 3 ), rig._log._listCommand.size() );
    SW_EXPECT_FALSE( rig.player().isPlaying( "fade" ) );

    SW_REQUIRE_TRUE( rig.player().play( "fade", 2.0f, 0 ) ); // 끝없이, 두 배속
    rig.player().update( 100.0f );
    SW_EXPECT_TRUE( rig.player().isPlaying( "fade" ) );
    rig.player().stop( "fade" );
    SW_EXPECT_FALSE( rig.player().isPlaying( "fade" ) );
}

/** @brief [UIAnimationPlayerTest] 거꾸로: 끝에서 처음으로 가고, 재생 중 뒤집으면 그 자리에서 돌아간다 */
SW_TEST_CASE( UIAnimationPlayerTest, ReverseGoesBack )
{
    const ScopedMotion motion( false );
    AnimationRig       rig;
    rig.player().setAnimations( { AnimationRig::makeFade() } );
    SW_REQUIRE_TRUE( rig.player().playReverse( "fade" ) );
    SW_EXPECT_NEAR( 1.0f, rig.opacity(), kTolerance );
    rig.player().update( 0.25f );
    SW_EXPECT_NEAR( 0.75f, rig.opacity(), kTolerance );

    rig.player().reverse( "fade" );
    rig.player().update( 0.25f );
    SW_EXPECT_NEAR( 1.0f, rig.opacity(), kTolerance );
    SW_EXPECT_FALSE( rig.player().isPlaying( "fade" ) );

    rig.player().reverse( "fade" ); // 재생 중이 아니면 거꾸로 재생이다
    rig.player().update( 1.5f );
    SW_EXPECT_NEAR( 0.0f, rig.opacity(), kTolerance );
}

/** @brief [UIAnimationPlayerTest] 찾아가기: 그 시각의 값을 쓰고 멈춘다. 지나간 이벤트는 보내지 않고, 풀면 이어 간다 */
SW_TEST_CASE( UIAnimationPlayerTest, SeekPausesWithoutEvents )
{
    const ScopedMotion motion( false );
    AnimationRig       rig;
    rig.player().setAnimations( { AnimationRig::makeFade() } );
    SW_REQUIRE_TRUE( rig.player().seek( "fade", 0.75f ) );
    SW_EXPECT_NEAR( 0.75f, rig.opacity(), kTolerance );
    rig.player().update( 1.0f );
    SW_EXPECT_NEAR( 0.75f, rig.opacity(), kTolerance );
    SW_EXPECT_TRUE( rig._log._listCommand.empty() );

    SW_REQUIRE_TRUE( rig.player().seek( "fade", 99.0f ) ); // 길이로 자른다
    SW_EXPECT_NEAR( 1.0f, rig.opacity(), kTolerance );
    SW_REQUIRE_TRUE( rig.player().seek( "fade", 0.25f ) );
    rig.player().setPaused( "fade", false );
    rig.player().update( 0.5f );
    SW_EXPECT_NEAR( 0.75f, rig.opacity(), kTolerance );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._log._listCommand.size() );
}

/** @brief [UIAnimationPlayerTest] 곡선 위 사건: 값이 임계값을 지나는 방향에 맞을 때만 나간다. 거꾸로 재생에서는 내려가는 쪽이다 */
SW_TEST_CASE( UIAnimationPlayerTest, CurveEventsFollowDirection )
{
    const ScopedMotion motion( false );
    AnimationRig       rig;
    UIAnimation        animation = AnimationRig::makeFade();
    animation._listEvent.clear();
    UIAnimationCurveEvent rising;
    rising._threshold                       = 0.5f;
    rising._crossing                        = UICurveCrossing::Rising;
    rising._command                         = "up";
    UIAnimationCurveEvent falling           = rising;
    falling._crossing                       = UICurveCrossing::Falling;
    falling._command                        = "down";
    animation._listTrack[0]._listCurveEvent = { rising, falling };
    rig.player().setAnimations( { animation } );

    SW_REQUIRE_TRUE( rig.player().play( "fade" ) );
    rig.player().update( 0.4f );
    SW_EXPECT_TRUE( rig._log._listCommand.empty() );
    rig.player().update( 0.2f );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._log._listCommand.size() );
    SW_EXPECT_EQUAL( string( "up" ), rig._log._listCommand[0] );
    rig.player().update( 1.0f );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._log._listCommand.size() );

    rig._log._listCommand.clear();
    SW_REQUIRE_TRUE( rig.player().playReverse( "fade" ) );
    rig.player().update( 1.5f );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._log._listCommand.size() );
    SW_EXPECT_EQUAL( string( "down" ), rig._log._listCommand[0] );
}

/** @brief [UIAnimationPlayerTest] 트윈: 지금 값에서 끝값으로 옮기고, 같은 칸의 새 트윈은 앞의 것을 대신한다. 위젯이 사라지면 빠진다. 틀린 경로와 값은 거절한다 */
SW_TEST_CASE( UIAnimationPlayerTest, TweensReplaceAndFinish )
{
    SW_EXPECT_LOG( "UIAnimationPlayer.Tween" );
    SW_EXPECT_LOG( "UIAnimationPlayer.TweenCannotRead" );
    SW_EXPECT_LOG( "UIAnimationPlayer.TweenWidgetNotTree" );
    const ScopedMotion motion( false );
    AnimationRig       rig;
    const WidgetID     leafID = rig._pLeaf->getID();
    rig._pLeaf->setOpacity( 0.0f );

    SW_REQUIRE_TRUE( rig.player().tween( leafID, "_opacity", "1", 1.0f, BlendCurve::Linear ) );
    SW_EXPECT_EQUAL( uint32( 1 ), rig.player().getTweenCount() );
    rig.player().update( 0.5f );
    SW_EXPECT_NEAR( 0.5f, rig.opacity(), kTolerance );

    SW_REQUIRE_TRUE( rig.player().tween( leafID, "_opacity", "0", 1.0f, BlendCurve::Linear ) );
    SW_EXPECT_EQUAL( uint32( 1 ), rig.player().getTweenCount() );
    rig.player().update( 0.5f );
    SW_EXPECT_NEAR( 0.25f, rig.opacity(), kTolerance );
    rig.player().update( 1.0f );
    SW_EXPECT_NEAR( 0.0f, rig.opacity(), kTolerance );
    SW_EXPECT_EQUAL( uint32( 0 ), rig.player().getTweenCount() );

    SW_REQUIRE_TRUE( rig.player().tween( leafID, "_opacity", "1", 0.0f ) ); // 길이 0 은 바로 끝이다
    SW_EXPECT_NEAR( 1.0f, rig.opacity(), kTolerance );
    SW_EXPECT_EQUAL( uint32( 0 ), rig.player().getTweenCount() );

    SW_EXPECT_FALSE( rig.player().tween( leafID, "_nothing", "1", 1.0f ) );
    SW_EXPECT_FALSE( rig.player().tween( leafID, "_opacity", "abc", 1.0f ) );
    SW_EXPECT_FALSE( rig.player().tween( kInvalidWidgetID, "_opacity", "1", 1.0f ) );

    SW_REQUIRE_TRUE( rig.player().tween( leafID, "_opacity", "0", 1.0f ) );
    (void)rig._pLeaf->getParent()->removeChild( rig._pLeaf );
    rig.player().update( 0.1f );
    SW_EXPECT_EQUAL( uint32( 0 ), rig.player().getTweenCount() );
}

/** @brief [UIAnimationPlayerTest] 움직임 줄이기: 끝 값을 바로 쓰고 이벤트는 모두 보낸다(닫기 애니메이션의 명령이 사라지지 않는다). 재생 중에 켜도 끝으로 간다 */
SW_TEST_CASE( UIAnimationPlayerTest, ReduceMotionJumpsToEnd )
{
    AnimationRig rig;
    rig.player().setAnimations( { AnimationRig::makeFade() } );
    {
        const ScopedMotion motion( true );
        SW_REQUIRE_TRUE( rig.player().play( "fade" ) );
        SW_EXPECT_NEAR( 1.0f, rig.opacity(), kTolerance );
        SW_EXPECT_FALSE( rig.player().isPlaying( "fade" ) );
        SW_EXPECT_EQUAL( size_t( 1 ), rig._log._listCommand.size() );

        SW_REQUIRE_TRUE( rig.player().tween( rig._pLeaf->getID(), "_opacity", "0", 5.0f ) );
        SW_EXPECT_NEAR( 0.0f, rig.opacity(), kTolerance );
    }
    {
        const ScopedMotion motion( false );
        SW_REQUIRE_TRUE( rig.player().play( "fade", 1.0f, 0 ) );
        rig.player().update( 0.1f );
    }
    {
        const ScopedMotion motion( true );
        rig.player().update( 0.1f );
        SW_EXPECT_NEAR( 1.0f, rig.opacity(), kTolerance );
        SW_EXPECT_FALSE( rig.player().isAnyPlaying() );
    }
}

/** @brief [UIAnimationPlayerTest] 잘못된 트랙은 건너뛴다 - 없는 위젯 · 없는 경로 · 읽지 못할 값 · 시간 순서가 틀린 키. 애니메이션은 나머지로 돈다 */
SW_TEST_CASE( UIAnimationPlayerTest, BadTracksAreSkipped )
{
    SW_EXPECT_LOG( "UIAnimationPlayer.AnimationNoWidgetNamed" );
    SW_EXPECT_LOG( "UIAnimationPlayer.AnimationTrackSkipped" );
    SW_EXPECT_LOG( "UIAnimationPlayer.AnimationKeyValueCannot" );
    SW_EXPECT_LOG( "UIAnimationPlayer.AnimationKeysNotTime" );
    const ScopedMotion     motion( false );
    AnimationRig           rig;
    UIAnimation            animation = AnimationRig::makeFade();
    const UIAnimationTrack good      = animation._listTrack[0];

    UIAnimationTrack noWidget   = good;
    noWidget._widget            = "gone";
    UIAnimationTrack noPath     = good;
    noPath._property            = "_nothing";
    UIAnimationTrack badValue   = good;
    badValue._listKey[0]._value = "abc";
    UIAnimationTrack badOrder   = good;
    badOrder._listKey[1]._time  = -1.0f;
    animation._listTrack        = { noWidget, noPath, badValue, badOrder, good };
    rig.player().setAnimations( { animation } );

    SW_REQUIRE_TRUE( rig.player().play( "fade" ) );
    rig.player().update( 0.5f );
    SW_EXPECT_NEAR( 0.5f, rig.opacity(), kTolerance );
}
