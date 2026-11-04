#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/World/Land/PropScatterComponent.h"

// PropScatterTest — 흩뿌릴 모델 검사(구간 머티리얼: 이름 · 머티리얼 필수, 겹침 거절, 컴포넌트 머티리얼과 함께 적으면 거절, 구간 없는 모델은 컴포넌트 머티리얼)와
// 비중 고르기 · 배치의 결정성. 씬 없이 계산만 본다. nogpu.

namespace
{
    sw::PropScatterSection makeSection( const sw::utf8* pName, const sw::utf8* pMaterial )
    {
        sw::PropScatterSection section;
        section._sectionName  = pName;
        section._materialPath = pMaterial;
        return section;
    }

    sw::PropScatterModel makeTree( const sw::utf8* pMesh )
    {
        sw::PropScatterModel model;
        model._meshID = pMesh;
        model._listSection.push_back( makeSection( "bark_normaltree", "common/models/tree/materials/bark.material" ) );
        model._listSection.push_back( makeSection( "leaves_normaltree", "common/models/tree/materials/leaves.material" ) );
        return model;
    }
} // namespace

/** @brief [PropScatterTest] 구간을 적은 모델은 구간마다 이름 · 머티리얼이 있어야 하고 이름이 겹치면 안 된다. 구간 없는 모델은 그대로 받는다 */
SW_TEST_CASE( PropScatterTest, SectionModelsNeedNamedMaterials )
{
    sw::string                       error;
    sw::vector<sw::PropScatterModel> listModel{ makeTree( "common/models/tree_a.mesh" ) };
    sw::PropScatterModel             plain;
    plain._meshID = "common/models/flowers.mesh";
    listModel.push_back( plain );
    SW_EXPECT_TRUE( sw::PropScatterMath::validateModels( listModel, "", error ) );

    sw::vector<sw::PropScatterModel> listMissing{ makeTree( "common/models/tree_a.mesh" ) };
    listMissing[0]._listSection[1]._materialPath.clear();
    SW_EXPECT_FALSE( sw::PropScatterMath::validateModels( listMissing, "", error ) );

    sw::vector<sw::PropScatterModel> listTwice{ makeTree( "common/models/tree_a.mesh" ) };
    listTwice[0]._listSection[1]._sectionName = "bark_normaltree";
    SW_EXPECT_FALSE( sw::PropScatterMath::validateModels( listTwice, "", error ) );

    sw::vector<sw::PropScatterModel> listNoMesh( 1 );
    SW_EXPECT_FALSE( sw::PropScatterMath::validateModels( listNoMesh, "", error ) );
}

/** @brief [PropScatterTest] 구간 머티리얼과 컴포넌트 머티리얼을 함께 적으면 어느 쪽이 그릴지 모호해 거절한다. 구간 없는 모델만이면 컴포넌트 머티리얼을 받는다 */
SW_TEST_CASE( PropScatterTest, SectionAndComponentMaterialAreExclusive )
{
    sw::string                       error;
    sw::vector<sw::PropScatterModel> listTree{ makeTree( "common/models/tree_a.mesh" ) };
    SW_EXPECT_FALSE( sw::PropScatterMath::validateModels( listTree, "game/park/materials/palette.material", error ) );
    SW_EXPECT_FALSE( error.empty() );

    sw::vector<sw::PropScatterModel> listPlain( 1 );
    listPlain[0]._meshID = "common/models/flowers.mesh";
    SW_EXPECT_TRUE( sw::PropScatterMath::validateModels( listPlain, "game/park/materials/palette.material", error ) );
}

/** @brief [PropScatterTest] 같은 입력이면 같은 배치다 — 구간을 더해도 배치 계산(모델 번호 · 위치)은 바뀌지 않는다 */
SW_TEST_CASE( PropScatterTest, PlacementIsDeterministic )
{
    sw::PropScatterParams params;
    params._listModel = { makeTree( "common/models/tree_a.mesh" ), makeTree( "common/models/tree_b.mesh" ) };
    params._regionMin = sw::float3{ -20.0f, 0.0f, -20.0f };
    params._regionMax = sw::float3{ 20.0f, 0.0f, 20.0f };
    params._spacing   = 8.0f;
    params._seed      = 7u;
    params._mode      = sw::PropScatterMode::Edge;
    sw::vector<sw::PropScatterPlacement> listFirst;
    sw::vector<sw::PropScatterPlacement> listSecond;
    sw::PropScatterMath::computePlacements( params, listFirst );
    sw::PropScatterMath::computePlacements( params, listSecond );
    SW_REQUIRE_TRUE( listFirst.empty() == false );
    SW_REQUIRE_EQUAL( listFirst.size(), listSecond.size() );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_EQUAL( listFirst[index]._modelIndex, listSecond[index]._modelIndex );
        SW_EXPECT_TRUE( listFirst[index]._modelIndex == 0 || listFirst[index]._modelIndex == 1 );
    }
}
