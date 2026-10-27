/**
 * @file LayerWeightsData.h
 * @brief 지형 레이어 가중치 에셋(`.layerweights`)입니다. 텍셀마다 레이어 여덟의 가중치(8 비트)를 담습니다. 클라이언트와 전용 서버가 같은 파일을 CPU 로 읽습니다.
 * @details 형식(리틀 엔디언)은 다음과 같다.
 *          | 바이트 | 의미 |
 *          |---|---|
 *          | 4 | 매직 `SWLW` |
 *          | 4 | 버전 번호(`kVersion` = 2. 다른 판은 읽지 않는다) |
 *          | 4 | 너비 W(1 이상) |
 *          | 4 | 높이 H(1 이상) |
 *          | 4 | 레이어 수(`kLayerCount`) |
 *          | 8 × W × H | 텍셀마다 레이어 0..7 가중치(0..255, 행 우선이며 z 가 바깥, x 가 안쪽). 레이어 0..3 이 앞 네 바이트(RGBA 한 장), 4..7 이 뒤 네 바이트다 |
 *          가중치는 텍스처가 아니라 게임플레이 데이터다(발소리 · 표면 재질 · 이동 속도 판정). 텍스처(`.dds`)는 전용 서버 패키지에서 빠지므로
 *          (`Config/Engine/CookContract.json` 의 `target_excluded_asset_kinds`) 이 형식으로 둔다 — 서버도 레이어 질의에 정확히 답한다.
 *          클라이언트의 GPU 텍스처는 지형이 읽은 뒤 이 바이트로 만든다(`TerrainComponent`, `TextureCache::createCPUTexture`).
 *          결정과 근거는 `docs/09_Decisions.md` "지형 레이어 가중치는 텍스처가 아닌 데이터 에셋이다" 에 있다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Environment/Placement/SurfaceLayerWeights.h"
#include "Engine/Resource/ResourceID.h"

namespace sw
{
    /**
     * @struct LayerWeightsData
     * @brief 레이어 가중치 맵 한 장의 원시 값입니다. 읽기와 쓰기만 하고 월드 좌표는 모릅니다(`TerrainHeightfield` 가 압니다).
     */
    struct SW_API LayerWeightsData
    {
        static constexpr uint32      kMagic     = FourCcUtil::make( "SWLW" );
        static constexpr string_view kExtension = ".layerweights";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `LayerWeightsData.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "LayerWeightsData", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
        static constexpr uint32       kVersion       = 2u;
        static constexpr uint32       kLayerCount    = SurfaceLayerWeights::kLayerCount;
        static constexpr uint32       kMaxResolution = 8193u;

        vector<uint8> _bytes; ///< W × H × `kLayerCount` 바이트다. 텍셀 (x, z) 는 (z × W + x) × `kLayerCount` 에서 시작한다.
        uint32        _width{ 0 };
        uint32        _height{ 0 };

        /** @brief 값이 맞는 형태면 true 를 반환합니다(너비와 높이 1..`kMaxResolution`, 바이트 수). */
        bool isValid() const;

        /** @brief 바이트 배열에서 읽습니다. 형식이 틀리면 이유를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromMemory( const vector<uint8>& bytes, string_view sourceName );
        /** @brief 리소스 경로(`game/empty/heightfields/x.layerweights`)에서 읽습니다. 읽지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( const ResourceID& id );
        /** @brief 형식대로 바이트를 만듭니다. */
        void saveToMemory( vector<uint8>& outBytes ) const;
        /** @brief 절대 경로에 씁니다(에디터 저장과 테스트용). */
        [[nodiscard]] bool saveToFile( string_view filePath ) const;
    };
} // namespace sw
