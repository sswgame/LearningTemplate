/**
 * @file WaterBodyComponent.h
 * @brief 물입니다. 호수(사각 수면)와 강(경로를 따르는 띠)에 거스트너 파도, 깊이 색, 프레넬, 물가 거품을 입히고, 수면 높이, 노멀, 물속 질의를 제공합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Environment/Water/WaterWaveMath.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct RHIVertex;

    class GameObjectManager;
    class MeshInstanceBatch;
    class PrimitiveRegistry;
    class TerrainHeightfield;

    /** @brief 수면의 형태입니다. */
    ENUM()
    enum class WaterBodyShape : uint8
    {
        Lake = 0, ///< 오너 위치를 중심으로 한 사각 수면이다(`_size`).
        River,    ///< `_listRiverPoint` 를 이은 띠다(폭 `_riverWidth`). 점마다 수면 높이가 다르다.
    };
} // namespace sw

namespace sw
{
    /** @brief 물속 안개 값입니다(`WaterBodyComponent::findUnderwaterFog`). 안개 패스는 이 값을 읽습니다. */
    struct WaterUnderwaterFog
    {
        float4  _color{ 0.05f, 0.2f, 0.25f, 1.0f };
        float32 _density{ 0.15f };
        float32 _depth{ 0.0f }; ///< 수면에서 아래로 측정한 깊이(m)
    };
} // namespace sw

namespace sw
{
    /**
     * @class WaterBodyComponent
     * @brief 수면 메시(격자) 하나를 `MeshInstanceBatch` 로 그리고(반투명 패스), 같은 파도 식으로 CPU 질의에 답합니다.
     * @details 파도는 월드 (x, z) 의 함수라 수면 메시를 어디에 두든 이어지고, CPU 질의(`computeSurfaceHeight`)와 정점 셰이더가 같은 값을 낸다
     *          (`WaterWaveMath` 와 `gerstner.hlsli`, `RenderPassGPUTest.WaterWaveShaderMatchesCPU`). 파도 시간은 이 컴포넌트가 틱마다 쌓아 머티리얼에 싣고,
     *          질의도 그 시간을 쓴다(`getWaveTime`).
     *          수면 아래 깊이는 메시를 만들 때 지형(`TerrainComponent`)을 읽어 정점 색 알파에 베이크한다. 반투명 패스에는 장면 깊이 입력이 없어
     *          화면 공간에서 두께를 측정할 수 없기 때문이다. 지형이 없으면 깊은 물이다. 강은 정점 색 rg 에 흐름 방향을 싣는다(물결이 흐른다).
     *          강 경로는 점 목록이다. 같은 오브젝트에 스플라인 어댑터(GameFramework `SplineRiverPathComponent`)가 있으면 어댑터가 점을 만들어 넘기고(`setRiverPathFromSource`)
     *          점 목록은 읽기 전용이 된다 — Engine 이 GameFramework 의 스플라인을 읽을 수 없어(층 방향) 어댑터가 쪽에서 밀어 넣는다.
     *          스레드: 질의는 읽기만 한다. 메시 다시 만들기(`rebuildSurface`)는 틱 밖에서만 호출한다.
     */
    REFLECT( Category = "Environment", DisplayName = "Water Body", Tooltip = "Lake or river surface with Gerstner waves and surface queries", Meta = "Icon=Water, IconColor=Environment" )
    class SW_API WaterBodyComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        WaterBodyComponent();
        virtual ~WaterBodyComponent() override;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onPostLoad() override;
        /** @brief 로드 순서상 지형이 뒤에 서서 깊이를 못 구웠으면 틱 뒤에 수면을 다시 만듭니다. */
        void onBeginPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 소유 오브젝트를 켜고 끄면 수면 배치를 더티로 표시합니다. 빌더가 다시 확인합니다. */
        void onOwnerActiveInHierarchyChanged() override;
        void onTick( float32 deltaTime ) override;
        /** @brief 오너가 움직였다고 표시만 합니다(여러 스레드에서 불린다). 다음 틱이 틱 뒤로 다시 만들기를 미룹니다. */
        void onWorldTransformUpdated() override;

        /** @brief 수면 메시를 다시 만들고(지형 깊이 포함) 머티리얼 값을 싣습니다. 등록 전이면 아무것도 하지 않습니다. */
        void rebuildSurface();

        /**
         * @brief 월드 (x, z) 의 수면 높이(월드 y)를 구합니다. 이 물이 덮지 않는 위치면 false 를 반환합니다.
         * @param time 파도 시간입니다. 음수면 현재 시간(`getWaveTime`)을 씁니다.
         */
        [[nodiscard]] bool computeSurfaceHeight( float32 worldX, float32 worldZ, float32& outHeight, float32 time = -1.0f ) const;
        /** @brief 월드 (x, z) 의 수면 노멀을 구합니다. 덮지 않는 위치면 false 를 반환합니다. */
        [[nodiscard]] bool computeSurfaceNormal( float32 worldX, float32 worldZ, float3& outNormal, float32 time = -1.0f ) const;
        /** @brief 월드 점이 이 물의 수면 아래면 true 를 반환합니다(부력과 물속 판정). @p outDepth 는 수면에서 아래로 측정한 깊이입니다. */
        bool isUnderwater( const float3& worldPosition, float32& outDepth ) const;
        /** @brief 이 물이 덮는 위치(파도 없는 기준면 기준)면 true 를 반환합니다. */
        bool coversPosition( float32 worldX, float32 worldZ ) const;
        /** @brief 파도 값 넷(방향, 파장, 진폭, 가파름)을 반환합니다. 셰이더에 싣는 값과 같습니다. */
        void getWaveVectors( float4 ( &outArrWave )[shaderslot::kGerstnerWaveCount] ) const;
        /** @brief 지금 파도 시간(초)입니다. */
        float32 getWaveTime() const { return _waveTime; }
        /** @brief 파도 시간을 정합니다(테스트와 시퀀서용). 다음 틱부터 그 값에서 쌓입니다. */
        void setWaveTime( float32 time );
        /** @brief 수면 메시 정점 수입니다(테스트). */
        uint32 getSurfaceVertexCount() const { return _surfaceVertexCount; }

        /**
         * @brief @p manager 의 물 가운데 @p viewPosition 을 물속에 둔 첫 물의 안개를 구합니다. 물속 안개를 켠 물만 봅니다. 없으면 false 를 반환합니다.
         * @details 하늘, 시간, 높이 안개를 맡을 다음 작업이 이 값을 읽어 안개 패스에 적용한다(현재 렌더러에는 안개 패스가 없다).
         */
        [[nodiscard]] static bool findUnderwaterFog( const GameObjectManager& manager, const float3& viewPosition, WaterUnderwaterFog& outFog );
        /** @brief @p manager 의 물 가운데 월드 (x, z) 를 덮는 첫 물입니다. 빌린 포인터입니다. */
        static WaterBodyComponent* findWaterAt( const GameObjectManager& manager, float32 worldX, float32 worldZ );

        void setShape( WaterBodyShape shape ) { _shape = shape; }
        void setSize( const float2& size ) { _size = size; }
        void setWaves( const vector<GerstnerWave>& listWave ) { _listWave = listWave; }
        void setRiverPath( const vector<float3>& listPoint, float32 width );
        /**
         * @brief 강 경로를 다른 곳(스플라인 어댑터, GameFramework `SplineRiverPathComponent`)이 정한 값으로 덮고 수면을 다시 만듭니다.
         * @details 경로의 정본이 그 어댑터 하나가 되도록 `_bRiverPathDriven` 을 켠다 — 직렬화된 점 목록 `_listRiverPoint` 는 인스펙터가 막고 어댑터가 읽을 때마다 덮는다.
         *          Engine 은 GameFramework 의 스플라인을 읽을 수 없어(층 방향) 어댑터가 점을 만들어 이 함수로 넘긴다. 수면 다시 만들기는 틱 밖으로 미룬다.
         * @param listLocalPoint 이 물의 오너 위치 기준 점(월드 위치 − 오너 월드 위치, 회전 없음 — `_listRiverPoint` 와 같은 좌표). y 가 수면 높이다.
         */
        void setRiverPathFromSource( const vector<float3>& listLocalPoint );
        /** @brief 어댑터가 없어졌을 때 부릅니다 — 경로를 다시 손으로 고칠 수 있게 되고(점 목록은 마지막 값 그대로) 수면은 그대로 둡니다. */
        void clearRiverPathSource();
        /** @brief 강 경로를 어댑터가 정하는가? */
        bool                  isRiverPathDriven() const { return _bRiverPathDriven; }
        const vector<float3>& getRiverPoints() const { return _listRiverPoint; }
        float32               getRiverWidth() const { return _riverWidth; }
        void                  setMaterialPath( string_view path ) { _materialPath = path; }

    private:
        /** @brief 강에서 (x, z) 에 가장 가까운 경로 위 점의 기준 높이와 경로 거리를 찾습니다. 띠 밖이면 false 를 반환합니다. */
        bool findRiverBase( float32 worldX, float32 worldZ, float32& outBaseHeight ) const;
        /** @brief (x, z) 의 파도 없는 기준면 높이입니다. 덮지 않으면 false 입니다. */
        bool findBaseHeight( float32 worldX, float32 worldZ, float32& outBaseHeight ) const;
        /** @brief 호수 격자 정점을 만듭니다(깊이는 @p pTerrain 에서). */
        void makeLakeVertices( const TerrainHeightfield* pTerrain, vector<RHIVertex>& outListVertex, float32& outBoundsRadius ) const;
        /** @brief 강 띠 정점을 만듭니다. */
        void makeRiverVertices( const TerrainHeightfield* pTerrain, vector<RHIVertex>& outListVertex, float32& outBoundsRadius ) const;
        /** @brief 머티리얼 인스턴스에 파도, 색, 시간을 싣습니다. */
        void writeMaterialValues();

    private:
        PROPERTY( Category = "Water", DisplayName = "Shape" )
        WaterBodyShape _shape;
        PROPERTY( Category = "Water", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Empty uses the engine default water material" )
        string _materialPath; ///< 비면 엔진 기본 에셋 표의 `_waterMaterial` 이다
        PROPERTY( Category = "Water", DisplayName = "Size", Units = m, Tooltip = "Lake extent along x and z, centered on the owner" )
        float2 _size;
        PROPERTY( Category = "Water", DisplayName = "Cell Size", Min = 0.1, Units = m, Tooltip = "Surface grid spacing (waves need vertices)" )
        float32 _cellSize;
        PROPERTY( Category = "Water", DisplayName = "River Points", Units = m, EditCondition = "!_bRiverPathDriven",
                  Tooltip = "River centerline relative to the owner; y is the surface height (a path adapter on the same object overwrites it)" )
        vector<float3> _listRiverPoint;
        PROPERTY( Category = "Water", DisplayName = "River Width", Min = 0.1, Units = m )
        float32 _riverWidth;
        PROPERTY( Category = "Water", DisplayName = "River Path Driven", ReadOnly, Tooltip = "A path adapter (spline) on the same object decides the river points; the list above is read-only" )
        bool _bRiverPathDriven;
        PROPERTY( Category = "Waves", DisplayName = "Waves", Tooltip = "Up to four Gerstner waves" )
        vector<GerstnerWave> _listWave;
        PROPERTY( Category = "Shading", DisplayName = "Shallow Color", Tooltip = "Linear RGB; alpha = opacity at the shore" )
        float4 _shallowColor;
        PROPERTY( Category = "Shading", DisplayName = "Deep Color", Tooltip = "Linear RGB; alpha = opacity in deep water" )
        float4 _deepColor;
        PROPERTY( Category = "Shading", DisplayName = "Sky Color", Tooltip = "Reflected at grazing angles (fresnel)" )
        float4 _skyColor;
        PROPERTY( Category = "Shading", DisplayName = "Deep Depth", Min = 0.01, Units = m, Tooltip = "Depth where the deep color is reached" )
        float32 _deepDepth;
        PROPERTY( Category = "Shading", DisplayName = "Foam Width", Min = 0.0, Units = m, Tooltip = "Shore foam where the water is shallower than this" )
        float32 _foamWidth;
        PROPERTY( Category = "Shading", DisplayName = "Ripple Strength", Min = 0.0, Max = 1.0, Tooltip = "Small normal ripples on top of the waves" )
        float32 _rippleStrength;
        PROPERTY( Category = "Underwater", DisplayName = "Underwater Fog", Tooltip = "A camera below this surface gets underwater fog" )
        bool _bUnderwaterFog;
        PROPERTY( Category = "Underwater", DisplayName = "Fog Color" )
        float4 _fogColor;
        PROPERTY( Category = "Underwater", DisplayName = "Fog Density", Min = 0.0 )
        float32 _fogDensity;

        EnvironmentMaterial           _material;
        unique_ptr<MeshInstanceBatch> _batch;
        PrimitiveRegistry*            _pPrimitiveRegistry; ///< 등록된 매니저의 레지스트리(구조 링크라 생포인터)
        float3                        _surfaceOrigin;      ///< 메시를 만들 때의 오너 월드 위치다.
        float32                       _waveTime;
        uint32                        _surfaceVertexCount;
        atomic<bool>                  _bOriginDirty;    ///< 오너가 움직여 수면 원점이 낡았다
        bool                          _bBuiltOnTerrain; ///< 마지막 수면이 지형 깊이를 구웠다
    };
} // namespace sw
