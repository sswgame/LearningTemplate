#include "pch.h"

#include "Engine/Visual/Component/DecalComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    namespace
    {
        struct DecalComponentInternal
        {
            static constexpr float32 kDefaultRoughness       = 0.8f;   ///< 흙 · 아스팔트 · 칠 자국의 거칠기
            static constexpr float32 kDefaultFadeStartCosine = 0.3f;   ///< 투영 축과 약 72 도를 넘는 면(가파른 벽)부터 사라진다
            static constexpr float32 kMaxFadeStartCosine     = 0.999f; ///< 1 이면 페이드 폭이 0 이라 나눗셈이 터진다
        };
    } // namespace

    DecalComponent::DecalComponent()
        : _albedoTexture{}
        , _normalTexture{}
        , _color{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _roughness{ DecalComponentInternal::kDefaultRoughness }
        , _metallic{ 0.0f }
        , _normalStrength{ 1.0f }
        , _fadeStartCosine{ DecalComponentInternal::kDefaultFadeStartCosine }
        , _sortOrder{ 0 }
        , _channels{ DecalChannels::All }
        , _lifetimeSeconds{ 0.0f }
        , _fadeOutSeconds{ 0.0f }
        , _rainWashPerSecond{ 0.0f }
        , _ageSeconds{ 0.0f }
        , _washedRatio{ 0.0f }
    {
    }

    void DecalComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<DecalComponent>( this );
        setCanEverTick( _lifetimeSeconds > 0.0f ); // 시간 수명이 있을 때만 틱이 나이를 센다 — 비 씻김은 날씨 시스템이 넣는다
    }

    void DecalComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<DecalComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void DecalComponent::onTick( TickContext& context )
    {
        SceneComponent::onTick( context );
        if ( context.getDeltaTime() <= 0.0f || _lifetimeSeconds <= 0.0f )
            return;
        _ageSeconds += context.getDeltaTime();
        if ( _ageSeconds >= _lifetimeSeconds )
        {
            // 틱(워커) 안에서 오브젝트를 지우지 않는다 — 틱 뒤 게임 스레드에서 핸들로 다시 찾아 정리한다(그 사이 지워졌으면 부르지 않는다).
            GameObject* const        pOwner   = getOwner();
            GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
            if ( pManager != nullptr )
                pManager->deferPostTickOnComponent( *this, []( DecalComponent& decal )
                { decal.removeWhenGone(); } );
        }
    }

    void DecalComponent::setLifetime( float32 lifetimeSeconds, float32 fadeOutSeconds )
    {
        _lifetimeSeconds = lifetimeSeconds > 0.0f ? lifetimeSeconds : 0.0f;
        _fadeOutSeconds  = fadeOutSeconds > 0.0f ? fadeOutSeconds : 0.0f;
        setCanEverTick( _lifetimeSeconds > 0.0f );
    }

    void DecalComponent::applyRainWash( float32 rainStrength, float32 seconds )
    {
        if ( _rainWashPerSecond <= 0.0f || rainStrength <= 0.0f || seconds <= 0.0f )
            return;
        _washedRatio = MathUtil::saturate( _washedRatio + MathUtil::saturate( rainStrength ) * _rainWashPerSecond * seconds );
        if ( _washedRatio >= 1.0f )
            removeWhenGone();
    }

    float32 DecalComponent::computeOpacityScale() const
    {
        float32 life = 1.0f;
        if ( _lifetimeSeconds > 0.0f )
        {
            const float32 remaining = _lifetimeSeconds - _ageSeconds;
            life                    = _fadeOutSeconds > 0.0f ? MathUtil::saturate( remaining / _fadeOutSeconds ) : ( remaining > 0.0f ? 1.0f : 0.0f );
        }
        return life * ( 1.0f - _washedRatio );
    }

    void DecalComponent::removeWhenGone()
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( pOwner->getPrimarySceneComponent() == this )
            pOwner->destroy(); // 데칼 액터 — 오브젝트째(언리얼 DecalActor LifeSpan)
        else if ( GameObjectManager* const pManager = pOwner->getManager(); pManager != nullptr )
            pManager->destroyComponent( this );
    }

    void DecalComponent::writeDrawData( DecalDrawData& outData ) const
    {
        outData._world        = getWorldMatrix();
        outData._worldToLocal = outData._world.invert();
        outData._color        = _color;
        outData._color._w *= computeOpacityScale();
        outData._surface       = float4{ MathUtil::saturate( _roughness ), MathUtil::saturate( _metallic ), _normalStrength, MathUtil::clamp( _fadeStartCosine, -1.0f, DecalComponentInternal::kMaxFadeStartCosine ) };
        outData._albedoTexture = _albedoTexture;
        outData._normalTexture = _normalTexture;
        outData._sortOrder     = _sortOrder;
        // 노멀 텍스처가 없으면 노멀 칸을 덮지 않는다 — 덮으면 아래 면의 노멀이 상자 축으로 평평해진다.
        uint8 channels = static_cast<uint8>( _channels );
        if ( _normalTexture.isSet() == false )
            channels &= static_cast<uint8>( ~static_cast<uint8>( DecalChannels::Normal ) );
        outData._channels = channels;
    }
} // namespace sw
