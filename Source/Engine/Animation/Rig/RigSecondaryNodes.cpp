#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigIkSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigNodeLibrary.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Rig/RigSpringChain.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "RigSecondaryNodes" );

    namespace
    {
        struct RigSecondaryNodesInternal
        {
            static constexpr const utf8* kArrColliderShape[] = { "Sphere", "Capsule" };

            /** @brief 두 회전 사이의 각(라디안, 0..π)입니다. */
            static float32 computeAngleBetween( const quaternion& lhs, const quaternion& rhs )
            {
                const float32 dot = MathUtil::clamp( MathUtil::abs( lhs.dot( rhs ) ), 0.0f, 1.0f );
                return 2.0f * MathUtil::acos( dot );
            }

            /** @brief 가우스 RBF 핵입니다. */
            static float32 computeKernel( float32 angle, float32 radius ) { return std::exp( -( angle * angle ) / ( radius * radius ) ); }

            /**
             * @brief n × n 행렬(행 우선)의 역행렬을 가우스-요르단 소거로 구합니다. 특이하면 false 입니다.
             * @details 포즈 구동의 포즈 수(수 개 ~ 수십 개)라 묶을 때 한 번 푼다.
             */
            static bool invertMatrix( vector<float32> listMatrix, uint32 size, vector<float32>& outListInverse )
            {
                outListInverse.assign( size * size, 0.0f );
                for ( uint32 index = 0; index < size; ++index )
                {
                    outListInverse[index * size + index] = 1.0f;
                }
                for ( uint32 column = 0; column < size; ++column )
                {
                    uint32  pivot     = column;
                    float32 pivotSize = MathUtil::abs( listMatrix[column * size + column] );
                    for ( uint32 row = column + 1; row < size; ++row )
                    {
                        if ( MathUtil::abs( listMatrix[row * size + column] ) > pivotSize )
                        {
                            pivot     = row;
                            pivotSize = MathUtil::abs( listMatrix[row * size + column] );
                        }
                    }
                    if ( pivotSize < 1e-8f )
                        return false;
                    if ( pivot != column )
                    {
                        for ( uint32 item = 0; item < size; ++item )
                        {
                            std::swap( listMatrix[pivot * size + item], listMatrix[column * size + item] );
                            std::swap( outListInverse[pivot * size + item], outListInverse[column * size + item] );
                        }
                    }
                    const float32 scale = 1.0f / listMatrix[column * size + column];
                    for ( uint32 item = 0; item < size; ++item )
                    {
                        listMatrix[column * size + item] *= scale;
                        outListInverse[column * size + item] *= scale;
                    }
                    for ( uint32 row = 0; row < size; ++row )
                    {
                        if ( row == column )
                            continue;
                        const float32 factor = listMatrix[row * size + column];
                        if ( factor == 0.0f )
                            continue;
                        for ( uint32 item = 0; item < size; ++item )
                        {
                            listMatrix[row * size + item] -= factor * listMatrix[column * size + item];
                            outListInverse[row * size + item] -= factor * outListInverse[column * size + item];
                        }
                    }
                }
                return true;
            }
        };

        /**
         * @brief 스프링 사슬(꼬리 · 머리 가닥 · 망토). 키: `bones` · `stiffness` · `damping` · `gravity` · `particle_radius` · `fixed_step` · `max_substeps` ·
         *        `teleport_distance` · `lod_distance`(뷰에서 이보다 멀면 끔 — 0 이면 늘 켬) · `colliders`(`[{ "bone", "shape", "a", "b", "radius" }]`) ·
         *        `use_shared_colliders`(호스트가 준 충돌체 — 물리 에셋의 구 · 캡슐 — 도 씀).
         */
        class RigSpringChainNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override
            {
                unique_ptr<RigSpringChainNode> node = make_unique<RigSpringChainNode>( *this );
                node->reset();
                return node;
            }

            const utf8* getTypeName() const override { return "SpringChain"; }

            [[nodiscard]] bool parse( RigJsonReader& reader ) override
            {
                bool bShared               = false;
                _settings._gravityOverride = float3{ MathUtil::kMaxFloat, 0.0f, 0.0f }; // 표식 — "gravity" 를 읽었는지 가린다
                bool bOk                   = reader.readNameList( "bones", _listBoneName, true ) && reader.readFloat( "stiffness", _settings._stiffness, false ) &&
                           reader.readFloat( "damping", _settings._damping, false ) && reader.readFloat3( "gravity", _settings._gravityOverride, false ) && reader.readFloat( "gravity_scale", _settings._gravityScale, false ) &&
                           reader.readFloat( "particle_radius", _settings._particleRadius, false ) && reader.readFloat( "fixed_step", _settings._fixedStep, false ) &&
                           reader.readUint( "max_substeps", _settings._maxSubStep, false ) &&
                           reader.readFloat( "teleport_distance", _settings._teleportDistance, false ) && reader.readFloat( "lod_distance", _lodDistance, false ) &&
                           reader.readBool( "use_shared_colliders", bShared, false );
                // "gravity" 가 적혀 있으면 월드 중력 대신 그 값(덮어쓰기), 없으면 월드 중력 × gravity_scale.
                _settings._bUseGravityOverride = ( _settings._gravityOverride._x != MathUtil::kMaxFloat ) ? SW_TRUE : SW_FALSE;
                if ( _settings._bUseGravityOverride == SW_FALSE )
                    _settings._gravityOverride = float3{};
                _bUseSharedColliders      = bShared ? SW_TRUE : SW_FALSE;
                const JsonValue colliders = reader.readArray( "colliders", false );
                for ( size_t index = 0; bOk && colliders.isValid() && index < colliders.size(); ++index )
                {
                    RigJsonReader colliderReader( colliders.at( index ), reader.getContext() );
                    ColliderSpec  spec{};
                    uint32        shape = 0;
                    bOk                 = colliderReader.readName( "bone", spec._boneName, true ) &&
                          colliderReader.readChoice( "shape", RigSecondaryNodesInternal::kArrColliderShape, SW_COUNT_OF( RigSecondaryNodesInternal::kArrColliderShape ),
                                                     shape, true ) &&
                          colliderReader.readFloat3( "a", spec._collider._pointA, false ) && colliderReader.readFloat3( "b", spec._collider._pointB, false ) &&
                          colliderReader.readFloat( "radius", spec._collider._radius, true );
                    bOk                   = colliderReader.finish() && bOk;
                    spec._collider._shape = shape == 0 ? RigSpringColliderShape::Sphere : RigSpringColliderShape::Capsule;
                    _listColliderSpec.push_back( spec );
                }
                if ( bOk && ( _listBoneName.size() < 2 || _settings._fixedStep <= 0.0f || _settings._maxSubStep == 0 ) )
                {
                    reader.fail( "'bones' needs at least two bones, 'fixed_step' and 'max_substeps' must be positive" );
                    return false;
                }
                return bOk && reader.isOk();
            }

            bool bind( const RigBindContext& context ) override
            {
                if ( context.findBones( _listBoneName, _listBone, true ) == false )
                    return false;
                _listCollider.clear();
                for ( const ColliderSpec& spec : _listColliderSpec )
                {
                    RigSpringCollider collider = spec._collider;
                    if ( context.findBone( spec._boneName, collider._bone ) == false )
                        return false;
                    _listCollider.push_back( collider );
                }
                return true;
            }

            void prepare( const RigPrepareContext& context ) override
            {
                if ( _lodDistance <= 0.0f || context._bHasViewPosition == SW_FALSE )
                {
                    _bLodOff = SW_FALSE;
                    return;
                }
                const float32 distance = ( context._worldFromModel.getTranslation() - context._viewPosition ).getLength();
                const bool    bOff     = distance > _lodDistance;
                if ( bOff && _bLodOff == SW_FALSE )
                    _chain.reset(); // 다시 켤 때 애니메이션 자세에서 시작한다
                _bLodOff = bOff ? SW_TRUE : SW_FALSE;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                if ( _bLodOff == SW_TRUE )
                    return;
                const vector<RigSpringCollider>& listShared = context._pInstance->getSharedColliders();
                if ( _bUseSharedColliders == SW_TRUE && listShared.empty() == false )
                {
                    _listScratchCollider.assign( _listCollider.begin(), _listCollider.end() );
                    _listScratchCollider.insert( _listScratchCollider.end(), listShared.begin(), listShared.end() );
                    _chain.simulate( *context._pPose, _listBone, _settings, _listScratchCollider, context._worldFromModel, context._worldGravity, context._deltaSeconds, *context._pSpace );
                    return;
                }
                _chain.simulate( *context._pPose, _listBone, _settings, _listCollider, context._worldFromModel, context._worldGravity, context._deltaSeconds, *context._pSpace );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override { inoutListBone.insert( inoutListBone.end(), _listBone.begin(), _listBone.end() ); }

            void reset() override
            {
                _chain.reset();
                _bLodOff = SW_FALSE;
            }

        private:
            struct ColliderSpec
            {
                RigSpringCollider _collider{};
                hashed_string     _boneName{};
            };

            vector<hashed_string>     _listBoneName{};
            vector<uint32>            _listBone{};
            vector<ColliderSpec>      _listColliderSpec{};
            vector<RigSpringCollider> _listCollider{};
            vector<RigSpringCollider> _listScratchCollider{};
            RigSpringChain            _chain{};
            RigSpringSettings         _settings{};
            float32                   _lodDistance{ 0.0f };
            uint8                     _bUseSharedColliders{ SW_FALSE };
            uint8                     _bLodOff{ SW_FALSE };
        };

        /**
         * @brief 포즈 구동(RBF 포즈 드라이버). 키: `driver`(관절) · `radius_degrees`(가우스 폭) · `poses`(`[{ "name", "rotation", "morphs": [{ "morph", "weight" }],
         *        "bones": [{ "bone", "rotation", "translation" }] }]`).
         * @details 관절의 레퍼런스 기준 회전과 포즈마다의 회전 사이 각으로 가우스 핵을 재고, 묶을 때 푼 보간 행렬(Φ⁻¹)로 포즈 가중치를 냅니다 —
         *          포즈 자리에서는 그 포즈만 1 입니다. 가중치만큼 보정 모프(`RigInstance::getMorphWeights`)와 보정 본(로컬 회전 · 이동 덧셈)을 냅니다.
         *          팔꿈치를 굽히면 팔뚝 근육 보정 — 언리얼 Pose Driver · Maya RBF 의 자리입니다. 쉬는 포즈(출력 없음)도 데이터에 한 줄 넣습니다.
         */
        class RigPoseDriverNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigPoseDriverNode>( *this ); }
            const utf8*         getTypeName() const override { return "PoseDriver"; }

            [[nodiscard]] bool parse( RigJsonReader& reader ) override
            {
                float32 radiusDegrees = 45.0f;
                bool    bOk           = reader.readName( "driver", _driverName, true ) && reader.readFloat( "radius_degrees", radiusDegrees, false );
                _radius               = MathUtil::max( radiusDegrees, 1.0f ) * MathUtil::kDegreeToRadian;
                const JsonValue poses = reader.readArray( "poses", true );
                for ( size_t index = 0; bOk && poses.isValid() && index < poses.size(); ++index )
                {
                    RigJsonReader poseReader( poses.at( index ), reader.getContext() );
                    DriverPose    pose{};
                    bOk                    = poseReader.readName( "name", pose._name, true ) && poseReader.readRotationDegrees( "rotation", pose._rotation, true );
                    const JsonValue morphs = poseReader.readArray( "morphs", false );
                    for ( size_t morphIndex = 0; bOk && morphs.isValid() && morphIndex < morphs.size(); ++morphIndex )
                    {
                        RigJsonReader  morphReader( morphs.at( morphIndex ), reader.getContext() );
                        RigMorphWeight morph{};
                        bOk = morphReader.readName( "morph", morph._name, true ) && morphReader.readFloat( "weight", morph._weight, true );
                        bOk = morphReader.finish() && bOk;
                        pose._listMorph.push_back( morph );
                    }
                    const JsonValue bones = poseReader.readArray( "bones", false );
                    for ( size_t boneIndex = 0; bOk && bones.isValid() && boneIndex < bones.size(); ++boneIndex )
                    {
                        RigJsonReader  boneReader( bones.at( boneIndex ), reader.getContext() );
                        CorrectiveBone corrective{};
                        bOk = boneReader.readName( "bone", corrective._name, true ) && boneReader.readRotationDegrees( "rotation", corrective._rotation, false ) &&
                              boneReader.readFloat3( "translation", corrective._translation, false );
                        bOk = boneReader.finish() && bOk;
                        pose._listBone.push_back( corrective );
                    }
                    bOk = poseReader.finish() && bOk;
                    _listPose.push_back( pose );
                }
                if ( bOk && _listPose.empty() )
                {
                    reader.fail( "'poses' must not be empty" );
                    return false;
                }
                return bOk && reader.isOk();
            }

            bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _driverName, _driver ) == false )
                    return false;
                _driverReference = context._pSkeleton->getBone( _driver )._referencePose._rotation;
                for ( DriverPose& pose : _listPose )
                {
                    for ( CorrectiveBone& corrective : pose._listBone )
                    {
                        if ( context.findBone( corrective._name, corrective._bone ) == false )
                            return false;
                    }
                }
                const uint32    poseCount = static_cast<uint32>( _listPose.size() );
                vector<float32> kernel( poseCount * poseCount, 0.0f );
                for ( uint32 row = 0; row < poseCount; ++row )
                {
                    for ( uint32 column = 0; column < poseCount; ++column )
                    {
                        const float32 angle              = RigSecondaryNodesInternal::computeAngleBetween( _listPose[row]._rotation, _listPose[column]._rotation );
                        kernel[row * poseCount + column] = RigSecondaryNodesInternal::computeKernel( angle, _radius );
                    }
                }
                if ( RigSecondaryNodesInternal::invertMatrix( kernel, poseCount, _listInverseKernel ) == false )
                {
                    SW_LOG_ERROR( "Rig '%#': pose driver '%#' has two poses at the same rotation", context._label, _name.c_str() );
                    return false;
                }
                _listPoseWeight.assign( poseCount, 0.0f );
                _listKernel.assign( poseCount, 0.0f );
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer&   pose      = *context._pPose;
                const quaternion current   = ( RigIkSolver::makeInverse( _driverReference ) * pose.getLocalRotation( _driver ) ).normalize();
                const uint32     poseCount = static_cast<uint32>( _listPose.size() );
                for ( uint32 index = 0; index < poseCount; ++index )
                {
                    _listKernel[index] = RigSecondaryNodesInternal::computeKernel( RigSecondaryNodesInternal::computeAngleBetween( current, _listPose[index]._rotation ), _radius );
                }
                for ( uint32 row = 0; row < poseCount; ++row )
                {
                    float32 weight = 0.0f;
                    for ( uint32 column = 0; column < poseCount; ++column )
                    {
                        weight += _listInverseKernel[row * poseCount + column] * _listKernel[column];
                    }
                    _listPoseWeight[row] = MathUtil::saturate( weight );
                }
                for ( uint32 index = 0; index < poseCount; ++index )
                {
                    const float32     weight     = _listPoseWeight[index];
                    const DriverPose& driverPose = _listPose[index];
                    for ( const RigMorphWeight& morph : driverPose._listMorph )
                    {
                        context._pInstance->addMorphWeight( morph._name, morph._weight * weight * context._nodeWeight );
                    }
                    if ( weight <= 0.0f )
                        continue;
                    for ( const CorrectiveBone& corrective : driverPose._listBone )
                    {
                        BoneTransform local = pose.getLocal( corrective._bone );
                        local._rotation     = ( local._rotation * quaternion::slerp( quaternion::Identity, corrective._rotation, weight ) ).normalize();
                        local._translation  = local._translation + corrective._translation * weight;
                        pose.setLocal( corrective._bone, local );
                    }
                }
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override
            {
                for ( const DriverPose& pose : _listPose )
                {
                    for ( const CorrectiveBone& corrective : pose._listBone )
                    {
                        if ( std::find( inoutListBone.begin(), inoutListBone.end(), corrective._bone ) == inoutListBone.end() )
                            inoutListBone.push_back( corrective._bone );
                    }
                }
            }

        private:
            struct CorrectiveBone
            {
                quaternion    _rotation{};
                float3        _translation{};
                hashed_string _name{};
                uint32        _bone{ 0 };
            };

            struct DriverPose
            {
                vector<RigMorphWeight> _listMorph{};
                vector<CorrectiveBone> _listBone{};
                quaternion             _rotation{};
                hashed_string          _name{};
            };

            vector<DriverPose> _listPose{};
            vector<float32>    _listInverseKernel{};
            vector<float32>    _listKernel{};
            vector<float32>    _listPoseWeight{};
            quaternion         _driverReference{};
            hashed_string      _driverName{};
            float32            _radius{ 0.785f };
            uint32             _driver{ 0 };
        };

        struct RigSecondaryNodesFactory
        {
            static unique_ptr<RigNode> createSpringChain() { return make_unique<RigSpringChainNode>(); }
            static unique_ptr<RigNode> createPoseDriver() { return make_unique<RigPoseDriverNode>(); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void RigNodeLibrary::registerSecondaryNodes( RigNodeRegistry& registry )
    {
        registry.registerNode( "SpringChain", &RigSecondaryNodesFactory::createSpringChain );
        registry.registerNode( "PoseDriver", &RigSecondaryNodesFactory::createPoseDriver );
    }

    bool RigNodeLibrary::registerEngineNodes( RigNodeRegistry& registry )
    {
        registerIkNodes( registry );
        registerConstraintNodes( registry );
        registerSecondaryNodes( registry );
        return true;
    }
} // namespace sw
