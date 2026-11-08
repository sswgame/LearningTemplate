#include "pch.h"

#include "Engine/Animation/Rig/RigIKNodes.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct RigIKNodesInternal
        {
            /** @brief 보폭 워핑이 다리를 펴는 최대 비율(다리 길이 대비)입니다. 다 펴면 2 본 IK 의 무릎 방향이 정해지지 않는다. */
            static constexpr float32 kMaxLegExtension = 0.995f;

            /** @brief 스켈레톤 레퍼런스 포즈의 모델 공간 변환입니다(묶을 때 한 번). */
            static void computeReferenceModel( const Skeleton& skeleton, vector<float4x4>& outListModel )
            {
                Pose reference;
                reference.setToReference( skeleton );
                reference.computeModelSpace( skeleton.getParentIndices(), outListModel );
            }

            /**
             * @brief 사슬의 관절 제한을 읽습니다. 형식은 `[{ "bone", "type": "Cone"|"Hinge", "swing_degrees", "twist_degrees", "axis", "min_degrees", "max_degrees" }]` 입니다.
             * @details 본 이름은 바인딩할 때 사슬 인덱스로 바꾼다(여기서는 이름과 값만 읽는다).
             */
            struct LimitSpec
            {
                hashed_string _bone{};
                RigJointLimit _limit{};
            };

            [[nodiscard]] static bool parseLimits( RigXMLReader& reader, vector<LimitSpec>& outListSpec )
            {
                const RigXMLList limits = reader.readArray( "limits", false );
                if ( reader.isOk() == false )
                    return false;
                for ( size_t index = 0; limits.isValid() && index < limits.size(); ++index )
                {
                    RigXMLReader        limitReader( limits.at( index ), reader.getContext() );
                    RigJointLimitParams params;
                    bool                bOk = limitReader.readFields( params );
                    bOk                     = limitReader.finish() && bOk;
                    if ( bOk == false || params._type == RigJointLimitType::None )
                    {
                        reader.fail( "malformed limit (type is Cone or Hinge)" );
                        return false;
                    }
                    LimitSpec spec{};
                    spec._bone              = params._bone;
                    spec._limit._type       = params._type;
                    spec._limit._swingLimit = params._swingDegrees * MathUtil::kDegreeToRadian;
                    spec._limit._twistLimit = params._twistDegrees * MathUtil::kDegreeToRadian;
                    spec._limit._minAngle   = params._minDegrees * MathUtil::kDegreeToRadian;
                    spec._limit._maxAngle   = params._maxDegrees * MathUtil::kDegreeToRadian;
                    spec._limit._hingeAxis  = params._axis.normalize();
                    if ( spec._limit._hingeAxis.getLengthSquared() < 0.5f )
                        spec._limit._hingeAxis = float3::UnitX;
                    outListSpec.push_back( spec );
                }
                return true;
            }

            /** @brief 다리 목록(`feet`)을 읽습니다 — 발 디딤 · 보폭 워핑이 함께 쓴다. 비면 실패입니다. */
            template <typename TFoot>
            [[nodiscard]] static bool parseLegs( RigXMLReader& reader, vector<TFoot>& outListFoot )
            {
                const RigXMLList feet = reader.readArray( "feet", true );
                bool             bOk  = reader.isOk();
                for ( size_t index = 0; bOk && feet.isValid() && index < feet.size(); ++index )
                {
                    RigXMLReader footReader( feet.at( index ), reader.getContext() );
                    RigLegParams leg;
                    bOk = footReader.readFields( leg );
                    bOk = footReader.finish() && bOk;
                    TFoot foot{};
                    foot._rootName = leg._root;
                    foot._midName  = leg._mid;
                    foot._endName  = leg._end;
                    outListFoot.push_back( foot );
                }
                if ( bOk && outListFoot.empty() )
                {
                    reader.fail( "'feet' must not be empty" );
                    return false;
                }
                return bOk;
            }

            /** @brief 제한 이름을 사슬 인덱스로 옮기고, 레퍼런스 회전과 본 축(본에서 다음 사슬 본으로 가는 방향, 본 자기 공간)을 채웁니다. */
            [[nodiscard]] static bool bindLimits( const RigBindContext& context, const vector<LimitSpec>& listSpec, const vector<uint32>& listChainBone,
                                                  vector<RigJointLimit>& outListLimit )
            {
                outListLimit.assign( listChainBone.size(), RigJointLimit{} );
                if ( listSpec.empty() )
                {
                    outListLimit.clear();
                    return true;
                }
                vector<float4x4> listReference;
                computeReferenceModel( *context._pSkeleton, listReference );
                for ( const LimitSpec& spec : listSpec )
                {
                    uint32 bone = 0;
                    if ( context.findBone( spec._bone, bone ) == false )
                        return false;
                    const optional<size_t> chainFound = VectorUtil::findIndex( listChainBone, bone );
                    if ( chainFound.has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "RigIKNodes.RigLimitBoneNot", "Rig '%#': limit bone '%#' is not in the chain", context._label, spec._bone.c_str() );
                        return false;
                    }
                    const size_t   chainIndex = *chainFound;
                    RigJointLimit& limit      = outListLimit[chainIndex];
                    limit                     = spec._limit;
                    limit._referenceRotation  = context._pSkeleton->getBone( bone )._referencePose._rotation;
                    if ( chainIndex + 1 < listChainBone.size() )
                    {
                        const float3 toNext   = listReference[listChainBone[chainIndex + 1]].getTranslation() - listReference[bone].getTranslation();
                        const float3 boneAxis = float3::transform( toNext, RigIKSolver::makeInverse( listReference[bone].getRotation() ) ).normalize();
                        limit._boneAxis       = ( boneAxis.getLengthSquared() > 0.5f ) ? boneAxis : float3::UnitY;
                    }
                }
                return true;
            }
        };
        /**
         * @brief 2 본 IK(팔, 다리, 손 IK)입니다. 키는 `root`, `mid`, `end`, `target`, `pole`(대상, 선택), `match_rotation`(끝 본 회전을 대상 회전으로 맞춤),
         *        `keep_end_rotation`(기본 true, 끝 본의 모델 회전을 애니메이션 값으로 유지)입니다.
         * @details 왼손을 무기 손잡이 소켓에 붙이려면 `target` 을 무기 유닛의 `Grip` 소켓(space = 오른손)으로 두고 `match_rotation` 을 켠다. 그러면 손이 손잡이 방향으로 돈다.
         *          손을 벽 · 난간에 짚게 하려면 `target` 을 접촉 대상(`trace_from` — `RigTargetKind::Trace`)으로 둔다. 대상 가중치(닿고 놓을 때 `blend_speed` 로 오르내린다)만큼
         *          목표가 애니메이션 손 자리에서 접촉점으로 옮겨 가므로 손이 튀지 않는다.
         */
        class RigTwoBoneIKNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigTwoBoneIKNode>( *this ); }
            const utf8*         getTypeName() const override { return "TwoBoneIK"; }

            [[nodiscard]] bool parse( RigXMLReader& reader ) override
            {

                return reader.readFields( _params );
            }

            [[nodiscard]] bool bind( const RigBindContext& context ) override
            {
                vector<uint32> listBone;
                if ( context.findBones( { _params._root, _params._mid, _params._end }, listBone, true ) == false || context.findTarget( _params._target, _target ) == false )
                    return false;
                _root = listBone[0];
                _mid  = listBone[1];
                _end  = listBone[2];
                _pole = MathUtil::kMaxUInt32;
                if ( _params._pole.empty() == false && context.findTarget( _params._pole, _pole ) == false )
                    return false;
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer& pose = *context._pPose;
                float3         targetPosition{};
                quaternion     targetRotation{};
                if ( context._pInstance->resolveTarget( _target, pose, targetPosition, targetRotation ) == false )
                    return;
                const float32 targetWeight = context._pInstance->getTargetWeight( _target );
                if ( targetWeight < 1.0f )
                {
                    const quaternion endAnimated = pose.getModelRotation( _end );
                    targetPosition               = float3::lerp( pose.getModelPosition( _end ), targetPosition, targetWeight );
                    targetRotation               = quaternion::slerp( endAnimated, targetRotation, targetWeight );
                }
                float3           polePosition{};
                quaternion       poleRotation{};
                const bool       bPole     = _pole != MathUtil::kMaxUInt32 && context._pInstance->resolveTarget( _pole, pose, polePosition, poleRotation );
                const quaternion endBefore = pose.getModelRotation( _end );
                (void)RigIKSolver::solveTwoBone( pose, _root, _mid, _end, targetPosition, bPole ? &polePosition : nullptr, *context._pSpace );
                if ( _params._bMatchRotation )
                    pose.setModelRotation( _end, targetRotation );
                else if ( _params._bKeepEndRotation )
                    pose.setModelRotation( _end, endBefore );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override
            {
                inoutListBone.push_back( _root );
                inoutListBone.push_back( _mid );
                inoutListBone.push_back( _end );
            }

        private:
            RigTwoBoneIKParams _params{}; ///< 파일 키 · 기본값 · 범위(RigNodeParams.h)
            uint32             _root{ 0 };
            uint32             _mid{ 0 };
            uint32             _end{ 0 };
            uint32             _target{ 0 };
            uint32             _pole{ MathUtil::kMaxUInt32 };
        };

        /**
         * @brief 사슬 IK(FABRIK 또는 CCD)입니다. 키는 `bones`(루트에서 끝으로), `target`, `iterations`, `tolerance`, `max_step_degrees`(CCD), `limits`, `match_rotation` 입니다.
         */
        class RigChainIKNode final : public RigNode
        {
        public:
            /** @brief FABRIK 인지 CCD 인지 정합니다(만들 때 한 번). */
            void setFABRIK( bool bFABRIK ) { _bFABRIK = bFABRIK ? SW_TRUE : SW_FALSE; }

            unique_ptr<RigNode> clone() const override { return make_unique<RigChainIKNode>( *this ); }
            const utf8*         getTypeName() const override { return _bFABRIK == SW_TRUE ? "FABRIKChain" : "CCDChain"; }

            [[nodiscard]] bool parse( RigXMLReader& reader ) override
            {

                // 한 관절의 회전 상한은 CCD 만 받는다 — FABRIK 은 사슬 공통 칸만 읽는다(그 키를 적으면 모르는 키다).
                bool bOk                  = ( _bFABRIK == SW_TRUE ) ? reader.readFields( static_cast<RigChainIKParams&>( _params ) ) : reader.readFields( _params );
                _settings._iterationCount = _params._iterations;
                _settings._tolerance      = _params._tolerance;
                _settings._maxStepAngle   = _params._maxStepDegrees * MathUtil::kDegreeToRadian;
                bOk                       = reader.readNameList( "bones", _listBoneName, true ) && bOk;
                bOk                       = bOk && RigIKNodesInternal::parseLimits( reader, _listLimitSpec );
                if ( bOk && _listBoneName.size() < 2 )
                {
                    reader.fail( "'bones' needs at least two bones" );
                    return false;
                }
                return bOk;
            }

            [[nodiscard]] bool bind( const RigBindContext& context ) override
            {
                return context.findBones( _listBoneName, _listBone, true ) && context.findTarget( _params._target, _target ) &&
                       RigIKNodesInternal::bindLimits( context, _listLimitSpec, _listBone, _listLimit );
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer& pose = *context._pPose;
                float3         targetPosition{};
                quaternion     targetRotation{};
                if ( context._pInstance->resolveTarget( _target, pose, targetPosition, targetRotation ) == false )
                    return;
                if ( _bFABRIK == SW_TRUE )
                    (void)RigIKSolver::solveFABRIK( pose, _listBone, targetPosition, _listLimit, _settings, *context._pSpace );
                else
                    (void)RigIKSolver::solveCCD( pose, _listBone, targetPosition, _listLimit, _settings, *context._pSpace );
                if ( _params._bMatchRotation )
                    pose.setModelRotation( _listBone.back(), targetRotation );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override { inoutListBone.insert( inoutListBone.end(), _listBone.begin(), _listBone.end() ); }

        private:
            RigCCDChainParams                     _params{}; ///< 파일 키 · 기본값 · 범위(RigNodeParams.h)
            vector<hashed_string>                 _listBoneName{};
            vector<RigIKNodesInternal::LimitSpec> _listLimitSpec{};
            vector<uint32>                        _listBone{};
            vector<RigJointLimit>                 _listLimit{};
            RigChainSettings                      _settings{};
            uint32                                _target{ 0 };
            uint8                                 _bFABRIK{ SW_TRUE };
        };

        /**
         * @brief 조준과 시선(Aim, LookAt)입니다. 키는 `bone`, `target`, `aim_axis`(본 로컬, 기본 +Z), `max_degrees`(애니메이션 방향에서 벗어날 수 있는 상한),
         *        `chain`(`[{ "bone", "weight", "max_degrees" }]`, 조준 본의 조상들이 루트 쪽부터 회전을 나눠 받음 — 본마다 상한이 따로다)입니다.
         * @details 등뼈 → 목 → 머리 순으로 남은 회전의 몫을 받고, 본 하나가 상한에 막히면 남은 몫은 다음 본이 받는다(마지막 조준 본이 나머지를 맡는다).
         *          언리얼 Look At · Control Rig Aim 체인과 같은 분배다.
         */
        class RigAimNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigAimNode>( *this ); }
            const utf8*         getTypeName() const override { return "Aim"; }

            [[nodiscard]] bool parse( RigXMLReader& reader ) override
            {

                bool bOk               = reader.readFields( _params ); // 0..180 도는 PROPERTY 가 본다
                _maxAngle              = _params._maxDegrees * MathUtil::kDegreeToRadian;
                _aimAxis               = _params._aimAxis.normalize();
                const RigXMLList chain = reader.readArray( "chain", false );
                for ( size_t index = 0; bOk && chain.isValid() && index < chain.size(); ++index )
                {
                    RigXMLReader      entryReader( chain.at( index ), reader.getContext() );
                    RigAimChainParams entryParams;
                    bOk = entryReader.readFields( entryParams );
                    bOk = entryReader.finish() && bOk;
                    ChainEntry entry{};
                    entry._name     = entryParams._bone;
                    entry._weight   = entryParams._weight;
                    entry._maxAngle = entryParams._maxDegrees * MathUtil::kDegreeToRadian;
                    _listChain.push_back( entry );
                }
                if ( bOk && _aimAxis.getLengthSquared() < 0.5f )
                {
                    reader.fail( "'aim_axis' must not be zero" );
                    return false;
                }
                return bOk && reader.isOk();
            }

            [[nodiscard]] bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _params._bone, _bone ) == false || context.findTarget( _params._target, _target ) == false )
                    return false;
                for ( ChainEntry& entry : _listChain )
                {
                    if ( context.findBone( entry._name, entry._bone ) == false )
                        return false;
                }
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer&       pose  = *context._pPose;
                const RigSolveSpace& space = *context._pSpace;
                float3               targetPosition{};
                quaternion           targetRotation{};
                if ( context._pInstance->resolveTarget( _target, pose, targetPosition, targetRotation ) == false )
                    return;
                // 상한은 애니메이션 방향 기준이다. 목표 방향을 그 원뿔 안으로 먼저 잘라 둔다.
                const float3 bonePosition = pose.getModelPosition( _bone );
                const float3 animated     = space.projectVector( float3::transform( _aimAxis, pose.getModelRotation( _bone ) ) ).normalize();
                const float3 toTarget     = space.projectVector( targetPosition - bonePosition );
                if ( animated.getLengthSquared() < 0.5f || toTarget.getLengthSquared() < 1e-8f )
                    return;
                const float32    distance = toTarget.getLength();
                const quaternion turn     = RigAimNode::limitTurn( RigIKSolver::makeFromToRotation( animated, toTarget ), _maxAngle );
                const float3     aimPoint = bonePosition + float3::transform( animated, turn ) * distance;

                for ( const ChainEntry& entry : _listChain )
                {
                    const float3 currentAim = space.projectVector( float3::transform( _aimAxis, pose.getModelRotation( _bone ) ) );
                    const float3 desired    = space.projectVector( aimPoint - pose.getModelPosition( _bone ) );
                    if ( currentAim.getLengthSquared() < 1e-8f || desired.getLengthSquared() < 1e-8f )
                        continue;
                    const quaternion delta = RigIKSolver::makeFromToRotation( currentAim, desired );
                    pose.rotateModel( entry._bone, RigAimNode::limitTurn( quaternion::slerp( quaternion::Identity, delta, entry._weight ), entry._maxAngle ) );
                }
                (void)RigIKSolver::aimBone( pose, _bone, _aimAxis, aimPoint, MathUtil::kPi, 1.0f, space );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override
            {
                for ( const ChainEntry& entry : _listChain )
                {
                    inoutListBone.push_back( entry._bone );
                }
                inoutListBone.push_back( _bone );
            }

        private:
            /** @brief 회전 @p turn 을 @p maxAngle(라디안) 이하로 줄입니다(축은 그대로). */
            static quaternion limitTurn( const quaternion& turn, float32 maxAngle )
            {
                const float32 angle = 2.0f * MathUtil::acos( MathUtil::clamp( MathUtil::abs( turn._w ), 0.0f, 1.0f ) );
                if ( angle > maxAngle && angle > MathUtil::kEpsilon )
                    return quaternion::slerp( quaternion::Identity, turn, maxAngle / angle );
                return turn;
            }

        private:
            RigAimParams _params{}; ///< 파일 키 · 기본값 · 범위(RigNodeParams.h)
            struct ChainEntry
            {
                hashed_string _name{};
                float32       _weight{ 0.0f };
                float32       _maxAngle{ MathUtil::kPi }; ///< 이 본이 애니메이션 방향에서 돌 수 있는 상한(라디안)
                uint32        _bone{ 0 };
            };

            vector<ChainEntry> _listChain{};
            float3             _aimAxis{ 0.0f, 0.0f, 1.0f };
            float32            _maxAngle{ MathUtil::kPi };
            uint32             _bone{ 0 };
            uint32             _target{ 0 };
        };

        /**
         * @brief 발 디딤(Foot Placement)입니다. 키는 `pelvis`, `feet`(`[{ "root", "mid", "end" }]`), `trace_up`, `trace_down`, `max_pelvis_drop`, `max_raise`,
         *        `interp_speed`(초당 따라잡는 비율), `align_to_normal`, `max_align_degrees` 입니다.
         * @details 게임 스레드 준비 단계에서 발마다(지난 평가의 애니메이션 발 위치) 위에서 아래로 땅 광선을 쏜다. 평가에서는 발마다 땅 높이(유닛 바닥 기준)를
         *          부드럽게 따라가고, 낮은 쪽만큼 골반을 내린 뒤, 2 본 IK 로 발을 땅에 올리고 법선에 맞춰 돌린다. 언리얼의 Foot Placement 와 Leg IK 에 해당한다.
         */
        class RigFootPlacementNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigFootPlacementNode>( *this ); }
            const utf8*         getTypeName() const override { return "FootPlacement"; }

            [[nodiscard]] bool parse( RigXMLReader& reader ) override
            {

                bool bOk       = reader.readFields( _params );
                _maxAlignAngle = _params._maxAlignDegrees * MathUtil::kDegreeToRadian;
                bOk            = RigIKNodesInternal::parseLegs( reader, _listFoot ) && bOk;
                return bOk && reader.isOk();
            }

            [[nodiscard]] bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _params._pelvis, _pelvis ) == false )
                    return false;
                vector<float4x4> listReference;
                RigIKNodesInternal::computeReferenceModel( *context._pSkeleton, listReference );
                for ( Foot& foot : _listFoot )
                {
                    vector<uint32> listBone;
                    if ( context.findBones( { foot._rootName, foot._midName, foot._endName }, listBone, true ) == false )
                        return false;
                    foot._root          = listBone[0];
                    foot._mid           = listBone[1];
                    foot._end           = listBone[2];
                    foot._animatedModel = listReference[foot._end].getTranslation();
                }
                return true;
            }

            void prepare( const RigPrepareContext& context ) override
            {
                const float3   up             = float3::transformVector( float3::UnitY, context._worldFromModel ).normalize();
                const float4x4 modelFromWorld = context._worldFromModel.invert();
                // 출력 사본은 게임 스레드에서만 쓴다 — 지난 평가의 골반 · 발 높이(`getOutputValue`).
                _outputPelvisOffset = _pelvisOffset;
                _outputHitCount     = 0;
                for ( Foot& foot : _listFoot )
                {
                    foot._outputOffset = foot._offset;
                    _outputHitCount += foot._bHit == SW_TRUE ? 1u : 0u;
                    foot._bHit = SW_FALSE;
                    if ( context._pGroundQuery == nullptr )
                        continue;
                    // 발 위치(바닥 높이로 내린 값) 위 trace_up 에서 아래로 trace_up + trace_down 만큼 쏜다.
                    const float3 footOnFloor{ foot._animatedModel._x, 0.0f, foot._animatedModel._z };
                    const float3 origin = float3::transform( footOnFloor, context._worldFromModel ) + up * _params._traceUp;
                    RigGroundHit hit{};
                    if ( context._pGroundQuery->raycastGround( origin, -up, _params._traceUp + _params._traceDown, hit ) == false )
                        continue;
                    foot._hitModel    = float3::transform( hit._position, modelFromWorld );
                    foot._normalModel = float3::transformVector( hit._normal, modelFromWorld ).normalize();
                    foot._bHit        = SW_TRUE;
                }
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer& pose   = *context._pPose;
                const float32  blend  = ( _params._interpSpeed > 0.0f ) ? MathUtil::saturate( _params._interpSpeed * context._deltaSeconds ) : 1.0f;
                float32        lowest = 0.0f;
                for ( Foot& foot : _listFoot )
                {
                    foot._animatedModel = pose.getModelPosition( foot._end );
                    const float32 goal  = ( foot._bHit == SW_TRUE ) ? MathUtil::clamp( foot._hitModel._y, -_params._maxPelvisDrop, _params._maxRaise ) : 0.0f;
                    foot._offset        = foot._bInitialized == SW_TRUE ? MathUtil::lerp( foot._offset, goal, blend ) : goal;
                    foot._bInitialized  = SW_TRUE;
                    lowest              = MathUtil::min( lowest, foot._offset );
                }
                // 골반은 가장 낮은 발만큼 내린다. 높은 쪽 발은 무릎을 굽혀 올린다.
                const float32 pelvisGoal = MathUtil::max( lowest, -_params._maxPelvisDrop );
                _pelvisOffset            = _bPelvisInitialized == SW_TRUE ? MathUtil::lerp( _pelvisOffset, pelvisGoal, blend ) : pelvisGoal;
                _bPelvisInitialized      = SW_TRUE;
                pose.setModelPosition( _pelvis, pose.getModelPosition( _pelvis ) + float3{ 0.0f, _pelvisOffset, 0.0f } );

                for ( const Foot& foot : _listFoot )
                {
                    const quaternion footRotation = pose.getModelRotation( foot._end );
                    const float3     goal         = foot._animatedModel + float3{ 0.0f, foot._offset, 0.0f };
                    (void)RigIKSolver::solveTwoBone( pose, foot._root, foot._mid, foot._end, goal, nullptr, *context._pSpace );
                    quaternion aligned = footRotation;
                    if ( _params._bAlignToNormal && foot._bHit == SW_TRUE )
                    {
                        quaternion    tilt  = RigIKSolver::makeFromToRotation( float3::UnitY, foot._normalModel );
                        const float32 angle = 2.0f * MathUtil::acos( MathUtil::clamp( MathUtil::abs( tilt._w ), 0.0f, 1.0f ) );
                        if ( angle > _maxAlignAngle && angle > MathUtil::kEpsilon )
                            tilt = quaternion::slerp( quaternion::Identity, tilt, _maxAlignAngle / angle );
                        aligned = ( tilt * footRotation ).normalize();
                    }
                    pose.setModelRotation( foot._end, aligned );
                }
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override
            {
                inoutListBone.push_back( _pelvis );
                for ( const Foot& foot : _listFoot )
                {
                    inoutListBone.push_back( foot._root );
                    inoutListBone.push_back( foot._mid );
                    inoutListBone.push_back( foot._end );
                }
            }

            void reset() override
            {
                _bPelvisInitialized = SW_FALSE;
                for ( Foot& foot : _listFoot )
                {
                    foot._bInitialized = SW_FALSE;
                }
            }

            /** @brief 출력: `pelvis_offset`(골반을 옮긴 높이, 미터 · 음수면 내림), `foot_offset_<번호>`(`feet` 순서의 발 높이 보정), `ground_hits`(땅에 닿은 발 수). */
            [[nodiscard]] bool getOutputValue( const hashed_string& output, float32& outValue ) const override
            {
                static constexpr string_view kFootPrefix = "foot_offset_";
                const string_view            name        = output.c_str();
                if ( name == "pelvis_offset" )
                {
                    outValue = _outputPelvisOffset;
                    return true;
                }
                if ( name == "ground_hits" )
                {
                    outValue = static_cast<float32>( _outputHitCount );
                    return true;
                }
                if ( name.size() != kFootPrefix.size() + 1 || name.substr( 0, kFootPrefix.size() ) != kFootPrefix )
                    return false;
                const uint32 footIndex = static_cast<uint32>( name.back() - '0' );
                if ( footIndex >= _listFoot.size() )
                    return false;
                outValue = _listFoot[footIndex]._outputOffset;
                return true;
            }

        private:
            RigFootPlacementParams _params{}; ///< 파일 키 · 기본값 · 범위(RigNodeParams.h)
            struct Foot
            {
                hashed_string _rootName{};
                hashed_string _midName{};
                hashed_string _endName{};
                float3        _animatedModel{};
                float3        _hitModel{};
                float3        _normalModel{ 0.0f, 1.0f, 0.0f };
                float32       _offset{ 0.0f };
                float32       _outputOffset{ 0.0f }; ///< 게임 스레드 사본(준비 단계가 `_offset` 에서 옮긴다)
                uint32        _root{ 0 };
                uint32        _mid{ 0 };
                uint32        _end{ 0 };
                uint8         _bHit{ SW_FALSE };
                uint8         _bInitialized{ SW_FALSE };
            };

            vector<Foot> _listFoot{};
            float32      _maxAlignAngle{ 0.5f };
            float32      _pelvisOffset{ 0.0f };
            float32      _outputPelvisOffset{ 0.0f }; ///< 게임 스레드 사본(준비 단계가 `_pelvisOffset` 에서 옮긴다)
            uint32       _outputHitCount{ 0 };        ///< 지난 준비 단계에서 땅에 닿은 발 수
            uint32       _pelvis{ 0 };
            uint8        _bPelvisInitialized{ SW_FALSE };
        };

        /**
         * @brief 보폭 워핑(Stride Warping)입니다. 키는 `pelvis`, `feet`(`[{ "root", "mid", "end" }]`), `min_scale`, `max_scale`, `max_pelvis_drop` 이고 모두 필수입니다.
         * @details 재생 속도 매칭(`LocomotionSpeedUtil::computePlayRate`)이 범위에 막혀 남긴 몫을 보폭으로 맞춘다. 발마다 골반에서 이동 방향으로 떨어진 거리를
         *          배율만큼 늘리거나 줄이고(옆 성분은 그대로), 다리가 닿지 않으면 그만큼 골반을 내린 뒤(`max_pelvis_drop` 까지), 2 본 IK 로 발을 옮긴다.
         *          발의 모델 회전은 애니메이션 값을 지킨다. 배율과 방향은 게임이 프레임마다 조절 값으로 넣는다 — `stride_scale`
         *          (`LocomotionSpeedUtil::computeStrideScale`, `[min_scale, max_scale]` 로 자름), `direction_x`, `direction_z`(모델 공간 수평 이동 방향).
         *          배율이 1 이거나 방향이 없으면 포즈를 바꾸지 않는다. 언리얼 Stride Warping 노드(배율 · 골반 내리기)에 해당한다.
         */
        class RigStrideWarpingNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigStrideWarpingNode>( *this ); }
            const utf8*         getTypeName() const override { return "StrideWarping"; }

            [[nodiscard]] bool parse( RigXMLReader& reader ) override
            {

                bool bOk                    = reader.readFields( _params ); // 골반 내림 >= 0 은 PROPERTY 가 본다
                bOk                         = RigIKNodesInternal::parseLegs( reader, _listFoot ) && bOk;
                const bool bScaleRangeValid = 0.0f < _params._minScale && _params._minScale <= _params._maxScale;
                if ( bOk && bScaleRangeValid == false )
                {
                    reader.fail( "'min_scale' must be above 0 and not above 'max_scale'" );
                    return false;
                }
                return bOk && reader.isOk();
            }

            [[nodiscard]] bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _params._pelvis, _pelvis ) == false )
                    return false;
                vector<float4x4> listReference;
                RigIKNodesInternal::computeReferenceModel( *context._pSkeleton, listReference );
                for ( Foot& foot : _listFoot )
                {
                    vector<uint32> listBone;
                    if ( context.findBones( { foot._rootName, foot._midName, foot._endName }, listBone, true ) == false )
                        return false;
                    foot._root = listBone[0];
                    foot._mid  = listBone[1];
                    foot._end  = listBone[2];
                    // 다리 길이는 레퍼런스 포즈의 두 마디 길이 합이다(뼈 길이는 애니메이션이 바꾸지 않는다고 본다).
                    const float3 hip   = listReference[foot._root].getTranslation();
                    const float3 knee  = listReference[foot._mid].getTranslation();
                    const float3 ankle = listReference[foot._end].getTranslation();
                    foot._legLength    = ( knee - hip ).getLength() + ( ankle - knee ).getLength();
                }
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                const float32 scale           = MathUtil::clamp( _strideScale, _params._minScale, _params._maxScale );
                const float32 directionLength = MathUtil::sqrt( _directionX * _directionX + _directionZ * _directionZ );
                const bool    bNoWarp         = MathUtil::abs( scale - 1.0f ) <= MathUtil::kEpsilon || directionLength <= MathUtil::kEpsilon;
                if ( bNoWarp )
                    return;
                const float3   direction{ _directionX / directionLength, 0.0f, _directionZ / directionLength };
                RigPoseBuffer& pose   = *context._pPose;
                const float3   pelvis = pose.getModelPosition( _pelvis );

                // 1) 발 목표 — 골반에서 이동 방향으로 떨어진 거리만 배율만큼(옆 · 높이는 그대로).
                for ( Foot& foot : _listFoot )
                {
                    const float3  footPosition = pose.getModelPosition( foot._end );
                    const float32 along        = ( footPosition - pelvis ).dot( direction );
                    foot._goal                 = footPosition + direction * ( along * ( scale - 1.0f ) );
                    foot._rotation             = pose.getModelRotation( foot._end );
                }

                // 2) 골반 — 가장 멀리 뻗은 다리가 닿을 만큼 내린다.
                float32 drop = 0.0f;
                for ( const Foot& foot : _listFoot )
                {
                    const float3  hip            = pose.getModelPosition( foot._root );
                    const float32 reach          = foot._legLength * RigIKNodesInternal::kMaxLegExtension;
                    const float3  toGoal         = foot._goal - hip;
                    const float32 horizontalSq   = toGoal._x * toGoal._x + toGoal._z * toGoal._z;
                    const float32 reachHeightSq  = reach * reach - horizontalSq;
                    const float32 reachHeight    = reachHeightSq > 0.0f ? MathUtil::sqrt( reachHeightSq ) : 0.0f;
                    const float32 neededVertical = -toGoal._y; // 엉덩이가 발보다 높은 만큼이다
                    drop                         = MathUtil::max( drop, neededVertical - reachHeight );
                }
                drop = MathUtil::min( drop, _params._maxPelvisDrop );
                if ( drop > 0.0f )
                    pose.setModelPosition( _pelvis, pelvis - float3{ 0.0f, drop, 0.0f } );

                // 3) 발 — 2 본 IK 로 목표에 두고 애니메이션 발 회전을 지킨다.
                for ( const Foot& foot : _listFoot )
                {
                    (void)RigIKSolver::solveTwoBone( pose, foot._root, foot._mid, foot._end, foot._goal, nullptr, *context._pSpace );
                    pose.setModelRotation( foot._end, foot._rotation );
                }
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override
            {
                inoutListBone.push_back( _pelvis );
                for ( const Foot& foot : _listFoot )
                {
                    inoutListBone.push_back( foot._root );
                    inoutListBone.push_back( foot._mid );
                    inoutListBone.push_back( foot._end );
                }
            }

            bool setControl( const hashed_string& control, float32 value ) override
            {
                if ( control == hashed_string( "stride_scale" ) )
                    _strideScale = value;
                else if ( control == hashed_string( "direction_x" ) )
                    _directionX = value;
                else if ( control == hashed_string( "direction_z" ) )
                    _directionZ = value;
                else
                    return false;
                return true;
            }

        private:
            RigStrideWarpingParams _params{}; ///< 파일 키 · 기본값 · 범위(RigNodeParams.h)
            struct Foot
            {
                hashed_string _rootName{};
                hashed_string _midName{};
                hashed_string _endName{};
                float3        _goal{};
                quaternion    _rotation{};
                float32       _legLength{ 0.0f };
                uint32        _root{ 0 };
                uint32        _mid{ 0 };
                uint32        _end{ 0 };
            };

            vector<Foot> _listFoot{};
            float32      _strideScale{ 1.0f }; ///< 게임이 넣는 조절 값이다. 넣기 전에는 1(바꾸지 않음)이다
            float32      _directionX{ 0.0f };
            float32      _directionZ{ 1.0f }; ///< 넣기 전에는 모델 앞(+Z)이다
            uint32       _pelvis{ 0 };
        };

        /**
         * @brief 전신 IK 입니다. 골반을 효과기(손 · 발 사슬의 목표)들이 끄는 쪽으로 옮긴 뒤 사슬마다 FABRIK 으로 끝을 목표에 붙입니다.
         * @details 키는 `pelvis`, `chains`(`<item target="" weight="" match_rotation=""><bones><item>루트</item> … 끝</bones></item>` — weight 는 골반을 끄는 몫, 기본 1, 루트는 골반의 자손),
         *          `pelvis_iterations`(기본 4), `max_pelvis_offset`(애니메이션 골반에서 옮길 수 있는 거리, 미터, 필수), `iterations` · `tolerance`(사슬 FABRIK)입니다.
         *          골반 풀이: 반복마다 사슬 루트에서 목표까지가 사슬 길이를 넘는 만큼(닿지 않는 거리)을 목표 쪽 벡터로 모아 가중 평균만큼 골반을 옮긴다.
         *          발 가중치를 크게 두면 발이 골반을 붙잡고, 손이 멀리 닿으려 하면 그만큼 몸이 기운다(언리얼 Full Body IK 의 골반 효과기 · 사슬 강성 자리).
         *          회전 제한 · 척추 굽힘은 하지 않는다(필요하면 사슬에 척추 본을 넣는다).
         */
        class RigFullBodyIKNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigFullBodyIKNode>( *this ); }
            const utf8*         getTypeName() const override { return "FullBodyIK"; }

            [[nodiscard]] bool parse( RigXMLReader& reader ) override
            {

                bool bOk                  = reader.readFields( _params ); // 골반 오프셋 >= 0 은 PROPERTY 가 본다
                _settings._iterationCount = _params._iterations;
                _settings._tolerance      = _params._tolerance;
                const RigXMLList chains   = reader.readArray( "chains", true );
                for ( size_t index = 0; bOk && chains.isValid() && index < chains.size(); ++index )
                {
                    RigXMLReader           chainReader( chains.at( index ), reader.getContext() );
                    RigFullBodyChainParams chainParams;
                    Chain                  chain{};
                    bOk                   = chainReader.readFields( chainParams );
                    bOk                   = chainReader.readNameList( "bones", chain._listBoneName, true ) && bOk;
                    bOk                   = chainReader.finish() && bOk;
                    chain._targetName     = chainParams._target;
                    chain._pelvisWeight   = chainParams._weight;
                    chain._bMatchRotation = chainParams._bMatchRotation ? SW_TRUE : SW_FALSE;
                    if ( bOk && chain._listBoneName.size() < 2 )
                    {
                        reader.fail( "each chain needs at least two 'bones'" );
                        return false;
                    }
                    _listChain.push_back( std::move( chain ) );
                }
                if ( bOk && _listChain.empty() )
                {
                    reader.fail( "'chains' must not be empty" );
                    return false;
                }
                return bOk && reader.isOk();
            }

            [[nodiscard]] bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _params._pelvis, _pelvis ) == false )
                    return false;
                for ( Chain& chain : _listChain )
                {
                    if ( context.findBones( chain._listBoneName, chain._listBone, true ) == false || context.findTarget( chain._targetName, chain._target ) == false )
                        return false;
                    // 골반을 옮겨 사슬 루트가 따라와야 한다 — 루트가 골반의 자손이 아니면 골반 풀이가 그 사슬에 닿지 않는다.
                    int32 ancestor = static_cast<int32>( chain._listBone[0] );
                    while ( ancestor >= 0 && ancestor != static_cast<int32>( _pelvis ) )
                    {
                        ancestor = context._pSkeleton->getBone( static_cast<uint32>( ancestor ) )._parentIndex;
                    }
                    if ( ancestor < 0 || chain._listBone[0] == _pelvis )
                    {
                        SW_LOG_ERROR_ID( "RigIKNodes.RigFullBodyIKChainMust", "Rig '%#': FullBodyIK chain '%#' must start below pelvis '%#'", context._label, chain._listBoneName[0].c_str(), _params._pelvis.c_str() );
                        return false;
                    }
                }
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer& pose = *context._pPose;
                // 사슬 길이는 지금 포즈의 마디 길이 합이다(애니메이션 스케일을 따른다).
                for ( Chain& chain : _listChain )
                {
                    chain._length = 0.0f;
                    for ( size_t index = 1; index < chain._listBone.size(); ++index )
                    {
                        chain._length += ( pose.getModelPosition( chain._listBone[index] ) - pose.getModelPosition( chain._listBone[index - 1] ) ).getLength();
                    }
                }
                const float3 animatedPelvis = pose.getModelPosition( _pelvis );
                for ( uint32 iteration = 0; iteration < _params._pelvisIterations; ++iteration )
                {
                    float3  pull{};
                    float32 weightSum = 0.0f;
                    for ( Chain& chain : _listChain )
                    {
                        quaternion unusedRotation{};
                        chain._bHasTarget = context._pInstance->resolveTarget( chain._target, pose, chain._targetPosition, unusedRotation ) ? SW_TRUE : SW_FALSE;
                        if ( chain._bHasTarget == SW_FALSE )
                            continue;
                        const float3  toTarget = context._pSpace->projectVector( chain._targetPosition - pose.getModelPosition( chain._listBone[0] ) );
                        const float32 distance = toTarget.getLength();
                        const float32 shortBy  = distance - chain._length;
                        if ( shortBy > 0.0f && distance > MathUtil::kEpsilon )
                            pull += toTarget * ( shortBy / distance * chain._pelvisWeight );
                        weightSum += chain._pelvisWeight;
                    }
                    if ( weightSum <= 0.0f )
                        break;
                    const float3 step = pull / weightSum;
                    if ( step.getLength() <= _settings._tolerance )
                        break;
                    float3        offset       = pose.getModelPosition( _pelvis ) + step - animatedPelvis;
                    const float32 offsetLength = offset.getLength();
                    if ( offsetLength > _params._maxPelvisOffset )
                        offset = offset * ( _params._maxPelvisOffset / offsetLength );
                    pose.setModelPosition( _pelvis, animatedPelvis + offset );
                }
                for ( Chain& chain : _listChain )
                {
                    quaternion targetRotation{};
                    if ( context._pInstance->resolveTarget( chain._target, pose, chain._targetPosition, targetRotation ) == false )
                        continue;
                    (void)RigIKSolver::solveFABRIK( pose, chain._listBone, chain._targetPosition, span<const RigJointLimit>{}, _settings, *context._pSpace );
                    if ( chain._bMatchRotation == SW_TRUE )
                        pose.setModelRotation( chain._listBone.back(), targetRotation );
                }
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override
            {
                inoutListBone.push_back( _pelvis );
                for ( const Chain& chain : _listChain )
                {
                    inoutListBone.insert( inoutListBone.end(), chain._listBone.begin(), chain._listBone.end() );
                }
            }

        private:
            RigFullBodyIKParams _params{}; ///< 파일 키 · 기본값 · 범위(RigNodeParams.h)
            struct Chain
            {
                vector<hashed_string> _listBoneName{};
                vector<uint32>        _listBone{};
                hashed_string         _targetName{};
                float3                _targetPosition{};
                float32               _pelvisWeight{ 1.0f };
                float32               _length{ 0.0f };
                uint32                _target{ 0 };
                uint8                 _bMatchRotation{ SW_FALSE };
                uint8                 _bHasTarget{ SW_FALSE };
            };

            vector<Chain>    _listChain{};
            RigChainSettings _settings{};
            uint32           _pelvis{ 0 };
        };

        struct RigIKNodesFactory
        {
            static unique_ptr<RigNode> createFullBody() { return make_unique<RigFullBodyIKNode>(); }
            static unique_ptr<RigNode> createTwoBone() { return make_unique<RigTwoBoneIKNode>(); }
            static unique_ptr<RigNode> createFABRIK() { return createChain( true ); }
            static unique_ptr<RigNode> createCCD() { return createChain( false ); }
            static unique_ptr<RigNode> createChain( bool bFABRIK )
            {
                unique_ptr<RigChainIKNode> node = make_unique<RigChainIKNode>();
                node->setFABRIK( bFABRIK );
                return node;
            }
            static unique_ptr<RigNode> createAim() { return make_unique<RigAimNode>(); }
            static unique_ptr<RigNode> createFootPlacement() { return make_unique<RigFootPlacementNode>(); }
            static unique_ptr<RigNode> createStrideWarping() { return make_unique<RigStrideWarpingNode>(); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void RigIKNodes::registerNodes( RigNodeRegistry& registry )
    {
        registry.registerNode( "TwoBoneIK", &RigIKNodesFactory::createTwoBone );
        registry.registerNode( "FABRIKChain", &RigIKNodesFactory::createFABRIK );
        registry.registerNode( "CCDChain", &RigIKNodesFactory::createCCD );
        registry.registerNode( "Aim", &RigIKNodesFactory::createAim );
        registry.registerNode( "FootPlacement", &RigIKNodesFactory::createFootPlacement );
        registry.registerNode( "StrideWarping", &RigIKNodesFactory::createStrideWarping );
        registry.registerNode( "FullBodyIK", &RigIKNodesFactory::createFullBody );
    }
} // namespace sw
