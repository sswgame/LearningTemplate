#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigIkSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigNodeLibrary.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        /** @brief 이 파일의 노드가 함께 쓰는 변환 도우미입니다. */
        struct RigConstraintNodesInternal
        {
            /** @brief 회전 · 이동 한 벌(본 기준 상대 변환 · 출력 기억)입니다. */
            struct RigidTransform
            {
                float3     _position{};
                quaternion _rotation{};
            };

            /** @brief @p child 를 @p parent 기준으로 봅니다(스케일 없이). */
            static RigidTransform makeRelative( const RigidTransform& parent, const RigidTransform& child )
            {
                const quaternion inverse = parent._rotation.inverse();
                return RigidTransform{ float3::transform( child._position - parent._position, inverse ), ( inverse * child._rotation ).normalize() };
            }

            /** @brief 상대 변환을 @p parent 위에 얹습니다. */
            static RigidTransform compose( const RigidTransform& parent, const RigidTransform& relative )
            {
                return RigidTransform{ parent._position + float3::transform( relative._position, parent._rotation ), ( parent._rotation * relative._rotation ).normalize() };
            }

            static RigidTransform readBone( RigPoseBuffer& pose, uint32 bone ) { return RigidTransform{ pose.getModelPosition( bone ), pose.getModelRotation( bone ) }; }
        };

        /**
         * @brief 변환 복사 · 위치 · 회전 제약. 키: `bone` · `target` · `maintain_offset`(처음 평가 때의 상대 자리를 지킴),
         *        `CopyTransform` 만 `position` · `rotation`(기본 둘 다 true).
         */
        class RigCopyTransformNode final : public RigNode
        {
        public:
            enum class Mode : uint8
            {
                CopyTransform = 0,
                Position,
                Rotation,
            };

            /** @brief 종류를 정합니다(만들 때 한 번) — 위치 제약은 이동만, 회전 제약은 회전만 씁니다. */
            void setMode( Mode mode )
            {
                _mode      = mode;
                _bPosition = ( mode != Mode::Rotation ) ? SW_TRUE : SW_FALSE;
                _bRotation = ( mode != Mode::Position ) ? SW_TRUE : SW_FALSE;
            }

            unique_ptr<RigNode> clone() const override
            {
                unique_ptr<RigCopyTransformNode> node = make_unique<RigCopyTransformNode>( *this );
                node->_bCaptured                      = SW_FALSE;
                return node;
            }

            const utf8* getTypeName() const override
            {
                switch ( _mode )
                {
                    case Mode::CopyTransform:
                        return "CopyTransform";
                    case Mode::Position:
                        return "Position";
                    case Mode::Rotation:
                        return "Rotation";
                }
                return "CopyTransform";
            }

            [[nodiscard]] bool parse( RigJsonReader& reader ) override
            {
                bool bMaintain = false;
                bool bOk       = reader.readName( "bone", _boneName, true ) && reader.readName( "target", _targetName, true ) &&
                           reader.readBool( "maintain_offset", bMaintain, false );
                _bMaintainOffset = bMaintain ? SW_TRUE : SW_FALSE;
                if ( _mode == Mode::CopyTransform )
                {
                    bool bPosition = true;
                    bool bRotation = true;
                    bOk            = bOk && reader.readBool( "position", bPosition, false ) && reader.readBool( "rotation", bRotation, false );
                    _bPosition     = bPosition ? SW_TRUE : SW_FALSE;
                    _bRotation     = bRotation ? SW_TRUE : SW_FALSE;
                }
                return bOk;
            }

            bool bind( const RigBindContext& context ) override { return context.findBone( _boneName, _bone ) && context.findTarget( _targetName, _target ); }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer&                             pose = *context._pPose;
                RigConstraintNodesInternal::RigidTransform target{};
                if ( context._pInstance->resolveTarget( _target, pose, target._position, target._rotation ) == false )
                    return;
                if ( _bMaintainOffset == SW_TRUE && _bCaptured == SW_FALSE )
                {
                    _offset    = RigConstraintNodesInternal::makeRelative( target, RigConstraintNodesInternal::readBone( pose, _bone ) );
                    _bCaptured = SW_TRUE;
                }
                const RigConstraintNodesInternal::RigidTransform desired =
                    _bMaintainOffset == SW_TRUE ? RigConstraintNodesInternal::compose( target, _offset ) : target;
                if ( _bRotation == SW_TRUE )
                    pose.setModelRotation( _bone, desired._rotation );
                if ( _bPosition == SW_TRUE )
                    pose.setModelPosition( _bone, desired._position );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override { inoutListBone.push_back( _bone ); }
            void reset() override { _bCaptured = SW_FALSE; }

        private:
            RigConstraintNodesInternal::RigidTransform _offset{};
            hashed_string                              _boneName{};
            hashed_string                              _targetName{};
            uint32                                     _bone{ 0 };
            uint32                                     _target{ 0 };
            Mode                                       _mode{ Mode::CopyTransform };
            uint8                                      _bPosition{ SW_TRUE };
            uint8                                      _bRotation{ SW_TRUE };
            uint8                                      _bMaintainOffset{ SW_FALSE };
            uint8                                      _bCaptured{ SW_FALSE };
        };

        /**
         * @brief 부모 바꾸기(공간 전환). 키: `bone` · `parents`(대상 이름들) · `initial`(처음 부모 번호) · `settle_seconds`(바꾼 뒤 새 부모 자리로
         *        옮겨 가는 시간, 0 이면 오프셋을 끝까지 지킨다). 조절 `parent` = 부모 번호.
         * @details 바꾸는 순간 지난 프레임 출력 자리를 새 부모 기준으로 다시 재서 그대로 시작합니다(튀지 않음). `settle_seconds` 동안 그 오프셋을
         *          단위로 부드럽게 줄여 본이 새 부모(소켓) 자리에 앉습니다 — 무기를 손에서 등으로. 처음 평가는 첫 부모 기준으로 지금 자리를 잽니다.
         */
        class RigParentSwitchNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override
            {
                unique_ptr<RigParentSwitchNode> node = make_unique<RigParentSwitchNode>( *this );
                node->reset();
                return node;
            }

            const utf8* getTypeName() const override { return "ParentSwitch"; }

            [[nodiscard]] bool parse( RigJsonReader& reader ) override
            {
                const bool bOk = reader.readName( "bone", _boneName, true ) && reader.readNameList( "parents", _listParentName, true ) &&
                                 reader.readUint( "initial", _initialParent, false ) && reader.readFloat( "settle_seconds", _settleSeconds, false );
                if ( bOk && _initialParent >= _listParentName.size() )
                {
                    reader.fail( "'initial' is out of range of 'parents'" );
                    return false;
                }
                _activeParent = _initialParent;
                return bOk;
            }

            bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _boneName, _bone ) == false )
                    return false;
                _listParent.clear();
                for ( const hashed_string& name : _listParentName )
                {
                    uint32 target = 0;
                    if ( context.findTarget( name, target ) == false )
                        return false;
                    _listParent.push_back( target );
                }
                return true;
            }

            bool setControl( const hashed_string& control, float32 value ) override
            {
                if ( control != hashed_string( "parent" ) || value < 0.0f || static_cast<uint32>( value ) >= _listParent.size() )
                    return false;
                const uint32 parent = static_cast<uint32>( value );
                if ( parent != _activeParent )
                {
                    _activeParent   = parent;
                    _bSwitchPending = SW_TRUE;
                }
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer&                             pose = *context._pPose;
                RigConstraintNodesInternal::RigidTransform parent{};
                if ( context._pInstance->resolveTarget( _listParent[_activeParent], pose, parent._position, parent._rotation ) == false )
                    return;
                if ( _bCaptured == SW_FALSE || _bSwitchPending == SW_TRUE )
                {
                    // 처음이면 지금(애니메이션) 자리, 바꿈이면 지난 출력 자리를 새 부모 기준으로 잰다.
                    const RigConstraintNodesInternal::RigidTransform from = _bCaptured == SW_TRUE ? _lastOutput : RigConstraintNodesInternal::readBone( pose, _bone );
                    _offset                                               = RigConstraintNodesInternal::makeRelative( parent, from );
                    _settleElapsed                                        = 0.0f;
                    _bCaptured                                            = SW_TRUE;
                    _bSwitchPending                                       = SW_FALSE;
                }
                RigConstraintNodesInternal::RigidTransform relative = _offset;
                if ( _settleSeconds > 0.0f )
                {
                    _settleElapsed      = MathUtil::min( _settleElapsed + context._deltaSeconds, _settleSeconds );
                    const float32 ratio = _settleElapsed / _settleSeconds;
                    const float32 eased = ratio * ratio * ( 3.0f - 2.0f * ratio );
                    relative._position  = float3::lerp( _offset._position, float3::Zero, eased );
                    relative._rotation  = quaternion::slerp( _offset._rotation, quaternion::Identity, eased );
                }
                _lastOutput = RigConstraintNodesInternal::compose( parent, relative );
                pose.setModelRotation( _bone, _lastOutput._rotation );
                pose.setModelPosition( _bone, _lastOutput._position );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override { inoutListBone.push_back( _bone ); }

            void reset() override
            {
                _bCaptured      = SW_FALSE;
                _bSwitchPending = SW_FALSE;
                _settleElapsed  = 0.0f;
            }

        private:
            vector<hashed_string>                      _listParentName{};
            vector<uint32>                             _listParent{};
            RigConstraintNodesInternal::RigidTransform _offset{};
            RigConstraintNodesInternal::RigidTransform _lastOutput{};
            hashed_string                              _boneName{};
            float32                                    _settleSeconds{ 0.0f };
            float32                                    _settleElapsed{ 0.0f };
            uint32                                     _bone{ 0 };
            uint32                                     _initialParent{ 0 };
            uint32                                     _activeParent{ 0 };
            uint8                                      _bCaptured{ SW_FALSE };
            uint8                                      _bSwitchPending{ SW_FALSE };
        };

        /** @brief 거리 제한. 키: `bone` · `target` · `min` · `max` — 본을 대상에서 [min, max] 거리 안으로 옮깁니다. */
        class RigDistanceNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigDistanceNode>( *this ); }
            const utf8*         getTypeName() const override { return "Distance"; }

            [[nodiscard]] bool parse( RigJsonReader& reader ) override
            {
                const bool bOk = reader.readName( "bone", _boneName, true ) && reader.readName( "target", _targetName, true ) &&
                                 reader.readFloat( "min", _min, false ) && reader.readFloat( "max", _max, true );
                if ( bOk && ( _min < 0.0f || _max < _min ) )
                {
                    reader.fail( "'min' and 'max' must satisfy 0 <= min <= max" );
                    return false;
                }
                return bOk;
            }

            bool bind( const RigBindContext& context ) override { return context.findBone( _boneName, _bone ) && context.findTarget( _targetName, _target ); }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer& pose = *context._pPose;
                float3         targetPosition{};
                quaternion     targetRotation{};
                if ( context._pInstance->resolveTarget( _target, pose, targetPosition, targetRotation ) == false )
                    return;
                const float3  offset   = pose.getModelPosition( _bone ) - targetPosition;
                const float32 distance = offset.getLength();
                if ( distance < MathUtil::Epsilon )
                    return;
                const float32 clamped = MathUtil::clamp( distance, _min, _max );
                if ( clamped != distance )
                    pose.setModelPosition( _bone, targetPosition + offset * ( clamped / distance ) );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override { inoutListBone.push_back( _bone ); }

        private:
            hashed_string _boneName{};
            hashed_string _targetName{};
            float32       _min{ 0.0f };
            float32       _max{ 1.0f };
            uint32        _bone{ 0 };
            uint32        _target{ 0 };
        };

        /** @brief 회전 범위 제한. 키: `bone` · `min_degrees` · `max_degrees`([피치, 요, 롤], 레퍼런스 로컬 회전 기준). */
        class RigLimitRotationNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigLimitRotationNode>( *this ); }
            const utf8*         getTypeName() const override { return "LimitRotation"; }

            [[nodiscard]] bool parse( RigJsonReader& reader ) override
            {
                return reader.readName( "bone", _boneName, true ) && reader.readFloat3( "min_degrees", _minAngle, true ) &&
                       reader.readFloat3( "max_degrees", _maxAngle, true );
            }

            bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _boneName, _bone ) == false )
                    return false;
                _reference = context._pSkeleton->getBone( _bone )._referencePose._rotation;
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer&   pose  = *context._pPose;
                const quaternion delta = ( RigIkSolver::makeInverse( _reference ) * pose.getLocalRotation( _bone ) ).normalize();
                const float3     euler = delta.getEulerAngles() * MathUtil::RadianToDegree;
                const float3     clamped{ MathUtil::clamp( euler._x, _minAngle._x, _maxAngle._x ), MathUtil::clamp( euler._y, _minAngle._y, _maxAngle._y ),
                                      MathUtil::clamp( euler._z, _minAngle._z, _maxAngle._z ) };
                if ( clamped == euler )
                    return;
                pose.setLocalRotation( _bone, ( _reference * quaternion::createFromYawPitchRoll( clamped * MathUtil::DegreeToRadian ) ).normalize() );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override { inoutListBone.push_back( _bone ); }

        private:
            quaternion    _reference{};
            float3        _minAngle{};
            float3        _maxAngle{};
            hashed_string _boneName{};
            uint32        _bone{ 0 };
        };

        /**
         * @brief 트위스트 분배(팔뚝 비틀림). 키: `source`(손) · `axis`(본 로컬 비틀림 축, 기본 +Y) · `bones`(`[{ "bone", "weight" }]`).
         * @details 소스 본의 레퍼런스 기준 비틀림 각 θ 를 나눠 줍니다 — 비틀림 본 b 는 로컬에 θ × weight 를 더 돌리고, 그 본이 소스의 조상이면
         *          소스에서 그만큼 뺍니다(손이 두 번 돌지 않게). 손목을 돌릴 때 팔뚝이 사탕 포장지처럼 꼬이지 않습니다.
         */
        class RigTwistDistributionNode final : public RigNode
        {
        public:
            unique_ptr<RigNode> clone() const override { return make_unique<RigTwistDistributionNode>( *this ); }
            const utf8*         getTypeName() const override { return "TwistDistribution"; }

            [[nodiscard]] bool parse( RigJsonReader& reader ) override
            {
                bool bOk              = reader.readName( "source", _sourceName, true ) && reader.readFloat3( "axis", _axis, false );
                _axis                 = _axis.normalize();
                const JsonValue bones = reader.readArray( "bones", true );
                for ( size_t index = 0; bOk && bones.isValid() && index < bones.size(); ++index )
                {
                    RigJsonReader entryReader( bones.at( index ), reader.getContext() );
                    TwistBone     entry{};
                    bOk = entryReader.readName( "bone", entry._name, true ) && entryReader.readFloat( "weight", entry._weight, true );
                    bOk = entryReader.finish() && bOk;
                    _listTwist.push_back( entry );
                }
                if ( bOk && ( _listTwist.empty() || _axis.getLengthSquared() < 0.5f ) )
                {
                    reader.fail( "'bones' must not be empty and 'axis' must not be zero" );
                    return false;
                }
                return bOk && reader.isOk();
            }

            bool bind( const RigBindContext& context ) override
            {
                if ( context.findBone( _sourceName, _source ) == false )
                    return false;
                _sourceReference = context._pSkeleton->getBone( _source )._referencePose._rotation;
                _ancestorWeight  = 0.0f;
                for ( TwistBone& entry : _listTwist )
                {
                    if ( context.findBone( entry._name, entry._bone ) == false )
                        return false;
                    int32 current = context._pSkeleton->getBone( _source )._parentIndex;
                    while ( current >= 0 && static_cast<uint32>( current ) != entry._bone )
                        current = context._pSkeleton->getBone( static_cast<uint32>( current ) )._parentIndex;
                    entry._bAncestor = current >= 0 ? SW_TRUE : SW_FALSE;
                    if ( entry._bAncestor == SW_TRUE )
                        _ancestorWeight += entry._weight;
                }
                return true;
            }

            void evaluate( RigEvaluateContext& context ) override
            {
                RigPoseBuffer&   pose  = *context._pPose;
                const quaternion delta = ( RigIkSolver::makeInverse( _sourceReference ) * pose.getLocalRotation( _source ) ).normalize();
                quaternion       swing{};
                quaternion       twist{};
                RigIkSolver::decomposeSwingTwist( delta, _axis, swing, twist );
                const float32 angle = RigIkSolver::computeTwistAngle( twist, _axis );
                for ( const TwistBone& entry : _listTwist )
                {
                    const quaternion share = quaternion::createFromAxisAngle( _axis, angle * entry._weight );
                    pose.setLocalRotation( entry._bone, ( pose.getLocalRotation( entry._bone ) * share ).normalize() );
                }
                // 조상이 더 돈 만큼 소스의 로컬 앞(부모 쪽)에서 되돌린다 — 손의 모델 방향이 그대로 남는다.
                const quaternion ancestorShare = quaternion::createFromAxisAngle( _axis, -angle * _ancestorWeight );
                pose.setLocalRotation( _source, ( ancestorShare * pose.getLocalRotation( _source ) ).normalize() );
            }

            void collectWrittenBones( vector<uint32>& inoutListBone ) const override
            {
                for ( const TwistBone& entry : _listTwist )
                    inoutListBone.push_back( entry._bone );
                inoutListBone.push_back( _source );
            }

        private:
            struct TwistBone
            {
                hashed_string _name{};
                float32       _weight{ 0.5f };
                uint32        _bone{ 0 };
                uint8         _bAncestor{ SW_FALSE };
            };

            vector<TwistBone> _listTwist{};
            quaternion        _sourceReference{};
            float3            _axis{ 0.0f, 1.0f, 0.0f };
            hashed_string     _sourceName{};
            float32           _ancestorWeight{ 0.0f };
            uint32            _source{ 0 };
        };

        struct RigConstraintNodesFactory
        {
            static unique_ptr<RigNode> createCopyTransform() { return createCopy( RigCopyTransformNode::Mode::CopyTransform ); }
            static unique_ptr<RigNode> createPosition() { return createCopy( RigCopyTransformNode::Mode::Position ); }
            static unique_ptr<RigNode> createRotation() { return createCopy( RigCopyTransformNode::Mode::Rotation ); }
            static unique_ptr<RigNode> createCopy( RigCopyTransformNode::Mode mode )
            {
                unique_ptr<RigCopyTransformNode> node = make_unique<RigCopyTransformNode>();
                node->setMode( mode );
                return node;
            }
            static unique_ptr<RigNode> createParentSwitch() { return make_unique<RigParentSwitchNode>(); }
            static unique_ptr<RigNode> createDistance() { return make_unique<RigDistanceNode>(); }
            static unique_ptr<RigNode> createLimitRotation() { return make_unique<RigLimitRotationNode>(); }
            static unique_ptr<RigNode> createTwistDistribution() { return make_unique<RigTwistDistributionNode>(); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void RigNodeLibrary::registerConstraintNodes( RigNodeRegistry& registry )
    {
        registry.registerNode( "CopyTransform", &RigConstraintNodesFactory::createCopyTransform );
        registry.registerNode( "Position", &RigConstraintNodesFactory::createPosition );
        registry.registerNode( "Rotation", &RigConstraintNodesFactory::createRotation );
        registry.registerNode( "ParentSwitch", &RigConstraintNodesFactory::createParentSwitch );
        registry.registerNode( "Distance", &RigConstraintNodesFactory::createDistance );
        registry.registerNode( "LimitRotation", &RigConstraintNodesFactory::createLimitRotation );
        registry.registerNode( "TwistDistribution", &RigConstraintNodesFactory::createTwistDistribution );
    }
} // namespace sw
