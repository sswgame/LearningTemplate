#include "pch.h"

#include "Core/String/hashed_string.h"

#include "Editor/Common/Asset/VrmAvatarImporter.h"
#include "Editor/Common/Asset/VrmRuntimeAssets.h"

#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"

#include "TestFramework/TestFramework.h"

// VrmRuntimeAssetsTest — VRM 아바타 정보가 런타임 형식(얼굴 리그 · 후처리 리그)으로 옮겨지고, 그 파일을 엔진 타입이 읽는지.

namespace
{
    struct TestVrmRuntimeAssetsInternal
    {
        /** @brief hips → hair0 → hair1 → hair2, hair0 → braid0 가지가 있는 스켈레톤입니다. */
        static sw::Skeleton makeSkeleton()
        {
            sw::Skeleton            skeleton;
            const sw::BoneTransform identity{};
            const int32             hips  = skeleton.addBone( sw::hashed_string( "hips" ), -1, identity, sw::float4x4::Identity );
            const int32             hair0 = skeleton.addBone( sw::hashed_string( "hair0" ), hips, identity, sw::float4x4::Identity );
            const int32             hair1 = skeleton.addBone( sw::hashed_string( "hair1" ), hair0, identity, sw::float4x4::Identity );
            (void)skeleton.addBone( sw::hashed_string( "hair2" ), hair1, identity, sw::float4x4::Identity );
            (void)skeleton.addBone( sw::hashed_string( "braid0" ), hair0, identity, sw::float4x4::Identity );
            return skeleton;
        }

        static sw::editor::VrmSpringJoint makeJoint( const utf8* pBone )
        {
            sw::editor::VrmSpringJoint joint;
            joint._bone         = pBone;
            joint._stiffness    = 1.0f;
            joint._dragForce    = 0.4f;
            joint._gravityPower = 0.0f;
            return joint;
        }
    };
} // namespace

/**
 * @brief [VrmRuntimeAssetsTest] 표정은 같은 이름의 표정으로, aa · ee · ou 는 비즘으로, blink 는 깜빡임 타깃으로 옮겨지고 얼굴 리그가 그 파일을 읽는다
 */
SW_TEST_CASE( VrmRuntimeAssetsTest, ExpressionsBecomeFacialRig )
{
    sw::editor::VrmAvatarDef  def;
    sw::editor::VrmExpression happy;
    happy._name     = "happy";
    happy._listBind = {
        sw::editor::VrmExpressionBind{ "Face.smile", 1.0f }
    };
    sw::editor::VrmExpression aa;
    aa._name     = "aa";
    aa._listBind = {
        sw::editor::VrmExpressionBind{ "Face.mouthA", 0.8f }
    };
    sw::editor::VrmExpression blink;
    blink._name     = "blink";
    blink._listBind = {
        sw::editor::VrmExpressionBind{ "Face.eyeClose", 1.0f }
    };
    blink._overrideMouth = sw::editor::VrmExpressionOverride::Block;
    def._listExpression  = { happy, aa, blink };
    SW_ASSERT_TRUE( sw::editor::VrmRuntimeAssets::hasFacialRig( def ) );

    sw::vector<sw::string> listIgnored;
    const sw::string       text = sw::editor::VrmRuntimeAssets::makeFacialRigJSON( def, &listIgnored );
    sw::FacialRig          rig;
    SW_ASSERT_TRUE_MSG( rig.parseJSON( text, "vrm.facial.json" ), text.c_str() );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( rig.getExpressions().size() ) );
    SW_EXPECT_TRUE( rig.findExpressionIndex( sw::hashed_string( "happy" ) ) >= 0 );
    SW_EXPECT_TRUE( rig.findVisemeIndex( sw::hashed_string( "AA" ) ) >= 0 );
    SW_EXPECT_TRUE( rig.findVisemeIndex( sw::hashed_string( "sil" ) ) >= 0 );
    SW_ASSERT_EQUAL( size_t( 1 ), rig.getBlink()._listTarget.size() );
    SW_EXPECT_TRUE( rig.getBlink()._listTarget[0] == sw::hashed_string( "Face.eyeClose" ) );
    // override 는 얼굴 리그에 없는 규칙이라 무시 목록에 남는다.
    SW_EXPECT_EQUAL( size_t( 1 ), listIgnored.size() );
}

/**
 * @brief [VrmRuntimeAssetsTest] 스프링은 스켈레톤 부모를 따라 사슬로 끊겨 SpringChain 노드가 되고, 리그가 그 파일을 읽는다 — 가지는 부모에서 새 사슬이다
 */
SW_TEST_CASE( VrmRuntimeAssetsTest, SpringsBecomeSpringChainRig )
{
    using Internal                    = TestVrmRuntimeAssetsInternal;
    const sw::Skeleton       skeleton = Internal::makeSkeleton();
    sw::editor::VrmAvatarDef def;
    sw::editor::VrmSpring    spring;
    spring._name              = "Hair";
    spring._listJoint         = { Internal::makeJoint( "hair0" ), Internal::makeJoint( "hair1" ), Internal::makeJoint( "hair2" ), Internal::makeJoint( "braid0" ) };
    spring._listColliderGroup = { 0 };
    sw::editor::VrmSpringCollider head;
    head._bone             = "hips";
    head._radius           = 0.1f;
    def._listCollider      = { head };
    def._listColliderGroup = {
        sw::editor::VrmSpringColliderGroup{ "Head", { 0 } }
    };
    def._listSpring = { spring };
    SW_ASSERT_TRUE( sw::editor::VrmRuntimeAssets::hasSpringRig( def, skeleton ) );

    sw::vector<sw::string> listIgnored;
    const sw::string       text = sw::editor::VrmRuntimeAssets::makeSpringRigJSON( def, skeleton, &listIgnored );
    sw::RigAsset           rig;
    SW_ASSERT_TRUE_MSG( rig.parseJSON( text, "vrm.rig.json" ), text.c_str() );
    // hair0 → hair1 → hair2 한 사슬, braid0 는 부모 hair0 에서 시작하는 두 번째 사슬.
    SW_EXPECT_EQUAL( size_t( 2 ), rig.getNodes().size() );
    SW_EXPECT_TRUE( listIgnored.empty() );

    // 스켈레톤에 없는 관절은 넣지 않고 무시 목록에 남는다.
    def._listSpring[0]._listJoint.push_back( Internal::makeJoint( "missing" ) );
    listIgnored.clear();
    (void)sw::editor::VrmRuntimeAssets::makeSpringRigJSON( def, skeleton, &listIgnored );
    SW_EXPECT_EQUAL( size_t( 1 ), listIgnored.size() );
}
