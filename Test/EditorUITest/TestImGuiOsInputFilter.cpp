#include "pch.h"

#include "Editor/Common/Backend/ImGuiOsInputFilter.h"

#include "TestFramework/TestFramework.h"

#include <imgui.h>
#include <imgui_internal.h>

using namespace sw;
using namespace sw::editor;

// 무인 자동화의 ImGui 입력 거르개 — 플랫폼 백엔드가 넣는 실제 커서 · 키는 지우고 자동화 입력은 남긴다.

namespace
{
    struct ImGuiOsInputFilterTestInternal
    {
        /** @brief 플랫폼이 알리는 커서 아래 뷰포트 — 0 은 지금 값과 같아 ImGui 가 중복으로 버린다. */
        static constexpr ImGuiID kPlatformViewportID = 0x1234u;

        /** @brief 테스트 동안 ImGui 컨텍스트를 하나 세웠다 내립니다. */
        class ScopedContext
        {
        public:
            ScopedContext()
                : _pContext{ ImGui::CreateContext() }
            {
                ImGui::GetIO().DisplaySize = ImVec2( 640.0f, 480.0f );
            }

            ~ScopedContext()
            {
                if ( _pContext != nullptr )
                    ImGui::DestroyContext( _pContext );
            }

            ScopedContext( const ScopedContext& )            = delete;
            ScopedContext& operator=( const ScopedContext& ) = delete;

        private:
            ImGuiContext* _pContext{ nullptr };
        };

        /** @brief 플랫폼 백엔드가 한 프레임에 넣는 것처럼 실제 장치 사건을 넣습니다(커서 위치 · 커서 아래 뷰포트 · 단추 · 휠 · 키 · 글자 · 포커스). */
        static void addPlatformEvents()
        {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent( 385.0f, 500.0f );
            io.AddMouseViewportEvent( kPlatformViewportID );
            io.AddMouseButtonEvent( ImGuiMouseButton_Left, true );
            io.AddMouseWheelEvent( 0.0f, 1.0f );
            io.AddKeyEvent( ImGuiKey_W, true );
            io.AddInputCharacter( 'w' );
            io.AddFocusEvent( false );
        }

        static int32 countEvents( ImGuiInputEventType type )
        {
            int32 count = 0;
            for ( const ImGuiInputEvent& event : ImGui::GetCurrentContext()->InputEventsQueue )
            {
                if ( event.Type == type )
                    ++count;
            }
            return count;
        }

        static const ImGuiInputEvent* findLastMousePos()
        {
            const ImVector<ImGuiInputEvent>& queue = ImGui::GetCurrentContext()->InputEventsQueue;
            for ( int32 index = queue.Size - 1; index >= 0; --index )
            {
                if ( queue[index].Type == ImGuiInputEventType_MousePos )
                    return &queue[index];
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [EditorUIOsInputFilterTest] 막는 동안 플랫폼 사건은 포커스만 남고, 범위 밖에서 넣은 자동화 커서는 그대로 남는다
 * @details 시나리오의 뷰포트 비행에서 단추를 떼는 프레임에 실제 커서 자리가 마지막 위치가 되어 시점이 돌던 결함 — 그 프레임의 큐 모양을 그대로 만든다.
 */
SW_TEST_CASE( EditorUIOsInputFilterTest, BlockedDropsPlatformDeviceEventsKeepsAutomation )
{
    using Internal = ImGuiOsInputFilterTestInternal;
    Internal::ScopedContext context;
    // 앞 프레임에서 남은 자동화 사건(범위 앞)은 건드리지 않는다.
    ImGui::GetIO().AddMousePosEvent( 10.0f, 20.0f );
    {
        const ScopedImGuiOsInputFilter filter{ true };
        Internal::addPlatformEvents();
    }
    ImGui::GetIO().AddKeyEvent( ImGuiKey_D, true ); // 자동화가 뒤에 넣는 키

    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_MousePos ) );
    SW_EXPECT_EQUAL( 0, Internal::countEvents( ImGuiInputEventType_MouseViewport ) );
    SW_EXPECT_EQUAL( 0, Internal::countEvents( ImGuiInputEventType_MouseButton ) );
    SW_EXPECT_EQUAL( 0, Internal::countEvents( ImGuiInputEventType_MouseWheel ) );
    SW_EXPECT_EQUAL( 0, Internal::countEvents( ImGuiInputEventType_Text ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_Key ) ); // 자동화의 D 만
    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_Focus ) );
    const ImGuiInputEvent* pLastPos = Internal::findLastMousePos();
    SW_ASSERT_TRUE( pLastPos != nullptr );
    SW_EXPECT_EQUAL( 10.0f, pLastPos->MousePos.PosX );
    SW_EXPECT_EQUAL( 20.0f, pLastPos->MousePos.PosY );
}

/**
 * @brief [EditorUIOsInputFilterTest] 막지 않으면(일반 실행 · 혼합 시나리오) 플랫폼 사건과 자동화 사건이 모두 남는다
 */
SW_TEST_CASE( EditorUIOsInputFilterTest, NotBlockedKeepsEverything )
{
    using Internal = ImGuiOsInputFilterTestInternal;
    Internal::ScopedContext context;
    {
        const ScopedImGuiOsInputFilter filter{ false };
        Internal::addPlatformEvents();
    }
    ImGui::GetIO().AddMousePosEvent( 10.0f, 20.0f );

    SW_EXPECT_EQUAL( 2, Internal::countEvents( ImGuiInputEventType_MousePos ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_MouseViewport ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_MouseButton ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_MouseWheel ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_Key ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_Text ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( ImGuiInputEventType_Focus ) );
}

/**
 * @brief [EditorUIOsInputFilterTest] ImGui 컨텍스트가 없을 때 만들고 지워도 아무 일도 없다(에디터가 뜨기 전 창 메시지)
 */
SW_TEST_CASE( EditorUIOsInputFilterTest, NoContextIsHarmless )
{
    ImGuiContext* pPrevious = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext( nullptr );
    {
        const ScopedImGuiOsInputFilter filter{ true };
    }
    SW_EXPECT_TRUE( ImGui::GetCurrentContext() == nullptr );
    ImGui::SetCurrentContext( pPrevious );
}
