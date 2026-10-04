/**
 * @file HeightfieldImporter.h
 * @brief 높이장 원본(`heightfields_raw/` 의 16 비트 회색 PNG · `.r16`)을 `heightfields/<이름>.heightfield` 로 임포트합니다(`App --import-heightfields`).
 * @details 원본 옆의 `<이름>_holes.png`(회색, 128 미만 = 구멍)는 원본이 아니라 곁 파일입니다 — 그 원본의 칸 구멍 마스크가 되고, 원본 해시에 섞여
 *          곁 파일만 고쳐도 다시 임포트됩니다. `.r16` 은 작은 엔디언 uint16 의 정사각형(한 변 = √(바이트 / 2))입니다. 구멍 마스크는 칸
 *          (해상도 − 1)² 의 크기거나 샘플 해상도와 같은 크기(마지막 행 · 열은 버림)여야 합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct HeightfieldData;
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
        /** @brief 원본 파일이면 true 입니다(곁 파일 `_holes` 는 아닙니다). */
        static bool isSourceHeightfield( string_view path );
        /** @brief `heightfields_raw/` 아래의 원본 · 구멍 마스크(`.png` · `.r16`)인지입니다 — 에디터 핫 리로드가 이 경로를 텍스처가 아니라 높이장 임포트로 보낸다. */
        static bool isRawHeightfieldPath( string_view path );
        /**
         * @brief 에디터 핫 리로드가 넘긴 원본(리소스 루트 기준 경로)이 바뀌었습니다 — 어긋난 높이장을 모두 임포트합니다(`App --import-heightfields` 와 같은 길).
         * @return 처리했으면 true(임포트했거나 바뀐 것이 없었다). 높이장 원본 경로가 아니면 false 입니다.
         */
        [[nodiscard]] static bool importChangedSourceHeightfield( string_view relativePath );
    };
} // namespace sw::editor
