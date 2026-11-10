#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/CharacterGeometry.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/SocketEditModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct SocketEditModelTestUtil
    {
        /** @brief root → hand 뼈 배열입니다. hand 는 위로 1 m, y 축으로 90 도 돌아 있다. */
        static CharacterBoneArray makeBones()
        {
            CharacterBoneArray bones;
            const int32        root = bones.addBone( hashed_string( "root" ), -1, float4x4::Identity ).value();
            const float4x4     hand = float4x4::makeTrs( float3{ 0.0f, 1.0f, 0.0f }, quaternion::makeFromYawPitchRoll( float3{ 0.0f, MathUtil::kPi * 0.5f, 0.0f } ), float3{ 1.0f } );
            (void)bones.addBone( hashed_string( "hand" ), root, hand );
            bones.computeModelTransforms();
            return bones;
        }

        static bool isNear( const float4x4& a, const float4x4& b, float32 tolerance = 1.0e-4f )
        {
            for ( uint32 element = 0; element < 16; ++element )
            {
                if ( MathUtil::abs( a.data()[element] - b.data()[element] ) > tolerance )
                    return false;
            }
            return true;
        }

        /** @brief hand 를 부모로 한 소켓 grip 하나가 든 모델입니다. */
        static void addGrip( SocketEditModel& model )
        {
            SW_REQUIRE_TRUE( model.addSocket( hashed_string( "grip" ), hashed_string( "hand" ), hashed_string{} ).has_value() );
        }
    };
    using Util = SocketEditModelTestUtil;
} // namespace

// 기즈모가 놓은 유닛 자리를 부모 본 기준 로컬로 되돌리면, 런타임 계산(로컬 × 부모)이 같은 자리를 낸다.
SW_TEST_CASE( SocketEditModelTest, UnitTransformRoundTripsThroughTheParentBone )
{
    const CharacterBoneArray bones = Util::makeBones();
    SocketEditModel          model;
    Util::addGrip( model );
    const SocketDef* pGrip = model.findSocket( hashed_string( "grip" ) );
    SW_REQUIRE_NOT_NULL( pGrip );
    float4x4 parentUnit{};
    SW_REQUIRE_TRUE( model.getSockets().computeParentTransform( *pGrip, bones, parentUnit ) );
    SW_EXPECT_TRUE( Util::isNear( parentUnit, bones._listModel[1] ) );

    const float4x4 target = float4x4::makeTrs( float3{ 0.3f, 1.2f, -0.4f }, quaternion::makeFromYawPitchRoll( float3{ 0.2f, 0.1f, 0.0f } ), float3{ 1.0f } );
    SW_REQUIRE_TRUE( model.setUnitTransform( hashed_string( "grip" ), target, parentUnit, false ).has_value() );
    pGrip = model.findSocket( hashed_string( "grip" ) );
    float4x4 unit{};
    SW_REQUIRE_TRUE( model.getSockets().computeSocketTransform( *pGrip, bones, unit ) );
    SW_EXPECT_TRUE( Util::isNear( unit, target ) );
    // 크기는 그대로라 적지 않는다(적은 칸만 저장한다). 위치 · 회전은 적는다.
    SW_EXPECT_TRUE( ( pGrip->_fieldMask & SocketFieldBit::kTranslation ) != 0 );
    SW_EXPECT_TRUE( ( pGrip->_fieldMask & SocketFieldBit::kRotation ) != 0 );
}

// 한 번의 끌기(연속 편집 여러 번)는 되돌리기 한 단계다 — 위치 · 회전 · 크기를 칸마다 쌓지 않는다.
SW_TEST_CASE( SocketEditModelTest, GizmoDragIsOneUndoStep )
{
    const CharacterBoneArray bones = Util::makeBones();
    SocketEditModel          model;
    Util::addGrip( model );
    const float3 before = model.findSocket( hashed_string( "grip" ) )->_translation;
    for ( uint32 step = 1; step <= 5; ++step )
    {
        const float4x4 target = float4x4::makeTrs( float3{ 0.1f * static_cast<float32>( step ), 1.0f, 0.0f }, quaternion::Identity, float3{ 1.0f } );
        SW_REQUIRE_TRUE( model.setUnitTransform( hashed_string( "grip" ), target, bones._listModel[1], true ).has_value() );
    }
    model.endContinuousEdit();
    SW_EXPECT_TRUE( model.isDirty() );
    SW_REQUIRE_TRUE( model.undo() );
    const float3 after = model.findSocket( hashed_string( "grip" ) )->_translation;
    SW_EXPECT_TRUE( ( after - before ).getLength() < 1.0e-5f );
    // 소켓을 더한 단계만 남았다 — 한 번 더 되돌리면 소켓이 없다.
    SW_REQUIRE_TRUE( model.undo() );
    SW_EXPECT_TRUE( model.findSocket( hashed_string( "grip" ) ) == nullptr );
}

// 없는 소켓은 표를 바꾸지 않고 이유를 남긴다.
SW_TEST_CASE( SocketEditModelTest, UnknownSocketIsRefused )
{
    SocketEditModel      model;
    const expected<void> missing = model.setUnitTransform( hashed_string( "nothing" ), float4x4::Identity, float4x4::Identity, true );
    SW_REQUIRE_FALSE( missing.has_value() );
    SW_EXPECT_TRUE( missing.error().kind() == ErrorKind::NotFound );
    SW_EXPECT_FALSE( model.canUndo() );
}

// 기즈모로 옮긴 소켓을 저장 글로 쓰고 다시 읽어도 같은 자리다.
SW_TEST_CASE( SocketEditModelTest, GizmoEditSurvivesSaveAndReload )
{
    const CharacterBoneArray bones = Util::makeBones();
    SocketEditModel          model;
    Util::addGrip( model );
    const float4x4 target = float4x4::makeTrs( float3{ 0.25f, 1.5f, 0.1f }, quaternion::makeFromYawPitchRoll( float3{ 0.0f, 0.3f, 0.0f } ), float3{ 1.0f } );
    SW_REQUIRE_TRUE( model.setUnitTransform( hashed_string( "grip" ), target, bones._listModel[1], false ).has_value() );
    SocketEditModel reloaded;
    SW_REQUIRE_TRUE( reloaded.loadFromXMLText( model.saveToXMLText(), "test" ) );
    const SocketDef* pGrip = reloaded.findSocket( hashed_string( "grip" ) );
    SW_REQUIRE_NOT_NULL( pGrip );
    float4x4 unit{};
    SW_REQUIRE_TRUE( reloaded.getSockets().computeSocketTransform( *pGrip, bones, unit ) );
    SW_EXPECT_TRUE( Util::isNear( unit, target, 1.0e-3f ) ); // 회전은 파일에 도 단위 글로 적혀 소수 자리만큼 어긋난다
}
