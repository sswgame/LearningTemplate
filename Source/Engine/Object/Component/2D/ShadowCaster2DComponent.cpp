#include "pch.h"

#include "Engine/Object/Component/2D/ShadowCaster2DComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GpuLight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/2D/TileMapRendererComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/LightRegistry.h"

namespace sw
{
    ShadowCaster2DComponent::ShadowCaster2DComponent()
        : _size{ 1.0f, 1.0f }
        , _bUseTileMap{ true }
    {
    }

    void ShadowCaster2DComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getLightRegistry().addShadowCaster( this );
    }

    void ShadowCaster2DComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getLightRegistry().removeShadowCaster( this );
        Component::onUnregister( manager );
    }

    void ShadowCaster2DComponent::computeWorldSegments( vector<float4>& outListSegment, vector<float2>& outListOutward ) const
    {
        outListSegment.clear();
        outListOutward.clear();
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( _bUseTileMap )
        {
            const TileMapRendererComponent* pTiles = pOwner->getComponent<TileMapRendererComponent>();
            if ( pTiles != nullptr )
            {
                pTiles->computeWorldOutline( outListSegment, outListOutward );
                return;
            }
        }
        // 상자: 네 모서리를 월드로 옮기고, 변마다 바깥쪽은 가운데에서 멀어지는 쪽이다(거울 변환이어도 맞다).
        const SceneComponent* pRoot        = pOwner->getPrimarySceneComponent();
        const float4x4        world        = ( pRoot != nullptr ) ? pRoot->getWorldMatrix() : float4x4::Identity;
        const float32         halfX        = _size._x * 0.5f;
        const float32         halfY        = _size._y * 0.5f;
        const float3          arrCorner[4] = { float3::transform( float3{ -halfX, -halfY, 0.0f }, world ), float3::transform( float3{ halfX, -halfY, 0.0f }, world ),
                                               float3::transform( float3{ halfX, halfY, 0.0f }, world ), float3::transform( float3{ -halfX, halfY, 0.0f }, world ) };
        const float3          center       = float3::transform( float3{ 0.0f, 0.0f, 0.0f }, world );
        for ( uint32 edgeIndex = 0; edgeIndex < 4; ++edgeIndex )
        {
            const float3& start = arrCorner[edgeIndex];
            const float3& end   = arrCorner[( edgeIndex + 1 ) % 4];
            float2        normal{ end._y - start._y, start._x - end._x };
            const float2  middle{ ( start._x + end._x ) * 0.5f - center._x, ( start._y + end._y ) * 0.5f - center._y };
            if ( normal._x * middle._x + normal._y * middle._y < 0.0f )
                normal = float2{ -normal._x, -normal._y };
            if ( normal.getLengthSquared() > MathUtil::Epsilon )
                normal.normalize();
            outListSegment.push_back( float4{ start._x, start._y, end._x, end._y } );
            outListOutward.push_back( normal );
        }
    }

    void ShadowCaster2DComponent::appendGpuShadowSegments( vector<GpuLight>& inoutListLight ) const
    {
        vector<float4> listSegment;
        vector<float2> listEdgeNormal;
        computeWorldSegments( listSegment, listEdgeNormal );
        for ( size_t segmentIndex = 0; segmentIndex < listSegment.size(); ++segmentIndex )
        {
            GpuLight record{};
            record._positionRadius = listSegment[segmentIndex];
            record._colorIntensity = float4{ listEdgeNormal[segmentIndex]._x, listEdgeNormal[segmentIndex]._y, 0.0f, 0.0f };
            record._directionType  = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypeShadow2D ) };
            inoutListLight.push_back( record );
        }
    }
} // namespace sw
