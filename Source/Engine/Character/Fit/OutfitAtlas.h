/**
 * @file OutfitAtlas.h
 * @brief 아틀라스 굽기의 순수 계산 — 부품 텍스처를 한 장에 앉히는 배치, 배치대로 UV 를 옮기는 병합 훅, 텍스처 픽셀을 배치에 옮겨 그리는 합성입니다.
 * @details 같은 옷차림을 오래 쓰는 캐릭터(NPC 세트)는 부품마다 텍스처 · 머티리얼이 따로라 그리기 호출이 부품 수만큼 든다. 아틀라스는 부품 텍스처를 한 장에 모으고
 *          부품 메시의 UV 를 그 자리로 옮겨, 부품들이 머티리얼 하나(구간 하나)로 그려지게 한다(`CharacterMeshUtil::mergeMeshes` 의 훅 `IMeshMergeHooks`).
 *          이 파일은 디스크도 GPU 도 모른다 — 읽기 · 쓰기와 명령은 에디터의 `OutfitAtlasBaker` 가 한다. 언리얼 Mutable 의 텍스처 합성(Compose)과 같은 자리다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/Fit/MeshMerger.h"

namespace sw
{
    /** @brief 아틀라스에 앉힐 텍스처 하나입니다(부품 이름과 픽셀 크기). */
    struct OutfitAtlasSource
    {
        hashed_string _part{};
        uint32        _width{ 0 };
        uint32        _height{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 부품 텍스처가 아틀라스에 앉은 자리입니다(픽셀, 왼쪽 위가 원점 — 이미지 행 순서와 같다). 가장자리 번짐(패딩)은 이 사각형 밖이다. */
    struct OutfitAtlasRect
    {
        hashed_string _part{};
        uint32        _x{ 0 };
        uint32        _y{ 0 };
        uint32        _width{ 0 };
        uint32        _height{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 아틀라스 배치입니다(전부 static). */
    struct SW_API OutfitAtlasLayout
    {
        /** @brief 아틀라스 한 변의 상한(픽셀)입니다. */
        static constexpr uint32 kMaxAtlasSize = 8192;

        /**
         * @brief 텍스처들을 정사각 아틀라스 @p atlasSize 안에 선반 방식으로 앉힙니다 — 키가 큰 것부터 줄을 채우고 줄이 차면 다음 줄입니다. 결과는 결정적이고(같은 입력은 같은 배치) 입력 순서입니다.
         * @param padding 텍스처 사방에 비워 두는 번짐 폭(픽셀)입니다. 이웃 텍스처 사이에는 두 배가 든다.
         * @return 크기가 0 이거나, 이름이 겹치거나, 아틀라스(`kMaxAtlasSize` 이하)에 다 앉지 않으면 까닭을 @p pOutError 에 남기고 false 입니다.
         */
        [[nodiscard]] static bool pack( vector_reference<const OutfitAtlasSource> listSource, uint32 atlasSize, uint32 padding, vector<OutfitAtlasRect>& outListRect,
                                        string* pOutError );
    };
} // namespace sw

namespace sw
{
    /**
     * @class OutfitAtlasHooks
     * @brief 배치(`OutfitAtlasLayout`)대로 부품을 한 머티리얼 그룹으로 모으고 UV 를 아틀라스 안의 자리로 옮기는 병합 훅입니다.
     * @details 배치에 있는 부품은 모두 @p groupName 그룹이 되고, 없는 부품은 자기 이름의 그룹으로 남아 UV 도 그대로다. 옮길 UV 는 0..1 안이어야 한다 —
     *          타일링 UV 는 아틀라스에 앉힐 수 없어 범위 밖 UV 는 가장자리로 자르고 센다(`getClampedUvCount`; 부르는 쪽이 데이터 오류로 알린다).
     */
    class SW_API OutfitAtlasHooks final : public IMeshMergeHooks
    {
    public:
        OutfitAtlasHooks( const hashed_string& groupName, uint32 atlasSize, vector<OutfitAtlasRect> listRect );

        hashed_string getMaterialGroup( const MeshMergeSource& source ) const override;
        float2        remapUv( const MeshMergeSource& source, const float2& uv ) const override;
        /** @brief 지금까지 0..1 밖이라 잘라 낸 UV 수입니다. */
        uint32 getClampedUvCount() const { return _clampedUvCount; }

    private:
        const OutfitAtlasRect* findRect( const hashed_string& part ) const;

        hashed_string           _groupName;
        float32                 _atlasSize;
        vector<OutfitAtlasRect> _listRect;
        mutable uint32          _clampedUvCount;
    };
} // namespace sw

namespace sw
{
    /** @brief 부품 텍스처 픽셀을 아틀라스에 옮겨 그리는 합성입니다(RGBA8, 전부 static). */
    struct SW_API OutfitAtlasComposer
    {
        /** @brief 한 변 @p atlasSize 인 RGBA8 아틀라스를 투명한 검정으로 채워 만듭니다. */
        static void initialize( uint32 atlasSize, vector<uint8>& outListPixel );
        /**
         * @brief 텍스처(RGBA8, 빈틈없는 행)를 @p rect 에 복사하고, 사방 @p padding 픽셀을 가장자리 픽셀로 채웁니다(밉 · 필터링이 이웃 텍스처 색을 끌어오지 않게).
         * @return 텍스처 크기가 사각형과 다르거나 사각형이 아틀라스를 벗어나면 까닭을 @p pOutError 에 남기고 false 입니다.
         */
        [[nodiscard]] static bool blit( uint32 atlasSize, const OutfitAtlasRect& rect, uint32 padding, const vector<uint8>& textureRGBA, uint32 textureWidth, uint32 textureHeight,
                                        vector<uint8>& inoutAtlas, string* pOutError );
    };
} // namespace sw
