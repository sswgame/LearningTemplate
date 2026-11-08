#include "pch.h"

#include "Engine/Visual/Component/PostProcessVolumeComponent.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    namespace
    {
        struct PostProcessVolumeComponentInternal
        {
            /// @brief 기본 상자 반 크기 · 구 반지름(m) — 방 하나.
            static constexpr float32 kDefaultHalfExtent{ 5.0f };
            /// @brief 기본 블렌드 반경(m) — 언리얼 기본 100 cm 와 같다.
            static constexpr float32 kDefaultBlendRadius{ 1.0f };

            /** @brief 카메라 자리에 닿는 볼륨 하나입니다(섞는 차례를 정하려고 모은다). */
            struct Contribution
            {
                const PostProcessVolumeComponent* _pVolume;
                float32                           _weight;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    PostProcessVolumeComponent::PostProcessVolumeComponent()
        : _values{}
        , _overrides{}
        , _halfExtents{ PostProcessVolumeComponentInternal::kDefaultHalfExtent, PostProcessVolumeComponentInternal::kDefaultHalfExtent, PostProcessVolumeComponentInternal::kDefaultHalfExtent }
        , _radius{ PostProcessVolumeComponentInternal::kDefaultHalfExtent }
        , _blendRadius{ PostProcessVolumeComponentInternal::kDefaultBlendRadius }
        , _blendWeight{ 1.0f }
        , _priority{ 0.0f }
        , _shape{ PostProcessVolumeShape::Box }
    {
    }

    void PostProcessVolumeComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<PostProcessVolumeComponent>( this );
    }

    void PostProcessVolumeComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<PostProcessVolumeComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    float32 PostProcessVolumeComponent::computeWeight( const float3& position ) const
    {
        const float32 blendWeight = MathUtil::saturate( _blendWeight );
        // 모양 밖으로 나간 거리(안이면 0 이하). 상자는 축마다 밖으로 나간 양의 길이다(모서리 밖은 모서리까지의 거리).
        float32 outside = 0.0f;
        switch ( _shape )
        {
            case PostProcessVolumeShape::Unbound:
            {
                return blendWeight;
            }
            case PostProcessVolumeShape::Box:
            {
                const float3 offset = position - getWorldPosition();
                const float3 excess{ MathUtil::max( MathUtil::abs( offset._x ) - _halfExtents._x, 0.0f ), MathUtil::max( MathUtil::abs( offset._y ) - _halfExtents._y, 0.0f ),
                                     MathUtil::max( MathUtil::abs( offset._z ) - _halfExtents._z, 0.0f ) };
                outside = excess.getLength();
                break;
            }
            case PostProcessVolumeShape::Sphere:
            {
                outside = ( position - getWorldPosition() ).getLength() - _radius;
                break;
            }
        }
        if ( outside <= 0.0f )
            return blendWeight;
        if ( outside >= _blendRadius )
            return 0.0f;
        return blendWeight * ( 1.0f - outside / _blendRadius );
    }

    void PostProcessVolumeComponent::setSettings( const PostProcessValues& values, const PostProcessOverrides& overrides )
    {
        _values    = values;
        _overrides = overrides;
    }

    void PostProcessVolumeComponent::setShape( PostProcessVolumeShape shape, const float3& halfExtents, float32 radius, float32 blendRadius, float32 blendWeight, float32 priority )
    {
        _shape       = shape;
        _halfExtents = halfExtents;
        _radius      = MathUtil::max( radius, 0.0f );
        _blendRadius = MathUtil::max( blendRadius, 0.0f );
        _blendWeight = MathUtil::saturate( blendWeight );
        _priority    = priority;
    }

    PostProcessValues PostProcessVolumeComponent::resolveAt( const ComponentRegistry& registry, const float3& position, const PostProcessValues& base )
    {
        using Contribution                                                = PostProcessVolumeComponentInternal::Contribution;
        PostProcessValues                                         values  = base;
        const ComponentRegistry::View<PostProcessVolumeComponent> volumes = registry.getAll<PostProcessVolumeComponent>();
        if ( volumes.empty() )
            return values;
        vector<Contribution> listContribution;
        listContribution.reserve( volumes.size() );
        for ( const PostProcessVolumeComponent* pVolume : volumes )
        {
            if ( pVolume == nullptr || pVolume->isActive() == false )
                continue;
            const float32 weight = pVolume->computeWeight( position );
            if ( weight > 0.0f )
                listContribution.push_back( Contribution{ pVolume, weight } );
        }
        // 우선순위가 낮은 것부터 섞어 높은 것이 마지막에 덮는다. 같은 우선순위는 등록 순서다(안정 정렬).
        std::stable_sort( listContribution.begin(), listContribution.end(), []( const Contribution& left, const Contribution& right )
        { return left._pVolume->_priority < right._pVolume->_priority; } );
        for ( const Contribution& contribution : listContribution )
        {
            PostProcessValues::blendOverrides( values, contribution._pVolume->_values, contribution._pVolume->_overrides, contribution._weight );
        }
        return values;
    }
} // namespace sw
