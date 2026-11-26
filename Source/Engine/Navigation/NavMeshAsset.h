/**
 * @file NavMeshAsset.h
 * @brief 쿠킹한 내비메시(`.navmesh`)입니다. 씬 하나의 에이전트 종류마다 타일 바이트를 싣습니다. 타일을 한꺼번에 베이크하는 도우미도 있습니다.
 * @details **형식**(리틀 엔디언)은 머리와 타일 내용으로 나뉜다. 머리는 매직 `SWNV`, 형식 버전, 머리 길이(이 칸까지 포함한 머리 전체), 백엔드 이름, 백엔드 버전, 항목 수,
 *          항목마다 [종류 이름, 베이크 값 해시, 입력 해시, 경계 상자, 타일 수 → 타일마다 (x, z, 내용 안 시작, 길이, 타일 입력 해시),
 *          대략 그래프(클러스터 수 → 클러스터, 간선 수 → 간선 — `NavClusterGraph::write`)] 다. 타일이 곧 내비 데이터 청크다 — 셀과 같은 원점 · 반경으로 타일 단위로 오르내린다. 머리 뒤에 타일 바이트가 타일 순서로 빈틈없이 잇는다.
 *          타일 바이트는 백엔드의 것이고 그대로 `INavMesh::replaceTile` 에 넣는다. 스트리밍 씬은 머리만 읽고(`loadHeadFromAsset`) 타일을 원점 둘레에서 구간으로 올리고 내린다
 *          (Recast/Detour 의 타일 단위 `addTile` · `removeTile`, 언리얼 World Partition 의 내비 데이터 청크 스트리밍).
 *          백엔드 이름이나 버전이 다르면 읽지 않는다(다른 라이브러리로 바꾼 뒤 낡은 쿠킹본을 넣지 않는다). 읽기는 현재 형식만 받는다.
 *
 *          **쿠킹과 런타임 베이크.** 쿠킹(`App --cook-scenes`)은 내비 표면(`NavMeshSurfaceComponent`)이 있는 씬마다 `<씬 이름>.navmesh` 를 쓴다.
 *          배포본은 그것을 팩에서 읽고, Dev 는 쿠킹 폴더를 마운트하지 않으므로 처음 쓸 때 같은 함수(`NavMeshAsset::bakeAllTiles`)로 베이크한다.
 *          두 경로의 타일 바이트는 같다. 읽은 쪽은 입력 해시와 베이크 값 해시가 현재 씬과 테이블에 같을 때만 쓰고, 다르면 경고를 남기고 베이크한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Navigation/INavMeshTileStore.h"
#include "Engine/Navigation/NavClusterGraph.h"
#include "Engine/Serialization/Format/FormatDef.h"

namespace sw
{
    struct NavConvexVolume;

    class NavMeshGeometry;

    /** @brief 쿠킹본 안 타일 하나의 바이트 구간입니다(파일 처음에서 센 시작 · 길이). */
    struct NavTileRange
    {
        uint64 _offset{ 0 };
        uint64 _size{ 0 }; ///< 0 이면 그 타일에 걸을 곳이 없다
    };

    /** @brief 에이전트 종류 하나의 쿠킹한 타일들입니다. */
    struct NavMeshAssetEntry
    {
        hashed_string        _agentType{};
        uint64               _settingsHash{ 0 }; ///< `NavMeshSettings::computeAgentTypeHash`
        uint64               _inputHash{ 0 };    ///< `NavMeshGeometry::computeHash`
        AABB                 _bounds{};
        vector<NavTileData>  _listTile;          ///< 타일 자리와 바이트. 머리만 읽은 쿠킹본(`loadHeadFromAsset`)은 바이트가 비어 있다 — `_listTileRange` 로 읽는다
        vector<NavTileRange> _listTileRange;     ///< 쿠킹본에서 읽었으면 타일마다 파일 안 구간(`_listTile` 과 같은 순서). 쓰기는 보지 않는다
        vector<uint64>       _listTileInputHash; ///< 타일마다 그 베이크 영역의 입력 해시(`NavMeshGeometry::computeRegionHash`, `_listTile` 과 같은 순서). 비었거나 0 이면 모른다 — 런타임이 그 타일을 다시 굽는다
        NavClusterGraph      _clusterGraph;      ///< 모든 타일을 끼운 내비메시의 대략 그래프(쿠킹). 스트리밍 씬이 오르지 않은 구역을 지나는 먼 경로를 정한다(`NavStreamRoute`)
    };
} // namespace sw

namespace sw
{
    struct NavMeshBakeStats;

    /** @class NavMeshAsset @brief 파일 머리말 참고. */
    class SW_API NavMeshAsset
    {
    public:
        // 내비메시 하나를 통째로 베이크거나 쿠킹본에서 채우는 도우미입니다.
        /**
         * @brief 타일 격자의 모든 타일을 워커로 나눠 베이크하고(`engine::runParallel`) 끼웁니다. @p geometry 의 색인은 여기서 짓습니다.
         * @return 실패한 타일이 하나라도 있으면 false 입니다(나머지 타일은 끼운다).
         */
        static bool bakeAllTiles( INavMeshTileStore& navMesh, NavMeshGeometry& geometry, const vector<NavConvexVolume>* pExtraVolume = nullptr,
                                  NavMeshBakeStats* pOutStats = nullptr );
        /** @brief 쿠킹본의 항목을 끼웁니다(격자는 먼저 `initialize` 해 둔다). 하나라도 거절되면 false 입니다. */
        static bool installTiles( INavMeshTileStore& navMesh, const vector<NavTileData>& listTile );
        /** @brief 베이크 경계입니다. 입력 경계를 그대로 씁니다(비었으면 원점의 작은 상자입니다). */
        static AABB computeBakeBounds( const NavMeshGeometry& geometry );

        /** @brief 쿠킹본의 확장자입니다. */
        static constexpr string_view kExtension = ".navmesh";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `NavMeshAsset.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "NavMeshAsset", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };
        /** @brief 형식 버전입니다. 바꾸면 쿠킹본을 다시 만듭니다. */
        static constexpr FormatDef kFormat{ "SWNV" };

        NavMeshAsset();

        /** @brief 씬 경로(`.scene.xml`, `.scene.bin`)에 해당하는 쿠킹본 경로(`<씬 이름>.navmesh`)를 반환합니다. 씬 경로가 아니면 빈 문자열입니다. */
        static string makeCookedPath( string_view scenePath );

        /** @brief 바이트로 씁니다. */
        void writeBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 매직, 형식 버전, 백엔드가 다르거나 잘렸으면 오류를 남기고 false 를 반환하며 내용을 비웁니다. */
        [[nodiscard]] bool readBytes( const uint8* pData, size_t size, string_view sourceLabel );
        /** @brief 리소스 경로의 쿠킹본을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /**
         * @brief 리소스 경로의 쿠킹본에서 머리(항목 · 타일 자리 · 구간)만 구간으로 읽습니다 — 타일 바이트는 읽지 않는다(`hasTileBytes` 가 false).
         * @details 스트리밍 씬의 내비게이션이 쓴다 — 타일은 원점 둘레에서 `NavMeshAssetEntry::_listTileRange` 의 구간으로 비동기로 읽는다(`AssetIO::readBinaryAssetRangeAsync`).
         */
        [[nodiscard]] bool loadHeadFromAsset( const AssetID& assetID );
        /** @brief 타일 바이트가 메모리에 있으면 true 입니다(`readBytes` · `setEntry` 로 채웠다). 머리만 읽었으면 false 입니다. */
        bool hasTileBytes() const { return _bHeadOnly == SW_FALSE; }
        /** @brief 절대 경로에 씁니다(폴더가 없으면 만듭니다). */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;

        /** @brief 종류 이름의 항목입니다. 없으면 nullptr 입니다. */
        const NavMeshAssetEntry* findEntry( const hashed_string& agentType ) const;
        /** @brief 항목을 더하거나(같은 종류면) 바꿉니다. */
        void setEntry( NavMeshAssetEntry entry );

        const vector<NavMeshAssetEntry>& getEntries() const { return _listEntry; }

    private:
        /**
         * @brief 머리 @p pHead(@p headSize 바이트, 고정 칸 포함)를 풉니다. 타일 구간은 내용 길이 @p contentSize 안에 빈틈없이 놓여야 합니다.
         * @param pContent 있으면 내용 바이트(머리 바로 뒤)에서 타일 바이트를 옮겨 담습니다. 없으면 구간만 둡니다.
         */
        [[nodiscard]] bool readHead( const uint8* pHead, size_t headSize, uint64 contentSize, const uint8* pContent, string_view sourceLabel );

    private:
        vector<NavMeshAssetEntry> _listEntry;
        uint8                     _bHeadOnly : 1; ///< 머리만 읽었다(`loadHeadFromAsset`) — 타일 바이트가 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 베이크 한 번의 측정값입니다(진단과 측정용). */
    struct NavMeshBakeStats
    {
        float64 _milliseconds{ 0.0 };
        uint32  _tileCount{ 0 };       ///< 베이크한 타일 위치 수다.
        uint32  _filledTileCount{ 0 }; ///< 걸을 곳이 있는 타일 수다.
        uint32  _failedTileCount{ 0 };
        uint32  _polygonCount{ 0 };
    };
} // namespace sw
