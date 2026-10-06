/**
 * @file WidgetComponent.h
 * @brief 오브젝트에 붙는 UI 입니다 — 화면 마커(Screen)와 월드 사각형(World)(언리얼 UWidgetComponent · 유니티 World Space Canvas · Godot SubViewport → 3D).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    class UiSystem;
    class Widget;

    /** @brief 위젯 컴포넌트가 그리는 자리입니다. */
    ENUM()
    enum class WidgetSpace : uint8
    {
        Screen, ///< 월드 점을 화면에 투영해 HUD 처럼 — 크기 일정 · 가려지지 않는다
        World   ///< 렌더 텍스처에 그려 3D 사각형에 붙인다 — 깊이에 가려지고 원근이 있다
    };
} // namespace sw

namespace sw
{
    /** @brief 화면 마커 하나의 이번 프레임 자리입니다(`WidgetComponent::computeMarkerPlacement` 의 결과). */
    struct WidgetMarkerPlacement
    {
        float2  _position{};           ///< 마커의 기준점(피벗이 놓일 자리, UI 단위)
        float32 _scale{ 1.0f };        ///< 거리 배율(`_bScaleWithDistance` 일 때만 1 이 아니다)
        float32 _distance{ 0.0f };     ///< 카메라에서 점까지(m)
        uint8   _bVisible{ SW_TRUE };  ///< 보인다(카메라 뒤 · 화면 밖이고 가장자리에 붙이지 않거나, 최대 거리 밖이면 false)
        uint8   _bClamped{ SW_FALSE }; ///< 화면 밖이라 가장자리에 붙였다(방향 화살표 상태)
    };
} // namespace sw

namespace sw
{
    /**
     * @class WidgetComponent
     * @brief 오브젝트 위치에 위젯 하나를 띄웁니다. **Screen**: `UiSystem` 의 Hud 층 마커 화면(캔버스 패널)에 위젯을 자식으로 두고, 매 프레임 `UiSystem::update`
     *        (게임 틱 · 트랜스폼 적용 뒤 — 병렬 틱 밖)가 오브젝트의 월드 점을 게임 카메라로 투영해 그 슬롯을 옮긴다. **World**: 위젯 트리를 렌더 텍스처에 그려
     *        사각형 메시에 붙인다(렌더 텍스처 경로 `getRenderTargetPath`).
     * @details 위젯은 `setContent` 로 코드가 넘긴다(문서 `_documentPath` 를 푸는 것은 5-1). 화면 마커는 `_drawSize` 가 있으면 그 크기로 고정해 레이아웃 경계가 되고
     *          (자리가 바뀌어도 그 아래만 다시 놓는다), 없으면 위젯의 원하는 크기다. 카메라 뒤 · 화면 밖은 숨기거나(`_bClampToScreenEdge` 면 가장자리에 붙인다),
     *          `_maxDistance` 밖은 숨긴다. 2D(직교 카메라)도 같은 코드다. 서버처럼 UI 가 없으면 아무것도 하지 않는다.
     */
    REFLECT( Category = "UI", DisplayName = "Widget Component", Tooltip = "Shows a widget attached to this object, as a screen marker or a world quad" )
    class SW_API WidgetComponent : public Component
    {
    public:
        REFLECT_BODY();

        WidgetComponent();
        ~WidgetComponent() override;

        /** @brief UI 시스템에 등록합니다(화면 마커면 마커 화면에 위젯을 붙인다). */
        void onBeginPlay() override;
        /** @brief 등록을 풉니다(화면 마커 위젯을 뗀다). */
        void onEndPlay() override;

        /** @brief 띄울 위젯을 넘깁니다. 시작 뒤면 곧바로 마커로 붙고, 시작 전이면 시작할 때 붙는다(옛 위젯은 지운다). */
        void setContent( unique_ptr<Widget> content );
        /** @brief 지금 띄우는 위젯입니다(없으면 nullptr — 포인터는 그 호출 안에서만). */
        Widget* getContent() const;

        WidgetSpace   getSpace() const { return _space; }
        void          setSpace( WidgetSpace space ) { _space = space; }
        const float2& getDrawSize() const { return _drawSize; }
        void          setDrawSize( const float2& drawSize ) { _drawSize = drawSize; }
        const float2& getPivot() const { return _pivot; }
        void          setPivot( const float2& pivot ) { _pivot = pivot; }
        void          setScreenOffset( const float2& offset ) { _screenOffset = offset; }
        void          setClampToScreenEdge( bool bClamp ) { _bClampToScreenEdge = bClamp; }
        void          setMaxDistance( float32 maxDistance ) { _maxDistance = maxDistance; }
        void          setScaleWithDistance( bool bScale, float32 referenceDistance );

        /** @brief 마지막 갱신의 자리입니다(시험 · 방향 화살표). */
        const WidgetMarkerPlacement& getLastPlacement() const { return _lastPlacement; }
        /** @brief World 일 때 위젯을 그리는 렌더 텍스처 경로입니다(`rendertarget/widget_<컴포넌트 id>`). */
        string getRenderTargetPath() const;

        /**
         * @brief 화면 마커의 자리를 셉니다(순수 함수 — 시험이 직접 부른다).
         * @param viewProjection 게임 카메라의 뷰 · 투영(행 벡터 — `float4::transform`)
         * @param cameraPosition 카메라 월드 위치(거리 · 배율)
         * @param worldPosition  마커를 띄울 월드 점
         * @param viewportSize   UI 뷰포트 크기(UI 단위)
         */
        WidgetMarkerPlacement computeMarkerPlacement( const float4x4& viewProjection, const float3& cameraPosition, const float3& worldPosition,
                                                      const float2& viewportSize ) const;
        /** @brief 이번 프레임의 마커 자리를 적용합니다(`UiSystem::update` 가 부른다 — 카메라 · 오브젝트 위치를 스스로 찾는다). */
        void updateScreenMarker( const UiViewport& viewport );
        /** @brief 계산한 자리를 마커 위젯 슬롯에 적습니다(시험이 카메라 없이 부른다). */
        void applyPlacement( const WidgetMarkerPlacement& placement );
        /**
         * @brief 등록할 UI 시스템을 바꿉니다(옛 쪽에서 마커를 떼고 등록을 풀고, 새 쪽에 등록하고 마커를 붙인다). nullptr 이면 풀기만.
         * @details 시작할 때 엔진의 UI 시스템으로(서버처럼 없으면 아무것도 하지 않는다), 끝날 때 nullptr 로 부른다. 시험은 자기 UI 시스템을 넘긴다.
         */
        void      bindUiSystem( UiSystem* pUiSystem );
        UiSystem* getUiSystem() const { return _pUiSystem; }
        /** @brief UI 시스템이 먼저 내려간다 — 등록 · 마커를 알림 없이 잊습니다(`UiSystem::shutdown` 이 부른다). */
        void forgetUiSystem();

    private:
        /** @brief 콘텐츠를 UI 시스템의 마커 화면에 붙입니다(등록 · 콘텐츠가 다 있을 때). */
        void attachMarker();
        /** @brief 마커 위젯을 떼어 지웁니다. */
        void detachMarker();

    private:
        unique_ptr<Widget>    _pendingContent; ///< 아직 마커 화면에 붙이지 않은 위젯
        WidgetId              _markerWidget;   ///< 마커 화면에 붙인 위젯(없으면 무효)
        WidgetMarkerPlacement _lastPlacement;
        UiSystem*             _pUiSystem; ///< 등록한 UI 시스템(없으면 nullptr)
        PROPERTY( DisplayName = "Document", Tooltip = "UI document to show (document loading is a later stage; code uses setContent)" )
        string _documentPath;
        PROPERTY( DisplayName = "Space" )
        WidgetSpace _space;
        PROPERTY( DisplayName = "Draw Size", Tooltip = "Screen: fixed marker size (0 = desired size). World: render texture size", Meta = "Units=ui" )
        float2 _drawSize;
        PROPERTY( DisplayName = "Pivot", Tooltip = "Point of the widget placed on the anchor (0..1)" )
        float2 _pivot;
        PROPERTY( DisplayName = "World Size", Tooltip = "World quad size (World space)", Units = m )
        float2 _worldSize;
        PROPERTY( DisplayName = "Screen Offset", Meta = "Units=ui" )
        float2 _screenOffset;
        PROPERTY( DisplayName = "Max Distance", Tooltip = "Hide beyond this distance from the camera; 0 = no limit", Min = 0.0, Units = m )
        float32 _maxDistance;
        PROPERTY( DisplayName = "Reference Distance", Tooltip = "Distance at which a distance-scaled marker has scale 1", Min = 0.01, Units = m )
        float32 _referenceDistance;
        PROPERTY( DisplayName = "Clamp To Screen Edge", Tooltip = "Keep off-screen markers on the screen edge instead of hiding them" )
        bool _bClampToScreenEdge;
        PROPERTY( DisplayName = "Scale With Distance" )
        bool _bScaleWithDistance;
    };
} // namespace sw
