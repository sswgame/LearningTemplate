/**
 * @file ModelImporter.h
 * @brief glTF 2.0 모델 원본(`models_raw/` 의 `.glb` · `.gltf`)을 엔진 메시 에셋(`models/` 의 `.mesh`)으로 임포트합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct RHIVertex;
} // namespace sw

namespace sw::editor
{
    enum class AssetImportMode : uint8;

    struct AssetImportSummary;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImporter
     * @brief cgltf 로 glTF 를 읽어 기본 씬의 노드 계층(월드 변환 적용)을 한 메시로 합치고, meshoptimizer 로 인덱스 순서를 다듬은 뒤
     *        인덱스 없는 삼각형 목록(`RHIVertex`)으로 풀어 `.mesh` 로 씁니다.
     * @details 정점: 위치 · 노멀(없으면 면 노멀) · TEXCOORD_0(없으면 0) · 색 = 머티리얼 baseColorFactor × COLOR_0(있으면).
     *          삼각형이 아닌 프리미티브는 경고하고 건너뜁니다. 첫 baseColorTexture 의 이미지는 로그로만 알립니다 — 머티리얼은 게임이 고릅니다.
     */
    struct ModelImporter
    {
        /**
         * @brief glTF 파일 하나를 엔진 좌표계의 삼각형 목록으로 읽습니다. 삼각형이 하나도 없으면 false 입니다.
         * @details 시험과 `importModel` 이 같은 길을 씁니다(파일을 쓰지 않습니다).
         */
        [[nodiscard]] static bool readModel( string_view sourcePath, vector<RHIVertex>& outListVertex );

        /** @brief glTF 파일 하나를 `.mesh` 로 임포트합니다. */
        [[nodiscard]] static bool importModel( string_view sourcePath, string_view outputPath );

        /**
         * @brief 핫 리로드가 넘긴 파일이 모델 원본이면 임포트합니다(`models_raw/` 아래 → 옆 `models/` 의 `.mesh`).
         * @details 임포트된 `.mesh` 의 쓰기가 다음 감시 이벤트로 와서 메시 캐시가 제자리에서 다시 읽습니다. `models_raw/` 밖의 원본은 경고만 합니다.
         * @return 모델 원본이었으면(임포트했든 경고했든) true 이고, 호출자는 캐시를 다시 읽지 않습니다. `.mesh` 면 false 입니다.
         */
        [[nodiscard]] static bool importChangedSourceModel( string_view relativePath );

        /**
         * @brief 리소스 루트 아래 모든 `models_raw/` 의 원본을 그 폴더의 `import.stamp` 와 대조하고, @p mode 가 ImportStale 이면 어긋난 것을 임포트합니다.
         * @details 절차는 `AssetImportStampUtil::importAll` 입니다(텍스처와 같은 스탬프 형식 · 판정).
         */
        [[nodiscard]] static AssetImportSummary importAllModels( string_view resourceRoot, AssetImportMode mode );

        /** @brief 원본 경로에 대응하는 메시 경로입니다(`<x>/models_raw/<y>.glb` → `<x>/models/<y>.mesh`). `models_raw/` 구간이 없으면 빈 문자열입니다. */
        static string makeImportedModelPath( string_view rawModelPath );

        /**
         * @brief 원본 바이트(`.gltf` 면 그것이 가리키는 외부 버퍼 파일까지)와 임포터 버전을 섞은 FNV-1a 64 입니다. 읽지 못하면 0 입니다.
         */
        static uint64 computeSourceHash( string_view sourcePath );
    };
} // namespace sw::editor
