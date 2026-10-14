// 메시 모디파이어 패널의 순수 로직 — NumberList 행 연산(추가 · 삭제 · 복제 · 위아래), 열 선언 검사, 곡선 키 ↔ FloatCurve 변환, 구역 순서, 등록된 열 선언의 너비.
#include "pch.h"

#include "Engine/Utility/FloatCurve.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierParamEdit.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    const vector<float32> kThreeRows{ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
} // namespace

SW_TEST_CASE( MeshModifierPanelLogicTest, RowOperationsKeepRowsWhole )
{
    vector<float32> listNumber = kThreeRows;
    SW_EXPECT_EQUAL( size_t{ 3 }, MeshModifierParamEdit::getRowCount( listNumber, 2 ) );

    SW_ASSERT_TRUE( MeshModifierParamEdit::moveRowUp( listNumber, 2, 1 ) );
    SW_EXPECT_TRUE( ( listNumber == vector<float32>{ 3.0f, 4.0f, 1.0f, 2.0f, 5.0f, 6.0f } ) );
    SW_ASSERT_TRUE( MeshModifierParamEdit::moveRowDown( listNumber, 2, 1 ) );
    SW_EXPECT_TRUE( ( listNumber == vector<float32>{ 3.0f, 4.0f, 5.0f, 6.0f, 1.0f, 2.0f } ) );

    SW_ASSERT_TRUE( MeshModifierParamEdit::duplicateRow( listNumber, 2, 0 ) );
    SW_EXPECT_TRUE( ( listNumber == vector<float32>{ 3.0f, 4.0f, 3.0f, 4.0f, 5.0f, 6.0f, 1.0f, 2.0f } ) );
    SW_ASSERT_TRUE( MeshModifierParamEdit::removeRow( listNumber, 2, 3 ) );
    SW_EXPECT_TRUE( ( listNumber == vector<float32>{ 3.0f, 4.0f, 3.0f, 4.0f, 5.0f, 6.0f } ) );

    MeshModifierParamEdit::appendRow( listNumber, 2 );
    SW_EXPECT_TRUE( ( listNumber == vector<float32>{ 3.0f, 4.0f, 3.0f, 4.0f, 5.0f, 6.0f, 0.0f, 0.0f } ) );
}

SW_TEST_CASE( MeshModifierPanelLogicTest, RowOperationsRejectOutOfRange )
{
    vector<float32> listNumber = kThreeRows;
    SW_EXPECT_FALSE( MeshModifierParamEdit::moveRowUp( listNumber, 2, 0 ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::moveRowDown( listNumber, 2, 2 ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::removeRow( listNumber, 2, 3 ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::duplicateRow( listNumber, 2, 3 ) );
    SW_EXPECT_TRUE( listNumber == kThreeRows );

    // 남는 원소(행이 덜 찬 꼬리)는 행으로 세지 않고, 행을 붙이면 버린다
    vector<float32> listTail{ 1.0f, 2.0f, 3.0f };
    SW_EXPECT_EQUAL( size_t{ 1 }, MeshModifierParamEdit::getRowCount( listTail, 2 ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::removeRow( listTail, 2, 1 ) );
    MeshModifierParamEdit::appendRow( listTail, 2 );
    SW_EXPECT_TRUE( ( listTail == vector<float32>{ 1.0f, 2.0f, 0.0f, 0.0f } ) );
}

SW_TEST_CASE( MeshModifierPanelLogicTest, ColumnLayoutMustMatchStride )
{
    static constexpr MeshModifierListColumn kColumn[] = {
        { "Kind", MeshModifierListColumnKind::Combo, "A\0B\0" },
        { "Center", MeshModifierListColumnKind::Vector3 },
        { "Radius", MeshModifierListColumnKind::Float },
    };
    static constexpr MeshModifierListColumn kBadCombo[] = {
        { "Kind", MeshModifierListColumnKind::Combo },
    };
    MeshModifierParamDef def{
        "List", MeshModifierParamKind::NumberList, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f },
        5
    };
    SW_EXPECT_FALSE( MeshModifierParamEdit::isColumnLayoutValid( def ) ); // 열 선언이 없으면 일반 표
    SW_EXPECT_TRUE( MeshModifierParamEdit::isColumnLayoutValid( def.withColumns( kColumn ) ) );
    def._stride = 4;
    SW_EXPECT_FALSE( MeshModifierParamEdit::isColumnLayoutValid( def.withColumns( kColumn ) ) );
    def._stride = 1;
    SW_EXPECT_FALSE( MeshModifierParamEdit::isColumnLayoutValid( def.withColumns( kBadCombo ) ) );

    SW_EXPECT_EQUAL( 2u, MeshModifierParamEdit::getComboItemCount( "A\0B\0" ) );
    SW_EXPECT_EQUAL( 0u, MeshModifierParamEdit::getComboItemCount( nullptr ) );
}

SW_TEST_CASE( MeshModifierPanelLogicTest, EveryRegisteredColumnLayoutIsValid )
{
    // 열 선언을 붙인 매개변수는 모두 너비 합이 stride 와 맞아야 한다(틀리면 패널이 조용히 일반 표로 돌아간다)
    vector<const MeshModifierRegistration*> listRegistration;
    MeshModifierRegistry::getRegistrations( listRegistration );
    uint32 columnParamCount = 0;
    for ( const MeshModifierRegistration* pRegistration : listRegistration )
    {
        const unique_ptr<MeshModifier> pModifier = MeshModifierRegistry::create( pRegistration->_pID );
        SW_ASSERT_TRUE( pModifier != nullptr );
        for ( const MeshModifierParamDef& def : pModifier->getParamDefs() )
        {
            if ( def._pColumn == nullptr )
                continue;
            ++columnParamCount;
            SW_EXPECT_TRUE( MeshModifierParamEdit::isColumnLayoutValid( def ) );
        }
    }
    SW_EXPECT_TRUE( columnParamCount >= 5u );
}

SW_TEST_CASE( MeshModifierPanelLogicTest, CurveRoundTripIsLinearAndSorted )
{
    const vector<float2> listKey{
        float2{0.0f,  1.0f},
        float2{0.5f, -2.0f},
        float2{1.0f,  3.0f}
    };
    FloatCurve curve{};
    MeshModifierParamEdit::makeFloatCurve( listKey, curve );
    SW_ASSERT_EQUAL( size_t{ 3 }, curve._listKey.size() );
    for ( const FloatCurveKey& key : curve._listKey )
    {
        SW_EXPECT_TRUE( key._interpolation == CurveInterpolation::Linear );
        SW_EXPECT_FALSE( key._bAutoTangent );
    }
    SW_EXPECT_NEAR_EQUAL( -0.5f, curve.evaluate( 0.25f ), 1e-5f ); // 선형 보간: 1 과 -2 의 가운데

    vector<float2> listBack;
    MeshModifierParamEdit::makeCurveKeys( curve, listBack );
    SW_EXPECT_TRUE( listBack == listKey );

    // 편집기가 범위 밖 · 순서가 뒤집힌 키를 내도 매개변수 규칙(x ∈ [0,1], 오름차순)으로 돌아온다
    curve._listKey[0]._time = 1.5f;
    curve._listKey[2]._time = -1.0f;
    MeshModifierParamEdit::makeCurveKeys( curve, listBack );
    SW_ASSERT_EQUAL( size_t{ 3 }, listBack.size() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listBack[0]._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, listBack[0]._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listBack[2]._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listBack[2]._y, 1e-6f );
}

SW_TEST_CASE( MeshModifierPanelLogicTest, GroupOrderFollowsFirstAppearance )
{
    vector<MeshModifierParamDef> listDef;
    listDef.push_back( MeshModifierParamDef{ "A" }.withLayout( "Shape", nullptr ) );
    listDef.push_back( MeshModifierParamDef{ "B" } );
    listDef.push_back( MeshModifierParamDef{ "C" }.withLayout( "Limits", nullptr ) );
    listDef.push_back( MeshModifierParamDef{ "D" }.withLayout( "Shape", nullptr ) );
    vector<string> listGroup;
    MeshModifierParamEdit::makeGroupOrder( listDef, listGroup );
    SW_ASSERT_EQUAL( size_t{ 3 }, listGroup.size() );
    SW_EXPECT_TRUE( listGroup[0].empty() );
    SW_EXPECT_EQUAL( string( "Shape" ), listGroup[1] );
    SW_EXPECT_EQUAL( string( "Limits" ), listGroup[2] );
    SW_EXPECT_TRUE( MeshModifierParamEdit::isInGroup( listDef[1], "" ) );
    SW_EXPECT_TRUE( MeshModifierParamEdit::isInGroup( listDef[3], "Shape" ) );
    SW_EXPECT_FALSE( MeshModifierParamEdit::isInGroup( listDef[0], "" ) );
}
