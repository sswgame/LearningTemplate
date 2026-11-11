/**
 * @file FractureAsset.h
 * @brief 미리 쪼갠 메시(`.fracture`)입니다. 조각마다 형상(삼각형 목록, 겉면과 안쪽 면 슬롯), 볼록 껍질 점, 무게 중심, 부피를 담고, 묶음 계층과 연결 그래프도 담습니다.
 * @details 모델 임포트가 `.mesh` 옆에 쓴다(`<x>/models/<y>.fracture`). 런타임(`FractureComponent`)은 이 파일을 그대로 읽어 조각 바디와 스킨 조각 메시를 만든다.
 *          파쇄 계산은 쿠킹할 때 한 번만 한다. 언리얼 Chaos 의 Geometry Collection 에셋에 해당한다.
 *
 *          **형상**은 `.mesh` 와 같은 인덱스 없는 삼각형 목록(`RHIVertex`)이고, 조각 순서(잎 번호)대로 이어 붙어 있다.
 *          삼각형마다 슬롯 번호가 있다. 0 은 원래 겉면이고, 1 은 자른 안쪽 면이다(머티리얼이 따로 있다). 껍질 점은 조각 무게 중심 기준이다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Destruction/Fracture/FractureGraph.h"
#include "Engine/Graphics/Mesh/MeshVertex.h"

namespace sw
{
    /** @brief 삼각형의 머티리얼 슬롯입니다. */
    enum class FractureSurfaceSlot : uint8
    {
        Outer    = 0, ///< 원래 메시의 겉면이다.
        Interior = 1, ///< 쪼개면서 생긴 안쪽 면이다.
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 조각(잎) 하나의 데이터 구간입니다. */
    struct FracturePiece
    {
        float3 _boundsMin{}; ///< 메시 공간 경계다.
        float3 _boundsMax{};
        uint32 _firstVertex{ 0 };    ///< `FractureAsset::_listVertex` 안의 첫 정점이다(3 의 배수).
        uint32 _vertexCount{ 0 };    ///< 정점 수다(3 의 배수).
        uint32 _firstHullPoint{ 0 }; ///< `FractureAsset::_listHullPoint` 안의 첫 점이다.
        uint32 _hullPointCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief `.fracture` 하나의 내용입니다. 파일 머리말을 참고하세요. */
    struct SW_API FractureAsset
    {
        friend struct AssetTypeLoader;

        /** @brief 현재 형식의 버전입니다. 레이아웃을 바꾸면 올리고 모델을 다시 임포트해야 합니다. */
        static constexpr uint32 kVersion = 1;
        /** @brief 확장자입니다. */
        static constexpr string_view kExtension = ".fracture";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `FractureAsset.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "FractureAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        FractureGraph         _graph;
        vector<FracturePiece> _listPiece;        ///< 잎마다 하나다.
        vector<RHIVertex>     _listVertex;       ///< 모든 조각의 삼각형 목록이다(조각 순서).
        vector<uint8>         _listTriangleSlot; ///< 삼각형마다 `FractureSurfaceSlot` 값이다.
        vector<float3>        _listHullPoint;    ///< 조각 무게 중심 기준 볼록 껍질 점이다.
        float3                _boundsMin{};      ///< 전체 경계다(메시 공간).
        float3                _boundsMax{};
        uint64                _seed{ 0 }; ///< 쪼갤 때 쓴 시드다. 같은 원본, 규칙, 시드면 같은 조각이 나온다.

        /** @brief 조각 수(잎 수)입니다. */
        uint32 getPieceCount() const { return static_cast<uint32>( _listPiece.size() ); }
        /** @brief 조각의 정점들입니다. */
        vector_reference<const RHIVertex> getPieceVertices( uint32 piece ) const;
        /** @brief 조각의 껍질 점들(무게 중심 기준)입니다. */
        vector_reference<const float3> getPieceHull( uint32 piece ) const;
        /** @brief 슬롯 하나의 삼각형 수를 반환합니다. 그 슬롯의 메시를 만들 때 씁니다. */
        uint32 countTriangles( FractureSurfaceSlot slot ) const;
        /** @brief 구조가 맞는지(그래프, 구간, 슬롯 길이) 검사합니다. 틀리면 그 까닭입니다. */
        [[nodiscard]] expected<void> validate() const;
        /** @brief 비웁니다. */
        void clear();

        /** @brief 바이트로 만듭니다(리틀 엔디언). */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 매직, 버전, 길이, 구조가 틀리면 false 를 반환하고 내용을 비웁니다. */
        [[nodiscard]] bool readFromBytes( const uint8* pData, size_t size );
        /** @brief 파일로 씁니다. 부모 폴더가 없으면 만듭니다. */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /** @brief 메시 경로 옆의 파쇄 경로입니다(`a/b.mesh` → `a/b.fracture`). */
        static string makePathForMesh( string_view meshPath );
        /** @brief 곁 파일 규칙(`SW_ASSET_COMPANION`)입니다. 메시(`.mesh`)면 `makePathForMesh`, 아니면 빈 글 — 팩 쿠킹이 담은 메시 옆의 파쇄를 함께 담는다(`FractureComponent` 가 칸이 비면 이 이름으로 찾는다). */
        static string makeCompanionPath( string_view assetID );

    private:
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<FractureAsset>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
    };

    /** @brief 이 종류의 공유 캐시입니다 — 인스턴스화는 `FractureComponent.cpp` 의 정의 줄이 한다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_API_EXTERN_TEMPLATE, FractureAsset );
} // namespace sw
