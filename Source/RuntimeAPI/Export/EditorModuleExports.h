/**
 * @file EditorModuleExports.h
 * @brief 에디터 모듈의 진입점(C-ABI) 구현을 위한 매크로 모음
 *
 * @note 이 헤더는 **경계 계약이 아니라 모듈 쪽 접착제**다. 그래서 `ABI/` 와 달리 모듈 자신의
 *       서비스 로케이터(`Editor/`)를 끌어온다 — 모듈 구현 `.cpp` 만 include 한다.
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "RuntimeAPI/ABI/EditorAPI.h"
#include "RuntimeAPI/Export/ModuleForwardUtil.h"

namespace sw
{
    // 아래 매크로가 불투명 핸들을 되돌릴 때만 필요하다. ABI/ 쪽 계약 헤더는 이 타입들을 모른다.
    class IRHIDevice;
    class IWindow;
} // namespace sw

/**
 * @brief 에디터 모듈의 C-ABI 함수 테이블을 1줄로 구현 및 export하는 매크로
 * @param EditorClass sw::IEditor를 구현하는 에디터 클래스 (보통 sw::editor::ImGuiEditor)
 * @details 핸들 캐스팅과 널 검사는 `ModuleForwardUtil` 이 한다. 테이블에 항목을 하나 더
 *          붙일 때 건드릴 곳은 `EditorAPI` 구조체 한 줄과 여기 한 줄이어야 한다.
 */
// 이 매크로 인자는 **타입 이름**이다. 괄호로 감싸면 `sw_new (EditorClass)()` 처럼 되어
// 문법이 깨진다 — 검사기는 인자를 식으로 가정한다. 매크로 본문은 줄 연결이라 중간에
// 주석을 넣을 수 없으므로 정의 전체를 범위로 덮는다.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define SW_IMPLEMENT_EDITOR_MODULE( EditorClass )                                                                                                                                                                  \
    extern "C" SW_MODULE_API bool exportEditorAPI( sw::EditorAPI* pOutApi )                                                                                                                                        \
    {                                                                                                                                                                                                              \
        if ( pOutApi == nullptr )                                                                                                                                                                                  \
            return false;                                                                                                                                                                                          \
        pOutApi->create     = []() -> sw::EditorHandle { return sw_new EditorClass(); };                                                                                                                           \
        pOutApi->destroy    = []( sw::EditorHandle editorHandle ) { sw_delete( static_cast<EditorClass*>( editorHandle ) ); };                                                                                     \
        pOutApi->initialize = []( sw::EditorHandle editorHandle, sw::WindowHandle windowHandle, sw::RHIDeviceHandle rhiDeviceHandle ) -> bool                                                                      \
        { return sw::ModuleForwardUtil::callOr<EditorClass, bool>( editorHandle, false, &EditorClass::initialize, static_cast<sw::IWindow*>( windowHandle ), static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); }; \
        pOutApi->shutdown  = []( sw::EditorHandle editorHandle ) { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::shutdown ); };                                                        \
        pOutApi->updateUI  = []( sw::EditorHandle editorHandle ) { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::updateUI ); };                                                        \
        pOutApi->preRender = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle )                                                                                                              \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::preRender, static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                                               \
        pOutApi->render = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle )                                                                                                                 \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::render, static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                                                  \
        pOutApi->postPresent = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle )                                                                                                            \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::postPresent, static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                                             \
        pOutApi->abandonPendingDraw = []( sw::EditorHandle editorHandle )                                                                                                                                          \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::abandonPendingDraw ); };                                                                                                       \
        pOutApi->processEvent = []( sw::EditorHandle editorHandle, const sw::NativeWindowEvent* pEvent ) -> bool                                                                                                   \
        { return pEvent != nullptr ? sw::ModuleForwardUtil::callOr<EditorClass, bool>( editorHandle, false, &EditorClass::processEvent, *pEvent ) : false; };                                                      \
        pOutApi->registerTexture = []( sw::EditorHandle editorHandle, sw::TextureHandle textureHandle ) -> void*                                                                                                   \
        { return sw::ModuleForwardUtil::callOr<EditorClass, void*>( editorHandle, nullptr, &EditorClass::registerTexture, textureHandle ); };                                                                      \
        pOutApi->unregisterTexture = []( sw::EditorHandle editorHandle, void* pTextureId )                                                                                                                         \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::unregisterTexture, pTextureId ); };                                                                                            \
        pOutApi->getGameViewport = []( sw::EditorHandle editorHandle, uint64* pRenderTarget, uint32* pWidth, uint32* pHeight )                                                                                     \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::getGameViewport, pRenderTarget, pWidth, pHeight ); };                                                                          \
        pOutApi->getViewportCamera = []( sw::EditorHandle editorHandle ) -> void*                                                                                                                                  \
        { return sw::ModuleForwardUtil::callOr<EditorClass, void*>( editorHandle, nullptr, &EditorClass::getViewportCamera ); };                                                                                   \
        pOutApi->bindService = []( const sw::ModuleService* pService )                                                                                                                                             \
        {                                                                                                                                                                                                          \
            if ( pService != nullptr )                                                                                                                                                                             \
                sw::editor::bindEditorService( *pService );                                                                                                                                                        \
            else                                                                                                                                                                                                   \
                sw::editor::unbindEditorService();                                                                                                                                                                 \
        };                                                                                                                                                                                                         \
        pOutApi->isPlaying = []( sw::EditorHandle editorHandle ) -> bool                                                                                                                                           \
        { return sw::ModuleForwardUtil::callOr<EditorClass, bool>( editorHandle, false, &EditorClass::isPlaying ); };                                                                                              \
        pOutApi->isPaused = []( sw::EditorHandle editorHandle ) -> bool                                                                                                                                            \
        { return sw::ModuleForwardUtil::callOr<EditorClass, bool>( editorHandle, false, &EditorClass::isPaused ); };                                                                                               \
        pOutApi->stopSimulation = []( sw::EditorHandle editorHandle ) { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::stopSimulation ); };                                             \
        pOutApi->endFrame       = []( sw::EditorHandle editorHandle ) { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::onHostFrameEnd ); };                                             \
        return true;                                                                                                                                                                                               \
    }
// NOLINTEND(bugprone-macro-parentheses)
