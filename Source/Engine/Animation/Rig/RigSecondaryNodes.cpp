#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigBuiltinNodes.h"
#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Rig/RigSpringChain.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct RigSecondaryNodesInternal
        {
            /** @brief 가우스 RBF 핵입니다. */
            static float32 computeKernel( float32 angle, float32 radius ) { return std::exp( -( angle * angle ) / ( radius * radius ) ); }

            /**
             * @brief n × n 행렬(행 우선)의 역행렬을 가우스-요르단 소거로 구합니다. 특이하면 false 를 반환합니다.
             * @details 포즈 구동의 포즈 수는 수 개에서 수십 개라서, 바인딩할 때 한 번만 푼다.
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
         * @brief 스프링 체인(꼬리, 머리 가닥, 망토)입니다. 키는 `bones`, `stiffness`, `axis_pull`, `damping`, `gravity`, `particle_radius`, `fixed_step`, `max_substeps`,
         *        `joints`(`[{ "stiffness", "axis_pull", "damping", "gravity", "radius" }]` — 본 수 − 1 개, 입자마다 값),
         *        `teleport_distance`, `colliders`(`[{ "bone" 또는 "target", "shape", "a", "b", "radius" }]` — `target` 은 리그 목표라 다른 유닛의 본(망토가 몸의 엉덩이 · 다리)에 붙는다),
         *        `use_shared_colliders`(호스트가 준 충돌체, 즉 물리 에셋의 구와 캡슐도 사용)입니다. 멀리서 끄는 것은 공통 키 `max_lod`(LOD 등급, 꺼지고 켜질 때 페이드)입니다.
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

            [[nodiscard]] bool parse( RigXMLReader& reader ) override
            {

                bool bOk                    = reader.readProperties( _params ); // 범위(비율 0..1, 스텝 > 0 등)는 PROPERTY 가 본다
                bOk                         = reader.readNameList( "bones", _listBoneName, true ) && bOk;
                _settings._stiffness        = _params._stiffness;
                _settings._damping          = _params._damping;
                _settings._axisPull         = _params._axisPull;
                _settings._gravityScale     = _params._gravityScale;
                _settings._particleRadius   = _params._particleRadius;
                _settings._fixedStep        = _params._fixedStep;
                _settings._maxSubStep       = _params._maxSubsteps;
                _settings._teleportDistance = _params._teleportDistance;
                // "gravity" 는 적었는지가 뜻이다 — 적으면 월드 중력 대신 그 값(덮어쓰기), 없으면 월드 중력 × gravity_scale.
                _settings._gravityOverride     = float3{ MathUtil::kMaxFloat, 0.0f, 0.0f }; // 표식이다. "gravity" 를 읽었는지 구분한다.
                bOk                            = reader.readFloat3( "gravity", _settings._gravityOverride, false ) && bOk;
                _settings._bUseGravityOverride = ( _settings._gravityOverride._x != MathUtil::kMaxFloat ) ? SW_TRUE : SW_FALSE;
                if ( _settings._bUseGravityOverride == SW_FALSE )
                    _settings._gravityOverride = float3{};
                const RigXMLList colliders = reader.readArray( "colliders", false );
                for ( size_t index = 0; bOk && colliders.isValid() && index < colliders.size(); ++index )
                {
                    RigXMLReader            colliderReader( colliders.at( index ), reader.getContext() );
                    RigSpringColliderParams colliderParams;
                    bOk = colliderReader.readProperties( colliderParams );
                    bOk = colliderReader.finish() && bOk;
                    ColliderSpec spec{};
                    spec._boneName         = colliderParams._bone;
                    spec._targetName       = colliderParams._target;
                    spec._collider._shape  = colliderParams._shape;
                    spec._collider._pointA = colliderParams._a;
                    spec._collider._pointB = colliderParams._b;
                    spec._collider._radius = colliderParams._radius;
                    if ( bOk && spec._boneName.empty() == spec._targetName.empty() )
                    {
                        reader.fail( "a spring collider needs exactly one of 'bone' or 'target'" );
                        return false;
                    }
                    _listColliderSpec.push_back( spec );
                }
                // 입자마다의 물성(VRM 관절마다 값) — 적지 않은 키는 사슬 값이다. 길이는 본 수 − 1 이어야 한다(입자 하나에 하나).
                const RigXMLList joints = reader.readArray( "joints", false );
                _settings._listParticle.clear();
                for ( size_t index = 0; bOk && joints.isValid() && index < joints.size(); ++index )
                {
                    RigXMLReader         jointReader( joints.at( index ), reader.getContext() );
                    RigSpringJointParams jointParams;
                    jointParams._stiffness = _settings._stiffness;
                    jointParams._axisPull  = _settings._axisPull;
                    jointParams._damping   = _settings._damping;
                    jointParams._radius    = _settings._particleRadius;
                    bOk                    = jointReader.readProperties( jointParams );
                    bOk                    = jointReader.finish() && bOk;
                    _settings._listParticle.push_back(
                        RigSpringParticleSettings{ jointParams._gravity, jointParams._stiffness, jointParams._axisPull, jointParams._damping, jointParams._radius } );
                }
                if ( bOk && _listBoneName.size() < 2 )
                {
                    reader.fail( "'bones' needs at least two bones" );
                    return false;
                }
                const bool bJointCountMismatch = _settings._listParticle.empty() == false && _settings._listParticle.size() + 1 != _listBoneName.size();
                if ( bOk && bJointCountMismatch )
                {
                    reader.fail( "'joints' needs one entry per bone after the first" );
                    return false;
                }
                return bOk && reader.isOk();
            }

            [[nodiscard]] bool bind( const RigBindContext& context ) override
            {
                if ( context.findBones( _listBoneName, _listBone, true ) == false )
                    return false;
                _listCollider.clear();
                _listTargetCollider.clear();
                for ( const ColliderSpec& spec : _listColliderSpec )
                {
                    if ( spec._targetName.empty() == false )
                    {
                        TargetCollider targetCollider{ spec._collider, 0 };
                        if ( context.findTarget( spec._targetName, targetCollider._target ) == false )
                            return false;
                        _listTargetCollider.push_back( targetCollider );
                        continue;
                    }
                    RigSpringCollider collider = spec._collider;
                    if ( context.findBone( spec._boneName, collider._bone ) == false )
                        return false;
                    _listCollider.push_back( collider );
                }
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                const vector<RigSpringCollider>& listShared = context._pInstance->getSharedColliders();
                const bool                       bShared    = _params._bUseSharedColliders && listShared.empty() == false;
                if ( bShared == false && _listTargetCollider.empty() )
                {
                    _chain.simulate( *context._pPose, _listBone, _settings, _listCollider, context._worldFromModel, context._worldGravity, context._deltaSeconds, *context._pSpace );
                    return;
                }
                _listScratchCollider.assign( _listCollider.begin(), _listCollider.end() );
                if ( bShared )
                    _listScratchCollider.insert( _listScratchCollider.end(), listShared.begin(), listShared.end() );
                // 목표에 붙은 충돌체는 이번 프레임 목표 자리로 모델 공간에 옮긴다. 목표가 아직 풀리지 않았으면(유닛이 붙기 전) 그 충돌체는 이번 프레임에 없다.
                for ( const TargetCollider& targetCollider : _listTargetCollider )
                {
                    float3     position{};
                    quaternion rotation{};
                    if ( context._pInstance->resolveTarget( targetCollider._target, *context._pPose, position, rotation ) == false )
                        continue;
                    RigSpringCollider collider = targetCollider._collider;
                    collider._pointA           = position + float3::transform( collider._pointA, rotation );
                    collider._pointB           = position + float3::transform( collider._pointB, rotation );
                    collider._bone             = RigSpringCollider::kModelSpaceBone;
                    _listScratchCollider.push_back( collider );
                }
                _chain.simulate( *context._pPose, _listBone, _settings, _listScratchCollider, context._worldFromModel, context._worldGravity, context._deltaSeconds, *context._pSpace );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override { inoutListBone.insert( inoutListBone.end(), _listBone.begin(), _listBone.end() ); }

            void reset() override { _chain.reset(); }

        private:
            RigSpringChainParams _params{}; ///< 파일 키 · 기본값 · 범위(RigNodeParams.h)
            struct ColliderSpec
            {
                RigSpringCollider _collider{};
                hashed_string     _boneName{};
                hashed_string     _targetName{};
            };
            /** @brief 리그 목표에 붙은 충돌체입니다. 점은 목표 공간이다. */
            struct TargetCollider
            {
                RigSpringCollider _collider{};
                uint32            _target{ 0 };
            };

            vector<hashed_string>     _listBoneName{};
            vector<uint32>            _listBone{};
            vector<ColliderSpec>      _listColliderSpec{};
            vector<RigSpringCollider> _listCollider{};
            vector<TargetCollider>    _listTargetCollider{}; ///< 리그 목표(다른 유닛의 본)에 붙은 충돌체
            vector<RigSpringCollider> _listScratchCollider{};
            RigSpringChain            _chain{};
            RigSpringSettings         _settings{};
        };

        /**
         * @brief 포즈 구동(RBF 포즈 드라이버)입니다. 키는 `driver`(관절), `radius_degrees`(가우스 폭), `poses`(`[{ "name", "rotation", "morphs": [{ "morph", "weight" }],
         *        "bones": [{ "bone", "rotation", "translation" }] }]`)입니다.
         * @details 관절의 레퍼런스 기준 회전과 포즈마다의 회전 사이 각으로 가우스 핵을 구하고, 바인딩할 때 푼 보간 행렬(Φ⁻¹)로 포즈 가중치를 낸다.
         *          포즈 위치에서는 그 포즈만 1 이다. 가중치만큼 보정 모프(`RigInstance::getMorphWeights`)와 보정 본(로컬 회전과 이동 덧셈)을 낸다.
         *          팔꿈치를 굽히면 팔뚝 근육을 보정하는 식이며, 언리얼의 Pose Driver 와 Maya 의 RBF 에 해당한다. 쉬는 포즈(출력 없음)도 데이터에 한 줄 넣는다.
         */
        class RigPoseDriverNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigPoseDriverNode>( *this ); }
            const utf8*         getTypeName() const override { return "PoseDriver"; }

            [[nodiscard]] bool parse( RigXMLReader& reader ) override
            {

                bool bOk               = reader.readProperties( _params ); // 1..180 도는 PROPERTY 가 본다
                _radius                = _params._radiusDegrees * MathUtil::kDegreeToRadian;
                const RigXMLList poses = reader.readArray( "poses", true );
                for ( size_t index = 0; bOk && poses.isValid() && index < poses.size(); ++index )
                {
                    RigXMLReader        poseReader( poses.at( index ), reader.getContext() );
                    RigDriverPoseParams poseParams;
                    bOk = poseReader.readProperties( poseParams );
                    DriverPose pose{};
                    pose._name              = poseParams._name;
                    pose._rotation          = quaternion::makeFromYawPitchRoll( poseParams._rotation * MathUtil::kDegreeToRadian );
                    const RigXMLList morphs = poseReader.readArray( "morphs", false );
                    for ( size_t morphIndex = 0; bOk && morphs.isValid() && morphIndex < morphs.size(); ++morphIndex )
                    {
                        RigXMLReader         morphReader( morphs.at( morphIndex ), reader.getContext() );
                        RigDriverMorphParams morphProperties;
                        bOk = morphReader.readProperties( morphProperties );
                        bOk = morphReader.finish() && bOk;
                        RigMorphWeight morph{};
                        morph._name   = morphProperties._morph;
                        morph._weight = morphProperties._weight;
                        pose._listMorph.push_back( morph );
                    }
                    const RigXMLList bones = poseReader.readArray( "bones", false );
                    for ( size_t boneIndex = 0; bOk && bones.isValid() && boneIndex < bones.size(); ++boneIndex )
                    {
                        RigXMLReader            boneReader( bones.at( boneIndex ), reader.getContext() );
                        RigCorrectiveBoneParams boneProperties;
                        bOk = boneReader.readProperties( boneProperties );
                        bOk = boneReader.finish() && bOk;
                        CorrectiveBone corrective{};
                        corrective._name        = boneProperties._bone;
                        corrective._rotation    = quaternion::makeFromYawPitchRoll( boneProperties._rotation * MathUtil::kDegreeToRadian );
                        corrective._translation = boneProperties._translation;
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

            [[nodiscard]] bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _params._driver, _driver ) == false )
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
                        const float32 angle              = quaternion::getAngleBetween( _listPose[row]._rotation, _listPose[column]._rotation );
                        kernel[row * poseCount + column] = RigSecondaryNodesInternal::computeKernel( angle, _radius );
                    }
                }
                if ( RigSecondaryNodesInternal::invertMatrix( kernel, poseCount, _listInverseKernel ) == false )
                {
                    SW_LOG_ERROR_ID( "RigSecondaryNodes.RigPoseDriverTwo", "Rig '%#': pose driver '%#' has two poses at the same rotation", context._label, getName().c_str() );
                    return false;
                }
                _listPoseWeight.assign( poseCount, 0.0f );
                _listKernel.assign( poseCount, 0.0f );
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer&   pose      = *context._pPose;
                const quaternion current   = ( RigIKSolver::makeInverse( _driverReference ) * pose.getLocalRotation( _driver ) ).normalize();
                const uint32     poseCount = static_cast<uint32>( _listPose.size() );
                for ( uint32 index = 0; index < poseCount; ++index )
                {
                    _listKernel[index] = RigSecondaryNodesInternal::computeKernel( quaternion::getAngleBetween( current, _listPose[index]._rotation ), _radius );
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
                        if ( VectorUtil::contains( inoutListBone, corrective._bone ) == false )
                            inoutListBone.push_back( corrective._bone );
                    }
                }
            }

        private:
            RigPoseDriverParams _params{}; ///< 파일 키 · 기본값 · 범위(RigNodeParams.h)
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
    void RigSecondaryNodes::registerNodes( RigNodeRegistry& registry )
    {
        registry.registerNode( "SpringChain", &RigSecondaryNodesFactory::createSpringChain );
        registry.registerNode( "PoseDriver", &RigSecondaryNodesFactory::createPoseDriver );
    }
} // namespace sw
