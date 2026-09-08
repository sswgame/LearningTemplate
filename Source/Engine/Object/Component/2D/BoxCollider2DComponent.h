/**
 * @file BoxCollider2DComponent.h
 * @brief 2D Box Collider Component
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class PhysicsWorld;

    namespace generated
    {
        struct sw_BoxCollider2DComponent_Registrar;
    } // namespace generated

    REFLECT( Category = "Physics 2D", DisplayName = "Box Collider 2D", Tooltip = "2D Box collision volume" )
    class SW_API BoxCollider2DComponent : public SceneComponent
    {
        friend struct ::sw::generated::sw_BoxCollider2DComponent_Registrar;

    public:
        REFLECT_BODY();
        BoxCollider2DComponent();
        virtual ~BoxCollider2DComponent() override                             = default;
        BoxCollider2DComponent( BoxCollider2DComponent&& ) noexcept            = default;
        BoxCollider2DComponent& operator=( BoxCollider2DComponent&& ) noexcept = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onDestroy() override;
        /** @brief 물리 월드를 등록 시점에 받아 둡니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 물리 월드 참조를 놓습니다. */
        void onUnregister( GameObjectManager& manager ) override;
        void onTick( float32 deltaTime ) override;

        int32 getColliderType() const { return _colliderType; }
        void  setColliderType( int32 type ) { _colliderType = type; }

        /** @brief 소유자 위치 기준 콜라이더 중심 오프셋. */
        float2 getOffsetPosition() const { return _offsetPos; }
        void   setOffsetPosition( const float2& offset ) { _offsetPos = offset; }

        /** @brief 콜라이더 박스의 가로·세로 크기. */
        float2 getOffsetScale() const { return _offsetScale; }
        void   setOffsetScale( const float2& scale ) { _offsetScale = scale; }

        void getBounds( float2& outMin, float2& outMax ) const;
        bool intersects( const BoxCollider2DComponent* pOther ) const;
        bool intersects( const float2& point ) const;
        bool intersects( const float2& minB, const float2& maxB ) const;

    private:
        void unregisterPhysicsBody();
        void syncPhysicsBody();

        /**
         * @brief 콜라이더 오프셋/크기.
         * @details 예전엔 `string` 으로 두고 쓸 때마다 string_splitter 로 쪼개 parseFloat 했다.
         *          getBounds() 는 onTick 의 syncPhysicsBody 와 에디터의 기즈모·히트테스트가 부르므로,
         *          콜라이더 하나당 **매 프레임** 문자열 두 개를 쪼개고(vector<string> 할당) 실수 넷을
         *          파싱하고 있었다. GameFramework 의 HPBarBaseComponent 는 같은 개념을 이미 float2
         *          PROPERTY 로 들고 있다 — 리플렉션이 못 다루는 타입이라서가 아니었다.
         */
        PROPERTY( Category = "Collider", DisplayName = "Offset Position", Tooltip = "Collider center offset from the owner" )
        float2 _offsetPos;
        PROPERTY( Category = "Collider", DisplayName = "Offset Scale", Tooltip = "Collider box size" )
        float2 _offsetScale;
        /** @brief 등록 시점에 받은 물리 월드. 소유자를 거슬러 매니저를 찾지 않는다. */
        PhysicsWorld* _pPhysics;
        ObjectHandle  _physicsBody;
        float2        _cachedMin;
        float2        _cachedMax;
        PROPERTY( Category = "Collider", DisplayName = "Collider Type", Tooltip = "Physics collider type index" )
        int32 _colliderType;
    };
} // namespace sw
