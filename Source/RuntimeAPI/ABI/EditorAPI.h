/**
 * @file EditorAPI.h
 * @brief App ↔ EditorModule 통신용 함수 테이블입니다(IEditor 구현 세부 사항을 숨깁니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "RuntimeAPI/ABI/RuntimeHandles.h"

namespace sw
{
    // 핸들은 ABI/RuntimeHandles.h 가 모아 들고 있다. EditorHandle, TextureHandle 포함.

    struct ModuleService;
    struct NativeWindowEvent;

    // ------------------------------------------------------------------------------
    // 1) EditorAPI — C ABI 함수 테이블
    //    IEditor 구현은 EditorModule 안에 두고, App 은 이 포인터만 부른다
    // ------------------------------------------------------------------------------
    /** @brief EditorModule 이 exportEditorApi 로 채우고 App 이 부르는 함수 포인터 테이블입니다. */
    struct EditorAPI
    {
        EditorHandle ( *create )(){ nullptr };                                                                               /**< @brief 에디터 인스턴스를 생성합니다. */
        void ( *destroy )( EditorHandle editor ){ nullptr };                                                                 /**< @brief 에디터 인스턴스를 파괴합니다. */
        bool ( *initialize )( EditorHandle editor, WindowHandle window, RHIDeviceHandle rhiDevice ){ nullptr };              /**< @brief 윈도우 및 RHI 디바이스로 에디터를 초기화합니다. */
        void ( *shutdown )( EditorHandle editor ){ nullptr };                                                                /**< @brief 에디터를 종료합니다. */
        void ( *updateUi )( EditorHandle editor ){ nullptr };                                                                /**< @brief 메인 스레드에서 에디터 UI 및 플랫폼 윈도우를 갱신합니다. */
        void ( *preRender )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };                                    /**< @brief 렌더링 직전에 호출됩니다. */
        void ( *render )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };                                       /**< @brief GPU 에 에디터 UI DrawData 를 렌더링합니다. */
        void ( *postPresent )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };                                  /**< @brief 렌더링 결과가 출력된 후 호출됩니다 (멀티 뷰포트 처리용). */
        void ( *abandonPendingDraw )( EditorHandle editor ){ nullptr };                                                      /**< @brief 렌더 워커를 재운 뒤, 렌더 대기 중인 draw 스냅샷 표시를 버리게 합니다. */
        bool ( *processEvent )( EditorHandle editor, const NativeWindowEvent* pEvent ){ nullptr };                           /**< @brief 네이티브 이벤트를 에디터로 전달합니다. */
        void* ( *registerTexture )(EditorHandle editor, TextureHandle texture){ nullptr };                                   /**< @brief 텍스처를 ImGui 에 등록합니다. */
        void ( *unregisterTexture )( EditorHandle editor, void* pTextureId ){ nullptr };                                     /**< @brief 텍스처를 ImGui 에서 해제합니다. */
        void ( *getGameViewport )( EditorHandle editor, uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ){ nullptr }; /**< @brief 이번 프레임 Game View RT 핸들과 크기를 조회합니다. */
        void* ( *getViewportCamera )(EditorHandle editor){ nullptr };                                                        /**< @brief 이번 프레임 Game View 카메라(CameraComponent*)를 반환합니다. */
        void ( *bindService )( const ModuleService* pService ){ nullptr };                                                   /**< @brief ModuleService 를 에디터 모듈에 주입하거나 nullptr 로 해제합니다. */
        bool ( *isPlaying )( EditorHandle editor ){ nullptr };                                                               /**< @brief 에디터 시뮬레이션(PIE)이 실행 중인지 반환합니다. */
        bool ( *isPaused )( EditorHandle editor ){ nullptr };                                                                /**< @brief 에디터 시뮬레이션이 일시정지인지 반환합니다. */
        void ( *stopSimulation )( EditorHandle editor ){ nullptr };                                                          /**< @brief 에디터 시뮬레이션(PIE)을 정지합니다(핫 리로드 등). */
        void ( *endFrame )( EditorHandle editor ){ nullptr };                                                                /**< @brief 월드 틱 이후 Step 소비 등 프레임 마감을 합니다. */
    };

    /** @brief EditorModule 이 export 하는 API 테이블 함수의 형입니다(심볼 이름: exportEditorApi). */
    using PFN_ExportEditorAPI = bool ( * )( EditorAPI* pOutApi );
} // namespace sw

extern "C"
{
    // ------------------------------------------------------------------------------
    // 2) export — EditorModule 이 채우는 진입점
    // ------------------------------------------------------------------------------
    /** @brief 이 모듈이 빌드된 표 버전입니다. 호스트가 로드 전에 자기 것과 대조합니다. */
    SW_MODULE_API uint32 getEditorModuleAbiVersion();
    /** @brief 이 모듈이 빌드된 표 지문입니다. 버전과 함께 대조합니다. */
    SW_MODULE_API const utf8* getEditorModuleAbiStamp();

    /** @brief EditorModule API 테이블을 내보냅니다. */
    SW_MODULE_API bool exportEditorApi( sw::EditorAPI* pOutApi );
}
