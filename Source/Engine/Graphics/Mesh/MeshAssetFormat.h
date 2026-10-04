/**
 * @file MeshAssetFormat.h
 * @brief 메시 에셋 파일(`.mesh`)의 바이너리 형식 — 읽기는 런타임(`MeshCache`), 쓰기는 에디터의 모델 임포터가 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @struct MeshAssetData
     * @brief `.mesh` 하나의 내용 — 정점(인덱스 없는 삼각형 목록)과, 스킨드 메시면 정점마다의 스킨 가중치 · 스켈레톤 본 수입니다.
     */
    struct MeshAssetData
    {
        vector<RHIVertex>      _listVertex;
        vector<MeshSkinVertex> _listSkinVertex;     ///< 비었거나 `_listVertex` 와 같은 길이입니다.
        uint32                 _skinBoneCount{ 0 }; ///< 0 이면 스킨이 없습니다.

        /** @brief 스킨이 있으면 true 입니다. */
        bool hasSkin() const { return _skinBoneCount > 0 && _listSkinVertex.size() == _listVertex.size(); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MeshAssetFormat
     * @brief `.mesh` 파일 하나 = 머리(24 바이트) + 인덱스 없는 삼각형 목록 정점 배열 + (스킨이면) 정점마다 스킨 가중치입니다. 모든 값은 리틀 엔디언입니다.
     * @details 머리: 매직 `SWMS`(4) · 버전(uint32) · 정점 수(uint32, 3 의 배수이고 0 이 아님) · 정점 크기(uint32, `RHIVertex` 의 바이트 수) ·
     *          경계 반지름(float32, 원점에서 가장 먼 정점까지) · 스킨 본 수(uint32, 0 = 스킨 없음). 그 뒤에 정점마다 위치 3 · 노멀 3 · UV 2 · 색 4 개의
     *          float32 가 오고, 스킨 본 수가 0 이 아니면 정점마다 본 번호 4 개(uint16) · 가중치 4 개(float32)가 옵니다(본 번호는 스켈레톤 본 순서이고 본 수보다 작습니다).
     *          읽기는 지금 형식만 받습니다 — 버전 · 정점 크기가 다르거나 파일 길이가 머리와 맞지 않으면 거절합니다(옛 형식 리더는 두지 않고,
     *          형식을 바꾸면 원본에서 다시 임포트합니다).
     */
    struct SW_API MeshAssetFormat
    {
        /** @brief 지금 형식의 버전입니다. 배치를 바꾸면 올리고 모델 원본을 다시 임포트합니다. */
        static constexpr uint32 kVersion = 2;
        /** @brief 머리의 바이트 수입니다. */
        static constexpr uint32 kHeaderSize = 24;
        /** @brief 정점 하나의 바이트 수입니다(float32 12 개). */
        static constexpr uint32 kVertexSize = 48;
        /** @brief 스킨 정점 하나의 바이트 수입니다(uint16 4 개 + float32 4 개). */
        static constexpr uint32 kSkinVertexSize = 24;
        /** @brief 메시 에셋 확장자입니다. */
        static constexpr string_view kExtension = ".mesh";

        /** @brief 경로가 메시 에셋(`.mesh`)을 가리키는지 봅니다. 내장 도형 이름("Cube" 등)은 아닙니다. */
        static bool isMeshAssetPath( string_view path );

        /** @brief 내용을 `.mesh` 바이트로 만듭니다. 경계 반지름은 정점에서 구합니다. */
        static void makeBytes( const MeshAssetData& data, vector<uint8>& outBytes );
        /** @brief 스킨 없는 정점 배열을 `.mesh` 바이트로 만듭니다. */
        static void makeBytes( const vector<RHIVertex>& listVertex, vector<uint8>& outBytes );
        /**
         * @brief `.mesh` 바이트를 읽습니다. 형식이 맞지 않으면(본 번호가 본 수를 넘는 것 포함) false 이고 @p outData 는 비웁니다.
         * @param pOutBoundingRadius 머리의 경계 반지름을 받습니다(선택).
         */
        [[nodiscard]] static bool readFromBytes( const uint8* pData, size_t size, MeshAssetData& outData, float32* pOutBoundingRadius = nullptr );
        /** @brief 정점만 읽습니다(스킨은 버립니다). */
        [[nodiscard]] static bool readFromBytes( const uint8* pData, size_t size, vector<RHIVertex>& outListVertex, float32* pOutBoundingRadius = nullptr );

        /** @brief 내용을 @p path 에 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] static bool saveToFile( string_view path, const MeshAssetData& data );
        /** @brief 스킨 없는 정점 배열을 @p path 에 씁니다. */
        [[nodiscard]] static bool saveToFile( string_view path, const vector<RHIVertex>& listVertex );
        /** @brief 리소스 경로(또는 OS 절대 경로)의 `.mesh` 를 읽습니다. 팩 · 느슨한 파일 모두 `ResourceUtil` 로 읽습니다. */
        [[nodiscard]] static bool loadFromResource( string_view path, MeshAssetData& outData );
        /** @brief 정점만 읽습니다(스킨은 버립니다). */
        [[nodiscard]] static bool loadFromResource( string_view path, vector<RHIVertex>& outListVertex );
    };
} // namespace sw
