#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/EditorNodeGraphRules.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TestEditorNodeGraphRulesInternal
    {
        static constexpr float32 kTolerance = 1e-4f;

        static vector<EditorGraphNodeRect> makeThreeNodes()
        {
            return {
                EditorGraphNodeRect{1,   float2{ 10.0f, 50.0f }, float2{ 100.0f, 40.0f }},
                EditorGraphNodeRect{2,   float2{ 300.0f, 0.0f },  float2{ 60.0f, 20.0f }},
                EditorGraphNodeRect{3, float2{ 150.0f, 200.0f },  float2{ 40.0f, 80.0f }},
            };
        }
    };
} // namespace

/**
 * @brief [EditorNodeGraphRulesTest] 왼쪽 · 위 맞추기는 가장 바깥 노드의 끝으로 모으고, 노드 하나는 맞출 것이 없다
 */
SW_TEST_CASE( EditorNodeGraphRulesTest, AlignLeftAndTopUseOuterEdge )
{
    vector<EditorGraphNodeRect> listRect = TestEditorNodeGraphRulesInternal::makeThreeNodes();
    SW_REQUIRE_TRUE( EditorNodeGraphRules::alignNodes( listRect, EditorGraphAlign::Left ) );
    for ( const EditorGraphNodeRect& rect : listRect )
    {
        SW_EXPECT_NEAR( 10.0f, rect._position._x, TestEditorNodeGraphRulesInternal::kTolerance );
    }
    SW_REQUIRE_TRUE( EditorNodeGraphRules::alignNodes( listRect, EditorGraphAlign::Top ) );
    for ( const EditorGraphNodeRect& rect : listRect )
    {
        SW_EXPECT_NEAR( 0.0f, rect._position._y, TestEditorNodeGraphRulesInternal::kTolerance );
    }

    vector<EditorGraphNodeRect> listSingle{
        EditorGraphNodeRect{ 1, float2{ 5.0f, 5.0f }, float2{ 10.0f, 10.0f } }
    };
    SW_EXPECT_FALSE( EditorNodeGraphRules::alignNodes( listSingle, EditorGraphAlign::Left ) );
}

/**
 * @brief [EditorNodeGraphRulesTest] 나누기는 양 끝을 두고 노드 사이 빈칸을 같게 하며, 노드 둘이면 하지 않는다
 */
SW_TEST_CASE( EditorNodeGraphRulesTest, DistributeKeepsEndsAndEqualGaps )
{
    vector<EditorGraphNodeRect> listRect = TestEditorNodeGraphRulesInternal::makeThreeNodes();
    SW_REQUIRE_TRUE( EditorNodeGraphRules::alignNodes( listRect, EditorGraphAlign::DistributeHorizontal ) );
    // 가로 범위 10 ~ 360, 폭 합 200 → 빈칸 (350 - 200) / 2 = 75. 순서: 1(10) → 3(185) → 2(300)
    SW_EXPECT_NEAR( 10.0f, listRect[0]._position._x, TestEditorNodeGraphRulesInternal::kTolerance );
    SW_EXPECT_NEAR( 185.0f, listRect[2]._position._x, TestEditorNodeGraphRulesInternal::kTolerance );
    SW_EXPECT_NEAR( 300.0f, listRect[1]._position._x, TestEditorNodeGraphRulesInternal::kTolerance );

    vector<EditorGraphNodeRect> listTwo{ listRect[0], listRect[1] };
    SW_EXPECT_FALSE( EditorNodeGraphRules::alignNodes( listTwo, EditorGraphAlign::DistributeVertical ) );
}

/**
 * @brief [EditorNodeGraphRulesTest] 미니맵 변환은 노드와 보이는 범위를 모두 담고, 되돌리면 같은 캔버스 점이다
 */
SW_TEST_CASE( EditorNodeGraphRulesTest, MinimapFitsContentAndRoundTrips )
{
    const vector<EditorGraphNodeRect> listRect = TestEditorNodeGraphRulesInternal::makeThreeNodes();
    EditorGraphMinimapTransform       transform{};
    SW_REQUIRE_TRUE( EditorNodeGraphRules::fitMinimap( listRect, float2{ 0.0f, 0.0f }, float2{ 100.0f, 100.0f }, float2{ 180.0f, 120.0f }, transform ) );
    // 범위 0 ~ 360 × 0 ~ 280 → 배율 min(180/360, 120/280)
    SW_EXPECT_NEAR( 120.0f / 280.0f, transform._scale, TestEditorNodeGraphRulesInternal::kTolerance );
    const float2 mapPoint = EditorNodeGraphRules::canvasToMinimap( transform, float2{ 150.0f, 200.0f } );
    const float2 back     = EditorNodeGraphRules::minimapToCanvas( transform, mapPoint );
    SW_EXPECT_NEAR( 150.0f, back._x, TestEditorNodeGraphRulesInternal::kTolerance );
    SW_EXPECT_NEAR( 200.0f, back._y, TestEditorNodeGraphRulesInternal::kTolerance );
    SW_EXPECT_EQUAL( 3, EditorNodeGraphRules::findNearestNode( listRect, float2{ 170.0f, 240.0f } ) );
    SW_EXPECT_FALSE( EditorNodeGraphRules::fitMinimap( listRect, float2{}, float2{}, float2{ 0.0f, 120.0f }, transform ) );
}
