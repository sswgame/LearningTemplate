/**
 * @file Engine/Graphics/Canvas/CanvasOverlay.h
 * @brief 캔버스(런타임 UI) 위에 겹쳐 칠하는 등록입니다 — 칠하는 쪽이 `SW_CANVAS_OVERLAY` 한 줄로 스스로 등록하고, 패킷의 캔버스 채우기는 등록을 순서대로 돌기만 합니다.
 * @details 디버그 드로우 겹치기 · 캔버스 테스트 그림 같은 개발 겹치기는 개발 도구(`DevTools/Client`)가 등록한다 — 엔진은 무엇이 붙었는지 모르고, 배포본에는 등록이 없어 사본도 만들지 않는다.
 *          예전에는 패킷 채우기가 두 개발 스위치를 직접 읽었다. 언리얼 `UDebugDrawService::Register`(뷰포트 위 디버그 칠하기 등록)와 같은 자리다.
 *          등록마다 칠할 층(`CanvasOverlayLayer`)을 밝힌다: 개발 그림(디버그 드로우)은 사진 캡처 뒤의 개발 겹치기 층 — 스크린샷 · 골든에 들지 않는다(유니티 기즈모가
 *          게임 뷰 캡처에 들지 않는 것과 같다). 캡처가 확인 대상인 그림(캔버스 테스트 그림 — Canvas 패스 자체를 본다)만 주 출력 층이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    struct CanvasDrawList;
    struct CanvasFrameData;
    struct CanvasTargetDrawList;

    class FontService;

    /** @brief 겹치기가 칠할 층입니다. */
    enum class CanvasOverlayLayer : uint8
    {
        MainOutput, ///< 주 출력(UI 위) — 사진 캡처 · `<ExpectImage>` 골든에 든다. 그림 자체가 확인 대상일 때만 쓴다(캔버스 테스트 그림).
        DevOverlay  ///< 개발 겹치기 층(디버그 HUD 와 같은 목록 — `CanvasFrameData::getDevOverlay`) — 사진 캡처 뒤에 화면에만 합친다. 개발 그림의 기본 자리다.
    };

    /** @struct CanvasOverlayContext @brief 겹치기 하나에 넘기는 이번 프레임 값입니다(게임 스레드, 패킷을 채우는 동안만 유효). */
    struct CanvasOverlayContext
    {
        const float4x4* _pViewProj{ nullptr };    ///< 주 시점의 뷰-투영입니다. 패킷의 시점이 카메라에서 오지 않았으면 nullptr 입니다(월드 도형을 투영할 눈이 없다).
        float2          _targetSize{};            ///< 주 출력의 픽셀 크기입니다(물리 픽셀 = UI 단위).
        FontService*    _pFontService{ nullptr }; ///< 글꼴 시스템입니다. 없거나 초기화 전이면 글자를 칠하지 않습니다.
        uint64          _glyphFrameIndex{ 0 };    ///< 글리프 캐시의 프레임 시계입니다(`GlyphCache::getFrameIndex`).
    };

    /**
     * @class CanvasOverlayTarget
     * @brief 겹치기가 칠할 목록입니다. 층(`setLayer` — `CanvasOverlays::paintAll` 이 등록마다 맞춘다)마다 처음 `acquire` 할 때 그 층의 목록을 베껴 패킷에 싣고,
     *        같은 프레임의 다음 겹치기는 그 사본에 칠한다.
     * @details UI 목록은 공유 값이라 제자리에 칠하지 않는다. 칠하지 않는 프레임은 베끼지 않는다. 주 출력이 호스트 렌더 타깃(에디터 게임 뷰)이면 개발 겹치기 층은
     *          그 타깃이 아니라 자기 렌더 텍스처 대상(`CanvasFrameData::kDevOverlayTargetPath`)에 칠한다 — 디버그 HUD 와 같은 길이다.
     */
    class SW_API CanvasOverlayTarget
    {
    public:
        /** @param bHostRenderTarget 주 출력이 호스트 렌더 타깃이다(`RenderFramePacket::_outputRenderTarget` 이 0 이 아니다). */
        CanvasOverlayTarget( CanvasFrameData& inoutFrame, const float2& targetSize, bool bHostRenderTarget );
        ~CanvasOverlayTarget();
        CanvasOverlayTarget( const CanvasOverlayTarget& )            = delete;
        CanvasOverlayTarget& operator=( const CanvasOverlayTarget& ) = delete;

        /** @brief 다음 `acquire` · `markContentChanged` 가 다룰 층을 고릅니다. */
        void setLayer( CanvasOverlayLayer layer ) { _layer = layer; }
        /** @brief 지금 층의 칠할 목록입니다. 처음 부르면 그 층의 목록을 베껴 바꿔 싣는다(빈 목록이면 대상 크기를 맞춘다). */
        CanvasDrawList& acquire();
        /** @brief 지금 층의 사각형이 바뀌었음을 알립니다 — 내용 번호를 "모른다"(0)로 두어 렌더러가 늘 올리게 한다(겹친 도형은 프레임마다 바뀐다). */
        void markContentChanged();

    private:
        /** @brief 개발 겹치기 렌더 텍스처 대상(호스트 렌더 타깃일 때)입니다. 없으면 빈 대상을 더합니다. */
        CanvasTargetDrawList& acquireDevOverlayTarget();

    private:
        CanvasFrameData&           _frame;
        shared_ptr<CanvasDrawList> _pMainOverlay; ///< 주 출력의 베낀 사본(처음 `acquire` 전에는 비어 있다)
        shared_ptr<CanvasDrawList> _pDevOverlay;  ///< 개발 겹치기의 베낀 사본(백버퍼로 나갈 때, 처음 `acquire` 전에는 비어 있다)
        float2                     _targetSize;
        CanvasOverlayLayer         _layer;
        uint8                      _bHostRenderTarget : 1; ///< 주 출력이 호스트 렌더 타깃이다 — 개발 겹치기 층은 자기 렌더 텍스처 대상에 칠한다
        [[maybe_unused]] uint8     _reservedFlags     : 7;
    };

    /** @brief 겹치기 하나를 칠하는 함수입니다. 칠할 것이 없으면 `acquire` 를 부르지 않는다. 게임 스레드의 패킷 채우기가 프레임마다 부른다. */
    using CanvasOverlayPaintFunction = void ( * )( const CanvasOverlayContext& context, CanvasOverlayTarget& target );

    /** @struct CanvasOverlayRegistration @brief 등록 항목 하나입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct CanvasOverlayRegistration
    {
        const utf8*                _pName; ///< 겹치기 이름(예: `DebugDraw`)
        int32                      _order; ///< 칠하는 순서(작을수록 먼저 — 같은 층에서 뒤의 것이 위에 그려진다)
        CanvasOverlayLayer         _layer; ///< 칠할 층
        CanvasOverlayPaintFunction _pFunction;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CanvasOverlays
     * @brief 캔버스 겹치기 레지스트리입니다. 같은 이름이 둘이면 뒤의 것을 거절하고 오류를 남깁니다. 목록은 (순서, 이름) 순입니다. 게임 스레드와 정적 초기화에서만 쓴다.
     */
    class SW_API CanvasOverlays
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있으면 false 이고 등록하지 않습니다. */
        [[nodiscard]] static bool registerOverlay( const CanvasOverlayRegistration* pRegistration );
        static void               unregisterOverlay( const CanvasOverlayRegistration* pRegistration );
        /** @brief 등록 수입니다. 0 이면 패킷 채우기가 겹치기 값을 만들지도 않는다. */
        static uint32 getCount();
        /** @brief 등록된 겹치기를 순서대로, 등록마다 그 층(`CanvasOverlayRegistration::_layer`)에 칠합니다. */
        static void paintAll( const CanvasOverlayContext& context, CanvasOverlayTarget& target );
    };

    /** @brief `SW_CANVAS_OVERLAY` 가 두는 정적 등록자입니다(공통 틀 `StaticRegistrar`). */
    SW_DEFINE_STATIC_REGISTRAR( CanvasOverlayRegistrar, const CanvasOverlayRegistration*, &CanvasOverlays::registerOverlay, &CanvasOverlays::unregisterOverlay );
} // namespace sw

/** @brief 캔버스 겹치기를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다. */
#define SW_CANVAS_OVERLAY( id, pName, order, layer, pFunction ) \
    SW_STATIC_REGISTRATION( canvasOverlay, ::sw::CanvasOverlayRegistration, ::sw::CanvasOverlayRegistrar, id, pName, order, layer, pFunction )
