#include "pch.h"

#include "Engine/Physics/Asset/PhysicsRagdoll.h"

#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/PhysicsSettings.h"

namespace sw
{
    SW_LOG_CALLER( "PhysicsRagdoll" );

    namespace
    {
        struct PhysicsRagdollInternal
        {
            /** @brief 행렬의 자리 · 회전입니다(배율은 버린다). */
            static void decomposePose( const float4x4& matrix, float3& outPosition, quaternion& outRotation )
            {
                float3 scale{};
                if ( matrix.decompose( scale, outRotation, outPosition ) == false )
                {
                    outPosition = matrix.getTranslation();
                    outRotation = quaternion{};
                }
                outRotation.normalize();
            }

            static bool isSkeletonValid( const PhysicsSkeletonView& skeleton )
            {
                const size_t boneCount = skeleton._listBoneName.size();
                if ( skeleton._listParentIndex.size() != boneCount || skeleton._listModelSpaceBone.size() != boneCount )
                {
                    SW_LOG_ERROR( "Ragdoll skeleton arrays differ in length (names %#, parents %#, matrices %#)", boneCount, skeleton._listParentIndex.size(),
                                  skeleton._listModelSpaceBone.size() );
                    return false;
                }
                for ( size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex )
                {
                    const int32 parent = skeleton._listParentIndex[boneIndex];
                    if ( parent >= static_cast<int32>( boneIndex ) || parent < -1 )
                    {
                        SW_LOG_ERROR( "Ragdoll skeleton bone %# has parent %# - parents must come before their children", boneIndex, parent );
                        return false;
                    }
                }
                return true;
            }

            static uint8 resolveLayer( const PhysicsRagdollOptions& options, const hashed_string& layerName )
            {
                uint8 layer = 0;
                if ( options._pSettings != nullptr && layerName.empty() == false && options._pSettings->findLayerIndex( layerName, layer ) == false )
                {
                    SW_LOG_ERROR( "Ragdoll body names unknown layer '%#' - using layer 0", layerName.c_str() );
                    layer = 0;
                }
                return layer;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 PhysicsRagdoll::findBodyIndex( PhysicsBodyHandle body ) const
    {
        for ( size_t bodyIndex = 0; bodyIndex < _listBody.size(); ++bodyIndex )
        {
            if ( _listBody[bodyIndex] == body )
                return static_cast<int32>( bodyIndex );
        }
        return -1;
    }

    bool PhysicsRagdollBuilder::create( IPhysicsScene3D& scene, const PhysicsAsset& asset, const PhysicsSkeletonView& skeleton, const float4x4& worldFromModel,
                                        const PhysicsRagdollOptions& options, PhysicsRagdoll& outRagdoll )
    {
        outRagdoll = PhysicsRagdoll{};
        if ( PhysicsRagdollInternal::isSkeletonValid( skeleton ) == false || asset.validateAgainstSkeleton( skeleton._listBoneName, "ragdoll" ) == false )
            return false;

        const size_t boneCount = skeleton._listBoneName.size();
        const size_t bodyCount = asset._listBody.size();
        outRagdoll._listBodyOfBone.assign( boneCount, -1 );
        outRagdoll._listBoneIndex.assign( bodyCount, -1 );
        for ( size_t bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex )
        {
            for ( size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                if ( skeleton._listBoneName[boneIndex] == asset._listBody[bodyIndex]._bone )
                {
                    outRagdoll._listBoneIndex[bodyIndex]  = static_cast<int32>( boneIndex );
                    outRagdoll._listBodyOfBone[boneIndex] = static_cast<int32>( bodyIndex );
                    break;
                }
            }
        }

        // 바디 — 뼈의 월드 자세에 한 번에 넣는다. 셰이프는 뼈 로컬이라 바디의 틀이 곧 뼈의 틀이다.
        vector<PhysicsBodyDesc3D> listDesc( bodyCount );
        for ( size_t bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex )
        {
            const PhysicsAssetBodyDef& body      = asset._listBody[bodyIndex];
            const int32                boneIndex = outRagdoll._listBoneIndex[bodyIndex];
            PhysicsBodyDesc3D&         desc      = listDesc[bodyIndex];
            PhysicsRagdollInternal::decomposePose( skeleton._listModelSpaceBone[static_cast<size_t>( boneIndex )] * worldFromModel, desc._position, desc._rotation );
            desc._listShape        = body._listShape;
            desc._userData         = options._userData;
            desc._material         = body._material;
            desc._mass             = body._mass;
            desc._type             = options._bodyType;
            desc._layer            = PhysicsRagdollInternal::resolveLayer( options, body._layer.empty() ? asset._defaultLayer : body._layer );
            desc._bAllowTypeChange = true;
        }
        scene.createBodies( span<const PhysicsBodyDesc3D>{ listDesc.data(), listDesc.size() }, outRagdoll._listBody );
        for ( const PhysicsBodyHandle& body : outRagdoll._listBody )
        {
            if ( body.isValid() == false )
            {
                SW_LOG_ERROR( "Ragdoll: a body could not be created - nothing is kept" );
                destroy( scene, outRagdoll );
                return false;
            }
        }

        // 관절 — 부모 사슬에서 가장 가까운 바디가 있는 뼈와 잇는다. 자리는 이 뼈의 원점, 축은 뼈 로컬 축을 월드로 돌린 것.
        outRagdoll._listParentBody.assign( bodyCount, -1 );
        outRagdoll._listJoint.assign( bodyCount, PhysicsJointHandle{} );
        for ( size_t bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex )
        {
            int32 parentBone = skeleton._listParentIndex[static_cast<size_t>( outRagdoll._listBoneIndex[bodyIndex] )];
            while ( parentBone >= 0 && outRagdoll._listBodyOfBone[static_cast<size_t>( parentBone )] < 0 )
            {
                parentBone = skeleton._listParentIndex[static_cast<size_t>( parentBone )];
            }
            if ( parentBone < 0 )
                continue;
            const int32 parentBody                = outRagdoll._listBodyOfBone[static_cast<size_t>( parentBone )];
            outRagdoll._listParentBody[bodyIndex] = parentBody;

            const PhysicsAssetJointDef& joint = asset._listBody[bodyIndex]._joint;
            float3                      position{};
            quaternion                  rotation{};
            PhysicsRagdollInternal::decomposePose( skeleton._listModelSpaceBone[static_cast<size_t>( outRagdoll._listBoneIndex[bodyIndex] )] * worldFromModel, position,
                                                   rotation );
            PhysicsJointDesc3D desc;
            desc._bodyA                      = outRagdoll._listBody[static_cast<size_t>( parentBody )];
            desc._bodyB                      = outRagdoll._listBody[bodyIndex];
            desc._anchor                     = position;
            desc._anchorB                    = position;
            desc._axis                       = float3::transform( joint._twistAxis, rotation ).normalize();
            desc._normalAxis                 = float3::transform( joint._normalAxis, rotation ).normalize();
            desc._minLimit                   = joint._twistMin;
            desc._maxLimit                   = joint._twistMax;
            desc._swingLimitNormal           = joint._swingLimitNormal;
            desc._swingLimitPlane            = joint._swingLimitPlane;
            desc._type                       = joint._type;
            desc._bLimitsEnabled             = true;
            desc._bCollideConnectedBodies    = asset._bCollideJointedBodies;
            outRagdoll._listJoint[bodyIndex] = scene.createJoint( desc );
        }

        for ( const PhysicsAssetPairDef& pair : asset._listDisabledPair )
        {
            const int32 bodyA = asset.findBodyIndex( pair._boneA );
            const int32 bodyB = asset.findBodyIndex( pair._boneB );
            if ( bodyA >= 0 && bodyB >= 0 )
                scene.setPairCollision( outRagdoll._listBody[static_cast<size_t>( bodyA )], outRagdoll._listBody[static_cast<size_t>( bodyB )], false );
        }
        return true;
    }

    void PhysicsRagdollBuilder::destroy( IPhysicsScene3D& scene, PhysicsRagdoll& inoutRagdoll )
    {
        // 바디를 지우면 붙은 관절도 지워진다.
        vector<PhysicsBodyHandle> listValid;
        for ( const PhysicsBodyHandle& body : inoutRagdoll._listBody )
        {
            if ( body.isValid() )
                listValid.push_back( body );
        }
        scene.destroyBodies( span<const PhysicsBodyHandle>{ listValid.data(), listValid.size() } );
        inoutRagdoll = PhysicsRagdoll{};
    }

    void PhysicsRagdollBuilder::setBodyType( IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, PhysicsBodyType type )
    {
        for ( const PhysicsBodyHandle& body : ragdoll._listBody )
        {
            scene.setBodyType( body, type );
        }
    }

    void PhysicsRagdollBuilder::driveToPose( IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, const PhysicsSkeletonView& skeleton, const float4x4& worldFromModel,
                                             float32 deltaTime )
    {
        for ( size_t bodyIndex = 0; bodyIndex < ragdoll._listBody.size(); ++bodyIndex )
        {
            const size_t boneIndex = static_cast<size_t>( ragdoll._listBoneIndex[bodyIndex] );
            if ( boneIndex >= skeleton._listModelSpaceBone.size() )
                continue;
            float3     position{};
            quaternion rotation{};
            PhysicsRagdollInternal::decomposePose( skeleton._listModelSpaceBone[boneIndex] * worldFromModel, position, rotation );
            const PhysicsBodyHandle body = ragdoll._listBody[bodyIndex];
            if ( scene.isBodyEnabled( body ) == false )
                continue; // 시뮬레이션에서 뺀 바디(잘려 나간 영역)는 움직이지 않는다
            if ( deltaTime > 0.0f && scene.getBodyType( body ) == PhysicsBodyType::Kinematic )
                scene.moveKinematic( body, position, rotation, deltaTime );
            else
                scene.setBodyTransform( body, position, rotation );
        }
    }

    void PhysicsRagdollBuilder::readBoneTransforms( const IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, const PhysicsSkeletonView& skeleton,
                                                    const float4x4& worldFromModel, span<float4x4> outListModelSpaceBone )
    {
        const size_t boneCount = skeleton._listModelSpaceBone.size();
        if ( outListModelSpaceBone.size() < boneCount || skeleton._listParentIndex.size() != boneCount || ragdoll._listBodyOfBone.size() != boneCount )
        {
            SW_LOG_ERROR( "readBoneTransforms: the output, the skeleton and the ragdoll disagree on the bone count" );
            return;
        }
        const float4x4 modelFromWorld = worldFromModel.invert();
        for ( size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const float4x4& inputModel = skeleton._listModelSpaceBone[boneIndex];
            const int32     parent     = skeleton._listParentIndex[boneIndex];
            const int32     bodyIndex  = ragdoll._listBodyOfBone[boneIndex];
            float3          position{};
            quaternion      rotation{};
            if ( bodyIndex >= 0 && scene.getBodyTransform( ragdoll._listBody[static_cast<size_t>( bodyIndex )], position, rotation ) )
            {
                float3     inputScale{};
                quaternion inputRotation{};
                float3     inputPosition{};
                if ( inputModel.decompose( inputScale, inputRotation, inputPosition ) == false )
                    inputScale = float3{ 1.0f, 1.0f, 1.0f };
                float3     modelPosition{};
                quaternion modelRotation{};
                PhysicsRagdollInternal::decomposePose( float4x4::makeTrs( position, rotation, float3{ 1.0f, 1.0f, 1.0f } ) * modelFromWorld, modelPosition, modelRotation );
                outListModelSpaceBone[boneIndex] = float4x4::makeTrs( modelPosition, modelRotation, inputScale );
                continue;
            }
            if ( parent < 0 )
            {
                outListModelSpaceBone[boneIndex] = inputModel;
                continue;
            }
            // 바디 없는 뼈 — 입력 포즈의 부모 상대 변환을 새 부모 위에 놓는다.
            const float4x4 local             = inputModel * skeleton._listModelSpaceBone[static_cast<size_t>( parent )].invert();
            outListModelSpaceBone[boneIndex] = local * outListModelSpaceBone[static_cast<size_t>( parent )];
        }
    }
} // namespace sw
