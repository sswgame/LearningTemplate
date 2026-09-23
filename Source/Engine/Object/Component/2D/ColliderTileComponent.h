/**
 * @file ColliderTileComponent.h
 * @brief 2D 타일 콜라이더 컴포넌트입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    REFLECT()
    class SW_API ColliderTileComponent : public Component
    {
    public:
        REFLECT_BODY();
        ColliderTileComponent();
        virtual ~ColliderTileComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        int32 getTileType() const;
        void  setTileType( int32 type );

        int32 getCollisionSide() const;
        void  setCollisionSide( int32 side );

        bool isSolid() const;
        bool canPassFrom( int32 side ) const;
        bool overlapsBox( const float2& point, const float2& size ) const;

    private:
        PROPERTY()
        int32 _tileType;
        PROPERTY()
        int32 _collisionSide;
    };
} // namespace sw
