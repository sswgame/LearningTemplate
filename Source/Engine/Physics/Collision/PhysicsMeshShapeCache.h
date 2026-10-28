/**
 * @file PhysicsMeshShapeCache.h
 * @brief 볼록 껍질 · 삼각 메시 셰이프를 메시 에셋(`.mesh`)에서 채우는 창구입니다. 경로마다 한 번 읽어 점을 합치고(weld) 나눠 줍니다.
 * @details 셰이프 서술자(`PhysicsShapeDesc3D`)가 `_meshPath` 를 갖고 점을 직접 적지 않았으면, 바디를 만드는 백엔드와 디버그 그리기가 이 캐시에서
 *          점과 인덱스를 받는다. 언리얼이 스태틱 메시의 렌더 정점에서 충돌(`UBodySetup` 의 볼록 · 복잡 충돌)을 만드는 것과 같은 자리다.
 *
 *          **층.** 물리(층 6)가 그래픽스의 메시 에셋 형식(`MeshAssetFormat`, 층 4)으로 `.mesh` 를 직접 읽는다. 메시를 다시 읽거나 비우면
 *          `MeshCache` 가 등록된 무효화 고리(`SW_MESH_INVALIDATION_HOOK`)를 불러 이 표를 비운다 — 그래픽스는 물리를 모른다.
 *
 *          **스레드.** 모든 함수는 어느 스레드에서 불러도 된다(씬 로드 워커가 미리 `acquire` 해 두면 게임 스레드의 바디 생성이 파일을 읽지 않는다).
 *          파일 읽기는 잠금 밖에서 하고, 표만 잠근다. 읽지 못한 경로는 경로마다 한 번만 오류를 남기고 기억한다(디버그 그리기가 프레임마다 다시 읽지 않게) —
 *          `clear` 가 그 기억도 지운다(핫 리로드).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    struct PhysicsShapeDesc3D;

    /** @brief 메시 하나에서 만든 셰이프 기하입니다. 허용 오차(`PositionWelder`) 안의 위치를 한 점으로 합쳤고(점 순서는 처음 본 순서), 퇴화 삼각형(세 점 중 둘이 같다)은 뺐습니다. */
    struct PhysicsMeshShapeData
    {
        vector<float3> _listPoint; ///< 합친 점(메시 로컬). 볼록 껍질은 이것만 쓴다.
        vector<uint32> _listIndex; ///< 삼각형마다 점 번호 셋(삼각 메시).
    };
} // namespace sw

namespace sw
{
    /** @brief 셰이프 하나의 점과 인덱스를 보는 창입니다. 서술자 자신의 것이거나, 메시에서 왔으면 그 데이터를 붙들고 가리킵니다. */
    struct PhysicsShapeGeometry
    {
        span<const float3>                     _listPoint;
        span<const uint32>                     _listIndex;
        shared_ptr<const PhysicsMeshShapeData> _pMeshData; ///< 메시에서 왔을 때만 있다. 창이 살아 있는 동안 데이터를 붙든다.
    };
} // namespace sw

namespace sw
{
    /** @class PhysicsMeshShapeCache @brief 경로별 메시 셰이프 기하 표입니다. 파일 머리말 참고. */
    class SW_API PhysicsMeshShapeCache
    {
    public:
        /**
         * @brief 경로의 기하를 나눠 받습니다. 처음이면 읽어 합치고, 읽을 수 없으면 nullptr 입니다(경로마다 오류 한 번).
         * @details 표는 강한 참조라 `clear` 전까지 남는다(셰이프는 메시가 바뀔 때까지 같은 기하를 쓴다). 둘이 같은 경로를 동시에 읽으면 먼저 넣은 쪽이 남는다.
         */
        static shared_ptr<const PhysicsMeshShapeData> acquire( string_view path );
        /**
         * @brief 셰이프의 점과 인덱스를 받습니다. `_meshPath` 가 있고 점을 직접 적지 않았으면 메시에서, 아니면 서술자 자신의 것입니다.
         * @details 서술자 자신의 것이면 잠그지 않는다. 메시를 읽지 못했으면 빈 창이다.
         */
        static PhysicsShapeGeometry acquireGeometry( const PhysicsShapeDesc3D& shape );
        /** @brief 삼각형 목록(@p listTrianglePosition, 셋씩)의 허용 오차 안 위치를 합쳐(`PositionWelder`) 점과 인덱스로 만듭니다. 셋으로 나누어떨어지지 않는 끝은 버립니다. */
        static void makeShapeData( span<const float3> listTrianglePosition, PhysicsMeshShapeData& outData );

        /** @brief 표와 실패 기억을 비웁니다. 다음 요청은 파일을 새로 읽습니다(이미 만든 바디는 옛 기하 그대로). */
        static void clear();
        /** @brief 표에 든 경로 수입니다. */
        static size_t getCachedCount();
    };
} // namespace sw
