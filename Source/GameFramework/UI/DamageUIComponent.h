#pragma once
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/SpriteInstanceBatch.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class SpriteClipAsset;

    /**
     * @class DamageUIComponent
     * @brief 소유 오브젝트 자리에 데미지 숫자를 띄웁니다. 위로 떠오르며 흐려지고, 수명이 다하면 오브젝트를 지웁니다.
     * @details 숫자는 글리프 아틀라스 클립(`_digitClipPath`, 기본 `engine/textures/ui/digits.sprite.json`)의 프레임으로 그립니다 — 프레임 0..9 가
     *          숫자, 10 이 '-' 입니다. 아틀라스와 클립은 `Scripts/generate/GenerateUiGlyphAtlas.py` 가 함께 만듭니다(배치 규칙이 한 곳에만 있습니다).
     *          자릿수마다 스프라이트 하나(`SpriteInstanceBatch`, 최대 `kMaxGlyphCount` 장)이고 쓰지 않는 자리는 숨깁니다 — 값이 바뀌어도 구조
     *          변경이 없어 틱 중에 바꿔도 됩니다. 알파는 `_alpha`(수명에 따라 1 → 0)를 색의 알파에 곱합니다. 예전에는 수명 · 알파 · 위치만 계산하고
     *          그리는 곳이 없었고, 값을 넣는 세터도 없었습니다.
     */
    REFLECT( Category = "UI", DisplayName = "Damage UI Component", Tooltip = "Floating damage number drawn with a glyph atlas" )
    class SW_GF_API DamageUIComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 그릴 수 있는 글자 수입니다. int32 의 가장 긴 글(부호 + 열 자리)입니다. */
        static constexpr uint32 kMaxGlyphCount = 11;
        /** @brief '-' 글리프의 클립 프레임 번호입니다(0..9 는 숫자 그대로). */
        static constexpr int32 kMinusGlyphFrame = 10;

        DamageUIComponent();
        virtual ~DamageUIComponent() override = default;

        /** @brief 수명 · 알파를 처음으로 두고, 글리프 클립을 읽어 자릿수 스프라이트를 만듭니다. */
        void onBeginPlay() override;
        /** @brief 스프라이트를 놓습니다. */
        void onEndPlay() override;
        /** @brief 떠오르고 흐려지며, 수명이 다하면 오브젝트를 지웁니다. 틱 뒤에 글자 자리를 잡게 합니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 값 · 색 · 크기 칸을 고치면 글자를 다시 잡습니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 소유 오브젝트가 꺼지면 글자를 숨깁니다. */
        void onOwnerActiveInHierarchyChanged() override;

        /**
         * @brief 보일 값을 정합니다(데미지 이벤트의 입력). 음수면 '-' 를 붙입니다.
         * @details 자릿수가 바뀌어도 구조 변경이 없습니다(자리는 미리 있고 숨겨 둡니다). 시작 전이면 시작할 때 그립니다.
         */
        void setDamageValue( int32 value );
        /** @brief 보일 값입니다. */
        int32 getDamageValue() const { return _damageValue; }
        /** @brief 글자 색(알파 포함)을 정합니다. 그리는 알파는 이 알파 × `getAlpha()` 입니다. */
        void setColor( const float4& color );
        /** @brief 글자 색입니다. */
        const float4& getColor() const { return _color; }
        /** @brief 지금 흐림 정도(1 → 0)입니다. */
        float32 getAlpha() const { return _alpha; }

        /**
         * @brief 값을 글리프 프레임 번호들로 바꿉니다(왼쪽부터). '-' 는 `kMinusGlyphFrame` 입니다. 글자 수를 돌려줍니다.
         * @details 그리는 쪽과 시험이 같은 규칙을 씁니다. INT32_MIN 도 자릿수 그대로 냅니다(부호를 뒤집지 않고 자리마다 절댓값을 뽑습니다).
         */
        static uint32 makeGlyphFrames( int32 value, int32 ( &outArrFrame )[kMaxGlyphCount] );

        /** @brief 글자를 그리는 스프라이트 배치입니다(시험이 항목을 읽습니다). */
        const SpriteInstanceBatch& getSpriteBatch() const { return _spriteBatch; }

    private:
        /** @brief 틱 직후(트랜스폼 적용 뒤) 게임 스레드에서 글자 자리를 잡게 합니다. 틱 밖이면 바로 잡습니다. */
        void scheduleLayout();
        /** @brief 소유 오브젝트 자리를 가운데로 글자를 늘어놓습니다. 클립이 없거나 꺼졌으면 모두 숨깁니다. */
        void layoutSprites();

        PROPERTY( Category = "Damage", DisplayName = "Damage Value", Tooltip = "Number to show", Alias = "damageValue" )
        int32 _damageValue;
        PROPERTY( Category = "Animation", DisplayName = "Life Time", Tooltip = "Seconds until the number fades out and the object is destroyed; 0 keeps it", Min = 0.0,
                  Meta = "Units=s", Alias = "lifeTime" )
        float32 _lifeTime;
        PROPERTY( Category = "Animation", DisplayName = "Current Life", Tooltip = "Seconds since the number appeared", ReadOnly, Meta = "Units=s", Alias = "currentLife" )
        float32 _currentLife;
        PROPERTY( Category = "Animation", DisplayName = "Float Speed", Tooltip = "Upward speed in world units per second", Meta = "Units=m/s", Alias = "floatSpeed" )
        float32 _floatSpeed;
        PROPERTY( Category = "Animation", DisplayName = "Alpha", Tooltip = "Current fade (1 to 0)", Min = 0.0, Max = 1.0, ReadOnly, Alias = "alpha" )
        float32 _alpha;
        PROPERTY( Category = "Style", DisplayName = "Glyph Size", Tooltip = "Width (advance) and height of one glyph in world units", Min = 0.0, Meta = "Units=m" )
        float2 _glyphSize;
        PROPERTY( Category = "Style", DisplayName = "Color", Meta = "Color", Tooltip = "Glyph color; alpha is multiplied by the fade" )
        float4 _color;
        PROPERTY( Category = "Style", DisplayName = "Glyph Clip", AssetPath, AssetType = "SpriteClip", Tooltip = "Glyph atlas clip: frames 0-9 are digits, frame 10 is minus" )
        string                            _digitClipPath;
        shared_ptr<const SpriteClipAsset> _digitClip;   ///< 읽은 글리프 클립입니다. 저장하지 않습니다
        SpriteInstanceBatch               _spriteBatch; ///< 글자마다 한 장. 저장하지 않습니다
    };
} // namespace sw
