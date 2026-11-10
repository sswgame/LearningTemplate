// BodyDataEditTest — 캐릭터 편집 창 몸 탭의 데이터 파일 편집(채널 매핑 · 지글 · 체액 경로 · 구간 별칭)이 원소 열쇠 경로로 고치고 되돌리기 한 단계씩이며 주석을 지킨다,
// 몸 표면 집기가 지금 포즈 · 부품 표면까지 보고 맞은 구간 · UV 를 고른다, 배치 층이 집은 UV 로 옮겨진다(nogpu).
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodyCustomizationViewModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodyDataEditModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodySurfacePick.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 속성 글을 수로 읽습니다(쓰는 쪽의 자릿수 형식에 기대지 않는다). */
    float32 readNumber( const string& text )
    {
        float32 value = -1.0f;
        (void)StringUtil::parseFloat( text, value );
        return value;
    }

    constexpr const utf8* kProfilesText = R"(<?xml version="1.0" encoding="utf-8"?>
<!-- 설명 주석 — 저장해도 남아야 한다 -->
<BodyProfileCatalog>
  <BodyProfile id="Base"><Jiggle group="Torso" stiffness="0.35"/><Fluid name="TearL" channel="Tear" trigger="Cry" path="0.4 0.4; 0.4 0.5"/></BodyProfile>
  <BodyProfile id="Hero" _base="Base"/>
</BodyProfileCatalog>)";
} // namespace

/**
 * @brief [BodyDataEditTest] 파생 프로필의 지글을 처음 고치면 그 프로필에 덮어쓰기 원소가 생기고, 끌기는 한 단계로 합쳐지며, 되돌리면 원래 글이다
 */
SW_TEST_CASE( BodyDataEditTest, JiggleOverrideCoalescesAndUndoes )
{
    BodyDataEditModel profiles;
    SW_REQUIRE_TRUE( profiles.openText( kProfilesText, {} ) );
    const string opened = profiles.getText();
    SW_EXPECT_FALSE( profiles.isDirty() );
    SW_EXPECT_TRUE( opened.find( "설명 주석" ) != string::npos ); // 주석을 남긴 채 읽는다

    SW_REQUIRE_TRUE( BodyCustomizationViewModel::setJiggleValue( profiles, hashed_string( "Hero" ), hashed_string( "Torso" ), "stiffness", 0.5f, true ) );
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::setJiggleValue( profiles, hashed_string( "Hero" ), hashed_string( "Torso" ), "stiffness", 0.6f, true ) );
    const BodyDataElementKey arrHero[] = {
        BodyDataElementKey{"BodyProfile",    "id",  "Hero"},
        BodyDataElementKey{     "Jiggle", "group", "Torso"}
    };
    const BodyDataElementKey arrBase[] = {
        BodyDataElementKey{"BodyProfile",    "id",  "Base"},
        BodyDataElementKey{     "Jiggle", "group", "Torso"}
    };
    SW_EXPECT_NEAR( 0.6f, readNumber( profiles.readAttribute( arrHero, "stiffness" ) ), 1e-5f );
    SW_EXPECT_EQUAL( string( "0.35" ), profiles.readAttribute( arrBase, "stiffness" ) ); // 부모는 그대로다
    SW_EXPECT_TRUE( profiles.isDirty() );
    SW_EXPECT_TRUE( profiles.getText().find( "설명 주석" ) != string::npos );
    // 같은 값은 기록하지 않는다.
    SW_EXPECT_FALSE( BodyCustomizationViewModel::setJiggleValue( profiles, hashed_string( "Hero" ), hashed_string( "Torso" ), "stiffness", 0.6f, true ) );

    profiles.endContinuousEdit();
    SW_REQUIRE_TRUE( profiles.undo() ); // 끈 두 값이 한 단계다
    SW_EXPECT_FALSE( profiles.hasElement( arrHero ) );
    SW_EXPECT_EQUAL( opened, profiles.getText() );
    SW_EXPECT_FALSE( profiles.isDirty() );
    SW_REQUIRE_TRUE( profiles.redo() );
    SW_EXPECT_NEAR( 0.6f, readNumber( profiles.readAttribute( arrHero, "stiffness" ) ), 1e-5f );
}

/**
 * @brief [BodyDataEditTest] 체액 경로는 "u v; u v" 로 쓰고 점이 둘 미만이면 거절한다, 채널 축 더하기 · 받지 않음 선언 · 구간 별칭이 그 원소를 짚는다
 */
SW_TEST_CASE( BodyDataEditTest, FluidPathAxisAndAliasEdits )
{
    BodyDataEditModel profiles;
    SW_REQUIRE_TRUE( profiles.openText( kProfilesText, {} ) );
    const float2 arrPoint[] = {
        float2{0.25f,  0.5f},
        float2{ 0.5f, 0.75f},
        float2{ 0.5f,  1.0f}
    };
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::setFluidPath( profiles, hashed_string( "Base" ), hashed_string( "TearL" ), arrPoint, false ) );
    const BodyDataElementKey arrFluid[] = {
        BodyDataElementKey{"BodyProfile",   "id",  "Base"},
        BodyDataElementKey{      "Fluid", "name", "TearL"}
    };
    const string path = profiles.readAttribute( arrFluid, "path" );
    SW_EXPECT_EQUAL( size_t{ 2 }, static_cast<size_t>( std::count( path.begin(), path.end(), ';' ) ) ); // 점 셋 = 구분 둘
    SW_EXPECT_NEAR( 0.25f, readNumber( path.substr( 0, path.find( ' ' ) ) ), 1e-5f );
    SW_EXPECT_FALSE( BodyCustomizationViewModel::setFluidPath( profiles, hashed_string( "Base" ), hashed_string( "TearL" ), vector_reference<const float2>( arrPoint, 1 ), false ) );

    BodyDataEditModel shapes;
    SW_REQUIRE_TRUE( shapes.openText( R"(<BodyShapeSet><Axis name="Waist.Width" min="-1" max="1" default="0"/></BodyShapeSet>)", {} ) );
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::addAxis( shapes, hashed_string( "Bust.Lift" ) ) );
    const BodyDataElementKey arrAxis[] = {
        BodyDataElementKey{ "Axis", "name", "Bust.Lift" }
    };
    SW_EXPECT_NEAR( BodyCustomizationViewModel::kNewAxisMin, readNumber( shapes.readAttribute( arrAxis, "min" ) ), 1e-5f );
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::declareUnsupported( shapes, hashed_string( "Tail.Length" ) ) );
    SW_EXPECT_FALSE( BodyCustomizationViewModel::declareUnsupported( shapes, hashed_string( "Tail.Length" ) ) ); // 이미 있다
    SW_REQUIRE_TRUE( shapes.undo() );
    SW_REQUIRE_TRUE( shapes.undo() );
    SW_EXPECT_FALSE( shapes.hasElement( arrAxis ) );

    BodyDataEditModel visuals;
    SW_REQUIRE_TRUE( visuals.openText( R"(<ItemVisualCatalog><ItemVisual id="suit"><Part name="Body" kind="Skinned" mesh="suit.mesh"><Section name="mi_cloth" material="cloth.material"/></Part></ItemVisual></ItemVisualCatalog>)", {} ) );
    BodyCustomizationViewState::SectionRow row;
    row._visual  = hashed_string( "suit" );
    row._part    = hashed_string( "Body" );
    row._section = hashed_string( "mi_cloth" );
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::setSectionAlias( visuals, row, "cloth_main" ) );
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::setSectionDyeable( visuals, row, false ) );
    const BodyDataElementKey arrSection[] = {
        BodyDataElementKey{"ItemVisual",   "id",     "suit"},
        BodyDataElementKey{      "Part", "name",     "Body"},
        BodyDataElementKey{   "Section", "name", "mi_cloth"}
    };
    SW_EXPECT_EQUAL( string( "cloth_main" ), visuals.readAttribute( arrSection, "alias" ) );
    SW_EXPECT_EQUAL( string( "false" ), visuals.readAttribute( arrSection, "dyeable" ) );
    SW_EXPECT_EQUAL( string( "cloth.material" ), visuals.readAttribute( arrSection, "material" ) );
}

/**
 * @brief [BodyDataEditTest] 표면 집기는 가장 가까운 삼각형의 구간과 무게중심 UV 를 고르고, 비껴가면 false 다
 * @details 같은 xy 의 두 사각형(z = 0 구간 front, z = 1 구간 back)에 -z 쪽으로 쏜 광선은 z = 1 의 back 을 먼저 맞힌다.
 */
SW_TEST_CASE( BodyDataEditTest, SurfacePickFindsNearestSectionAndUV )
{
    vector<RHIVertex> listVertex( 8 );
    const float32     arrCorner[4][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    for ( uint32 layer = 0; layer < 2; ++layer )
    {
        for ( uint32 corner = 0; corner < 4; ++corner )
        {
            RHIVertex& vertex      = listVertex[layer * 4 + corner];
            vertex._arrPosition[0] = arrCorner[corner][0];
            vertex._arrPosition[1] = arrCorner[corner][1];
            vertex._arrPosition[2] = static_cast<float32>( layer );
            vertex._arrUV[0]       = arrCorner[corner][0];
            vertex._arrUV[1]       = arrCorner[corner][1];
        }
    }
    const vector<uint32>      listIndex{ 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
    const vector<MeshSection> listSection{
        MeshSection{hashed_string( "front" ), 0, 6, MeshPersonVisibility::Both},
        MeshSection{ hashed_string( "back" ), 6, 6, MeshPersonVisibility::Both}
    };

    vector<BodyPickSurface> listSurface( 1 );
    BodyPickSurface::build( listVertex, {}, {}, float4x4::Identity, listSurface[0] );
    listSurface[0]._listIndex   = listIndex;
    listSurface[0]._listSection = listSection;

    BodySurfaceHit hit{};
    SW_REQUIRE_TRUE( BodySurfaceHit::pick( listSurface, float3{ 0.25f, 0.75f, 5.0f }, float3{ 0.0f, 0.0f, -1.0f }, hit ) );
    SW_EXPECT_EQUAL( 0u, hit._surface );
    SW_EXPECT_TRUE( hit._section == 1u );
    SW_EXPECT_NEAR( 4.0f, hit._distance, 1e-4f );
    SW_EXPECT_NEAR( 0.25f, hit._uv._x, 1e-4f );
    SW_EXPECT_NEAR( 0.75f, hit._uv._y, 1e-4f );
    SW_EXPECT_FALSE( BodySurfaceHit::pick( listSurface, float3{ 3.0f, 3.0f, 5.0f }, float3{ 0.0f, 0.0f, -1.0f }, hit ) );
    SW_EXPECT_TRUE( BodySurfaceHit::findSection( listSection, 9 ).has_value() == false );
}

/**
 * @brief [BodyDataEditTest] 표면 집기는 지금 포즈(스킨 팔레트로 CPU 스키닝한 자리)로 맞추고, 여러 표면(몸 · 부품) 중 가장 가까운 것을 고른다
 * @details 본 0 에 묶인 사각형을 팔레트가 x 로 2 옮기면 바인드 자리(x 0.25)는 비껴가고 옮긴 자리(x 2.25)를 맞힌다. 앞에 둔 둘째 표면(부품)이 몸을 가린다.
 */
SW_TEST_CASE( BodyDataEditTest, SurfacePickFollowsPoseAndParts )
{
    vector<RHIVertex> listVertex( 4 );
    const float32     arrCorner[4][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    for ( uint32 corner = 0; corner < 4; ++corner )
    {
        listVertex[corner]._arrPosition[0] = arrCorner[corner][0];
        listVertex[corner]._arrPosition[1] = arrCorner[corner][1];
        listVertex[corner]._arrUV[0]       = arrCorner[corner][0];
        listVertex[corner]._arrUV[1]       = arrCorner[corner][1];
    }
    const vector<uint32>         listIndex{ 0, 1, 2, 0, 2, 3 };
    const vector<MeshSkinVertex> listSkin( 4 ); // 기본 영향 — 본 0 에 가중치 1
    const vector<float4x4>       listPalette{ float4x4::makeTranslation( 2.0f, 0.0f, 0.0f ) };

    vector<BodyPickSurface> listSurface( 1 );
    BodyPickSurface::build( listVertex, listSkin, listPalette, float4x4::Identity, listSurface[0] );
    listSurface[0]._listIndex = listIndex;

    BodySurfaceHit hit{};
    SW_EXPECT_FALSE( BodySurfaceHit::pick( listSurface, float3{ 0.25f, 0.5f, 5.0f }, float3{ 0.0f, 0.0f, -1.0f }, hit ) );
    SW_REQUIRE_TRUE( BodySurfaceHit::pick( listSurface, float3{ 2.25f, 0.5f, 5.0f }, float3{ 0.0f, 0.0f, -1.0f }, hit ) );
    SW_EXPECT_NEAR( 2.25f, hit._position._x, 1e-4f );
    SW_EXPECT_NEAR( 0.25f, hit._uv._x, 1e-4f );

    // 부품 표면 — 같은 사각형을 몸 앞(z = 1)에 단단하게 둔다.
    listSurface.emplace_back();
    BodyPickSurface::build( listVertex, {}, {}, float4x4::makeTranslation( 2.0f, 0.0f, 1.0f ), listSurface[1] );
    listSurface[1]._listIndex = listIndex;
    SW_REQUIRE_TRUE( BodySurfaceHit::pick( listSurface, float3{ 2.25f, 0.5f, 5.0f }, float3{ 0.0f, 0.0f, -1.0f }, hit ) );
    SW_EXPECT_EQUAL( 1u, hit._surface );
    SW_EXPECT_NEAR( 4.0f, hit._distance, 1e-4f );
}

/**
 * @brief [BodyDataEditTest] 배치 층은 집은 UV 로 옮겨지고(범위 밖은 끝에서 자른다) 끄는 동안 한 되돌리기 단계다
 */
SW_TEST_CASE( BodyDataEditTest, PlaceSkinLayerFromPickedUV )
{
    AppearanceEditModel model;
    model.setSelection( AppearanceSelection{} );
    SW_REQUIRE_TRUE( model.addSkinLayer( hashed_string( "rose" ), float4{ 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f ) );
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::placeSkinLayer( model, 0, float2{ 0.3f, 0.4f }, true ) );
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::placeSkinLayer( model, 0, float2{ 1.5f, 0.6f }, true ) );
    SW_EXPECT_NEAR( 1.0f, model.getSelection()._listSkinLayer[0]._offset._x, 1e-6f );
    SW_EXPECT_NEAR( 0.6f, model.getSelection()._listSkinLayer[0]._offset._y, 1e-4f );
    SW_EXPECT_FALSE( BodyCustomizationViewModel::placeSkinLayer( model, 3, float2{ 0.5f, 0.5f }, false ) );
    model.endContinuousEdit();
    SW_REQUIRE_TRUE( model.undo() ); // 끈 두 배치가 한 단계다
    SW_EXPECT_NEAR( 0.5f, model.getSelection()._listSkinLayer[0]._offset._x, 1e-6f );
}
