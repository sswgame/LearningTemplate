/**
 * @file UIInputRouter.h
 * @brief 입력을 UI 이벤트로 보내는 쪽입니다. 포커스, 포인터, 소비된 입력, UI 액션 맵, 입력 방식을 가집니다(`UISystem` 의 값 멤버).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Base/UIFocusManager.h"
#include "Engine/UI/Base/UIPointerState.h"
#include "Engine/UI/Base/WidgetNavigation.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Input/UIInputConsumption.h"

namespace sw
{
    struct RawInputEvent;

    class InputManager;
    class InputMap;
    class UIScreen;

    /** @brief UI 를 지금 어떤 방식으로 다루는지 나타냅니다(CommonUI 의 입력 방식). 탐색이면 포커스 테두리를 보이고, 포인터면 숨깁니다. */
    enum class UIInputMode : uint8
    {
        Pointer,   ///< 마우스, 터치
        Navigation ///< 키보드, 게임패드
    };
} // namespace sw

namespace sw
{
    /** @brief 이번 프레임에 `processPointer` 가 보낸 포인터 이벤트 하나와, 그 점 아래에 화면 위젯이 있었는지 나타냅니다. World 위젯 입력이 화면 위젯 아래가 아닌 이벤트만 가져간다. */
    struct UIRoutedPointerEvent
    {
        UIPointerEvent _event{};
        uint8          _bOverScreenWidget{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIInputRouter
     * @brief 입력 라우팅 규칙입니다(CommonUI 의 `UCommonUIActionRouter` 에 해당). 화면 스택은 갖지 않고, 부르는 쪽(`UISystem`)이 활성 화면과 화면 목록을 넘깁니다.
     * @details 프레임 순서는 `UISystem::processInput` 이 정한다(장치 갱신, 입력 방식, 포인터, 액션, 키보드 포커스).
     *          UI 가 사용한 액션의 물리 입력은 뗄 때까지 소비된 입력이다. 텍스트 입력 위젯이 포커스를 가지면 키보드 포커스 `UI` 를 잡고, 그동안 UI 액션은 Back 과 Tab 만 받는다.
     *          글자 콜백을 자기 주소로 등록하므로 옮기거나 복사하지 않는다.
     */
    class SW_API UIInputRouter
    {
    public:
        UIInputRouter();
        ~UIInputRouter();
        UIInputRouter( const UIInputRouter& )            = delete;
        UIInputRouter& operator=( const UIInputRouter& ) = delete;

        /**
         * @brief 입력을 연결하고 글자 콜백을 등록합니다. @p uiInputMapPath 가 비면 UI 액션이 없습니다(포인터만 동작).
         * @return 액션 맵을 지정했는데 읽지 못하면 false 입니다(입력은 연결된 채로 남습니다).
         */
        [[nodiscard]] bool initialize( InputManager& inputManager, string_view uiInputMapPath );
        /** @brief 키보드 포커스와 글자 콜백을 놓고 액션 맵과 소비 기록을 지웁니다. */
        void shutdown();

        /** @brief 이번 프레임의 소비 기록과 UI 액션 맵을 갱신하고 프레임 시간을 기억합니다(액션 이벤트에 싣습니다). */
        void update( float32 deltaSeconds );
        /**
         * @brief 이번 프레임 원시 이벤트에서 마지막으로 쓴 장치로 입력 방식을 정합니다(커서는 위치가 실제로 바뀐 이동만 인정합니다).
         * @return 방식을 가리키는 이벤트가 있었으면 true 이고 @p outMode 에 그 방식을 넣습니다. 지금 방식과 같을 수 있습니다.
         */
        [[nodiscard]] bool pollInputMode( UIInputMode& outMode );
        /** @brief 원시 이벤트 하나가 가리키는 입력 방식입니다. 입력 방식과 무관한 이벤트(작은 스틱 떨림 포함)면 false 입니다. */
        [[nodiscard]] static bool tryGetInputMode( const RawInputEvent& rawEvent, UIInputMode& outMode );
        /**
         * @brief 입력 방식을 바꾸고, 포커스 위젯의 스타일을 다시 맞추게 합니다(`:focus-visible` 은 입력 방식을 따릅니다).
         * @return 바뀌었으면 true 입니다. 탐색으로 바뀌었으면 부르는 쪽이 활성 화면의 포커스를 돌려줍니다.
         */
        bool setInputMode( UIInputMode mode );
        /** @brief 마우스 상태를 포인터 이벤트로 바꿔 @p listScreen(그리기 순서)으로 보냅니다. 창 픽셀은 @p viewport 의 배율로 UI 단위로 바꿉니다. */
        void processPointer( const vector<unique_ptr<UIScreen>>& listScreen, const UIViewport& viewport );
        /** @brief 화면이 없을 때 `UI.Pause` 가 오면 소비하고 true 를 반환합니다(부르는 쪽이 일시정지 메뉴를 엽니다). @p bHasPauseMenu 가 false 면 보지 않습니다. */
        [[nodiscard]] bool pollPauseAction( bool bHasPauseMenu );
        /** @brief UI 액션을 활성 화면 @p pActive 로 보냅니다(탐색, 스틱, 스크롤, 확인, 탭, 지우기, 뒤로). 없거나 액션을 받지 않는 화면이면 스틱 상태만 지웁니다. */
        void processActions( UIScreen* pActive, float32 deltaSeconds );
        /** @brief 포커스 위젯이 텍스트 입력 위젯이면 키보드 포커스 `UI` 를 잡고, 아니면 놓습니다. */
        void updateKeyboardFocus();
        /** @brief @p screen 이 활성이 될 때의 포커스를 돌려줍니다. 기억한 위젯이 있으면 그것이고, 없고 탐색 방식이면 기본 포커스(없으면 첫 위젯)입니다. */
        void restoreFocus( UIScreen& screen );
        /** @brief UI 맵 레이어를 맞춥니다. `UI` 는 활성 화면이 있을 때 켜고, `UIGlobal`(일시정지)은 활성 화면이 없고 일시정지 메뉴가 있을 때 켭니다. */
        void syncInputLayers( const UIScreen* pActive, bool bHasPauseMenu );
        /** @brief 지울 화면의 트리를 포인터와 포커스가 놓게 합니다. */
        void forgetScreen( UIScreen& screen );

        /** @brief @p inputMap 의 액션 @p action 을 지금 누르는 물리 입력이 모두 UI 에 소비된 것이면 true 입니다. */
        bool isActionConsumed( const InputMap& inputMap, const hashed_string& action ) const;
        /** @brief @p inputMap 의 액션 @p action 이 지금 쓰는 물리 입력을 소비합니다. */
        void consumeAction( const InputMap& inputMap, const hashed_string& action );

        UIFocusManager&       getFocusManager() { return _focus; }
        const UIFocusManager& getFocusManager() const { return _focus; }
        const UIPointerState& getPointerState() const { return _pointer; }
        /** @brief 이번 프레임(`update` 뒤)에 `processPointer` 가 보낸 포인터 이벤트입니다. 다음 `update` 가 비웁니다. */
        const vector<UIRoutedPointerEvent>& getFramePointerEvents() const { return _listFramePointerEvent; }
        /** @brief 마지막으로 처리한 포인터 위치입니다(UI 단위, 뷰포트 원점 기준). 툴팁이 이 옆에 뜬다. */
        const float2&             getLastPointerPosition() const { return _lastPointerPosition; }
        UIInputConsumption&       getConsumption() { return _consumption; }
        const UIInputConsumption& getConsumption() const { return _consumption; }
        UIInputMode               getInputMode() const { return _inputMode; }
        InputManager*             getInputManager() const { return _pInput; }
        /** @brief UI 액션 맵입니다(액션 맵을 지정하지 않았으면 nullptr). */
        InputMap* getUIInputMap() const { return _uiInputMap.get(); }

    private:
        /** @brief 탐색 액션 하나를 처리합니다. 포커스 경로에 먼저 보내고, 아무도 소비하지 않으면 포커스를 옮깁니다. 사용했으면 true 입니다. */
        bool handleNavigation( UIScreen& screen, const hashed_string& action, UINavigationDirection direction );
        /** @brief 스틱 탐색을 처리합니다. 크게 기울이면 큰 축 방향으로 한 번 이동하고, 그 뒤 탐색 반복 간격으로 반복합니다. */
        void processStickNavigation( UIScreen& screen, float32 deltaSeconds );
        /** @brief 스크롤 액션(`UI.Scroll`, 오른쪽 스틱)을 포커스 경로로 보냅니다. 포커스 경로가 없으면 포인터가 올라간 경로로 보냅니다. 스크롤 패널이 쓰면 소비합니다. */
        void processScroll( UIScreen& screen );
        /** @brief 액션 이벤트 하나를 활성 화면의 포커스 경로로 보냅니다. 처리됐으면 true 입니다. */
        bool routeAction( UIScreen& screen, const hashed_string& action, const float2& value );
        /** @brief 액션 이벤트 하나를 @p path 로 보냅니다(이번 입력 프레임 시간을 싣습니다). 처리됐으면 true 입니다. */
        bool routeActionAlong( UIScreen& screen, const UIWidgetPath& path, const hashed_string& action, const float2& value );
        /** @brief 키보드 포커스가 `UI` 일 때 오는 글자를 포커스 위젯에 보냅니다. */
        void onTextInput( string_view text );
        /** @brief 키보드 포커스가 `UI` 일 때 오는 조합 중 글자를 포커스 위젯에 보냅니다. */
        void onTextComposition( string_view text );
        /** @brief 글자 이벤트를 포커스 위젯 하나에 보냅니다. */
        void dispatchTextEvent( string_view text, bool bComposition );
        /** @brief 포인터 이벤트 하나를 받을 화면으로 보냅니다. 처리했으면 true 입니다. */
        bool dispatchPointerEvent( const vector<unique_ptr<UIScreen>>& listScreen, const UIPointerEvent& event );
        /** @brief 점 아래의 맨 위 화면을 반환합니다(입력을 막는 화면 아래로는 내려가지 않습니다). 없으면 nullptr 입니다. */
        static UIScreen* findPointerScreen( const vector<unique_ptr<UIScreen>>& listScreen, const float2& point );

    private:
        UIFocusManager               _focus;
        UIPointerState               _pointer;
        vector<UIRoutedPointerEvent> _listFramePointerEvent; ///< 이번 프레임에 보낸 포인터 이벤트입니다(`getFramePointerEvents`).
        UIInputConsumption           _consumption;
        unique_ptr<InputMap>         _uiInputMap; ///< UI 액션 맵입니다(레이어 `UI` 는 활성 화면이 있을 때만 켭니다).
        InputManager*                _pInput;
        Subscription                 _textInputSubscription;       ///< 입력 관리자의 UI 포커스 글자 칸 구독
        Subscription                 _textCompositionSubscription; ///< 입력 관리자의 UI 포커스 조합 글자 칸 구독
        float2                       _lastPointerPosition;
        int2                         _lastMousePixel;     ///< 입력 방식을 정할 때 본 마지막 커서 위치입니다(창 픽셀).
        float32                      _stickRepeatSeconds; ///< 스틱 탐색의 다음 반복까지 남은 시간입니다.
        float32                      _inputDeltaSeconds;  ///< 이번 `processInput` 의 프레임 시간입니다(액션 이벤트에 싣습니다).
        UINavigationDirection        _stickDirection;     ///< 스틱이 지금 가리키는 탐색 방향입니다(기울이지 않았으면 Next 이며 쓰지 않는 값입니다).
        UIInputMode                  _inputMode;
        uint8                        _bPointerKnown    : 1; ///< 마우스 위치를 한 번 읽었는지 나타냅니다(첫 프레임의 Move 기준).
        uint8                        _bStickHeld       : 1; ///< 스틱이 탐색 임계값을 넘어 기울어 있는지 나타냅니다.
        uint8                        _bMousePixelKnown : 1; ///< `_lastMousePixel` 을 한 번 읽었는지 나타냅니다.
    };
} // namespace sw
