/**
 * @file HeightfieldImporter.h
 * @brief 높이장 원본(`heightfields_raw/` 의 16 비트 회색 PNG 와 `.r16`)을 `heightfields/<이름>.heightfield` 로 임포트합니다(`App --import-heightfields`).
 * @details 원본 옆의 `<이름>_holes.png`(회색, 128 미만이 구멍)는 원본이 아니라 보조 파일이다. 그 원본의 셀 구멍 마스크가 되고 원본 해시에 섞이므로,
 *          보조 파일만 고쳐도 다시 임포트된다. `.r16` 은 리틀 엔디언 uint16 의 정사각형이고 한 변은 √(바이트 / 2) 다. 구멍 마스크는 셀 수
 *          (해상도 − 1)² 크기이거나, 샘플 해상도와 같은 크기여야 한다. 샘플 해상도 크기면 마지막 행과 열은 버린다.
 *
 *          바깥 도구(World Machine, Gaea, 이미지 편집기)가 만든 스플랫을 같은 폴더의 `<이름>_splat.png`(RGBA PNG, 채널마다 레이어 0..3 가중치)로 두면
 *          `heightfields/<이름>.layerweights`(`LayerWeightsData`)로 임포트한다. 보조 파일이 아니라 스탬프 줄을 따로 갖는 원본이다. 높이장과 따로 바뀌고,
 *          브러시가 칠해 저장한 결과를 손댄 것으로 잡는다. 알파가 없는 PNG 는 거절한다. 디코더가 알파를 255 로 채워 레이어 3 이 말없이 가득 차기 때문이다.
 *          가중치 바이트는 그대로 옮기고 정규화하지 않는다. 지형이 읽을 때 합으로 나눈다(`TerrainHeightfield::computeLayerWeightsAtUv`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct HeightfieldData;
    struct LayerWeightsData;
} // namespace sw

namespace sw::editor
{
    enum class AssetImportMode : uint8;

    struct AssetImportSummary;

    /**
     * @struct HeightfieldImporter
     * @brief 높이장 원본 → `.heightfield` 입니다.
     */
    struct HeightfieldImporter
    {
        /** @brief 원본(+ 곁 구멍 마스크)을 읽어 높이장으로 만듭니다. */
        [[nodiscard]] static bool readHeightfield( string_view sourcePath, HeightfieldData& outData );
        /** @brief 원본 하나를 임포트해 @p outputPath 에 씁니다. */
        [[nodiscard]] static bool importHeightfield( string_view sourcePath, string_view outputPath );
        /** @brief 리소스 트리의 모든 `heightfields_raw/` 를 스탬프와 대조하고, @p mode 가 ImportStale 이면 어긋난 것을 임포트합니다. */
        [[nodiscard]] static AssetImportSummary importAllHeightfields( string_view resourceRoot, AssetImportMode mode );
        /** @brief 원본 경로의 결과 경로입니다(`…/heightfields_raw/x.png` → `…/heightfields/x.heightfield`). */
        static string makeImportedHeightfieldPath( string_view rawPath );
        /** @brief 원본 바이트 + 곁 구멍 마스크 + 임포터 판의 해시입니다. */
        static uint64 computeSourceHash( string_view sourcePath );
        /** @brief 원본 파일이면 true 입니다(곁 파일 `_holes` 와 스플랫 원본 `_splat` 은 아닙니다). */
        static bool isSourceHeightfield( string_view path );
        /** @brief 스플랫 원본(`<이름>_splat.png`)이면 true 입니다. */
        static bool isSourceLayerWeights( string_view path );
        /** @brief 스플랫 원본(RGBA PNG)을 읽어 레이어 가중치로 만듭니다. 알파가 없거나 크기가 범위 밖이면 이유를 남기고 false 를 반환합니다. */
        [[nodiscard]] static bool readLayerWeights( string_view sourcePath, LayerWeightsData& outData );
        /** @brief 스플랫 원본의 결과 경로입니다(`…/heightfields_raw/x_splat.png` → `…/heightfields/x.layerweights`). */
        static string makeImportedLayerWeightsPath( string_view rawPath );
        /** @brief `heightfields_raw/` 아래의 원본, 구멍 마스크, 스플랫 원본(`.png`, `.r16`)이면 true 입니다. 에디터 핫 리로드는 이 경로를 텍스처가 아니라 높이장 임포트로 보냅니다. */
        static bool isRawHeightfieldPath( string_view path );
        /**
         * @brief 에디터 핫 리로드가 넘긴 원본(리소스 루트 기준 경로)이 바뀌었을 때 부릅니다. 어긋난 높이장을 모두 임포트합니다(`App --import-heightfields` 와 같은 경로).
         * @return 처리했으면 true(임포트했거나 바뀐 것이 없었다). 높이장 원본 경로가 아니면 false 입니다.
         */
        [[nodiscard]] static bool importChangedSourceHeightfield( string_view relativePath );
    };
} // namespace sw::editor
