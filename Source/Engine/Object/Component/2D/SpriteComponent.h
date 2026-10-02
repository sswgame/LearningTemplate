/**
 * @file SpriteComponent.h
 * @brief 2D 오브젝트의 스프라이트 메시를 그리는 컴포넌트입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class SpriteComponent
     * @brief 텍스처를 입힌 사각형을 그립니다(2D). 메시는 사각형, 머티리얼은 스프라이트 머티리얼(`sprite2d.material`)이 기본입니다.
     * @details 텍스처는 그 머티리얼의 **인스턴스**가 덮어쓰고(`albedoMap`), 같은 (머티리얼, 텍스처)의 스프라이트는 인스턴스 하나를 나눠 씁니다 —
     *          배치 키가 인스턴스라 그래야 한 드로우로 묶입니다. 유니티 `SpriteRenderer`(스프라이트 기본 머티리얼 · 텍스처는 프로퍼티 블록) ·
     *          언리얼 Paper2D(`UPaperSpriteComponent`)의 자리입니다. 예전에는 메시 컴포넌트의 기본을 그대로 따라 씬 기본 머티리얼의 **단위 큐브**로
     *          그려졌고, 텍스처 · 메시 · 머티리얼 이름 칸은 저장만 되고 읽는 곳이 없었습니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Sprite Component", Tooltip = "2D Sprite rendering component" )
    class SW_API SpriteComponent : public MeshComponent
    {
    public:
        REFLECT_BODY();
        SpriteComponent();
        virtual ~SpriteComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        /** @brief 메시 · 머티리얼을 풀고 텍스처 인스턴스를 맞춥니다. */
        void resolveRenderAssets() override;
        /** @brief 텍스처 · 머티리얼 칸을 고치면 텍스처 인스턴스를 다시 맞춥니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 단위 사각형(한 변 1)의 반대각선에 월드 X · Y 스케일 중 큰 쪽을 곱한 구입니다. */
        bool getWorldBounds( float3& outCenter, float32& outRadius ) const override;

        /** @brief 스프라이트 텍스처 경로입니다. 비어 있으면 흰 사각형(머티리얼의 색)입니다. */
        const string& getTextureName() const { return _textureName; }
        /** @brief 텍스처를 바꾸고 인스턴스를 다시 맞춥니다. */
        void setTextureName( string_view texture );

        const string& getSpriteName() const { return _spriteName; }
        void          setSpriteName( const string& sprite ) { _spriteName = sprite; }

    protected:
        /** @brief 사각형입니다. */
        string_view getDefaultMeshId() const override { return "Quad"; }
        /** @brief 스프라이트 머티리얼(투명 · `sprite2d.hlsl`)입니다. */
        hashed_string getDefaultMaterialPath() const override;

    private:
        /**
         * @brief 텍스처 인스턴스를 지금 머티리얼 · 텍스처에 맞춥니다. 같은 (머티리얼, 텍스처)는 인스턴스 하나를 나눠 씁니다.
         * @details 이 컴포넌트가 건 인스턴스만 바꾸거나 뗍니다(`_appliedTexture`) — 텍스처가 비어 있으면 코드가 건 인스턴스는 그대로 둡니다.
         */
        void refreshTextureInstance();

        PROPERTY( Category = "Rendering", DisplayName = "Texture", AssetPath, AssetType = "Texture", Tooltip = "Texture asset name", Alias = "Texture" )
        string        _textureName;
        hashed_string _appliedTexture; ///< 이 컴포넌트가 건 텍스처 인스턴스의 텍스처. 비어 있으면 건 것이 없다
        PROPERTY( Category = "Rendering", DisplayName = "Sprite Clip", Tooltip = "Sprite clip identifier", Alias = "SpriteName" )
        string _spriteName;
    };
} // namespace sw
