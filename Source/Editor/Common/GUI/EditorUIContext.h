/**
 * @file EditorUIContext.h
 * @brief 에디터 ImGui · ImPlot 컨텍스트를 확장 DLL 의 ImGui 사본에 거는 결속기입니다(ImGui 헤더를 include 하지 않습니다 — 컨텍스트는 `void*`).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
    /** @brief 지금 에디터 UI 컨텍스트입니다. 확장 DLL 이 자기 ImGui 사본에 겁니다. */
    struct EditorUIContextState
    {
        void* _pImGuiContext{ nullptr };  ///< ImGuiContext*
        void* _pImPlotContext{ nullptr }; ///< ImPlotContext*
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorUIBinderRegistration
     * @brief DLL 하나의 결속 함수입니다. 확장 모듈마다 CMake 가 만든 소스(`<모듈>UIBinder.cpp`)가 하나 둡니다.
     * @details vcpkg imgui 는 정적 라이브러리라 DLL 마다 `GImGui` · 할당자 사본이 생깁니다. 결속 함수는 **그 DLL 의** ImGui 사본에
     *          `SetAllocatorFunctions` · `SetCurrentContext` 를 겁니다(Dear ImGui FAQ "DLL 경계"). 할당자는 엔진 Memory 라 어느 DLL 이 잡고 풀어도 같습니다.
     */
    struct EditorUIBinderRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "uibinder";

        void ( *_pfnBind )( const EditorUIContextState& state );

        /**
         * @brief 목록에 붙는 순간 UI 컨텍스트가 이미 있으면 바로 겁니다(에디터가 뜬 뒤 붙은 확장 모듈 · 핫 리로드).
         * @details 붙기 전에 만든 컨텍스트는 `EditorUIContext::publish` 가 목록을 돌며 걸지만, 그 뒤에 붙은 모듈은 목록에 없어 빠진다 — 예전에는 등록자 생성자(정적 초기화)에서만 걸어,
         *          "DLL 이 실린 뒤 · 컨텍스트가 생긴 뒤 · 모듈이 붙기 전" 순서면 그 DLL 의 ImGui 사본이 끝내 비어 첫 그리기에서 죽었다(Mesh Modifiers 패널).
         */
        static void onAttached( const EditorExtensionRegistration& registration );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorUIContext
     * @brief 에디터가 ImGui 컨텍스트를 만들거나 지울 때 알리는 곳입니다. 알릴 때마다 등록된 결속 함수를 모두 부릅니다.
     */
    struct SW_EDITOR_API EditorUIContext
    {
        /** @brief `ImGuiEditor::initialize` 가 컨텍스트를 만든 직후(상태), 지우기 직전(빈 상태)에 부릅니다. */
        static void publish( const EditorUIContextState& state );
        /** @brief 지금 상태입니다. 컨텍스트가 없으면 두 값 모두 nullptr 입니다. 값은 이 모듈의 ImGui · ImPlot 이 가진 현재 컨텍스트이고, 따로 보관하지 않는다. */
        static EditorUIContextState getCurrent();
    };
} // namespace sw::editor

namespace sw::editor
{
    // 확장 DLL 의 등록자가 부르므로 헤더에 둔다(이 형식은 내보내지 않는다). 컨텍스트는 에디터 모듈이 내보낸 `getCurrent` 가 준다.
    inline void EditorUIBinderRegistration::onAttached( const EditorExtensionRegistration& registration )
    {
        const auto&                binder = static_cast<const EditorUIBinderRegistration&>( registration );
        const EditorUIContextState state  = EditorUIContext::getCurrent();
        if ( state._pImGuiContext != nullptr && binder._pfnBind != nullptr )
            binder._pfnBind( state );
    }
} // namespace sw::editor
