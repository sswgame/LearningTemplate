/**
 * @file EditorModuleExports.h
 * @brief 에디터 모듈의 진입점(C-ABI) 구현을 위한 매크로 모음입니다.
 *
 * @note 이 헤더는 **경계 계약이 아니라 모듈 쪽 접착제**입니다. 그래서 `ABI/` 와 달리 모듈 자신의
 *       서비스 로케이터(`Editor/`)를 끌어옵니다. 모듈 구현 `.cpp` 만 include 합니다.
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "RuntimeAPI/ABI/EditorAPI.h"
#include "RuntimeAPI/ABI/ModuleAbi.h"
#include "RuntimeAPI/Export/ModuleForwardUtil.h"

namespace sw
{
    // 아래 매크로가 불투명 핸들을 되돌릴 때만 필요하다. ABI/ 쪽 계약 헤더는 이 타입들을 모른다.
    class IRHIDevice;
    class IWindow;
} // namespace sw

/**
 * @brief 에디터 모듈의 C-ABI 함수 테이블을 한 줄로 구현하고 export 하는 매크로입니다.
 * @param EditorClass sw::IEditor 를 구현하는 에디터 클래스 (보통 sw::editor::ImGuiEditor)
 * @details 핸들 캐스팅과 널 검사는 `ModuleForwardUtil` 이 합니다. 테이블에 항목을 하나 더
 *          붙일 때 건드릴 곳은 `EditorAPI` 구조체 한 줄과 여기 한 줄이어야 합니다.
 */
// 이 매크로 인자는 **타입 이름**이다. 괄호로 감싸면 `sw_new (EditorClass)()` 처럼 되어
// 문법이 깨진다. 검사기는 인자를 식으로 가정한다. 매크로 본문은 줄 연결이라 중간에
// 주석을 넣을 수 없으므로 정의 전체를 범위로 덮는다.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define SW_IMPLEMENT_EDITOR_MODULE( EditorClass )                                                                                                                                                                  \
    extern "C" SW_MODULE_API uint32      getEditorModuleAbiVersion() { return sw::kModuleAbiVersion; }                                                                                                             \
    extern "C" SW_MODULE_API const utf8* getEditorModuleAbiStamp() { return sw::kModuleAbiStamp; }                                                                                                                 \
    extern "C" SW_MODULE_API bool        exportEditorAPI( sw::EditorAPI* pOutAPI )                                                                                                                                 \
    {                                                                                                                                                                                                              \
        if ( pOutAPI == nullptr )                                                                                                                                                                                  \
            return false;                                                                                                                                                                                          \
        pOutAPI->create     = []() -> sw::EditorHandle { return sw_new EditorClass(); };                                                                                                                           \
        pOutAPI->destroy    = []( sw::EditorHandle editorHandle ) { sw_delete( static_cast<EditorClass*>( editorHandle ) ); };                                                                                     \
        pOutAPI->initialize = []( sw::EditorHandle editorHandle, sw::WindowHandle windowHandle, sw::RHIDeviceHandle rhiDeviceHandle ) -> bool                                                                      \
        { return sw::ModuleForwardUtil::callOr<EditorClass, bool>( editorHandle, false, &EditorClass::initialize, static_cast<sw::IWindow*>( windowHandle ), static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); }; \
        pOutAPI->shutdown  = []( sw::EditorHandle editorHandle ) { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::shutdown ); };                                                        \
        pOutAPI->updateUI  = []( sw::EditorHandle editorHandle ) { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::updateUI ); };                                                        \
        pOutAPI->preRender = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle )                                                                                                              \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::preRender, static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                                               \
        pOutAPI->render = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle )                                                                                                                 \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::render, static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                                                  \
        pOutAPI->postPresent = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle )                                                                                                            \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::postPresent, static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                                             \
        pOutAPI->abandonPendingDraw = []( sw::EditorHandle editorHandle )                                                                                                                                          \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::abandonPendingDraw ); };                                                                                                       \
        pOutAPI->processEvent = []( sw::EditorHandle editorHandle, const sw::NativeWindowEvent* pEvent ) -> bool                                                                                                   \
        { return pEvent != nullptr ? sw::ModuleForwardUtil::callOr<EditorClass, bool>( editorHandle, false, &EditorClass::processEvent, *pEvent ) : false; };                                                      \
        pOutAPI->registerTexture = []( sw::EditorHandle editorHandle, sw::TextureHandle textureHandle ) -> void*                                                                                                   \
        { return sw::ModuleForwardUtil::callOr<EditorClass, void*>( editorHandle, nullptr, &EditorClass::registerTexture, textureHandle ); };                                                                      \
        pOutAPI->unregisterTexture = []( sw::EditorHandle editorHandle, void* pTextureId )                                                                                                                         \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::unregisterTexture, pTextureId ); };                                                                                            \
        pOutAPI->getGameViewport = []( sw::EditorHandle editorHandle, uint64* pRenderTarget, uint32* pWidth, uint32* pHeight )                                                                                     \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::getGameViewport, pRenderTarget, pWidth, pHeight ); };                                                                          \
        pOutAPI->getSceneViewport = []( sw::EditorHandle editorHandle, uint64* pRenderTarget, uint32* pWidth, uint32* pHeight )                                                                                    \
        { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::getSceneViewport, pRenderTarget, pWidth, pHeight ); };                                                                         \
        pOutAPI->getSceneViewCamera = []( sw::EditorHandle editorHandle ) -> void*                                                                                                                                 \
        { return sw::ModuleForwardUtil::callOr<EditorClass, void*>( editorHandle, nullptr, &EditorClass::getSceneViewCamera ); };                                                                                  \
        pOutAPI->bindService = []( const sw::ModuleService* pService )                                                                                                                                             \
        {                                                                                                                                                                                                          \
            if ( pService != nullptr )                                                                                                                                                                             \
                sw::editor::bindEditorService( *pService );                                                                                                                                                        \
            else                                                                                                                                                                                                   \
                sw::editor::unbindEditorService();                                                                                                                                                                 \
        };                                                                                                                                                                                                         \
        pOutAPI->isPlaying = []( sw::EditorHandle editorHandle ) -> bool                                                                                                                                           \
        { return sw::ModuleForwardUtil::callOr<EditorClass, bool>( editorHandle, false, &EditorClass::isPlaying ); };                                                                                              \
        pOutAPI->isPaused = []( sw::EditorHandle editorHandle ) -> bool                                                                                                                                            \
        { return sw::ModuleForwardUtil::callOr<EditorClass, bool>( editorHandle, false, &EditorClass::isPaused ); };                                                                                               \
        pOutAPI->stopSimulation = []( sw::EditorHandle editorHandle ) { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::stopSimulation ); };                                             \
        pOutAPI->endFrame       = []( sw::EditorHandle editorHandle ) { sw::ModuleForwardUtil::callVoid<EditorClass>( editorHandle, &EditorClass::onHostFrameEnd ); };                                             \
        return true;                                                                                                                                                                                               \
    }
// NOLINTEND(bugprone-macro-parentheses)
