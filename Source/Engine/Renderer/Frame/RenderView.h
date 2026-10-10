/**
 * @file RenderView.h
 * @brief "어느 눈으로 보는가" 를 한 자리에 모은 타입입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHIConstantBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @enum RenderViewType
     * @brief 프레임이 그리는 시점입니다. 컬링 · 정렬 산출물이 이 단위로 갈립니다.
     * @details 언리얼이 뷰마다 `FViewInfo` 와 `FInstanceCullingContext` 를 두는 자리와 같습니다.
     */
    enum class RenderViewType : uint32
    {
        Main   = 0, ///< 게임 카메라
        Shadow = 1, ///< 그림자 라이트
        Count  = 2  ///< 고정 뷰의 수 — 추가 뷰(CCTV · PiP)의 컬링 칸은 이 뒤에 붙는다(`kFirstExtraCullView`)
    };

    /// @brief 추가 뷰의 첫 컬링 칸입니다. 칸 0 은 주 시점, 1 은 그림자 라이트입니다.
    inline constexpr uint32 kFirstExtraCullView = static_cast<uint32>( RenderViewType::Count );
    /// @brief 한 프레임에 들 수 있는 추가 뷰의 최대 수입니다(컬링 칸 · 상수버퍼 · 트랜지언트 풀이 뷰마다 하나다).
    inline constexpr uint32 kMaxExtraRenderView = 8;

    /** @brief 추가 뷰의 그림이 가는 곳입니다. */
    enum class RenderViewOutputKind : uint8
    {
        ScreenRect    = 0, ///< 주 출력(백버퍼 · 게임 뷰)의 사각형 — 주 시점 위에 겹친다(분할 화면 · PiP)
        RenderTexture = 1, ///< 렌더 텍스처(`rendertarget/<이름>`) — 머티리얼이 읽는다(CCTV 모니터 · 백미러)
        HostTarget    = 2, ///< 호스트가 만든 렌더 타깃(에디터 씬 뷰) — 핸들(`RenderViewRequest::_hostTarget`)로 받고, 그린 뒤 셰이더 읽기 상태로 둔다
    };

    /**
     * @struct RenderViewSettings
     * @brief 뷰 하나의 출력 사각형 · 해상도 배율 · 끌 기능 · 컷 표시입니다. 주 시점과 추가 뷰가 같은 묶음을 씁니다(값, 게임 스레드 → 렌더 스레드).
     */
    struct RenderViewSettings
    {
        float4  _screenRect{ 0.0f, 0.0f, 1.0f, 1.0f }; ///< 출력 안의 사각형(x, y, 너비, 높이 — 0..1, 왼쪽 위 원점). 렌더 텍스처는 늘 전체
        float32 _resolutionScale{ 1.0f };              ///< 출력 크기에 곱하는 내부 해상도
        uint8   _bShadows{ SW_TRUE };                  ///< 그림자를 그린다(끄면 그림자 맵을 지우기만 한다 — 모두 밝다)
        uint8   _bPostProcess{ SW_TRUE };              ///< 후처리(블룸 · 외곽선 · 톤맵 · TAA)를 건다
        uint8   _bCut{ SW_FALSE };                     ///< 이번 프레임에 화면이 끊겼다(언리얼 `bCameraCut`) — TAA 기록을 버린다

        /** @brief 사각형이 출력 전체인지입니다. */
        bool isFullRect() const { return _screenRect._x <= 0.0f && _screenRect._y <= 0.0f && _screenRect._z >= 1.0f && _screenRect._w >= 1.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderViewRequest
     * @brief 이번 프레임의 추가 뷰 하나(게임 스레드가 카메라에서 만들어 패킷에 싣는다)입니다. 값만 들고 씬을 가리키지 않습니다.
     * @details 살아 있는 추가 뷰는 갱신 주기로 쉬는 프레임에도 실린다(`_bRender` = 0) — 렌더러가 그 뷰의 풀 · 텍스처 · 컬링 칸을 들고 있게.
     */
    struct RenderViewRequest
    {
        float4x4             _viewProj{};
        float3               _position{};
        float3               _transparentSortAxis{}; ///< 이 뷰의 투명 정렬 축(직교 카메라의 시선). 0 이면 눈까지의 거리로 정렬한다(주 뷰와 같은 규칙)
        RenderViewSettings   _settings{};
        hashed_string        _renderTexture{};  ///< `RenderTexture` 의 경로(`rendertarget/<이름>`)
        RHITextureHandle     _hostTarget{ 0 };  ///< `HostTarget` 의 렌더 타깃 핸들
        uint64               _viewId{ 0 };      ///< 카메라 컴포넌트 id — 렌더러가 뷰마다의 상태를 이것으로 찾는다
        uint32               _outputWidth{ 0 }; ///< 출력 크기(렌더 텍스처 크기, 화면 사각형이면 그 픽셀 크기)
        uint32               _outputHeight{ 0 };
        RenderViewOutputKind _outputKind{ RenderViewOutputKind::RenderTexture };
        uint8                _bRender{ SW_TRUE }; ///< 이번 프레임에 그린다(갱신 주기 · 보이는가 · 예산이 정한다)
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트(에디터)가 만든 오프스크린 출력 하나입니다. 렌더 타깃이 0 이면 이번 프레임에 그 뷰를 그리지 않습니다. */
    struct HostViewTarget
    {
        RHITextureHandle _renderTarget{ 0 };
        uint32           _width{ 0 };
        uint32           _height{ 0 };

        /** @brief 그릴 수 있는 출력인지입니다. */
        bool isValid() const { return _renderTarget != 0 && _width > 0 && _height > 0; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct HostViewTargets
     * @brief 호스트가 이번 프레임에 그려 달라는 뷰 둘입니다(`EngineLoop::tick`).
     * @details 게임 뷰는 게임 카메라 · 화면 UI · 화면 사각형 뷰가 드는 주 출력이고, 씬 뷰는 호스트 카메라의 추가 뷰(`RenderViewOutputKind::HostTarget`)입니다.
     *          게임 뷰가 없고 씬 뷰만 있으면 씬 뷰가 주 출력이 됩니다(화면 UI · 화면 사각형 뷰 없이). 둘 다 없으면 백버퍼에 게임 카메라로 그립니다(에디터 없는 실행).
     */
    struct HostViewTargets
    {
        HostViewTarget _game;
        HostViewTarget _scene;

        /** @brief 씬 뷰가 주 출력인지(게임 뷰가 없다)입니다. */
        bool isSceneViewMain() const { return _game.isValid() == false && _scene.isValid(); }
        /** @brief 이번 프레임의 주 출력입니다(둘 다 없으면 빈 값 — 백버퍼). */
        const HostViewTarget& getMainOutput() const { return isSceneViewMain() ? _scene : _game; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderView
     * @brief 뷰 하나가 갖는 상태입니다. 행렬, 절두체, 그리고 **자기 상수버퍼**를 담습니다.
     * @details 뷰마다 달라지는 것을 모두 여기 모읍니다. 뷰를 얻으면 그 뷰의 버퍼가 딸려 오고, 다른 뷰의 것을 집으려면
     *          일부러 다른 인덱스를 써야 해서 눈에 띕니다. 주의: "이 값은 누구 것인가" 가 타입에 없으면 컬링 상수버퍼를
     *          뷰들이 나눠 써 **메인 패스가 그림자 라이트의 절두체로 걸러집니다**(화면 절반이 사라집니다).
     *
     * @note 컬링 **산출물**(간접 인자 · 가시 인스턴스 목록)은 인스턴스 수에 맞춰 커지므로 GPUScene 이
     *       소유합니다(`GPUScene::getCullView`). 여기 있는 것은 그 산출물을 **만들 때 넣는 입력**입니다.
     */
    struct SW_API RenderView
    {
        /// @brief 이 뷰의 뷰 x 프로젝션입니다. 절두체는 여기서만 뽑습니다(둘이 어긋날 자리를 없앱니다).
        float4x4 _viewProj{};
        /// @brief 이 뷰의 눈 위치입니다. 투명 정렬 키(카메라까지의 거리)가 이 값을 씁니다.
        float3 _position{};
        /// @brief `_viewProj` 에서 뽑은 절두체 여섯 평면입니다(왼/오/아래/위/근/원, 정규화됨). 식은 `Frustum` 하나로, CPU 공간 질의(`BVHTree3D`)와 같습니다.
        Frustum _frustum{};
        /**
         * @brief 이 뷰 전용 컬링 상수버퍼입니다.
         * @details **뷰마다 하나여야 합니다.** 하나를 나눠 쓰면 두 번째 업로드가 첫 번째 디스패치가 읽을
         *          내용을 덮어씁니다. CPU 는 디스패치 사이에 쓰지만 GPU 는 제출 뒤에 읽기 때문입니다.
         */
        RHIConstantBufferSlot _cullCb;
        /**
         * @brief 이 뷰 전용 인스턴스 정렬 상수버퍼입니다(투명 깊이 정렬 — 정렬 키가 이 뷰의 눈까지의 거리다).
         * @details 컬링 상수버퍼와 같은 이유로 뷰마다 하나다. 하나를 나눠 쓰면 모든 뷰가 마지막 뷰의 눈 자리로 정렬한다.
         */
        RHIConstantBufferSlot _sortCb;

        /** @brief 컬링을 돌릴 준비가 됐는지(상수버퍼가 있는지) 반환합니다. */
        bool isReadyForCulling() const { return _cullCb.isValid(); }

        /**
         * @brief 뷰 행렬을 정하고 절두체를 **함께** 갱신합니다.
         * @details 행렬만 바꾸고 평면을 안 바꾸면 컬링이 지난 프레임의 시점으로 판정합니다. 둘을 한
         *          함수로 묶어 그 상태가 존재할 수 없게 합니다.
         */
        void setViewProjection( const float4x4& viewProj );
    };
} // namespace sw
