/**
 * @file SpriteComponent.h
 * @brief 2D 오브젝트의 스프라이트 메시를 그리는 컴포넌트입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SpriteClipAsset;

    /**
     * @class SpriteComponent
     * @brief 텍스처를 입힌 사각형을 그립니다(2D). 메시는 사각형, 머티리얼은 스프라이트 머티리얼(`sprite2d.material`)이 기본입니다.
     * @details 텍스처는 그 머티리얼의 **인스턴스**가 덮어쓰고(`albedoMap`), 같은 (머티리얼, 텍스처)의 스프라이트는 인스턴스 하나를 나눠 씁니다 —
     *          배치 키가 인스턴스라 그래야 한 드로우로 묶입니다. 유니티 `SpriteRenderer`(스프라이트 기본 머티리얼 · 텍스처는 프로퍼티 블록) ·
     *          언리얼 Paper2D(`UPaperSpriteComponent`)의 자리입니다. 예전에는 메시 컴포넌트의 기본을 그대로 따라 씬 기본 머티리얼의 **단위 큐브**로
     *          그려졌고, 텍스처 · 메시 · 머티리얼 이름 칸은 저장만 되고 읽는 곳이 없었습니다.
     *
     *          **무엇을 보이나.** 아틀라스의 한 프레임(UV 사각형)과 색입니다. 둘 다 머티리얼 인스턴스가 아니라 GPU 인스턴스에 실립니다
     *          (`MeshComponent::getSpriteInstanceData` → `GpuInstance::_sprite`) — 프레임이 넘어가거나 알파가 줄어도 배치는 그대로입니다.
     *          프레임은 클립(`_clipPath`, `.sprite.json`)이 있으면 그 클립의 `_clipFrame` 번째 프레임이고, 없으면 `_uvRect` 입니다.
     *          텍스처 칸이 비어 있으면 클립의 아틀라스를 씁니다. 애니메이터(`SpriteAnimatorComponent`)는 `setClipFrame` 으로 프레임만 넘깁니다.
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
        /** @brief 메시 · 머티리얼을 풀고 클립 · 텍스처 인스턴스 · 프레임을 맞춥니다. */
        void resolveRenderAssets() override;
        /**
         * @brief 텍스처 · 클립 · 프레임 · 색 칸을 고치면 다시 맞춥니다.
         * @details 값이 같아도 다시 맞춥니다 — 에디터 핫 리로드는 클립을 제자리로 다시 읽은 뒤 `_clipPath` 로 이것을 부릅니다.
         */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 단위 사각형(한 변 1)의 반대각선에 월드 X · Y 스케일 중 큰 쪽을 곱한 구입니다. */
        bool getWorldBounds( float3& outCenter, float32& outRadius ) const override;

        /** @brief 스프라이트 텍스처 경로입니다. 비어 있으면 클립의 아틀라스, 그것도 없으면 흰 사각형(머티리얼의 색)입니다. */
        const string& getTextureName() const { return _textureName; }
        /** @brief 텍스처를 바꾸고 인스턴스를 다시 맞춥니다. */
        void setTextureName( string_view texture );

        /** @brief 스프라이트 클립(`.sprite.json`) 경로입니다. 비어 있으면 클립 없이 `_uvRect` 를 보입니다. */
        const string& getClipPath() const { return _clipPath; }
        /** @brief 클립을 바꾸고 읽습니다(같은 클립은 스프라이트끼리 나눠 갖습니다 — `SpriteClipCache::acquire`). */
        void setClipPath( string_view path );
        /** @brief 읽은 클립입니다. 경로가 비었거나 읽지 못했으면 nullptr 입니다. */
        const SpriteClipAsset* getClip() const { return _clip.get(); }

        /** @brief 보일 클립 프레임 번호입니다. */
        int32 getClipFrame() const { return _clipFrame; }
        /** @brief 보일 클립 프레임을 정합니다(0 아래는 0). 클립보다 크면 마지막 프레임을 보입니다. */
        void setClipFrame( int32 frame );

        /** @brief 클립이 없을 때 보일 UV 사각형 (u, v, 폭, 높이) 입니다. 기본은 텍스처 전체입니다. */
        const float4& getUvRect() const { return _uvRect; }
        /** @brief 클립이 없을 때 보일 UV 사각형을 정합니다. 폭이 음수면 좌우가 뒤집힙니다. */
        void setUvRect( const float4& uvRect );

        /** @brief 색(곱)입니다. 알파는 불투명도입니다. 기본은 흰색 불투명입니다. */
        const float4& getTint() const { return _tint; }
        /** @brief 색을 정합니다(머티리얼 색 · 텍스처에 곱합니다). 페이드 · 피격 색은 여기입니다 — 머티리얼 인스턴스를 만들지 않습니다. */
        void setTint( const float4& tint );

        /** @brief 지금 보이는 UV 사각형입니다 — 클립이 있으면 그 프레임의 것, 없으면 `_uvRect` 입니다. */
        float4 getDisplayedUvRect() const;

    protected:
        /** @brief 양면 스프라이트 사각형입니다(`MeshUtil::createSpriteQuad` — 어느 쪽에서 봐도 텍스처가 뒤집히지 않습니다). */
        string_view getDefaultMeshId() const override { return "Sprite"; }
        /** @brief 스프라이트 머티리얼(투명 · `sprite2d.hlsl`)입니다. */
        hashed_string getDefaultMaterialPath() const override;

    private:
        /**
         * @brief 텍스처 인스턴스를 지금 머티리얼 · 텍스처에 맞춥니다. 같은 (머티리얼, 텍스처)는 인스턴스 하나를 나눠 씁니다.
         * @details 이 컴포넌트가 건 인스턴스만 바꾸거나 뗍니다(`_appliedTexture`) — 텍스처가 비어 있으면 코드가 건 인스턴스는 그대로 둡니다.
         */
        void refreshTextureInstance();
        /** @brief `_clipPath` 의 클립을 잡습니다. 이미 그 경로를 잡았으면 아무것도 하지 않습니다. 못 읽으면 한 번 알리고 클립 없이 그립니다. */
        void refreshClip();
        /** @brief 보일 프레임 · 색을 GPU 인스턴스 칸으로 묶어 넘깁니다(`MeshComponent::setSpriteInstanceData`). */
        void refreshSpriteInstanceData();
        /** @brief 텍스처 칸이 비었으면 클립의 아틀라스, 아니면 텍스처 칸입니다. */
        string_view getEffectiveTexture() const;

        PROPERTY( Category = "Rendering", DisplayName = "Texture", AssetPath, AssetType = "Texture", Tooltip = "Texture asset name; empty uses the clip atlas" )
        string        _textureName;
        hashed_string _appliedTexture; ///< 이 컴포넌트가 건 텍스처 인스턴스의 텍스처. 비어 있으면 건 것이 없다
        /**
         * @brief 스프라이트 클립(`.sprite.json`) 경로입니다. 보일 프레임은 번호(`_clipFrame`)로 따로 듭니다 — 프레임마다 문자열을 만들어
         *        파싱하지 않고 번호 하나를 넘깁니다.
         */
        PROPERTY( Category = "Rendering", DisplayName = "Sprite Clip", AssetPath, AssetType = "SpriteClip",
                  Tooltip = "Sprite clip (.sprite.json): atlas frames and named animations" )
        string _clipPath;
        PROPERTY( Category = "Rendering", DisplayName = "Clip Frame", Tooltip = "Frame of the sprite clip to show", Min = 0.0 )
        int32 _clipFrame;
        PROPERTY( Category = "Rendering", DisplayName = "UV Rect", Tooltip = "Atlas rectangle (u, v, width, height) shown when there is no clip" )
        float4 _uvRect;
        PROPERTY( Category = "Rendering", DisplayName = "Tint", Meta = "Color", Tooltip = "Color multiplied into the sprite; alpha is opacity" )
        float4                            _tint;
        shared_ptr<const SpriteClipAsset> _clip;           ///< 읽은 클립(나눠 가진 것). 저장하지 않습니다
        string                            _loadedClipPath; ///< `_clip` 이 어느 경로의 것인지(실패한 경로도 — 같은 실패를 되풀이해 읽지 않습니다)
    };
} // namespace sw
