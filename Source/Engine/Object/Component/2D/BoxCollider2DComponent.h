/**
 * @file BoxCollider2DComponent.h
 * @brief 2D 박스 콜라이더 컴포넌트입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;
    class PhysicsWorld;

    REFLECT( Category = "Physics 2D", DisplayName = "Box Collider 2D", Tooltip = "2D Box collision volume" )
    class SW_API BoxCollider2DComponent : public SceneComponent
    {
        friend class GameObjectManager; ///< step 직전에 바디를 맞추고(`syncPhysicsBody`) 목록 자리(`_colliderIndex`)를 적는다

    public:
        REFLECT_BODY();
        /** @brief 매니저의 콜라이더 목록에 없다는 표시입니다. */
        static constexpr uint32 kNotRegistered = 0xFFFFFFFFu;

        BoxCollider2DComponent();
        virtual ~BoxCollider2DComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onDestroy() override;
        /** @brief 물리 월드를 등록 시점에 받아 두고 매니저의 콜라이더 목록에 듭니다(바디는 매니저가 step 직전에 맞춘다 — 틱하지 않는다). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 바디를 빼고 물리 월드 참조 · 콜라이더 목록 자리를 놓습니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 물리 레이어입니다(`CollisionLayers`). 시작한 뒤에 바꿔도 다음 step 부터 겹침이 그 레이어로 걸러집니다. */
        int32 getColliderType() const { return _colliderType; }
        void  setColliderType( int32 type ) { _colliderType = type; }

        /**
         * @brief 연속 충돌(CCD)로 판정하는지입니다(유니티 `Rigidbody2D.collisionDetectionMode = Continuous` · 언리얼 `bUseCCD`).
         * @details 켜면 물리가 지난 step 의 자리에서 지금 자리까지 상자를 쓸어, 한 프레임에 얇은 콜라이더를 건너뛴 것도 겹침으로 냅니다
         *          (`PhysicsWorld::step`). 빠른 것(투사체)에만 켭니다 — 순간이동도 그 길을 쓸어 지나간 것과 닿습니다.
         */
        bool isContinuous() const { return _bContinuous; }
        void setContinuous( bool bContinuous ) { _bContinuous = bContinuous; }

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
        bool intersects( const BoxCollider2DComponent* pOther ) const;
        bool intersects( const float2& point ) const;
        bool intersects( const float2& minB, const float2& maxB ) const;

    private:
        void unregisterPhysicsBody();
        /** @brief 바디를 지금 상자 · 레이어 · 연속 여부에 맞춥니다. 시작 전이거나 꺼져 있으면 바디를 뺍니다(겹침에 들지 않는다). */
        void syncPhysicsBody();

        /**
         * @brief 콜라이더 오프셋 · 크기입니다.
         * @details 예전에는 `string` 으로 두고 쓸 때마다 string_splitter 로 쪼개 parseFloat 했습니다.
         *          getBounds() 는 onTick 의 syncPhysicsBody 와 에디터의 기즈모 · 히트 테스트가 부르므로,
         *          콜라이더 하나당 **매 프레임** 문자열 두 개를 쪼개고(vector<string> 할당) 실수 넷을
         *          파싱하고 있었습니다. GameFramework 의 HPBarBaseComponent 는 같은 개념을 이미 float2
         *          PROPERTY 로 들고 있었습니다. 리플렉션이 다루지 못하는 타입이라서가 아니었습니다.
         */
        PROPERTY( Category = "Collider", DisplayName = "Offset Position", Tooltip = "Collider center offset from the owner" )
        float2 _offsetPos;
        PROPERTY( Category = "Collider", DisplayName = "Offset Scale", Tooltip = "Collider box size" )
        float2 _offsetScale;
        /** @brief 등록 시점에 받은 물리 월드입니다. 소유자를 거슬러 올라가 매니저를 찾지 않습니다. */
        PhysicsWorld* _pPhysics;
        SlotHandle    _physicsBody;
        PROPERTY( Category = "Collider", DisplayName = "Collider Type", Tooltip = "Physics collider type index" )
        int32  _colliderType;
        uint32 _colliderIndex; ///< 매니저의 콜라이더 목록 자리(`GameObjectManager::registerCollider`). 없으면 `kNotRegistered`
        PROPERTY( Category = "Collider", DisplayName = "Continuous", Tooltip = "Sweep the box from its last physics step so a fast mover cannot pass through a thin collider" )
        bool _bContinuous;
    };
} // namespace sw
