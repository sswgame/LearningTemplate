#include "pch.h"

#include "Engine/Visual/Component/DecalComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    namespace
    {
        struct DecalComponentInternal
        {
            static constexpr float32 kDefaultRoughness      = 0.8f;   ///< 흙 · 아스팔트 · 칠 자국의 거칠기
            static constexpr float32 kDefaultAngleFadeStart = 0.3f;   ///< 투영 축과 약 72 도를 넘는 면(가파른 벽)부터 사라진다
            static constexpr float32 kMaxAngleFadeStart     = 0.999f; ///< 1 이면 페이드 폭이 0 이라 나눗셈이 터진다
        };
    } // namespace

    DecalComponent::DecalComponent()
        : _albedoTexture{}
        , _normalTexture{}
        , _color{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _roughness{ DecalComponentInternal::kDefaultRoughness }
        , _metallic{ 0.0f }
        , _normalStrength{ 1.0f }
        , _angleFadeStart{ DecalComponentInternal::kDefaultAngleFadeStart }
        , _sortOrder{ 0 }
        , _channels{ DecalChannels::All }
    {
    }

    void DecalComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<DecalComponent>( this );
    }

    void DecalComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<DecalComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void DecalComponent::writeDrawData( DecalDrawData& outData ) const
    {
        outData._world         = getWorldMatrix();
        outData._worldToLocal  = outData._world.invert();
        outData._color         = _color;
        outData._surface       = float4{ MathUtil::saturate( _roughness ), MathUtil::saturate( _metallic ), _normalStrength, MathUtil::clamp( _angleFadeStart, -1.0f, DecalComponentInternal::kMaxAngleFadeStart ) };
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
