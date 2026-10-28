/**
 * @file HeightfieldAsset.h
 * @brief 높이장 에셋(`.heightfield`)입니다. 16 비트 높이 N × N 과 셀 구멍 마스크를 담습니다. 원본(`heightfields_raw/` 의 16 비트 PNG 와 `.r16`)을
 *        `App --import-heightfields` 가 이 형식으로 임포트합니다.
 * @details 형식(리틀 엔디언)은 다음과 같다.
 *          | 바이트 | 의미 |
 *          |---|---|
 *          | 4 | 매직 `SWHF` |
 *          | 4 | 버전 번호(`kVersion`) |
 *          | 4 | 해상도 N(한 변의 샘플 수, 2 이상) |
 *          | 4 | 플래그(비트 0 = 구멍 마스크가 있다) |
 *          | 2 × N × N | 높이(0..65535, 행 우선이며 z 가 바깥, x 가 안쪽) |
 *          | (N-1) × (N-1) | 구멍(셀마다 1 바이트, 0 이 아니면 구멍). 플래그가 있을 때만 있다 |
 *          높이의 월드 값(최저와 최고)과 넓이는 에셋이 아니라 `TerrainComponent` 가 정한다(언리얼 랜드스케이프의 Z 스케일에 해당한다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @struct HeightfieldAsset
     * @brief 높이장 한 장의 원시 값입니다. 읽기, 쓰기, 셀 조회만 하고 월드 좌표는 모릅니다(`TerrainHeightfield` 가 압니다).
     */
    struct SW_API HeightfieldAsset
    {
        static constexpr uint32      kMagic     = FourCcUtil::make( "SWHF" );
        static constexpr string_view kExtension = ".heightfield";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `HeightfieldAsset.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "HeightfieldAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
        static constexpr uint32       kVersion       = 1u;
        static constexpr uint32       kFlagHoleMask  = 1u;
        static constexpr uint32       kMaxResolution = 8193u;

        vector<uint16> _listHeight;   ///< N × N 배열이다. 인덱스는 z × N + x 다.
        vector<uint8>  _listHoleCell; ///< (N−1) × (N−1), 비었으면 구멍이 없다
        uint32         _resolution{ 0 };

        /** @brief 값이 맞는 형태면 true 를 반환합니다(해상도 2 이상, 높이 수, 구멍 수). */
        bool isValid() const;
        /** @brief 샘플 (x, z) 의 원시 높이입니다. */
        uint16 getSample( uint32 sampleX, uint32 sampleZ ) const { return _listHeight[static_cast<size_t>( sampleZ ) * _resolution + sampleX]; }
        /** @brief 셀 (x, z) 가 구멍이면 true 를 반환합니다. */
        bool isHoleCell( uint32 cellX, uint32 cellZ ) const;

        /** @brief 바이트 배열에서 읽습니다. 형식이 틀리면 이유를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromMemory( const vector<uint8>& bytes, string_view sourceName );
        /** @brief 리소스 경로(`game/empty/heightfields/x.heightfield`)에서 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& id );
        /** @brief 형식대로 바이트를 만듭니다. */
        void saveToMemory( vector<uint8>& outBytes ) const;
        /** @brief 절대 경로에 씁니다(임포터와 테스트용). */
        [[nodiscard]] bool saveToFile( string_view filePath ) const;
    };
} // namespace sw
