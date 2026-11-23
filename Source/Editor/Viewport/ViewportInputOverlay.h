/**
 * @file ViewportInputOverlay.h
 * @brief 게임 뷰포트 위에 입력에 따라 움직이는 가상 컨트롤러와 커맨드 기록 HUD 를 그리는 에디터 위젯입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorSubsystem.h"

namespace sw
{
    class InputManager;
} // namespace sw

struct ImDrawList;
struct ImVec2;

namespace sw::editor
{
    /** @brief 뷰포트 오버레이 HUD 앵커 위치 */
    enum class ViewportOverlayPosition : uint8
    {
        BottomRight = 0,
        BottomLeft,
        TopRight,
        TopLeft
    };

    /** @brief 오버레이가 그리는 페이스 버튼(다이아몬드 차례)입니다. `ViewportInputOverlayState::_faceButtonDownMask` 의 비트 자리다. */
    enum class ViewportOverlayFaceButton : uint8
    {
        A = 0,
        B,
        X,
        Y,
        Count
    };

    /**
     * @struct ViewportInputOverlayState
     * @brief 오버레이가 이번 프레임에 그릴 입력 값입니다. 메인 스레드의 모델이 입력 관리자를 읽어 채우고(`gather`), 그리는 쪽은 이 값만 본다 — 입력 관리자 · 입력 맵 포인터를 받지 않는다.
     * @details 패드가 없으면 스틱은 가운데, 트리거는 0, 버튼은 모두 뗀 값이다.
     */
    struct ViewportInputOverlayState
    {
        float2         _leftStick{};
        float2         _rightStick{};
        float32        _leftTrigger{ 0.0f };
        float32        _rightTrigger{ 0.0f };
        uint8          _faceButtonDownMask{ 0 }; ///< 눌린 페이스 버튼(비트 = `ViewportOverlayFaceButton`)
        vector<string> _listActiveAction;        ///< 지금 눌린 입력 맵 액션의 표시 글("글리프 액션 이름"), 입력 맵 차례

        /** @brief 페이스 버튼 @p button 이 눌렸으면 true 입니다. */
        bool isFaceButtonDown( ViewportOverlayFaceButton button ) const { return ( _faceButtonDownMask & ( 1u << static_cast<uint32>( button ) ) ) != 0; }
        /**
         * @brief @p input 의 첫 패드와 입력 맵을 읽어 @p outState 를 채웁니다(Model — `Model/ViewportInputOverlaySettings.cpp`).
         * @param bWithActions 눌린 액션 목록까지 모으면 true 입니다(오버레이의 커맨드 기록이 켜졌을 때).
         */
        static void gather( const InputManager& input, bool bWithActions, ViewportInputOverlayState& outState );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ViewportInputOverlay
     * @brief 뷰포트 오버레이 HUD 설정과 그 설정대로 뷰포트 렌더 타깃 위에 ImDrawList 로 컨트롤러 HUD 를 그리는 일입니다.
     */
    struct ViewportInputOverlay
    {
        float32                 _opacity{ 0.85f };
        float32                 _scale{ 1.0f };
        ViewportOverlayPosition _position{ ViewportOverlayPosition::BottomRight };
        uint8                   _bEnabled            : 1;
        uint8                   _bShowStick          : 1;
        uint8                   _bShowButtons        : 1;
        uint8                   _bShowCommandHistory : 1;
        [[maybe_unused]] uint8  _reserved            : 4;

        ViewportInputOverlay()
            : _opacity{ 0.85f }
            , _scale{ 1.0f }
            , _position{ ViewportOverlayPosition::BottomRight }
            , _bEnabled{ SW_FALSE }
            , _bShowStick{ SW_TRUE }
            , _bShowButtons{ SW_TRUE }
            , _bShowCommandHistory{ SW_TRUE }
            , _reserved{ 0 }
        {
        }

        /** @brief 뷰포트 영역 위에 이 설정대로 @p state(입력 값)를 그립니다. 꺼져 있으면 그리지 않는다. */
        void draw( ImDrawList* pDrawList, const ImVec2& viewportScreenPos, const ImVec2& viewportSize, const ViewportInputOverlayState& state ) const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ViewportInputOverlaySettings
     * @brief 오버레이 설정 한 벌입니다(입력 맵 패널이 고치고 게임 뷰가 읽는다). 에디터 서브시스템이라 컨텍스트와 함께 생기고 사라진다 — 정적 칸에 두지 않는다.
     */
    class ViewportInputOverlaySettings final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ViewportInputOverlaySettings";

        ViewportInputOverlay _overlay{};
        const utf8*          getSubsystemName() const override { return kSubsystemName; }
        void                 resetToFreshEditor() override { _overlay = ViewportInputOverlay{}; }
    };
} // namespace sw::editor
