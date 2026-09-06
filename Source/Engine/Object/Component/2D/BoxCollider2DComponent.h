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

        string getOffsetPos() const { return _offsetPos; }
        void   setOffsetPos( const string& pos ) { _offsetPos = pos; }

        string getOffsetScale() const { return _offsetScale; }
        void   setOffsetScale( const string& scale ) { _offsetScale = scale; }

        float2 getOffsetPosition() const;
        float2 getOffsetScaleVec() const;

        void getBounds( float2& outMin, float2& outMax ) const;
        bool intersects( const BoxCollider2DComponent* pOther ) const;
        bool intersects( const float2& point ) const;
        bool intersects( const float2& minB, const float2& maxB ) const;

    private:
        void unregisterPhysicsBody();
        void syncPhysicsBody();

        PROPERTY( Category = "Collider", DisplayName = "Offset Position", Tooltip = "2D Offset position as string format" )
        string _offsetPos;
        PROPERTY( Category = "Collider", DisplayName = "Offset Scale", Tooltip = "2D Offset scale as string format" )
        string _offsetScale;
        /** @brief 등록 시점에 받은 물리 월드. 소유자를 거슬러 매니저를 찾지 않는다. */
        PhysicsWorld* _pPhysics;
        ObjectHandle  _physicsBody;
        float2        _cachedMin;
        float2        _cachedMax;
        PROPERTY( Category = "Collider", DisplayName = "Collider Type", Tooltip = "Physics collider type index" )
        int32 _colliderType;
    };
} // namespace sw
