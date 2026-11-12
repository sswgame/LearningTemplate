#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Console/DebugHUDRegistry.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickCircuit.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickCircuitDef.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickNodeRegistry.h"

using namespace sw;

namespace
{
    /** @brief 회로 규칙만 보는 작은 종류들입니다. 게임 쪽 종류와 섞이지 않게 테스트 등록표에만 둡니다. */
    struct GimmickTestKinds
    {
        // Sensor: 센서 값이 문턱 이상이면 켠다. 충격은 상태 0 에 쌓는다.
        static void stepSensor( GimmickNodeContext& context )
        {
            context._pFloatState[0] += context._sensorImpulse;
            context.setOutput( 0, context._sensorValue >= context.getNumber( 0 ) );
        }
        // And: 두 입력이 모두 켜지면 켠다. 연결이 안 된 입력은 켠 것으로 본다.
        static void stepAnd( GimmickNodeContext& context ) { context.setOutput( 0, context.isInputHigh( 0, true ) && context.isInputHigh( 1, true ) ); }
        // Count: 입력이 오를 때마다 정수 상태를 올린다.
        static void stepCount( GimmickNodeContext& context )
        {
            if ( context.isRising( 0 ) )
                ++context._pIntState[0];
            context._pFloatState[0] = static_cast<float32>( context._pIntState[0] );
        }
        // Delay: 지난 스텝의 입력을 낸다. 고리를 끊는다.
        static void stepDelay( GimmickNodeContext& context ) { context.setOutput( 0, context.wasInput( 0 ) ); }
        static void commitDelay( GimmickNodeContext& context ) { (void)context; }
        // Ranged: 매개변수가 범위 밖이면 초기화에서 거절한다.
        static bool initRanged( GimmickNodeContext& context )
        {
            if ( context.getNumber( 0 ) >= 0.0f )
                return true;
            *context._pError = "amount must not be negative";
            return false;
        }
        static void stepNothing( GimmickNodeContext& context ) { (void)context; }

        static void registerAll( GimmickNodeRegistry& registry )
        {
            GimmickNodeKind sensor;
            sensor._name       = hashed_string( "Sensor" );
            sensor._category   = GimmickNodeCategory::Sensor;
            sensor._listOutput = { hashed_string( "Out" ) };
            GimmickParamSpec threshold;
            threshold._name         = hashed_string( "Threshold" );
            threshold._defaultText  = "1";
            sensor._listParam       = { threshold };
            sensor._floatStateCount = 1;
            sensor._pStep           = &stepSensor;
            SW_REQUIRE_TRUE( registry.registerKind( sensor ) );

            GimmickNodeKind gate;
            gate._name       = hashed_string( "And" );
            gate._listInput  = { hashed_string( "A" ), hashed_string( "B" ) };
            gate._listOutput = { hashed_string( "Out" ) };
            gate._pStep      = &stepAnd;
            SW_REQUIRE_TRUE( registry.registerKind( gate ) );

            GimmickNodeKind count;
            count._name            = hashed_string( "Count" );
            count._category        = GimmickNodeCategory::Actuator;
            count._listInput       = { hashed_string( "In" ) };
            count._floatStateCount = 1;
            count._intStateCount   = 1;
            count._pStep           = &stepCount;
            SW_REQUIRE_TRUE( registry.registerKind( count ) );

            GimmickNodeKind delay;
            delay._name       = hashed_string( "Delay" );
            delay._listInput  = { hashed_string( "In" ) };
            delay._listOutput = { hashed_string( "Out" ) };
            delay._pStep      = &stepDelay;
            delay._pCommit    = &commitDelay;
            SW_REQUIRE_TRUE( registry.registerKind( delay ) );

            GimmickNodeKind ranged;
            ranged._name = hashed_string( "Ranged" );
            GimmickParamSpec amount;
            amount._name        = hashed_string( "Amount" );
            amount._defaultText = "1";
            GimmickParamSpec move;
            move._name        = hashed_string( "Move" );
            move._defaultText = "0 3 0";
            move._type        = GimmickParamType::Vector;
            GimmickParamSpec label;
            label._name         = hashed_string( "Label" );
            label._defaultText  = "door";
            label._type         = GimmickParamType::Text;
            ranged._listParam   = { amount, move, label };
            ranged._pInitialize = &initRanged;
            ranged._pStep       = &stepNothing;
            SW_REQUIRE_TRUE( registry.registerKind( ranged ) );
        }
    };

    /** @brief 센서 → And(B 는 비움) → 카운터 회로입니다. */
    struct GimmickFixture
    {
        GimmickNodeRegistry _registry;
        GimmickCircuitDef   _def;
        GimmickCircuit      _circuit;
        int32               _sensor{ -1 };
        int32               _gate{ -1 };
        int32               _count{ -1 };

        GimmickFixture()
        {
            GimmickTestKinds::registerAll( _registry );
            (void)_def.addNode( hashed_string( "s" ), hashed_string( "Sensor" ) );
            (void)_def.addNode( hashed_string( "g" ), hashed_string( "And" ) );
            (void)_def.addNode( hashed_string( "c" ), hashed_string( "Count" ) );
            SW_REQUIRE_TRUE( _def.addWire( "s.Out", "g.A" ) );
            SW_REQUIRE_TRUE( _def.addWire( "g.Out", "c.In" ) );
        }

        void build()
        {
            vector<string> listError;
            SW_REQUIRE_TRUE( _circuit.populate( _def, _registry, listError ) );
            _sensor = _circuit.findNode( hashed_string( "s" ) ).value();
            _gate   = _circuit.findNode( hashed_string( "g" ) ).value();
            _count  = _circuit.findNode( hashed_string( "c" ) ).value();
        }

        /** @brief 정의가 거절되는지와 오류 글에 @p pExpected 가 있는지 봅니다. */
        void expectRefused( const utf8* pExpected )
        {
            vector<string> listError;
            SW_EXPECT_FALSE( _circuit.populate( _def, _registry, listError ) );
            SW_EXPECT_FALSE( _circuit.isBuilt() );
            bool bFound = false;
            for ( const string& error : listError )
            {
                bFound = bFound || error.find( pExpected ) != string::npos;
            }
            SW_EXPECT_TRUE_MSG( bFound, pExpected );
        }
    };
} // namespace

// ---------------------------------------------------------------------- 만들기와 거절

SW_TEST_CASE( GimmickCircuitTest, SignalsFlowInOneStepInDependencyOrder )
{
    GimmickFixture rig;
    rig.build();
    SW_EXPECT_TRUE( rig._circuit.isBuilt() );
    SW_EXPECT_EQUAL( 3, rig._circuit.getNodeCount() );

    rig._circuit.setSensorValue( rig._sensor, 1.0f );
    rig._circuit.step();
    SW_EXPECT_TRUE( rig._circuit.getOutput( rig._sensor, hashed_string( "Out" ) ) );
    SW_EXPECT_TRUE( rig._circuit.getOutput( rig._gate, hashed_string( "Out" ) ) ); // 같은 스텝에 닿는다
    SW_EXPECT_EQUAL( 1, rig._circuit.getIntState( rig._count, 0 ) );
    SW_EXPECT_NEAR( 1.0f, rig._circuit.getActuatorValue( rig._count ), 1e-5f );

    rig._circuit.step(); // 이미 켜져 있어 오르지 않았다
    SW_EXPECT_EQUAL( 1, rig._circuit.getIntState( rig._count, 0 ) );
    rig._circuit.setSensorValue( rig._sensor, 0.0f );
    rig._circuit.step();
    rig._circuit.setSensorValue( rig._sensor, 1.0f );
    rig._circuit.step();
    SW_EXPECT_EQUAL( 2, rig._circuit.getIntState( rig._count, 0 ) );
    SW_EXPECT_EQUAL( 4u, rig._circuit.getStepIndex() );
}

// 런타임 디버거: 출력이 바뀐 스텝만 고정 용량 고리에 남고, 줄은 포트 이름으로 적힌다.
SW_TEST_CASE( GimmickCircuitTest, RecentTransitionsAreRecordedAndCapped )
{
    GimmickFixture rig;
    rig.build();
    rig._circuit.setSensorValue( rig._sensor, 1.0f );
    rig._circuit.step(); // s · g 가 켜진다
    rig._circuit.step(); // 바뀐 것 없음
    vector<GimmickOutputTransition> listTransition;
    rig._circuit.collectRecentTransitions( listTransition );
    SW_REQUIRE_TRUE( listTransition.size() >= 2 );
    SW_EXPECT_EQUAL( 0u, listTransition[0]._stepIndex );
    SW_EXPECT_EQUAL( 0u, listTransition[0]._beforeBits );
    SW_EXPECT_EQUAL( 1u, listTransition[0]._afterBits );

    for ( uint32 index = 0; index < GimmickCircuit::kTransitionCapacity * 2; ++index )
    {
        rig._circuit.setSensorValue( rig._sensor, ( index % 2 ) == 0 ? 0.0f : 1.0f );
        rig._circuit.step();
    }
    rig._circuit.collectRecentTransitions( listTransition );
    SW_EXPECT_EQUAL( size_t( GimmickCircuit::kTransitionCapacity ), listTransition.size() );

    DebugHUDSectionWriter writer;
    rig._circuit.writeDebugLines( writer, 64 );
    SW_REQUIRE_TRUE( writer.getLineCount() > 3 );
    SW_EXPECT_EQUAL( string( "step" ), writer.getLineAt( 0 )._label );
    SW_EXPECT_EQUAL( string( "s (Sensor)" ), writer.getLineAt( 1 )._label );
}

// 정의 순서가 평가 순서가 아니다.
SW_TEST_CASE( GimmickCircuitTest, DefinitionOrderDoesNotMatter )
{
    GimmickNodeRegistry registry;
    GimmickTestKinds::registerAll( registry );
    GimmickCircuitDef def;
    (void)def.addNode( hashed_string( "c" ), hashed_string( "Count" ) );
    (void)def.addNode( hashed_string( "g" ), hashed_string( "And" ) );
    (void)def.addNode( hashed_string( "s" ), hashed_string( "Sensor" ) );
    SW_REQUIRE_TRUE( def.addWire( "s.Out", "g.A" ) );
    SW_REQUIRE_TRUE( def.addWire( "g.Out", "c.In" ) );
    GimmickCircuit circuit;
    vector<string> listError;
    SW_REQUIRE_TRUE( circuit.populate( def, registry, listError ) );
    circuit.setSensorValue( circuit.findNode( hashed_string( "s" ) ).value(), 1.0f );
    circuit.step();
    SW_EXPECT_EQUAL( 1, circuit.getIntState( circuit.findNode( hashed_string( "c" ) ).value(), 0 ) );
}

SW_TEST_CASE( GimmickCircuitTest, InvertedWireFlipsTheSignal )
{
    GimmickNodeRegistry registry;
    GimmickTestKinds::registerAll( registry );
    GimmickCircuitDef def;
    (void)def.addNode( hashed_string( "s" ), hashed_string( "Sensor" ) );
    (void)def.addNode( hashed_string( "g" ), hashed_string( "And" ) );
    SW_REQUIRE_TRUE( def.addWire( "s.Out", "g.A", true ) );
    GimmickCircuit circuit;
    vector<string> listError;
    SW_REQUIRE_TRUE( circuit.populate( def, registry, listError ) );
    circuit.step();
    SW_EXPECT_TRUE( circuit.getOutput( 1, 0 ) );
    circuit.setSensorValue( 0, 5.0f );
    circuit.step();
    SW_EXPECT_FALSE( circuit.getOutput( 1, 0 ) );
}

/**
 * @brief [GimmickCircuitTest] 32 비트 이름 해시가 같은 두 노드 종류는 등록이 오류를 남기고 뒤의 것을 거절한다 — 회로 상태가 종류를 그 해시로 적는다
 */
SW_TEST_CASE( GimmickCircuitTest, NodeKindNamesSharingAHashAreRefused )
{
    GimmickNodeRegistry registry;
    GimmickNodeKind     kind;
    kind._name  = hashed_string( "ui.key1022789" );
    kind._pStep = &GimmickTestKinds::stepNothing;
    SW_REQUIRE_TRUE( registry.registerKind( kind ) );
    kind._name = hashed_string( "ui.key1239192" );
    SW_EXPECT_LOG( "Gimmick.NodeKindHashClash" );
    SW_EXPECT_FALSE( registry.registerKind( kind ) );
    SW_EXPECT_NULL( registry.findKind( hashed_string( "ui.key1239192" ) ) );
}

SW_TEST_CASE( GimmickCircuitTest, BrokenDefinitionsNameTheProblem )
{
    {
        GimmickFixture rig;
        (void)rig._def.addNode( hashed_string( "x" ), hashed_string( "NoSuchKind" ) );
        rig.expectRefused( "unknown node kind" );
    }
    {
        GimmickFixture rig;
        (void)rig._def.addNode( hashed_string( "s" ), hashed_string( "Sensor" ) );
        rig.expectRefused( "duplicate node id" );
    }
    {
        // 대소문자 무시 FNV-1a 32 비트가 같은 두 id 다 — 저장 상태가 노드를 그 해시로 짝지으니 만들기가 거절한다.
        GimmickFixture rig;
        (void)rig._def.addNode( hashed_string( "ui.key1022789" ), hashed_string( "Sensor" ) );
        (void)rig._def.addNode( hashed_string( "ui.key1239192" ), hashed_string( "Sensor" ) );
        rig.expectRefused( "share a 32-bit name hash" );
    }
    {
        GimmickFixture rig;
        (void)rig._def.addNode( hashed_string{}, hashed_string( "Sensor" ) );
        rig.expectRefused( "has no id" );
    }
    {
        GimmickFixture rig;
        SW_REQUIRE_TRUE( rig._def.addWire( "nobody.Out", "g.B" ) );
        rig.expectRefused( "unknown source node" );
    }
    {
        GimmickFixture rig;
        SW_REQUIRE_TRUE( rig._def.addWire( "s.Nope", "g.B" ) );
        rig.expectRefused( "unknown output" );
    }
    {
        GimmickFixture rig;
        SW_REQUIRE_TRUE( rig._def.addWire( "s.Out", "g.Nope" ) );
        rig.expectRefused( "unknown input" );
    }
}

SW_TEST_CASE( GimmickCircuitTest, ParametersAreCheckedAgainstTheKind )
{
    {
        GimmickFixture  rig;
        GimmickNodeDef& node = rig._def.addNode( hashed_string( "r" ), hashed_string( "Ranged" ) );
        node.setParam( hashed_string( "Nope" ), "1" );
        rig.expectRefused( "unknown parameter" );
    }
    {
        GimmickFixture  rig;
        GimmickNodeDef& node = rig._def.addNode( hashed_string( "r" ), hashed_string( "Ranged" ) );
        node.setParam( hashed_string( "Amount" ), "oops" );
        rig.expectRefused( "is not a number" );
    }
    {
        GimmickFixture  rig;
        GimmickNodeDef& node = rig._def.addNode( hashed_string( "r" ), hashed_string( "Ranged" ) );
        node.setParam( hashed_string( "Amount" ), "-1" );
        rig.expectRefused( "amount must not be negative" ); // 초기화의 이유가 그대로 나온다
    }
}

SW_TEST_CASE( GimmickCircuitTest, ParameterValuesAreRead )
{
    GimmickFixture  rig;
    GimmickNodeDef& node = rig._def.addNode( hashed_string( "r" ), hashed_string( "Ranged" ) );
    node.setParam( hashed_string( "Amount" ), "2.5" );
    rig.build();
    const int32                 ranged = rig._circuit.findNode( hashed_string( "r" ) ).value();
    const sw::optional<float32> amount = rig._circuit.readParamNumber( ranged, "Amount" );
    SW_REQUIRE_TRUE( amount.has_value() );
    SW_EXPECT_NEAR( 2.5f, *amount, 1e-5f );
    SW_EXPECT_NEAR( 3.0f, rig._circuit.readParamVector( ranged, "Move" )._y, 1e-5f ); // 기본값
    SW_EXPECT_TRUE( *rig._circuit.readParamText( ranged, "Label" ) == "door" );
    SW_EXPECT_FALSE( rig._circuit.readParamNumber( ranged, "Nope" ).has_value() );
    SW_EXPECT_TRUE( rig._circuit.readParamText( ranged, "Nope" ) == nullptr );
}

// 지연 없는 고리는 값이 정해지지 않는다.
SW_TEST_CASE( GimmickCircuitTest, LoopWithoutADelayIsRefusedWithItsNodes )
{
    GimmickFixture rig;
    (void)rig._def.addNode( hashed_string( "h" ), hashed_string( "And" ) );
    SW_REQUIRE_TRUE( rig._def.addWire( "g.Out", "h.A" ) );
    SW_REQUIRE_TRUE( rig._def.addWire( "h.Out", "g.B" ) );
    rig.expectRefused( "signal loop without a Delay" );
}

// 지연이 고리를 끊고, 신호는 한 바퀴에 한 스텝씩 돈다.
SW_TEST_CASE( GimmickCircuitTest, DelayBreaksTheLoopAndHoldsAStep )
{
    GimmickFixture rig;
    (void)rig._def.addNode( hashed_string( "d" ), hashed_string( "Delay" ) );
    SW_REQUIRE_TRUE( rig._def.addWire( "g.Out", "d.In" ) );
    SW_REQUIRE_TRUE( rig._def.addWire( "d.Out", "g.B", true ) ); // 지연된 자기 부정 — 켰다 껐다 한다
    rig.build();
    rig._circuit.setSensorValue( rig._sensor, 1.0f );
    vector<uint8> listGate;
    for ( int32 index = 0; index < 6; ++index )
    {
        rig._circuit.step();
        listGate.push_back( rig._circuit.getOutput( rig._gate, 0 ) ? 1 : 0 );
    }
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listGate[0] ) );
    int32 transitionCount = 0;
    for ( size_t index = 1; index < listGate.size(); ++index )
    {
        transitionCount += listGate[index] != listGate[index - 1] ? 1 : 0;
    }
    SW_EXPECT_TRUE( transitionCount >= 2 );
}

// ---------------------------------------------------------------------- 시간과 입력

SW_TEST_CASE( GimmickCircuitTest, ImpulsesLastOneStep )
{
    GimmickFixture rig;
    rig.build();
    rig._circuit.addSensorImpulse( rig._sensor, 2.0f );
    rig._circuit.addSensorImpulse( rig._sensor, 3.0f );
    rig._circuit.step();
    SW_EXPECT_NEAR( 5.0f, rig._circuit.getFloatState( rig._sensor, 0 ), 1e-5f );
    rig._circuit.step();
    SW_EXPECT_NEAR( 5.0f, rig._circuit.getFloatState( rig._sensor, 0 ), 1e-5f );
    SW_EXPECT_NEAR( 0.0f, rig._circuit.getFloatState( rig._sensor, 9 ), 1e-5f ); // 없는 칸
    SW_EXPECT_EQUAL( 0, rig._circuit.getIntState( rig._sensor, 0 ) );
}

SW_TEST_CASE( GimmickCircuitTest, UpdateConsumesFixedSteps )
{
    GimmickFixture rig;
    rig.build();
    const float32 stepTime = rig._circuit.getStepTime();
    SW_EXPECT_EQUAL( 0, rig._circuit.update( stepTime * 0.5f ) );
    SW_EXPECT_EQUAL( 1, rig._circuit.update( stepTime * 0.6f ) );
    SW_EXPECT_EQUAL( 2, rig._circuit.update( stepTime * 2.0f ) );
    SW_EXPECT_EQUAL( 3u, rig._circuit.getStepIndex() );
}

SW_TEST_CASE( GimmickCircuitTest, UnbuiltCircuitDoesNothing )
{
    GimmickCircuit circuit;
    SW_EXPECT_FALSE( circuit.isBuilt() );
    SW_EXPECT_EQUAL( 0, circuit.update( 10.0f ) );
    SW_EXPECT_EQUAL( 0, circuit.consumeTime( 10.0f ) );
    circuit.resetToInitial();
    circuit.setSensorValue( 0, 1.0f );
    circuit.addSensorImpulse( 0, 1.0f );
    circuit.setPathLength( 5, 1.0f );
    SW_EXPECT_FALSE( circuit.findNode( hashed_string( "x" ) ).has_value() );
}

SW_TEST_CASE( GimmickCircuitTest, ResetReturnsToTheInitialState )
{
    GimmickFixture rig;
    rig.build();
    const uint64 initialHash = rig._circuit.computeStateHash();
    rig._circuit.setSensorValue( rig._sensor, 1.0f );
    rig._circuit.step();
    SW_EXPECT_NOT_EQUAL( initialHash, rig._circuit.computeStateHash() );
    rig._circuit.resetToInitial();
    SW_EXPECT_EQUAL( 0u, rig._circuit.getStepIndex() );
    SW_EXPECT_EQUAL( 0, rig._circuit.getIntState( rig._count, 0 ) );
    SW_EXPECT_EQUAL( initialHash, rig._circuit.computeStateHash() );
}

// 같은 입력은 같은 상태다 — 재접속 · 저장 복원의 근거다.
SW_TEST_CASE( GimmickCircuitTest, SameInputsSameHash )
{
    GimmickFixture first;
    GimmickFixture second;
    first.build();
    second.build();
    for ( int32 step = 0; step < 8; ++step )
    {
        const float32 value = step % 3 == 0 ? 1.0f : 0.0f;
        first._circuit.setSensorValue( first._sensor, value );
        second._circuit.setSensorValue( second._sensor, value );
        first._circuit.step();
        second._circuit.step();
        SW_EXPECT_EQUAL( first._circuit.computeStateHash(), second._circuit.computeStateHash() );
    }
}

// ---------------------------------------------------------------------- 상태 저장

SW_TEST_CASE( GimmickCircuitTest, SavedStateRestoresExactly )
{
    GimmickFixture rig;
    rig.build();
    rig._circuit.setSensorValue( rig._sensor, 1.0f );
    rig._circuit.step();
    GimmickCircuitState saved;
    rig._circuit.saveState( saved );
    const uint64 savedHash = rig._circuit.computeStateHash();

    rig._circuit.setSensorValue( rig._sensor, 0.0f );
    rig._circuit.step();
    rig._circuit.step();
    const GimmickStateLoad result = rig._circuit.loadState( saved, GimmickStateMatch::Exact );
    SW_EXPECT_TRUE( result._bApplied );
    SW_EXPECT_TRUE( result._match == GimmickStateMatch::Exact );
    SW_EXPECT_EQUAL( savedHash, rig._circuit.computeStateHash() );
    SW_EXPECT_EQUAL( 1u, rig._circuit.getStepIndex() );
}

// 노드를 더한 판으로 옛 저장을 읽으면, 허락할 때만 짝 맞는 노드를 옮기고 새 노드는 처음 상태다.
SW_TEST_CASE( GimmickCircuitTest, AddedNodeNeedsPermissionToLoad )
{
    GimmickFixture older;
    older.build();
    older._circuit.setSensorValue( older._sensor, 1.0f );
    older._circuit.step();
    GimmickCircuitState saved;
    older._circuit.saveState( saved );

    GimmickFixture newer;
    (void)newer._def.addNode( hashed_string( "extra" ), hashed_string( "Count" ) );
    newer.build();
    const GimmickStateLoad refused = newer._circuit.loadState( saved, GimmickStateMatch::Exact );
    SW_EXPECT_TRUE( refused._match == GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_FALSE( refused._bApplied );
    SW_EXPECT_EQUAL( 0, newer._circuit.getIntState( newer._count, 0 ) ); // 그대로다

    const GimmickStateLoad applied = newer._circuit.loadState( saved, GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_TRUE( applied._bApplied );
    SW_EXPECT_EQUAL( 1, newer._circuit.getIntState( newer._count, 0 ) );
    SW_EXPECT_EQUAL( 0, newer._circuit.getIntState( newer._circuit.findNode( hashed_string( "extra" ) ).value(), 0 ) );
}

// 같은 id 인데 종류가 다르면 어떤 허락으로도 옮기지 않고 그 노드를 알려 준다.
SW_TEST_CASE( GimmickCircuitTest, ChangedNodeKindIsAMismatch )
{
    GimmickFixture older;
    older.build();
    GimmickCircuitState saved;
    older._circuit.saveState( saved );

    GimmickNodeRegistry registry;
    GimmickTestKinds::registerAll( registry );
    GimmickCircuitDef def;
    (void)def.addNode( hashed_string( "s" ), hashed_string( "Sensor" ) );
    (void)def.addNode( hashed_string( "g" ), hashed_string( "Sensor" ) ); // 종류를 바꿨다
    (void)def.addNode( hashed_string( "c" ), hashed_string( "Count" ) );
    GimmickCircuit circuit;
    vector<string> listError;
    SW_REQUIRE_TRUE( circuit.populate( def, registry, listError ) );
    const GimmickStateLoad result = circuit.loadState( saved, GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_TRUE( result._match == GimmickStateMatch::Mismatch );
    SW_EXPECT_FALSE( result._bApplied );
    SW_EXPECT_TRUE( result._changedNode == circuit.findNode( hashed_string( "g" ) ) );
}

SW_TEST_CASE( GimmickCircuitTest, DuplicateSavedIDIsAMismatch )
{
    GimmickFixture rig;
    rig.build();
    GimmickCircuitState saved;
    rig._circuit.saveState( saved );
    saved._listNode.push_back( saved._listNode[0] );
    const GimmickStateLoad result = rig._circuit.loadState( saved, GimmickStateMatch::NodeSetChanged );
    SW_EXPECT_TRUE( result._match == GimmickStateMatch::Mismatch );
    SW_EXPECT_FALSE( result._changedNode.has_value() );
    SW_EXPECT_FALSE( result._bApplied );
}

// ---------------------------------------------------------------------- 등록표 · 정의

SW_TEST_CASE( GimmickCircuitTest, ValidateBuildsNothing )
{
    GimmickFixture rig;
    vector<string> listError;
    SW_EXPECT_TRUE( GimmickCircuit::validate( rig._def, rig._registry, listError ) );
    SW_EXPECT_TRUE( listError.empty() );
    SW_EXPECT_FALSE( rig._circuit.isBuilt() );
}

SW_TEST_CASE( GimmickCircuitDefTest, WiresAreNodeDotPort )
{
    GimmickCircuitDef def;
    SW_EXPECT_TRUE( def.addWire( "a.Out", "b.In" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, def.getWires().size() );
    SW_EXPECT_TRUE( def.getWires()[0]._fromPort == hashed_string( "Out" ) );
    SW_EXPECT_FALSE( def.addWire( "noPort", "b.In" ) );
    SW_EXPECT_FALSE( def.addWire( "a.Out", "noPort" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, def.getWires().size() );
}

SW_TEST_CASE( GimmickCircuitDefTest, SetParamReplacesAndFindReads )
{
    GimmickNodeDef node;
    node.setParam( hashed_string( "A" ), "1" );
    node.setParam( hashed_string( "A" ), "2" );
    SW_EXPECT_EQUAL( size_t{ 1 }, node._listParam.size() );
    SW_EXPECT_TRUE( *node.findParam( hashed_string( "A" ) ) == "2" );
    SW_EXPECT_TRUE( node.findParam( hashed_string( "B" ) ) == nullptr );
}
