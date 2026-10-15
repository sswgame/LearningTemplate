/**
 * @file EditorSelfTestInput.h
 * @brief 에디터 자체 테스트가 마우스, 키, 글자 입력을 흉내 내는 함수와, 테스트가 누를 위젯의 레이블을 제공합니다.
 * @details 테스트 단계가 이벤트를 큐에 넣으면 다음 프레임의 플랫폼 newFrame 뒤, ImGui::NewFrame 앞에 ImGuiIO 로 넣는다(`flushIntoImGui`).
 *          플랫폼 백엔드가 넣은 실제 커서 위치보다 뒤에 넣으므로 이 입력이 우선한다. 위젯 위치는 패널이 그린 직후 `EditorSelfTestMarks::note` 로 남긴 레이블에서 읽는다.
 *          테스트가 꺼져 있으면 레이블을 기록하지 않는다.
 *          플랫폼 백엔드는 프레임마다 실제 커서를 다시 넣을 수 있으므로, 누르기, 떼기, 호버는 그 단계마다 레이블 위로 다시 옮긴 뒤 넣는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
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
        uint32 _viewportID{ 0 };   ///< 위젯이 그려진 뷰포트. 떠 있는 창(자기 플랫폼 창)이면 마우스를 옮길 때 그 뷰포트를 "커서 아래" 로 알린다
        uint32 _frame{ 0 };        ///< 기록한 ImGui 프레임 번호. 지난 프레임에 그려지지 않은 위젯을 누르지 않게 한다
        bool   _bClipped{ false }; ///< 사각형이 그린 창의 보이는 영역 밖이다(스크롤 아래). 누르면 다른 위젯이 받으므로 클릭 · 드래그 단계가 실패로 알린다
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
        static void note( const utf8* pKey );
        /** @brief 화면 픽셀 사각형(@p min 에서 @p max)을 @p pKey 로 기록합니다. 좌표를 변환해 그리는 캔버스(노드 그래프) 안의 위젯이 화면 좌표로 바꿔서 넘깁니다. */
        static void noteRect( const utf8* pKey, const float2& min, const float2& max );
        /** @brief 기록된 레이블을 찾습니다. 이번 프레임이나 지난 프레임에 기록된 것만 반환합니다. 없으면 false 입니다. */
        static bool find( string_view key, EditorSelfTestMark& outMark );
        /** @brief 실행기가 테스트를 켜고 끌 때 호출합니다. 끄면 레이블과 남은 입력을 비웁니다. */
        static void setEnabled( bool bEnabled );
        /** @brief 레이블을 기록하는 중이면 true 입니다. 레이블 문자열을 만드는 데 비용이 드는 곳(항목마다 다른 레이블)이 먼저 확인합니다. */
        static bool isEnabled();
    };
} // namespace sw::editor

namespace sw::editor
{
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
        /**
         * @brief 이 뒤에 넣는 이벤트는 다음 ImGui 프레임에 들어갑니다.
         * @details 플랫폼 백엔드가 프레임마다 실제 커서를 넣으므로 누름과 뗌을 한 프레임에 몰면 뗄 때 커서가 위젯 밖에 있다. 그래서 뗌 앞에서 끊고 커서를 다시 옮긴다.
         */
        static void waitNextFrame();
        /** @brief 키 하나를 누르거나 뗍니다. ImGuiKey 값을 쓰며, 수정자는 ImGuiMod_* 입니다. */
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
        static void               releaseMouseHold();
        /** @brief 큐를 ImGuiIO 에 넣고 비웁니다. `ImGuiEditor::beginFrame` 이 플랫폼 newFrame 뒤에 호출합니다. */
        static void flushIntoImGui();
    };
} // namespace sw::editor
