/**
 * @file ModelImportMaterial.h
 * @brief VRM 툰 머티리얼을 읽고, 합친 메시를 (머티리얼, 1인칭 가시성) 구간으로 나누고, 구간 텍스처 원본과 머티리얼 파일을 씁니다.
 * @note 임포터 내부 전용입니다. 공개 API 는 `ModelImporter.h` 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Asset/Model/ModelImportUtil.h"
#include "Editor/Common/Asset/Model/ModelImporter.h"
#include "Editor/Common/Asset/Model/VrmAvatarImporter.h"
#include "Editor/Common/Asset/Model/VrmMaterialImporter.h"

#include <cgltf.h>

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct ModelImportMaterial
     * @brief VRM 툰 머티리얼을 읽고, 합친 메시를 (머티리얼, 1인칭 가시성) 구간으로 나누고, 구간 텍스처 원본과 머티리얼 파일을 씁니다.
     */
    struct ModelImportMaterial
    {
        /**
         * @brief VRM 이면 glTF 머티리얼마다 툰 머티리얼 값을 읽습니다(0.x 는 `extensions.VRM.materialProperties`, 1.0 은 `VRMC_materials_mtoon`).
         * @param outListToon glTF 머티리얼 순서의 툰 머티리얼입니다. VRM 이 아니면 비어 있습니다.
         * @return 모르는 키나 값이 있으면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] static bool readToonMaterials( const cgltf_data& data, string_view sourcePath, vector<ToonMaterialDesc>& outListToon,
                                                     vector<string>& outListIgnored );

        /**
         * @brief 합친 메시의 삼각형을 (머티리얼, 1인칭 가시성) 묶음마다 이어 놓고 구간 표를 만듭니다. 메시는 한 벌이다 — 구간은 그 메시의 정점 범위다(`MeshSection`).
         * @details 1인칭 가시성은 VRM firstPerson 의 glTF 메시마다 값이다. Auto(적지 않은 메시 포함)는 UniVRM 과 같이 1인칭 본(머리) 아래 본에 가중치가 있는
         *          꼭짓점을 하나라도 가진 삼각형을 3인칭 전용으로, 나머지를 둘 다로 나눈다. 묶음 안의 순서는 정점 캐시 · 오버드로 최적화를 묶음마다 따로 한다.
         *          삼각형이 없는 묶음은 빠진다. 구간 이름은 머티리얼 파일 이름이고, 가시성이 둘 다가 아니면 `_tp` · `_fp` 를 붙인다.
         */
        static void populateMaterialSections( ModelImportUtil::MergedMesh& inoutBody, const cgltf_data& data, const vector<ToonMaterialDesc>& listToon, ModelImportResult& inoutResult );

        /** @brief glTF 메시의 VRM 1인칭 가시성을 찾습니다. 적지 않은 메시(와 nullptr)는 Auto 입니다. 이름은 아바타 임포터가 쓴 규칙(makeMeshName)과 같습니다. */
        [[nodiscard]] static VrmFirstPersonMode findFirstPersonMode( const cgltf_data& data, const VrmAvatarDef& avatar, const cgltf_mesh* pMesh );

        /** @brief 삼각형의 꼭짓점 중 머리 아래 본(`listHeadBone[본] == SW_TRUE`)에 가중치가 있는 것이 하나라도 있는지 알려 줍니다. */
        [[nodiscard]] static bool triangleTouchesHead( const ModelImportUtil::MergedMesh& body, const vector<uint8>& listHeadBone, size_t triangle );

        /**
         * @brief 구간 머티리얼이 쓰는 텍스처의 원본 이미지 바이트를 모읍니다. glTF 텍스처 번호로 찾으며, 파일 이름은 이미지 이름입니다(겹치면 번호를 붙인다).
         * @details 내장 이미지(bufferView)는 바이트 그대로 가져오고, 바깥 파일(uri)은 원본 옆에서 읽는다. data URI 이미지는 지원하지 않는다(경고를 남긴다).
         * @return 원본이 가리키는 바깥 이미지 파일을 읽지 못하면 오류를 남기고 false 입니다(임포트 실패).
         */
        [[nodiscard]] static bool collectSectionTextures( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult );

        /**
         * @brief VRM 의 머티리얼 구간의 툰 머티리얼과 그 텍스처 원본을 씁니다. 구간 자체는 본 메시(`<y>.mesh`)의 `SECT` 덩어리다 — 구간 메시를 따로 쓰지 않는다.
         * @details 머티리얼은 `<옆 폴더>/materials/<머티리얼>.material` 에 쓴다(엔진 툰 머티리얼이 틀이다). 같은 머티리얼의 구간 여럿(1인칭 가시성이 다른)은 파일 하나를 쓴다.
         *          텍스처 원본(내장 이미지 바이트 그대로)은 `<x>/textures_raw/<y>/<이미지>.png` 에 꺼내 두며, 바이트가 같으면 건드리지 않는다.
         *          머티리얼은 텍스처 임포트(`App --import-textures`)가 만들 `<x>/textures/<y>/<이미지>.dds` 를 가리킨다.
         *          원본은 텍스처 임포트의 스탬프가 지킨다.
         */
        [[nodiscard]] static bool writeMaterialSections( const AssetPaths& assetPaths, const ModelImportResult& result, string_view sourcePath, string_view sideFolder );

        /**
         * @brief 원본 경로에서 원본 텍스처 폴더를 구합니다(`<x>/models_raw/<y>.vrm` → `<x>/textures_raw/<y>`). `models_raw/` 구간이 없으면 빈 문자열입니다.
         */
        static string makeRawTextureFolder( string_view sourcePath );
    };
} // namespace sw::editor
