/**
 * @file ShadowSetup.h
 * @brief 한 프레임의 그림자 배치를 게임 스레드에서 만듭니다 — 그림자 아틀라스의 칸 나누기, 뷰마다의 방향광 캐스케이드, 점광 · 스폿 타일.
 * @details 그림자 맵은 첨부 하나(`ShadowMap`)이고 그 안을 칸으로 나눠 쓰는 **아틀라스**다(유니티 URP · Godot 의 그림자 아틀라스).
 *          큐브 · 배열 텍스처가 아니라 2D 칸이라 RHI · 바인딩 계약(DX11 · GL 의 엔진 텍스처 슬롯 넷)이 그대로이고, 점광은 칸 여섯, 스폿은 칸 하나다.
 *          렌더 스레드는 씬을 못 보므로 여기서 만든 값(`FrameShadowData` · `ViewShadowData`)만 패킷으로 넘어간다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    struct DirectionalShadowProjection;

    class DirectionalLightComponent;

    /** @brief 그림자 아틀라스의 칸 하나입니다(텍셀 단위, 왼쪽 위 원점 — `RHIViewport` 와 같은 규약). */
    struct ShadowAtlasCell
    {
        uint32 _x{ 0 };
        uint32 _y{ 0 };
        uint32 _size{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 그림자 패스가 그리는 타일 하나입니다 — 칸과, 그 칸 안의 NDC 로 그리는 뷰-투영입니다. */
    struct ShadowTileDraw
    {
        float4x4        _viewProj{};
        ShadowAtlasCell _cell{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShadowAtlasLayout
     * @brief 그림자 아틀라스를 칸으로 나눈 배치입니다. 한 프레임의 모든 뷰가 같은 배치를 씁니다.
     * @details 방향광 캐스케이드가 하나이고 점광 · 스폿 그림자가 없으면 아틀라스 전체가 그 캐스케이드다(아틀라스가 생기기 전과 같은 그림).
     *          그 밖에는 네 사분면으로 나눠 캐스케이드가 앞 사분면부터 하나씩 쓰고, 남은 사분면은 2x2 로 나눠 점광 · 스폿 칸이 된다.
     *          점광 · 스폿 그림자가 있으면 캐스케이드는 둘까지다(칸 여덟 이상을 남긴다 — 점광 하나가 칸 여섯이다).
     */
    class SW_API ShadowAtlasLayout
    {
    public:
        /// @brief 점광 · 스폿 그림자가 있을 때 캐스케이드 수의 상한입니다.
        static constexpr uint32 kMaxCascadeWithLocalShadow = 2;
        /// @brief 사분면 하나를 나누는 점광 · 스폿 칸 수(2x2)입니다.
        static constexpr uint32 kLocalCellsPerQuadrant = 4;

        /**
         * @brief 배치를 만듭니다.
         * @param atlasResolution 아틀라스 한 변의 텍셀 수(`FrameRenderer::getShadowMapResolution`)
         * @param cascadeCount    방향광이 원하는 캐스케이드 수(1~4)
         * @param localTileCount  점광 · 스폿 그림자가 원하는 칸 수(점광 6, 스폿 1의 합). 칸이 모자라면 뒤의 빛은 그림자가 없다.
         */
        static ShadowAtlasLayout make( uint32 atlasResolution, uint32 cascadeCount, uint32 localTileCount );

        /** @brief 아틀라스 한 변의 텍셀 수입니다. */
        uint32 getAtlasResolution() const { return _atlasResolution; }
        /** @brief 캐스케이드 칸 수입니다. 뷰는 이보다 적게 쓸 수 있습니다(직교 카메라는 하나). */
        uint32 getCascadeCount() const { return _cascadeCount; }
        /** @brief 캐스케이드 칸 한 변이 아틀라스에서 차지하는 비율입니다(1 = 아틀라스 전체, 0.5 = 사분면). 셰이더의 `g_ShadowCascadeLayout.y` 입니다. */
        float32 getCascadeTileScale() const { return _bQuadrant == SW_TRUE ? 0.5f : 1.0f; }
        /** @brief 캐스케이드 칸 한 변의 텍셀 수입니다. 방향광 볼륨의 텍셀 스냅 · 바이어스가 이 크기로 잽니다. */
        uint32 getCascadeTileResolution() const { return _bQuadrant == SW_TRUE ? _atlasResolution / 2u : _atlasResolution; }
        /** @brief 캐스케이드 c 의 칸입니다(사분면 0 왼쪽 위, 1 오른쪽 위, 2 왼쪽 아래, 3 오른쪽 아래 — 셰이더가 같은 순서로 원점을 구한다). */
        ShadowAtlasCell getCascadeCell( uint32 cascadeIndex ) const;
        /** @brief 점광 · 스폿 칸 수입니다. */
        uint32 getLocalCapacity() const { return _localCapacity; }
        /** @brief 점광 · 스폿 칸 t 입니다. */
        ShadowAtlasCell getLocalCell( uint32 tileIndex ) const;

    private:
        uint32 _atlasResolution{ 1 };
        uint32 _cascadeCount{ 1 };
        uint32 _localCapacity{ 0 };
        uint8  _bQuadrant{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ViewShadowData
     * @brief 뷰 하나의 방향광 그림자입니다 — 셰이더 상수, 그림자 컬링 절두체, 그릴 캐스케이드 타일. 값 타입이라 게임 스레드에서 렌더 스레드로 넘어갑니다.
     * @details 추가 뷰(CCTV · 분할 화면)는 자기 카메라에 맞춘 것을 따로 가진다 — 주 시점 볼륨을 쓰면 다른 곳을 보는 뷰는 그림자가 빠진다(언리얼의 뷰별 그림자).
     */
    struct ViewShadowData
    {
        float4x4       _lightViewProj{};                                 ///< `g_LightViewProj` — 모든 캐스케이드를 덮는 기준 볼륨
        float4x4       _cullViewProj{};                                  ///< 그림자 패스 컬링 절두체 — 기준 볼륨에 점광 · 스폿의 경계 구를 더한 상자(없으면 기준 볼륨)
        float4         _shadowParams{};                                  ///< `g_ShadowParams`
        float4         _cascadeLayout{};                                 ///< `g_ShadowCascadeLayout` — x 캐스케이드 수, y 칸 배율, z 경계 섞기 폭(캐스케이드 깊이 구간의 비율), w 0
        float4         _arrCascade[shaderslot::kShadowCascadeMax]{};     ///< `g_ShadowCascade0..3` — (배율, 이동 x, 이동 y, 깊이 끝)
        ShadowTileDraw _arrCascadeTile[shaderslot::kShadowCascadeMax]{}; ///< 캐스케이드마다 그릴 행렬 · 칸
        uint32         _cascadeCount{ 0 };
        uint8          _bValid{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct FrameShadowData
     * @brief 한 프레임에 모든 뷰가 같이 쓰는 그림자 값입니다 — 아틀라스 배치, 주 시점의 방향광 그림자, 점광 · 스폿 타일.
     * @details 패킷이 통째로 싣고 렌더러가 맞바꾸기로 받는다(저장소가 돌아 다음 프레임에 다시 쓰인다).
     */
    struct FrameShadowData
    {
        ShadowAtlasLayout      _layout{};
        ViewShadowData         _mainView{};
        vector<GPULight>       _listTileElement{}; ///< 라이트 버퍼의 빛 뒤에 붙는 타일 원소(타일마다 `shaderslot::kShadowTileElementCount` 개)
        vector<ShadowTileDraw> _listLocalTile{};   ///< 점광 · 스폿 타일 — 모든 뷰의 그림자 패스가 자기 아틀라스에 같은 칸으로 그린다
        vector<float4>         _listLocalBound{};  ///< 그림자를 드리우는 점광 · 스폿의 경계 구(xyz 중심, w 반경) — 그림자 컬링 상자에 더한다

        /** @brief 값을 비웁니다. 저장소는 남깁니다. */
        void clear()
        {
            _layout   = ShadowAtlasLayout{};
            _mainView = ViewShadowData{};
            _listTileElement.clear();
            _listLocalTile.clear();
            _listLocalBound.clear();
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShadowSetupUtil
     * @brief 그림자 배치를 만드는 함수 묶음입니다. 패킷 경로(`RenderPacketBuilder`)와 씬 직접 경로(`FrameRenderer::execute`)가 같은 함수를 부릅니다.
     */
    struct SW_API ShadowSetupUtil
    {
        /// @brief 캐스케이드 경계를 섞는 폭 — 캐스케이드가 맡는 깊이 구간의 이 비율만큼 끝에서 다음 캐스케이드와 섞는다(유니티 URP Cascade Blend).
        static constexpr float32 kCascadeBlendFraction = 0.1f;

        /**
         * @brief 한 프레임의 그림자 배치를 만듭니다. @p inoutListLight 에서 그림자를 드리우는 점광 · 스폿의 원소에 그림자 칸(`_params.x` · `_params.w`)을 씁니다.
         * @param pShadowLight          그림자 맵의 방향광(`Scene::findShadowCastingDirectionalLight`). 없으면 주 시점 그림자는 비고 렌더러가 키 라이트로 폴백한다.
         * @param pMainViewProj         주 시점 카메라의 뷰-투영. 없으면 고정 볼륨이다.
         * @param bHasKeyLight          씬에 주광이 있는가 — 주광은 있는데 그림자 빛이 없으면 그림자가 지지 않는다(행렬 0).
         * @param listShadowLightIndex  `collectSceneLights` 가 준 그림자를 드리우는 점광 · 스폿의 원소 번호
         */
        static void populateFrame( const DirectionalLightComponent* pShadowLight, const float4x4* pMainViewProj, bool bHasKeyLight, uint32 atlasResolution,
                                   const vector<uint32>& listShadowLightIndex, vector<GPULight>& inoutListLight, FrameShadowData& outFrame );

        /** @brief 추가 뷰 하나의 방향광 그림자입니다. 그림자 빛이 없으면 프레임의 주 시점 값을 그대로 씁니다. */
        static ViewShadowData makeViewShadowFor( const DirectionalLightComponent* pShadowLight, const float4x4& viewProj, const FrameShadowData& frame );

        /**
         * @brief 그림자 빛이 없을 때(키 라이트 폴백) 원점 둘레 작은 볼륨의 그림자입니다. 렌더 스레드가 패킷에 주 시점 그림자가 없을 때 씁니다.
         * @param lightDirection 빛이 나아가는 방향(정규화되지 않아도 된다 — 0 이면 기본 방향)
         */
        static ViewShadowData makeFallbackShadow( const float3& lightDirection, const FrameShadowData& frame );

        /** @brief 점광 · 스폿 그림자가 원하는 칸 수입니다(점광 6, 스폿 1의 합). 그 밖의 원소는 세지 않습니다. */
        static uint32 countLocalTiles( const vector<GPULight>& listLight, const vector<uint32>& listShadowLightIndex );

        /**
         * @brief 점광 · 스폿에 아틀라스 칸을 앞에서부터 나눠 줍니다. 칸이 모자란 빛은 그림자 없이 비춥니다.
         * @details 칸을 받은 빛 원소에 `_params.x = 1`, `_params.w = 첫 타일 원소의 상대 번호` 를 쓰고, 타일마다 라이트 버퍼 원소 둘(아틀라스 자리를 접은 뷰-투영,
         *          칸 사각형 · 바이어스), 그릴 타일, 경계 구를 더한다. 점광의 면은 +X −X +Y −Y +Z −Z 순서이고 시야각은 90°에 PCF 여유 텍셀을 더한 것이다.
         */
        static void populateLocalShadows( const ShadowAtlasLayout& layout, const vector<uint32>& listShadowLightIndex, vector<GPULight>& inoutListLight, FrameShadowData& outFrame );

        /** @brief 방향광 투영 하나를 뷰 그림자 값으로 옮깁니다. 컬링 절두체는 투영의 빛 공간 상자에 @p listLocalBound 를 더한 것입니다. */
        static ViewShadowData makeViewShadow( const DirectionalShadowProjection& projection, const ShadowAtlasLayout& layout, const vector<float4>& listLocalBound );
    };
} // namespace sw
