/**
 * @file FractureComponentBase.h
 * @brief 쪼갠 조각을 런타임에 다루는 컴포넌트의 공통 기반입니다. 3D `FractureComponent` 와 2D `Fracture2DComponent` 가 상속하며, 구조 상태(`DestructionState`)를 보관하고 피해 사건을 받아 바디와 렌더링을 바꿉니다.
 * @details **온전할 때는 오브젝트의 메시 하나가 그리기 하나다.** 이 컴포넌트는 그리지도 않고 바디도 갖지 않는다. 충돌은 오브젝트의 `RigidBodyComponent` 가 맡는다.
 *          처음 떨어진 조각이 생기면(그룹이 갈라지면) **쪼갠 상태로 바뀐다.** 오브젝트의 메시를 숨기고 강체 컴포넌트를 끈 뒤,
 *          조각마다 본 하나를 둔 스킨드 메시 둘(겉면과 안쪽 면, 슬롯마다 그리기 하나, `FractureRenderUtil`)을 같은 오브젝트에 붙인다.
 *          앵커에 붙은 조각은 조각마다 정적 바디를 한 번에 만든다. 미리 만들어 둔 껍질 셰이프를 나눠 쓴다.
 *          떨어진 그룹은 그룹마다 동적 바디 하나(조각 껍질의 컴파운드)를 만든다.
 *          그룹이 갈라지면 이전 바디를 지우고, 갈라진 덩어리마다 자세와 속도(질량 중심의 선속도 + 각속도 × 거리)를 이어받는 새 바디를 만든다.
 *
 *          **예산과 정리(Chaos 의 Remove on Sleep).** 떨어진 그룹이 잠든 채 `sleepRemoveTime` 이 지나면 바디를 제거한다.
 *          `keepCollisionVolume` 이상인 큰 덩어리는 잠든 바디를 남겨 충돌을 유지한다.
 *          작은 파편(`smallDebrisVolume` 미만)은 `lifetime` 이 지난 뒤 `fadeTime` 동안 줄어들며 사라진다.
 *          떨어진 그룹의 바디가 `maxBodies`(그리고 전역 `gv_destructionMaxDebrisBodies`)를 넘으면 가장 오래된 작은 파편부터 사라지게 한다.
 *          움직이는 바디도 없고 사라지는 중인 조각도 없으면, 현재 자세를 정적 메시 둘에 베이크하고 스킨드 메시를 숨긴다(프레임 비용 0).
 *          다시 피해를 받으면 스킨드 메시로 돌아간다.
 *
 *          **피해.** `applyDamage`(메시 공간), `applyPointDamageAtWorld`, `applyRadialDamageAtWorld`, `applyRaycastDamage` 는 어느 틱에서든 불러도 된다.
 *          요청은 잠금 아래 쌓이고 다음 물리 프레임에 게임 스레드에서 적용된다.
 *          충돌(`onCollisionBegin` 의 시작 충격량)은 권한이 있을 때(`_bAuthority`)만 사건이 된다.
 *          적용한 사건은 `getEventLog()` 에 쌓인다. 다른 기계가 이 사건을 같은 순서로 `applyDamage` 에 넘기면 같은 상태가 된다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Destruction/FractureNetworkSync.h"
#include "Engine/Destruction/FractureRenderUnits.h"
#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct DestructionRandom;
    struct FractureAsset;

    class GameObjectManager;
    class IFracturePhysics;
    class MeshComponent;
    class ValidationContext;

    /** @brief 어느 조각을 땅에 붙은(앵커) 것으로 볼지 정합니다. 앵커 볼륨(`_listAnchorVolume`)은 항상 추가로 적용합니다. */
    ENUM()
    enum class FractureAnchorMode : uint8
    {
        None = 0, ///< 앵커가 없다. 통째로 동적이다(상자, 통).
        Bottom,   ///< 가장 낮은 면에서 `_anchorTolerance` 안에 닿는 조각이다(벽, 기둥).
        World,    ///< 시작할 때 다른 오브젝트의 정적 바디에 닿은 조각이다.
    };
} // namespace sw

namespace sw
{
    /** @brief 앵커 볼륨 하나(오브젝트 로컬 상자)입니다. 무게 중심이 안에 있는 조각은 앵커가 됩니다. */
    REFLECT()
    struct SW_API FractureAnchorVolume
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Box centre in object space", Units = m )
        float3 _center{};
        PROPERTY( Tooltip = "Box half size", Units = m )
        float3 _halfExtents{ 0.5f, 0.5f, 0.5f };
    };
} // namespace sw

namespace sw
{
    /** @brief 런타임 그룹 하나의 바디, 자세, 수명입니다. */
    struct FractureGroupRuntime
    {
        float3             _position{};      ///< 그룹 자세다(오브젝트 원점이 있는 월드 위치).
        quaternion         _rotation{};      ///< 그룹 자세의 회전이다.
        float3             _drivePosition{}; ///< 네트워크가 정한 목표 자세다(`_bDriven`).
        quaternion         _driveRotation{};
        PhysicsBodyHandle  _body{};  ///< 떨어진 그룹은 동적 바디 하나를 가진다. 앵커 그룹은 잎마다 정적 바디가 있고, 컴포넌트가 잎 테이블로 관리한다.
        PhysicsShapeHandle _shape{}; ///< 떨어진 그룹의 컴파운드 셰이프다(바디와 함께 놓는다).
        uint32             _groupID{ 0 };
        uint32             _spawnOrder{ 0 }; ///< 만든 순서다(예산이 모자라면 오래된 것부터 고른다).
        float32            _volume{ 0.0f };  ///< 배율을 곱한 부피다(세제곱미터).
        float32            _age{ 0.0f };
        float32            _sleepTime{ 0.0f };
        float32            _fade{ 1.0f }; ///< 1 = 온전, 0 = 사라짐(본 배율)
        uint8              _bAnchored{ SW_FALSE };
        uint8              _bFrozen{ SW_FALSE }; ///< 바디를 제거했다(자세 고정).
        uint8              _bFading{ SW_FALSE };
        uint8              _bGone{ SW_FALSE };   ///< 사라졌다(그리지 않는다).
        uint8              _bDriven{ SW_FALSE }; ///< 네트워크가 자세를 정한다. 바디는 키네마틱이고 스텝마다 목표로 옮긴다.
        uint8              _layer{ 0 };          ///< 떨어진 그룹 바디의 물리 레이어다(덩어리, 파편).
    };
} // namespace sw

namespace sw
{
    /** @brief 아직 적용하지 않은 피해 하나입니다. 월드 요청은 물리 프레임에서 그 시점의 자세로 메시 공간 사건이 됩니다. */
    struct FracturePendingDamage
    {
        enum class Kind : uint8
        {
            Mesh = 0,    ///< 메시 공간 사건 그대로다(네트워크로 받은 것).
            WorldPoint,  ///< 월드의 맞은 지점이다.
            WorldRadial, ///< 월드 폭발이다.
            Snapshot,    ///< 상태 스냅샷이다(`applyNetworkSnapshot`). 앞뒤 사건과 받은 순서대로 적용한다.
        };

        vector<uint8>          _snapshotBytes{}; ///< Snapshot 의 바이트다.
        FractureGroupPose      _groupPose{};     ///< Mesh 사건이 맞힌 떨어진 그룹의, 보낸 쪽 자세다(`_bHasGroupPose`). 적용 전에 그 그룹을 이 위치로 옮긴다.
        DestructionDamageEvent _event{};         ///< Mesh 면 그대로 쓰고, 아니면 변형, 반경, 충격량만 쓴다.
        float3                 _worldPoint{};    ///< 월드 중심이다.
        float3                 _worldDirection{};
        PhysicsBodyHandle      _hitBody{};
        Kind                   _kind{ Kind::Mesh };
        uint8                  _bHasGroupPose{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    REFLECT( Abstract, Category = "Physics", DisplayName = "Fracture Base", Tooltip = "Base of the 3D and 2D destructible components" )
    class SW_API FractureComponentBase : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        FractureComponentBase();
        ~FractureComponentBase() override;

        void onBeginPlay() override;
        void onCollisionBegin( const CollisionInfo& collision ) override;

        /** @brief 메시 공간 피해 사건 하나를 쌓습니다. 다음 물리 프레임에 적용하며, 어느 틱에서든 부를 수 있습니다. */
        void applyDamage( const DestructionDamageEvent& event );
        /**
         * @brief 받은 사건과, 그 사건이 맞힌 떨어진 그룹의 보낸 쪽 자세를 함께 쌓습니다(네트워크). 적용하기 전에 그 그룹을 그 자세로 옮긴다.
         *        그러면 갈라진 조각은 받는 쪽의 늦은 자세가 아니라, 보낸 쪽이 갈랐던 위치에서 생성된다.
         */
        void applyDamage( const DestructionDamageEvent& event, const FractureGroupPose& groupPose );
        /** @brief 월드의 맞은 지점에 피해를 줍니다(무기). @p hitBody 가 이 오브젝트의 떨어진 덩어리면 그 덩어리의 잎만 맞습니다. */
        void applyPointDamageAtWorld( const float3& worldPoint, const float3& worldDirection, float32 strain, float32 impulse, PhysicsBodyHandle hitBody );
        /** @brief 월드의 폭발 피해를 줍니다. 붙은 구조와 반경 안의 떨어진 덩어리마다 사건을 하나씩 만듭니다. */
        void applyRadialDamageAtWorld( const float3& worldCenter, float32 radius, float32 strain, float32 impulse );
        /**
         * @brief 광선이 처음 닿은 파괴 오브젝트의 맞은 지점에 피해를 줍니다(3D). 닿은 것이 파괴 오브젝트가 아니면 false 를 반환합니다.
         * @param pOutPoint 닿은 지점(월드)을 받습니다(선택).
         */
        [[nodiscard]] static bool applyRaycastDamage( GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 strain, float32 impulse,
                                                      float3* pOutPoint = nullptr );

        /**
         * @brief 월드 구(@p worldCenter, @p radius)가 이 오브젝트의 조각 경계 구에 닿는지 반환합니다. 폭발이 벽 같은 큰 오브젝트의 가장자리만 스쳐도 맞게 하려는 것입니다.
         *        파쇄 데이터를 아직 읽지 않았으면 오브젝트 원점까지의 거리로 판정합니다.
         */
        bool isReachedBy( const float3& worldCenter, float32 radius ) const;
        /** @brief 쪼갠 상태로 바뀌었는지 반환합니다. 처음 떨어진 조각이 생긴 뒤에 true 입니다. */
        bool isFractured() const { return _bFractured == SW_TRUE; }
        /** @brief 잎이 앵커까지 이어진 그룹에 속하면 true 입니다. 쪼갠 뒤 그 위치에 정적 바디로 서 있는 조각입니다. 쪼개기 전이면 false 입니다. */
        bool isLeafInAnchoredGroup( uint32 leaf ) const;
        /** @brief 쪼갤 때의 오브젝트 자세(메시 공간에서 월드로, 선택한 배율 포함)입니다. 붙어 있는 조각의 정적 바디가 이 자세에 놓입니다. */
        float4x4 makeFracturedObjectMatrix() const;
        /** @brief 쉬는 자세를 정적 메시에 베이크해 스킨드 메시를 숨긴 상태인지 반환합니다. */
        bool isBaked() const { return _renderUnits.isBaked(); }
        /** @brief 파쇄 데이터를 읽었는지입니다. */
        bool                       hasFractureData() const { return _asset != nullptr; }
        const DestructionState&    getState() const { return _state; }
        const DestructionEventLog& getEventLog() const { return _eventLog; }
        /** @brief 사건 기록과 같은 인덱스의 값입니다. 그 사건이 맞힌 떨어진 그룹의 적용 직전 자세입니다. 앵커 그룹이거나 그룹이 없으면 `_groupID` 가 0 입니다. */
        const vector<FractureGroupPose>& getEventGroupPoses() const { return _listEventGroupPose; }
        const FractureAsset*             findAsset() const { return _asset.get(); }
        /** @brief 지금 있는 동적 바디 수(떨어진 그룹)입니다. */
        uint32 getDynamicBodyCount() const;
        /** @brief 지금 있는 정적 조각 바디 수(붙은 조각)입니다. */
        uint32 getStaticBodyCount() const;
        /** @brief 아직 그려지는 떨어진 그룹 수입니다. */
        uint32 getDebrisGroupCount() const;
        /** @brief 잎마다 지금 본 로컬 변환(오브젝트 메시 공간)입니다. */
        const vector<BoneTransform>& getPiecePoses() const { return _listPiecePose; }
        /** @brief 지난 물리 프레임의 처리 시간(마이크로초)입니다. 사건 적용, 바디 생성, 자세 읽기, 렌더링 갱신으로 나뉩니다. */
        float32 getLastApplyMicroseconds() const { return _lastApplyMicroseconds; }
        float32 getLastPoseMicroseconds() const { return _lastPoseMicroseconds; }
        /** @brief 아직 짓지 않은 잎 충돌 셰이프 수입니다. 온전한 동안 프레임마다 예산만큼 줄고, 깨지면 남은 것을 그 자리에서 짓습니다. 상태를 시작하기 전이면 0 입니다. */
        uint32 getPendingLeafShapeCount() const;

        /** @brief 충돌이 사건을 만들지 정하는 권한입니다. 받는 쪽(클라이언트)은 끄고 받은 사건만 적용합니다. */
        void setAuthority( bool bAuthority ) { _bAuthority = bAuthority; }
        bool hasAuthority() const { return _bAuthority; }
        void setAnchorMode( FractureAnchorMode mode ) { _anchorMode = mode; }
        void setProfilePath( string_view path ) { _profilePath = string{ path }; }
        void setInteriorMaterialPath( const hashed_string& path ) { _interiorMaterialPath = path; }

        // --- 네트워크(GF_NetClientServer 의 DestructionReplication 이 쓴다) ----------------------------------------------------------

        /** @brief 상태를 시작했는지 반환합니다(첫 물리 프레임 뒤). 그 전의 사건과 스냅샷은 쌓였다가 시작하면 적용됩니다. */
        bool isStateReady() const { return _bStateReady == SW_TRUE; }
        /** @brief 아직 적용하지 않은 피해나 스냅샷이 있는지 반환합니다. 해시를 비교하기 전에 확인합니다. */
        bool hasPendingDamage() const;
        /** @brief 이 부피(배율을 곱한 세제곱미터)의 떨어진 그룹이 덩어리인지 반환합니다. 프로필의 `keepCollisionVolume` 이상이면 덩어리이고, 서버가 자세를 보냅니다. 그보다 작으면 꾸밈용 파편입니다. */
        bool isChunkVolume( float32 volume ) const { return volume >= _profile._keepCollisionVolume; }
        /**
         * @brief 받는 쪽(권한 없음)이 사건 수 @p eventCount 에 이르렀을 때의 상태 해시를 반환합니다(최근 `FractureNetworkSync::kRecentHashCount` 개). 없으면 false 를 반환합니다.
         * @details 사건은 물리 프레임마다 모아서 적용하므로, 서버가 보낸 (사건 수, 해시)와 같은 순간이 현재 상태로 오지 않을 수 있다.
         *          그래서 사건마다 해시를 기록해 둔다.
         */
        [[nodiscard]] bool findRecentStateHash( uint32 eventCount, uint64& outHash ) const;
        /** @brief 파괴 재질 테이블입니다. 상태를 시작한 뒤의 값입니다. */
        const DestructionProfile& getProfile() const { return _profile; }
        /** @brief 떨어진(앵커가 아닌) 그룹들의 현재 자세입니다. 그룹 번호 순입니다. */
        void collectGroupPoses( vector<FractureGroupPose>& outListPose ) const;
        /**
         * @brief 늦게 참가했거나 상태가 어긋났을 때 바로잡는 스냅샷입니다. 구조 상태(`DestructionState::writeSnapshot`)와 떨어진 그룹의 자세, 사라짐 여부를 담습니다.
         * @details 사건열을 처음부터 다시 돌리지 않는다. 받는 쪽은 `applyNetworkSnapshot` 한 번으로 같은 해시, 같은 그룹, 같은 위치의 덩어리가 된다.
         */
        void makeNetworkSnapshot( vector<uint8>& outBytes ) const;
        /** @brief 스냅샷을 쌓습니다. 다음 물리 프레임에서 앞서 쌓인 사건 뒤에 적용하며, 바디와 렌더링을 그룹마다 다시 만듭니다. */
        void applyNetworkSnapshot( const uint8* pData, size_t size );
        /**
         * @brief 떨어진 그룹 하나의 자세를 네트워크가 정합니다(클라이언트의 덩어리). @p worldCenter 는 월드 질량 중심입니다. 그 바디는 키네마틱이 되고 스텝마다 이 자세로 옮겨집니다.
         * @details 그룹이 아직 없거나(사건이 오기 전) 사라졌으면 아무것도 하지 않는다. 다음 물리 프레임에 반영된다.
         */
        void driveGroup( uint32 groupID, const float3& worldCenter, const quaternion& rotation );

    protected:
        /** @brief 2D 물리(Box2D)를 쓰는지입니다. */
        virtual bool uses2DPhysics() const = 0;
        /** @brief 파쇄 데이터를 반환합니다(3D 는 `.fracture` 캐시, 2D 는 다각형을 그 자리에서 쪼갭니다). 없으면 nullptr 입니다. */
        virtual shared_ptr<const FractureAsset> acquireFracture() = 0;
        /** @brief 파쇄 데이터를 다시 받아야 하는지입니다(핫 리로드). 쪼갠 뒤에는 묻지 않습니다. */
        virtual bool isFractureStale() const { return false; }
        /** @brief 붙어 있는 조각의 형태가 바뀌었을 때 불립니다. 처음 쪼갰을 때, 붙은 그룹이 갈라졌을 때, 떨어졌을 때입니다. 게임 스레드의 물리 프레임 안에서 호출됩니다. */
        virtual void onAnchoredShapeChanged() {}

        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;
        void postPhysicsStep( ScenePhysics& physics ) override;
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief 데이터, 테이블, 앵커로 상태를 시작합니다. */
        bool initializeState( ScenePhysics& physics );
        /** @brief 앵커 잎을 정합니다(모드와 볼륨). */
        void computeAnchors( ScenePhysics& physics, vector<uint8>& outListAnchor ) const;
        /** @brief 쌓인 사건을 적용하고, 바뀐 그룹의 바디와 렌더링을 고칩니다. */
        void processPendingEvents( ScenePhysics& physics );
        /** @brief 오브젝트의 지금 자세(월드)를 읽습니다. */
        void readObjectPose( float3& outPosition, quaternion& outRotation, float32& outScale ) const;
        /**
         * @brief 잎마다 충돌 셰이프(볼록 껍질)의 점을 @p scale 로 만들고 셰이프는 비웁니다. 같은 배율로 이미 만들었으면 아무것도 하지 않습니다.
         * @details 껍질 셰이프 생성이 깨지는 프레임 비용의 대부분이고(200 조각 벽 약 11 ms) 상태를 시작할 때 모두 지으면 플레이 첫 프레임이
         *          파괴물 수에 비례해 길어진다. 그래서 온전한 프레임마다 예산(`gv_fractureShapeWarmMicroseconds`)만큼 나눠 짓고, 그 전에 깨지면 남은 잎을
         *          바디를 만들며 짓는다. 깨질 때는 정적 바디와 떨어진 덩어리(`createCompoundShape`)가 만들어 둔 잎 셰이프를 나눠 쓴다.
         */
        void prepareLeafPoints( float32 scale );
        /** @brief 처음 떨어진 조각이 생겼을 때 호출합니다. 오브젝트의 메시와 강체를 넘겨받고 조각 렌더링과 바디를 만듭니다. */
        void activate( ScenePhysics& physics );
        /** @brief 사건 하나의 결과로 그룹 바디를 고칩니다. */
        void applyGroupChange( ScenePhysics& physics, const DestructionChange& change, const DestructionDamageEvent* pEvent, uint32 eventIndex );
        /** @brief 사라진 그룹의 런타임을 지웁니다. 지운 런타임의 사본은 @p outListRemoved 에, 파괴할 바디는 @p outListDestroy 에 담습니다. */
        void removeGroupRuntimes( const DestructionChange& change, vector<PhysicsBodyHandle>& outListDestroy, vector<FractureGroupRuntime>& outListRemoved );
        /** @brief 새 그룹의 런타임을 만듭니다. 부모 바디의 운동을 이어받고, 떨어진 그룹이면 사건의 충격을 더해 동적 바디를 지습니다. */
        void createGroupRuntime( const DestructionGroup& group, bool bFirst, const vector<FractureGroupRuntime>& listRemoved, const DestructionDamageEvent* pEvent, DestructionRandom& random );
        /** @brief 사건의 충격이 그룹에 주는 선속도 변화를 반환합니다(같은 시드와 사건이면 같은 흩기). */
        float3 computeImpactVelocity( const DestructionGroup& group, const FractureGroupRuntime& runtime, const DestructionDamageEvent& event, DestructionRandom& random ) const;
        /** @brief 떨어진 그룹 하나의 동적 바디를 만듭니다(앵커 그룹은 `syncStaticBodies` 가 잎마다). */
        void spawnGroup( FractureGroupRuntime& inoutRuntime, const DestructionGroup& group, const float3& linearVelocity, const float3& angularVelocity );
        /** @brief 앵커 그룹의 잎에만 정적 바디가 있게 맞춥니다. 남은 바디는 그대로 두므로 붙은 벽이 갈라져도 다시 만들지 않습니다. */
        void syncStaticBodies( vector<PhysicsBodyHandle>& inoutListDestroy );
        /** @brief 메시 공간 사건 하나를 상태에 적용하고, 기록과 그룹 바디를 고칩니다. */
        void applyMeshEvent( ScenePhysics& physics, const DestructionDamageEvent& event, const FractureGroupPose* pGroupPose = nullptr );
        /** @brief 월드 요청 하나를 현재 자세로 메시 공간 사건들로 바꿉니다. */
        void convertWorldDamage( const FracturePendingDamage& pending, vector<DestructionDamageEvent>& outListEvent ) const;
        /** @brief 잎에서 런타임 슬롯으로 가는 테이블을 다시 만듭니다. */
        void rebuildLeafRuntimeMap();
        /** @brief 떨어진 그룹의 자세를 읽고 수명, 수면, 페이드, 예산을 진행합니다. 움직인 것이 있으면 true 입니다. */
        bool updateGroups( float32 deltaTime );
        /** @brief 잎마다 본 로컬 변환을 다시 구해 렌더 유닛에 씁니다. */
        void writePiecePoses();
        /** @brief 렌더 유닛의 스킨드 메시와 정적(베이크한) 메시를 전환합니다. */
        void setBaked( bool bBaked );
        /** @brief 월드 점을 그룹의 메시 공간으로 옮깁니다. */
        float3 toMeshSpace( const FractureGroupRuntime& runtime, const float3& worldPoint ) const;
        /** @brief 바디가 속한 그룹 번호입니다. 없으면 0 입니다. */
        uint32 findGroupOfBody( PhysicsBodyHandle body ) const;
        /** @brief 그룹 번호의 런타임입니다. 없으면 nullptr 입니다. */
        FractureGroupRuntime*       findRuntime( uint32 groupID );
        const FractureGroupRuntime* findRuntime( uint32 groupID ) const;
        /** @brief 넘겨받은 온전한 메시(`_intactMesh`)를 찾습니다. 없거나 사라졌으면 nullptr 입니다. */
        MeshComponent* resolveIntactMesh() const;
        /** @brief 바디가 하나도 남지 않게 해제하고 셰이프를 지웁니다. */
        void releaseAllBodies();
        /** @brief 쌓인 스냅샷 하나를 지금 적용합니다. 상태를 바꾸고 그룹 바디와 렌더링을 다시 만듭니다. */
        void applySnapshotNow( ScenePhysics& physics, const vector<uint8>& bytes );
        /** @brief 쌓인 자세 목표(`FractureNetworkSync`)를 그룹에 반영합니다. 처음 받은 그룹은 바디를 키네마틱으로 바꿉니다. */
        void applyDriveTargets();
        /** @brief 씬의 파편 바디 수(예산)를 바꿉니다. */
        void changeDebrisBodyCount( int32 delta );
        /** @brief 받는 쪽이면 현재 상태의 (사건 수, 해시)를 최근 해시 테이블에 기록합니다. */
        void recordRecentStateHash();

    private:
        /** @brief `_staticLayer` 가 물리 레이어 표에 있는지 봅니다(`ComponentNameValidation`). */
        void validateStaticLayer( ValidationContext& context ) const;
        /** @brief `_chunkLayer` 가 물리 레이어 표에 있는지 봅니다(`ComponentNameValidation`). */
        void validateChunkLayer( ValidationContext& context ) const;
        /** @brief `_debrisLayer` 가 물리 레이어 표에 있는지 봅니다(`ComponentNameValidation`). */
        void validateDebrisLayer( ValidationContext& context ) const;

    private:
        PROPERTY( Category = "Fracture", DisplayName = "Profile", AssetPath, Tooltip = "Destruction profile (*.destruction.xml); empty uses the engine default" )
        string _profilePath;
        PROPERTY( Category = "Fracture", DisplayName = "Interior Material", AssetPath, AssetType = "Material", Tooltip = "Material of the cut faces; empty uses the outer one" )
        hashed_string _interiorMaterialPath;
        PROPERTY( Category = "Fracture", DisplayName = "Static Layer", Tooltip = "Physics layer of pieces still attached to the anchors", Validate = validateStaticLayer )
        hashed_string _staticLayer;
        PROPERTY( Category = "Fracture", DisplayName = "Chunk Layer", Tooltip = "Physics layer of falling chunks that keep collision", Validate = validateChunkLayer )
        hashed_string _chunkLayer;
        PROPERTY( Category = "Fracture", DisplayName = "Debris Layer", Tooltip = "Physics layer of small debris", Validate = validateDebrisLayer )
        hashed_string _debrisLayer;
        PROPERTY( Category = "Fracture", DisplayName = "Anchor Volumes", Tooltip = "Object-space boxes; pieces whose centre is inside are anchored" )
        vector<FractureAnchorVolume> _listAnchorVolume;
        PROPERTY( Category = "Fracture", DisplayName = "Seed", Tooltip = "Scatter seed (send it with the damage events to sync)" )
        uint32 _seed;
        PROPERTY( Category = "Fracture", DisplayName = "Anchor Tolerance", Min = 0.0, Tooltip = "Bottom mode: distance above the lowest face that still counts", Units = m )
        float32 _anchorTolerance;
        PROPERTY( Category = "Fracture", DisplayName = "Anchor Mode", Tooltip = "Which pieces hold the rest up" )
        FractureAnchorMode _anchorMode;
        PROPERTY( Category = "Fracture", DisplayName = "Authority", Tooltip = "Physics impacts create damage events (turn off on network clients)" )
        bool _bAuthority;

        shared_ptr<const FractureAsset>                  _asset;
        unique_ptr<IFracturePhysics>                     _physics;
        DestructionProfile                               _profile;
        DestructionState                                 _state;
        DestructionEventLog                              _eventLog;
        vector<FractureGroupPose>                        _listEventGroupPose; ///< `_eventLog` 와 같은 인덱스다. 맞은 떨어진 그룹의 적용 직전 자세다.
        mutable mutex                                    _pendingMutex;
        FractureNetworkSync                              _networkSync; ///< 최근 상태 해시, `driveGroup` 의 자세 목표, 스냅샷 형식이다.
        vector<FracturePendingDamage> _listPendingDamage SW_GUARDED_BY( _pendingMutex );
        vector<PhysicsBodyHandle>                        _listLeafStaticBody; ///< 잎마다 정적 바디다(앵커 그룹의 잎만).
        vector<FractureGroupRuntime>                     _listRuntime;        ///< 그룹 번호 오름차순이다.
        vector<BoneTransform>                            _listPiecePose;      ///< 잎마다 본 로컬 변환이다.
        vector<uint32>                                   _listRuntimeOfLeaf;  ///< 잎에서 `_listRuntime` 인덱스로 가는 테이블이다(그때마다 다시 만든다).
        FractureRenderUnits                              _renderUnits;        ///< 스킨드 메시 둘과 베이크한 정적 메시 둘이다.
        TComponentHandle<MeshComponent>                  _intactMesh;
        ComponentHandle                                  _intactBody;     ///< 3D 는 `RigidBodyComponent`, 2D 는 `RigidBody2DComponent` 라 타입 없는 핸들로 둔다
        float3                                           _objectPosition; ///< 쪼갤 때의 오브젝트 자세(메시 공간 → 월드)
        quaternion                                       _objectRotation;
        float3                                           _intactLinearVelocity;
        float3                                           _intactAngularVelocity;
        float32                                          _objectScale;    ///< 고른 배율(축 배율의 평균)
        float32                                          _leafShapeScale; ///< 잎 껍질 점을 만든 배율이다(0 = 아직 만들지 않았다).
        float32                                          _lastApplyMicroseconds;
        float32                                          _lastPoseMicroseconds;
        float32                                          _frameSimulatedTime; ///< 이번 물리 프레임에 진행한 고정 스텝 시간이다(수명과 페이드에 쓰며 결정적이다).
        uint32                                           _nextSpawnOrder;
        uint32                                           _stillFrameCount; ///< 움직인 것이 없었던 연속 프레임 수다(베이크 판정).
        uint8                                            _bStateReady;
        uint8                                            _bFractured;
        uint8                                            _bPoseDirty;
        uint8                                            _bFractureMissing; ///< 데이터를 받지 못했다. 캐시 세대가 바뀔 때까지 다시 묻지 않는다.
    };
} // namespace sw
