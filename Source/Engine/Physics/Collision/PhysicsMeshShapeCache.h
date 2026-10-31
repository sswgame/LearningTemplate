/**
 * @file PhysicsMeshShapeCache.h
 * @brief 볼록 껍질 · 삼각 메시 셰이프를 메시 에셋(`.mesh`)에서 채우는 창구입니다. 경로마다 한 번 읽어 점을 합치고(weld) 나눠 줍니다.
 * @details 셰이프 서술자(`PhysicsShapeDesc3D`)가 `_meshPath` 를 갖고 점을 직접 적지 않았으면, 바디를 만드는 백엔드와 디버그 그리기가 이 캐시에서
 *          점과 인덱스를 받는다. 언리얼이 스태틱 메시의 렌더 정점에서 충돌(`UBodySetup` 의 볼록 · 복잡 충돌)을 만드는 것과 같은 자리다.
 *
 *          **층.** 물리(층 6)가 그래픽스의 메시 에셋 형식(`MeshAssetFormat`, 층 4)으로 `.mesh` 를 직접 읽는다. 메시를 다시 읽거나 비우면
 *          `MeshCache` 가 엔진 범위의 `IMeshDerivedCache`(이 표의 멤버가 그 타입으로 걸린다)를 불러 이 표를 비운다 — 그래픽스는 물리를 모른다.
 *
 *          **소유.** 표는 서비스 범위의 서브시스템 `PhysicsMeshShapeTable` 이 갖고(`EngineSubsystemSet`), 창구(`PhysicsMeshShapeCache`)는 서비스 표의 칸으로 찾는다.
 *          칸이 비면(엔진 없는 도구 · 테스트) 메시 셰이프를 만들지 않는다. 예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다.
 *
 *          **스레드.** 모든 함수는 어느 스레드에서 불러도 된다(씬 로드 워커가 미리 `acquire` 해 두면 게임 스레드의 바디 생성이 파일을 읽지 않는다).
 *          파일 읽기는 잠금 밖에서 하고, 표만 잠근다. 읽지 못한 경로는 경로마다 한 번만 오류를 남기고 기억한다(디버그 그리기가 프레임마다 다시 읽지 않게) —
 *          `clear` 가 그 기억도 지운다(핫 리로드).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Graphics/Mesh/IMeshDerivedCache.h"

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
    };
} // namespace sw

namespace sw
{
    /**
     * @class PhysicsMeshShapeTable
     * @brief 경로 → 메시 셰이프 기하 표와 읽지 못한 경로 기억입니다(서비스 범위의 서브시스템). `PhysicsMeshShapeCache` 창구만 부릅니다.
     * @details **상한은 두지 않습니다.** 표의 크기는 경로 수이고, 경로는 물리 셰이프 서술자가 적은 `.mesh` 리소스 경로뿐이라 콘텐츠에 있는 메시 파일 수가 위쪽 한계입니다
     *          (런타임에 만들어지는 경로 · 키가 없고, 같은 경로는 한 항목). 씬을 바꿔도 같은 메시를 다시 읽지 않으려고 남깁니다. 비우는 때는 메시가 다시 읽힐 때와
     *          메시 캐시를 비울 때(`IMeshDerivedCache`)뿐이다. 크기는 `[CacheStats] PhysicsMeshShape` 줄의 항목 수로 본다.
     *          **읽지 못한 경로는 영원히 믿지 않는다.** 기억한 때의 `FileUtil::getCreationEpoch` 가 지금과 같은 동안만 믿고, 바뀌었으면 다시 읽어 본다.
     *          언리얼 `UBodySetup` 의 쿠킹된 충돌 데이터를 물리 쪽이 갖는 자리.
     */
    class SW_API PhysicsMeshShapeTable final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "PhysicsMeshShapeTable";

        ~PhysicsMeshShapeTable() override;

        // ISubsystem — 세울 것이 없다(처음 요청한 경로부터 채운다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 메시 캐시가 찾는 자리(`IMeshDerivedCache`)입니다. 기동이 이 멤버를 그 타입으로 건다(`SubsystemScope::exposeMember`). */
        IMeshDerivedCache& getMeshDerivedCache() { return _meshDerivedCache; }

        /** @brief `PhysicsMeshShapeCache::acquire` 의 본문입니다. */
        shared_ptr<const PhysicsMeshShapeData> acquire( string_view path ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 표와 실패 기억을 비웁니다. 다음 요청은 파일을 새로 읽습니다(이미 만든 바디는 옛 기하 그대로). */
        void clear() SW_EXCLUDES_LOCK( _mutex );
        /** @brief 표에 든 경로 수입니다. */
        size_t getCachedCount() SW_EXCLUDES_LOCK( _mutex );

    private:
        /**
         * @class MeshDerivedCache
         * @brief 메시가 다시 읽히거나 비워지면 표와 실패 기억을 비우는 `IMeshDerivedCache` 입니다. 표가 그 인터페이스를 상속하지 않고 이 멤버를 건다(수명은 표와 같다).
         */
        class MeshDerivedCache final : public IMeshDerivedCache
        {
        public:
            explicit MeshDerivedCache( PhysicsMeshShapeTable& owner )
                : _owner{ owner }
            {
            }

            void onMeshesInvalidated() override { _owner.clear(); }

        private:
            PhysicsMeshShapeTable& _owner;
        };

    private:
        MeshDerivedCache                                                             _meshDerivedCache{ *this };
        mutex                                                                        _mutex;
        unordered_map<string, shared_ptr<const PhysicsMeshShapeData>> _mapPathToData SW_GUARDED_BY( _mutex );
        unordered_map<string, uint64> _mapFailedPathToEpoch                          SW_GUARDED_BY( _mutex ); ///< 읽지 못한 경로 → 그때의 만든 횟수
    };
} // namespace sw
