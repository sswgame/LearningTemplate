// 메시 모디파이어 매개변수 값 - 곡선(보간 · 접선) · 곡선 목록 · 참조 목록의 다듬기, 저장 문서 왕복, 목록 항목 연산, 참조 걸러 보기(파생 컴포넌트).
#include "pch.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/FloatCurve.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierParamEdit.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ParamValuesTestInternal
    {
        static uint32 findParam( const MeshModifier& modifier, string_view name )
        {
            uint32 paramIndex = 0;
            (void)modifier.tryGetParamIndex( name, paramIndex ); // 이름이 없으면 0 - 호출하는 테스트가 값으로 걸러낸다
            return paramIndex;
        }

        static FloatCurveKey makeKey( float32 time, float32 value, CurveInterpolation interpolation, bool bAutoTangent )
        {
            FloatCurveKey key{};
            key._time          = time;
            key._value         = value;
            key._interpolation = interpolation;
            key._bAutoTangent  = bAutoTangent;
            return key;
        }
    };
} // namespace

SW_TEST_CASE( MeshModifierParamValueTest, CurveKeepsCubicInterpolationInsteadOfFlatteningToLinear )
{
    unique_ptr<MeshModifier> sculpt = MeshModifierRegistry::create( "curvesculpt" );
    SW_ASSERT_NOT_NULL( sculpt.get() );
    const uint32 curveIndex = ParamValuesTestInternal::findParam( *sculpt, "Curve" );

    // 0 -> 1 두 키: 선형이면 가운데 0.5, 접선이 평평한 큐빅이면 역시 0.5 이지만 1/4 지점은 선형(0.25)과 다르다
    MeshModifierParamValue value{};
    value._curve._listKey.push_back( ParamValuesTestInternal::makeKey( 0.0f, 0.0f, CurveInterpolation::Cubic, false ) );
    value._curve._listKey.push_back( ParamValuesTestInternal::makeKey( 1.0f, 1.0f, CurveInterpolation::Linear, false ) );
    sculpt->setParamValue( curveIndex, value );
    const float32 cubicQuarter = sculpt->evaluateCurve( curveIndex, 0.25f );
    SW_EXPECT_TRUE( cubicQuarter < 0.2f ); // 평평한 접선으로 천천히 오른다
    SW_EXPECT_TRUE( sculpt->getParamValue( curveIndex )._curve._listKey[0]._interpolation == CurveInterpolation::Cubic );

    // 같은 키를 선형으로 바꾸면 직선이다
    value._curve._listKey[0]._interpolation = CurveInterpolation::Linear;
    sculpt->setParamValue( curveIndex, value );
    SW_EXPECT_NEAR_EQUAL( 0.25f, sculpt->evaluateCurve( curveIndex, 0.25f ), 1e-5f );
}

SW_TEST_CASE( MeshModifierParamValueTest, CurveIsSortedClampedAndFinite )
{
    unique_ptr<MeshModifier> sculpt = MeshModifierRegistry::create( "curvesculpt" );
    SW_ASSERT_NOT_NULL( sculpt.get() );
    const uint32 curveIndex = ParamValuesTestInternal::findParam( *sculpt, "Curve" );

    // 편집기가 범위 밖 · 순서가 뒤집힌 · 유한하지 않은 키를 내도 매개변수 규칙(시간 ∈ [0,1], 오름차순, 유한)으로 돌아온다
    MeshModifierParamValue value{};
    value._curve._listKey.push_back( ParamValuesTestInternal::makeKey( 1.5f, 1.0f, CurveInterpolation::Linear, false ) );
    value._curve._listKey.push_back( ParamValuesTestInternal::makeKey( 0.5f, -2.0f, CurveInterpolation::Linear, false ) );
    value._curve._listKey.push_back( ParamValuesTestInternal::makeKey( -1.0f, 3.0f, CurveInterpolation::Linear, false ) );
    value._curve._listKey.push_back( ParamValuesTestInternal::makeKey( 0.2f, MathUtil::kMaxFloat * 2.0f, CurveInterpolation::Linear, false ) );
    sculpt->setParamValue( curveIndex, value );
    const vector<FloatCurveKey>& listKey = sculpt->getParamValue( curveIndex )._curve._listKey;
    SW_ASSERT_EQUAL( size_t{ 3 }, listKey.size() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listKey[0]._time, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, listKey[0]._value, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listKey[1]._time, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listKey[2]._time, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, sculpt->evaluateCurve( curveIndex, 0.25f ), 1e-5f ); // 시간 0.25 는 키 3 과 -2 의 가운데라 선형으로 0.5
}

SW_TEST_CASE( MeshModifierParamValueTest, StackDocumentKeepsCurveInterpolationAndLists )
{
    MeshModifierStack stack;
    const int32       layeredIndex = stack.addModifier( "curvesculptlayered" );
    const int32       multiIndex   = stack.addModifier( "conformmulti" );
    SW_ASSERT_TRUE( layeredIndex >= 0 && multiIndex >= 0 );
    MeshModifier* pLayered = stack.findModifier( layeredIndex );
    MeshModifier* pMulti   = stack.findModifier( multiIndex );

    MeshModifierParamValue shape{};
    shape._curve = test::MeshModifierTestUtil::makeMixedCurve( 0.7f );
    pLayered->setParamValue( ParamValuesTestInternal::findParam( *pLayered, "Shape" ), shape );

    MeshModifierParamValue layerCurves{};
    layerCurves._listCurve.push_back( test::MeshModifierTestUtil::makeMixedCurve( 0.3f ) );
    FloatCurve manualTangents{};
    manualTangents._listKey.push_back( ParamValuesTestInternal::makeKey( 0.0f, 0.0f, CurveInterpolation::Cubic, false ) );
    manualTangents._listKey.push_back( ParamValuesTestInternal::makeKey( 1.0f, 2.0f, CurveInterpolation::Linear, false ) );
    manualTangents._listKey[0]._arriveTangent = 0.5f;
    manualTangents._listKey[0]._leaveTangent  = -1.25f;
    layerCurves._listCurve.push_back( manualTangents );
    layerCurves._listCurve.push_back( FloatCurve{} ); // 키 없는 곡선도 자리를 지킨다
    const uint32 layerCurveIndex = ParamValuesTestInternal::findParam( *pLayered, "Layer Curves" );
    pLayered->setParamValue( layerCurveIndex, layerCurves );

    MeshModifierParamValue targets{};
    targets._listText         = { "floor", "", "wall one" };
    const uint32 targetsIndex = ParamValuesTestInternal::findParam( *pMulti, "Targets" );
    pMulti->setParamValue( targetsIndex, targets );

    const string      text = stack.makeJSONText( 2 );
    MeshModifierStack reread;
    SW_ASSERT_TRUE( reread.readJSONText( text ) );
    SW_EXPECT_EQUAL( text, reread.makeJSONText( 2 ) );

    const MeshModifier*       pLayeredBack = reread.findModifier( layeredIndex );
    const MeshModifier*       pMultiBack   = reread.findModifier( multiIndex );
    const FloatCurve&         shapeBack    = pLayeredBack->getParamValue( ParamValuesTestInternal::findParam( *pLayeredBack, "Shape" ) )._curve;
    const vector<FloatCurve>& listBack     = pLayeredBack->getParamValue( layerCurveIndex )._listCurve;
    SW_ASSERT_EQUAL( size_t{ 3 }, shapeBack._listKey.size() );
    SW_EXPECT_TRUE( shapeBack._listKey[1]._interpolation == CurveInterpolation::Cubic );
    SW_EXPECT_TRUE( shapeBack._listKey[2]._interpolation == CurveInterpolation::Constant );
    for ( const float32 time : { 0.1f, 0.4f, 0.6f, 0.9f } )
    {
        SW_EXPECT_NEAR_EQUAL( shape._curve.evaluate( time ).value(), shapeBack.evaluate( time ).value(), 1e-5f );
    }
    SW_ASSERT_EQUAL( size_t{ 3 }, listBack.size() );
    SW_EXPECT_NEAR_EQUAL( layerCurves._listCurve[0].evaluate( 0.3f ).value(), listBack[0].evaluate( 0.3f ).value(), 1e-5f );
    SW_ASSERT_EQUAL( size_t{ 2 }, listBack[1]._listKey.size() );
    SW_EXPECT_FALSE( listBack[1]._listKey[0]._bAutoTangent );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listBack[1]._listKey[0]._arriveTangent, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -1.25f, listBack[1]._listKey[0]._leaveTangent, 1e-6f );
    SW_EXPECT_TRUE( listBack[2]._listKey.empty() );

    const vector<string>& listTarget = pMultiBack->getParamValue( targetsIndex )._listText;
    SW_ASSERT_EQUAL( size_t{ 3 }, listTarget.size() );
    SW_EXPECT_EQUAL( string( "floor" ), listTarget[0] );
    SW_EXPECT_TRUE( listTarget[1].empty() );
    SW_EXPECT_EQUAL( string( "wall one" ), listTarget[2] );
}

SW_TEST_CASE( MeshModifierParamValueTest, OldLinearCurveDocumentStillLoads )
{
    // 보간 칸이 없던 옛 문서: 키는 [시간, 값] 쌍이고 선형이다
    const string      text = R"({"version":1,"recomputeNormals":false,"modifiers":[{"type":"curvesculpt","params":{"Curve":[[0,1],[0.5,3],[1,1]]}}]})";
    MeshModifierStack stack;
    SW_ASSERT_TRUE( stack.readJSONText( text ) );
    SW_ASSERT_EQUAL( 1, stack.getModifierCount() );
    const MeshModifier* pSculpt    = stack.findModifier( 0 );
    const uint32        curveIndex = ParamValuesTestInternal::findParam( *pSculpt, "Curve" );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pSculpt->evaluateCurve( curveIndex, 0.25f ), 1e-5f );
    SW_EXPECT_TRUE( pSculpt->getParamValue( curveIndex )._curve._listKey[0]._interpolation == CurveInterpolation::Linear );
}

SW_TEST_CASE( MeshModifierParamValueTest, ListsAreCappedByDeclaredMaximum )
{
    unique_ptr<MeshModifier> multi = MeshModifierRegistry::create( "conformmulti" );
    SW_ASSERT_NOT_NULL( multi.get() );
    const uint32 targetsIndex = ParamValuesTestInternal::findParam( *multi, "Targets" );
    const size_t limit        = multi->getParamDefs()[targetsIndex].getListLimit();
    SW_EXPECT_TRUE( limit >= 4u );

    MeshModifierParamValue value{};
    for ( size_t index = 0; index < limit + 5; ++index )
    {
        value._listText.push_back( string( "object" ) );
    }
    multi->setParamValue( targetsIndex, value );
    SW_EXPECT_EQUAL( limit, multi->getParamValue( targetsIndex )._listText.size() );

    // 다른 종류의 칸은 무시된다(참조 목록에 곡선 목록을 넘겨도 아무 일도 없다)
    MeshModifierParamValue wrong{};
    wrong._listCurve.push_back( FloatCurve{} );
    multi->setParamValue( targetsIndex, wrong );
    SW_EXPECT_TRUE( multi->getParamValue( targetsIndex )._listText.empty() );
    SW_EXPECT_TRUE( multi->getParamValue( targetsIndex )._listCurve.empty() );
}

SW_TEST_CASE( MeshModifierParamValueTest, ResetParamsEmptiesLists )
{
    unique_ptr<MeshModifier> layered = MeshModifierRegistry::create( "curvesculptlayered" );
    SW_ASSERT_NOT_NULL( layered.get() );
    const uint32           listIndex = ParamValuesTestInternal::findParam( *layered, "Layer Curves" );
    MeshModifierParamValue value{};
    value._listCurve.push_back( test::MeshModifierTestUtil::makeMixedCurve( 0.5f ) );
    layered->setParamValue( listIndex, value );
    SW_EXPECT_EQUAL( size_t{ 1 }, layered->getParamValue( listIndex )._listCurve.size() );
    layered->resetParams();
    SW_EXPECT_TRUE( layered->getParamValue( listIndex )._listCurve.empty() );
}

SW_TEST_CASE( MeshModifierParamEditTest, ListActionsKeepItemsWholeAndRespectLimits )
{
    vector<string> listItem{ "a", "b", "c" };
    SW_EXPECT_TRUE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::MoveUp, 1 }, listItem, 4 ) );
    SW_EXPECT_TRUE( ( listItem == vector<string>{ "b", "a", "c" } ) );
    SW_EXPECT_TRUE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::MoveDown, 1 }, listItem, 4 ) );
    SW_EXPECT_TRUE( ( listItem == vector<string>{ "b", "c", "a" } ) );
    SW_EXPECT_TRUE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::Duplicate, 0 }, listItem, 4 ) );
    SW_EXPECT_TRUE( ( listItem == vector<string>{ "b", "b", "c", "a" } ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::Duplicate, 0 }, listItem, 4 ) ); // 상한
    SW_EXPECT_TRUE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::Remove, 1 }, listItem, 4 ) );
    SW_EXPECT_TRUE( ( listItem == vector<string>{ "b", "c", "a" } ) );

    // 범위 밖 · 끝 항목 · 아무 동작 없음은 목록을 바꾸지 않고 false 다
    const vector<string> before = listItem;
    SW_EXPECT_FALSE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::MoveUp, 0 }, listItem, 4 ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::MoveDown, 2 }, listItem, 4 ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::Remove, 3 }, listItem, 4 ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{}, listItem, 4 ) );
    SW_EXPECT_TRUE( listItem == before );
    // 패널이 이미 항목을 붙인 경우는 바뀐 것으로 센다
    SW_EXPECT_TRUE( MeshModifierParamEdit::applyListAction( MeshModifierListAction{ MeshModifierListActionKind::Added, 0 }, listItem, 4 ) );
}

SW_TEST_CASE( MeshModifierParamEditTest, ComponentHintMatchesDerivedComponents )
{
    GameObjectManager manager;
    GameObject*       pPlain    = manager.createGameObject( hashed_string( "PlainMesh" ) );
    GameObject*       pSkeletal = manager.createGameObject( hashed_string( "SkeletalMesh" ) );
    GameObject*       pEmpty    = manager.createGameObject( hashed_string( "Empty" ) );
    SW_ASSERT_TRUE( pPlain != nullptr && pSkeletal != nullptr && pEmpty != nullptr );
    SW_ASSERT_NOT_NULL( pPlain->addComponent<MeshComponent>() );
    SW_ASSERT_NOT_NULL( pSkeletal->addComponent<SkeletalMeshComponent>() );

    // 걸러 보기가 없으면 늘 통과한다
    SW_EXPECT_TRUE( MeshModifierParamEdit::matchesComponentHint( *pEmpty, nullptr ) );
    SW_EXPECT_TRUE( MeshModifierParamEdit::matchesComponentHint( *pEmpty, "" ) );

    // 기반 타입 이름은 파생 컴포넌트를 가진 오브젝트도 잡는다
    SW_EXPECT_TRUE( MeshModifierParamEdit::matchesComponentHint( *pPlain, "MeshComponent" ) );
    SW_EXPECT_TRUE( MeshModifierParamEdit::matchesComponentHint( *pSkeletal, "MeshComponent" ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::matchesComponentHint( *pEmpty, "MeshComponent" ) );

    // 파생 타입 이름은 기반만 가진 오브젝트를 잡지 않는다
    SW_EXPECT_TRUE( MeshModifierParamEdit::matchesComponentHint( *pSkeletal, "SkeletalMeshComponent" ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::matchesComponentHint( *pPlain, "SkeletalMeshComponent" ) );

    // 등록되지 않은 이름은 이름 그대로 찾으므로 아무도 통과하지 못한다
    SW_EXPECT_FALSE( MeshModifierParamEdit::matchesComponentHint( *pPlain, "NoSuchComponent" ) );
}
