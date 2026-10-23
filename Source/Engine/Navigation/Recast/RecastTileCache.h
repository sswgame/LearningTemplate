/**
 * @file RecastTileCache.h
 * @brief Detour TileCache 로 움직이는 장애물을 처리하는 `RecastNavMesh` 의 부품입니다(`NavObstacleMode::TileCache`). 라이브러리 헤더는 .cpp 에서만 include 합니다.
 * @details 정적 기하는 타일마다 높이 층(`rcHeightfieldLayer`)으로 한 번만 베이크하고 층을 압축해 둔다. 장애물(원기둥 · 요 회전 상자)이 생기거나 움직이면 그 층 위에서만
 *          폴리곤을 다시 짓는다 — 복셀화 · 깎기 · 영역 나누기를 다시 하지 않아 다시 베이크보다 훨씬 싸다(Recast 샘플 `Sample_TempObstacles`, 언리얼 동적 장애물).
 *          타일 바이트(`NavTileData::_bytes`)는 이 모드에서 "층 묶음" 이다 — 층 수만큼의 압축 층과 그 타일에서 시작하는 오프 메시 링크를 싣는다.
 *          그래서 베이크 · 쿠킹 · 끼우기 길(`INavMesh::bakeTile` · `replaceTile` · `collectTiles`)은 그대로이고, 바이트의 뜻만 모드가 정한다.
 *          층은 압축하지 않고 그대로 싣는다(저장 압축기) — 층 하나는 타일 크기² 바이트 셋이라 작고, 압축 코덱을 하나 더 들이지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavigationTypes.h"

struct rcCompactHeightfield;
struct rcConfig;

class dtNavMesh;
class dtTileCache;
class rcContext;

namespace sw
{
    struct NavAgentTypeDef;

    /** @class RecastTileCache @brief 파일 머리말 참고. 게임 스레드에서만 다룹니다(층 묶음 만들기 `makeLayerPack` 만 워커에서 돕니다). */
    class RecastTileCache
    {
    public:
        /** @brief 타일 하나가 담는 높이 층의 상한입니다(값은 `NavigationConstant::kMaxCacheLayerCount`). */
        static constexpr uint32 kMaxLayerCount = NavigationConstant::kMaxCacheLayerCount;

        RecastTileCache();
        ~RecastTileCache();

        RecastTileCache( const RecastTileCache& )            = delete;
        RecastTileCache& operator=( const RecastTileCache& ) = delete;

        /** @brief 격자와 몸 값으로 캐시를 세웁니다. 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool initialize( const NavAgentTypeDef& agentType, const NavTileGrid& grid, float32 minY, float32 maxY );
        /** @brief 층 · 장애물 · 묶음을 모두 놓습니다. */
        void shutdown();

        /**
         * @brief 압축 높이장(영역 칠하기까지 끝난 것)을 층으로 나눠 층 묶음 바이트를 만듭니다. 입력만 읽으므로 워커에서 돌아도 됩니다.
         * @param listLink 시작점이 이 타일에 있는 오프 메시 링크입니다(층에서 지을 때마다 다시 싣는다).
         * @return 층을 나누거나 압축하다 실패하면 false 입니다. 걸을 곳이 없으면 바이트가 빈 채로 true 입니다.
         */
        [[nodiscard]] static bool makeLayerPack( rcContext& context, rcCompactHeightfield& compact, const rcConfig& config, int32 tileX, int32 tileZ,
                                                 const vector<NavOffMeshLink>& listLink, vector<uint8>& outBytes );

        /** @brief 층 묶음을 끼웁니다 — 그 자리의 옛 층 · 내비메시 타일을 빼고, 새 층을 넣고, 지금 장애물로 내비메시 타일을 짓습니다. 바이트가 비면 빼기만 합니다. */
        [[nodiscard]] bool installPack( const NavTileData& pack, dtNavMesh& navMesh );
        /** @brief 끼운 층 묶음을 모두 베껴 냅니다(쿠킹). */
        void collectPacks( vector<NavTileData>& outListPack ) const;

        /** @brief 장애물을 넣습니다(다음 `update` 가 닿은 타일을 다시 짓는다). 캐시가 가득하면 오류를 남기고 0 입니다. */
        NavObstacleRef addObstacle( const NavCacheObstacleDesc& desc );
        /** @brief 장애물을 뺍니다. 모르는 번호는 할 일이 없습니다. */
        void removeObstacle( NavObstacleRef obstacle );
        /**
         * @brief 쌓인 장애물 요청을 처리하고 닿은 타일을 층에서 다시 짓습니다. 질의가 없는 동안(게임 스레드 · 씬 틱 밖)만 부릅니다.
         * @return 내비메시 타일을 하나라도 다시 지었으면 true 입니다(링크 막기 · 경로 캐시를 다시 맞춘다).
         */
        [[nodiscard]] bool update( float32 deltaSeconds, dtNavMesh& navMesh );
        /** @brief 지금 든 장애물 수입니다(진단 · 테스트). */
        uint32 getObstacleCount() const;

    private:
        struct Parts;
        unique_ptr<Parts>   _pParts;     ///< 할당기 · 압축기 · 메시 처리기(라이브러리 타입 — .cpp 에만 있다)
        dtTileCache*        _pTileCache; ///< 층과 장애물
        vector<NavTileData> _listPack;   ///< 끼운 층 묶음(쿠킹 · 메시 처리기의 링크 찾기)
    };
} // namespace sw
