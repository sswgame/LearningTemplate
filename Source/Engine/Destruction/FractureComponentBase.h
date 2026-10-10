/**
 * @file FractureComponentBase.h
 * @brief 쪼갠 조각을 런타임에 다루는 컴포넌트의 공통(3D `FractureComponent` · 2D `Fracture2DComponent`) — 구조 상태(`DestructionState`)를 들고,
 *        피해 사건을 받아 바디 · 그림을 바꿉니다.
 * @details **온전할 때는 오브젝트의 메시 하나가 그리기 하나**입니다(이 컴포넌트는 그리지도 바디를 들지도 않는다 — 오브젝트의 `RigidBodyComponent` 가
 *          충돌을 맡는다). 처음 떨어진 것이 생기면(그룹이 갈라지면) **쪼갠 상태로 바꿉니다**: 오브젝트의 메시를 숨기고 강체 컴포넌트를 끄고, 조각마다
 *          본 하나인 스킨드 메시 둘(겉면 · 안쪽 면 — 칸마다 그리기 하나, `FractureRenderUtil`)을 같은 오브젝트에 붙이고, 앵커에 붙은 조각은 조각마다
 *          정적 바디(미리 지은 껍질 셰이프를 나눠 쓴다, 한 번에 만든다), 떨어진 그룹은 그룹마다 동적 바디 하나(조각 껍질의 컴파운드)를 만듭니다.
 *          그룹이 갈라지면 옛 바디를 지우고 갈라진 덩어리마다 그 자세 · 속도(질량 중심의 선속도 + 각속도 × 거리)로 새 바디를 만듭니다.
 *
 *          **예산 · 정리(Chaos 의 Remove on Sleep).** 떨어진 그룹이 잠들어 `sleepRemoveTime` 을 쉬면 바디를 뺍니다(큰 덩어리 —
 *          `keepCollisionVolume` 이상 — 는 잠든 바디를 남겨 충돌을 지킨다). 작은 파편(`smallDebrisVolume` 미만)은 `lifetime` 뒤에 `fadeTime` 동안
 *          줄어들며 사라집니다. 떨어진 그룹의 바디가 `maxBodies`(그리고 전역 `gv_destructionMaxDebrisBodies`)를 넘으면 가장 오래된 작은 것부터
 *          사라지게 합니다. 움직이는 바디가 하나도 없고 사라지는 중인 것도 없으면 지금 자세를 정적 메시 둘에 구워 스킨드 메시를 숨깁니다(프레임 비용 0).
 *          다시 맞으면 스킨드 메시로 돌아옵니다.
 *
 *          **피해.** `applyDamage`(메시 공간) · `applyPointDamageAtWorld` · `applyRadialDamageAtWorld` · `applyRaycastDamage` 는 어느 틱에서든 불러도 되고
 *          (잠금 아래 쌓인다), 다음 물리 프레임에 게임 스레드에서 적용됩니다. 부딪힘(`onCollisionBegin` 의 시작 충격량)은 권한이 있을 때(`_bAuthority`)만
 *          사건이 됩니다. 적용한 사건은 `getEventLog()` 에 쌓입니다 — 다른 기계는 그것을 `applyDamage` 로 같은 순서로 받으면 같은 상태입니다.
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
#include "Core/Network/Replication/TickRingBuffer.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct FractureAsset;

    class GameObjectManager;
    class IFracturePhysics;
    class Mesh;
    class MeshComponent;
    class SkeletalMeshComponent;

    /** @brief 어느 조각을 땅에 붙은(앵커) 것으로 볼지입니다. 앵커 볼륨(`_listAnchorVolume`)은 늘 더해집니다. */
    ENUM()
    enum class FractureAnchorMode : uint8
    {
        None = 0, ///< 앵커 없음 — 통째로 동적(상자 · 통)
        Bottom,   ///< 가장 낮은 면에서 `_anchorTolerance` 안에 닿는 조각(벽 · 기둥)
        World,    ///< 시작할 때 다른 오브젝트의 정적 바디에 닿은 조각
    };
} // namespace sw

namespace sw
{
    /** @brief 앵커 볼륨 하나(오브젝트 로컬 상자)입니다 — 안에 무게 중심이 든 조각은 앵커입니다. */
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
    /** @brief 런타임 그룹 하나의 바디 · 자세 · 수명입니다. */
    struct FractureGroupRuntime
    {
        float3             _position{};      ///< 그룹 자세(오브젝트 원점이 있는 월드 자리)
        quaternion         _rotation{};      ///< 그룹 자세의 회전
        float3             _drivePosition{}; ///< 네트워크가 정한 목표 자세(`_bDriven`)
        quaternion         _driveRotation{};
        PhysicsBodyHandle  _body{};  ///< 떨어진 그룹: 동적 바디 하나(앵커 그룹은 잎마다 정적 바디 — 컴포넌트가 잎 표로 든다)
        PhysicsShapeHandle _shape{}; ///< 떨어진 그룹의 컴파운드 셰이프(바디와 함께 놓는다)
        uint32             _groupID{ 0 };
        uint32             _spawnOrder{ 0 }; ///< 만든 순서(예산이 오래된 것부터 고른다)
        float32            _volume{ 0.0f };  ///< 배율을 건 부피(세제곱미터)
        float32            _age{ 0.0f };
        float32            _sleepTime{ 0.0f };
        float32            _fade{ 1.0f }; ///< 1 = 온전, 0 = 사라짐(본 배율)
        uint8              _bAnchored{ SW_FALSE };
        uint8              _bFrozen{ SW_FALSE }; ///< 바디를 뺐다(자세 고정)
        uint8              _bFading{ SW_FALSE };
        uint8              _bGone{ SW_FALSE };   ///< 사라졌다(그리지 않는다)
        uint8              _bDriven{ SW_FALSE }; ///< 네트워크가 자세를 정한다 — 바디는 키네마틱, 스텝마다 목표로 옮긴다
        uint8              _layer{ 0 };          ///< 떨어진 그룹 바디의 물리 레이어(덩어리 · 파편)
    };
} // namespace sw

namespace sw
{
    /** @brief 떨어진 그룹 하나의 지금 자세입니다(네트워크 복제가 읽는다). */
    struct FractureGroupPose
    {
        float3     _position{}; ///< 그룹 자세의 자리(오브젝트 원점이 있는 월드 자리)
        float3     _center{};   ///< 질량 중심(월드) — 보간 · 오차는 이것으로 본다(원점은 덩어리에서 멀 수 있어 돌면 크게 움직인다)
        quaternion _rotation{};
        uint32     _groupID{ 0 };
        float32    _volume{ 0.0f };       ///< 배율을 건 부피 — 덩어리(`isChunkVolume`)면 서버가 자세를 보낸다
        uint8      _bResting{ SW_FALSE }; ///< 잠들었거나 바디를 뺐다(멈췄다)
        uint8      _bGone{ SW_FALSE };    ///< 사라졌다
        uint8      _bDriven{ SW_FALSE };  ///< 네트워크가 자세를 정한다(클라이언트의 덩어리 — 키네마틱)
        uint8      _bHasBody{ SW_FALSE }; ///< 바디가 있다(충돌한다)
        uint8      _layer{ 0 };           ///< 바디의 물리 레이어
    };
} // namespace sw

namespace sw
{
    /** @brief 아직 적용하지 않은 피해 하나입니다(월드 요청은 물리 프레임에 그 때의 자세로 메시 공간 사건이 된다). */
    struct FracturePendingDamage
    {
        enum class Kind : uint8
        {
            Mesh = 0,    ///< 메시 공간 사건 그대로(네트워크로 받은 것)
            WorldPoint,  ///< 월드 맞은 자리
            WorldRadial, ///< 월드 폭발
            Snapshot,    ///< 상태 스냅숏(`applyNetworkSnapshot`) — 앞뒤 사건과 받은 순서대로 적용한다
        };

        vector<uint8>          _snapshotBytes{}; ///< Snapshot 의 바이트
        FractureGroupPose      _groupPose{};     ///< Mesh — 사건이 맞힌 떨어진 그룹의 보낸 쪽 자세(`_bHasGroupPose`). 적용 전에 그 자리로 옮긴다
        DestructionDamageEvent _event{};         ///< Mesh 면 그대로, 아니면 변형 · 반경 · 충격량만
        float3                 _worldPoint{};    ///< 월드 중심
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

        static constexpr uint32 kRecentHashCount = 64; ///< 받는 쪽이 사건마다 적어 두는 최근 상태 해시 수

        FractureComponentBase();
        ~FractureComponentBase() override;

        void onBeginPlay() override;
        void onCollisionBegin( const CollisionInfo& collision ) override;

        /** @brief 메시 공간 피해 사건 하나를 쌓습니다(다음 물리 프레임에 적용). 어느 틱에서든 부를 수 있습니다. */
        void applyDamage( const DestructionDamageEvent& event );
        /**
         * @brief 받은 사건 + 그 사건이 맞힌 떨어진 그룹의 보낸 쪽 자세입니다(네트워크). 적용하기 전에 그 그룹을 그 자리로 옮긴다 — 갈라진 조각이 받는 쪽의
         *        늦은 자세가 아니라 보낸 쪽이 갈랐던 자리에서 태어난다.
         */
        void applyDamage( const DestructionDamageEvent& event, const FractureGroupPose& groupPose );
        /** @brief 월드의 맞은 자리 피해입니다(무기). @p hitBody 가 이 오브젝트의 떨어진 덩어리면 그 덩어리의 잎만 맞습니다. */
        void applyPointDamageAtWorld( const float3& worldPoint, const float3& worldDirection, float32 strain, float32 impulse, PhysicsBodyHandle hitBody );
        /** @brief 월드의 폭발 피해입니다. 붙은 구조와, 반경 안의 떨어진 덩어리마다 사건을 하나씩 만듭니다. */
        void applyRadialDamageAtWorld( const float3& worldCenter, float32 radius, float32 strain, float32 impulse );
        /**
         * @brief 광선이 처음 닿은 파괴 오브젝트에 맞은 자리 피해를 줍니다(3D). 닿은 것이 파괴 오브젝트가 아니면 false 입니다.
         * @param pOutPoint 닿은 자리(월드)를 받습니다(선택).
         */
        [[nodiscard]] static bool applyRaycastDamage( GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 strain, float32 impulse,
                                                      float3* pOutPoint = nullptr );

        /**
         * @brief 월드 구(@p worldCenter · @p radius)가 이 오브젝트의 조각 경계 구에 닿는지입니다(폭발이 벽처럼 큰 것의 가장자리만 스쳐도 맞게).
         *        파쇄 데이터를 아직 읽지 않았으면 오브젝트 원점까지의 거리로 봅니다.
         */
        bool isReachedBy( const float3& worldCenter, float32 radius ) const;
        /** @brief 쪼갠 상태로 바뀌었는지입니다(처음 떨어진 것이 생긴 뒤). */
        bool isFractured() const { return _bFractured == SW_TRUE; }
        /** @brief 잎이 붙어 있는(앵커까지 이어진) 그룹에 들었으면 true 입니다 — 쪼갠 뒤 그 자리에 정적 바디로 선 조각. 쪼개기 전이면 false 입니다. */
        bool isLeafInAnchoredGroup( uint32 leaf ) const;
        /** @brief 쪼갤 때의 오브젝트 자세(메시 공간 → 월드, 고른 배율)입니다 — 붙어 있는 조각의 정적 바디가 이 자세에 선다. */
        float4x4 makeFracturedObjectMatrix() const;
        /** @brief 쉬는 자세를 정적 메시에 구워 스킨드 메시를 숨긴 상태인지입니다. */
        bool isBaked() const { return _bBaked == SW_TRUE; }
        /** @brief 파쇄 데이터를 읽었는지입니다. */
        bool                       hasFractureData() const { return _asset != nullptr; }
        const DestructionState&    getState() const { return _state; }
        const DestructionEventLog& getEventLog() const { return _eventLog; }
        /** @brief 사건 기록과 같은 자리 — 그 사건이 맞힌 떨어진 그룹의 적용 직전 자세입니다(앵커 그룹 · 그룹 없음은 `_groupID` 0). */
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
        /** @brief 지난 물리 프레임의 처리 시간(마이크로초 — 사건 적용 · 바디 만들기 / 자세 읽기 · 그림)입니다. */
        float32 getLastApplyMicroseconds() const { return _lastApplyMicroseconds; }
        float32 getLastPoseMicroseconds() const { return _lastPoseMicroseconds; }

        /** @brief 부딪힘이 사건을 만들지(권한)입니다. 받는 쪽(클라이언트)은 끄고 받은 사건만 적용합니다. */
        void setAuthority( bool bAuthority ) { _bAuthority = bAuthority; }
        bool hasAuthority() const { return _bAuthority; }
        void setAnchorMode( FractureAnchorMode mode ) { _anchorMode = mode; }
        void setProfilePath( string_view path ) { _profilePath = string{ path }; }
        void setInteriorMaterialPath( const hashed_string& path ) { _interiorMaterialPath = path; }

        // --- 네트워크(GF_NetClientServer 의 DestructionReplication 이 쓴다) ----------------------------------------------------------

        /** @brief 상태를 시작했는지입니다(첫 물리 프레임 뒤). 그 전의 사건 · 스냅숏은 쌓였다가 시작하면 적용된다. */
        bool isStateReady() const { return _bStateReady == SW_TRUE; }
        /** @brief 아직 적용하지 않은 피해 · 스냅숏이 있는지입니다(해시를 비교하기 전에 본다). */
        bool hasPendingDamage() const;
        /** @brief 이 부피(배율을 건 세제곱미터)의 떨어진 그룹이 덩어리인지입니다 — 표의 `keepCollisionVolume` 이상. 덩어리는 서버가 자세를 보내고, 아래는 파편(꾸밈)이다. */
        bool isChunkVolume( float32 volume ) const { return volume >= _profile._keepCollisionVolume; }
        /**
         * @brief 받는 쪽(권한 없음)이 사건 수 @p eventCount 에 이르렀을 때의 상태 해시입니다(최근 `kRecentHashCount` 개). 없으면 false.
         * @details 사건은 물리 프레임마다 묶여 적용되므로 서버가 보낸 (사건 수, 해시)와 같은 순간이 지금 상태로 오지 않을 수 있다 — 사건마다 적어 둔다.
         */
        [[nodiscard]] bool findRecentStateHash( uint32 eventCount, uint64& outHash ) const;
        /** @brief 파괴 재질 표입니다(상태를 시작한 뒤의 값). */
        const DestructionProfile& getProfile() const { return _profile; }
        /** @brief 떨어진(앵커가 아닌) 그룹들의 지금 자세입니다(그룹 번호 순). */
        void collectGroupPoses( vector<FractureGroupPose>& outListPose ) const;
        /**
         * @brief 늦은 참가 · 어긋남 바로잡기용 스냅숏 — 구조 상태(`DestructionState::writeSnapshot`)와 떨어진 그룹의 자세 · 사라짐입니다.
         * @details 사건열을 처음부터 다시 돌리지 않는다. 받는 쪽은 `applyNetworkSnapshot` 한 번으로 같은 해시 · 같은 그룹 · 그 자리의 덩어리가 된다.
         */
        void makeNetworkSnapshot( vector<uint8>& outBytes ) const;
        /** @brief 스냅숏을 쌓습니다(다음 물리 프레임에 앞서 쌓인 사건 뒤에 적용). 바디 · 그림을 그룹마다 다시 짓는다. */
        void applyNetworkSnapshot( const uint8* pData, size_t size );
        /**
         * @brief 떨어진 그룹 하나의 자세를 네트워크가 정합니다(클라이언트의 덩어리) — @p worldCenter 는 질량 중심(월드). 그 바디는 키네마틱이 되고 스텝마다 이 자세로 옮겨진다.
         * @details 그룹이 아직 없거나(사건이 오기 전) 사라졌으면 아무것도 하지 않는다. 다음 물리 프레임에 든다.
         */
        void driveGroup( uint32 groupID, const float3& worldCenter, const quaternion& rotation );

    protected:
        /** @brief 2D 물리(Box2D)를 쓰는지입니다. */
        virtual bool uses2DPhysics() const = 0;
        /** @brief 파쇄 데이터를 줍니다(3D: `.fracture` 캐시, 2D: 다각형을 그 자리에서 쪼갬). 없으면 nullptr 입니다. */
        virtual shared_ptr<const FractureAsset> acquireFracture() = 0;
        /** @brief 파쇄 데이터를 다시 받아야 하는지입니다(핫 리로드). 쪼갠 뒤에는 묻지 않습니다. */
        virtual bool isFractureStale() const { return false; }
        /** @brief 붙어 있는 조각의 모양이 바뀌었습니다(처음 쪼갬 · 붙은 그룹이 갈라짐 · 떨어짐). 게임 스레드 · 물리 프레임 안에서 불립니다. */
        virtual void onAnchoredShapeChanged() {}

        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;
        void postPhysicsStep( ScenePhysics& physics ) override;
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief 데이터 · 표 · 앵커로 상태를 시작합니다. */
        bool initializeState( ScenePhysics& physics );
        /** @brief 앵커 잎을 정합니다(모드 + 볼륨). */
        void computeAnchors( ScenePhysics& physics, vector<uint8>& outListAnchor ) const;
        /** @brief 쌓인 사건을 적용하고 바뀐 그룹의 바디 · 그림을 고칩니다. */
        void processPendingEvents( ScenePhysics& physics );
        /** @brief 오브젝트의 지금 자세(월드)를 읽습니다. */
        void readObjectPose( float3& outPosition, quaternion& outRotation, float32& outScale ) const;
        /**
         * @brief 잎마다 충돌 셰이프(볼록 껍질)를 @p scale 로 모두 짓습니다. 같은 배율로 이미 지었으면 아무것도 하지 않습니다.
         * @details 껍질 짓기가 깨지는 프레임 비용의 대부분이라(200 조각 벽 ≈ 8 ms) 상태를 시작할 때 미리 짓습니다 — 깨질 때는 정적 바디 · 떨어진
         *          덩어리(`createCompoundShape`)가 지어 둔 잎 셰이프를 나눠 씁니다.
         */
        void prepareLeafShapes( float32 scale );
        /** @brief 처음 떨어진 것이 생겼다 — 오브젝트의 메시 · 강체를 넘겨받고 조각 그림 · 바디를 만듭니다. */
        void activate( ScenePhysics& physics );
        /** @brief 사건 하나의 결과로 그룹 바디를 고칩니다. */
        void applyGroupChange( ScenePhysics& physics, const DestructionChange& change, const DestructionDamageEvent* pEvent, uint32 eventIndex );
        /** @brief 떨어진 그룹 하나의 동적 바디를 만듭니다(앵커 그룹은 `syncStaticBodies` 가 잎마다). */
        void spawnGroup( FractureGroupRuntime& inoutRuntime, const DestructionGroup& group, const float3& linearVelocity, const float3& angularVelocity );
        /** @brief 앵커 그룹의 잎에만 정적 바디가 있게 맞춥니다(남는 것은 그대로 — 붙은 벽이 갈라져도 다시 만들지 않는다). */
        void syncStaticBodies( vector<PhysicsBodyHandle>& inoutListDestroy );
        /** @brief 메시 공간 사건 하나를 상태에 적용하고 기록 · 그룹 바디를 고칩니다. */
        void applyMeshEvent( ScenePhysics& physics, const DestructionDamageEvent& event, const FractureGroupPose* pGroupPose = nullptr );
        /** @brief 월드 요청 하나를 지금 자세로 메시 공간 사건들로 바꿉니다. */
        void convertWorldDamage( const FracturePendingDamage& pending, vector<DestructionDamageEvent>& outListEvent ) const;
        /** @brief 잎 → 런타임 자리 표를 다시 짓습니다. */
        void rebuildLeafRuntimeMap();
        /** @brief 그림 유닛(스킨드 메시 둘)을 만듭니다. */
        void createRenderUnits();
        /** @brief 떨어진 그룹의 자세를 읽고 수명 · 잠 · 페이드 · 예산을 진행합니다. 움직인 것이 있으면 true 입니다. */
        bool updateGroups( float32 deltaTime );
        /** @brief 잎마다 본 로컬 변환을 다시 구하고 그림에 씁니다. */
        void writePiecePoses();
        /** @brief 스킨드 메시와 정적(구운) 메시를 오갑니다. */
        void setBaked( bool bBaked );
        /** @brief 월드 점을 그룹의 메시 공간으로 옮깁니다. */
        float3 toMeshSpace( const FractureGroupRuntime& runtime, const float3& worldPoint ) const;
        /** @brief 바디가 속한 그룹 번호입니다. 없으면 0 입니다. */
        uint32 findGroupOfBody( PhysicsBodyHandle body ) const;
        /** @brief 그룹 번호의 런타임입니다. 없으면 nullptr 입니다. */
        FractureGroupRuntime*       findRuntime( uint32 groupID );
        const FractureGroupRuntime* findRuntime( uint32 groupID ) const;
        /** @brief 이 오브젝트의 컴포넌트 핸들을 풉니다. 사라졌으면 nullptr 입니다. */
        Component* resolveOwnedComponent( const ComponentHandle& handle ) const;
        /** @brief 바디가 하나도 남지 않게 놓고 셰이프를 지웁니다. */
        void releaseAllBodies();
        /** @brief 쌓인 스냅숏 하나를 지금 적용합니다 — 상태를 바꾸고 그룹 바디 · 그림을 다시 짓는다. */
        void applySnapshotNow( ScenePhysics& physics, const vector<uint8>& bytes );
        /** @brief 쌓인 자세 목표를 그룹에 옮깁니다(처음 받은 그룹은 바디를 키네마틱으로). */
        void applyDriveTargets();
        /** @brief 씬의 파편 바디 수(예산)를 바꿉니다. */
        void changeDebrisBodyCount( int32 delta );
        /** @brief 받는 쪽이면 지금 상태의 (사건 수, 해시)를 최근 표에 적습니다. */
        void recordRecentStateHash();

    private:
        PROPERTY( Category = "Fracture", DisplayName = "Profile", AssetPath, Tooltip = "Destruction profile (*.destruction.xml); empty uses the engine default" )
        string _profilePath;
        PROPERTY( Category = "Fracture", DisplayName = "Interior Material", AssetPath, AssetType = "Material", Tooltip = "Material of the cut faces; empty uses the outer one" )
        hashed_string _interiorMaterialPath;
        PROPERTY( Category = "Fracture", DisplayName = "Static Layer", Tooltip = "Physics layer of pieces still attached to the anchors" )
        hashed_string _staticLayer;
        PROPERTY( Category = "Fracture", DisplayName = "Chunk Layer", Tooltip = "Physics layer of falling chunks that keep collision" )
        hashed_string _chunkLayer;
        PROPERTY( Category = "Fracture", DisplayName = "Debris Layer", Tooltip = "Physics layer of small debris" )
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

        shared_ptr<const FractureAsset> _asset;
        unique_ptr<IFracturePhysics>    _physics;
        DestructionProfile              _profile;
        DestructionState                _state;
        DestructionEventLog             _eventLog;
        vector<FractureGroupPose>       _listEventGroupPose; ///< `_eventLog` 와 같은 자리 — 맞은 떨어진 그룹의 적용 직전 자세
        mutable mutex                   _pendingMutex;
        vector<FractureGroupPose>       _listPendingDrive; ///< `driveGroup` 이 쌓은 목표(잠금 아래)
        TickRingBuffer<uint64>          _listRecentHash;   ///< 받는 쪽 — 사건 수로 찾는 최근 상태 해시(처음 적을 때 `kRecentHashCount` 칸을 잡는다)
        vector<FracturePendingDamage>   _listPendingDamage;
        vector<PhysicsBodyHandle>       _listLeafStaticBody; ///< 잎마다 정적 바디(앵커 그룹의 잎만)
        vector<FractureGroupRuntime>    _listRuntime;        ///< 그룹 번호 오름차순
        vector<BoneTransform>           _listPiecePose;      ///< 잎마다 본 로컬
        vector<uint32>                  _listRuntimeOfLeaf;  ///< 잎 → `_listRuntime` 자리(그때마다 다시 짓는다)
        shared_ptr<Mesh>                _arrSkinnedMesh[2];  ///< 칸(겉면 · 안쪽 면)마다 스킨드 메시 원본
        ComponentHandle                 _arrSkinnedUnit[2];
        ComponentHandle                 _arrBakedUnit[2];
        ComponentHandle                 _intactMesh;
        ComponentHandle                 _intactBody;
        float3                          _objectPosition; ///< 쪼갤 때의 오브젝트 자세(메시 공간 → 월드)
        quaternion                      _objectRotation;
        float3                          _intactLinearVelocity;
        float3                          _intactAngularVelocity;
        float32                         _objectScale;    ///< 고른 배율(축 배율의 평균)
        float32                         _leafShapeScale; ///< 잎 셰이프를 지은 배율(0 = 아직 짓지 않았다)
        float32                         _lastApplyMicroseconds;
        float32                         _lastPoseMicroseconds;
        float32                         _frameSimulatedTime; ///< 이번 물리 프레임에 돈 고정 스텝 시간(수명 · 페이드 — 결정적)
        uint32                          _nextSpawnOrder;
        uint32                          _stillFrameCount; ///< 움직인 것이 없었던 연속 프레임 수(굽기 판정)
        uint8                           _bStateReady;
        uint8                           _bFractured;
        uint8                           _bBaked;
        uint8                           _bPoseDirty;
        uint8                           _bFractureMissing; ///< 데이터를 못 받았다 — 캐시 세대가 바뀔 때까지 다시 묻지 않는다
    };
} // namespace sw
