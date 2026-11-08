/**
 * @file RenderView.h
 * @brief "어느 눈으로 보는가" 를 한 곳에 모은 타입을 정의합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Graphics/RHI/RHIConstantBuffer.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/View/FrameViewports.h"
#include "Engine/Graphics/View/RenderViewSettings.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"

namespace sw
{
    /**
     * @enum RenderViewType
     * @brief 프레임이 그리는 시점입니다. 컬링과 정렬 산출물이 이 단위로 나뉩니다.
     * @details 언리얼이 뷰마다 `FViewInfo` 와 `FInstanceCullingContext` 를 두는 것과 같다.
     */
    enum class RenderViewType : uint32
    {
        Main   = 0, ///< 게임 카메라
        Shadow = 1, ///< 그림자 라이트
        Count  = 2  ///< 고정 뷰의 수. 추가 뷰(CCTV, PiP)의 컬링 슬롯은 이 뒤에 붙는다(`kFirstExtraCullView`).
    };

    /// @brief 추가 뷰의 첫 컬링 슬롯입니다. 슬롯 0은 주 시점이고 1은 주 시점의 그림자입니다.
    inline constexpr uint32 kFirstExtraCullView = static_cast<uint32>( RenderViewType::Count );
    /// @brief 추가 뷰 하나가 쓰는 컬링 슬롯 수입니다 — 자기 시점 하나, 자기 그림자 패스 하나(뷰마다 그림자 볼륨이 다르다).
    inline constexpr uint32 kCullSlotsPerExtraView = 2;
    /// @brief 한 프레임에 가질 수 있는 추가 뷰의 최대 수입니다(컬링 슬롯, 상수 버퍼, 트랜지언트 풀이 뷰마다 하나씩 있습니다).
    inline constexpr uint32 kMaxExtraRenderView = 8;

    /** @brief 추가 뷰의 렌더링 결과가 가는 곳입니다. */
    enum class RenderViewOutputKind : uint8
    {
        ScreenRect     = 0, ///< 주 출력(백버퍼, 게임 뷰)의 사각형이며 주 시점 위에 겹친다(분할 화면, PiP).
        RenderTexture  = 1, ///< 렌더 텍스처(`rendertarget/<이름>`)이며 머티리얼이 읽는다(CCTV 모니터, 백미러).
        ViewportTarget = 2, ///< 호스트가 만든 렌더 타깃(에디터 씬 뷰)이다. 핸들(`RenderViewRequest::_viewportTarget`)로 받고, 그린 뒤 셰이더 읽기 상태로 둔다.
    };

} // namespace sw

namespace sw
{
    /**
     * @struct RenderViewRequest
     * @brief 이번 프레임의 추가 뷰 하나입니다. 게임 스레드가 카메라에서 만들어 패킷에 싣습니다. 값만 가지며 씬을 가리키지 않습니다.
     * @details 살아 있는 추가 뷰는 갱신 주기 때문에 쉬는 프레임에도 실린다(`_bRender` = 0). 렌더러가 그 뷰의 풀, 텍스처, 컬링 슬롯을 계속 가지고 있게 하려는 것이다.
     */
    struct RenderViewRequest
    {
        float4x4             _viewProj{};
        float3               _position{};
        float3               _transparentSortAxis{}; ///< 이 뷰의 투명 정렬 축(직교 카메라의 시선). 0이면 눈까지의 거리로 정렬한다(주 뷰와 같은 규칙).
        RenderViewSettings   _settings{};
        hashed_string        _renderTexture{};     ///< `RenderTexture` 의 경로(`rendertarget/<이름>`)
        RHITextureHandle     _viewportTarget{ 0 }; ///< `ViewportTarget` 의 렌더 타깃 핸들
        uint64               _viewID{ 0 };         ///< 카메라 컴포넌트 id. 렌더러가 이것으로 뷰마다의 상태를 찾는다.
        uint32               _outputWidth{ 0 };    ///< 출력 크기(렌더 텍스처 크기이며, 화면 사각형이면 그 픽셀 크기)
        uint32               _outputHeight{ 0 };
        RenderViewOutputKind _outputKind{ RenderViewOutputKind::RenderTexture };
        uint8                _bRender{ SW_TRUE }; ///< 이번 프레임에 그린다(갱신 주기, 가시 여부, 예산이 정한다).
        /// @brief 이 뷰의 카메라에 맞춘 방향광 그림자입니다(그리는 뷰만 채운다). 비어 있으면 주 시점의 것을 쓴다.
        ViewShadowData _shadow{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderView
     * @brief 뷰 하나가 가지는 상태입니다. 행렬, 절두체, 그리고 **자기 상수 버퍼**를 담습니다.
     * @details 뷰마다 달라지는 것을 모두 여기 모은다. 뷰를 얻으면 그 뷰의 버퍼가 따라오고, 다른 뷰의 것을 집으려면
     *          일부러 다른 인덱스를 써야 해서 눈에 띈다. 주의: "이 값은 누구 것인가" 가 타입에 없으면 컬링 상수 버퍼를
     *          뷰들이 나눠 써서 **메인 패스가 그림자 라이트의 절두체로 걸러진다**(화면 절반이 사라진다).
     *
     * @note 컬링 **산출물**(간접 인자와 가시 인스턴스 목록)은 인스턴스 수에 맞춰 커지므로 GPUScene 이
     *       소유한다(`GPUScene::getCullView`). 여기 있는 것은 그 산출물을 **만들 때 넣는 입력**이다.
     */
    struct SW_API RenderView
    {
        /// @brief 이 뷰의 뷰 × 프로젝션 행렬입니다. 절두체는 여기서만 뽑습니다(둘이 어긋날 여지를 없앱니다).
        float4x4 _viewProj{};
        /// @brief 이 뷰의 눈 위치입니다. 투명 정렬 키(카메라까지의 거리)가 이 값을 씁니다.
        float3 _position{};
        /// @brief `_viewProj` 에서 뽑은 절두체 평면 여섯 개입니다(왼쪽, 오른쪽, 아래, 위, 근평면, 원평면이며 정규화되어 있습니다). 식은 `Frustum` 하나로, CPU 공간 질의(`BVHTree3D`)와 같습니다.
        Frustum _frustum{};
        /**
         * @brief 이 뷰 전용 컬링 상수 버퍼입니다.
         * @details **뷰마다 하나여야 한다.** 하나를 나눠 쓰면 두 번째 업로드가 첫 번째 디스패치가 읽을
         *          내용을 덮어쓴다. CPU 는 디스패치 사이에 쓰지만 GPU 는 제출 뒤에 읽기 때문이다.
         */
        RHIConstantBuffer _cullCb;
        /**
         * @brief 이 뷰 전용 인스턴스 정렬 상수 버퍼입니다(투명 깊이 정렬이며, 정렬 키가 이 뷰의 눈까지의 거리입니다).
         * @details 컬링 상수 버퍼와 같은 이유로 뷰마다 하나다. 하나를 나눠 쓰면 모든 뷰가 마지막 뷰의 눈 위치로 정렬한다.
         */
        RHIConstantBuffer _sortCb;

        /** @brief 컬링을 돌릴 준비가 되었는지(상수 버퍼가 있는지) 반환합니다. */
        bool isReadyForCulling() const { return _cullCb.isValid(); }

        /**
         * @brief 뷰 행렬을 정하고 절두체를 **함께** 갱신합니다.
         * @details 행렬만 바꾸고 평면을 바꾸지 않으면 컬링이 지난 프레임의 시점으로 판정한다. 둘을 한
         *          함수로 묶어 그런 상태가 존재할 수 없게 한다.
         */
        void setViewProjection( const float4x4& viewProj );
    };
} // namespace sw
