// HapticEffectTest — 진동 효과 데이터의 읽기 · 검사 · 보간과, 재생이 출력에 내는 값과 끝에서 멈추는 것(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Feedback/HapticEffect.h"
#include "Engine/Input/Feedback/HapticPlayback.h"
#include "Engine/Input/Feedback/IInputFeedbackDevice.h"

using namespace sw;

namespace
{
    /** @brief 받은 값을 기록하는 출력입니다. */
    class RecordingFeedbackDevice final : public IInputFeedbackDevice
    {
    public:
        float32 _low{ 0.0f };
        float32 _high{ 0.0f };
        uint32  _setCount{ 0 };
        uint32  _stopCount{ 0 };

    public:
        bool setVibration( float32 lowFrequency, float32 highFrequency ) override
        {
            _low  = lowFrequency;
            _high = highFrequency;
            ++_setCount;
            return true;
        }
        void stopVibration() override
        {
            _low  = 0.0f;
            _high = 0.0f;
            ++_stopCount;
        }
    };

    constexpr const utf8* kRampXML = R"(<HapticEffectDef>
    <_listKey>
        <HapticKeyDef _time="0" _low="1" _high="0" />
        <HapticKeyDef _time="0.5" _low="0" _high="1" />
    </_listKey>
</HapticEffectDef>)";
} // namespace

SW_TEST_CASE( HapticEffectTest, InterpolatesBetweenKeysAndHoldsTheEnd )
{
    HapticEffectDef effect;
    SW_EXPECT_TRUE( effect.loadFromXMLText( kRampXML ) );
    SW_EXPECT_NEAR( effect.getDuration(), 0.5f, 1.0e-6f );
    const float2 middle = effect.evaluate( 0.25f );
    SW_EXPECT_NEAR( middle._x, 0.5f, 1.0e-5f );
    SW_EXPECT_NEAR( middle._y, 0.5f, 1.0e-5f );
    const float2 after = effect.evaluate( 2.0f );
    SW_EXPECT_NEAR( after._x, 0.0f, 1.0e-6f );
    SW_EXPECT_NEAR( after._y, 1.0f, 1.0e-6f );
}

SW_TEST_CASE( HapticEffectTest, RejectsKeysThatDoNotStartAtZeroOrGoBackInTime )
{
    SW_EXPECT_LOG( "HapticEffect.FirstKeyNotZero" );
    SW_EXPECT_LOG( "HapticEffect.BadKey" );
    SW_EXPECT_LOG( "HapticEffect.LoopWithoutLength" );
    HapticEffectDef effect;
    SW_EXPECT_FALSE( effect.loadFromXMLText( R"(<HapticEffectDef><_listKey><HapticKeyDef _time="0.1" _low="1" _high="1" /></_listKey></HapticEffectDef>)" ) );
    SW_EXPECT_FALSE( effect.loadFromXMLText( R"(<HapticEffectDef><_listKey><HapticKeyDef _time="0" _low="1" _high="1" /><HapticKeyDef _time="0" _low="0" _high="0" /></_listKey></HapticEffectDef>)" ) );
    SW_EXPECT_FALSE( effect.loadFromXMLText( R"(<HapticEffectDef _bLoop="true"><_listKey><HapticKeyDef _time="0" _low="1" _high="1" /></_listKey></HapticEffectDef>)" ) );
}

SW_TEST_CASE( HapticEffectTest, PlaybackSendsScaledValuesAndStopsAtTheEnd )
{
    shared_ptr<HapticEffectDef> pEffect = make_shared<HapticEffectDef>();
    SW_EXPECT_TRUE( pEffect->loadFromXMLText( kRampXML ) );

    RecordingFeedbackDevice output;
    HapticPlayback          playback;
    SW_EXPECT_TRUE( playback.start( pEffect, 0.5f, output ) );
    SW_EXPECT_NEAR( output._low, 0.5f, 1.0e-5f );
    SW_EXPECT_NEAR( output._high, 0.0f, 1.0e-5f );

    playback.advance( 0.25f, output );
    SW_EXPECT_NEAR( output._low, 0.25f, 1.0e-5f );
    SW_EXPECT_NEAR( output._high, 0.25f, 1.0e-5f );
    SW_EXPECT_TRUE( playback.isPlaying() );

    playback.advance( 0.5f, output );
    SW_EXPECT_FALSE( playback.isPlaying() );
    SW_EXPECT_EQUAL( output._stopCount, 1u );
}

SW_TEST_CASE( HapticEffectTest, ConstantEffectIsSentOnce )
{
    RecordingFeedbackDevice output;
    HapticPlayback          playback;
    SW_EXPECT_TRUE( playback.start( HapticEffectDef::makeConstant( 0.3f, 0.7f, 1.0f ), 1.0f, output ) );
    playback.advance( 0.1f, output );
    playback.advance( 0.1f, output );
    SW_EXPECT_EQUAL( output._setCount, 1u );
    SW_EXPECT_LOG( "HapticPlayback.NoEffect" );
    SW_EXPECT_FALSE( playback.start( nullptr, 1.0f, output ) );
}
