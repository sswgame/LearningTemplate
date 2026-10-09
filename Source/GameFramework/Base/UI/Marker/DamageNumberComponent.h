#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class TextWidget;
    class WidgetComponent;

    /**
     * @class DamageNumberComponent
     * @brief 소유 오브젝트 자리에 데미지 숫자를 띄웁니다. 위로 떠오르며 흐려지고, 수명이 다하면 오브젝트를 지웁니다.
     * @details 숫자는 같은 오브젝트의 `WidgetComponent`(Screen — 화면 마커)에 넣는 글 위젯 하나입니다 — 스타일 클래스 `damage`(테마 시트가 크기 · 굵기 ·
     *          외곽선을 정한다), 색은 `_color`, 불투명도는 `_alpha`(수명에 따라 1 → 0). 화면 공간이라 거리와 상관없이 같은 크기로 선명합니다.
     *          값 · 색 · 알파는 틱 **뒤에** 넣습니다(위젯은 게임 스레드만 고친다). 피해를 내는 곳은 `spawnNumber` 하나를 씁니다(마커 · 숫자 컴포넌트를 함께 붙인다).
     */
    REFLECT( Category = "UI", DisplayName = "Damage Number Component", Tooltip = "Floating damage number drawn by the object's screen-marker WidgetComponent" )
    class SW_GF_API DamageNumberComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief `spawnNumber` 로 띄운 숫자의 수명(초) · 떠오르는 빠르기(m/s)입니다. */
        static constexpr float32 kSpawnedLifeTime   = 0.9f;
        static constexpr float32 kSpawnedFloatSpeed = 1.2f;
        /** @brief 숫자 글 위젯의 스타일 클래스입니다(엔진 기본 테마 `TextWidget.damage`). */
        static constexpr const utf8* kStyleClass = "damage";

        DamageNumberComponent();
        virtual ~DamageNumberComponent() override = default;

        /**
         * @brief 글 위젯을 같은 오브젝트의 `WidgetComponent` 에 넣고, 알파를 흐른 수명에서 구합니다.
         * @details 흐른 수명(`_currentLife`)은 되돌리지 않습니다 — 떠 있는 동안 상태를 다시 읽은 숫자(플레이 중 되돌리기 · 핫 리로드)는 남은 수명을
         *          이어 갑니다. 새로 만든 숫자는 0 에서 시작합니다.
         */
        void onBeginPlay() override;
        /** @brief 떠오르고 흐려지며, 수명이 다하면 오브젝트를 지웁니다. 틱 뒤에 글 위젯 값을 넣게 합니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 값 · 색 칸을 고치면 글 위젯을 다시 채웁니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 소유 오브젝트가 꺼지면 숫자를 숨깁니다. */
        void onOwnerActiveInHierarchyChanged() override;

        /** @brief 보일 값을 정합니다(데미지 이벤트의 입력). 음수면 '-' 가 붙습니다. 시작 전이면 시작할 때 그립니다. */
        void setDamageValue( int32 value );
        /** @brief 보일 값입니다. */
        int32 getDamageValue() const { return _damageValue; }
        /** @brief 글자 색(알파 포함)을 정합니다. 그리는 불투명도는 이 알파 × `getAlpha()` 입니다. */
        void setColor( const float4& color );
        /** @brief 글자 색입니다. */
        const float4& getColor() const { return _color; }
        /** @brief 지금 흐림 정도(1 → 0)입니다. */
        float32 getAlpha() const { return _alpha; }
        /** @brief 사라지기까지의 초입니다. 0 이면 지우지 않고 그대로 둡니다(씬에 놓은 견본 숫자). */
        void    setLifeTime( float32 lifeTime ) { _lifeTime = lifeTime < 0.0f ? 0.0f : lifeTime; }
        float32 getLifeTime() const { return _lifeTime; }
        /** @brief 위로 떠오르는 빠르기(m/s)입니다. 수명이 0 이면 움직이지 않습니다. */
        void    setFloatSpeed( float32 floatSpeed ) { _floatSpeed = floatSpeed; }
        float32 getFloatSpeed() const { return _floatSpeed; }

        /**
         * @brief 피해 숫자 하나를 @p position 에 띄웁니다 — 떠오르며 흐려지다 `kSpawnedLifeTime` 초 뒤 지워집니다.
         * @details 피해를 내는 모든 곳(`UnitStatsComponent` · `AbilitySystemComponent`)이 이것 하나를 씁니다. 오브젝트에 씬 컴포넌트 · 화면 마커
         *          (`WidgetComponent` — 가운데 피벗) · 이 컴포넌트를 붙입니다. 틱 중이면 만들기를 틱 직후로 미룹니다(틱 안에서는 컴포넌트를 붙일 수 없다).
         *          직접 만들지 말 것 — 수명 기본값 0 은 "지우지 않음" 이라 맞을 때마다 숫자 오브젝트가 남습니다.
         */
        static void spawnNumber( GameObjectManager& manager, const float3& position, int32 value );

        /** @brief 같은 오브젝트의 화면 마커 컴포넌트입니다(없으면 nullptr). */
        WidgetComponent* findWidgetComponent() const;
        /** @brief 숫자 글 위젯입니다(아직 넣지 않았으면 nullptr — 시험이 글 · 색 · 불투명도를 읽는다). */
        const TextWidget* findTextWidget() const;

    private:
        /** @brief 흐른 수명에 맞는 흐림(1 → 0)입니다. 수명이 0 이면(사라지지 않는 숫자) 1 입니다. */
        float32 computeAlpha() const;
        /** @brief 틱 직후(트랜스폼 적용 뒤) 게임 스레드에서 글 위젯 값을 넣게 합니다. 틱 밖이면 바로 넣습니다. */
        void scheduleRefresh();
        /** @brief 글 위젯에 값 · 색 · 불투명도를 넣고, 꺼졌으면 마커를 숨깁니다. */
        void refreshWidget();

        PROPERTY( Category = "Damage", DisplayName = "Damage Value", Tooltip = "Number to show" )
        int32 _damageValue;
        PROPERTY( Category = "Animation", DisplayName = "Life Time", Tooltip = "Seconds until the number fades out and the object is destroyed; 0 keeps it", Min = 0.0,
                  Units = s )
        float32 _lifeTime;
        PROPERTY( Category = "Animation", DisplayName = "Current Life", Tooltip = "Seconds since the number appeared", ReadOnly, Units = s )
        float32 _currentLife;
        PROPERTY( Category = "Animation", DisplayName = "Float Speed", Tooltip = "Upward speed in world units per second", Units = "m/s" )
        float32 _floatSpeed;
        PROPERTY( Category = "Animation", DisplayName = "Alpha", Tooltip = "Current fade (1 to 0)", Min = 0.0, Max = 1.0, ReadOnly )
        float32 _alpha;
        PROPERTY( Category = "Style", DisplayName = "Color", Meta = "Color", Tooltip = "Text color; alpha is multiplied by the fade" )
        float4 _color;
        bool   _bWarnedNoWidgetComponent; ///< 같은 오브젝트에 WidgetComponent 가 없다고 한 번 알렸다
    };
} // namespace sw
