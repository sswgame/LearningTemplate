// 카탈로그 곡선 도우미 — 중첩된 곡선 찾기(모양으로 판정), 색 그래디언트 묶기, 표본, 키 더하기(빈 채널 포함)와 키 칸 편집.
#include "pch.h"

#include "Editor/Panels/Catalog/CatalogCurveModel.h"
#include "Editor/Panels/Catalog/CatalogDocument.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    // 엔진 표본 효과(`engine/vfx/sparks.vfx.xml`)와 같은 모양: 방출기 하나에 크기 곡선 하나와 색 네 채널(알파는 키 둘, 파랑은 빈 채널).
    constexpr const utf8* kEffect =
        "<VfxEffectDef>\n"
        "\t<_listEmitter>\n"
        "\t\t<VfxEmitterDef _name=\"Smoke\" _maxParticles=\"32\">\n"
        "\t\t\t<_sizeOverLife><_listKey><FloatCurveKey _time=\"0\" _value=\"1\" _interpolation=\"Linear\"/><FloatCurveKey _time=\"2\" _value=\"3\" _interpolation=\"Linear\"/></_listKey></_sizeOverLife>\n"
        "\t\t\t<_colorOverLife>\n"
        "\t\t\t\t<_red><_listKey><FloatCurveKey _time=\"0\" _value=\"1\" _interpolation=\"Linear\"/></_listKey></_red>\n"
        "\t\t\t\t<_green><_listKey><FloatCurveKey _time=\"0\" _value=\"0.5\" _interpolation=\"Linear\"/></_listKey></_green>\n"
        "\t\t\t\t<_blue/>\n"
        "\t\t\t\t<_alpha><_listKey><FloatCurveKey _time=\"0\" _value=\"1\" _interpolation=\"Linear\"/><FloatCurveKey _time=\"1\" _value=\"0\" _interpolation=\"Linear\"/></_listKey></_alpha>\n"
        "\t\t\t</_colorOverLife>\n"
        "\t\t</VfxEmitterDef>\n"
        "\t</_listEmitter>\n"
        "</VfxEffectDef>\n";

    const CatalogCurve* findCurve( const vector<CatalogCurve>& listCurve, string_view channel )
    {
        for ( const CatalogCurve& curve : listCurve )
        {
            if ( string_view{ curve._channel } == channel )
                return &curve;
        }
        return nullptr;
    }
} // namespace

/**
 * @brief [CatalogCurveModelTest] 한 행 아래의 곡선을 모양(`_listKey` 자식)으로 찾고 경로 이름표를 붙인다. 빈 색 채널(`<_blue/>`)은 형제에 곡선이 있을 때만 곡선으로 본다
 * @details 변이: 이름으로 곡선을 판정하면(`_sizeOverLife` 만 찾으면) 색 채널이 빠지고, 빈 채널을 거르면 파랑 키를 더할 길이 없다.
 */
SW_TEST_CASE( CatalogCurveModelTest, CollectFindsNestedCurvesByShape )
{
    CatalogDocument document;
    SW_ASSERT_TRUE( document.load( kEffect ) );
    const int32          listEmitterRow = document.getGroups()[0]._listRow[0];
    vector<CatalogCurve> listCurve;
    CatalogCurveModel::collectCurves( document.getParsed(), listEmitterRow, listCurve );
    SW_ASSERT_EQUAL( size_t( 5 ), listCurve.size() ); // 크기 + 색 네 채널(빈 파랑 포함)
    const CatalogCurve* pSize = findCurve( listCurve, "_sizeOverLife" );
    SW_ASSERT_TRUE( pSize != nullptr );
    SW_EXPECT_EQUAL( pSize->_label, string( "VfxEmitterDef(Smoke)/_sizeOverLife" ) );
    SW_EXPECT_EQUAL( pSize->_listKey.size(), size_t( 2 ) );
    const CatalogCurve* pBlue = findCurve( listCurve, "_blue" );
    SW_ASSERT_TRUE( pBlue != nullptr );
    SW_EXPECT_TRUE( pBlue->_listKey.empty() );

    vector<CatalogGradient> listGradient;
    CatalogCurveModel::collectGradients( listCurve, listGradient );
    SW_ASSERT_EQUAL( size_t( 1 ), listGradient.size() );
    SW_EXPECT_EQUAL( listGradient[0]._label, string( "VfxEmitterDef(Smoke)/_colorOverLife" ) );
}

/**
 * @brief [CatalogCurveModelTest] 표본은 엔진 곡선(`FloatCurve`)과 같은 값이다 - 직선 보간 크기 곡선의 가운데는 키 두 값의 평균이고, 키가 없는 곡선은 표본이 없다
 */
SW_TEST_CASE( CatalogCurveModelTest, SamplesMatchTheEngineCurve )
{
    CatalogDocument document;
    SW_ASSERT_TRUE( document.load( kEffect ) );
    vector<CatalogCurve> listCurve;
    CatalogCurveModel::collectCurves( document.getParsed(), document.getGroups()[0]._listRow[0], listCurve );
    const CatalogCurve* pSize = findCurve( listCurve, "_sizeOverLife" );
    SW_ASSERT_TRUE( pSize != nullptr );
    SW_EXPECT_EQUAL( 2.0f, CatalogCurveModel::computeEndTime( *pSize ) );
    vector<float32> listValue;
    SW_ASSERT_TRUE( CatalogCurveModel::sample( *pSize, 2.0f, 3, listValue ) ); // 시각 0 · 1 · 2
    SW_ASSERT_EQUAL( size_t( 3 ), listValue.size() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listValue[0], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, listValue[1], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, listValue[2], 1e-4f );
    const CatalogCurve* pBlue = findCurve( listCurve, "_blue" );
    SW_ASSERT_TRUE( pBlue != nullptr );
    SW_EXPECT_FALSE( CatalogCurveModel::sample( *pBlue, 1.0f, 3, listValue ) );
}

/**
 * @brief [CatalogCurveModelTest] 키 칸 편집은 셀 편집과 같은 길이고, 빈 채널에 키를 더하면 `_listKey` 와 키가 만들어지며, 키를 빼면 줄어든다 - 주석과 들여쓰기는 남는다
 * @details 변이: 빈 채널에서 `_listKey` 를 만들지 않으면 키 더하기가 실패하고, 키 값을 `setCell` 이 아닌 원문 통째 교체로 고치면 되돌리기가 한 단계에 안 맞는다.
 */
SW_TEST_CASE( CatalogCurveModelTest, EditAddAndRemoveKeys )
{
    CatalogDocument document;
    SW_ASSERT_TRUE( document.load( kEffect ) );
    const int32          owner = document.getGroups()[0]._listRow[0];
    vector<CatalogCurve> listCurve;
    CatalogCurveModel::collectCurves( document.getParsed(), owner, listCurve );
    const CatalogCurve* pSize = findCurve( listCurve, "_sizeOverLife" );
    SW_ASSERT_TRUE( pSize != nullptr );
    SW_ASSERT_TRUE( document.setCell( pSize->_listKey[1]._elementIndex, "_value", "5" ) );

    CatalogCurveModel::collectCurves( document.getParsed(), owner, listCurve );
    SW_ASSERT_EQUAL( 5.0f, findCurve( listCurve, "_sizeOverLife" )->_listKey[1]._value );

    const CatalogCurve* pBlue = findCurve( listCurve, "_blue" );
    SW_ASSERT_TRUE( pBlue != nullptr );
    SW_ASSERT_TRUE( CatalogCurveModel::addKey( document, pBlue->_elementIndex ) );
    CatalogCurveModel::collectCurves( document.getParsed(), owner, listCurve );
    pBlue = findCurve( listCurve, "_blue" );
    SW_ASSERT_TRUE( pBlue != nullptr );
    SW_ASSERT_EQUAL( size_t( 1 ), pBlue->_listKey.size() );
    SW_EXPECT_TRUE( pBlue->_listKey[0]._bValid );
    SW_EXPECT_EQUAL( 1.0f, pBlue->_listKey[0]._value ); // 키가 없던 곡선의 첫 키 값

    const CatalogCurve* pAlpha = findCurve( listCurve, "_alpha" );
    SW_ASSERT_TRUE( pAlpha != nullptr );
    SW_ASSERT_TRUE( CatalogCurveModel::addKey( document, pAlpha->_elementIndex ) );
    CatalogCurveModel::collectCurves( document.getParsed(), owner, listCurve );
    pAlpha = findCurve( listCurve, "_alpha" );
    SW_ASSERT_EQUAL( size_t( 3 ), pAlpha->_listKey.size() );
    SW_EXPECT_NEAR_EQUAL( 1.1f, pAlpha->_listKey[2]._time, 1e-4f ); // 마지막 키(1) 뒤 한 걸음

    SW_ASSERT_TRUE( document.removeRow( pAlpha->_listKey[2]._elementIndex ) );
    CatalogCurveModel::collectCurves( document.getParsed(), owner, listCurve );
    SW_EXPECT_EQUAL( size_t( 2 ), findCurve( listCurve, "_alpha" )->_listKey.size() );

    // 모든 편집은 되돌리기 한 단계씩이다(빈 채널의 키 더하기는 `_listKey` 와 키 두 단계).
    uint32 undoCount = 0;
    while ( document.canUndo() )
    {
        document.undo();
        ++undoCount;
    }
    SW_EXPECT_EQUAL( 5u, undoCount ); // 값 고침 1 + 파랑 키 더하기 2 + 알파 키 더하기 1 + 알파 키 빼기 1
    SW_EXPECT_FALSE( document.isDirty() );
}
