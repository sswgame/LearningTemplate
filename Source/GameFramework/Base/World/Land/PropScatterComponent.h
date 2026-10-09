/**
 * @file PropScatterComponent.h
 * @brief 장식(나무 · 꽃 · 바위) 흩뿌리기 — 씨앗 고정 배치를 영역 가장자리 · 안쪽 격자 · 배치 규칙(밀도 · 최소 거리 · 경사 · 높이 · 레이어 필터)으로 두고,
 *        제외 원은 비웁니다. 규칙 모드는 Engine 의 배치 코어(`PlacementScatter`)를 쓰고 영역 아래 지형(`TerrainComponent`)을 표면으로 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Placement/PlacementRule.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 흩뿌릴 자리의 모양입니다. */
    ENUM()
    enum class PropScatterMode : uint8
    {
        Edge = 0, ///< 영역 네 변을 따라 한 줄(잔디 가장자리의 나무)
        Fill,     ///< 영역 안쪽 격자(숲 · 꽃밭)
        Rules,    ///< 배치 규칙(`_rule`) — 밀도 · 최소 거리 · 경사 · 높이 · 레이어 필터, 영역 아래 지형 위에
    };
} // namespace sw

namespace sw
{
    /** @brief 고를 수 있는 모델 하나 — 메시 경로와 뽑힐 비중입니다. */
    REFLECT()
    struct SW_GF_API PropScatterModel
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, AssetType = "Mesh" )
        string _meshId{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 아무것도 놓지 않는 원(땅 위 — y 는 보지 않는다)입니다. 정문 앞 길 · 건물 자리를 비운다. */
    REFLECT()
    struct SW_GF_API PropScatterExclusion
    {
        REFLECT_BODY();

        PROPERTY( Units = m )
        float3 _center{};
        PROPERTY( Min = 0.0, Units = m )
        float32 _radius{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 흩뿌리기의 입력입니다(컴포넌트의 PROPERTY 를 모은 것 — 계산은 씬 없이 시험한다). */
    struct PropScatterParams
    {
        vector<PropScatterModel>     _listModel{};
        vector<PropScatterExclusion> _listExclusion{};
        float3                       _regionMin{};
        float3                       _regionMax{};
        float32                      _spacing{ 9.0f };
        float32                      _alongJitter{ 3.0f };  ///< 줄을 따라 흔드는 폭(m)
        float32                      _inwardJitter{ 4.0f }; ///< 변에서 안쪽으로 흔드는 폭(m) — 안쪽 모드는 두 축 모두 이 폭
        float32                      _scaleMin{ 1.0f };
        float32                      _scaleMax{ 1.0f };
        uint32                       _seed{ 1u };
        PropScatterMode              _mode{ PropScatterMode::Edge };
        PlacementRule                _rule{}; ///< 규칙 모드의 규칙 — 씨앗은 `_seed` 가 아니라 이 안의 것이다
    };
} // namespace sw

namespace sw
{
    /** @brief 놓을 것 하나입니다. */
    struct PropScatterPlacement
    {
        float3  _position{};
        float32 _scale{ 1.0f };
        float32 _yaw{ 0.0f }; ///< 라디안
        int32   _modelIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PropScatterMath
     * @brief 배치 계산입니다. 같은 입력이면 같은 배치입니다(xorshift32 — 씨앗 0 은 고정점이라 1 로 바꾼다).
     * @details 자리마다 난수를 (자리 흔들기 둘 →) 모델 → 제외 판정 → 크기 → 요 순으로 꺼냅니다. 제외된 자리는 크기 · 요를 꺼내지 않습니다.
     */
    struct SW_GF_API PropScatterMath
    {
        /** @brief 배치를 계산합니다. 규칙 모드는 평평한 표면(높이 = 영역 최소 y)입니다 — 지형 위는 `computeRulePlacements` 입니다. */
        static void computePlacements( const PropScatterParams& params, vector<PropScatterPlacement>& outListPlacement );
        /**
         * @brief 규칙 모드 배치를 표면 @p pSurface 위에 계산합니다(평면 좌표 = 월드 x · z). nullptr 이면 높이 = 영역 최소 y 인 평면입니다.
         * @details 제외 원은 원 제외 영역으로, 모델 비중은 항목 비중으로 옮겨 `PlacementScatter::scatter` 에 넘깁니다. 노멀 맞춤은 오브젝트 회전이 요뿐이라 쓰지 않습니다.
         */
        static void computeRulePlacements( const PropScatterParams& params, const IPlacementSurface* pSurface, vector<PropScatterPlacement>& outListPlacement );
        /** @brief [0, 1] 하나를 꺼내고 상태를 나아가게 합니다. */
        static float32 nextUnit( uint32& inoutState );
        /** @brief 비중대로 고른 모델 번호입니다. @p unit 은 [0, 1] 입니다. 모델이 없거나 비중 합이 0 이면 −1 입니다. */
        static int32 pickModel( const vector<PropScatterModel>& listModel, float32 unit );
    };
} // namespace sw

namespace sw
{
    /**
     * @class PropScatterComponent
     * @brief 플레이가 시작되면 배치대로 메시 오브젝트를 세우고, 끝나면 걷습니다. 씬에는 설정만 저장됩니다(세운 오브젝트는 저장하지 않는 런타임 산출물).
     * @details 세운 오브젝트는 핸들로 듭니다. 틱 중에 시작돼도 `executeOrDeferPostTick` 으로 틱 뒤에 세웁니다. 틱하지 않습니다.
     *          핸들 목록은 PROPERTY 입니다 — 핫 리로드는 오브젝트를 같은 id 로 되살리므로, 다시 만든 컴포넌트가 그 핸들로 세운 것을 알아보고
     *          한 벌 더 세우지 않습니다. 에디터는 플레이 전 상태로 되돌리므로 씬 파일에는 빈 목록이 남습니다.
     */
    REFLECT( Category = "World", DisplayName = "Prop Scatter", Tooltip = "Scatters seeded decoration meshes along or inside a region at play start" )
    class SW_GF_API PropScatterComponent : public Component
    {
    public:
        REFLECT_BODY();

        PropScatterComponent();
        virtual ~PropScatterComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;

        /** @brief 고를 모델 목록을 바꿉니다(다음 시작부터). */
        void setModels( const vector<PropScatterModel>& listModel ) { _listModel = listModel; }
        /** @brief 비울 원 목록을 바꿉니다(다음 시작부터). */
        void setExclusions( const vector<PropScatterExclusion>& listExclusion ) { _listExclusion = listExclusion; }
        /** @brief 지금 PROPERTY 로 계산 입력을 만듭니다. */
        PropScatterParams makeParams() const;
        /** @brief 세운 오브젝트를 모두 지웁니다(상태 저장 전 · 플레이 끝). */
        void   despawnProps();
        uint32 getSpawnedCount() const { return static_cast<uint32>( _listSpawned.size() ); }

    private:
        void spawnProps();
        /** @brief 든 핸들이 모두 살아 있는 오브젝트로 풀리고 하나 이상이면 true 입니다. */
        bool areSpawnedPropsAlive() const;

    private:
        PROPERTY( Category = "Scatter", DisplayName = "Models", Tooltip = "Meshes to pick from, with weights" )
        vector<PropScatterModel> _listModel;
        PROPERTY( Category = "Scatter", DisplayName = "Exclusions", Tooltip = "Ground circles left empty" )
        vector<PropScatterExclusion> _listExclusion;
        PROPERTY( Category = "Scatter", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Material of every prop; empty uses the scene default" )
        string _materialPath;
        PROPERTY( Category = "Scatter", DisplayName = "Prop Name", Tooltip = "Name of the spawned objects" )
        string _propName;
        PROPERTY( Category = "Scatter", DisplayName = "Region Min", Units = m )
        float3 _regionMin;
        PROPERTY( Category = "Scatter", DisplayName = "Region Max", Units = m )
        float3 _regionMax;
        PROPERTY( Category = "Scatter", DisplayName = "Spacing", Tooltip = "Distance between rows or grid cells", Min = 0.5, Units = m )
        float32 _spacing;
        PROPERTY( Category = "Scatter", DisplayName = "Along Jitter", Tooltip = "Random offset along an edge row", Min = 0.0, Units = m )
        float32 _alongJitter;
        PROPERTY( Category = "Scatter", DisplayName = "Inward Jitter", Tooltip = "Random offset inwards from an edge (both axes in Fill mode)", Min = 0.0, Units = m )
        float32 _inwardJitter;
        PROPERTY( Category = "Scatter", DisplayName = "Scale Min", Min = 0.01 )
        float32 _scaleMin;
        PROPERTY( Category = "Scatter", DisplayName = "Scale Max", Min = 0.01 )
        float32 _scaleMax;
        PROPERTY( Category = "Scatter", DisplayName = "Seed", Tooltip = "Same seed, same layout" )
        uint32 _seed;
        PROPERTY( Category = "Scatter", DisplayName = "Mode", Tooltip = "Edge rows, a filled grid or placement rules" )
        PropScatterMode _mode;
        PROPERTY( Category = "Scatter", DisplayName = "Rule", Tooltip = "Density, spacing and filters used in Rules mode (on the terrain below the region)" )
        PlacementRule _rule;

        PROPERTY( Category = "Scatter", DisplayName = "Spawned", Tooltip = "Props spawned at play start (runtime)" )
        vector<GameObjectHandle> _listSpawned;
    };
} // namespace sw
