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
     * @enum SpriteDrawMode
     * @brief 스프라이트를 어떤 메시로 그릴지 정합니다(유니티의 `SpriteDrawMode`).
     */
    ENUM()
    enum class SpriteDrawMode : uint8
    {
        Simple = 0, ///< 단위 사각형 하나를 트랜스폼 스케일로 늘립니다
        Sliced = 1, ///< 9-슬라이스입니다. 모서리는 자연 크기이고, 변과 가운데가 `_size` 까지 늘어납니다
        Tiled  = 2, ///< 모서리는 자연 크기이고, 변과 가운데가 자연 크기 타일로 반복됩니다
    };

    /**
     * @class SpriteComponent
     * @brief 텍스처를 입힌 사각형을 그립니다(2D). 메시는 사각형, 머티리얼은 스프라이트 머티리얼(`sprite2d.material`)이 기본입니다.
     * @details 텍스처는 그 머티리얼의 **인스턴스**가 덮어쓰고(`albedoMap`), 같은 (머티리얼, 텍스처)의 스프라이트는 인스턴스 하나를 나눠 쓴다.
     *          배치 키가 인스턴스라서 그래야 한 드로우로 묶인다. 유니티의 `SpriteRenderer`(스프라이트 기본 머티리얼, 텍스처는 프로퍼티 블록)와
     *          언리얼의 Paper2D(`UPaperSpriteComponent`)에 해당한다.
     *
     *          **무엇을 보여 주나.** 아틀라스의 한 프레임(UV 사각형)과 색이다. 둘 다 머티리얼 인스턴스가 아니라 GPU 인스턴스에 실린다
     *          (`MeshComponent::getSpriteInstanceData` → `GPUInstance::_sprite`). 그래서 프레임이 넘어가거나 알파가 줄어도 배치는 그대로다.
     *          프레임은 클립(`_clipPath`, `.sprite.json`)이 있으면 그 클립의 `_clipFrame` 번째 프레임이고, 없으면 `_uvRect` 다.
     *          텍스처 경로가 비어 있으면 클립의 아틀라스를 쓴다. 애니메이터(`SpriteAnimatorComponent`)는 `setClipFrame` 으로 프레임 번호만 넘긴다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Sprite Component", Tooltip = "2D Sprite rendering component", Meta = "Icon=Sprite" )
    class SW_API SpriteComponent : public MeshComponent
    {
    public:
        REFLECT_BODY();
        SpriteComponent();
        virtual ~SpriteComponent() override = default;

        void onBeginPlay() override;
        /** @brief 메시와 머티리얼을 해석하고 클립, 텍스처 인스턴스, 프레임을 맞춥니다. */
        void resolveRenderAssets() override;
        /**
         * @brief 텍스처, 클립, 프레임, 색 필드를 고치면 다시 맞춥니다.
         * @details 값이 같아도 다시 맞춘다. 에디터 핫 리로드는 클립을 제자리로 다시 읽은 뒤 `_clipPath` 로 이 함수를 부른다.
         */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 단위 사각형(한 변 1)의 반대각선에 월드 X, Y 스케일 중 큰 쪽을 곱한 구입니다. */
        bool getWorldBounds( float3& outCenter, float32& outRadius ) const override;

        /** @brief 스프라이트 텍스처 경로입니다. 비어 있으면 클립의 아틀라스를 쓰고, 그것도 없으면 흰 사각형(머티리얼의 색)입니다. */
        const string& getTextureName() const { return _textureName; }
        /** @brief 텍스처를 바꾸고 인스턴스를 다시 맞춥니다. */
        void setTextureName( string_view texture );

        /** @brief 노멀 맵 텍스처 경로입니다(라이트를 받는 머티리얼 `sprite2dlit.material` 이 읽습니다). 비어 있으면 N·L 없이 비춥니다. */
        const string& getNormalMapName() const { return _normalMapName; }
        /** @brief 노멀 맵을 바꾸고 텍스처 인스턴스를 다시 맞춥니다. */
        void setNormalMapName( string_view normalMap );

        /** @brief 스프라이트 클립(`.sprite.json`) 경로입니다. 비어 있으면 클립 없이 `_uvRect` 를 보입니다. */
        const string& getClipPath() const { return _clipPath; }
        /** @brief 클립을 바꾸고 읽습니다(같은 클립은 스프라이트끼리 공유합니다. `SpriteClipCache::acquire` 참고). */
        void setClipPath( string_view path );
        /** @brief 읽은 클립입니다. 경로가 비었거나 읽지 못했으면 nullptr 입니다. */
        const SpriteClipAsset* getClip() const { return _clip.get(); }

        /** @brief 보여 줄 클립 프레임 번호입니다. */
        int32 getClipFrame() const { return _clipFrame; }
        /** @brief 보여 줄 클립 프레임을 정합니다(0 미만은 0). 클립보다 크면 마지막 프레임을 보여 줍니다. */
        void setClipFrame( int32 frame );

        /** @brief 클립이 없을 때 보여 줄 UV 사각형 (u, v, 폭, 높이)입니다. 기본은 텍스처 전체입니다. */
        const float4& getUvRect() const { return _uvRect; }
        /** @brief 클립이 없을 때 보여 줄 UV 사각형을 정합니다. 폭이 음수이면 좌우가 뒤집힙니다. */
        void setUvRect( const float4& uvRect );

        /** @brief 색(곱)입니다. 알파는 불투명도입니다. 기본은 흰색 불투명입니다. */
        const float4& getTint() const { return _tint; }
        /** @brief 색을 정합니다(머티리얼 색과 텍스처에 곱합니다). 페이드와 피격 색은 여기서 정합니다. 머티리얼 인스턴스를 만들지 않습니다. */
        void setTint( const float4& tint );

        /** @brief 정렬 레이어 이름입니다(`render2d.xml` 의 테이블). 기본은 `Default` 입니다. */
        const hashed_string& getSortingLayer() const { return _sortingLayer; }
        /** @brief 정렬 레이어를 정합니다. 모르는 이름은 오류를 남기고 `Default` 로 그립니다. */
        void setSortingLayer( const hashed_string& layerName );
        /** @brief 레이어 안 순서입니다. 클수록 위에 그려집니다. */
        int32 getOrderInLayer() const { return _orderInLayer; }
        /** @brief 레이어 안 순서를 정합니다([-32767, 32767] 로 제한합니다). */
        void setOrderInLayer( int32 order );

        /** @brief 그리기 방식입니다(Simple, Sliced, Tiled). */
        SpriteDrawMode getDrawMode() const { return _drawMode; }
        /** @brief 그리기 방식을 바꾸고 메시를 다시 고릅니다. */
        void setDrawMode( SpriteDrawMode mode );
        /** @brief Sliced 와 Tiled 의 로컬 크기(폭, 높이)입니다. Simple 은 이 값을 보지 않습니다(트랜스폼 스케일이 크기입니다). */
        const float2& getSize() const { return _size; }
        /** @brief Sliced 와 Tiled 의 크기를 정합니다(음수는 0). */
        void setSize( const float2& size );
        /** @brief 클립 프레임에 테두리가 없을 때 쓰는 9-슬라이스 테두리 (왼쪽, 아래, 오른쪽, 위, 프레임 비율)입니다. */
        const float4& getSliceBorder() const { return _sliceBorder; }
        /** @brief 클립 프레임에 테두리가 없을 때 쓰는 테두리를 정합니다. */
        void setSliceBorder( const float4& border );
        /** @brief 현재 쓰는 테두리입니다. 보이는 클립 프레임에 테두리가 있으면 그것(에셋), 없으면 `_sliceBorder` 입니다. */
        float4 getEffectiveSliceBorder() const;

        /** @brief 현재 보이는 UV 사각형입니다. 클립이 있으면 그 프레임의 것, 없으면 `_uvRect` 입니다. */
        float4 getDisplayedUvRect() const;

    protected:
        /** @brief 양면 스프라이트 사각형입니다(`MeshUtil::createSpriteQuad`. 어느 쪽에서 봐도 텍스처가 뒤집히지 않습니다). */
        string_view getDefaultMeshID() const override { return "Sprite"; }
        /** @brief 스프라이트 머티리얼(투명, `sprite2d.hlsl`)입니다. */
        hashed_string getDefaultMaterialPath() const override;

    private:
        /**
         * @brief 텍스처 인스턴스를 현재 머티리얼과 텍스처에 맞춥니다. 같은 (머티리얼, 텍스처)는 인스턴스 하나를 공유합니다.
         * @details 이 컴포넌트가 등록한 인스턴스만 바꾸거나 해제한다(`_appliedTexture`). 텍스처가 비어 있으면 코드가 등록한 인스턴스는 그대로 둔다.
         */
        void refreshTextureInstance();
        /** @brief `_clipPath` 의 클립을 잡습니다. 이미 그 경로를 잡았으면 아무것도 하지 않습니다. 읽지 못하면 한 번 알리고 클립 없이 그립니다. */
        void refreshClip();
        /** @brief 보일 프레임과 색을 GPU 인스턴스 슬롯으로 묶어 넘깁니다(`MeshComponent::setSpriteInstanceData`). */
        void refreshSpriteInstanceData();
        /**
         * @brief 그리기 방식, 크기, 테두리에 맞는 메시를 설정합니다. Sliced 와 Tiled 는 `SpriteMeshBuilder::acquireSlicedMesh`(같은 값끼리 공유),
         *        Simple 은 공유 사각형을 씁니다. 같은 메시면 아무것도 하지 않습니다.
         */
        void refreshDrawModeMesh();
        /** @brief 정렬 레이어와 순서를 정렬 키로 바꿔 메시 컴포넌트에 넘깁니다(`MeshComponent::setSortKey`). */
        void refreshSortKey();
        /** @brief 텍스처 경로가 비었으면 클립의 아틀라스, 아니면 텍스처 경로를 반환합니다. */
        string_view getEffectiveTexture() const;

        PROPERTY( Category = "Rendering", DisplayName = "Texture", AssetPath, AssetType = "Texture", Tooltip = "Texture asset name; empty uses the clip atlas" )
        string _textureName;
        PROPERTY( Category = "Rendering", DisplayName = "Normal Map", AssetPath, AssetType = "Texture", Tooltip = "Normal map read by the lit sprite material" )
        string        _normalMapName;
        hashed_string _appliedTexture;   ///< 이 컴포넌트가 등록한 텍스처 인스턴스의 텍스처다. 비어 있으면 등록한 것이 없다
        hashed_string _appliedNormalMap; ///< 그 인스턴스의 노멀 맵
        /**
         * @brief 스프라이트 클립(`.sprite.json`) 경로입니다. 보여 줄 프레임은 번호(`_clipFrame`)로 따로 가집니다. 프레임마다 문자열을 만들어
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
        float4 _tint;
        /**
         * @brief 정렬 레이어 이름입니다. 투명 큐는 레이어, 레이어 안 순서, 깊이 순으로 그립니다(유니티의 Sorting Layer, Godot 의 CanvasLayer).
         * @details 같은 Z 의 월드 UI 와 스프라이트처럼 깊이가 같은 것의 앞뒤를 데이터로 정한다. 이름은 `render2d.xml` 의 테이블에 있어야 한다.
         */
        PROPERTY( Category = "Sorting", DisplayName = "Sorting Layer", Tooltip = "Sorting layer name from render2d.xml; earlier layers draw first" )
        hashed_string _sortingLayer;
        PROPERTY( Category = "Rendering", DisplayName = "Draw Mode", Tooltip = "Simple stretches the quad; Sliced keeps the corners; Tiled repeats the middle" )
        SpriteDrawMode _drawMode;
        PROPERTY( Category = "Rendering", DisplayName = "Size", Tooltip = "Local width and height used when the draw mode slices or repeats the sprite", Min = 0.0, Units = m )
        float2 _size;
        PROPERTY( Category = "Rendering", DisplayName = "Slice Border",
                  Tooltip = "9-slice border (left, bottom, right, top) as frame fractions; used when the clip frame has none", Min = 0.0, Max = 1.0 )
        float4 _sliceBorder;
        uint8  _bSliceMeshApplied; ///< 슬라이스 메시를 설정한 상태인지 나타낸다. Simple 로 돌아갈 때 사각형을 다시 설정한다. 저장하지 않는다
        PROPERTY( Category = "Sorting", DisplayName = "Order In Layer", Tooltip = "Draw order inside the sorting layer; higher draws on top", Min = -32767.0,
                  Max = 32767.0 )
        int32                             _orderInLayer;
        shared_ptr<const SpriteClipAsset> _clip;           ///< 읽은 클립(공유하는 것)이다. 저장하지 않는다
        string                            _loadedClipPath; ///< `_clip` 이 어느 경로의 것인지 나타낸다. 실패한 경로도 기록해 같은 실패를 되풀이해 읽지 않는다
    };
} // namespace sw
