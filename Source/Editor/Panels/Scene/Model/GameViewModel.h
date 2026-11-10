/**
 * @file GameViewModel.h
 * @brief Game 창의 모델 — 화면 비율, 카메라 없음 안내 판정, 떼어 낸 창의 포인터 · 키를 게임 입력에 넣는 짝 기억, 디버그 드로우 겹치기 명령. ImGui 를 모릅니다.
 * @details 패널(`GameViewPanel`)은 ImGui 창 · 뷰포트 상태를 읽어 값으로 넘기고 이미지 · 툴바를 그린다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (유니티 Game 뷰가 PlayModeView 상태(화면 비율 · 포커스)와 IMGUI 그리기를 나누는 것과 같다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/Workspace/EditorViewTargetUtil.h"

namespace sw
{
    class InputManager;
} // namespace sw

namespace sw::editor
{
    struct EditorPointerState;
    struct ViewportInputOverlay;

    /**
     * @class GameViewModel
     * @brief Game 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class GameViewModel
    {
    public:
        GameViewModel();

        EditorGameViewAspect getAspect() const { return _aspect; }
        void                 setAspect( EditorGameViewAspect aspect ) { _aspect = aspect; }

        /** @brief 활성 씬에 쓸 수 있는 게임 카메라가 없는지 다시 봅니다. 그 값은 `isNoCameraHintShown` 이 돌려줍니다. */
        void refreshNoCameraHint();
        /** @brief 마지막 판정에 게임 카메라가 없었으면(꺼짐 · 지움) true 입니다 — 패널이 "No camera rendering" 을 띄운다. */
        bool isNoCameraHintShown() const { return _bNoCameraHintShown; }

        /**
         * @brief 패널이 다른 OS 창(ImGui 보조 뷰포트)에 있으면 그 창의 마우스(이동 · 단추 · 휠)를 주 창 클라이언트 좌표로 옮겨 게임 입력에 넣습니다.
         * @param bDetached 패널 창이 주 뷰포트가 아니다.
         * @param bHovered 포인터가 패널 창(자식 포함) 위에 있다.
         * @param mainViewportPos 주 뷰포트의 화면 자리 — 입력 장치의 좌표 약속("주 창 클라이언트 픽셀")으로 옮길 때 뺀다.
         * @details 보조 창의 마우스 메시지는 ImGui 백엔드의 창 프로시저만 받아 게임 입력(`InputManager`)에 닿지 않는다. 이미지 자리(`markViewDrawn`)와 같은 기준이라 런타임 UI 포인터가 맞는다.
         *          주 창에 있으면 주 창 메시지가 이미 간다. 넣은 누름의 짝 뗌은 패널 밖이어도 넣는다 — 안 넣으면 게임에서 단추가 눌린 채 남는다.
         */
        void forwardDetachedPointer( bool bDetached, bool bHovered, const EditorPointerState& pointer, float2 mainViewportPos );
        /**
         * @brief 떼어 낸 창이 포커스일 때 읽은 키 · 글자 사건 @p listEvent 를 게임 입력에 넣습니다(`DetachedKeyForwarder`).
         * @details @p bForward 가 거짓(포커스를 잃었거나 패널을 주 창에 되돌림)이면 누른 채 남은 키를 놓는다.
         */
        void forwardDetachedKeyboard( bool bForward, const vector<RawInputEvent>& listEvent );

        /**
         * @brief 게임 뷰의 디버그 드로우 겹치기를 켜거나 끕니다(서버 디버깅 S6).
         * @details 이 에디터의 게임 뷰와, 멀티 프로세스 재생이 띄운 자식 프로세스(클라이언트의 권위 고스트) 모두에 같은 값을 건다.
         */
        static void setDebugDrawShown( bool bShown );
        /** @brief 개발 겹치기 렌더 텍스처(`CanvasFrameData::kDevOverlayTargetPath`)의 RHI 핸들입니다. 아직 없거나 GPU 에 올라가지 않았으면 0 입니다. */
        static uint64 findDevOverlayTexture();
        /** @brief 에디터 범위의 입력 관리자입니다(커서 모양 · 입력 오버레이). 없으면 nullptr 입니다. View 가 서비스 창구를 보지 않게 여기서 찾는다. */
        static InputManager* findInputManager();
        /** @brief 뷰포트 입력 오버레이(눌린 키 · 패드 표시)입니다. 없으면 nullptr 입니다. */
        static ViewportInputOverlay* findInputOverlay();
        /** @brief 게임 뷰가 그리는 카메라(활성 씬의 게임 카메라)의 월드 위치를 @p outPosition 에 담습니다. 없으면 false 입니다(감시 값이 읽는다). */
        [[nodiscard]] static bool findCameraPosition( float3& outPosition );
        /** @brief 주 출력(게임 뷰)의 보기 모드 번호(`RenderViewMode`)를 @p outMode 에 담습니다. 렌더러가 없으면(필수 서비스 — 버그) 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool findViewMode( uint32& outMode );

    private:
        EditorGameViewAspect _aspect;                 ///< 이미지 화면 비율(자유 · 16:9)
        uint8                _bNoCameraHintShown : 1; ///< 마지막 판정에 카메라가 없었다
        uint8                _forwardedButtonMask;    ///< 보조 창에서 게임으로 누름을 넣은 단추(비트 = 넘기는 단추 표 순서). 패널 밖에서 떼도 뗌을 넣는다
        DetachedKeyForwarder _keyForwarder;           ///< 보조 창의 키를 게임으로 넣고, 누름을 넣은 채 남은 키를 기억한다
    };
} // namespace sw::editor
