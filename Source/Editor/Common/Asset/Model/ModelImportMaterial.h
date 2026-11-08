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
#include "Editor/Common/Asset/Model/ToonMaterialDef.h"
#include "Editor/Common/Asset/Model/VrmAvatarDef.h"

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
        [[nodiscard]] static bool readToonMaterials( const cgltf_data& data, string_view sourcePath, vector<ToonMaterialDef>& outListToon,
                                                     vector<string>& outListIgnored );

        /**
         * @brief 일반 glTF(VRM 아님)에 머티리얼이 여럿이고 그중 기본색 텍스처가 있는 것이 있으면 모든 머티리얼을 @p inoutResult 의 `_listPBRMaterial` 에 읽습니다(glTF 순서).
         * @details 그런 모델은 머티리얼마다 구간이 된다(언리얼 · 유니티 · Godot 의 머티리얼 슬롯 · 서브메시 · 서피스와 같다 — 첫 머티리얼만 쓰면 나머지 면이 엉뚱한 텍스처로 그려진다).
         *          텍스처 없는 머티리얼만 여럿이면 지금처럼 색을 정점에 구운 메시 하나다(구간 없음). 이름은 glTF 이름(없으면 `material<번호>`)의 파일 이름 꼴이다.
         * @return 원본이 가리키는 바깥 이미지 파일을 읽지 못하면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool readSectionPBRMaterials( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult );

        /**
         * @brief 합친 메시의 삼각형을 (머티리얼, 1인칭 가시성) 묶음마다 이어 놓고 구간 표를 만듭니다. 메시는 한 벌이다 — 구간은 그 메시의 정점 범위다(`MeshSection`).
         * @param listMaterialName glTF 머티리얼마다의 이름입니다(툰 · PBR 머티리얼 표의 이름). 이 표 밖의 머티리얼을 쓰는 삼각형은 어느 구간에도 들지 않는다.
         * @details 1인칭 가시성은 VRM firstPerson 의 glTF 메시마다 값이다. Auto(적지 않은 메시 포함)는 UniVRM 과 같이 1인칭 본(머리) 아래 본에 가중치가 있는
         *          꼭짓점을 하나라도 가진 삼각형을 3인칭 전용으로, 나머지를 둘 다로 나눈다(VRM 이 아니면 아바타가 비어 모두 둘 다다). 묶음 안의 순서는 정점 캐시 ·
         *          오버드로 최적화를 묶음마다 따로 한다. 삼각형이 없는 묶음은 빠진다. 구간 이름은 머티리얼 파일 이름이고, 가시성이 둘 다가 아니면 `_tp` · `_fp` 를 붙인다.
         */
        static void populateMaterialSections( ModelImportUtil::MergedMesh& inoutBody, const cgltf_data& data, const vector<string>& listMaterialName, ModelImportResult& inoutResult );
        /**
         * @brief 다시 임포트할 때 날 이름(머티리얼 · 메시)과 가시성이 같은 앞 구간의 이름을 이어받습니다. 이어받은 구간 수를 돌려줍니다.
         * @details 구간 이름은 머티리얼 파일 이름(겹치면 번호)이라 원본에서 머티리얼 하나가 늘거나 이름이 바뀌면 다른 구간의 번호까지 밀린다 — 외형 데이터(`<Section name>` · 별칭 ·
         *          염색 include)는 그 이름을 짚으므로 날 이름으로 앞 이름을 지킨다(언리얼 FBX 다시 임포트가 머티리얼 슬롯 이름으로 슬롯을 맞추는 자리). 다른 새 구간이 이미 쓰는 이름은 받지 않는다.
         */
        static uint32 keepPreviousSectionNames( vector<ModelImportSection>& inoutListSection, const vector<MeshSection>& listPrevious );

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
         * @brief 머티리얼 구간의 머티리얼(VRM 은 툰, 일반 glTF 는 PBR)과 그 텍스처 원본을 씁니다. 구간 자체는 본 메시(`<y>.mesh`)의 `SECT` 덩어리다 — 구간 메시를 따로 쓰지 않는다.
         * @details 머티리얼 값은 공유 저장소에 한 벌(`writeSharedMaterial`)이고 슬롯은 `<옆 폴더>/materials/<머티리얼>.material` 한 줄이다(엔진 툰 · PBR 머티리얼이 부모다). 같은 머티리얼의 구간 여럿(1인칭 가시성이 다른)은 파일 하나를 쓴다.
         *          PBR 구간의 텍스처는 `writePBRMaterial` 과 같은 규칙이다. 내장 이미지는 공유 원본으로 꺼낸다(`writeEmbeddedImage`).
         *          머티리얼은 텍스처 임포트(`App --import-textures`)가 만들 DDS 를 가리킨다. 원본은 텍스처 임포트의 스탬프가 지킨다.
         */
        [[nodiscard]] static bool writeMaterialSections( const AssetPaths& assetPaths, const ModelImportResult& result, string_view sourcePath, string_view sideFolder );

        /**
         * @brief 일반 glTF 의 첫 금속도 · 거칠기 머티리얼(기본색 텍스처가 있는 것)을 읽어 @p inoutResult 의 `_pbrMaterial` 에 담습니다. 없으면 아무것도 하지 않습니다.
         * @details 구간 없는 메시(정적 · 스킨드)의 머티리얼이다 — 텍스처 머티리얼이 있는데 머티리얼이 여럿인 모델은 구간으로 나뉘어(`readSectionPBRMaterials`) 여기에 오지 않는다.
         * @return 원본이 가리키는 바깥 이미지 파일을 읽지 못하면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool readPBRMaterial( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult );
        /** @brief 기본색 텍스처가 있는 첫 금속도 · 거칠기 머티리얼입니다(구간 없는 메시가 PBR 머티리얼로 옮기는 것). 없으면 nullptr 입니다. */
        [[nodiscard]] static const cgltf_material* findTexturedPBRMaterial( const cgltf_data& data );
        /**
         * @brief 읽은 PBR 머티리얼의 내장 텍스처 원본(`writeEmbeddedImage`)과 머티리얼(`writeSharedMaterial`)을 씁니다.
         * @details 텍스처 이름 꼬리가 텍스처 임포트 규칙을 고른다(`_normal` → BC5 선형, `_orm` → 선형). 머티리얼은 엔진 PBR 견본(`EngineDefaultAssets::_pbrMaterialTemplate`)을 부모로 다른 칸만 적는다.
         */
        [[nodiscard]] static bool writePBRMaterial( const AssetPaths& assetPaths, const ModelImportResult& result, string_view sourcePath, string_view sideFolder );
        /** @brief glTF 머티리얼 하나를 PBR 머티리얼 값으로 읽습니다(@p rawName 이 머티리얼 · 텍스처 파일 이름의 바탕이다). 바깥 이미지 파일을 못 읽으면 false 입니다. */
        [[nodiscard]] static bool readPBRMaterialValues( const cgltf_material& material, const string& rawName, string_view sourcePath, ModelImportPBRMaterial& outPBR );
        /** @brief PBR 머티리얼 하나의 텍스처 원본과 머티리얼(슬롯 `<옆 폴더>/materials/<@p fileStem>.material` + 공유 값)을 씁니다(`writePBRMaterial` 의 몸). */
        [[nodiscard]] static bool writePBRMaterialFile( const AssetPaths& assetPaths, const ModelImportPBRMaterial& pbr, string_view fileStem, string_view sourcePath, string_view sideFolder );
        /** @brief glTF 이미지 하나의 원본 바이트를 읽습니다(내장은 그대로, 바깥 파일은 원본 옆에서). 바깥 파일을 못 읽으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool readImageBytes( const cgltf_image& image, string_view sourcePath, ModelImportTexture& outTexture );

        /** @brief 공유 머티리얼의 저장소 종류 폴더입니다(`ImportedAssetStore`). */
        static constexpr string_view kSharedMaterialKind = "materials";

        /** @brief 공유 머티리얼 한 벌입니다. */
        struct SharedMaterialText
        {
            string _key;  ///< 이름을 뺀 내용의 키이자 파일 이름 · 머티리얼 이름
            string _text; ///< 부모(견본)와 다른 칸만 적은 XML
        };

        /**
         * @brief 견본 @p templateText(경로 @p templatePath)에 값을 채운 @p fullText 를 견본과 다른 칸만 남긴 공유 머티리얼로 줄입니다(`MaterialXML::makeParentDelta`). 값까지 같으면 이름이 달라도 같은 키입니다.
         * @details 두 글은 같은 견본을 읽어 고친 것이라 안 고친 속성의 글자가 같다. 칸 형식은 적지 않는다 — 셰이더 리플렉션이 정한다.
         * @return 글을 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool makeSharedMaterialText( string_view fullText, string_view templateText, string_view templatePath, SharedMaterialText& outShared );
        /** @brief 공유 머티리얼을 가리키는 슬롯 파일 한 줄(`<MaterialDef formatVersion name parent/>`)입니다. */
        [[nodiscard]] static string makeSlotMaterialText( string_view fileStem, string_view sharedAssetID );
        /** @brief 공유 머티리얼(`<x>/models/_shared/materials/<키>.material`)과 슬롯 파일을 씁니다. 원본이 `models_raw/` 아래가 아니거나 쓰지 못하면 false 입니다. */
        [[nodiscard]] static bool writeSharedMaterial( const AssetPaths& assetPaths, string_view fullText, string_view templateText, string_view templatePath, string_view fileStem, string_view sourcePath, string_view sideFolder );
        /** @brief 슬롯 파일이 가리키는 공유 머티리얼의 리소스 경로입니다(임포트가 쓰는 프리팹은 공유 파일을 바로 가리킨다). 읽지 못하면 빈 글입니다. */
        [[nodiscard]] static string findSharedMaterial( string_view sideFolder, string_view fileStem );
        /**
         * @brief 내장 이미지를 공유 원본(`<x>/textures_raw/_shared/<규칙>/<키>.<확장자>`)으로 꺼내고 텍스처 임포트가 만들 DDS 의 리소스 경로를 @p outAssetPath 에 담습니다.
         * @details 모델마다 꺼내면 같은 아틀라스를 쓰는 모델 수만큼 DDS 가 생긴다(KayKit Dungeon 19 벌). 규칙은 폴더 이름에 남아 설정이 다르면 다른 원본이다. 바이트가 같으면 건드리지 않는다.
         * @return 임포트 설정이 깨졌거나 규칙이 겹치거나 쓰지 못하면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool writeEmbeddedImage( const AssetPaths& assetPaths, const ModelImportTexture& texture, string_view rawTextureFolder, string& outAssetPath );

        /**
         * @brief 원본 경로에서 원본 텍스처 폴더를 구합니다(`<x>/models_raw/<y>.vrm` → `<x>/textures_raw/<y>`). `models_raw/` 구간이 없으면 빈 문자열입니다.
         */
        static string makeRawTextureFolder( string_view sourcePath );
    };
} // namespace sw::editor
