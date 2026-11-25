/**
 * @file ShadowSetup.h
 * @brief 한 프레임의 그림자 배치를 게임 스레드에서 만듭니다. 그림자 아틀라스의 슬롯 나누기, 뷰마다의 방향광 캐스케이드, 점광과 스폿 타일을 다룹니다.
 * @details 그림자 맵은 아틀라스 둘입니다 — 방향광 캐스케이드의 `ShadowMap` 과 점광 · 스폿의 `LocalShadowMap`(유니티 URP 의 주광 · 추가 광 아틀라스 분리).
 *          한 장에 섞으면 캐스케이드가 사분면을 내줘야 하고 점광 · 스폿 칸이 캐스케이드 수에 묶였다(감사 RN-4). 두 장은 한 변이 같다(`gv_shadowMapResolution`).
 *          큐브나 배열 텍스처가 아니라 2D 슬롯이라 RHI 와 바인딩 계약이 그대로이고, 점광은 슬롯 여섯, 스폿은 슬롯 하나입니다.
 *          렌더 스레드는 씬을 못 보므로 여기서 만든 값(`FrameShadowData`, `ViewShadowData`)만 패킷으로 넘어갑니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    struct DirectionalShadowProjection;
    struct RenderConfig;

    class DirectionalLightComponent;

    /** @brief 그림자 아틀라스의 슬롯 하나입니다(텍셀 단위, 왼쪽 위 원점이며 `RHIViewport` 와 같은 규약입니다). */
    struct ShadowAtlasCell
    {
        uint32 _x{ 0 };
        uint32 _y{ 0 };
        uint32 _size{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 그림자 패스가 그리는 타일 하나입니다. 슬롯과, 그 슬롯 안의 NDC 로 그리는 뷰-투영을 담습니다. */
    struct ShadowTileDraw
    {
        float4x4        _viewProj{};
        ShadowAtlasCell _cell{};
        uint64          _key{ 0 }; ///< 타일의 정체 — 점광 · 스폿은 (컴포넌트 id, 면)(`LocalShadowAtlas::makeTileKey`). 그림자 캐시가 순번이 아니라 이 값으로 지난 프레임 타일을 찾는다. 캐스케이드는 0
    };

    /** @brief 그림자를 드리우는 점광 · 스폿 하나 — 라이트 원소 번호와 빛의 정체(컴포넌트 id)입니다. 정체가 아틀라스 칸 · 그림자 캐시를 프레임 사이에 잇는다. */
    struct LocalShadowLight
    {
        uint32 _lightIndex{ 0 };
        uint64 _lightID{ 0 };
    };

    /** @brief 점광 · 스폿 아틀라스에 칸을 달라는 요청 하나(빛 하나)입니다. */
    struct LocalShadowRequest
    {
        uint64 _lightID{ 0 };     ///< 빛의 정체(`LocalShadowLight::_lightID`)
        uint32 _faceCount{ 1 };   ///< 칸 수(점광 6, 스폿 1). 면은 모두 같은 크기다
        uint32 _desiredSize{ 0 }; ///< 화면 크기로 고른 칸 한 변(텍셀, `FrameShadowData::computeLocalShadowDesiredSize`). 0 이면 가장 작은 칸이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShadowAtlasLayout
     * @brief 방향광 아틀라스(`ShadowMap`)를 캐스케이드 슬롯으로 나눈 배치입니다. 한 프레임의 모든 뷰가 같은 배치를 씁니다.
     * @details 캐스케이드가 하나면 아틀라스 전체가 그 캐스케이드이고, 둘 이상이면 네 사분면을 앞에서부터 하나씩 씁니다(셰이더가 사분면 c 를 원점으로 구한다).
     *          점광 · 스폿은 다른 텍스처(`LocalShadowMap`, `LocalShadowAtlas`)라 캐스케이드 수 · 해상도가 점광 · 스폿 수와 상관없다(감사 RN-4 —
     *          예전에는 한 장에 섞어 점광 · 스폿이 있으면 캐스케이드가 셋으로 줄고 하나여도 사분면으로 줄었다). 유니티 URP 의 주광 아틀라스와 같은 자리다.
     */
    class SW_API ShadowAtlasLayout
    {
    public:
        /**
         * @brief 배치를 만듭니다.
         * @param atlasResolution 아틀라스 한 변의 텍셀 수(패킷의 설정 사본 `RenderFrameSettings::_shadowMapResolution` — 점광 · 스폿 아틀라스도 같은 값이다)
         * @param cascadeCount    방향광이 원하는 캐스케이드 수(1~4)
         */
        static ShadowAtlasLayout make( uint32 atlasResolution, uint32 cascadeCount );

        /** @brief 아틀라스 한 변의 텍셀 수입니다. 점광 · 스폿 아틀라스도 같다. */
        uint32 getAtlasResolution() const { return _atlasResolution; }
        /** @brief 캐스케이드 슬롯 수입니다. 뷰는 이보다 적게 쓸 수 있습니다(직교 카메라는 하나). */
        uint32 getCascadeCount() const { return _cascadeCount; }
        /** @brief 캐스케이드 슬롯 한 변이 아틀라스에서 차지하는 비율입니다(1 = 아틀라스 전체, 0.5 = 사분면). 셰이더의 `g_ShadowCascadeLayout.y` 입니다. */
        float32 getCascadeTileScale() const { return _cascadeCount > 1 ? 0.5f : 1.0f; }
        /** @brief 캐스케이드 슬롯 한 변의 텍셀 수입니다. 방향광 볼륨의 텍셀 스냅과 바이어스가 이 크기로 측정합니다. */
        uint32 getCascadeTileResolution() const { return _cascadeCount > 1 ? _atlasResolution / 2u : _atlasResolution; }
        /** @brief 캐스케이드 c 의 슬롯입니다(사분면 0 왼쪽 위, 1 오른쪽 위, 2 왼쪽 아래, 3 오른쪽 아래. 셰이더가 같은 순서로 원점을 구합니다). */
        ShadowAtlasCell getCascadeCell( uint32 cascadeIndex ) const;

    private:
        uint32 _atlasResolution{ 1 };
        uint32 _cascadeCount{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalShadowAtlas
     * @brief 점광 · 스폿 아틀라스(`LocalShadowMap`)의 칸 할당기입니다. 게임 스레드의 패킷 수집기가 하나를 갖고 프레임마다 `build` 합니다.
     * @details 칸 한 변은 빛마다 화면 크기로 고른다 — `LocalShadowRequest::_desiredSize` 이상인 가장 작은 (아틀라스 반 ÷ 2ⁿ)이고 하한은 `_shadowLocalMinTileResolution` 이다.
     *          칸 넓이 합이 아틀라스를 넘으면 하한보다 큰 칸을 모두 반으로 줄이기를 들어갈 때까지 되풀이한다(유니티 URP 추가 광 아틀라스가 넘칠 때 칸을 줄이는 것과 같다).
     *          지난 프레임에 같은 빛 · 같은 크기로 서 있던 칸은 그 자리를 지킨다 — 우선순위가 뒤집혀도 칸이 옮겨지지 않아 그림자 캐시가 Reuse 다
     *          (언리얼 VSM 이 빛마다 페이지를 캐시하는 것과 같은 까닭). 그 자리를 지키느라 빈 곳이 조각나 어느 빛이 못 들어가면 처음부터 다시 쌓는다.
     *          이번 프레임에 요청하지 않은 빛의 칸은 풀린다. 크기는 내림차순(우선순위 순서 = 화면 크기 순서)이고 칸은 자기 크기의 배수 자리에 놓여 겹치지 않는다.
     */
    class SW_API LocalShadowAtlas
    {
    public:
        /// @brief 칸을 받지 못한 요청의 `getFirstCell` 값입니다.
        static constexpr uint32 kNotPlaced = ~0u;

        /** @brief 타일의 정체(`ShadowTileDraw::_key`) — 빛의 정체와 면 번호(0~5)입니다. */
        static uint64 makeTileKey( uint64 lightID, uint32 faceIndex ) { return ( lightID << 3u ) | static_cast<uint64>( faceIndex ); }

        /**
         * @brief 이번 프레임의 칸을 나눕니다.
         * @param atlasResolution   아틀라스 한 변의 텍셀 수. 지난 프레임과 다르면 자리를 지키지 않는다
         * @param minTileResolution 칸 한 변의 하한(`RenderConfig::_shadowLocalMinTileResolution`)
         * @param listRequest       빛마다의 요청 — 우선순위 순서(앞이 먼저 칸을 받는다)
         */
        void build( uint32 atlasResolution, uint32 minTileResolution, const vector<LocalShadowRequest>& listRequest );

        /** @brief 요청 @p requestIndex 의 첫 칸 번호(`getCell`)입니다. 면은 그 뒤에 이어진다. 칸을 받지 못했으면 `kNotPlaced` 입니다. */
        uint32 getFirstCell( uint32 requestIndex ) const { return requestIndex < _listFirstCell.size() ? _listFirstCell[requestIndex] : kNotPlaced; }
        /** @brief 칸 @p cellIndex 입니다(`getFirstCell` + 면 번호). */
        const ShadowAtlasCell& getCell( uint32 cellIndex ) const { return _listCell[cellIndex]; }
        /** @brief 가장 작은 칸 한 변(이번 `build` 의 하한)입니다 — 칸이 모자랄 때의 로그에 쓴다. */
        uint32 getMinTileResolution() const { return _minTileResolution; }

    private:
        /** @brief 이번 크기(`_listSize`)로 칸을 놓습니다. @p bKeep 이면 지난 프레임 자리를 먼저 지킨다. 모든 요청이 칸을 받았으면 true 입니다. */
        bool place( const vector<LocalShadowRequest>& listRequest, bool bKeep );
        /** @brief 격자(@p unitX, @p unitY 부터 한 변 @p units 칸)가 비어 있는지입니다. */
        bool isFree( uint32 unitX, uint32 unitY, uint32 units ) const;
        /** @brief 격자를 채우거나(@p bOccupied) 비웁니다. */
        void mark( uint32 unitX, uint32 unitY, uint32 units, bool bOccupied );

    private:
        unordered_map<uint64, ShadowAtlasCell> _mapLastCell;   ///< 지난 프레임 타일 키 → 칸
        vector<ShadowAtlasCell>                _listCell;      ///< 이번 프레임 칸(요청마다 면 수만큼 이어진다)
        vector<uint32>                         _listFirstCell; ///< 요청마다 첫 칸 번호(`kNotPlaced`)
        vector<uint32>                         _listOffset;    ///< 요청마다 `_listCell` 안의 자리(칸을 못 받아도 잡힌다)
        vector<uint32>                         _listSize;      ///< 요청마다 이번 칸 한 변
        vector<uint8>                          _listOccupied;  ///< 격자(한 칸 = 이번 가장 작은 칸)마다 찼는지
        uint32                                 _atlasResolution{ 0 };
        uint32                                 _minTileResolution{ 0 };
        uint32                                 _unit{ 1 };     ///< 격자 한 칸의 텍셀 수
        uint32                                 _gridSize{ 0 }; ///< 격자 한 변의 칸 수
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ViewShadowData
     * @brief 뷰 하나의 방향광 그림자입니다. 셰이더 상수, 그림자 컬링 절두체, 그릴 캐스케이드 타일을 담습니다. 값 타입이라 게임 스레드에서 렌더 스레드로 넘어갑니다.
     * @details 추가 뷰(CCTV, 분할 화면)는 자기 카메라에 맞춘 것을 따로 가집니다. 주 시점 볼륨을 쓰면 다른 곳을 보는 뷰는 그림자가 빠집니다(언리얼의 뷰별 그림자).
     */
    struct ViewShadowData
    {
        float4x4       _lightViewProj{};                                 ///< `g_LightViewProj`: 모든 캐스케이드를 덮는 기준 볼륨
        float4x4       _cullViewProj{};                                  ///< 그림자 패스 컬링 절두체. 기준 볼륨에 점광과 스폿의 경계 구를 더한 상자(없으면 기준 볼륨)
        float4         _shadowParams{};                                  ///< `g_ShadowParams`
        float4         _cascadeLayout{};                                 ///< `g_ShadowCascadeLayout`: x 캐스케이드 수, y 슬롯 배율, z 경계 섞기 폭(캐스케이드 깊이 구간의 비율), w 거리 끝에서 옅어지기 시작하는 깊이(m, `DirectionalShadowProjection::_fadeStartDepth`)
        float4         _arrCascade[shaderslot::kShadowCascadeMax]{};     ///< `g_ShadowCascade0..3` — (배율, 이동 x, 이동 y, 깊이 끝)
        ShadowTileDraw _arrCascadeTile[shaderslot::kShadowCascadeMax]{}; ///< 캐스케이드마다 그릴 행렬과 슬롯
        float32        _directionalPenumbraScale{ 0.0f };                ///< PCSS 반그림자 배율(`ShadowFilter::computeDirectionalPenumbraScale`) — `g_ShadowFilter.y`
        uint32         _cascadeCount{ 0 };
        uint8          _bValid{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct FrameShadowData
     * @brief 한 프레임에 모든 뷰가 같이 쓰는 그림자 값입니다. 아틀라스 배치, 주 시점의 방향광 그림자, 점광과 스폿 타일을 담습니다.
     * @details 패킷이 통째로 싣고 렌더러가 맞바꾸기로 받습니다(저장소가 돌아 다음 프레임에 다시 쓰입니다).
     */
    struct SW_API FrameShadowData
    {
        ShadowAtlasLayout      _layout{};
        ViewShadowData         _mainView{};
        vector<GPULight>       _listTileElement{};                             ///< 라이트 버퍼의 빛 뒤에 붙는 타일 원소(타일마다 `shaderslot::kShadowTileElementCount` 개)
        vector<ShadowTileDraw> _listLocalTile{};                               ///< 점광과 스폿 타일(점광 · 스폿 아틀라스 `LocalShadowMap` 의 칸). 모든 뷰의 그림자 패스가 자기 아틀라스에 같은 슬롯으로 그린다
        vector<float4>         _listLocalBound{};                              ///< 그림자를 드리우는 점광과 스폿의 경계 구(xyz 중심, w 반경). 그림자 컬링 상자에 더한다
        vector<uint32>         _listLocalTileLight{};                          ///< 점광과 스폿 타일마다 그 빛의 번호(`_listLocalBound` · `_listLocalCullViewProj` 의 번호). 점광의 면 여섯은 같은 번호다
        vector<float4x4>       _listLocalCullViewProj{};                       ///< 빛마다의 그림자 컬링 절두체 — 스폿은 그 타일의 뷰-투영, 점광은 경계 구를 덮는 직교 상자(감사 RN-5, 빛마다 컬링 슬롯)
        float4                 _contactShadowLights{ kNoContactShadowLights }; ///< 접촉 그림자 채널 넷의 빛 원소 번호(-1 = 빈 채널) — `g_ContactShadowLights`

        /** @brief 값을 비웁니다. 저장소는 남깁니다. */
        void clear()
        {
            _layout   = ShadowAtlasLayout{};
            _mainView = ViewShadowData{};
            _listTileElement.clear();
            _listLocalTile.clear();
            _listLocalBound.clear();
            _listLocalTileLight.clear();
            _listLocalCullViewProj.clear();
            _contactShadowLights = kNoContactShadowLights;
        }

        /// @brief 접촉 그림자 채널 수입니다(마스크 첨부 RGBA 한 장 — 한 프레임에 접촉 그림자를 받는 빛의 상한).
        static constexpr uint32 kContactShadowChannelCount = 4;
        /// @brief 빈 채널만 있는 `_contactShadowLights` 입니다.
        static constexpr float4 kNoContactShadowLights{ -1.0f, -1.0f, -1.0f, -1.0f };

        /** @brief 이번 프레임에 접촉 그림자를 받는 빛이 하나라도 있으면 true 입니다. 없으면 접촉 그림자 패스가 돌지 않는다(`FrameViewResources::applyViewPassSelection`). */
        bool hasContactShadows() const { return _contactShadowLights._x >= 0.0f; }

        /**
         * @brief 접촉 그림자를 켠 빛(`GPULight::_extra.y` > 0, 3D 빛만)에 채널을 나눠 줍니다. 방향광이 먼저이고 나머지는 목록 순서이며 넷을 넘는 빛은 접촉 그림자 없이 비춥니다.
         * @details 채널을 받은 빛 원소에 `_extra.z = 채널 + 1` 을 쓰고(조명이 마스크의 그 채널을 곱한다), `_contactShadowLights` 의 그 칸에 빛 원소 번호를 쓴다(패스가 그 빛 쪽으로 걷는다).
         *          언리얼은 빛마다 그 빛의 조명 셰이더 안에서 걷는다 — 여기서는 패스 하나가 빛 넷을 한 번에 걸어 포워드 · 디퍼드가 같은 마스크를 읽는다.
         *          받지 못한 빛의 `_extra.z` 는 0 이다.
         */
        static void populateContactShadows( vector<GPULight>& inoutListLight, FrameShadowData& outFrame );

        // 그림자 배치를 만드는 함수 묶음입니다. 패킷 경로(`RenderPacketBuilder`)와 씬 직접 경로(`FrameRenderer::execute`)가 같은 함수를 부릅니다.
        /// @brief 캐스케이드 경계를 섞는 폭입니다. 캐스케이드가 맡는 깊이 구간의 이 비율만큼 끝에서 다음 캐스케이드와 섞습니다(유니티 URP Cascade Blend).
        static constexpr float32 kCascadeBlendFraction = 0.1f;

        /**
         * @brief 한 프레임의 그림자 배치를 만듭니다. @p inoutListLight 에서 그림자를 드리우는 점광과 스폿의 원소에 그림자 슬롯(`_params.x`, `_params.w`)을 씁니다.
         * @param pShadowLight          그림자 맵의 방향광(`DirectionalLightComponent::findShadowCasting`). 없으면 주 시점은 그림자 없는 뷰(`makeUnshadowedView`)입니다.
         * @param pMainViewProj         주 시점 카메라의 뷰-투영. 없으면 고정 볼륨입니다.
         * @param renderConfig          렌더 정책(캐스케이드 분할 · 점광 · 스폿 깊이 바이어스 — `EngineConfig::_render`)
         * @param listShadowLight       `collectSceneLights` 가 준 그림자를 드리우는 점광과 스폿(원소 번호 · 정체)
         * @param inoutLocalAtlas       점광 · 스폿 아틀라스 할당기(부르는 쪽이 프레임 사이에 들고 있다 — 서 있는 빛의 칸을 지킨다)
         * @details 주 시점 그림자(`outFrame._mainView`)는 늘 유효합니다. 그림자 빛이 없을 때 키 라이트로 볼륨을 대신 만들지 않습니다.
         */
        static void populateFrame( const DirectionalLightComponent* pShadowLight, const float4x4* pMainViewProj, uint32 atlasResolution, const RenderConfig& renderConfig,
                                   const vector<LocalShadowLight>& listShadowLight, LocalShadowAtlas& inoutLocalAtlas, vector<GPULight>& inoutListLight, FrameShadowData& outFrame );

        /** @brief 추가 뷰 하나의 방향광 그림자입니다. 그림자 빛이 없으면 프레임의 주 시점 값을 그대로 씁니다. */
        static ViewShadowData makeViewShadowFor( const DirectionalLightComponent* pShadowLight, const float4x4& viewProj, const RenderConfig& renderConfig, const FrameShadowData& frame );

        /**
         * @brief 그림자 없는 뷰입니다 — 행렬은 기본값이고, 그림자를 받는 방향광이 없으므로 셰이더는 이 볼륨을 읽지 않습니다.
         * @details 그림자를 드리우는 방향광이 없는 프레임과, 렌더 스레드가 그림자 배치 없이 받은 프레임(게임 스레드가 씬을 그리지 않았다)이 씁니다.
         */
        static ViewShadowData makeUnshadowedView( const ShadowAtlasLayout& layout );

        /**
         * @brief 점광과 스폿에 점광 · 스폿 아틀라스 슬롯을 우선순위 순서로 나눠 줍니다(`LocalShadowAtlas`). 슬롯이 모자란 빛은 그림자 없이 비춥니다.
         * @details 우선순위는 화면 크기의 대리값(경계 구 반경 / 카메라 깊이)이 큰 빛부터이고, 칸 한 변도 그 화면 크기로 고릅니다(`computeLocalShadowDesiredSize`).
         *          언리얼이 그림자 해상도와 아틀라스 위치를 투영한 화면 반경으로 고르는 것과 같은 기준입니다. @p pPriorityViewProj 가 없으면(카메라 없음) 목록 순서이고
         *          칸은 가장 큰 크기를 원합니다. 값이 같으면 목록 순서를 지킵니다. 타일의 정체(`ShadowTileDraw::_key`)는 (컴포넌트 id, 면)입니다.
         *          빛마다 경계 구와 그림자 컬링 절두체(`_listLocalCullViewProj`)를, 타일마다 그 빛의 번호(`_listLocalTileLight`)를 더합니다.
         *          슬롯을 받은 빛 원소에 `_params.x = 1`, `_params.w = 첫 타일 원소의 상대 번호` 를 쓰고, 타일마다 라이트 버퍼 원소 둘(아틀라스 위치를 접은 뷰-투영,
         *          슬롯 사각형과 바이어스 — 그 원소의 `_directionType` 은 PCSS 의 (가까운 면, 먼 면, 거리 1 m 의 텍셀 길이, 원천 반지름)), 그릴 타일, 경계 구를 더합니다. 점광의 면은 +X −X +Y −Y +Z −Z 순서이고 시야각은 90°에 PCF 여유 텍셀을 더한 것입니다.
         * @param pPriorityViewProj 주 시점 카메라의 뷰-투영(우선순위의 기준). nullptr 이면 목록 순서입니다.
         */
        static void populateLocalShadows( uint32 atlasResolution, const float4x4* pPriorityViewProj, const RenderConfig& renderConfig, const vector<LocalShadowLight>& listShadowLight,
                                          LocalShadowAtlas& inoutLocalAtlas, vector<GPULight>& inoutListLight, FrameShadowData& outFrame );

        /**
         * @brief 점광과 스폿 그림자 하나의 아틀라스 우선순위입니다. 경계 구 반경 / 카메라 깊이(클립 w)이며, 클수록 먼저 슬롯을 받습니다.
         * @details 카메라가 구 안이거나 아주 가까우면 깊이를 반경의 `kLocalPriorityMinDepthFraction` 로 막아 값이 끝없이 커지지 않습니다. 구 전체가 카메라 뒤면 0 입니다.
         */
        static float32 computeLocalShadowPriority( const GPULight& light, const float4x4& viewProj );

        /// @brief 우선순위 깊이의 하한입니다. 반경의 이 비율입니다. 카메라가 빛 구 안에 있는 빛들은 반경이 큰 쪽이 먼저입니다.
        static constexpr float32 kLocalPriorityMinDepthFraction = 0.1f;

        /**
         * @brief 점광 · 스폿 칸 한 변의 화면 크기 기준(텍셀)입니다 — 투영한 경계 구 반경(NDC, 화면 반 높이 = 1) × 아틀라스 한 변.
         * @details 반경이 화면 반 높이의 절반을 덮는 빛이 아틀라스 반(가장 큰 칸)을 원한다. 투영 배율은 뷰-투영의 x · y 열 길이(회전은 길이를 바꾸지 않는다)라
         *          원근 · 직교가 같은 식이다. @p priority 는 `computeLocalShadowPriority` 다(반경 / 깊이 — 같은 뷰에서는 칸 크기도 우선순위 순서다).
         */
        static uint32 computeLocalShadowDesiredSize( float32 priority, const float4x4& viewProj, uint32 atlasResolution );

        /** @brief 방향광 투영 하나를 뷰 그림자 값으로 옮깁니다. 컬링 절두체는 투영의 빛 공간 상자에 @p listLocalBound 를 더한 것입니다. */
        static ViewShadowData makeViewShadow( const DirectionalShadowProjection& projection, const ShadowAtlasLayout& layout, const vector<float4>& listLocalBound );
    };
} // namespace sw
