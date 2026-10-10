/**
 * @file PoseModifierComponent.h
 * @brief 후처리 리그 컴포넌트 — 리그 에셋(`*.rig.json`, IK · 제약 · 스프링 본 노드 목록)을 그래프 뒤 · 스키닝 앞(`AnimationPhase::PostProcess`)에 돌립니다.
 * @details 평가는 자기 onTick 이 아니라 `AnimationSystem` 이 합니다 — 같은 오브젝트의 `SkeletalMeshComponent`(유닛)에 단계 일로 붙고, 유닛들은
 *          레벨 안에서 병렬입니다. 대상이 다른 유닛의 본 · 소켓이면 그 유닛을 의존으로 걸어 먼저 평가되게 하고, 고리가 생기면 로드 오류로 그 대상을 끕니다.
 *          언리얼 Control Rig(후처리 노드) · IK Rig, 유니티 Animation Rigging(Rig Builder · 제약 컴포넌트)의 자리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObject;
    class PoseModifierComponent;
    class ResolvedSocketTable;
    class RigAsset;
    class SkeletalAnimatorComponent;
    class SkeletalMeshComponent;
    class SocketSet;

    /**
     * @class PoseModifierBinding
     * @brief 컴포넌트가 유닛 · 리그에 보이는 얼굴입니다(단계 일 · 커브 출처 · 소켓 풀이). 리플렉션 컴포넌트는 기반 클래스 하나만 두므로 이 객체가 구현합니다.
     */
    class SW_API PoseModifierBinding final : public IAnimationPhaseTask, public IRigCurveSource, public IRigSocketResolver
    {
    public:
        explicit PoseModifierBinding( PoseModifierComponent& owner );

        bool            isAnimationActive() const override;
        void            prepareAnimationFrame( SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void            runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void            onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;
        float32         getCurveValue( const hashed_string& curveName ) const override;
        RigSocketLookup findSocket( const hashed_string& socketName, hashed_string& outParentBone, BoneTransform& outLocal ) const override;

    private:
        PoseModifierComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /** @class PoseModifierComponent @brief 파일 머리말 참고. */
    REFLECT( Category = "Character", DisplayName = "Pose Modifier", Tooltip = "Post-process rig (IK, constraints, spring bones) evaluated after the animation graph and before skinning" )
    class SW_API PoseModifierComponent : public Component
    {
        friend class PoseModifierBinding;

    public:
        REFLECT_BODY();

        PoseModifierComponent();
        virtual ~PoseModifierComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 리그 에셋 경로를 바꾸고 다시 묶습니다. */
        void          setRigPath( string_view path );
        const string& getRigPath() const { return _rigPath; }
        /** @brief 리그를 런타임에 정합니다(저장되지 않습니다 — 시험 · 절차 생성). */
        void setRigAsset( shared_ptr<const RigAsset> asset );
        /** @brief 자기 유닛의 소켓 에셋 경로입니다(`*.sockets.xml`, 소켓 대상이 쓴다). */
        void setSocketSetPath( string_view path );
        /** @brief 자기 유닛의 소켓을 런타임에 정합니다. */
        void setOwnSockets( shared_ptr<const SocketSet> sockets );
        /** @brief 스프링 공유 충돌체를 읽을 물리 에셋 경로입니다(뼈의 구 · 캡슐). */
        void setPhysicsAssetPath( string_view path );

        /**
         * @brief 리그의 `unit` 이름에 유닛을 겁니다(장비 부품 · 다른 캐릭터). @p sockets 는 그 유닛의 소켓 에셋(소켓 대상이 쓴다).
         * @details 걸지 않은 이름은 자식 → 매니저 전체 순으로 같은 이름의 오브젝트를 찾습니다. 외형 통합은 이 함수로 슬롯 유닛을 겁니다.
         */
        void bindUnit( const hashed_string& unitName, SkeletalMeshComponent* pUnit, shared_ptr<const SocketSet> sockets = nullptr );
        /** @brief 리그의 `object` 이름에 오브젝트를 겁니다. 걸지 않은 이름은 같은 이름의 오브젝트를 찾습니다. */
        void bindObject( const hashed_string& objectName, GameObject* pObject );
        /**
         * @brief 해석된 외형의 소켓 표를 겁니다(빌림). 소켓 대상의 이름(`MainHand.Grip`)을 이 표에서 먼저 찾고, 그 소켓의 유닛을 @p listUnitByIndex
         *        (표의 유닛 번호 → 유닛)에서 고릅니다. 표면 기준 소켓의 체형 보정은 따르지 않습니다(부모 본 + 소켓 로컬).
         */
        void setSocketTable( const ResolvedSocketTable* pTable, const vector<SkeletalMeshComponent*>& listUnitByIndex );

        /** @brief 시퀀서 칸 가중치입니다(그 칸을 쓰는 노드에 곱합니다). */
        void setSlotWeight( const hashed_string& slotName, float32 weight ) { _instance.setSlotWeight( slotName, weight ); }
        /** @brief 시퀀서 칸을 놓습니다. */
        void clearSlotWeight( const hashed_string& slotName ) { _instance.clearSlotWeight( slotName ); }
        /** @brief 노드 조절 값(부모 바꾸기의 `parent` 등)입니다. 노드 · 조절이 없으면 false 입니다. */
        bool setNodeControl( const hashed_string& nodeName, const hashed_string& control, float32 value ) { return _instance.setNodeControl( nodeName, control, value ); }
        /** @brief 땅 질의를 바꿉니다(빌림, nullptr 이면 씬 물리). */
        void setGroundQuery( const IRigGroundQuery* pQuery ) { _pGroundQueryOverride = pQuery; }
        /** @brief 켜고 끕니다. 끄면 쉬는 유닛처럼 비용이 없습니다. */
        void setRigEnabled( bool bEnabled );
        bool isRigEnabled() const { return _bRigEnabled == SW_TRUE; }
        /** @brief 스프링 · 부모 바꾸기 상태를 버립니다(순간이동 뒤). */
        void resetRig() { _instance.reset(); }

        /** @brief 리그가 묶여 돌 준비가 됐는지입니다. */
        bool isRigReady() const { return _instance.isInitialized(); }
        /** @brief 이름의 대상이 값을 받고 있는지입니다(묶임 · 고리 진단). */
        bool isTargetBound( const hashed_string& targetName ) const;
        /** @brief 포즈 구동이 낸 보정 모프 가중치입니다(모든 단계 뒤 게임 스레드에서 읽습니다). GPU 모프 풀은 아직 이름 모프를 받지 않습니다. */
        const vector<RigMorphWeight>& getMorphWeights() const { return _instance.getMorphWeights(); }
        /** @brief 실행 상태(시험 · 진단)입니다. */
        RigInstance&       getRigInstance() { return _instance; }
        const RigInstance& getRigInstance() const { return _instance; }

    private:
        /** @brief 바깥 대상 하나의 묶음입니다. */
        struct ExternalTarget
        {
            float4x4                     _unitToModel{};         ///< 대상 유닛 모델 → 내 유닛 모델(프레임 시작에 찍음)
            BoneTransform                _socketLocal{};         ///< 대상 본 기준 오프셋(소켓 로컬 × 데이터 오프셋)
            const SkeletalMeshComponent* _pFrameUnit{ nullptr }; ///< 이번 프레임에 푼 대상 유닛(프레임 시작 ~ 평가 동안만 빌림)
            ComponentHandle              _unit{};                ///< 다른 유닛(본 · 소켓 대상)
            GameObjectHandle             _object{};              ///< 오브젝트 대상
            int32                        _unitBone{ -1 };        ///< 대상 유닛의 본
            int32                        _spaceBone{ -1 };       ///< `space` — 내 유닛의 본
            uint8                        _bBound{ SW_FALSE };
            uint8                        _bLive{ SW_FALSE }; ///< 평가 중(워커)에 대상 유닛의 이번 프레임 포즈를 읽는다(의존을 건 대상)
        };

        struct UnitBinding
        {
            hashed_string               _name{};
            shared_ptr<const SocketSet> _sockets{};
            ComponentHandle             _unit{};
        };

        struct ObjectBinding
        {
            hashed_string    _name{};
            GameObjectHandle _object{};
        };

        /** @brief 유닛 · 에셋 · 대상을 다시 묶습니다(게임 스레드). */
        void bindRig();
        /** @brief 바깥 대상을 오브젝트 · 유닛에 잇고 의존을 겁니다. */
        void bindExternalTargets();
        /** @brief 묶을 때 건 의존을 뗍니다. */
        void releaseDependencies();
        /** @brief 이름의 유닛(명시 → 자식 → 매니저)입니다. */
        SkeletalMeshComponent* findUnitByName( const hashed_string& unitName, shared_ptr<const SocketSet>* pOutSockets ) const;
        /** @brief 이름의 오브젝트(명시 → 매니저)입니다. */
        GameObject* findObjectByName( const hashed_string& objectName ) const;
        /** @brief @p pUpstream 을 의존으로 걸면 고리가 생기는지입니다. */
        bool wouldCreateCycle( const SkeletalMeshComponent* pUpstream ) const;
        /** @brief 물리 에셋의 구 · 캡슐을 스프링 공유 충돌체로 옮깁니다. */
        void loadSharedColliders();
        /** @brief 게임 스레드 준비입니다. */
        void prepareFrame( SkeletalMeshComponent& unit, const AnimationFrameContext& context );
        /** @brief 워커 평가입니다(PostProcess). */
        void evaluateFrame( SkeletalMeshComponent& unit );

        PROPERTY( Category = "Rig", DisplayName = "Rig", AssetPath, AssetType = "Rig", Tooltip = "Post-process rig asset (*.rig.json): ordered IK / constraint / spring nodes" )
        string _rigPath;
        PROPERTY( Category = "Rig", DisplayName = "Sockets", AssetPath, AssetType = "SocketSet", Tooltip = "This unit's socket asset (*.sockets.xml) for socket targets" )
        string _socketSetPath;
        PROPERTY( Category = "Rig", DisplayName = "Physics Asset", AssetPath, AssetType = "PhysicsAsset", Tooltip = "Sphere / capsule bodies used as spring bone colliders" )
        string _physicsAssetPath;

        PoseModifierBinding              _binding;
        RigInstance                      _instance;
        shared_ptr<const RigAsset>       _asset;
        shared_ptr<const SocketSet>      _ownSockets;
        vector<ExternalTarget>           _listExternal;
        vector<UnitBinding>              _listUnitBinding;
        vector<ObjectBinding>            _listObjectBinding;
        vector<ComponentHandle>          _listDependency; ///< 이 컴포넌트가 건 의존(뗄 때 쓴다)
        vector<ComponentHandle>          _listTableUnit;  ///< 소켓 표의 유닛 번호 → 유닛
        unique_ptr<IRigGroundQuery>      _groundQuery;    ///< 씬 물리를 감싼 땅 질의
        float4x4                         _worldFromModel;
        const ResolvedSocketTable*       _pSocketTable;
        const IRigGroundQuery*           _pGroundQueryOverride;
        const SkeletalAnimatorComponent* _pFrameAnimator; ///< 이번 프레임의 같은 유닛 애니메이터(커브 출처, 프레임 시작에 찾음)
        SkeletalMeshComponent*           _pUnit;
        uint64                           _boundContentID;
        PROPERTY( Category = "Rig", DisplayName = "Enabled", Tooltip = "Evaluate the rig; disabled rigs cost nothing" )
        uint8                  _bRigEnabled : 1;
        [[maybe_unused]] uint8 _reserved    : 7;
    };
} // namespace sw
