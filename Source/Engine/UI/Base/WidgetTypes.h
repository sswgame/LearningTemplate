/**
 * @file WidgetTypes.h
 * @brief 위젯 계층의 값 타입입니다. 번호, 보임, 무효화 이유, 기하(배치 결과), 렌더 변환, 뷰포트를 정의합니다.
 * @details 좌표는 UI 단위(UI 배율을 곱하기 전, 1080p 기준 픽셀과 같은 크기)이고 원점은 화면 왼쪽 위이며 y 는 아래가 + 이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 트리 안에서 위젯을 가리키는 번호입니다. 프로세스 안에서 다시 쓰지 않으며, 포커스, 포인터 캡처, 호버는 포인터가 아니라 이 번호를 보관합니다. */
    using WidgetID = uint32;
    /** @brief 위젯이 없음을 뜻하는 번호입니다(위젯 번호는 1 부터 시작합니다). */
    inline constexpr WidgetID kInvalidWidgetID = 0;

    /** @brief 보임과 히트 테스트 방식입니다(언리얼 ESlateVisibility 와 같은 다섯 가지). */
    ENUM()
    enum class WidgetVisibility : uint8
    {
        Visible,             ///< 보이고 클릭을 받습니다.
        Collapsed,           ///< 보이지 않고 공간도 차지하지 않습니다(레이아웃 크기 0).
        Hidden,              ///< 보이지 않지만 공간은 차지합니다.
        HitTestInvisible,    ///< 보이지만 자기와 자식 모두 클릭을 받지 않습니다(HUD 장식).
        SelfHitTestInvisible ///< 보이고 자기만 클릭을 받지 않으며 자식은 받습니다(겹친 패널).
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 위젯의 흐름 방향입니다(UMG 의 FlowDirection, Godot 의 layout_direction). 오른쪽에서 왼쪽이면 패널이 자식 배치를 거울로 놓고 텍스트의 문단 방향이 RTL 이 됩니다.
     * @details 루트의 Inherit 은 문화권(`LocalizationManager::isRightToLeft` → `UILayoutContext::_bRightToLeft`)을 따른다. 숫자 입력 필드나 시계처럼
     *          문화권과 상관없이 왼쪽에서 오른쪽이어야 하는 위젯은 LeftToRight 로 고정한다.
     */
    ENUM()
    enum class UIFlowDirection : uint8
    {
        Inherit,     ///< 부모(루트면 문화권)를 따릅니다.
        LeftToRight, ///< 왼쪽에서 오른쪽으로 고정합니다.
        RightToLeft  ///< 오른쪽에서 왼쪽으로 고정합니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WidgetDirty
     * @brief 무효화 이유(비트)입니다. 레이아웃과 그리기를 나누는 것이 유지형 UI 의 핵심입니다(언리얼의 EInvalidateWidgetReason).
     */
    struct WidgetDirty
    {
        static constexpr uint32 kNone        = 0;
        static constexpr uint32 kLayout      = SW_BIT( 0 ); ///< 원하는 크기가 바뀔 수 있습니다. 부모 방향으로 레이아웃 경계까지 올라갑니다.
        static constexpr uint32 kChildLayout = SW_BIT( 1 ); ///< 자식 중 하나가 kLayout 입니다. 이 위젯은 자기 원하는 크기를 다시 측정해야 하는지 자식에게 묻습니다.
        static constexpr uint32 kArrange     = SW_BIT( 2 ); ///< 크기는 같고 자식 위치만 다시 배치합니다(정렬, 스크롤 오프셋).
        static constexpr uint32 kPaint       = SW_BIT( 3 ); ///< 자기 그림만 다시 그립니다(색, 글자 색, 이미지 변경). 레이아웃은 그대로입니다.
        static constexpr uint32 kStyle       = SW_BIT( 4 ); ///< 계산된 스타일을 다시 구합니다(상태나 클래스 변경). 결과에 따라 kLayout 또는 kPaint 로 번집니다.
        static constexpr uint32 kTransform   = SW_BIT( 5 ); ///< 렌더 변환과 불투명도만 바뀝니다. 레이아웃을 건드리지 않습니다(애니메이션의 값싼 경로).
        static constexpr uint32 kVisibility  = SW_BIT( 6 ); ///< 보임이 바뀌었습니다. 보수적으로 레이아웃과 그리기를 모두 다시 합니다.
        static constexpr uint32 kLayoutRoot  = SW_BIT( 7 ); ///< (트리가 붙입니다) 다시 측정할 루트 목록에 이미 올라 있습니다. 같은 루트를 두 번 적지 않기 위한 표시입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIStyleState
     * @brief 스타일 선택자의 상태(`:hover` 등) 비트입니다. 위젯이 `Widget::computeStyleStates` 로 답하고, `:focus-visible` 은 스타일 순회가 입력 방식으로 더합니다.
     */
    struct UIStyleState
    {
        static constexpr uint32 kNone         = 0;
        static constexpr uint32 kHover        = SW_BIT( 0 ); ///< 포인터가 이 위젯(또는 자손) 위에 있습니다.
        static constexpr uint32 kPressed      = SW_BIT( 1 ); ///< 누른 상태입니다(버튼).
        static constexpr uint32 kFocus        = SW_BIT( 2 ); ///< 포커스를 가지고 있습니다.
        static constexpr uint32 kFocusVisible = SW_BIT( 3 ); ///< 포커스를 가지고 있고 입력 방식이 탐색입니다(포커스 테두리가 보이는 경우).
        static constexpr uint32 kDisabled     = SW_BIT( 4 ); ///< 자기나 조상이 꺼져 있습니다.
        static constexpr uint32 kChecked      = SW_BIT( 5 ); ///< 체크 상자가 켜져 있습니다.
        static constexpr uint32 kSelected     = SW_BIT( 6 ); ///< 선택한 항목입니다(목록, 콤보 선택지).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIRect
     * @brief 축 정렬 사각형입니다(UI 단위, y 는 아래가 +). 포커스 탐색과 자르기 판정이 씁니다.
     */
    struct SW_API UIRect
    {
        float32 _left{ 0.0f };
        float32 _top{ 0.0f };
        float32 _right{ 0.0f };
        float32 _bottom{ 0.0f };

        static UIRect makeFromPositionSize( float32 x, float32 y, float32 width, float32 height ) { return UIRect{ x, y, x + width, y + height }; }
        /** @brief 두 구간 [aMin, aMax] 와 [bMin, bMax] 사이의 틈입니다. 겹치면 0 입니다. */
        static float32 computeRangeGap( float32 aMin, float32 aMax, float32 bMin, float32 bMax );

        float32 getLeft() const { return _left; }
        float32 getTop() const { return _top; }
        float32 getRight() const { return _right; }
        float32 getBottom() const { return _bottom; }
        float2  getCenter() const { return float2{ ( _left + _right ) * 0.5f, ( _top + _bottom ) * 0.5f }; }
        /** @brief 두 사각형이 넓이를 가지고 겹치면 true 입니다(변만 닿으면 false). */
        bool intersects( const UIRect& other ) const { return _left < other._right && other._left < _right && _top < other._bottom && other._top < _bottom; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WidgetGeometry
     * @brief 배치 결과입니다. 레이아웃 사각형과 화면으로 가는 누적 렌더 변환을 가집니다.
     * @details 로컬 점(레이아웃 사각형 왼쪽 위가 원점) p 의 화면 점은 _translation + p.x × _axisX + p.y × _axisY 이다.
     *          렌더 변환이 없으면 축은 단위 벡터이고 _translation 이 곧 _position 이다.
     */
    struct SW_API WidgetGeometry
    {
        float2 _position{};          ///< 레이아웃 사각형의 왼쪽 위입니다(렌더 변환을 적용하기 전, 화면 UI 단위).
        float2 _size{};              ///< 레이아웃 사각형 크기입니다.
        float2 _axisX{ 1.0f, 0.0f }; ///< 로컬 x 1 이 화면에서 가리키는 벡터입니다.
        float2 _axisY{ 0.0f, 1.0f }; ///< 로컬 y 1 이 화면에서 가리키는 벡터입니다.
        float2 _translation{};       ///< 로컬 원점의 화면 점입니다.

        /** @brief 렌더 변환이 없는 기하를 만듭니다(화면 사각형 그대로). */
        static WidgetGeometry makeAxisAligned( const float2& position, const float2& size );

        /** @brief 로컬 점(레이아웃 사각형 왼쪽 위 원점)을 화면 점으로 바꿉니다. */
        float2 transformPoint( const float2& local ) const;
        /** @brief 화면 점을 로컬 점으로 바꿉니다(히트 테스트). 축이 퇴화(넓이 0)하면 false 입니다. */
        [[nodiscard]] bool inverseTransformPoint( const float2& screen, float2& outLocal ) const;
        /** @brief 로컬 점이 레이아웃 사각형 안이면 true 입니다(왼쪽과 위쪽 변은 포함, 오른쪽과 아래쪽 변은 제외). */
        bool containsLocal( const float2& local ) const { return 0.0f <= local._x && local._x < _size._x && 0.0f <= local._y && local._y < _size._y; }
        /** @brief 축이 단위(회전, 기울임, 배율이 없음)면 true 입니다. */
        bool isAxisAligned() const;
        /** @brief 레이아웃 사각형의 네 꼭짓점을 화면으로 옮긴 축 정렬 경계 상자입니다. */
        UIRect computeScreenBounds() const;

        bool operator==( const WidgetGeometry& other ) const;
        bool operator!=( const WidgetGeometry& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WidgetRenderTransform
     * @brief 렌더 변환입니다. 레이아웃이 끝난 뒤 그리기와 히트 테스트에만 적용합니다(UMG 의 Render Transform).
     * @details 피벗(위젯 사각형 안의 0..1) 기준으로 배율, 기울임, 회전, 이동 순서로 적용한다.
     */
    REFLECT()
    struct SW_API WidgetRenderTransform
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Translation", Meta = "Units=ui" )
        float2 _translation{};
        PROPERTY( DisplayName = "Scale" )
        float2 _scale{ 1.0f, 1.0f };
        PROPERTY( DisplayName = "Shear" )
        float2 _shear{};
        PROPERTY( DisplayName = "Angle", Units = rad )
        float32 _angle{ 0.0f }; ///< 회전입니다(라디안, 피벗 둘레). 엔진의 각도는 모두 라디안입니다.
        PROPERTY( DisplayName = "Pivot", Tooltip = "Pivot in the widget's own rect (0..1)" )
        float2 _pivot{ 0.5f, 0.5f };

        /** @brief 아무것도 바꾸지 않는 변환이면 true 입니다(피벗은 보지 않습니다). */
        bool isIdentity() const;
        /**
         * @brief 이 변환을 레이아웃 사각형 @p geometry 에 겹친 기하를 만듭니다.
         * @details 피벗 점(사각형 안의 _pivot 비율)을 고정한 채 배율, 기울임, 회전을 적용하고 _translation 만큼 옮긴다.
         */
        WidgetGeometry applyTo( const WidgetGeometry& geometry ) const;

        bool operator==( const WidgetRenderTransform& other ) const;
        bool operator!=( const WidgetRenderTransform& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIViewport
     * @brief UI 가 그려질 화면 하나입니다. 게임 창이면 백버퍼, 에디터면 게임 뷰 렌더 타깃입니다.
     * @details 물리 크기는 UI 크기 × 배율이다. `UIScaleUtil::makeViewport` 가 해상도 규칙, 사용자 배율, 안전 영역으로 채운다.
     */
    struct UIViewport
    {
        float2  _size{};          ///< UI 단위 크기(물리 크기 / 배율)입니다. 레이아웃 루트가 놓이는 사각형입니다.
        float2  _physicalSize{};  ///< 물리 픽셀 크기입니다.
        float4  _safeInsets{};    ///< 안전 영역(왼쪽, 위, 오른쪽, 아래, UI 단위)입니다. `SafeZonePanel` 이 안쪽으로 밀어 넣습니다.
        float32 _uiScale{ 1.0f }; ///< UI 단위에서 물리 픽셀로의 배율입니다.
        /// 이 화면의 왼쪽 위가 놓인 창 클라이언트 픽셀입니다. 게임 창(백버퍼)이면 0 이고, 에디터 게임 뷰면 패널 안 이미지 자리(`HostViewTarget::_windowX`)입니다.
        /// 포인터는 창 픽셀에서 이것을 뺀 뒤 배율로 나눕니다(`UIInputRouter::processPointer`).
        int2 _pointerOrigin{};
    };
} // namespace sw
