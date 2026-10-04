#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "GameFramework/Components/PropScatterComponent.h"

#include "TestFramework/TestFramework.h"

// 장식 흩뿌리기 — 결정성 · 제외 원 · 개수 · 비중 고르기, 씬 파일의 설정 모양.

using namespace sw;

namespace
{
    struct PropScatterTestUtil
    {
        static PropScatterParams makeParkEdge()
        {
            PropScatterParams params;
            params._listModel = {
                PropScatterModel{"a.mesh", 0.45f},
                PropScatterModel{"b.mesh", 0.35f},
                PropScatterModel{"c.mesh",  0.2f}
            };
            params._regionMin    = float3{ -100.0f, 0.0f, -45.0f };
            params._regionMax    = float3{ 140.0f, 0.0f, 265.0f };
            params._spacing      = 9.0f;
            params._alongJitter  = 3.0f;
            params._inwardJitter = 4.0f;
            params._scaleMin     = 3.0f;
            params._scaleMax     = 4.5f;
            params._seed         = 0x9E3779B9u;
            params._mode         = PropScatterMode::Edge;
            return params;
        }

        static bool isSamePlacement( const PropScatterPlacement& lhs, const PropScatterPlacement& rhs )
        {
            return lhs._modelIndex == rhs._modelIndex && lhs._position._x == rhs._position._x && lhs._position._z == rhs._position._z &&
                   lhs._scale == rhs._scale && lhs._yaw == rhs._yaw;
        }
    };
} // namespace

/**
 * @brief [PropScatterTest] 같은 입력은 같은 배치다(실행마다 같은 공원) · 씨앗이 다르면 배치가 다르다 · 크기와 요는 범위 안이다
 */
SW_TEST_CASE( PropScatterTest, SameSeedGivesTheSameLayout )
{
    PropScatterParams            params = PropScatterTestUtil::makeParkEdge();
    vector<PropScatterPlacement> listFirst;
    vector<PropScatterPlacement> listSecond;
    PropScatterMath::computePlacements( params, listFirst );
    PropScatterMath::computePlacements( params, listSecond );
    SW_ASSERT_EQUAL( listFirst.size(), listSecond.size() );
    SW_ASSERT_TRUE( listFirst.empty() == false );
    bool bSame    = true;
    bool bInRange = true;
    for ( size_t placementIndex = 0; placementIndex < listFirst.size(); ++placementIndex )
    {
        const PropScatterPlacement& placement = listFirst[placementIndex];
        bSame                                 = bSame && PropScatterTestUtil::isSamePlacement( placement, listSecond[placementIndex] );
        bInRange                              = bInRange && 3.0f <= placement._scale && placement._scale <= 4.5f && 0.0f <= placement._yaw && placement._yaw <= MathUtil::Pi * 2.0f;
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( bInRange );

    params._seed = 77u;
    vector<PropScatterPlacement> listOther;
    PropScatterMath::computePlacements( params, listOther );
    bool bAllSame = listOther.size() == listFirst.size();
    for ( size_t placementIndex = 0; bAllSame && placementIndex < listFirst.size(); ++placementIndex )
        bAllSame = PropScatterTestUtil::isSamePlacement( listFirst[placementIndex], listOther[placementIndex] );
    SW_EXPECT_FALSE( bAllSame );
}

/**
 * @brief [PropScatterTest] 가장자리 모드는 네 변에 한 줄씩(아래 · 위 변이 모서리를 맡는다), 안쪽 모드는 격자 한 칸에 하나를 놓는다 · 흔들기는 안쪽으로만
 */
SW_TEST_CASE( PropScatterTest, CountFollowsTheModeAndTheSpacing )
{
    PropScatterParams params = PropScatterTestUtil::makeParkEdge();
    params._regionMin        = float3{ 0.0f, 0.0f, 0.0f };
    params._regionMax        = float3{ 90.0f, 0.0f, 45.0f };
    vector<PropScatterPlacement> listPlacement;
    PropScatterMath::computePlacements( params, listPlacement );
    // x: 0, 9, …, 90 → 11 칸씩 두 줄, z: 9 … 36 → 4 칸씩 두 줄.
    SW_EXPECT_EQUAL( static_cast<size_t>( 11 * 2 + 4 * 2 ), listPlacement.size() );
    bool bInside = true;
    for ( const PropScatterPlacement& placement : listPlacement )
    {
        bInside = bInside && -1.0e-4f <= placement._position._z && placement._position._z <= 45.0f + 1.0e-4f;
        bInside = bInside && -1.0e-4f <= placement._position._x && placement._position._x <= 93.0f + 1.0e-4f; // 줄을 따라서는 끝 칸만 밖으로 흔든다
    }
    SW_EXPECT_TRUE( bInside );

    params._mode = PropScatterMode::Fill;
    PropScatterMath::computePlacements( params, listPlacement );
    SW_EXPECT_EQUAL( static_cast<size_t>( 11 * 6 ), listPlacement.size() ); // z: 0, 9, …, 45

    params._listModel.clear();
    PropScatterMath::computePlacements( params, listPlacement );
    SW_EXPECT_TRUE( listPlacement.empty() );
}

/**
 * @brief [PropScatterTest] 제외 원 안에는 아무것도 놓지 않고, 원 밖의 자리는 그대로다(원이 앞 자리의 난수를 건드리지 않는다)
 */
SW_TEST_CASE( PropScatterTest, ExclusionCirclesStayEmpty )
{
    PropScatterParams            params = PropScatterTestUtil::makeParkEdge();
    vector<PropScatterPlacement> listOpen;
    PropScatterMath::computePlacements( params, listOpen );

    const float3 gate{ 20.0f, 0.0f, -45.0f };
    params._listExclusion = {
        PropScatterExclusion{ gate, 14.0f }
    };
    vector<PropScatterPlacement> listExcluded;
    PropScatterMath::computePlacements( params, listExcluded );
    SW_EXPECT_TRUE( listExcluded.size() < listOpen.size() );
    bool bOutside = true;
    for ( const PropScatterPlacement& placement : listExcluded )
    {
        const float32 deltaX = placement._position._x - gate._x;
        const float32 deltaZ = placement._position._z - gate._z;
        bOutside             = bOutside && deltaX * deltaX + deltaZ * deltaZ >= 14.0f * 14.0f;
    }
    SW_EXPECT_TRUE( bOutside );
    // 첫 자리는 원보다 앞(x = −100)이라 그대로다.
    SW_ASSERT_TRUE( listExcluded.empty() == false );
    SW_EXPECT_TRUE( PropScatterTestUtil::isSamePlacement( listOpen.front(), listExcluded.front() ) );
}

/**
 * @brief [PropScatterTest] 모델은 누적 비중의 경계로 고른다 · 비중 0 은 뽑히지 않는다 · 비중이 모두 0 이면 고르지 못한다
 */
SW_TEST_CASE( PropScatterTest, ModelsArePickedByWeight )
{
    const vector<PropScatterModel> listModel = {
        PropScatterModel{   "a.mesh", 0.45f},
        PropScatterModel{"skip.mesh",  0.0f},
        PropScatterModel{   "b.mesh", 0.35f},
        PropScatterModel{   "c.mesh",  0.2f}
    };
    SW_EXPECT_EQUAL( 0, PropScatterMath::pickModel( listModel, 0.0f ) );
    SW_EXPECT_EQUAL( 0, PropScatterMath::pickModel( listModel, 0.44f ) );
    SW_EXPECT_EQUAL( 2, PropScatterMath::pickModel( listModel, 0.46f ) );
    SW_EXPECT_EQUAL( 2, PropScatterMath::pickModel( listModel, 0.79f ) );
    SW_EXPECT_EQUAL( 3, PropScatterMath::pickModel( listModel, 0.81f ) );
    SW_EXPECT_EQUAL( 3, PropScatterMath::pickModel( listModel, 1.0f ) );
    const vector<PropScatterModel> listZero = {
        PropScatterModel{ "a.mesh", 0.0f }
    };
    SW_EXPECT_EQUAL( -1, PropScatterMath::pickModel( listZero, 0.5f ) );
}

/**
 * @brief [PropScatterTest] 씬 파일의 설정 모양 — 모델 · 제외 원 목록(구조체 원소)과 모드 이름을 읽어 계산 입력으로 옮긴다
 */
SW_TEST_CASE( PropScatterTest, SceneStateFillsTheParams )
{
    const string      xml = "<GameObject _schemaVersion=\"0\" _name=\"Trees\" _bActive=\"true\"><_listComponent>"
                            "<PropScatterComponent _regionMin=\"-1,0,-2\" _regionMax=\"3,0,4\" _spacing=\"2.5\" _seed=\"99\" _mode=\"Fill\" _scaleMin=\"2\" _scaleMax=\"3\">"
                            "<_listModel><PropScatterModel _meshId=\"game/x/models/tree.mesh\" _weight=\"2\"/>"
                            "<PropScatterModel _meshId=\"game/x/models/rock.mesh\" _weight=\"0.5\"/></_listModel>"
                            "<_listExclusion><PropScatterExclusion _center=\"1,0,1\" _radius=\"1.5\"/></_listExclusion>"
                            "</PropScatterComponent></_listComponent></GameObject>";
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Trees" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pObject, xml ) );
    const PropScatterComponent* pScatter = pObject->getComponent<PropScatterComponent>();
    SW_ASSERT_NOT_NULL( pScatter );

    const PropScatterParams params = pScatter->makeParams();
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), params._listModel.size() );
    SW_EXPECT_TRUE( params._listModel[1]._meshId == "game/x/models/rock.mesh" );
    SW_EXPECT_NEAR_EQUAL( 0.5f, params._listModel[1]._weight, 1.0e-6f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), params._listExclusion.size() );
    SW_EXPECT_NEAR_EQUAL( 1.5f, params._listExclusion[0]._radius, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, params._regionMin._z, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, params._spacing, 1.0e-6f );
    SW_EXPECT_EQUAL( 99u, params._seed );
    SW_EXPECT_TRUE( params._mode == PropScatterMode::Fill );
    SW_EXPECT_NEAR_EQUAL( 3.0f, params._scaleMax, 1.0e-6f );
}

/**
 * @brief [PropScatterTest] 규칙 모드는 배치 코어를 쓴다 — 최소 거리 · 결정성을 지키고, 제외 원은 원 제외 영역이 되며, 모델 비중이 항목 비중이고, 평면 높이는 영역 y 다
 */
SW_TEST_CASE( PropScatterTest, RulesModeUsesThePlacementCore )
{
    PropScatterParams params  = PropScatterTestUtil::makeParkEdge();
    params._mode              = PropScatterMode::Rules;
    params._regionMin         = float3{ 0.0f, 1.5f, 0.0f };
    params._regionMax         = float3{ 40.0f, 1.5f, 40.0f };
    params._rule._density     = 0.3f;
    params._rule._minDistance = 2.0f;
    params._rule._seed        = 5u;
    params._listExclusion     = {
        PropScatterExclusion{ float3{ 20.0f, 0.0f, 20.0f }, 8.0f }
    };
    params._listModel[1]._weight = 0.0f; // 두 번째 모델은 뽑히지 않는다
    vector<PropScatterPlacement> listFirst;
    vector<PropScatterPlacement> listSecond;
    PropScatterMath::computePlacements( params, listFirst );
    PropScatterMath::computePlacements( params, listSecond );
    SW_ASSERT_TRUE( listFirst.size() > 50 );
    SW_ASSERT_EQUAL( listFirst.size(), listSecond.size() );
    bool    bRules  = true;
    float32 closest = MathUtil::MaxFloat;
    for ( size_t first = 0; first < listFirst.size(); ++first )
    {
        const PropScatterPlacement& placement = listFirst[first];
        bRules                                = bRules && PropScatterTestUtil::isSamePlacement( placement, listSecond[first] ) && placement._modelIndex != 1 && placement._position._y == 1.5f;
        const float32 deltaX                  = placement._position._x - 20.0f;
        const float32 deltaZ                  = placement._position._z - 20.0f;
        bRules                                = bRules && deltaX * deltaX + deltaZ * deltaZ >= 64.0f;
        for ( size_t second = first + 1; second < listFirst.size(); ++second )
        {
            const float32 gapX = placement._position._x - listFirst[second]._position._x;
            const float32 gapZ = placement._position._z - listFirst[second]._position._z;
            closest            = MathUtil::min( closest, MathUtil::sqrt( gapX * gapX + gapZ * gapZ ) );
        }
    }
    SW_EXPECT_TRUE( bRules );
    SW_EXPECT_TRUE( closest >= 2.0f );
}
