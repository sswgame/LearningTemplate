/**
 * @file EditorSelfTestInput.h
 * @brief 에디터 자체 시험이 마우스 · 키 · 글자 입력을 흉내 내는 창구와, 시험이 누를 위젯의 이름표입니다.
 * @details 시험 단계가 이벤트를 큐에 넣으면 다음 프레임의 플랫폼 newFrame 뒤 · ImGui::NewFrame 앞에 ImGuiIO 로 넣습니다(`flushIntoImGui`) — 플랫폼 백엔드가
 *          넣은 실제 커서 위치보다 뒤라 이긴다. 위젯 위치는 패널이 그린 직후 `EditorSelfTestMarks::note` 로 남긴 이름표에서 읽습니다(시험이 꺼져 있으면 아무것도 적지 않는다).
 *          주의: 플랫폼 백엔드는 프레임마다 실제 커서를 다시 넣을 수 있다 — 누르기 · 떼기 · 호버는 그 단계마다 이름표 위로 다시 옮긴 뒤 넣는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 이름표 하나 — 위젯이 그려진 프레임의 ID 와 화면 사각형(ImGui 좌표)입니다. */
    struct EditorSelfTestMark
    {
        float2 _min{};
        float2 _max{};
        uint32 _itemId{ 0 };
        uint32 _viewportId{ 0 }; ///< 위젯이 그려진 뷰포트 — 떠 있는 창(자기 플랫폼 창)이면 마우스를 옮길 때 그 뷰포트를 "커서 아래" 로 알린다
        uint32 _frame{ 0 };      ///< 적힌 ImGui 프레임 번호 — 지난 프레임에 안 그려진 위젯을 누르지 않게
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorSelfTestMarks
     * @brief 패널이 위젯을 그린 직후 남기는 이름표입니다. 시험이 켜졌을 때만 적습니다.
     */
    struct EditorSelfTestMarks
    {
        /** @brief 바로 앞에 그린 위젯(ImGui "last item")을 @p pKey 로 적습니다. 시험이 꺼져 있으면 아무것도 하지 않습니다. */
        static void note( const utf8* pKey );
        /** @brief 적힌 이름표를 찾습니다. 이번 프레임이나 지난 프레임에 적힌 것만 돌려줍니다. 없으면 false. */
        static bool find( string_view key, EditorSelfTestMark& outMark );
        /** @brief 실행기가 시험을 켜고 끌 때 부릅니다. 끄면 이름표와 남은 입력을 비웁니다. */
        static void setEnabled( bool bEnabled );
        /** @brief 이름표를 적는 중이면 true 입니다 — 이름표 글을 만드는 데 비용이 드는 자리(항목마다 다른 이름표)가 먼저 묻는다. */
        static bool isEnabled();
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorSelfTestInput
     * @brief 다음 프레임에 넣을 입력 이벤트 큐입니다. 시험 단계 하나가 이벤트 몇 개를 넣고 `Continue` 를 돌려주면 다음 프레임에 들어간다.
     * @details ImGui 는 같은 프레임의 누름 · 뗌을 프레임마다 하나씩 흘려 처리한다(trickle) — 클릭은 단계 둘(누르기 · 떼기)로 나눠 넣는다.
     */
    struct EditorSelfTestInput
    {
        /** @brief 마우스를 옮깁니다. @p viewportId 가 0 이 아니면 그 뷰포트를 커서 아래 뷰포트로 알립니다(플랫폼이 실제 커서로 넣는 값을 이긴다). */
        static void moveMouse( const float2& position, uint32 viewportId = 0 );
        static void setMouseButton( int32 button, bool bDown );
        static void typeText( string_view utf8Text );
        /**
         * @brief 이 뒤에 넣는 사건은 다음 ImGui 프레임에 들어갑니다.
         * @details 플랫폼 백엔드가 프레임마다 실제 커서를 넣으므로 누름 · 뗌을 한 프레임에 몰면 뗄 때 커서가 위젯 밖이다 — 뗌 앞에서 끊고 커서를 다시 옮긴다.
         */
        static void waitNextFrame();
        /** @brief 키 하나(ImGuiKey 값 — 수정자는 ImGuiMod_*)를 누르거나 뗍니다. */
        static void setKey( int32 imguiKey, bool bDown );
        /**
         * @brief 키 이름(`ImGui::GetKeyName` 의 글 — "Enter" · "Escape" · "A" · "F5", 수정자는 "Ctrl" · "Shift" · "Alt")을 ImGuiKey 값으로 찾습니다.
         * @return 없는 이름이면 false 입니다.
         */
        [[nodiscard]] static bool findKeyByName( string_view name, int32& outImguiKey );
        /** @brief 이름표의 가운데로 마우스를 옮깁니다. 이름표가 없으면 false. */
        [[nodiscard]] static bool moveMouseToMark( string_view key );
        /** @brief 큐를 ImGuiIO 에 넣고 비웁니다. `ImGuiEditor::beginFrame` 이 플랫폼 newFrame 뒤에 부릅니다. */
        static void flushIntoImGui();
    };
} // namespace sw::editor
