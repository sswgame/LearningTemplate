/**
 * @file EditorSelfTestInput.h
 * @brief 에디터 자체 테스트가 마우스, 키, 글자 입력을 흉내 내는 함수와, 테스트가 누를 위젯의 레이블을 제공합니다.
 * @details 테스트 단계가 이벤트를 큐에 넣으면 다음 프레임의 플랫폼 newFrame 뒤, ImGui::NewFrame 앞에 ImGuiIO 로 넣는다(`flushIntoImGui`).
 *          플랫폼 백엔드가 넣은 실제 커서 위치보다 뒤에 넣으므로 이 입력이 우선한다. 위젯 위치는 패널이 그린 직후 `EditorSelfTestMarks::note` 로 남긴 레이블에서 읽는다.
 *          테스트가 꺼져 있으면 레이블을 기록하지 않는다.
 *          플랫폼 백엔드는 프레임마다 실제 커서를 다시 넣을 수 있으므로, 누르기, 떼기, 호버는 그 단계마다 레이블 위로 다시 옮긴 뒤 넣는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 레이블 하나입니다. 위젯이 그려진 프레임의 ID 와 화면 사각형(ImGui 좌표)을 담습니다. */
    struct EditorSelfTestMark
    {
        float2 _min{};
        float2 _max{};
        uint32 _itemID{ 0 };
        uint32 _windowID{ 0 };         ///< 위젯을 그린 ImGui 창. 누를 점의 최상위 창이 이 창(또는 그 조상 · 이 위젯이 담은 자식 창)이 아니면 가려진 것이다(`EditorSelfTestInput::hitTestMark`)
        uint32 _viewportID{ 0 };       ///< 위젯이 그려진 뷰포트. 떠 있는 창(자기 플랫폼 창)이면 마우스를 옮길 때 그 뷰포트를 "커서 아래" 로 알린다
        uint32 _frame{ 0 };            ///< 기록한 ImGui 프레임 번호. 지난 프레임에 그려지지 않은 위젯을 누르지 않게 한다
        uint32 _stableStartFrame{ 0 }; ///< 사각형 · 뷰포트 · 잘림이 지금 값으로 이어서 그려진 첫 프레임(그 사이 빠진 프레임이 있으면 다시 센다)
        bool   _bClipped{ false };     ///< 사각형 가운데(클릭이 닿는 점)가 그린 창의 보이는 영역 밖이다(스크롤 아래 · 가장자리에 걸침). 입력 단계가 그 창을 스크롤해 보이게 한 뒤 누른다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        /**
         * @brief 두 프레임 연속 같은 자리에 그려졌으면 true 입니다 — 입력 단계가 이때까지 시계를 붙잡는다(창이 열리며 자리를 잡는 중이면 기다린다).
         * @details 예전에는 그려지기만 하면 눌러, 시나리오마다 앞에 고정 대기(10 프레임 이상)를 두어 도킹 · 팝업이 자리 잡기를 기다렸다. Playwright 의 "stable" 확인과 같다.
         */
        [[nodiscard]] bool isSettled() const { return _frame > _stableStartFrame; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorSelfTestMarks
     * @brief 패널이 위젯을 그린 직후 남기는 레이블입니다. 테스트가 켜졌을 때만 기록합니다.
     */
    struct SW_EDITOR_API EditorSelfTestMarks
    {
        /** @brief 바로 앞에 그린 위젯(ImGui "last item")을 @p pKey 로 기록합니다. 테스트가 꺼져 있으면 아무것도 하지 않습니다. */
        static void record( const utf8* pKey );
        /**
         * @brief `note` 와 같되, 바로 앞 위젯이 입력 칸 + 오른쪽 레이블 글이면 사각형을 입력 칸(`ImGui::CalcItemWidth()`)까지만 남깁니다.
         * @details 레이블이 붙은 위젯의 "last item" 은 레이블 글까지 덮어, 좁은 창에서 그 가운데가 레이블 글(눌러도 열리지 않는 곳) 위에 놓인다.
         */
        static void recordInputFrame( const utf8* pKey );
        /** @brief 화면 픽셀 사각형(@p min 에서 @p max)을 @p pKey 로 기록합니다. 좌표를 변환해 그리는 캔버스(노드 그래프) 안의 위젯이 화면 좌표로 바꿔서 넘깁니다. */
        static void recordRect( const utf8* pKey, const float2& min, const float2& max );
        /** @brief 기록된 레이블을 찾습니다. 이번 프레임이나 지난 프레임에 기록된 것만 반환합니다. 없으면 false 입니다. */
        [[nodiscard]] static bool find( string_view key, EditorSelfTestMark& outMark );
        /**
         * @brief 레이블 @p key 가 다음에 기록될 때 그린 창의 보이는 영역 밖이면 그 창을 스크롤해 보이게 합니다(한 번).
         * @details 입력 단계가 누르기 전에 부른다 — 언리얼 Automation Driver · Playwright 처럼 대상을 보이게 한 뒤 누른다. 배치가 바뀌어 버튼이 스크롤 아래로 밀려도 시나리오는 그대로다.
         */
        static void requestScrollIntoView( string_view key );
        /**
         * @brief 실행기가 테스트를 켜고 끌 때 호출합니다. 끄면 레이블과 남은 입력을 비웁니다.
         * @return 에디터 컨텍스트가 없어 기록할 곳이 없으면 false 입니다(켠 것으로 치지 않는다 — 부른 쪽이 오류로 알린다).
         */
        [[nodiscard]] static bool setEnabled( bool bEnabled );
        /** @brief 레이블을 기록하는 중이면 true 입니다. 레이블 문자열을 만드는 데 비용이 드는 곳(항목마다 다른 레이블)이 먼저 확인합니다. */
        static bool isEnabled();
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 레이블의 누를 점에 포인터를 두었을 때 그 점의 입력을 받는 쪽입니다(`EditorSelfTestInput::hitTestMark`). */
    enum class EditorMarkHit : uint8
    {
        Pending,   ///< 아직 판정할 표본이 없다(레이블이 없거나, 포인터가 그 점에 닿은 프레임의 표본이 아직 없다)
        Reachable, ///< 그 점의 최상위 창이 레이블 주인이다 — 누르면 레이블이 받는다
        Covered,   ///< 다른 창(오버레이 툴바 · 팝업 · 모달)이 그 점을 덮어 누르면 그쪽이 받는다
    };

    /**
     * @struct EditorSelfTestInput
     * @brief 다음 프레임에 넣을 입력 이벤트 큐입니다. 테스트 단계 하나가 이벤트 몇 개를 넣고 `Continue` 를 반환하면 다음 프레임에 들어갑니다.
     * @details ImGui 는 같은 프레임의 누름과 뗌을 프레임마다 하나씩 흘려 처리한다(trickle). 그래서 클릭은 누르기와 떼기 두 단계로 나눠 넣는다.
     */
    struct EditorSelfTestInput
    {
        /** @brief 마우스를 옮깁니다. @p viewportID 가 0 이 아니면 그 뷰포트를 커서 아래 뷰포트로 알립니다. 플랫폼이 실제 커서로 넣는 값보다 우선합니다. */
        static void moveMouse( const float2& position, uint32 viewportID = 0 );
        static void setMouseButton( int32 button, bool bDown );
        static void typeText( string_view utf8Text );
        /** @brief 세로 휠을 @p notches 눈금 돌립니다(양수면 위로). 커서 아래 창이 받는다. */
        static void scrollMouseWheel( float32 notches );
        /**
         * @brief 이 뒤에 넣는 이벤트는 다음 ImGui 프레임에 들어갑니다.
         * @details 플랫폼 백엔드가 프레임마다 실제 커서를 넣으므로 누름과 뗌을 한 프레임에 몰면 뗄 때 커서가 위젯 밖에 있다. 그래서 뗌 앞에서 끊고 커서를 다시 옮긴다.
         */
        static void waitNextFrame();
        /** @brief 키 하나를 누르거나 뗍니다. ImGuiKey 값을 쓰며, 수정자는 왼쪽 키(ImGuiKey_LeftCtrl 등)입니다 — ImGui 가 그 키로 수정자를 만들고 에디터 입력 맵으로 가는 길도 같은 키를 본다. */
        static void setKey( int32 imguiKey, bool bDown );
        /**
         * @brief 키 이름을 ImGuiKey 값으로 찾습니다. `ImGui::GetKeyName` 의 문자열이며 "Enter", "Escape", "A", "F5" 가 있고, 수정자는 "Ctrl", "Shift", "Alt" 입니다.
         * @return 없는 이름이면 false 입니다.
         */
        [[nodiscard]] static bool findKeyByName( string_view name, int32& outImguiKey );
        /** @brief 레이블의 @p anchor 위치(0..1, 가운데가 (0.5, 0.5))로 마우스를 옮깁니다. 레이블이 없으면 false 입니다. */
        [[nodiscard]] static bool moveMouseToMark( string_view key, const float2& anchor = float2{ 0.5f, 0.5f } );
        /**
         * @brief 레이블의 가운데에 커서를 붙잡아 둡니다. `releaseMouseHold` 가 호출될 때까지 프레임마다 그 위치를 다시 넣습니다. 버튼을 누른 채 여러 프레임을 두는
         *        단계(`EditorClick state="down"`, 뷰포트 비행)에서 플랫폼이 넣는 실제 커서가 호버를 풀지 않게 하기 위해서입니다. 레이블이 없으면 false 입니다.
         */
        [[nodiscard]] static bool holdMouseAtMark( string_view key, const float2& anchor = float2{ 0.5f, 0.5f } );
        /** @brief ImGui 가 본 포인터가 지금 레이블의 @p anchor 자리에 있으면 true 입니다. 레이블이 없으면 false 입니다. */
        [[nodiscard]] static bool isMouseAtMark( string_view key, const float2& anchor = float2{ 0.5f, 0.5f } );
        /**
         * @brief 포인터가 레이블의 @p anchor 자리에 있던 프레임에 그 점의 최상위 창(ImGui `HoveredWindow`)이 레이블 주인인지 봅니다. 가려졌으면 @p outCoverName 에 덮은 창 이름을 넣습니다.
         * @details 잘림 검사(`_bClipped`)는 창 밖으로 잘린 것만 잡는다 — 같은 창 위의 오버레이(씬 뷰 하단 툴바 자식 창) · 팝업에 가려지면 누름이 그쪽으로 가 단계는 성공하고 뒤 기대만 엉뚱하게 졌다.
         *          주인은 레이블을 그린 창, 그 조상(제목 표시줄 · 도크 탭 · 열린 메뉴 머리), 레이블 사각형 안에 든 자식 창(목록 상자처럼 레이블이 담은 내용)이다.
         *          언리얼 Automation Driver 의 hit-test 확인 · Playwright actionability 의 "receives events"(elementFromPoint) 와 같은 자리다.
         */
        [[nodiscard]] static EditorMarkHit hitTestMark( string_view key, const float2& anchor, string& outCoverName );
        static void                        releaseMouseHold();
        /** @brief 앞 단계가 넣은 이벤트가 아직 ImGui 에 다 들어가지 않았으면 true 입니다. 입력 단계는 앞 입력이 다 들어간 뒤에 자리를 본다. */
        [[nodiscard]] static bool hasPendingEvents();
        /**
         * @brief 키 · 글 입력을 받을 곳(초점 창과 그 사각형, 활성 · 내비 위젯, 열린 팝업 수)이 두 프레임 연속 같으면 true 입니다.
         * @details 키 · 글 단계에는 누를 레이블이 없다 — 그 대상은 키보드 초점이다. 클릭이 연 팝업 · 입력 칸이 초점을 잡는 데 한두 프레임이 걸린다.
         */
        [[nodiscard]] static bool isKeyboardTargetSettled();
        /**
         * @brief 화면이 자리 잡기를 기다리기 시작합니다. 돌려준 번호를 `isLayoutSettledAfter` 에 넘깁니다.
         * @details 번호는 에디터가 다음에 여는 프레임에 찍힌다(`flushIntoImGui`). 앞 단계(개발 명령 · 입력 · 변수)는 이 호출 전에 다 돌았고, 그 요청은 다음 프레임이 열릴 때
         *          받아들여지므로, 이 번호 이상이 찍힌 프레임은 앞 단계를 모두 반영해 그린 화면이다 — 효과가 몇 프레임 뒤에 나오는지 세지 않는다.
         */
        [[nodiscard]] static uint32 beginSettleWait();
        /** @brief 마지막 `beginSettleWait` 의 번호입니다. 상태가 새로 만들어졌으면(에디터 컨텍스트를 다시 세움) 0 부터 다시 센다. */
        [[nodiscard]] static uint32 getSettleWaitSerial();
        /**
         * @brief `beginSettleWait` 가 준 @p waitSerial 이상이 찍힌 프레임 둘이 연속으로 같은 배치면 true 입니다 — 보이는 창 모두의 자리(위치 · 크기 · 도크 노드 · 뷰포트 · 접힘),
         *        메인 뷰포트 크기, 열린 팝업 수.
         * @details 창 크기 바꾸기 · 레이아웃 재설정 · 패널 열기 뒤 도킹이 자리를 잡는 데 몇 프레임이 걸린다 — 시나리오가 그것을 고정 대기로 때우지 않게 한다(`EditorWaitSettled`).
         *          위젯 하나의 자리(`EditorSelfTestMark::isSettled`)를 창 전체로 넓힌 것이다. 기록이 꺼져 있으면(표본이 없다) false 다.
         */
        [[nodiscard]] static bool isLayoutSettledAfter( uint32 waitSerial );
        /** @brief 창 배치 서명이 마지막으로 바뀐 까닭입니다(움직인 창 이름 등) — 자리 잡기 시한 초과를 알릴 때 붙인다. 표본이 없으면 빈 글입니다. */
        [[nodiscard]] static string describeLastLayoutChange();
        /** @brief 큐를 ImGuiIO 에 넣고 비웁니다. `ImGuiEditor::beginFrame` 이 플랫폼 newFrame 뒤에 호출합니다. 넣기 전에 키보드 대상을 표본으로 남긴다. */
        static void flushIntoImGui();
    };
} // namespace sw::editor
