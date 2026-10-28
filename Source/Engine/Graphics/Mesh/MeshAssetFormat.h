/**
 * @file MeshAssetFormat.h
 * @brief 메시 에셋 파일(`.mesh`)의 바이너리 형식입니다. 런타임(`MeshCache`)이 읽고, 에디터의 모델 임포터가 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @struct MeshAssetData
     * @brief `.mesh` 하나의 내용입니다. 정점(인덱스 없는 삼각형 목록)과, 스킨드 메시면 정점별 스킨 가중치와 스켈레톤 본 수를 담습니다.
     */
    struct MeshAssetData
    {
        vector<RHIVertex>       _listVertex;
        vector<MeshSkinVertex>  _listSkinVertex;     ///< 비었거나 `_listVertex` 와 같은 길이입니다.
        vector<MeshMorphTarget> _listMorphTarget;    ///< 모프 타깃(블렌드 셰이프). 없으면 비어 있습니다
        vector<MeshSection>     _listSection;        ///< 구간(머티리얼 구간 · 시점 가시성). 없으면 비어 있습니다(`SECT` 덩어리)
        vector<MeshLODLevel>    _listLOD;            ///< LOD 1 이상(낮은 상세 단계). 없으면 비어 있습니다(`LODS` 덩어리)
        uint32                  _skinBoneCount{ 0 }; ///< 0 이면 스킨이 없습니다.

        /** @brief 스킨이 있으면 true 입니다. */
        bool hasSkin() const { return _skinBoneCount > 0 && _listSkinVertex.size() == _listVertex.size(); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MeshAssetFormat
     * @brief `.mesh` 파일은 헤더(40바이트), 인덱스 없는 삼각형 목록 정점 배열, (스킨이 있으면) 정점별 스킨 가중치로 이루어집니다. 모든 값은 리틀 엔디언입니다.
     * @details 헤더는 다음 순서입니다. 매직 `SWMS`(4바이트), 버전(uint32), 정점 수(uint32, 3 의 배수이고 0 이 아님), 정점 크기(uint32, 압축 정점의 바이트 수 32),
     *          경계 반지름(float32, 원점에서 가장 먼 정점까지), 스킨 본 수(uint32, 0 이면 스킨 없음), UV 경계(float32 4 개 — 최소 u · v, 최대 u · v)입니다.
     *          UV 경계는 기본 정점과 모든 LOD 의 UV 를 덮고, 정점의 UV 는 이 사각형 안으로 정규화한 `unorm16x2` 입니다(오차는 경계 폭의 1/65535).
     *          그 뒤에 정점마다 32 바이트가 옵니다: 위치 float32 3개(12), 노멀 `OctahedralNormal`(4), 탄젠트 `OctahedralNormal`(4 — xyz 는 쓰기가 `Mesh::computeTangents` 로 구한다),
     *          UV `unorm16x2`(4), 색 `unorm8x4`(4 — 0..1 밖은 잘린다), 플래그 uint32(4 — 비트 0 이 켜지면 바이탄젠트 부호가 -1, 나머지 비트는 0)입니다.
     *          법선 · 탄젠트는 풀면 단위 길이이고 오차가 1e-4 라디안 아래라 위치와 달리 비트까지 같게 돌아오지 않습니다. 스킨 본 수가 0 이 아니면 정점마다 본 번호 4개(uint16)와 가중치 4개(float32)가 옵니다. 본 번호는 스켈레톤 본 순서이고 본 수보다 작습니다.
     *          그 뒤에 선택 청크가 0개 이상 옵니다. 청크는 이름(4바이트), 길이(uint32), 내용으로 이루어집니다. 지금 아는 청크는 `MRPH`(모프 타깃), `SECT`(구간 —
     *          구간 수, 구간마다 이름 길이와 이름, 첫 정점 · 정점 수 · 시점 가시성의 uint32), `LODS`(낮은 상세 단계 — 단계 수, 단계마다 화면 크기 임계값 float32 ·
     *          정점 수 uint32 · 그 수만큼의 정점(머리 뒤 정점과 같은 32 바이트 압축 배치, 탄젠트는 단계마다 따로 구한다))과, 스킨드 메시의 `LSKN`(낮은 상세 단계의 스킨 —
     *          `LODS` 뒤에 오고 단계 수, 단계마다 정점 수 uint32 · 그 수만큼의 스킨 정점(머리 뒤 스킨과 같은 24 바이트 배치))이고,
     *          모르는 청크는 거절합니다. `MRPH` 는 타깃 수, 타깃별 이름 길이와 이름, 차이 수, 차이 목록(정점 번호 uint32, 위치 차이 3개와 노멀 차이 3개의 float32)을 담습니다.
     *          LOD 가 없는 파일은 LOD 0 하나입니다.
     *          읽기는 지금 형식만 받습니다. 버전이나 정점 크기가 다르거나 파일 길이가 헤더 및 청크와 맞지 않으면 거절합니다. 이전 형식 리더는 두지 않고,
     *          형식을 바꾸면 원본에서 다시 임포트합니다.
     */
    struct SW_API MeshAssetFormat
    {
        /** @brief 지금 형식의 버전입니다. 레이아웃을 바꾸면 올리고 모델 원본을 다시 임포트합니다. */
        static constexpr uint32 kVersion = 4;
        /** @brief 헤더의 바이트 수입니다(고정 필드 24 + UV 경계 float32 4 개). */
        static constexpr uint32 kHeaderSize = 40;
        /** @brief 파일의 정점 하나의 바이트 수입니다(압축 배치 — 메모리의 `RHIVertex` 는 64 바이트 float32 16 개이고 읽을 때 풉니다). */
        static constexpr uint32 kVertexSize = 32;
        /** @brief 스킨 정점 하나의 바이트 수입니다(uint16 4 개 + float32 4 개). */
        static constexpr uint32 kSkinVertexSize = 24;
        /** @brief 메시 에셋 확장자입니다. */
        static constexpr string_view kExtension = ".mesh";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `MeshAssetFormat.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "MeshAssetFormat", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 경로가 메시 에셋(`.mesh`)을 가리키는지 확인합니다. 내장 도형 이름("Cube" 등)은 해당하지 않습니다. */
        static bool isMeshAssetPath( string_view path );

        /** @brief 내용을 `.mesh` 바이트로 만듭니다. 경계 반지름과 탄젠트는 정점에서 구합니다(받은 탄젠트는 쓰지 않습니다). */
        static void makeBytes( const MeshAssetData& data, vector<uint8>& outBytes );
        /** @brief 스킨 없는 정점 배열을 `.mesh` 바이트로 만듭니다. */
        static void makeBytes( const vector<RHIVertex>& listVertex, vector<uint8>& outBytes );
        /**
         * @brief `.mesh` 바이트를 읽습니다. 형식이 맞지 않으면(본 번호가 본 수를 넘는 것, 길이 1 이 아니거나 부호가 ±1 이 아닌 탄젠트 포함) false 이고 @p outData 는 비웁니다.
         * @param pOutBoundingRadius 머리의 경계 반지름을 받습니다(선택).
         */
        [[nodiscard]] static bool readFromBytes( const uint8* pData, size_t size, MeshAssetData& outData, float32* pOutBoundingRadius = nullptr );
        /** @brief 정점만 읽습니다(스킨은 버립니다). */
        [[nodiscard]] static bool readFromBytes( const uint8* pData, size_t size, vector<RHIVertex>& outListVertex, float32* pOutBoundingRadius = nullptr );

        /** @brief 내용을 @p path 에 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] static bool saveToFile( string_view path, const MeshAssetData& data );
        /** @brief 스킨 없는 정점 배열을 @p path 에 씁니다. */
        [[nodiscard]] static bool saveToFile( string_view path, const vector<RHIVertex>& listVertex );
        /** @brief 리소스 경로(또는 OS 절대 경로)의 `.mesh` 를 읽습니다. 팩 파일과 느슨한 파일 모두 `AssetPaths` 로 읽습니다. */
        [[nodiscard]] static bool loadFromAsset( const AssetID& assetID, MeshAssetData& outData );
        /** @brief 정점만 읽습니다(스킨은 버립니다). */
        [[nodiscard]] static bool loadFromAsset( const AssetID& assetID, vector<RHIVertex>& outListVertex );
    };
} // namespace sw
