/**
 * @file BoxCollider2DComponent.h
 * @brief 2D 박스 콜라이더 컴포넌트입니다 — 씬의 2D 물리(Box2D) 안의 센서 바디 하나로 겹침을 냅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct SweptOverlap2D;

    class GameObjectManager;
    class ScenePhysics;

    /**
     * @class BoxCollider2DComponent
     * @brief 축 정렬 상자 하나의 2D 콜라이더입니다(유니티 `BoxCollider2D` 의 트리거 쪽, Godot `Area2D`).
     * @details 플레이 중에는 씬의 2D 물리 씬에 **키네마틱 센서 바디**를 하나 둡니다. 막는 콜라이더도 트리거도 센서입니다(`PhysicsBodyDesc::_bOverlapOnly`) —
     *          Box2D 는 키네마틱끼리 접촉을 만들지 않으므로, 센서여야 콜라이더끼리 겹침이 납니다. 그래서 이 콜라이더는 강체를 막지 않고 겹침만 냅니다
     *          (`onOverlapBegin/End`, `OverlapInfo::_bOtherTrigger` 로 트리거를 가린다). 질의(광선 · 쓸기)에서는 트리거가 아닌 콜라이더가 막는 것으로 보입니다.
     *
     *          바디는 상자(`getBounds`)를 그대로 따릅니다 — 매 물리 프레임 처음(`beginPhysicsFrame`)에 자리를 옮기고, 크기 · 레이어 · 트리거가 바뀌면
     *          다시 만듭니다. 상자는 축 정렬이라 바디는 돌지 않습니다.
     *
     *          **연속 판정**(`_bContinuous`, 투사체): Box2D 센서는 연속 충돌이 없으므로, 지난 자리에서 지금 자리까지 상자를 쓸어 지나친 것을
     *          닿은 시점 순서로 알립니다(`ScenePhysics::queueSweptOverlap2D`, 시작과 끝을 함께). 지금 자리에서도 겹친 것은 센서가 시작을 냅니다.
     *          가만히 있던 상대는 Box2D 쓸기(`shapeCastAll`)로, 같은 프레임에 움직인 상대는 두 이동의 차이(상대 운동, Box2D 총알 TOI)로 측정합니다.
     *          첫 프레임의 출발점은 시작할 때(`onBeginPlay`)의 자리입니다 — 바디가 첫 물리 프레임에 생겨도 시작 뒤 첫 틱의 이동이 쓸립니다.
     *          순간이동(`SceneComponent::teleportTo`)한 프레임은 쓸지 않습니다.
     */
    REFLECT( Category = "Physics 2D", DisplayName = "Box Collider 2D", Tooltip = "2D Box collision volume", Meta = "Icon=Collider, IconColor=Physics" )
    class SW_API BoxCollider2DComponent : public PhysicsComponent
    {
        friend class ScenePhysics; ///< 콜라이더 목록 자리(`_colliderIndex`)를 적고, 모든 콜라이더를 옮긴 뒤 연속 판정의 쓸기를 부른다

    public:
        REFLECT_BODY();
        /** @brief 씬 물리의 콜라이더 목록에 없다는 표시입니다. */
        static constexpr uint32 kNotRegistered = invalid_index::kUint32;

        BoxCollider2DComponent();
        ~BoxCollider2DComponent() override = default;

        /** @brief 씬 물리에 등록하고(바디는 시작한 뒤 첫 물리 프레임에 생긴다) 콜라이더 목록에 듭니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 바디를 놓고 콜라이더 목록 · 씬 물리에서 빠집니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 지금 자리를 연속 판정의 첫 출발점으로 둡니다(바디는 첫 물리 프레임에 생긴다 — 그 사이의 이동도 쓸린다). */
        void onBeginPlay() override;

        /** @brief 물리 레이어 번호입니다(`CollisionLayers`). 시작한 뒤에 바꿔도 다음 물리 프레임부터 그 레이어로 걸러집니다. */
        int32 getColliderType() const { return _colliderType; }
        void  setColliderType( int32 type ) { _colliderType = type; }

        /**
         * @brief 연속 판정인지입니다(유니티 `Rigidbody2D.collisionDetectionMode = Continuous` · 언리얼 `bUseContinuousCollision`).
         * @details 켜면 지난 물리 프레임의 자리에서 지금 자리까지 상자를 쓸어, 한 프레임에 얇은 콜라이더를 건너뛴 것도 겹침으로 냅니다. 빠른 것(투사체)에만 켭니다.
         *          순간이동은 `SceneComponent::teleportTo` 로 합니다 — 그냥 옮기면 그 길도 쓸립니다.
         */
        bool isContinuous() const { return _bContinuous; }
        void setContinuous( bool bContinuous ) { _bContinuous = bContinuous; }

        /**
         * @brief 트리거인지입니다(유니티 `Collider.isTrigger` · 언리얼 Overlap 반응).
         * @details 트리거도 겹침 이벤트는 냅니다 — 받는 쪽이 `OverlapInfo::_bOtherTrigger` 로 가립니다. 투사체 · 공격 판정은 트리거에 막히지도 피해를
         *          주지도 않습니다(감지 범위 · 구역 볼륨이 총알을 먹지 않는다). 트리거는 질의(광선 · 쓸기)에도 보이지 않습니다.
         */
        bool isTrigger() const { return _bTrigger; }
        void setTrigger( bool bTrigger ) { _bTrigger = bTrigger; }

        /** @brief 소유자 위치를 기준으로 한 콜라이더 중심 오프셋입니다. */
        float2 getOffsetPosition() const { return _offsetPos; }
        void   setOffsetPosition( const float2& offset ) { _offsetPos = offset; }

        /** @brief 콜라이더 박스의 가로 · 세로 크기입니다(로컬 — 월드 스케일을 받는다). */
        float2 getOffsetScale() const { return _offsetScale; }
        void   setOffsetScale( const float2& scale ) { _offsetScale = scale; }

        void getBounds( float2& outMin, float2& outMax ) const;
        /** @brief 물리가 쓰는 상자(`getBounds`)를 덮는 구입니다. 중심의 Z 는 컴포넌트의 월드 Z 입니다. */
        bool getWorldBounds( float3& outCenter, float32& outRadius ) const override;
        /** @brief 물리가 판정하는 그 상자(`getBounds`)입니다. 깊이는 없습니다(월드 Z 한 점). */
        bool getWorldBox( AABB& outBox ) const override;
        /**
         * @brief 두 상자(`getBounds`)가 겹치는지입니다 — 순수 기하, 레이어를 보지 않습니다(유니티 `Bounds.Intersects`).
         * @details 바디 등록 여부와 무관하게 지금 상자로 측정합니다. 에디터 히트 테스트 · 배치 검사처럼 "공간이 겹치는가" 를 물을 때 씁니다.
         */
        bool overlapsBounds( const BoxCollider2DComponent* pOther ) const;
        /** @brief 상자가 [@p minB, @p maxB] 와 겹치는지입니다(경계 포함, 순수 기하). */
        bool overlapsBounds( const float2& minB, const float2& maxB ) const;
        /** @brief 상자가 @p point 를 담는지입니다(경계 포함, 순수 기하). */
        bool containsPoint( const float2& point ) const;
        /**
         * @brief 두 콜라이더가 물리 규칙으로 겹치는지입니다 — 상자가 겹치고 레이어 행렬이 그 쌍을 허용해야 합니다(유니티 `Collider2D.IsTouching`).
         * @details 지금 상자로 측정합니다 — 바디가 아직 없어도(시작 전 · 에디터) 같은 답을 냅니다. 레이어 행렬은 등록된 씬의 2D 물리 씬의 것이고(`getLayerCollision`),
         *          어느 쪽도 2D 씬이 없으면 기본 행렬(모든 쌍 허용)입니다.
         */
        bool isTouching( const BoxCollider2DComponent* pOther ) const;

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief 지금 상자 · 레이어 · 트리거로 바디를 만듭니다. */
        void createBody( ScenePhysics& physics, const float2& center, const float2& halfExtents );
        /** @brief 이번 물리 프레임에 바디가 옮겨졌는지입니다(출발점 `_frameFromCenter` 와 바디 중심이 다르다). 순간이동한 프레임은 옮겨지지 않은 것으로 봅니다. */
        bool hasMovedThisFrame() const { return _body.isValid() && _frameFromCenter != _bodyCenter; }
        /**
         * @brief 연속 판정 — 이번 프레임의 출발점에서 바디 중심까지 상자를 쓸어 지나친 것을 씬 물리에 알립니다(`ScenePhysics::queueSweptOverlap2D`).
         * @details 가만히 있던 바디는 Box2D 쓸기로, @p listMoved (이번 프레임에 움직인 콜라이더, 자기 포함)는 두 이동의 차이로 측정한다. 움직인 바디를 Box2D
         *          쓸기로 측정하면 상대가 이번 자리에 선 채로 측정돼 길을 가로질러 건너간 상대를 놓친다. 출발점에서 이미 겹친 것(닿은 시점 0)과 도착점에서도
         *          겹친 것은 뺀다 — 앞은 지난 프레임의 센서가, 뒤는 이번 프레임의 센서가 시작을 낸다. 모든 콜라이더의 `beginPhysicsFrame` 뒤에 부른다.
         */
        void sweepPassedBodies( ScenePhysics& physics, const vector<BoxCollider2DComponent*>& listMoved );
        /** @brief 이 콜라이더와 상대 바디가 지나치며 겹친 기록 하나를 만듭니다(`sweepPassedBodies`). */
        SweptOverlap2D makeSweptOverlap( PhysicsBodyHandle otherBody, uint64 otherUserData, float32 hitFraction, bool bOtherTrigger ) const;

        /**
         * @brief 콜라이더 오프셋 · 크기입니다.
         * @details getBounds() 는 물리 프레임과 에디터의 기즈모 · 히트 테스트가 매 프레임 부르므로, 문자열이 아니라 float2 PROPERTY 로 듭니다.
         */
        PROPERTY( Category = "Collider", DisplayName = "Offset Position", Tooltip = "Collider center offset from the owner" )
        float2 _offsetPos;
        PROPERTY( Category = "Collider", DisplayName = "Offset Scale", Tooltip = "Collider box size" )
        float2 _offsetScale;
        PROPERTY( Category = "Collider", DisplayName = "Collider Type", Tooltip = "Physics collider type index" )
        int32 _colliderType;
        PROPERTY( Category = "Collider", DisplayName = "Continuous", Tooltip = "Sweep the box from its last physics frame so a fast mover cannot pass through a thin collider" )
        bool _bContinuous;
        PROPERTY( Category = "Collider", DisplayName = "Is Trigger", Tooltip = "Reports overlaps but does not block - projectiles and attacks ignore it" )
        bool _bTrigger;

        PhysicsBodyHandle _body;              ///< 2D 씬의 센서 바디(시작 전 · 꺼짐이면 무효)
        float2            _bodyCenter;        ///< 바디에 준 마지막 중심(이번 프레임의 도착점)
        float2            _bodyHalfExtent;    ///< 바디를 만든 반 크기(바뀌면 다시 만든다)
        float2            _sweepOrigin;       ///< 다음 물리 프레임의 출발점 — 시작할 때의 자리, 그 뒤로는 지난 프레임의 중심
        float2            _frameFromCenter;   ///< 이번 물리 프레임의 출발점(순간이동했거나 출발점이 없으면 도착점과 같다)
        float2            _bodyOffsetPos;     ///< 바디 상자를 잰 때의 `_offsetPos`(세터가 표시를 남기지 않아 값으로 비교한다)
        float2            _bodyOffsetScale;   ///< 바디 상자를 잰 때의 `_offsetScale`
        int32             _bodyLayer;         ///< 바디를 만든 레이어
        bool              _bBodyTrigger;      ///< 바디를 만든 트리거 여부
        bool              _bSweepOriginValid; ///< `_sweepOrigin` 이 있는지(시작 전 · 바디를 놓은 뒤에는 없다 — 다시 켠 콜라이더는 꺼진 자리에서 쓸리지 않는다)
        uint32            _colliderIndex;     ///< 씬 물리의 콜라이더 목록 자리(`ScenePhysics::registerCollider2D`). 없으면 `kNotRegistered`
    };
} // namespace sw
