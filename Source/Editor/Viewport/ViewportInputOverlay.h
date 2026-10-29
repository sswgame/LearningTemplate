/**
 * @file ViewportInputOverlay.h
 * @brief 게임 뷰포트 위에 입력에 따라 움직이는 가상 컨트롤러와 커맨드 기록 HUD 를 그리는 에디터 위젯입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorSubsystem.h"

namespace sw
{
    class InputManager;
    class InputMap;
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

    /** @brief 뷰포트 오버레이 HUD 설정 */
    struct ViewportInputOverlayConfig
    {
        float32                 _opacity{ 0.85f };
        float32                 _scale{ 1.0f };
        ViewportOverlayPosition _position{ ViewportOverlayPosition::BottomRight };
        uint8                   _bEnabled            : 1;
        uint8                   _bShowStick          : 1;
        uint8                   _bShowButtons        : 1;
        uint8                   _bShowCommandHistory : 1;
        [[maybe_unused]] uint8  _reserved            : 4;

        ViewportInputOverlayConfig()
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
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ViewportInputOverlay
     * @brief 뷰포트 렌더 타깃 위에 ImDrawList 로 컨트롤러 HUD 를 그립니다.
     */
    class ViewportInputOverlay
    {
    public:
        /** @brief 뷰포트 영역 위에 @p config 대로 오버레이를 렌더링합니다. */
        static void draw( ImDrawList* pDrawList, const ImVec2& viewportScreenPos, const ImVec2& viewportSize, const InputManager* pInput, const InputMap* pInputMap, const ViewportInputOverlayConfig& config );
    };

    /**
     * @class ViewportInputOverlaySettings
     * @brief 오버레이 설정 한 벌입니다(입력 맵 패널이 고치고 게임 뷰가 읽는다). 에디터 서브시스템이라 컨텍스트와 함께 생기고 사라진다 — 정적 칸에 두지 않는다.
     */
    class ViewportInputOverlaySettings final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ViewportInputOverlaySettings";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        ViewportInputOverlayConfig _config{};
    };
} // namespace sw::editor
