// 기믹 회로 — 센서 → 연산자 → 액추에이터 배선, 검증, 고정 스텝 타이밍, 상태 저장 · 복원(노드 id 짝짓기) · 결정성, 상태 상한, 스플라인 무버.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuit.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitDef.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickNodeRegistry.h"
#include "GameFramework/Base/World/Spline/SplinePath.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct GimmickTestInternal
    {
        static bool buildFromXML( GimmickCircuit& outCircuit, const utf8* pXML, vector<string>& outListError )
        {
            GimmickCircuitDef def;
            if ( def.loadFromXMLText( pXML, "test.gimmick.xml" ) == false )
                return false;
            return outCircuit.populate( def, GimmickNodeRegistry::getDefault(), outListError );
        }

        static bool buildFromXML( GimmickCircuit& outCircuit, const utf8* pXML )
        {
            vector<string> listError;
            return buildFromXML( outCircuit, pXML, listError );
        }

        static bool hasErrorContaining( const vector<string>& listError, const utf8* pText )
        {
            for ( const string& error : listError )
            {
                if ( error.find( pText ) != string::npos )
                    return true;
            }
            return false;
        }

        static string joinErrors( const vector<string>& listError )
        {
            string joined;
            for ( const string& error : listError )
            {
                joined += "\n  " + error;
            }
            return joined;
        }

        /** @brief 두 입력 센서(Signal a · b)를 연산자 하나에 물린 회로입니다. */
        static string makeBinaryOperatorXML( const utf8* pKind )
        {
            return string( "<GimmickCircuit><Node id=\"a\" kind=\"Signal\"/><Node id=\"b\" kind=\"Signal\"/><Node id=\"op\" kind=\"" ) + pKind +
                   "\"/><Wire from=\"a.Active\" to=\"op.A\"/><Wire from=\"b.Active\" to=\"op.B\"/></GimmickCircuit>";
        }

        /** @brief 두 입력 연산자의 진리표 넷(00 01 10 11)을 비트로 돌려줍니다(비트 i = 입력 i 의 결과). */
        static uint32 computeTruthTable( const utf8* pKind )
        {
            GimmickCircuit circuit;
            if ( buildFromXML( circuit, makeBinaryOperatorXML( pKind ).c_str() ) == false )
                return 0xFFu;
            const int32 a      = circuit.findNode( "a" );
            const int32 b      = circuit.findNode( "b" );
            const int32 op     = circuit.findNode( "op" );
            uint32      result = 0;
            for ( uint32 row = 0; row < 4; ++row )
            {
                circuit.setSensorValue( a, ( row & 1u ) != 0 ? 1.0f : 0.0f );
                circuit.setSensorValue( b, ( row & 2u ) != 0 ? 1.0f : 0.0f );
                circuit.step();
                if ( circuit.getOutput( op, hashed_string( "Out" ) ) )
                    result |= 1u << row;
            }
            return result;
        }

        static void stepTimes( GimmickCircuit& circuit, int32 count )
        {
            for ( int32 stepIndex = 0; stepIndex < count; ++stepIndex )
            {
                circuit.step();
            }
        }
    };
} // namespace

/**
 * @brief [GimmickTest] 연산자 진리표 — And · Or · Xor 의 네 줄, Not 은 한 입력, 배선의 invert 는 Not 과 같다
 */
SW_TEST_CASE( GimmickTest, OperatorTruthTables )
{
    // 비트 i = (a = i&1, b = i&2) 줄의 결과
    SW_EXPECT_EQUAL( 0x8u, GimmickTestInternal::computeTruthTable( "And" ) ); // 11 만
    SW_EXPECT_EQUAL( 0xEu, GimmickTestInternal::computeTruthTable( "Or" ) );  // 01 10 11
    SW_EXPECT_EQUAL( 0x6u, GimmickTestInternal::computeTruthTable( "Xor" ) ); // 01 10

    GimmickCircuit circuit;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( circuit, R"(
<GimmickCircuit>
  <Node id="a" kind="Signal"/>
  <Node id="not" kind="Not"/>
  <Node id="inverted" kind="Or"/>
  <Node id="alone" kind="And"/>
  <Wire from="a.Active" to="not.In"/>
  <Wire from="a.Active" to="inverted.A" invert="true"/>
</GimmickCircuit>)" ) );
    const int32 a        = circuit.findNode( "a" );
    const int32 notNode  = circuit.findNode( "not" );
    const int32 inverted = circuit.findNode( "inverted" );
    const int32 alone    = circuit.findNode( "alone" );
    circuit.step();
    SW_EXPECT_TRUE( circuit.getOutput( notNode, hashed_string( "Out" ) ) );
    SW_EXPECT_TRUE( circuit.getOutput( inverted, hashed_string( "Out" ) ) );
    SW_EXPECT_FALSE( circuit.getOutput( alone, hashed_string( "Out" ) ) ); // 연결이 없는 And 는 거짓
    circuit.setSensorValue( a, 1.0f );
    circuit.step();
    SW_EXPECT_FALSE( circuit.getOutput( notNode, hashed_string( "Out" ) ) );
    SW_EXPECT_FALSE( circuit.getOutput( inverted, hashed_string( "Out" ) ) );
}

/**
 * @brief [GimmickTest] 상태 연산자 — Latch(Set 남김 · Reset 우선), Toggle(오름마다), Counter(목표 · 내림 · Reset), Sequence(순서 · 실패 · 남음)
 */
SW_TEST_CASE( GimmickTest, LatchToggleCounterSequence )
{
    GimmickCircuit circuit;
    vector<string> listError;
    SW_ASSERT_TRUE_MSG( GimmickTestInternal::buildFromXML( circuit, R"(
<GimmickCircuit>
  <Node id="set" kind="Signal"/>
  <Node id="reset" kind="Signal"/>
  <Node id="latch" kind="Latch"/>
  <Node id="toggle" kind="Toggle"/>
  <Node id="counter" kind="Counter" target="3"/>
  <Node id="seq" kind="Sequence" count="3"/>
  <Node id="s0" kind="Signal"/>
  <Node id="s1" kind="Signal"/>
  <Node id="s2" kind="Signal"/>
  <Wire from="set.Active" to="latch.Set"/>
  <Wire from="reset.Active" to="latch.Reset"/>
  <Wire from="set.Active" to="toggle.In"/>
  <Wire from="set.Active" to="counter.Count"/>
  <Wire from="reset.Active" to="counter.Reset"/>
  <Wire from="s0.Active" to="seq.In0"/>
  <Wire from="s1.Active" to="seq.In1"/>
  <Wire from="s2.Active" to="seq.In2"/>
</GimmickCircuit>)",
                                                           listError ),
                        GimmickTestInternal::joinErrors( listError ).c_str() );
    const int32 set     = circuit.findNode( "set" );
    const int32 reset   = circuit.findNode( "reset" );
    const int32 latch   = circuit.findNode( "latch" );
    const int32 toggle  = circuit.findNode( "toggle" );
    const int32 counter = circuit.findNode( "counter" );

    // 펄스 한 번: Latch 는 남고 Toggle 은 뒤집히고 Counter 는 1.
    circuit.setSensorValue( set, 1.0f );
    circuit.step();
    circuit.setSensorValue( set, 0.0f );
    circuit.step();
    SW_EXPECT_TRUE( circuit.getOutput( latch, hashed_string( "Out" ) ) );
    SW_EXPECT_TRUE( circuit.getOutput( toggle, hashed_string( "Out" ) ) );
    SW_EXPECT_EQUAL( 1, circuit.getIntState( counter, 0 ) );
    // 두 번 더 → Counter 가 목표에 닿아 OnReached 는 한 걸음만.
    for ( int32 pulse = 0; pulse < 2; ++pulse )
    {
        circuit.setSensorValue( set, 1.0f );
        circuit.step();
        circuit.setSensorValue( set, 0.0f );
        if ( pulse == 1 )
            SW_EXPECT_TRUE( circuit.getOutput( counter, hashed_string( "OnReached" ) ) );
        circuit.step();
    }
    SW_EXPECT_TRUE( circuit.getOutput( counter, hashed_string( "Reached" ) ) );
    SW_EXPECT_FALSE( circuit.getOutput( counter, hashed_string( "OnReached" ) ) );
    SW_EXPECT_TRUE( circuit.getOutput( toggle, hashed_string( "Out" ) ) ); // 세 번 뒤집혔다
    // Set 과 Reset 이 함께면 Reset 이 이긴다.
    circuit.setSensorValue( set, 1.0f );
    circuit.setSensorValue( reset, 1.0f );
    circuit.step();
    SW_EXPECT_FALSE( circuit.getOutput( latch, hashed_string( "Out" ) ) );
    SW_EXPECT_FALSE( circuit.getOutput( counter, hashed_string( "Reached" ) ) );

    // Sequence — 0 1 2 는 끝, 0 2 는 실패 후 처음부터.
    const int32 seq        = circuit.findNode( "seq" );
    const int32 arrIn[3]   = { circuit.findNode( "s0" ), circuit.findNode( "s1" ), circuit.findNode( "s2" ) };
    const int32 arrPress[] = { 0, 2, 0, 1, 2 };
    bool        bFailed    = false;
    for ( const int32 press : arrPress )
    {
        circuit.setSensorValue( arrIn[press], 1.0f );
        circuit.step();
        bFailed = bFailed || circuit.getOutput( seq, hashed_string( "OnFail" ) );
        circuit.setSensorValue( arrIn[press], 0.0f );
        circuit.step();
    }
    SW_EXPECT_TRUE( bFailed );
    SW_EXPECT_TRUE( circuit.getOutput( seq, hashed_string( "Done" ) ) );
}

/**
 * @brief [GimmickTest] 시간 연산자 — Delay 는 오름 · 내림을 정확히 N 걸음 뒤에 옮기고, Pulse 는 정확히 N 걸음 켜지며, Timer 는 간격마다 한 번 운다
 * @details 60 Hz 에서 0.5 초 = 30 걸음. 걸음 수로 세므로 실수 누적 오차로 한 걸음 어긋나지 않는다.
 */
SW_TEST_CASE( GimmickTest, DelayPulseTimerUseFixedSteps )
{
    GimmickCircuit circuit;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( circuit, R"(
<GimmickCircuit stepTime="0.0166666667">
  <Node id="in" kind="Signal"/>
  <Node id="delay" kind="Delay" seconds="0.5"/>
  <Node id="pulse" kind="Pulse" seconds="0.25"/>
  <Node id="timer" kind="Timer" interval="0.1"/>
  <Wire from="in.Active" to="delay.In"/>
  <Wire from="in.Active" to="pulse.In"/>
</GimmickCircuit>)" ) );
    const int32 in    = circuit.findNode( "in" );
    const int32 delay = circuit.findNode( "delay" );
    const int32 pulse = circuit.findNode( "pulse" );
    const int32 timer = circuit.findNode( "timer" );

    circuit.setSensorValue( in, 1.0f );
    int32 riseStep  = -1;
    int32 pulseOn   = 0;
    int32 tickCount = 0;
    for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
    {
        if ( stepIndex == 10 )
            circuit.setSensorValue( in, 0.0f ); // 10 걸음 켜졌다 꺼진다
        circuit.step();
        if ( riseStep < 0 && circuit.getOutput( delay, hashed_string( "Out" ) ) )
            riseStep = stepIndex;
        pulseOn += circuit.getOutput( pulse, hashed_string( "Out" ) ) ? 1 : 0;
        tickCount += circuit.getOutput( timer, hashed_string( "OnTick" ) ) ? 1 : 0;
        if ( stepIndex == 39 )
            SW_EXPECT_TRUE( circuit.getOutput( delay, hashed_string( "Out" ) ) ); // 30 + 9 — 아직 켜짐
        if ( stepIndex == 40 )
            SW_EXPECT_FALSE( circuit.getOutput( delay, hashed_string( "Out" ) ) ); // 내림도 30 걸음 뒤
    }
    SW_EXPECT_EQUAL( 30, riseStep );
    SW_EXPECT_EQUAL( 15, pulseOn );   // 0.25 초 = 15 걸음
    SW_EXPECT_EQUAL( 10, tickCount ); // 60 걸음 / 6 걸음
}

/**
 * @brief [GimmickTest] 지연을 지나는 고리는 허용되어 진동기가 되고, 지연 없는 고리는 짓기 오류다
 */
SW_TEST_CASE( GimmickTest, DelayBreaksSignalLoops )
{
    GimmickCircuit oscillator;
    vector<string> listError;
    SW_ASSERT_TRUE_MSG( GimmickTestInternal::buildFromXML( oscillator, R"(
<GimmickCircuit>
  <Node id="not" kind="Not"/>
  <Node id="delay" kind="Delay" seconds="0.05"/>
  <Wire from="not.Out" to="delay.In"/>
  <Wire from="delay.Out" to="not.In"/>
</GimmickCircuit>)",
                                                           listError ),
                        GimmickTestInternal::joinErrors( listError ).c_str() );
    const int32 delay   = oscillator.findNode( "delay" );
    int32       toggles = 0;
    bool        bLast   = false;
    for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
    {
        oscillator.step();
        const bool bNow = oscillator.getOutput( delay, hashed_string( "Out" ) );
        toggles += bNow != bLast ? 1 : 0;
        bLast = bNow;
    }
    SW_EXPECT_EQUAL( 19, toggles ); // 3 걸음마다 바뀐다(3, 6, …, 57)

    GimmickCircuit loop;
    listError.clear();
    SW_EXPECT_FALSE( GimmickTestInternal::buildFromXML( loop, R"(
<GimmickCircuit>
  <Node id="a" kind="Or"/>
  <Node id="b" kind="Not"/>
  <Wire from="a.Out" to="b.In"/>
  <Wire from="b.Out" to="a.A"/>
</GimmickCircuit>)",
                                                        listError ) );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "signal loop without a Delay through nodes a, b" ),
                        GimmickTestInternal::joinErrors( listError ).c_str() );
}

/**
 * @brief [GimmickTest] 배선 검증 — 모르는 종류 · 대상 노드 · 출력 · 입력 · 매개변수, 숫자가 아닌 숫자, 겹친 id, 모르는 모드를 모두 한 번에 모아 거절한다
 */
SW_TEST_CASE( GimmickTest, WiringValidationReportsEveryError )
{
    GimmickCircuit circuit;
    vector<string> listError;
    {
        SW_TEST_DEFENSIVE_SCOPE( "a broken wire is reported" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
        SW_EXPECT_FALSE( GimmickTestInternal::buildFromXML( circuit, R"(
    <GimmickCircuit>
      <Node id="plate" kind="PressurePlate" threshold="heavy"/>
      <Node id="plate" kind="Volume"/>
      <Node id="door" kind="Door" opnTime="1"/>
      <Node id="ghost" kind="Teleporter"/>
      <Node id="mover" kind="Mover" mode="Sideways"/>
      <Wire from="plate.Pressed" to="gate.Open"/>
      <Wire from="plate.Squashed" to="door.Open"/>
      <Wire from="plate.Pressed" to="door.Slam"/>
    </GimmickCircuit>)",
                                                            listError ) );
    }
    const string joined = GimmickTestInternal::joinErrors( listError );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "parameter 'threshold' is not a number" ), joined.c_str() );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "duplicate node id" ), joined.c_str() );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "unknown parameter 'opnTime'" ), joined.c_str() );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "'ghost' (Teleporter): unknown node kind" ), joined.c_str() );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "mode must be Once, Loop or PingPong" ), joined.c_str() );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "unknown target node 'gate'" ), joined.c_str() );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "unknown output 'Squashed'" ), joined.c_str() );
    SW_EXPECT_TRUE_MSG( GimmickTestInternal::hasErrorContaining( listError, "unknown input 'Slam'" ), joined.c_str() );
    SW_EXPECT_FALSE( circuit.isBuilt() );

    // 형식 오류(점 없는 배선)는 읽기에서 거절한다.
    GimmickCircuitDef def;
    {
        SW_TEST_DEFENSIVE_SCOPE( "a wire without a port is rejected" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
        SW_EXPECT_FALSE( def.loadFromXMLText( "<GimmickCircuit><Wire from=\"plate\" to=\"door.Open\"/></GimmickCircuit>", "broken.gimmick.xml" ) );
    }
}

/**
 * @brief [GimmickTest] 눌림판 → 펄스 → 문 — 올라서면 문이 openTime 동안 열리고, 내려와도 펄스 동안은 열려 있다가 닫힌다(시간제 스위치)
 */
SW_TEST_CASE( GimmickTest, PressurePlateOpensDoorForPulse )
{
    GimmickCircuit circuit;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( circuit, R"(
<GimmickCircuit stepTime="0.1">
  <Node id="plate" kind="PressurePlate" threshold="50"/>
  <Node id="hold" kind="Pulse" seconds="2"/>
  <Node id="door" kind="Door" openTime="0.5" closeTime="1"/>
  <Wire from="plate.OnPress" to="hold.In"/>
  <Wire from="hold.Out" to="door.Open"/>
</GimmickCircuit>)" ) );
    const int32 plate = circuit.findNode( "plate" );
    const int32 door  = circuit.findNode( "door" );
    circuit.setSensorValue( plate, 20.0f ); // 가벼운 것은 누르지 못한다
    GimmickTestInternal::stepTimes( circuit, 5 );
    SW_EXPECT_TRUE( circuit.getOutput( door, hashed_string( "Closed" ) ) );
    circuit.setSensorValue( plate, 80.0f );
    GimmickTestInternal::stepTimes( circuit, 3 );
    SW_EXPECT_TRUE( circuit.getOutput( door, hashed_string( "Moving" ) ) );
    GimmickTestInternal::stepTimes( circuit, 3 ); // 0.5 초를 넘었다 — 다 열림
    SW_EXPECT_NEAR_EQUAL( 1.0f, circuit.getActuatorValue( door ), 1.0e-4f );
    SW_EXPECT_TRUE( circuit.getOutput( door, hashed_string( "Opened" ) ) );
    circuit.setSensorValue( plate, 0.0f );
    GimmickTestInternal::stepTimes( circuit, 13 ); // 펄스 20 걸음 중 19 걸음째 — 아직 열림
    SW_EXPECT_TRUE( circuit.getOutput( door, hashed_string( "Opened" ) ) );
    GimmickTestInternal::stepTimes( circuit, 12 ); // 닫히기 1 초
    SW_EXPECT_TRUE( circuit.getOutput( door, hashed_string( "Closed" ) ) );
}

/**
 * @brief [GimmickTest] 스플라인 무버 — 호 길이 L, 속도 v 면 정확히 L / v 초째 걸음에 끝에 닿는다(그 전 걸음은 아니다), 핑퐁은 돌아온다
 * @details 곡선(Catmull-Rom)의 호 길이로 재므로 조절점 사이 직선 길이와 다르다. 무버의 거리를 스플라인에 넣은 자리가 끝 조절점이다.
 */
SW_TEST_CASE( GimmickTest, MoverReachesEndAtArcLengthTime )
{
    SplinePath path;
    SW_ASSERT_TRUE( path.initialize( {
                                         float3{0.0f, 0.0f, 0.0f},
                                         float3{3.0f, 0.0f, 4.0f},
                                         float3{6.0f, 0.0f, 0.0f},
                                         float3{9.0f, 2.0f, 4.0f}
    },
                                     SplineType::CatmullRom, false, 32 ) );
    const float32 chordLength = 5.0f + 5.0f + float3::getDistance( float3{ 6.0f, 0.0f, 0.0f }, float3{ 9.0f, 2.0f, 4.0f } );
    SW_EXPECT_TRUE( path.getLength() > chordLength ); // 곡선은 현보다 길다

    GimmickCircuit circuit;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( circuit, R"(
<GimmickCircuit stepTime="0.02">
  <Node id="once" kind="Mover" speed="2" mode="Once"/>
  <Node id="pingpong" kind="Mover" speed="2" mode="PingPong"/>
</GimmickCircuit>)" ) );
    const int32 once     = circuit.findNode( "once" );
    const int32 pingpong = circuit.findNode( "pingpong" );
    circuit.setPathLength( once, path.getLength() );
    circuit.setPathLength( pingpong, path.getLength() );
    const float32 travelTime    = path.getLength() / 2.0f;
    const int32   expectedSteps = static_cast<int32>( travelTime / 0.02f ) + 1; // L / v 초를 처음 넘기는 걸음
    int32         arriveStep    = -1;
    for ( int32 stepIndex = 0; stepIndex < expectedSteps * 2 + 5; ++stepIndex )
    {
        circuit.step();
        if ( arriveStep < 0 && circuit.getOutput( once, hashed_string( "AtEnd" ) ) )
            arriveStep = stepIndex + 1;
    }
    SW_EXPECT_TRUE_MSG( static_cast<float32>( arriveStep ) * 0.02f >= travelTime && static_cast<float32>( arriveStep - 1 ) * 0.02f < travelTime,
                        ( std::to_string( arriveStep ) + " steps for " + std::to_string( travelTime ) + " s" ).c_str() );
    const SplineSample end = path.sampleAtDistance( circuit.getActuatorValue( once ) );
    SW_EXPECT_NEAR_EQUAL( 9.0f, end._position._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, end._position._y, 1.0e-3f );
    SW_EXPECT_TRUE( circuit.getOutput( pingpong, hashed_string( "AtStart" ) ) || circuit.getActuatorValue( pingpong ) < path.getLength() * 0.1f ); // 돌아왔다
}

/**
 * @brief [GimmickTest] 상태 저장 · 읽기 · 처음으로 — 저장한 바이트를 읽으면 같은 해시로 이어 가고, 다른 모양의 회로는 거절하며, 처음으로 돌리면 지은 직후다
 */
SW_TEST_CASE( GimmickTest, StateSaveLoadResetRoundTrip )
{
    const utf8*    pXML = R"(
<GimmickCircuit>
  <Node id="use" kind="Interaction"/>
  <Node id="counter" kind="Counter" target="2"/>
  <Node id="door" kind="Door" openTime="0.5"/>
  <Node id="delay" kind="Delay" seconds="0.2"/>
  <Wire from="use.OnUsed" to="counter.Count"/>
  <Wire from="counter.Reached" to="delay.In"/>
  <Wire from="delay.Out" to="door.Open"/>
</GimmickCircuit>)";
    GimmickCircuit circuit;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( circuit, pXML ) );
    const uint64 initialHash = circuit.computeStateHash();
    const int32  use         = circuit.findNode( "use" );
    circuit.addSensorImpulse( use, 1.0f );
    GimmickTestInternal::stepTimes( circuit, 3 );
    circuit.addSensorImpulse( use, 1.0f );
    GimmickTestInternal::stepTimes( circuit, 4 ); // 지연 큐 안에 바뀜이 남아 있는 때
    GimmickCircuitState checkpoint;
    circuit.saveState( checkpoint );
    const uint64 checkpointHash = circuit.computeStateHash();
    GimmickTestInternal::stepTimes( circuit, 40 );
    const uint64  finalHash  = circuit.computeStateHash();
    const float32 finalValue = circuit.getActuatorValue( circuit.findNode( "door" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, finalValue, 1.0e-4f );

    // 체크포인트로 돌아가 같은 걸음을 다시 — 같은 끝 해시.
    const GimmickStateLoad back = circuit.loadState( checkpoint, GimmickStateMatch::Exact );
    SW_ASSERT_TRUE( back._bApplied );
    SW_EXPECT_TRUE( back._match == GimmickStateMatch::Exact );
    SW_EXPECT_EQUAL( checkpointHash, circuit.computeStateHash() );
    GimmickTestInternal::stepTimes( circuit, 40 );
    SW_EXPECT_EQUAL( finalHash, circuit.computeStateHash() );

    // 새로 지은 회로에 읽어도 같다(레벨 세이브 → 다시 로드).
    GimmickCircuit reloaded;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( reloaded, pXML ) );
    SW_ASSERT_TRUE( reloaded.loadState( checkpoint, GimmickStateMatch::Exact )._bApplied );
    GimmickTestInternal::stepTimes( reloaded, 40 );
    SW_EXPECT_EQUAL( finalHash, reloaded.computeStateHash() );

    // 노드가 줄어든 회로 — 같은 빌드끼리 쓰는 한계(Exact)로는 거절하고 그대로 둔다.
    GimmickCircuit other;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( other, "<GimmickCircuit><Node id=\"use\" kind=\"Interaction\"/></GimmickCircuit>" ) );
    const uint64           otherHash = other.computeStateHash();
    const GimmickStateLoad refused   = other.loadState( checkpoint, GimmickStateMatch::Exact );
    SW_EXPECT_FALSE( refused._bApplied );
    SW_EXPECT_TRUE( refused._match == GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_EQUAL( otherHash, other.computeStateHash() );

    // 슬롯 하나가 모자란 상태(종류가 슬롯을 바꿨는데 버전을 올리지 않은 것과 같은 모양)는 어느 한계로도 받지 않는다.
    GimmickCircuitState shortSlot = checkpoint;
    const int32         delay     = circuit.findNode( "delay" );
    shortSlot._listNode[static_cast<size_t>( delay )]._listIntState.pop_back();
    const uint64           beforeHash = circuit.computeStateHash();
    const GimmickStateLoad broken     = circuit.loadState( shortSlot, GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_FALSE( broken._bApplied );
    SW_EXPECT_TRUE( broken._match == GimmickStateMatch::Mismatch );
    SW_EXPECT_EQUAL( delay, broken._changedNode );
    SW_EXPECT_EQUAL( beforeHash, circuit.computeStateHash() );

    circuit.resetToInitial();
    SW_EXPECT_EQUAL( initialHash, circuit.computeStateHash() );
    SW_EXPECT_EQUAL( 0u, circuit.getStepIndex() );
}

/**
 * @brief [GimmickTest] 결정성 — 같은 입력 열이면 프레임 시간을 어떻게 나눠 주든 같은 걸음 수 · 같은 상태 해시, 입력 하나가 다르면 다른 해시
 */
SW_TEST_CASE( GimmickTest, SameInputsSameStateHash )
{
    const utf8*    pXML = R"(
<GimmickCircuit stepTime="0.0166666667">
  <Node id="plate" kind="PressurePlate" threshold="1"/>
  <Node id="timer" kind="Timer" interval="0.3"/>
  <Node id="count" kind="Counter" target="4"/>
  <Node id="mover" kind="Mover" speed="1.5" mode="PingPong" curve="SmoothStep" length="7" pause="0.2"/>
  <Node id="hazard" kind="Hazard" onTime="0.4" offTime="0.6" phase="0.1"/>
  <Node id="rot" kind="Rotator" speed="45"/>
  <Node id="elevator" kind="Elevator" floors="0 3 6" speed="2"/>
  <Wire from="timer.OnTick" to="count.Count"/>
  <Wire from="plate.Pressed" to="mover.Reverse"/>
  <Wire from="count.Reached" to="elevator.Up"/>
  <Wire from="plate.OnPress" to="elevator.Call2"/>
  <Wire from="plate.OnPress" to="count.Reset"/>
</GimmickCircuit>)";
    GimmickCircuit runA;
    GimmickCircuit runB;
    GimmickCircuit runC;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( runA, pXML ) );
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( runB, pXML ) );
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( runC, pXML ) );
    const int32 plate = runA.findNode( "plate" );
    // A 는 걸음을 직접, B 는 들쭉날쭉한 프레임 시간으로 — 입력은 같은 걸음 번호에 넣는다.
    for ( int32 stepIndex = 0; stepIndex < 600; ++stepIndex )
    {
        const float32 weight = ( 100 <= stepIndex && stepIndex < 250 ) ? 2.0f : 0.0f;
        runA.setSensorValue( plate, weight );
        runC.setSensorValue( plate, stepIndex == 300 ? 2.0f : weight );
        runA.step();
        runC.step();
    }
    // 프레임 시간이 한 걸음보다 짧아 한 번의 update 가 낸 걸음은 많아야 하나다 — 그 걸음의 입력은 update 전 걸음 번호로 정한다.
    int32 frame = 0;
    while ( runB.getStepIndex() < 600 )
    {
        const float32 weight = ( 100 <= runB.getStepIndex() && runB.getStepIndex() < 250 ) ? 2.0f : 0.0f;
        runB.setSensorValue( plate, weight );
        (void)runB.update( ( frame % 3 == 0 ) ? 0.005f : 0.0123f );
        ++frame;
    }
    SW_EXPECT_EQUAL( 600u, runA.getStepIndex() );
    SW_EXPECT_EQUAL( runA.getStepIndex(), runB.getStepIndex() );
    GimmickCircuitState stateA;
    GimmickCircuitState stateB;
    runA.saveState( stateA );
    runB.saveState( stateB );
    // 누적 시간(프레임 분할의 흔적)만 다를 수 있다 — 그것을 빼고 같은 상태다.
    stateB._accumulator = stateA._accumulator;
    SW_EXPECT_TRUE( FixedLayout::equals( stateA, stateB ) );
    SW_EXPECT_NOT_EQUAL( runA.computeStateHash(), runC.computeStateHash() );
}

/**
 * @brief [GimmickTest] 노드 id 짝짓기 — 디자이너가 노드를 더하고 지운 회로에 옛 상태를 읽으면 남은 노드는 이어 가고, 새 노드는 처음 상태, 지운 노드는 버린다
 * @details 회로 전체 해시 하나로 거절하면 노드 하나만 고쳐도 그 회로의 옛 세이브가 모두 실패한다. 실패는 "같은 id 인데 종류가 바뀐 노드" 로 좁힌다.
 */
SW_TEST_CASE( GimmickTest, StateMatchesNodesByID )
{
    const utf8*    pOldXML = R"(
<GimmickCircuit>
  <Node id="use" kind="Interaction"/>
  <Node id="counter" kind="Counter" target="5"/>
  <Node id="gone" kind="Latch"/>
  <Wire from="use.OnUsed" to="counter.Count"/>
</GimmickCircuit>)";
    GimmickCircuit oldCircuit;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( oldCircuit, pOldXML ) );
    const int32 use = oldCircuit.findNode( "use" );
    for ( int32 press = 0; press < 3; ++press )
    {
        oldCircuit.addSensorImpulse( use, 1.0f );
        GimmickTestInternal::stepTimes( oldCircuit, 2 );
    }
    GimmickCircuitState saved;
    oldCircuit.saveState( saved );
    const int32 oldCount = oldCircuit.getIntState( oldCircuit.findNode( "counter" ), 0 );
    SW_EXPECT_EQUAL( 3, oldCount );

    // 순서를 바꾸고(counter 가 먼저), 노드 하나를 지우고(gone), 하나를 더한(door) 회로.
    const utf8*    pNewXML = R"(
<GimmickCircuit>
  <Node id="counter" kind="Counter" target="5"/>
  <Node id="use" kind="Interaction"/>
  <Node id="door" kind="Door" startOpen="1"/>
  <Wire from="use.OnUsed" to="counter.Count"/>
</GimmickCircuit>)";
    GimmickCircuit newCircuit;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( newCircuit, pNewXML ) );
    GimmickCircuit fresh;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( fresh, pNewXML ) );
    const int32 door = newCircuit.findNode( "door" );

    const GimmickStateLoad sameBuild = newCircuit.loadState( saved, GimmickStateMatch::Exact );
    SW_EXPECT_FALSE( sameBuild._bApplied ); // 롤백 · 넷은 같은 회로만 받는다

    const GimmickStateLoad edited = newCircuit.loadState( saved, GimmickStateMatch::NodeSetChanged );
    SW_ASSERT_TRUE( edited._bApplied );
    SW_EXPECT_TRUE( edited._match == GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_EQUAL( oldCount, newCircuit.getIntState( newCircuit.findNode( "counter" ), 0 ) );
    SW_EXPECT_EQUAL( oldCircuit.getStepIndex(), newCircuit.getStepIndex() );
    SW_EXPECT_NEAR_EQUAL( fresh.getActuatorValue( door ), newCircuit.getActuatorValue( door ), 1.0e-6f ); // 새 노드는 처음 상태(startOpen)
    SW_EXPECT_NEAR_EQUAL( 1.0f, newCircuit.getActuatorValue( door ), 1.0e-6f );

    // 같은 id 의 종류를 바꾸면(Counter → Toggle) 어느 한계로도 읽지 않고 그 노드를 알린다.
    GimmickCircuit retyped;
    SW_ASSERT_TRUE( GimmickTestInternal::buildFromXML( retyped, "<GimmickCircuit><Node id=\"use\" kind=\"Interaction\"/><Node id=\"counter\" kind=\"Toggle\"/></GimmickCircuit>" ) );
    const uint64           retypedHash = retyped.computeStateHash();
    const GimmickStateLoad changed     = retyped.loadState( saved, GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_FALSE( changed._bApplied );
    SW_EXPECT_TRUE( changed._match == GimmickStateMatch::Mismatch );
    SW_EXPECT_EQUAL( retyped.findNode( "counter" ), changed._changedNode );
    SW_EXPECT_EQUAL( retypedHash, retyped.computeStateHash() );

    // 종류의 상태 버전이 다른 저장(슬롯 수는 같다)도 같다.
    GimmickCircuitState oldVersion = saved;
    for ( GimmickNodeState& node : oldVersion._listNode )
    {
        if ( node._id == hashed_string( "counter" ).getHash() )
            ++node._stateVersion;
    }
    SW_EXPECT_TRUE( oldCircuit.loadState( oldVersion, GimmickStateMatch::NodeSetChanged )._match == GimmickStateMatch::Mismatch );

    // 같은 id 가 둘인 저장 상태는 깨진 것이다.
    GimmickCircuitState duplicated = saved;
    duplicated._listNode.push_back( duplicated._listNode[0] );
    const GimmickStateLoad twice = oldCircuit.loadState( duplicated, GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_TRUE( twice._match == GimmickStateMatch::Mismatch );
    SW_EXPECT_EQUAL( -1, twice._changedNode );
}

/**
 * @brief [GimmickTest] 상태 상한 — 회로 · 종류 등록이 상태가 담을 수 있는 수(`kMaxNodeCount` · `kMaxSlotCount`)를 넘는 것을 오류로 막는다
 * @details 상한의 기준은 그 상수이고 `PROPERTY( MaxCount = kMaxSlotCount )` 가 이름으로 가리키므로, 코덱의 상한과 어긋나는지는 대조하지 않는다.
 */
SW_TEST_CASE( GimmickTest, StateLimitsRejectOversizedCircuitAndKind )
{
    constexpr uint32 kSlot = GimmickNodeState::kMaxSlotCount;
    // 기본 종류는 모두 상한 안이다(Delay 의 정수 슬롯 20 개가 가장 크다).
    for ( const GimmickNodeKind& kind : GimmickNodeRegistry::getDefault().getKinds() )
    {
        SW_EXPECT_TRUE( kind._floatStateCount <= kSlot && kind._intStateCount <= kSlot );
    }
    GimmickNodeRegistry registry = GimmickNodeRegistry::getDefault();
    GimmickNodeKind     wide     = *registry.findKind( hashed_string( "Delay" ) );
    wide._name                   = hashed_string( "WideDelay" );
    wide._intStateCount          = static_cast<uint16>( kSlot + 1 );
    SW_EXPECT_FALSE( registry.registerKind( wide ) );
    wide._intStateCount = static_cast<uint16>( kSlot );
    SW_EXPECT_TRUE( registry.registerKind( wide ) );

    // 노드 수가 상한을 넘는 회로는 populate 가 한 줄 오류로 멈춘다.
    string xml = "<GimmickCircuit>";
    for ( uint32 node = 0; node <= GimmickCircuitState::kMaxNodeCount; ++node )
    {
        xml += "<Node id=\"n" + string( std::to_string( node ).c_str() ) + "\" kind=\"Signal\"/>";
    }
    xml += "</GimmickCircuit>";
    GimmickCircuit tooBig;
    vector<string> listError;
    SW_EXPECT_FALSE( GimmickTestInternal::buildFromXML( tooBig, xml.c_str(), listError ) );
    SW_EXPECT_EQUAL( size_t( 1 ), listError.size() );
    SW_EXPECT_TRUE( GimmickTestInternal::hasErrorContaining( listError, "a circuit state holds" ) );
    SW_EXPECT_FALSE( tooBig.isBuilt() );
}
