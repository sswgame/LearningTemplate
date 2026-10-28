/**
 * @file OutfitAtlasBaker.h
 * @brief 옷차림 아틀라스 굽기(쿠킹) — 부품 메시와 부품 텍스처를 읽어, 텍스처를 한 장에 모으고 UV 를 옮긴 병합 메시(`.mesh`)와 아틀라스 이미지(PNG)를 씁니다.
 * @details 계산은 엔진의 `OutfitAtlasLayout` · `OutfitAtlasHooks` · `OutfitAtlasComposer` · `CharacterMeshUtil::mergeMeshes` 가 하고, 이 파일은 읽기 · 쓰기와 명세 파일만 맡는다.
 *          명세(`*.outfitatlas.xml`)의 예:
 *          @code
 *          <OutfitAtlas group="Outfit" outputMesh="game/x/atlas/knight_outfit.mesh" size="1024" padding="4">
 *            <Part name="Torso" mesh="game/x/models/torso.mesh"/>
 *            <Part name="Legs" mesh="game/x/models/legs.mesh"/>
 *            <Map name="BaseColor" output="game/x/textures_raw/knight_outfit_basecolor.png">
 *              <Source part="Torso" texture="game/x/textures_raw/torso_basecolor.png"/>
 *              <Source part="Legs" texture="game/x/textures_raw/legs_basecolor.png"/>
 *            </Map>
 *          </OutfitAtlas>
 *          @endcode
 *          배치는 첫 `Map` 의 텍스처 크기로 정하고, 다른 `Map`(법선 · 거칠기 …)은 부품마다 같은 크기여야 한다(같은 배치를 쓴다). 결과 PNG 는 원본 이미지 폴더에 쓰므로
 *          텍스처 임포트(`--import-textures`)가 보통 텍스처처럼 처리한다. 결과 메시는 구간 하나(그룹 이름)이고, 그 구간을 그리는 머티리얼이 아틀라스 텍스처를 가리킨다(손으로 만든다).
 *          0..1 밖 UV(타일링)를 가진 부품은 아틀라스에 앉힐 수 없어 굽기를 거절한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Asset/AssetID.h"

namespace sw::editor
{
    /** @brief 명세의 부품 하나입니다(이름과 메시 경로). */
    struct OutfitAtlasPartSpec
    {
        hashed_string _name{};
        string        _mesh{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 명세의 텍스처 하나입니다(부품과 이미지 경로). */
    struct OutfitAtlasSourceSpec
    {
        hashed_string _part{};
        string        _texture{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 명세의 텍스처 종류 하나입니다(예: BaseColor) — 부품마다 이미지 하나로 아틀라스 이미지 하나를 만든다. */
    struct OutfitAtlasMapSpec
    {
        hashed_string                 _name{};
        string                        _output{};
        vector<OutfitAtlasSourceSpec> _listSource{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 아틀라스 굽기 명세입니다. 수치의 기본값은 이 정의의 초기값이 정본입니다. */
    struct OutfitAtlasSpec
    {
        hashed_string               _group{};
        string                      _outputMesh{};
        int32                       _size{ 1024 };
        int32                       _padding{ 4 };
        vector<OutfitAtlasPartSpec> _listPart{};
        vector<OutfitAtlasMapSpec>  _listMap{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 굽기 결과의 요약입니다. */
    struct OutfitAtlasBakeResult
    {
        uint32         _partCount{ 0 };
        uint32         _vertexCount{ 0 };
        string         _meshPath{};
        vector<string> _listTexturePath{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 아틀라스 굽기입니다(전부 static). */
    struct SW_EDITOR_API OutfitAtlasBaker
    {
        /** @brief 명세 파일의 접미사입니다. */
        static constexpr string_view kExtension = ".outfitatlas.xml";

        /** @brief XML 본문에서 명세를 읽습니다. 모르는 속성 · 원소, 빠진 필수 값, 부품에 없는 이름, 부품마다 텍스처가 하나씩 아닌 `Map` 은 오류입니다. */
        [[nodiscard]] static bool parseSpec( string_view xmlText, string_view sourceName, OutfitAtlasSpec& outSpec );
        /** @brief 리소스 경로의 명세 파일을 읽습니다. */
        [[nodiscard]] static bool loadSpec( const AssetID& assetID, OutfitAtlasSpec& outSpec );
        /**
         * @brief 명세대로 굽습니다 — 메시와 이미지를 읽고, 배치하고, 병합 메시와 아틀라스 이미지를 씁니다. 경로는 리소스 루트 기준입니다.
         * @return 읽을 수 없거나, 아틀라스에 다 앉지 않거나, 이미지 크기가 맞지 않거나, 0..1 밖 UV 가 있거나, 쓸 수 없으면 오류를 남기고 false 입니다(그때는 아무것도 쓰지 않습니다).
         */
        [[nodiscard]] static bool bake( const OutfitAtlasSpec& spec, OutfitAtlasBakeResult& outResult );
    };
} // namespace sw::editor
