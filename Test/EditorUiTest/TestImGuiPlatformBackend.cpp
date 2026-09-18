#include "pch.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#include "Engine/Graphics/RHI/RHITypes.h"

#include "TestFramework/TestFramework.h"

#include <imgui.h>

using namespace sw;
using namespace sw::editor;

// ImGui 플랫폼 백엔드 — **초기화가 실패한 뒤의 뒤처리**가 살아남는가.
//
// 이 스위트가 `EditorTest` 가 아니라 여기 있는 이유는 ImGui 컨텍스트가 필요해서다.
// 창이나 GPU 는 필요 없다 — `ImGui::CreateContext()` 만 있으면 된다.

namespace
{
    /** @brief 테스트 동안 ImGui 컨텍스트를 하나 세웠다 내립니다. */
    class ScopedImGuiContext
    {
    public:
        ScopedImGuiContext()
            : _pContext{ ImGui::CreateContext() }
        {
            ImGuiIO& io = ImGui::GetIO();
            // 창이 없으므로 크기를 직접 준다 — 없으면 ImGui 가 단정에 걸린다.
            io.DisplaySize = ImVec2( 640.0f, 480.0f );
        }

        ~ScopedImGuiContext()
        {
            if ( _pContext != nullptr )
                ImGui::DestroyContext( _pContext );
        }

        ScopedImGuiContext( const ScopedImGuiContext& )            = delete;
        ScopedImGuiContext& operator=( const ScopedImGuiContext& ) = delete;

        bool isValid() const { return _pContext != nullptr; }

    private:
        ImGuiContext* _pContext{ nullptr };
    };
} // namespace

/**
 * @brief [EditorUiPlatformBackendTest] 초기화 없이 shutdown 해도 살아남는다
 * @details `ImGuiEditor::initialize()` 는 단계가 실패할 때마다
 *          `shutdownPartialInitialization()` 을 부르고, 그 함수는 `_platformBackend->shutdown()`
 *          을 부른다. 그런데 `ImGuiWin32PlatformBackend::shutdown()` 은
 *          `ImGui_ImplWin32_Shutdown()` 을 **무조건** 불렀다 — 짝이 되는 Init 이 없으면
 *          그 함수 첫 줄의 단정("No platform backend to shutdown, or already shutdown?")에 걸린다.
 *          **즉 실패를 수습하라고 있는 경로가 곧바로 죽었다.**
 *          렌더러 백엔드 넷은 모두 `BackendRendererUserData` 로 같은 것을 막고 있었고,
 *          플랫폼 쪽만 빠져 있었다(2026-09-18 수정, `BackendPlatformUserData` 로 맞춤).
 */
SW_TEST_CASE( EditorUiPlatformBackendTest, ShutdownWithoutInitializeIsSafe )
{
    ScopedImGuiContext context;
    SW_ASSERT_TRUE( context.isValid() );

    unique_ptr<IImGuiPlatformBackend> backend = IImGuiPlatformBackend::createPlatformBackend();
    SW_ASSERT_TRUE( backend != nullptr );

    // 초기화한 적이 없으므로 ImGui 쪽 백엔드 데이터는 비어 있다 — 이 전제가 이 테스트의 핵심이다.
    SW_ASSERT_EQUAL( nullptr, ImGui::GetIO().BackendPlatformUserData );

    backend->shutdown();

    // 여기까지 왔다는 것이 결과다. 그리고 두 번 불러도 같아야 한다(뒤처리는 여러 번 올 수 있다).
    backend->shutdown();
    SW_EXPECT_EQUAL( nullptr, ImGui::GetIO().BackendPlatformUserData );
}

/**
 * @brief [EditorUiPlatformBackendTest] 창 없이 initialize 하면 실패로 답한다
 * @details 실패 경로가 실제로 돌아야 위 케이스가 의미를 갖는다 — `initialize` 가 널 창에
 *          대해 `false` 를 돌려주고, 그 뒤 `shutdown()` 이 안전해야 한다. 이 둘이 함께
 *          `ImGuiEditor::initialize()` 의 실패 경로를 흉내 낸다.
 */
SW_TEST_CASE( EditorUiPlatformBackendTest, InitializeWithoutWindowFailsThenShutdownIsSafe )
{
    ScopedImGuiContext context;
    SW_ASSERT_TRUE( context.isValid() );

    unique_ptr<IImGuiPlatformBackend> backend = IImGuiPlatformBackend::createPlatformBackend();
    SW_ASSERT_TRUE( backend != nullptr );

    SW_EXPECT_FALSE( backend->initialize( nullptr, RHIBackend::DirectX12 ) );
    SW_EXPECT_EQUAL( nullptr, ImGui::GetIO().BackendPlatformUserData );

    backend->shutdown();
}
