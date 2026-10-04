/**
 * @file ShadowCaster2DComponent.h
 * @brief 2D 그림자 가림막 — 상자 모양 또는 같은 오브젝트의 타일맵 외곽선이 2D 빛을 가립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct GpuLight;

    /**
     * @class ShadowCaster2DComponent
     * @brief 그림자를 켠 2D 빛(`PointLight2DComponent::_bCastShadows`)을 가리는 토막들입니다. 유니티 Shadow Caster 2D(+ Composite) · Godot LightOccluder2D 의 자리입니다.
     * @details 모양: 같은 오브젝트에 `TileMapRendererComponent` 가 있으면 그 외곽선(단단한 칸의 바깥 변, 바깥쪽 방향 포함), 없으면 오브젝트 원점의 `_size`
     *          상자(월드 트랜스폼을 따름)입니다. 토막은 프레임마다 빛 목록 뒤에 그림자 원소(`shaderslot::kLightTypeShadow2D`)로 붙고, 셰이더는 빛을 등진
     *          토막만 가림막으로 셉니다 — 가림막 안쪽은 자기 그림자를 받지 않습니다(Self Shadows 꺼짐).
     *          빛 등록부(`LightRegistry::addShadowCaster`)에 붙을 때 등록합니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Shadow Caster 2D", Tooltip = "Blocks 2D lights with a box or the tile map outline" )
    class SW_API ShadowCaster2DComponent : public Component
    {
    public:
        REFLECT_BODY();
        ShadowCaster2DComponent();
        virtual ~ShadowCaster2DComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 월드의 가림막 토막 (x0, y0, x1, y1) 과 바깥쪽 방향을 모읍니다. */
        void computeWorldSegments( vector<float4>& outListSegment, vector<float2>& outListOutward ) const;
        /** @brief 토막마다 그림자 원소 하나를 @p inoutListLight 뒤에 붙입니다(빛 수집이 부릅니다). */
        void appendGpuShadowSegments( vector<GpuLight>& inoutListLight ) const;

        void          setSize( const float2& size ) { _size = size; }
        const float2& getSize() const { return _size; }

    private:
        PROPERTY( Category = "Shadow", DisplayName = "Box Size", Min = 0.0, Tooltip = "Width and height of the box shape centered on the object", Meta = "Units=m" )
        float2 _size;
        PROPERTY( Category = "Shadow", DisplayName = "Use Tile Map", Tooltip = "Use the outline of the tile map on this object instead of the box" )
        bool _bUseTileMap;
    };
} // namespace sw
