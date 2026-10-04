/**
 * @file NavMeshSettings.h
 * @brief 사람이 고치는 내비게이션 표 — 에이전트 종류(몸 크기 · 경사 · 계단 · 셀 크기 · 타일) · 영역(이름 · 비용) · 군중 · 재베이크 예산입니다.
 * @details 기본 표는 `engine/navigation/navmeshsettings.xml` 입니다. 코드는 이름으로만 고릅니다(`findAgentType` · `findAreaIndex`).
 *          모르는 키 · 겹친 이름 · 범위를 벗어난 값은 읽을 때 오류이고, 영역이 하나도 없으면 `Default` 하나를 채웁니다.
 *          에이전트 종류마다 내비메시가 하나씩 따로 베이크해집니다(언리얼 `SupportedAgents` · 유니티 Agent Type).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 에이전트 종류 하나 — 이 몸이 걸을 내비메시를 베이크하는 값입니다.
     * @code
     *     <NavAgentTypeDef _name="Humanoid" _radius="0.4" _height="1.9" _maxClimb="0.45" _maxSlope="45" _cellSize="0.2" _cellHeight="0.1" _tileSize="48" />
     * @endcode
     */
    REFLECT()
    struct SW_API NavAgentTypeDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Name agents and surfaces pick it by" )
        hashed_string _name{};
        PROPERTY( Min = 0.0, Tooltip = "Body radius; walls are eroded by this much", Meta = "Units=m" )
        float32 _radius{ 0.4f };
        PROPERTY( Min = 0.0, Tooltip = "Body height; lower ceilings are not walkable", Meta = "Units=m" )
        float32 _height{ 2.0f };
        PROPERTY( Min = 0.0, Tooltip = "Highest step the body walks up", Meta = "Units=m" )
        float32 _maxClimb{ 0.4f };
        PROPERTY( Min = 0.0, Max = 89.0, Tooltip = "Steepest walkable slope", Meta = "Units=deg" )
        float32 _maxSlope{ 45.0f };
        PROPERTY( Min = 0.01, Tooltip = "Voxel size on XZ (smaller is more exact and slower to bake)", Meta = "Units=m" )
        float32 _cellSize{ 0.2f };
        PROPERTY( Min = 0.01, Tooltip = "Voxel size on Y", Meta = "Units=m" )
        float32 _cellHeight{ 0.1f };
        PROPERTY( Min = 8, Max = 256, Tooltip = "Tile edge in cells; a change rebakes one tile" )
        uint32 _tileSize{ 48 };
        PROPERTY( Min = 0, Tooltip = "Islands smaller than this many cells (as a square side) are dropped" )
        uint32 _minRegionSize{ 4 };
        PROPERTY( Min = 0, Tooltip = "Regions smaller than this many cells (as a square side) merge into neighbours" )
        uint32 _mergeRegionSize{ 16 };
        PROPERTY( Min = 0.0, Tooltip = "Longest polygon edge along walls (0 = no limit)", Meta = "Units=m" )
        float32 _maxEdgeLength{ 12.0f };
        PROPERTY( Min = 0.1, Tooltip = "How far the simplified outline may stray from the voxels, in cells" )
        float32 _maxEdgeError{ 1.3f };
        PROPERTY( Min = 0.0, Tooltip = "Height detail sample spacing in cells (0 = no detail)" )
        float32 _detailSampleDistance{ 6.0f };
        PROPERTY( Min = 0.0, Tooltip = "Height detail error in cell heights" )
        float32 _detailSampleMaxError{ 1.0f };
        PROPERTY( Min = 1, Tooltip = "Agents one crowd of this type holds" )
        uint32 _maxCrowdAgentCount{ 512 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 영역 하나 — 이름과 지날 때의 비용 배율입니다. 표의 순서가 영역 번호(0..15)이고 0 번이 보통 땅입니다.
     * @code
     *     <NavAreaDef _name="Mud" _cost="4" />
     * @endcode
     */
    REFLECT()
    struct SW_API NavAreaDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Area name" )
        hashed_string _name{};
        PROPERTY( Min = 1.0, Tooltip = "Path cost multiplier per metre (1 = plain ground)" )
        float32 _cost{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 내비게이션 표 하나입니다. 씬의 내비게이션(`SceneNavigation`)이 처음 쓸 때 읽습니다.
     * @details 쿠킹본(`.navmesh`)은 베이크에 들어간 값의 해시(`computeAgentTypeHash`)를 함께 적어 두고, 표가 바뀌면 런타임이 다시 베이크합니다.
     */
    REFLECT()
    struct SW_API NavMeshSettings
    {
        REFLECT_BODY();

        /** @brief 기본 표의 리소스 경로입니다. */
        static constexpr string_view kResourcePath = "engine/navigation/navmeshsettings.xml";

        PROPERTY( Tooltip = "Agent kinds; each bakes its own navmesh" )
        vector<NavAgentTypeDef> _listAgentType;
        PROPERTY( Tooltip = "Areas in index order (at most 16); index 0 is plain ground" )
        vector<NavAreaDef> _listArea;
        PROPERTY( Tooltip = "Agent kind used when an agent or surface names none (empty = the first)" )
        hashed_string _defaultAgentType{};
        PROPERTY( Min = 1, Tooltip = "Tile rebakes running on workers at once; more wait for the next frame" )
        uint32 _maxConcurrentTileBakeCount{ 4 };
        PROPERTY( Min = 0.0, Tooltip = "An obstacle has to move this far before its tiles rebake", Meta = "Units=m" )
        float32 _obstacleMoveThreshold{ 0.25f };

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면(파일 없음 · 모르는 이름 · 겹친 이름 · 범위 밖) 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief XML 문자열을 읽고 검사합니다(시험용). */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText );
        /** @brief 이름이 겹치지 않고 · 영역이 16 개 이하이고 · 값이 범위 안이고 · 기본 종류가 있는 이름인지 봅니다. 어긋나면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validate() const;
        /** @brief 영역 · 에이전트 종류가 비어 있으면 `Default` 하나씩을 채웁니다. */
        void ensureDefaults();

        /** @brief 이름의 에이전트 종류입니다. 이름이 비면 기본 종류입니다. 없으면 nullptr 입니다. */
        const NavAgentTypeDef* findAgentType( const hashed_string& name ) const;
        /** @brief 이름의 영역 번호입니다. 없으면 false 입니다. */
        [[nodiscard]] bool findAreaIndex( const hashed_string& name, uint8& outIndex ) const;
        /** @brief 표의 영역 비용으로 채운 기본 거름입니다. */
        NavQueryFilter makeDefaultFilter() const;
        /** @brief 에이전트 종류 하나를 베이크하는 값의 해시입니다(쿠킹본이 낡았는지 본다). */
        static uint64 computeAgentTypeHash( const NavAgentTypeDef& agentType );
    };
} // namespace sw
