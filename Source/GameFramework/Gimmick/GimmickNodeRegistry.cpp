#include "pch.h"

#include "GameFramework/Gimmick/GimmickNodeRegistry.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Camera/CameraBlend.h"
#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    namespace
    {
        /** @brief 내장 노드 종류입니다. 포트 번호는 종류를 만들 때 적은 이름의 자리와 같습니다. */
        struct GimmickNodeRegistryInternal
        {
            static constexpr uint32 kMaxPortCount     = 32;
            static constexpr int32  kDelayCapacity    = 16;
            static constexpr int32  kDelayQueueStart  = 4;
            static constexpr int32  kMaxElevatorFloor = 8;

            enum class MoverMode : int32
            {
                Once = 0,
                Loop,
                PingPong
            };

            static GimmickParamSpec makeParam( const utf8* pName, const utf8* pDefault, GimmickParamType type = GimmickParamType::Number )
            {
                GimmickParamSpec spec;
                spec._name        = hashed_string( pName );
                spec._defaultText = string( pDefault );
                spec._type        = type;
                return spec;
            }

            static GimmickNodeKind makeKind( const utf8* pName, GimmickNodeCategory category, std::initializer_list<const utf8*> listInput,
                                             std::initializer_list<const utf8*> listOutput, std::initializer_list<GimmickParamSpec> listParam, uint16 floatCount,
                                             uint16 intCount, GimmickNodeKind::InitializeFunc pInitialize, GimmickNodeKind::StepFunc pStep,
                                             GimmickNodeKind::StepFunc pCommit = nullptr )
            {
                GimmickNodeKind kind;
                kind._name     = hashed_string( pName );
                kind._category = category;
                for ( const utf8* pPort : listInput )
                    kind._listInput.push_back( hashed_string( pPort ) );
                for ( const utf8* pPort : listOutput )
                    kind._listOutput.push_back( hashed_string( pPort ) );
                kind._listParam.assign( listParam.begin(), listParam.end() );
                kind._floatStateCount = floatCount;
                kind._intStateCount   = intCount;
                kind._pInitialize     = pInitialize;
                kind._pStep           = pStep;
                kind._pCommit         = pCommit;
                return kind;
            }

            static bool failParam( GimmickNodeContext& context, const utf8* pMessage )
            {
                if ( context._pError != nullptr )
                    *context._pError = pMessage;
                return false;
            }

            static GimmickNodeRegistry createDefaultRegistry()
            {
                GimmickNodeRegistry registry;
                GimmickNodeRegistry::registerBuiltinKinds( registry );
                return registry;
            }

            static uint32 countBits( uint32 value )
            {
                uint32 count = 0;
                for ( ; value != 0; value &= value - 1 )
                    ++count;
                return count;
            }

            // ------------------------------------------------------------------------------
            // 센서 — 바깥이 `_sensorValue`(수준) · `_sensorImpulse`(충격)를 넣는다
            // ------------------------------------------------------------------------------
            /** @brief Volume — 인원 수. 출력 Occupied · Empty · OnEnter · OnExit, 매개변수 minCount. 상태 int[0] = 지난 인원. */
            static void stepVolume( GimmickNodeContext& context )
            {
                const int32 count     = static_cast<int32>( context._sensorValue + 0.5f );
                const int32 previous  = context._pIntState[0];
                const bool  bOccupied = count >= MathUtil::max( 1, static_cast<int32>( context.getNumber( 0 ) ) );
                context.setOutput( 0, bOccupied );
                context.setOutput( 1, bOccupied == false );
                context.setOutput( 2, count > previous );
                context.setOutput( 3, count < previous );
                context._pIntState[0] = count;
            }

            /** @brief PressurePlate — 무게. 출력 Pressed · OnPress · OnRelease, 매개변수 threshold · latch. 상태 int[0] = 눌림. */
            static void stepPressurePlate( GimmickNodeContext& context )
            {
                const bool bWasPressed = context._pIntState[0] != 0;
                const bool bLatch      = context.getNumber( 1 ) != 0.0f;
                const bool bPressed    = context._sensorValue >= context.getNumber( 0 ) || ( bLatch && bWasPressed );
                context.setOutput( 0, bPressed );
                context.setOutput( 1, bPressed && bWasPressed == false );
                context.setOutput( 2, bPressed == false && bWasPressed );
                context._pIntState[0] = bPressed ? 1 : 0;
            }

            /** @brief Laser · Signal — 값 0.5 이상이면 켬. 출력 (켬, 끔, 켜짐 펄스, 꺼짐 펄스). 상태 int[0] = 켬. */
            static void stepBinarySensor( GimmickNodeContext& context )
            {
                const bool bWasOn = context._pIntState[0] != 0;
                const bool bOn    = context._sensorValue >= 0.5f;
                context.setOutput( 0, bOn );
                context.setOutput( 1, bOn == false );
                context.setOutput( 2, bOn && bWasOn == false );
                context.setOutput( 3, bOn == false && bWasOn );
                context._pIntState[0] = bOn ? 1 : 0;
            }

            /** @brief Proximity — 값 = 가장 가까운 대상까지 거리. 출력 Near · Far · OnApproach · OnLeave, 매개변수 radius. */
            static void stepProximity( GimmickNodeContext& context )
            {
                const bool bWasNear = context._pIntState[0] != 0;
                const bool bNear    = context._sensorValue <= context.getNumber( 0 );
                context.setOutput( 0, bNear );
                context.setOutput( 1, bNear == false );
                context.setOutput( 2, bNear && bWasNear == false );
                context.setOutput( 3, bNear == false && bWasNear );
                context._pIntState[0] = bNear ? 1 : 0;
            }

            /** @brief Timer — 입력 Enable · Reset, 출력 OnTick · Running, 매개변수 interval · startEnabled · repeat. 상태 int[0] = 지난 걸음, int[1] = 울렸음. */
            static void stepTimer( GimmickNodeContext& context )
            {
                if ( context.isRising( 1 ) )
                {
                    context._pIntState[0] = 0;
                    context._pIntState[1] = 0;
                }
                const bool bEnabled = context.isInputHigh( 0, context.getNumber( 1 ) != 0.0f );
                const bool bRepeat  = context.getNumber( 2 ) != 0.0f;
                const bool bCanRun  = bEnabled && ( bRepeat || context._pIntState[1] == 0 );
                bool       bTick    = false;
                if ( bCanRun )
                {
                    ++context._pIntState[0];
                    if ( context._pIntState[0] >= context.toSteps( context.getNumber( 0 ), 1 ) )
                    {
                        bTick                 = true;
                        context._pIntState[0] = 0;
                        context._pIntState[1] = 1;
                    }
                }
                context.setOutput( 0, bTick );
                context.setOutput( 1, bCanRun );
            }

            /** @brief Damage — 충격 = 이번 피해. 입력 Reset, 출력 OnDamaged · Broken, 매개변수 threshold · health. 상태 float[0] = 누적, int[0] = 부서짐. */
            static void stepDamage( GimmickNodeContext& context )
            {
                if ( context.isRising( 0 ) )
                {
                    context._pFloatState[0] = 0.0f;
                    context._pIntState[0]   = 0;
                }
                const bool bDamaged = context._sensorImpulse > 0.0f && context._sensorImpulse > context.getNumber( 0 );
                if ( bDamaged )
                    context._pFloatState[0] += context._sensorImpulse;
                const float32 health = context.getNumber( 1 );
                if ( health > 0.0f && context._pFloatState[0] >= health )
                    context._pIntState[0] = 1;
                context.setOutput( 0, bDamaged );
                context.setOutput( 1, context._pIntState[0] != 0 );
            }

            /** @brief Interaction — 충격 = 사용 횟수. 출력 OnUsed · Toggled. 상태 int[0] = 뒤집힘. */
            static void stepInteraction( GimmickNodeContext& context )
            {
                const int32 useCount = static_cast<int32>( context._sensorImpulse + 0.5f );
                if ( ( useCount & 1 ) != 0 )
                    context._pIntState[0] ^= 1;
                context.setOutput( 0, useCount > 0 );
                context.setOutput( 1, context._pIntState[0] != 0 );
            }

            /** @brief Constant — 출력 Out = value ≠ 0. */
            static void stepConstant( GimmickNodeContext& context ) { context.setOutput( 0, context.getNumber( 0 ) != 0.0f ); }

            // ------------------------------------------------------------------------------
            // 연산자
            // ------------------------------------------------------------------------------
            /** @brief And — 연결된 입력이 모두 참(연결이 없으면 거짓). */
            static void stepAnd( GimmickNodeContext& context )
            {
                const uint32 connected = context._connectedInputBits;
                context.setOutput( 0, connected != 0 && ( context._inputBits & connected ) == connected );
            }

            /** @brief Or — 입력 하나라도 참. */
            static void stepOr( GimmickNodeContext& context ) { context.setOutput( 0, ( context._inputBits & context._connectedInputBits ) != 0 ); }

            /** @brief Xor — 참인 입력이 홀수 개. */
            static void stepXor( GimmickNodeContext& context ) { context.setOutput( 0, ( countBits( context._inputBits & context._connectedInputBits ) & 1u ) != 0 ); }

            /** @brief Not — 입력을 뒤집는다. */
            static void stepNot( GimmickNodeContext& context ) { context.setOutput( 0, context.isInput( 0 ) == false ); }

            static bool initializeStartBit( GimmickNodeContext& context )
            {
                context._pIntState[0] = context.getNumber( 0 ) != 0.0f ? 1 : 0;
                context.setOutput( 0, context._pIntState[0] != 0 );
                return true;
            }

            /** @brief Toggle — In 이 오를 때마다 뒤집고 Reset 이 오르면 처음 값. 매개변수 start. */
            static void stepToggle( GimmickNodeContext& context )
            {
                if ( context.isRising( 1 ) )
                    context._pIntState[0] = context.getNumber( 0 ) != 0.0f ? 1 : 0;
                else if ( context.isRising( 0 ) )
                    context._pIntState[0] ^= 1;
                context.setOutput( 0, context._pIntState[0] != 0 );
            }

            /** @brief Latch — Set 이면 참으로, Reset 이면 거짓으로 남는다(둘 다면 Reset). 매개변수 start. */
            static void stepLatch( GimmickNodeContext& context )
            {
                if ( context.isInput( 1 ) )
                    context._pIntState[0] = 0;
                else if ( context.isInput( 0 ) )
                    context._pIntState[0] = 1;
                context.setOutput( 0, context._pIntState[0] != 0 );
            }

            /** @brief Counter — Count 가 오르면 +1, Down 이면 −1(0 아래로 가지 않는다), Reset 이면 0. 출력 Reached(≥ target) · OnReached. */
            static void stepCounter( GimmickNodeContext& context )
            {
                int32& count = context._pIntState[0];
                if ( context.isRising( 2 ) )
                    count = 0;
                if ( context.isRising( 0 ) )
                    ++count;
                if ( context.isRising( 1 ) )
                    count = MathUtil::max( 0, count - 1 );
                const bool bReached = count >= MathUtil::max( 1, static_cast<int32>( context.getNumber( 0 ) ) );
                context.setOutput( 1, bReached && context.wasOutput( 0 ) == false );
                context.setOutput( 0, bReached );
            }

            /**
             * @brief Delay — 입력의 바뀜(오름 · 내림)을 seconds 만큼(걸음 수) 뒤에 출력에 옮깁니다. 고리를 끊는 노드입니다.
             * @details 상태 int[0] = 출력, [1] = 마지막으로 받은 입력, [2] = 대기 수, [3] = 머리, [4..] = 바뀔 걸음(원형 큐). 큐가 차면 마지막으로 받은
             *          바뀜을 지웁니다 — 새 바뀜이 그것을 되돌리는 것이라(바뀜은 늘 번갈아 온다) 창보다 짧은 펄스가 사라질 뿐 수준은 어긋나지 않습니다.
             */
            static void stepDelay( GimmickNodeContext& context )
            {
                int32* pState = context._pIntState;
                while ( pState[2] > 0 && static_cast<uint32>( pState[kDelayQueueStart + pState[3]] ) <= context._stepIndex )
                {
                    pState[0] ^= 1;
                    pState[3] = ( pState[3] + 1 ) % kDelayCapacity;
                    --pState[2];
                }
                context.setOutput( 0, pState[0] != 0 );
            }

            static void commitDelay( GimmickNodeContext& context )
            {
                int32*      pState = context._pIntState;
                const int32 input  = context.isInput( 0 ) ? 1 : 0;
                if ( input == pState[1] )
                    return;
                pState[1] = input;
                if ( pState[2] >= kDelayCapacity )
                {
                    --pState[2];
                    return;
                }
                const int32 slot                = ( pState[3] + pState[2] ) % kDelayCapacity;
                pState[kDelayQueueStart + slot] = static_cast<int32>( context._stepIndex ) + context.toSteps( context.getNumber( 0 ), 1 );
                ++pState[2];
            }

            /** @brief Pulse — In 이 오르면 seconds 동안 Out 이 참(다시 오르면 처음부터). 상태 int[0] = 남은 걸음. */
            static void stepPulse( GimmickNodeContext& context )
            {
                if ( context.isRising( 0 ) )
                    context._pIntState[0] = context.toSteps( context.getNumber( 0 ), 1 );
                context.setOutput( 0, context._pIntState[0] > 0 );
                if ( context._pIntState[0] > 0 )
                    --context._pIntState[0];
            }

            /**
             * @brief Sequence — In0..In(count−1) 이 차례로 올라야 Done(남는다). 틀린 입력이 오르면 OnFail 이고 처음부터(그것이 In0 이면 첫 칸은 맞은 것).
             * @details 한 걸음에 여럿이 오르면 번호 순서로 봅니다. Reset 이 오르면 처음으로. 상태 int[0] = 진행, int[1] = 끝남.
             */
            static void stepSequence( GimmickNodeContext& context )
            {
                int32& progress = context._pIntState[0];
                int32& done     = context._pIntState[1];
                if ( context.isRising( 8 ) )
                {
                    progress = 0;
                    done     = 0;
                }
                const int32 count = MathUtil::clamp( static_cast<int32>( context.getNumber( 0 ) ), 1, 8 );
                bool        bStep = false;
                bool        bFail = false;
                for ( int32 input = 0; input < count && done == 0; ++input )
                {
                    if ( context.isRising( static_cast<uint32>( input ) ) == false )
                        continue;
                    if ( input == progress )
                    {
                        ++progress;
                        bStep = true;
                    }
                    else
                    {
                        progress = input == 0 ? 1 : 0;
                        bFail    = true;
                    }
                    if ( progress >= count )
                        done = 1;
                }
                context.setOutput( 0, done != 0 );
                context.setOutput( 1, bFail );
                context.setOutput( 2, bStep );
            }

            // ------------------------------------------------------------------------------
            // 액추에이터 — 상태 float[0] 이 오브젝트에 거는 값(열림 · 거리 · 높이 · 각)
            // ------------------------------------------------------------------------------
            static bool initializeDoor( GimmickNodeContext& context )
            {
                const bool bOpen        = context.getNumber( 2 ) != 0.0f;
                context._pFloatState[0] = bOpen ? 1.0f : 0.0f;
                context.setOutput( 0, bOpen );
                context.setOutput( 1, bOpen == false );
                return true;
            }

            /** @brief Door — 입력 Open(수준) · Toggle(오름) · Lock(수준, 멈춤), 출력 Opened · Closed · Moving · OnOpened · OnClosed. */
            static void stepDoor( GimmickNodeContext& context )
            {
                if ( context.isRising( 1 ) )
                    context._pIntState[0] ^= 1;
                const bool    bWantOpen = context.isInputHigh( 0, context.getNumber( 2 ) != 0.0f ) != ( context._pIntState[0] != 0 );
                float32&      openness  = context._pFloatState[0];
                const float32 previous  = openness;
                if ( context.isInput( 2 ) == false )
                {
                    const float32 duration = bWantOpen ? context.getNumber( 0 ) : context.getNumber( 1 );
                    const float32 delta    = duration > 0.0f ? context._stepTime / duration : 1.0f;
                    openness               = MathUtil::clamp( openness + ( bWantOpen ? delta : -delta ), 0.0f, 1.0f );
                }
                const bool bOpened = openness >= 1.0f;
                const bool bClosed = openness <= 0.0f;
                context.setOutput( 3, bOpened && context.wasOutput( 0 ) == false );
                context.setOutput( 4, bClosed && context.wasOutput( 1 ) == false );
                context.setOutput( 0, bOpened );
                context.setOutput( 1, bClosed );
                context.setOutput( 2, openness != previous );
            }

            static bool initializeMover( GimmickNodeContext& context )
            {
                const string& mode = context.getText( 1 );
                if ( StringUtil::equals( mode, "Once", true ) )
                    context._pIntState[3] = static_cast<int32>( MoverMode::Once );
                else if ( StringUtil::equals( mode, "Loop", true ) )
                    context._pIntState[3] = static_cast<int32>( MoverMode::Loop );
                else if ( StringUtil::equals( mode, "PingPong", true ) )
                    context._pIntState[3] = static_cast<int32>( MoverMode::PingPong );
                else
                    return failParam( context, "mode must be Once, Loop or PingPong" );
                CameraBlendCurve curve{ CameraBlendCurve::Linear };
                if ( engine::getTypeRegistry().enumFromString( string_view( context.getText( 2 ) ), curve ) == false )
                    return failParam( context, "curve is not a CameraBlendCurve name (Linear, EaseIn, EaseOut, EaseInOut, SmoothStep, Cubic, Exponential)" );
                context._pIntState[4] = static_cast<int32>( curve );
                context._pIntState[1] = 1;
                context.setOutput( 0, true );
                return true;
            }

            /**
             * @brief Mover — 길(호 길이)을 따라 speed(m/s)로 갑니다. 입력 Enable(수준, 연결이 없으면 autoStart) · Reverse(수준) · Restart(오름),
             *        출력 AtStart · AtEnd · Moving · OnArrive. 모드 Once · Loop · PingPong, 곡선 curve, 끝에서 pause 초 멈춤.
             * @details 진행은 정수 걸음 p(0..P, P = ⌈길이 / 속도 / 걸음⌉)로 셉니다 — 끝에 닿는 걸음이 정확히 정해지고 누적 오차가 없습니다. 거리 = 곡선(p / P) × 길이.
             *          상태 float[0] = 거리, int[0] = p, [1] = 방향, [2] = 남은 멈춤, [3] = 모드, [4] = 곡선.
             */
            static void stepMover( GimmickNodeContext& context )
            {
                int32*        pState     = context._pIntState;
                const float32 length     = context._pathLength > 0.0f ? context._pathLength : context.getNumber( 5 );
                const float32 speed      = context.getNumber( 0 );
                const bool    bCanMove   = length > 0.0f && speed > 0.0f && context._stepTime > 0.0f;
                const int32   totalSteps = bCanMove ? MathUtil::max( 1, static_cast<int32>( MathUtil::ceil( length / speed / context._stepTime - 1.0e-4f ) ) ) : 1;
                if ( context.isRising( 2 ) )
                {
                    pState[0] = 0;
                    pState[1] = 1;
                    pState[2] = 0;
                }
                const bool      bEnabled = bCanMove && context.isInputHigh( 0, context.getNumber( 4 ) != 0.0f );
                const bool      bReverse = context.isInput( 1 );
                const MoverMode mode     = static_cast<MoverMode>( pState[3] );
                bool            bMoving  = false;
                bool            bArrive  = false;
                if ( bEnabled && pState[2] > 0 )
                {
                    --pState[2];
                }
                else if ( bEnabled )
                {
                    const int32 pauseSteps = context.toSteps( context.getNumber( 3 ), 0 );
                    switch ( mode )
                    {
                        case MoverMode::Once:
                        {
                            const int32 target = bReverse ? 0 : totalSteps;
                            if ( pState[0] != target )
                            {
                                pState[0] += pState[0] < target ? 1 : -1;
                                bMoving = true;
                                bArrive = pState[0] == target;
                            }
                            break;
                        }
                        case MoverMode::Loop:
                        {
                            const int32 direction = bReverse ? -1 : 1;
                            if ( direction > 0 && pState[0] >= totalSteps )
                                pState[0] = 0;
                            else if ( direction < 0 && pState[0] <= 0 )
                                pState[0] = totalSteps;
                            pState[0] += direction;
                            bMoving = true;
                            bArrive = direction > 0 ? pState[0] >= totalSteps : pState[0] <= 0;
                            if ( bArrive )
                                pState[2] = pauseSteps;
                            break;
                        }
                        case MoverMode::PingPong:
                        {
                            pState[0] += pState[1];
                            bMoving = true;
                            if ( pState[0] >= totalSteps )
                            {
                                pState[0] = totalSteps;
                                pState[1] = -1;
                                bArrive   = true;
                                pState[2] = pauseSteps;
                            }
                            else if ( pState[0] <= 0 )
                            {
                                pState[0] = 0;
                                pState[1] = 1;
                                bArrive   = true;
                                pState[2] = pauseSteps;
                            }
                            break;
                        }
                    }
                }
                pState[0] = MathUtil::clamp( pState[0], 0, totalSteps );
                CameraBlendSpec spec;
                spec._curve             = static_cast<CameraBlendCurve>( pState[4] );
                const float32 weight    = evaluateBlendWeight( spec, static_cast<float32>( pState[0] ) / static_cast<float32>( totalSteps ) );
                context._pFloatState[0] = bCanMove ? weight * length : 0.0f;
                context.setOutput( 0, pState[0] <= 0 );
                context.setOutput( 1, pState[0] >= totalSteps );
                context.setOutput( 2, bMoving );
                context.setOutput( 3, bArrive );
            }

            static bool initializeElevator( GimmickNodeContext& context )
            {
                float32      arrFloor[kMaxElevatorFloor] = {};
                const uint32 floorCount                  = GameDataXml::parseFloats( context.getText( 0 ), arrFloor, kMaxElevatorFloor );
                if ( floorCount == 0 )
                    return failParam( context, "floors needs at least one height (\"0 4 8\")" );
                for ( uint32 floor = 0; floor < floorCount; ++floor )
                    context._pFloatState[1 + floor] = arrFloor[floor];
                context._pIntState[1]   = static_cast<int32>( floorCount );
                context._pIntState[0]   = MathUtil::clamp( static_cast<int32>( context.getNumber( 2 ) ), 0, static_cast<int32>( floorCount ) - 1 );
                context._pFloatState[0] = context._pFloatState[1 + context._pIntState[0]];
                return true;
            }

            /** @brief Elevator — 입력 Up · Down · Call0..Call3(오름), 출력 Moving · OnArrive · AtBottom · AtTop. 상태 float[0] = 높이, [1..] = 층, int[0] = 목표 층. */
            static void stepElevator( GimmickNodeContext& context )
            {
                int32&      target     = context._pIntState[0];
                const int32 floorCount = context._pIntState[1];
                if ( context.isRising( 0 ) )
                    target = MathUtil::min( target + 1, floorCount - 1 );
                if ( context.isRising( 1 ) )
                    target = MathUtil::max( target - 1, 0 );
                for ( int32 call = 0; call < 4 && call < floorCount; ++call )
                {
                    if ( context.isRising( static_cast<uint32>( 2 + call ) ) )
                        target = call;
                }
                float32&      height = context._pFloatState[0];
                const float32 goal   = context._pFloatState[1 + target];
                const float32 delta  = MathUtil::max( 0.0f, context.getNumber( 1 ) ) * context._stepTime;
                const bool    bReach = MathUtil::abs( goal - height ) <= delta;
                height               = bReach ? goal : height + ( goal > height ? delta : -delta );
                const bool bMoving   = bReach == false;
                context.setOutput( 1, bReach && context.wasOutput( 0 ) );
                context.setOutput( 0, bMoving );
                context.setOutput( 2, bReach && target == 0 );
                context.setOutput( 3, bReach && target == floorCount - 1 );
            }

            /**
             * @brief Rotator — 입력 Enable(수준, 연결이 없으면 autoStart) · Reverse(수준), 출력 Rotating · AtTarget · AtRest. 상태 float[0] = 각(도).
             * @details targetAngle 이 0 이면 켜진 동안 계속 돈다(0..360 으로 감김). 아니면 켜지면 그 각으로, 꺼지면 0 으로 speed(도/초)로 간다(도개교 · 회전문).
             */
            static void stepRotator( GimmickNodeContext& context )
            {
                float32&      angle    = context._pFloatState[0];
                const float32 speed    = context.getNumber( 0 );
                const float32 target   = context.getNumber( 1 );
                const bool    bEnabled = context.isInputHigh( 0, context.getNumber( 2 ) != 0.0f );
                const float32 previous = angle;
                if ( target == 0.0f )
                {
                    if ( bEnabled )
                    {
                        angle = MathUtil::fmod( angle + speed * context._stepTime * ( context.isInput( 1 ) ? -1.0f : 1.0f ), 360.0f );
                        if ( angle < 0.0f )
                            angle += 360.0f;
                    }
                }
                else
                {
                    const float32 goal  = bEnabled ? target : 0.0f;
                    const float32 delta = MathUtil::abs( speed ) * context._stepTime;
                    angle               = MathUtil::abs( goal - angle ) <= delta ? goal : angle + ( goal > angle ? delta : -delta );
                }
                context.setOutput( 0, angle != previous );
                context.setOutput( 1, target != 0.0f && angle == target );
                context.setOutput( 2, angle == 0.0f );
            }

            /** @brief Spawner — 입력 Spawn · Reset(오름), 출력 OnSpawn · Exhausted, 매개변수 maxCount(0 = 끝없음) · prefab · offset. 상태 int[0] = 낸 수. */
            static void stepSpawner( GimmickNodeContext& context )
            {
                if ( context.isRising( 1 ) )
                    context._pIntState[0] = 0;
                const int32 maxCount   = static_cast<int32>( context.getNumber( 0 ) );
                const bool  bExhausted = maxCount > 0 && context._pIntState[0] >= maxCount;
                const bool  bSpawn     = context.isRising( 0 ) && bExhausted == false;
                if ( bSpawn )
                    ++context._pIntState[0];
                context.setOutput( 0, bSpawn );
                context.setOutput( 1, maxCount > 0 && context._pIntState[0] >= maxCount );
            }

            static bool initializeHazard( GimmickNodeContext& context )
            {
                const int32 onSteps   = context.toSteps( context.getNumber( 0 ), 1 );
                const int32 offSteps  = context.getNumber( 1 ) > 0.0f ? context.toSteps( context.getNumber( 1 ), 1 ) : 0;
                context._pIntState[0] = context.toSteps( context.getNumber( 2 ), 0 ) % ( onSteps + offSteps );
                return true;
            }

            /**
             * @brief Hazard — 입력 Enable(수준, 연결이 없으면 startEnabled), 출력 Active · OnActivate · OnDamageTick. onTime 켜짐 · offTime 꺼짐을
             *        되풀이(offTime 0 이면 늘 켜짐), phase 로 시작을 민다. 켜진 동안 damageInterval 마다 OnDamageTick(켜지는 걸음에 한 번).
             */
            static void stepHazard( GimmickNodeContext& context )
            {
                const int32 onSteps  = context.toSteps( context.getNumber( 0 ), 1 );
                const int32 offSteps = context.getNumber( 1 ) > 0.0f ? context.toSteps( context.getNumber( 1 ), 1 ) : 0;
                const bool  bEnabled = context.isInputHigh( 0, context.getNumber( 3 ) != 0.0f );
                bool        bActive  = false;
                if ( bEnabled )
                {
                    bActive = offSteps == 0 || context._pIntState[0] < onSteps;
                    if ( offSteps > 0 )
                        context._pIntState[0] = ( context._pIntState[0] + 1 ) % ( onSteps + offSteps );
                }
                const bool bActivated = bActive && context.wasOutput( 0 ) == false;
                bool       bTick      = false;
                if ( bActivated )
                {
                    context._pIntState[1] = 0;
                    bTick                 = true;
                }
                else if ( bActive )
                {
                    ++context._pIntState[1];
                    if ( context._pIntState[1] >= context.toSteps( context.getNumber( 5 ), 1 ) )
                    {
                        context._pIntState[1] = 0;
                        bTick                 = true;
                    }
                }
                context.setOutput( 0, bActive );
                context.setOutput( 1, bActivated );
                context.setOutput( 2, bTick );
            }

            /** @brief Light — 입력 On(수준, 연결이 없으면 startOn) · Toggle(오름), 출력 Lit. */
            static void stepLight( GimmickNodeContext& context )
            {
                if ( context.isRising( 1 ) )
                    context._pIntState[0] ^= 1;
                context.setOutput( 0, context.isInputHigh( 0, context.getNumber( 0 ) != 0.0f ) != ( context._pIntState[0] != 0 ) );
            }

            /** @brief Sound — 입력 Play(오름), 출력 OnPlay. 소리 경로(sound)는 씬 쪽이 낸다. */
            static void stepSound( GimmickNodeContext& context ) { context.setOutput( 0, context.isRising( 0 ) ); }

            /** @brief Enable — 입력 Enable(수준, 연결이 없으면 startEnabled) · Toggle(오름), 출력 Enabled. 씬 쪽이 대상 오브젝트를 켜고 끈다. */
            static void stepEnable( GimmickNodeContext& context )
            {
                if ( context.isRising( 1 ) )
                    context._pIntState[0] ^= 1;
                context.setOutput( 0, context.isInputHigh( 0, context.getNumber( 0 ) != 0.0f ) != ( context._pIntState[0] != 0 ) );
            }

            static bool initializeEnable( GimmickNodeContext& context )
            {
                context.setOutput( 0, context.getNumber( 0 ) != 0.0f );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 GimmickNodeContext::toSteps( float32 seconds, int32 minSteps ) const
    {
        if ( _stepTime <= 0.0f )
            return minSteps;
        return MathUtil::max( minSteps, static_cast<int32>( MathUtil::round( seconds / _stepTime ) ) );
    }

    void GimmickNodeContext::setOutput( uint32 port, bool bValue )
    {
        if ( bValue )
            _outputBits |= 1u << port;
        else
            _outputBits &= ~( 1u << port );
    }

    int32 GimmickNodeKind::findInput( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listInput.size(); ++index )
        {
            if ( _listInput[index] == name )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 GimmickNodeKind::findOutput( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listOutput.size(); ++index )
        {
            if ( _listOutput[index] == name )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 GimmickNodeKind::findParam( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listParam.size(); ++index )
        {
            if ( _listParam[index]._name == name )
                return static_cast<int32>( index );
        }
        return -1;
    }

    const GimmickNodeRegistry& GimmickNodeRegistry::getDefault()
    {
        static const GimmickNodeRegistry s_registry = GimmickNodeRegistryInternal::createDefaultRegistry();
        return s_registry;
    }

    bool GimmickNodeRegistry::registerKind( const GimmickNodeKind& kind )
    {
        const bool bTooManyPorts = kind._listInput.size() > GimmickNodeRegistryInternal::kMaxPortCount ||
                                   kind._listOutput.size() > GimmickNodeRegistryInternal::kMaxPortCount;
        if ( kind._name.empty() || bTooManyPorts || kind._pStep == nullptr || findKind( kind._name ) != nullptr )
            return false;
        _listKind.push_back( kind );
        return true;
    }

    const GimmickNodeKind* GimmickNodeRegistry::findKind( const hashed_string& name ) const
    {
        for ( const GimmickNodeKind& kind : _listKind )
        {
            if ( kind._name == name )
                return &kind;
        }
        return nullptr;
    }

    void GimmickNodeRegistry::registerBuiltinKinds( GimmickNodeRegistry& inoutRegistry )
    {
        using Internal                          = GimmickNodeRegistryInternal;
        constexpr GimmickNodeCategory kS        = GimmickNodeCategory::Sensor;
        constexpr GimmickNodeCategory kO        = GimmickNodeCategory::Operator;
        constexpr GimmickNodeCategory kA        = GimmickNodeCategory::Actuator;
        constexpr GimmickParamType    kV        = GimmickParamType::Vector;
        constexpr GimmickParamType    kT        = GimmickParamType::Text;
        const GimmickNodeKind         arrKind[] = {
            // 센서
            Internal::makeKind( "Volume", kS, {}, { "Occupied", "Empty", "OnEnter", "OnExit" }, { Internal::makeParam( "minCount", "1" ) }, 0, 1, nullptr,
                                        &Internal::stepVolume ),
            Internal::makeKind( "PressurePlate", kS, {}, { "Pressed", "OnPress", "OnRelease" },
                                        { Internal::makeParam( "threshold", "1" ), Internal::makeParam( "latch", "0" ) }, 0, 1, nullptr, &Internal::stepPressurePlate ),
            Internal::makeKind( "Laser", kS, {}, { "Blocked", "Clear", "OnBlock", "OnClear" }, { Internal::makeParam( "range", "10" ), Internal::makeParam( "direction", "0 0 1", kV ) },
                                        0, 1, nullptr, &Internal::stepBinarySensor ),
            Internal::makeKind( "Signal", kS, {}, { "Active", "Inactive", "OnActivate", "OnDeactivate" }, {}, 0, 1, nullptr, &Internal::stepBinarySensor ),
            Internal::makeKind( "Proximity", kS, {}, { "Near", "Far", "OnApproach", "OnLeave" },
                                        { Internal::makeParam( "radius", "3" ), Internal::makeParam( "tag", "", kT ), Internal::makeParam( "planar", "0" ) }, 0, 1, nullptr,
                                        &Internal::stepProximity ),
            Internal::makeKind( "Timer", kS, { "Enable", "Reset" }, { "OnTick", "Running" },
                                        { Internal::makeParam( "interval", "1" ), Internal::makeParam( "startEnabled", "1" ), Internal::makeParam( "repeat", "1" ) }, 0, 2, nullptr,
                                        &Internal::stepTimer ),
            Internal::makeKind( "Damage", kS, { "Reset" }, { "OnDamaged", "Broken" }, { Internal::makeParam( "threshold", "0" ), Internal::makeParam( "health", "0" ) }, 1,
                                        1, nullptr, &Internal::stepDamage ),
            Internal::makeKind( "Interaction", kS, {}, { "OnUsed", "Toggled" }, {}, 0, 1, nullptr, &Internal::stepInteraction ),
            Internal::makeKind( "Constant", kS, {}, { "Out" }, { Internal::makeParam( "value", "1" ) }, 0, 0, nullptr, &Internal::stepConstant ),
            // 연산자
            Internal::makeKind( "And", kO, { "A", "B", "C", "D" }, { "Out" }, {}, 0, 0, nullptr, &Internal::stepAnd ),
            Internal::makeKind( "Or", kO, { "A", "B", "C", "D" }, { "Out" }, {}, 0, 0, nullptr, &Internal::stepOr ),
            Internal::makeKind( "Xor", kO, { "A", "B", "C", "D" }, { "Out" }, {}, 0, 0, nullptr, &Internal::stepXor ),
            Internal::makeKind( "Not", kO, { "In" }, { "Out" }, {}, 0, 0, nullptr, &Internal::stepNot ),
            Internal::makeKind( "Toggle", kO, { "In", "Reset" }, { "Out" }, { Internal::makeParam( "start", "0" ) }, 0, 1, &Internal::initializeStartBit, &Internal::stepToggle ),
            Internal::makeKind( "Latch", kO, { "Set", "Reset" }, { "Out" }, { Internal::makeParam( "start", "0" ) }, 0, 1, &Internal::initializeStartBit, &Internal::stepLatch ),
            Internal::makeKind( "Counter", kO, { "Count", "Down", "Reset" }, { "Reached", "OnReached" }, { Internal::makeParam( "target", "1" ) }, 0, 1, nullptr,
                                        &Internal::stepCounter ),
            Internal::makeKind( "Delay", kO, { "In" }, { "Out" }, { Internal::makeParam( "seconds", "1" ) }, 0,
                                        static_cast<uint16>( Internal::kDelayQueueStart + Internal::kDelayCapacity ), nullptr, &Internal::stepDelay, &Internal::commitDelay ),
            Internal::makeKind( "Pulse", kO, { "In" }, { "Out" }, { Internal::makeParam( "seconds", "1" ) }, 0, 1, nullptr, &Internal::stepPulse ),
            Internal::makeKind( "Sequence", kO, { "In0", "In1", "In2", "In3", "In4", "In5", "In6", "In7", "Reset" }, { "Done", "OnFail", "OnStep" },
                                        { Internal::makeParam( "count", "2" ) }, 0, 2, nullptr, &Internal::stepSequence ),
            // 액추에이터
            Internal::makeKind( "Door", kA, { "Open", "Toggle", "Lock" }, { "Opened", "Closed", "Moving", "OnOpened", "OnClosed" },
                                        { Internal::makeParam( "openTime", "1" ), Internal::makeParam( "closeTime", "1" ), Internal::makeParam( "startOpen", "0" ),
                                          Internal::makeParam( "openOffset", "0 0 0", kV ), Internal::makeParam( "openRotation", "0 0 0", kV ) },
                                        1, 1, &Internal::initializeDoor, &Internal::stepDoor ),
            Internal::makeKind( "Mover", kA, { "Enable", "Reverse", "Restart" }, { "AtStart", "AtEnd", "Moving", "OnArrive" },
                                        { Internal::makeParam( "speed", "1" ), Internal::makeParam( "mode", "Once", kT ), Internal::makeParam( "curve", "Linear", kT ),
                                          Internal::makeParam( "pause", "0" ), Internal::makeParam( "autoStart", "1" ), Internal::makeParam( "length", "0" ),
                                          Internal::makeParam( "faceForward", "0" ) },
                                        1, 5, &Internal::initializeMover, &Internal::stepMover ),
            Internal::makeKind( "Elevator", kA, { "Up", "Down", "Call0", "Call1", "Call2", "Call3" }, { "Moving", "OnArrive", "AtBottom", "AtTop" },
                                        { Internal::makeParam( "floors", "0 4", kT ), Internal::makeParam( "speed", "2" ), Internal::makeParam( "startFloor", "0" ),
                                          Internal::makeParam( "axis", "0 1 0", kV ) },
                                        static_cast<uint16>( 1 + Internal::kMaxElevatorFloor ), 2, &Internal::initializeElevator, &Internal::stepElevator ),
            Internal::makeKind( "Rotator", kA, { "Enable", "Reverse" }, { "Rotating", "AtTarget", "AtRest" },
                                        { Internal::makeParam( "speed", "90" ), Internal::makeParam( "targetAngle", "0" ), Internal::makeParam( "autoStart", "1" ),
                                          Internal::makeParam( "axis", "0 1 0", kV ) },
                                        1, 0, nullptr, &Internal::stepRotator ),
            Internal::makeKind( "Spawner", kA, { "Spawn", "Reset" }, { "OnSpawn", "Exhausted" },
                                        { Internal::makeParam( "maxCount", "0" ), Internal::makeParam( "prefab", "", kT ), Internal::makeParam( "offset", "0 0 0", kV ) }, 0, 1,
                                        nullptr, &Internal::stepSpawner ),
            Internal::makeKind( "Hazard", kA, { "Enable" }, { "Active", "OnActivate", "OnDamageTick" },
                                        { Internal::makeParam( "onTime", "1" ), Internal::makeParam( "offTime", "0" ), Internal::makeParam( "phase", "0" ),
                                          Internal::makeParam( "startEnabled", "1" ), Internal::makeParam( "damage", "10" ), Internal::makeParam( "damageInterval", "0.5" ) },
                                        0, 2, &Internal::initializeHazard, &Internal::stepHazard ),
            Internal::makeKind( "Light", kA, { "On", "Toggle" }, { "Lit" }, { Internal::makeParam( "startOn", "0" ), Internal::makeParam( "intensity", "1" ) }, 0, 1, nullptr,
                                        &Internal::stepLight ),
            Internal::makeKind( "Sound", kA, { "Play" }, { "OnPlay" }, { Internal::makeParam( "sound", "", kT ) }, 0, 0, nullptr, &Internal::stepSound ),
            Internal::makeKind( "Enable", kA, { "Enable", "Toggle" }, { "Enabled" }, { Internal::makeParam( "startEnabled", "1" ) }, 0, 1, &Internal::initializeEnable,
                                        &Internal::stepEnable ),
        };
        for ( const GimmickNodeKind& kind : arrKind )
        {
            const bool bAdded = inoutRegistry.registerKind( kind );
            SW_ASSERT( bAdded );
            (void)bAdded;
        }
    }
} // namespace sw
