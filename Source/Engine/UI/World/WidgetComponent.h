/**
 * @file WidgetComponent.h
 * @brief 오브젝트에 붙는 UI 입니다. 화면 마커(Screen)와 월드 사각형(World) 두 가지가 있으며, 언리얼의 UWidgetComponent, 유니티의 World Space Canvas, Godot 의 SubViewport 를 3D 에 붙이는 방식에 해당합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    struct UILayoutContext;
    struct UIPaintContext;

    class UISystem;
    class Widget;
    class WidgetTree;

    /** @brief 위젯 컴포넌트가 그리는 방식입니다. */
    ENUM()
    enum class WidgetSpace : uint8
    {
        Screen, ///< 월드 점을 화면에 투영해 HUD 처럼 보입니다. 크기가 일정하고 가려지지 않습니다.
        World   ///< 렌더 텍스처에 그려 3D 사각형에 붙입니다. 깊이에 가려지고 원근이 있습니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 화면 마커 하나의 이번 프레임 위치입니다(`WidgetComponent::computeMarkerPlacement` 의 결과). */
    struct WidgetMarkerPlacement
    {
        float2  _position{};           ///< 마커의 기준점입니다(피벗이 놓일 위치, UI 단위).
        float32 _scale{ 1.0f };        ///< 거리 배율입니다(`_bScaleWithDistance` 일 때만 1 이 아닙니다).
        float32 _distance{ 0.0f };     ///< 카메라에서 점까지의 거리(m)입니다.
        float32 _edgeAngle{ 0.0f };    ///< 가장자리에 붙였을 때 화면 가운데에서 점 쪽을 향하는 각도입니다(화면 위가 0, 시계 방향, 라디안). 방향 화살표의 렌더 변환 각도로 씁니다.
        uint8   _bVisible{ SW_TRUE };  ///< 보이는지 나타냅니다(카메라 뒤나 화면 밖이고 가장자리에 붙이지 않거나, 최대 거리 밖이면 false).
        uint8   _bClamped{ SW_FALSE }; ///< 화면 밖이라 가장자리에 붙였는지 나타냅니다(방향 화살표 상태).
    };
} // namespace sw

namespace sw
{
    /**
     * @class WidgetComponent
     * @brief 오브젝트 위치에 위젯 하나를 띄웁니다.
     * @details **Screen**: `UISystem` 의 HUD 레이어 마커 화면(캔버스 패널)에 위젯을 자식으로 두고, 매 프레임 `UISystem::update`
     *          (게임 틱과 트랜스폼 적용 뒤, 병렬 틱 밖)가 오브젝트의 월드 점을 게임 카메라로 투영해 그 슬롯을 옮긴다.
     *          **World**: 위젯 트리를 렌더 텍스처에 그려 사각형 메시에 붙인다(렌더 텍스처 경로는 `getRenderTargetPath`).
     *
     *          위젯은 `setContent` 로 코드가 넘긴다(문서 `_documentPath` 를 해석하는 것은 5-1 단계에서 한다). 화면 마커는 `_drawSize` 가 있으면 그 크기로 고정해 레이아웃 경계가 되고
     *          (위치가 바뀌어도 그 아래만 다시 배치한다), 없으면 위젯의 원하는 크기를 쓴다. 기준점은 오브젝트 위치 + `_worldOffset`(월드 축, 머리 위 HP 바)이다.
     *          카메라 뒤나 화면 밖은 숨기고(`_bClampToScreenEdge` 면 가장자리에 붙인다), `_maxDistance` 밖도 숨긴다. 코드가 `setHidden` 으로 숨길 수도 있다
     *          (HP 바의 "맞으면 보임" 정책이며 저장하지 않는다). 2D(직교 카메라)도 같은 코드로 처리한다. 서버처럼 UI 가 없으면 아무것도 하지 않는다.
     */
    REFLECT( Category = "UI", DisplayName = "Widget Component", Tooltip = "Shows a widget attached to this object, as a screen marker or a world quad", Meta = "Icon=Widget" )
    class SW_API WidgetComponent : public Component
    {
    public:
        REFLECT_BODY();

        WidgetComponent();
        ~WidgetComponent() override;

        /** @brief UI 시스템에 등록합니다(화면 마커면 마커 화면에 위젯을 붙입니다). */
        void onBeginPlay() override;
        /** @brief 등록을 해제합니다(화면 마커 위젯을 뗍니다). */
        void onEndPlay() override;

        /** @brief 띄울 위젯을 넘깁니다. 시작 뒤면 곧바로 마커로 붙고, 시작 전이면 시작할 때 붙습니다(기존 위젯은 지웁니다). */
        void setContent( unique_ptr<Widget> content );
        /** @brief 지금 띄우는 위젯입니다(없으면 nullptr). 포인터는 그 호출 안에서만 씁니다. */
        Widget* getContent() const;

        WidgetSpace   getSpace() const { return _space; }
        void          setSpace( WidgetSpace space ) { _space = space; }
        const float2& getDrawSize() const { return _drawSize; }
        void          setDrawSize( const float2& drawSize ) { _drawSize = drawSize; }
        const float2& getPivot() const { return _pivot; }
        void          setPivot( const float2& pivot ) { _pivot = pivot; }
        void          setScreenOffset( const float2& offset ) { _screenOffset = offset; }
        const float3& getWorldOffset() const { return _worldOffset; }
        void          setWorldOffset( const float3& offset ) { _worldOffset = offset; }
        /** @brief 화면 마커를 숨깁니다(코드의 보이기 정책이며 저장하지 않습니다). 다음 `updateScreenMarker` 가 접습니다. */
        void setHidden( bool bHidden ) { _bHidden = bHidden ? SW_TRUE : SW_FALSE; }
        bool isHidden() const { return _bHidden == SW_TRUE; }
        void setClampToScreenEdge( bool bClamp ) { _bClampToScreenEdge = bClamp; }
        void setMaxDistance( float32 maxDistance ) { _maxDistance = maxDistance; }
        void setScaleWithDistance( bool bScale, float32 referenceDistance );

        /** @brief 마지막 갱신의 위치입니다(테스트와 방향 화살표가 씁니다). */
        const WidgetMarkerPlacement& getLastPlacement() const { return _lastPlacement; }
        /** @brief World 일 때 위젯을 그리는 렌더 텍스처 경로입니다(`rendertarget/widget_<컴포넌트 id>`). */
        string getRenderTargetPath() const;

        /**
         * @brief 화면 마커의 위치를 계산합니다(순수 함수이며 테스트가 직접 부릅니다).
         * @param viewProjection 게임 카메라의 뷰와 투영 행렬입니다(행 벡터, `float4::transform`).
         * @param cameraPosition 카메라 월드 위치입니다(거리와 배율 계산용).
         * @param worldPosition  마커를 띄울 월드 점입니다.
         * @param viewportSize   UI 뷰포트 크기입니다(UI 단위).
         */
        WidgetMarkerPlacement computeMarkerPlacement( const float4x4& viewProjection, const float3& cameraPosition, const float3& worldPosition,
                                                      const float2& viewportSize ) const;
        /** @brief 이번 프레임의 마커 위치를 적용합니다(`UISystem::update` 가 부르며, 카메라와 오브젝트 위치는 스스로 찾습니다). */
        void updateScreenMarker( const UIViewport& viewport );
        /** @brief 계산한 위치를 마커 위젯 슬롯에 적습니다(테스트가 카메라 없이 부릅니다). 보이면 끝에 `onMarkerPlaced` 를 부릅니다. */
        void applyPlacement( const WidgetMarkerPlacement& placement );
        /**
         * @brief 등록할 UI 시스템을 바꿉니다. 기존 시스템에서 마커를 떼고 등록을 해제한 뒤, 새 시스템에 등록하고 마커를 붙입니다. nullptr 이면 해제만 합니다.
         * @details 시작할 때 엔진의 UI 시스템으로 부르고(서버처럼 없으면 아무것도 하지 않는다), 끝날 때 nullptr 로 부른다. 테스트는 자기 UI 시스템을 넘긴다.
         */
        void      bindUISystem( UISystem* pUISystem );
        UISystem* getUISystem() const { return _pUISystem; }
        /** @brief UI 시스템이 먼저 종료된 경우 등록과 마커를 알림 없이 잊습니다(`UISystem::shutdown` 이 부릅니다). */
        void forgetUISystem();

        // --- World(렌더 텍스처 사각형) -----------------------------------------------
        /** @brief World 의 렌더 텍스처 크기입니다(픽셀 = UI 단위, 배율 1). `_drawSize` 를 쓰며, 없으면 기본 256 × 128 입니다. */
        float2 getWorldTextureSize() const;
        /**
         * @brief World 위젯 트리를 배치하고 칠해 렌더 텍스처 목록을 갱신합니다(`UISystem::update` 가 부르며, 배율 1, 뷰포트는 텍스처 크기).
         * @details 내용이 지난번과 같으면 번호를 올리지 않으므로 렌더러가 텍스처를 다시 그리지 않는다.
         */
        void updateWorldCanvas( const UILayoutContext& baseLayout, const UIPaintContext& basePaint );
        /** @brief World 목록을 렌더 텍스처 대상으로 덧붙입니다(`UISystem::collectWorldCanvases`). World 가 아니거나 트리가 없으면 아무것도 하지 않습니다. */
        void appendWorldCanvas( vector<CanvasTargetDrawList>& inoutListTarget ) const;
        /** @brief World 목록의 내용 번호입니다(테스트용). */
        uint64 getWorldCanvasRevision() const { return _worldRevision; }
        /** @brief World 사각형 오브젝트입니다(없으면 무효이며, 시작할 때 오브젝트 아래에 만듭니다). */
        GameObjectHandle getWorldQuad() const { return _worldQuad; }

    protected:
        /** @brief 화면 마커를 이번 프레임 위치에 놓았을 때 부릅니다(보일 때만). 파생 클래스(목표 마커)가 방향 화살표와 거리 텍스트를 쓰는 곳입니다. 기본은 아무것도 하지 않습니다. */
        virtual void onMarkerPlaced( Widget& marker, const WidgetMarkerPlacement& placement );

    private:
        /** @brief 콘텐츠를 UI 시스템의 마커 화면에 붙입니다(등록과 콘텐츠가 모두 있을 때). */
        void attachMarker();
        /** @brief 마커 위젯을 떼어 지웁니다. */
        void detachMarker();
        /** @brief World 사각형(자식 오브젝트: 스프라이트 사각형 메시와, 렌더 텍스처를 읽는 프리멀티플라이 스프라이트 머티리얼 인스턴스)을 만듭니다. */
        void createWorldQuad();
        /** @brief World 사각형 오브젝트를 지웁니다. */
        void destroyWorldQuad();

    private:
        unique_ptr<Widget>     _pendingContent; ///< 아직 마커 화면에 붙이지 않은 위젯입니다.
        WidgetID               _markerWidget;   ///< 마커 화면에 붙인 위젯입니다(없으면 무효).
        WidgetMarkerPlacement  _lastPlacement;
        UISystem*              _pUISystem;     ///< 등록한 UI 시스템입니다(없으면 nullptr).
        unique_ptr<WidgetTree> _worldTree;     ///< World 위젯 트리입니다(루트는 콘텐츠).
        CanvasDrawList         _worldCanvas;   ///< World 의 마지막 그리기 목록입니다(텍스처 픽셀).
        CanvasDrawList         _worldScratch;  ///< World 의 칠하는 중인 목록입니다.
        uint64                 _worldRevision; ///< World 의 `_worldCanvas` 내용 번호입니다.
        GameObjectHandle       _worldQuad;     ///< World 의 사각형 자식 오브젝트입니다.
        uint8                  _bHidden;       ///< 코드가 숨겼는지 나타냅니다(`setHidden`, 저장하지 않습니다).
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
        PROPERTY( DisplayName = "World Offset", Tooltip = "Screen: offset of the anchor from the object position, on the world axes", Units = m )
        float3 _worldOffset;
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
