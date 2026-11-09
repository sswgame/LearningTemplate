/**
 * @file LightComponent.h
 * @brief 빛 종류들의 공통 기반입니다. 색 · 세기 · 위치 · 방향 규약 · 레지스트리 등록을 한 곳에 둡니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct GPULight;

    /**
     * @brief 빛 종류입니다(게임 코드가 GPU 칸 상수 `shaderslot::kLightType*` 를 모르고 빛을 종류별로 찾는다). 값은 셰이더의 종류 값과 같다 — `LightComponent.cpp` 가 맞는지 컴파일에서 본다.
     * @details 컴포넌트 레지스트리의 칸 번호가 이 값이다(`LightComponent::getRegistryIndex`).
     */
    enum class LightKind : uint8
    {
        Directional = 0,
        Point,
        Spot,
        Point2D,
        Global2D,
        Freeform2D,
    };

    /**
     * @class LightComponent
     * @brief 빛 하나의 공통 부분입니다. 종류(방향광 · 점광 · 스포트)는 파생이 정하고, 레지스트리와 수집이 그 종류로 나눕니다.
     * @details 언리얼 `ULightComponent` 의 자리입니다. 색 · 세기의 세터 · 등록과 해제 · 위치 · 방향 함수는 여기 하나입니다. 새 종류는 파생
     *          하나(GPU 원소의 자기 칸은 `writeGPULightKindFields` 재정의) + 셰이더 분기 하나입니다. 수집(`collectSceneLights`)은 종류를 모릅니다.
     *
     *          **방향 규약.** 파생의 기본 방향은 **로컬** 방향이고, 월드 회전(부모 포함)이 그것을 돌립니다. 회전이 없는 루트 빛은 기본
     *          방향을 그대로 씁니다. 주의: "로컬 회전이 0 이면 기본 방향, 아니면 전방(+Z)" 처럼 갈라 두면 부모의 회전을 무시하고, 회전이
     *          임계값을 넘는 순간 방향이 기본 방향에서 +Z 로 튄다.
     *
     *          `REFLECT( Abstract )` 라 만들 수 없고 컴포넌트 팩토리도 없습니다. 색 · 세기 PROPERTY(`_color` · `_intensity`)는 저장된 씬 ·
     *          바이너리 · 기본값이 이름으로 묶입니다.
     */
    REFLECT( Abstract, Category = "Rendering 3D", DisplayName = "Light", Tooltip = "Common part of every light kind", Meta = "Icon=LightPoint, IconColor=Light, Billboard" )
    class SW_API LightComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 소멸자입니다. */
        virtual ~LightComponent() override = default;

        /** @brief 빛 종류입니다(`shaderslot::kLightType*`). 레지스트리와 수집이 이것으로 나눕니다. */
        uint32 getLightType() const { return _lightType; }
        /** @brief 빛 종류입니다(게임 코드용 — GPU 칸 상수 대신). */
        LightKind getLightKind() const { return static_cast<LightKind>( _lightType ); }
        /** @brief 컴포넌트 레지스트리에서 @p kind 의 빛이 든 칸 번호입니다(`ComponentRegistry::getAll<LightComponent>( 칸 )`). */
        static constexpr uint32 getRegistryIndex( LightKind kind ) { return static_cast<uint32>( kind ); }

        /** @brief 빛 색입니다. */
        const float3& getColor() const { return _color; }
        /** @brief 빛 색을 설정합니다. */
        void setColor( const float3& color );

        /** @brief 빛 세기입니다. */
        float32 getIntensity() const { return _intensity; }
        /** @brief 빛 세기를 설정합니다. 음수는 0 으로 자릅니다. */
        void setIntensity( float32 intensity );

        /**
         * @brief 3D 빛(방향광 · 스폿)이 나아갈 방향(부모 공간)으로 로컬 회전을 정합니다. 롤은 0 입니다.
         * @details 3D 빛은 로컬 앞(+Z)을 비춘다 — 카메라와 같은 규약이라 회전이 곧 방향이다(유니티 `transform.forward = dir`, 언리얼 라이트의 앞 축).
         *          예전 규약(기울어진 기본 방향에 회전을 곱함)은 피치 1.5 를 준 해가 위를 향하는 함정이었다.
         */
        void setLocalLightDirection( const float3& localDirection );

        /** @brief 이 빛의 월드 위치입니다. */
        float3 getLightPosition() const;

        /**
         * @brief 이 빛 하나를 GPU 원소로 씁니다. 공통 칸(색 · 세기 · 종류)은 여기서, 종류마다 다른 칸은 파생의 `writeGPULightKindFields` 가 채웁니다.
         * @details 그림자 플래그(`_params.x`)는 쓰지 않습니다 — 그림자 맵의 빛은 씬이 하나 고르므로(`DirectionalLightComponent::findShadowCasting`)
         *          모으는 쪽이 켭니다. @p outLight 의 다른 칸은 모두 덮어씁니다.
         */
        void writeGPULight( GPULight& outLight ) const;
        /**
         * @brief 이 빛 원소 **바로 뒤**에 붙는 원소를 씁니다(자유 모양 2D 빛의 변). 기본은 없습니다.
         * @details 셰이더는 빛 원소의 개수 칸(자유 모양은 `_extra.z`)만큼 뒤 원소를 읽는다 — 모으는 쪽(`collectSceneLights`)이 `writeGPULight` 바로 다음에 부른다.
         */
        virtual void appendGPUTrailingElements( vector<GPULight>& /*inoutListLight*/ ) const {}
        /**
         * @brief 빛 텍스처(쿠키)의 텍스처 경로입니다. 비면 쿠키가 없습니다. 2D 점 · 스폿 빛만 씁니다(`PointLight2DComponent`).
         * @details 렌더러가 프레임마다 모아(`collectSceneLights` 의 쿠키 목록) 쿠키 아틀라스(`Light2DCookieAtlas`)의 칸에 그리고, 빛 원소의 `_extra.w` 에 칸 번호를 적는다.
         */
        virtual hashed_string getCookieTexturePath() const { return {}; }
        /**
         * @brief 렌즈 플레어 파일(`LensFlareAsset`, `*.lensflare.xml`)의 경로입니다. 비면 플레어가 없습니다. 3D 빛(방향광 · 점광 · 스폿)만 그린다.
         * @details 렌더 수집기가 빛마다 읽어 패킷에 싣고(`RenderPacketSceneCollector::collectLensFlares`) 렌즈 플레어 패스가 그린다. 못 읽으면 오류를 한 번 남기고 그 빛은 플레어가 없다.
         */
        const string& getLensFlarePath() const { return _lensFlarePath; }

        /** @brief 씬에 붙을 때 빛 레지스트리의 자기 종류 칸에 자기를 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 씬에서 떨어질 때 등록을 해제합니다. */
        void onUnregister( GameObjectManager& manager ) override;

    protected:
        /**
         * @brief 종류와 그 종류의 기본 색 · 세기로 만듭니다. 파생만 부릅니다.
         * @param lightType        `shaderslot::kLightType*`
         * @param defaultColor     기본 색(파생의 `XxxInternal` 상수)
         * @param defaultIntensity 기본 세기
         */
        LightComponent( uint32 lightType, const float3& defaultColor, float32 defaultIntensity );

        /**
         * @brief 로컬 기본 방향을 월드 회전(부모 포함)으로 돌린 빛 방향(정규화)입니다.
         * @details 스케일이 0 이라 방향이 사라지면 기본 방향을 그대로 돌려줍니다. 회전에 대해 연속이고 부모를 따릅니다.
         */
        float3 computeLightDirection( const float3& defaultLocalDirection ) const;

        /**
         * @brief 종류마다 다른 GPU 칸(위치 · 반경 · 방향 · 원뿔)을 씁니다. `writeGPULight` 가 공통 칸을 채운 뒤 부릅니다.
         * @details 방향은 `_directionType` 의 xyz 만 씁니다 — w 는 종류이고 이미 채워져 있습니다.
         */
        virtual void writeGPULightKindFields( GPULight& outLight ) const = 0;

    private:
        PROPERTY( Category = "Light", DisplayName = "Color", Meta = "Color", Tooltip = "Light color" )
        float3 _color;
        PROPERTY( Category = "Light", DisplayName = "Intensity", Min = 0.0, Tooltip = "Light intensity" )
        float32 _intensity;
        PROPERTY( Category = "Light", DisplayName = "Lens Flare", Tooltip = "Lens flare data (.lensflare.xml) drawn for this 3D light - empty draws none" )
        string _lensFlarePath;
        uint32 _lightType; ///< `shaderslot::kLightType*`. 만들 때 파생이 정하고 바뀌지 않습니다
    };
} // namespace sw
