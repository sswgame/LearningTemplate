#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierGPUProgram.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// MeshModifierGPUProgramTest — 스택 → GPU 변형 프로그램 변환(GPU 없이). 실행 결과가 CPU 와 같은지는 MeshModifierGPUTest.ProgramMatchesCPU 가 본다.
// ------------------------------------------------------------------------------

using namespace sw;
using namespace sw::editor;

/**
 * @brief [MeshModifierGPUProgramTest] 셰이더 종류 표가 `supportsGPU()` 인 등록 모디파이어 집합과 정확히 같다
 * @details 표에만 있으면 셰이더가 모르는 CPU 전용 모디파이어를 GPU 로 보내고, 등록에만 있으면 GPU 로 옮길 수 있다고 말하면서 늘 CPU 로 떨어진다.
 *          표의 매개변수는 모두 스칼라 · Vector3 라 기본 배치가 매개변수 하나당 float4 하나다(셰이더가 선언 번호로 읽는다).
 */
SW_TEST_CASE( MeshModifierGPUProgramTest, KindTableMatchesSupportsGPU )
{
    vector<const MeshModifierRegistration*> listRegistration;
    MeshModifierRegistry::getRegistrations( listRegistration );
    uint32 supportedCount = 0;
    for ( const MeshModifierRegistration* pRegistration : listRegistration )
    {
        const unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( pRegistration->_pID );
        SW_ASSERT_NOT_NULL( modifier.get() );
        const bool bInTable = MeshModifierGPUProgram::findKind( pRegistration->_pID ) >= 0;
        SW_EXPECT_TRUE_MSG( modifier->supportsGPU() == bInTable, pRegistration->_pID );
        if ( modifier->supportsGPU() == false )
            continue;
        ++supportedCount;
        vector<float4> listParam;
        SW_EXPECT_TRUE( modifier->packGPUParams( listParam ) );
        SW_EXPECT_TRUE_MSG( static_cast<uint32>( listParam.size() ) == modifier->getParamCount(), pRegistration->_pID );
    }
    SW_EXPECT_EQUAL( supportedCount, MeshModifierGPUProgram::getKindCount() );
    for ( uint32 kind = 0; kind < MeshModifierGPUProgram::getKindCount(); ++kind )
    {
        SW_EXPECT_EQUAL( MeshModifierGPUProgram::findKind( MeshModifierGPUProgram::getKindTypeID( kind ) ), static_cast<int32>( kind ) );
    }
}

/**
 * @brief [MeshModifierGPUProgramTest] 프로그램 모양: 머리(수 · 시간) 뒤에 켜진 모디파이어마다 고정 기록 + 매개변수, 상자는 CPU 적용이 모은 값
 */
SW_TEST_CASE( MeshModifierGPUProgramTest, MakeWritesHeaderRecordsAndBounds )
{
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 4 );
    MeshModifierStack      stack;
    const int32            bendIndex  = stack.addModifier( "bend" );
    const int32            noiseIndex = stack.addModifier( "noise" );
    const int32            offIndex   = stack.addModifier( "twist" );
    SW_ASSERT_TRUE( bendIndex >= 0 );
    SW_ASSERT_TRUE( noiseIndex >= 0 );
    SW_ASSERT_TRUE( offIndex >= 0 );
    test::MeshModifierTestUtil::fillParams( *stack.findModifier( bendIndex ), 1 );
    test::MeshModifierTestUtil::fillParams( *stack.findModifier( noiseIndex ), 2 );
    stack.findModifier( offIndex )->setEnabled( false ); // 꺼진 모디파이어는 기록하지 않는다

    vector<MeshModifierBounds> listBounds;
    MeshModifierEvaluation     evaluation{};
    evaluation._time             = 1.25f;
    evaluation._pListInputBounds = &listBounds;
    MeshModifierMesh mesh        = source;
    stack.apply( mesh, evaluation );
    SW_ASSERT_EQUAL( listBounds.size(), static_cast<size_t>( 2 ) );

    vector<float4> listProgram;
    SW_ASSERT_TRUE( MeshModifierGPUProgram::make( stack, evaluation, listBounds, false, listProgram ) );
    const uint32 bendParamCount  = stack.findModifier( bendIndex )->getParamCount();
    const uint32 noiseParamCount = stack.findModifier( noiseIndex )->getParamCount();
    SW_ASSERT_EQUAL( listProgram.size(), static_cast<size_t>( MeshModifierGPUProgram::kHeaderFloat4Count + 2 * MeshModifierGPUProgram::kRecordFloat4Count + bendParamCount + noiseParamCount ) );
    SW_EXPECT_EQUAL( listProgram[0]._x, 2.0f );
    SW_EXPECT_EQUAL( listProgram[0]._y, 1.25f );

    const float4& bendRecord = listProgram[1];
    SW_EXPECT_EQUAL( bendRecord._x, static_cast<float32>( MeshModifierGPUProgram::findKind( "bend" ) ) );
    SW_EXPECT_EQUAL( bendRecord._y, static_cast<float32>( bendParamCount ) );
    SW_EXPECT_EQUAL( listProgram[2]._x, listBounds[0]._min._x );
    SW_EXPECT_EQUAL( listProgram[3]._y, listBounds[0]._max._y );
    SW_EXPECT_EQUAL( listProgram[2]._w, 1.0f );
    SW_EXPECT_EQUAL( listProgram[1 + MeshModifierGPUProgram::kRecordFloat4Count]._x, stack.findModifier( bendIndex )->getFloat( 0 ) ); // 첫 매개변수 = Angle

    const float4& noiseRecord = listProgram[1 + MeshModifierGPUProgram::kRecordFloat4Count + bendParamCount];
    SW_EXPECT_EQUAL( noiseRecord._x, static_cast<float32>( MeshModifierGPUProgram::findKind( "noise" ) ) );
}

/**
 * @brief [MeshModifierGPUProgramTest] CPU 전용 모디파이어 · 법선 재계산 · 가중치 채널 · 상자 수 불일치는 만들지 않는다
 */
SW_TEST_CASE( MeshModifierGPUProgramTest, RejectsWhatTheShaderCannotRun )
{
    MeshModifierStack stack;
    SW_ASSERT_TRUE( stack.addModifier( "bend" ) >= 0 );
    const int32 rubberIndex = stack.addModifier( "rubber" ); // 곡선 매개변수 - CPU 전용
    SW_ASSERT_TRUE( rubberIndex >= 0 );

    int32 blockingIndex = -1;
    SW_EXPECT_FALSE( MeshModifierGPUProgram::canMake( stack, &blockingIndex ) );
    SW_EXPECT_EQUAL( blockingIndex, rubberIndex );

    const vector<MeshModifierBounds> listBounds( 1, MeshModifierBounds::makeFromPoints( vector<float3>{
                                                        float3{-1.0f, -1.0f, -1.0f},
                                                        float3{ 1.0f,  1.0f,  1.0f}
    } ) );
    vector<float4>                   listProgram;
    SW_EXPECT_FALSE( MeshModifierGPUProgram::make( stack, MeshModifierEvaluation{}, listBounds, false, listProgram ) );
    SW_EXPECT_TRUE( listProgram.empty() );

    stack.findModifier( rubberIndex )->setEnabled( false );
    SW_EXPECT_TRUE( MeshModifierGPUProgram::make( stack, MeshModifierEvaluation{}, listBounds, false, listProgram ) );
    SW_EXPECT_FALSE( MeshModifierGPUProgram::make( stack, MeshModifierEvaluation{}, listBounds, true, listProgram ) );                    // 가중치
    SW_EXPECT_FALSE( MeshModifierGPUProgram::make( stack, MeshModifierEvaluation{}, vector<MeshModifierBounds>{}, false, listProgram ) ); // 상자 없음
    stack.setRecomputingNormals( true );
    SW_EXPECT_FALSE( MeshModifierGPUProgram::canMake( stack, &blockingIndex ) );
    SW_EXPECT_EQUAL( blockingIndex, -1 );
}
