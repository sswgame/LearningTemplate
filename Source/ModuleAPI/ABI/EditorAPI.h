/**
 * @file EditorAPI.h
 * @brief App ↔ EditorModule 통신용 함수 테이블입니다(IEditor 구현 세부 사항을 숨깁니다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/MemoryTag.h"

#include "ModuleAPI/ABI/ModuleABI.h"
#include "ModuleAPI/ABI/ModuleHandles.h"

namespace sw
{
    // 핸들은 ABI/ModuleHandles.h 가 모아 들고 있다. EditorHandle 포함.

    struct PreviewSceneView;
    struct ViewportTarget;
    struct NativeWindowEvent;

    class ISubsystemLocator;

    /**
     * @brief 에디터가 이번 프레임 뒤에 곧바로 다음 프레임을 그려야 하는 까닭입니다(비트 묶음, `EditorAPI::getRedrawReasons`). C ABI 로는 uint32 로 건넵니다.
     * @details 하나도 없으면 에디터는 조용하다 — 호스트의 앱 루프가 창 메시지가 올 때까지 잔다(`FramePacingState::EditorIdle`).
     *          까닭을 빠뜨리면 그 화면이 다음 창 메시지(또는 깨움 상한)까지 멈추므로, 시간으로 움직이는 것은 모두 여기에 든다.
     */
    enum class EditorRedrawReason : uint32
    {
        None          = 0,
        Input         = 1u << 0, ///< ImGui 가 이번 프레임에 입력 이벤트를 받았거나 마우스 단추가 눌려 있다
        ActiveItem    = 1u << 1, ///< 끌기 · 글 입력 · 창 옮기기 · 끌어 놓기처럼 잡고 있는 항목이 있다
        Simulation    = 1u << 2, ///< 에디터 플레이 · 시뮬레이션이 돈다
        Notification  = 1u << 3, ///< 알림 토스트 · 진행 막대가 떠 있다(시간이 지나면 사라진다)
        Animation     = 1u << 4, ///< 패널이 연속 프레임을 청했다(틱하는 미리보기 씬 · 읽는 중인 썸네일, `EditorRenderViews::requestAnimationFrame`)
        BackgroundJob = 1u << 5, ///< 프레임마다 나아가는 일이 있다(원본 임포트 · 라이트 · 반사 프로브 굽기) — 워커가 끝내도 창 메시지는 오지 않는다
        AssetsLoading = 1u << 6, ///< 씬이 아직 다 도착하지 않았다(메시 · 텍스처 로드 · 업로드, 머티리얼 PSO 빌드, 씬 전환 — 감시 값 `Assets.Pending` 과 같은 판정). 잠들면 덜 그린 화면이 남는다
    };

    // ------------------------------------------------------------------------------
    // 1) EditorAPI — C ABI 함수 테이블
    //    IEditor 구현은 EditorModule 안에 두고, App 은 이 포인터만 부른다
    // ------------------------------------------------------------------------------
    /**
     * @brief `EditorAPI` 표의 항목 목록입니다 — `X( 반환형, 이름, ( 매개변수 ) )`. 구조체 멤버와 스탬프(`kEditorModuleABIStamp`)가 둘 다 여기서 나옵니다.
     * @details 항목을 더하거나 순서 · 시그니처를 바꾸면 스탬프가 저절로 바뀌어 옛 모듈 DLL 은 로드 때 거부됩니다(`ModuleABI.h`).
     *          줄 연결 매크로 안이라 설명은 블록 주석으로 씁니다(줄 주석은 다음 줄을 삼킨다).
     */
#define SW_EDITOR_API_ENTRIES( X ) /** 에디터 인스턴스를 생성합니다. */                                                                                                                         \
    X( EditorHandle, create, () )                                                                                                                                                               \
    /** 에디터 인스턴스를 파괴합니다. */                                                                                                                                                        \
    X( void, destroy, ( EditorHandle editor ) )                                                                                                                                                 \
    /** 윈도우 및 RHI 디바이스로 에디터를 초기화합니다. */                                                                                                                                      \
    X( bool, initialize, ( EditorHandle editor, WindowHandle window, RHIDeviceHandle rhiDevice ) )                                                                                              \
    /** 창 · RHI 없이 에디터를 세웁니다(에디터 단위 테스트 — 프레임을 돌리지 않는다). */                                                                                                        \
    X( bool, initializeHeadless, ( EditorHandle editor ) )                                                                                                                                      \
    /** 에디터를 종료합니다. */                                                                                                                                                                 \
    X( void, shutdown, ( EditorHandle editor ) )                                                                                                                                                \
    /** 메인 스레드에서 에디터 UI 및 플랫폼 윈도우를 갱신합니다. */                                                                                                                             \
    X( void, updateUI, ( EditorHandle editor ) )                                                                                                                                                \
    /** UI 가 보낸 명령(뷰 크기 · 재생)을 적용합니다. 게임 스레드가 씬 틱 · 틱 여부 질의 앞에서 부릅니다. */                                                                                    \
    X( void, applyUICommands, ( EditorHandle editor ) )                                                                                                                                         \
    /** 렌더 패스 밖에서 이번 프레임 UI 의 업로드(그리기 목록 · 텍스처 연산 · bindless 등록)를 합니다. */                                                                                       \
    X( void, prepareRender, ( EditorHandle editor, RHIDeviceHandle rhiDevice ) )                                                                                                                \
    /** 열린 백버퍼 패스 안에서 에디터 UI 를 그립니다(업로드는 prepareRender 가 했다). */                                                                                                       \
    X( void, render, ( EditorHandle editor, RHIDeviceHandle rhiDevice ) )                                                                                                                       \
    /** 렌더링 결과가 출력된 후 호출됩니다(멀티 뷰포트 처리용). */                                                                                                                              \
    X( void, postPresent, ( EditorHandle editor, RHIDeviceHandle rhiDevice ) )                                                                                                                  \
    /** 에디터 인스턴스를 없애기 직전에만 부릅니다(렌더 워커는 이미 비운 뒤). 렌더 대기 중인 draw 스냅샷 표시를 버리게 합니다. 모듈 리로드 중간에 부르면 UI 스레드와 렌더 스레드가 겹칩니다. */ \
    X( void, abandonPendingDraw, ( EditorHandle editor ) )                                                                                                                                      \
    /** 네이티브 이벤트를 에디터로 전달합니다. */                                                                                                                                               \
    X( bool, processEvent, ( EditorHandle editor, const NativeWindowEvent* pEvent ) )                                                                                                           \
    /** 이번 프레임 게임 뷰 RT 핸들 · 크기 · 창 안 자리를 씁니다(패널이 안 보이면 빈 값). */                                                                                                    \
    X( void, getGameViewport, ( EditorHandle editor, ViewportTarget * pOutTarget ) )                                                                                                            \
    /** 이번 프레임 씬 뷰 RT 핸들 · 크기 · 창 안 자리를 씁니다(패널이 안 보이면 빈 값). */                                                                                                      \
    X( void, getSceneViewport, ( EditorHandle editor, ViewportTarget * pOutTarget ) )                                                                                                           \
    /** 이번 프레임 씬 뷰 카메라(에디터 카메라, CameraComponent*)를 반환합니다. */                                                                                                              \
    X( void*, getSceneViewCamera, ( EditorHandle editor ) )                                                                                                                                     \
    /** 호스트 서브시스템 범위를 에디터 모듈에 주입하거나 nullptr 로 해제합니다. */                                                                                                             \
    X( void, bindService, ( const ISubsystemLocator* pService ) )                                                                                                                               \
    /** 에디터 시뮬레이션(PIE)이 실행 중인지 반환합니다. */                                                                                                                                     \
    X( bool, isPlaying, ( EditorHandle editor ) )                                                                                                                                               \
    /** 에디터 시뮬레이션이 일시정지인지 반환합니다. */                                                                                                                                         \
    X( bool, isPaused, ( EditorHandle editor ) )                                                                                                                                                \
    /** 에디터 시뮬레이션(PIE)을 정지합니다(핫 리로드 등). */                                                                                                                                   \
    X( void, stopSimulation, ( EditorHandle editor ) )                                                                                                                                          \
    /** 월드 틱 이후 Step 소비 등 프레임 마감을 합니다. */                                                                                                                                      \
    X( void, endFrame, ( EditorHandle editor ) )                                                                                                                                                \
    /** 이번 프레임에 보인 미리보기 씬 뷰(툴 창)를 pOutView 에 최대 capacity 개 씁니다. 쓴 개수를 반환합니다(PreviewSceneView, Engine 의 값 타입). */                                           \
    X( uint32, getPreviewSceneViews, ( EditorHandle editor, PreviewSceneView * pOutView, uint32 capacity ) )                                                                                    \
    /** 이번 프레임의 다시 그릴 까닭(EditorRedrawReason 비트 묶음)입니다. updateUI 뒤에 부릅니다. 0 이면 에디터가 조용합니다. */                                                                \
    X( uint32, getRedrawReasons, ( EditorHandle editor ) )                                                                                                                                      \
    /** 앱이 끝나는 중이니 에디터의 백그라운드 잡(색인 스캔 · 임포트 …)은 일찍 끝내라는 협조 신호를 올립니다. 호스트가 종료 대기(flushRenderWorkers) 앞에서 한 번 부릅니다. */                  \
    X( void, requestBackgroundStop, ( EditorHandle editor ) )                                                                                                                                   \
    /** 지금 이 모듈을 내리면 안 되면 true 입니다(다른 스레드가 이 모듈 코드에 있다 — 열린 파일 대화상자). 인스턴스가 없어도 부를 수 있다. 호스트가 리로드 잡음으로 건다. */                    \
    X( bool, isReloadHeld, () )

    /** @brief EditorModule 이 exportEditorAPI 로 채우고 App 이 부르는 함수 포인터 테이블입니다. */
    struct EditorAPI
    {
        SW_EDITOR_API_ENTRIES( SW_MODULE_ABI_ENTRY_MEMBER )
    };

    /** @brief EditorModule 이 export 하는 API 테이블 함수의 형입니다(심볼 이름: exportEditorAPI). */
    using PFN_ExportEditorAPI = bool ( * )( EditorAPI* pOutAPI );

    /** @brief 헤드리스 임포트가 다루는 원본 종류입니다. C ABI 로는 uint32 로 건넵니다. */
    enum class EditorImportKind : uint32
    {
        Texture        = 0, ///< `textures_raw/` 의 이미지 → DDS(`App --import-textures` · `--check-textures`)
        Model          = 1, ///< `models_raw/` 의 glTF → `.mesh`(`App --import-models` · `--check-models`)
        Heightfield    = 2, ///< `heightfields_raw/` 의 16 비트 PNG · `.r16` → `.heightfield`(`App --import-heightfields` · `--check-heightfields`)
        TextureVariant = 3, ///< `textures_raw/` 의 이미지 → 모든 배포 대상의 형식 변형(`a.astc.dds`, `App --cook-texture-variants` · `--check-texture-variants`)
    };

    /**
     * @brief 이름으로 찾는 헤드리스 진입점(에디터 인스턴스 없이 모듈만 올려 부른다)의 목록입니다 — `X( 반환형, 형 이름, ( 매개변수 ) )` 가 `PFN_<형 이름>` 이 됩니다.
     * @details 시그니처가 바뀌면 표와 같이 `kEditorModuleABIStamp` 가 바뀝니다(v2 에서 `importEditorAssets` 가 서비스 표를 받게 된 변경이 이 자리).
     *          심볼 이름은 아래 `k...Symbol` 이고, 구현은 아래 extern "C" 선언입니다.
     */
#define SW_EDITOR_HEADLESS_ENTRIES( X )                                                                                                          \
    /** 헤드리스 에셋 임포트(심볼 kImportEditorAssetsSymbol). */                                                                                 \
    X( int32, ImportEditorAssets, ( uint32 kind, uint32 checkOnly, const ISubsystemLocator* pService ) )                                         \
    /** 헤드리스 로컬라이제이션(심볼 kRunEditorLocalizationTaskSymbol). */                                                                       \
    X( int32, RunEditorLocalizationTask, ( uint32 task, const utf8* pPoPath, const utf8* pProjectArgument, const ISubsystemLocator* pService ) ) \
    /** 헤드리스 에셋 참조 검사(심볼 kCheckEditorAssetReferencesSymbol). */                                                                      \
    X( int32, CheckEditorAssetReferences, ( const ISubsystemLocator* pService ) )

    SW_EDITOR_HEADLESS_ENTRIES( SW_MODULE_ABI_ENTRY_PFN )

    /** @brief 에디터 모듈 경계의 스탬프입니다 — `EditorAPI` 표와 헤드리스 진입점 목록의 글. 에디터 모듈이 내보내고(`getEditorModuleABIStamp`) 호스트가 로드 전에 대조합니다. */
    inline constexpr uint64 kEditorModuleABIStamp =
        ModuleABIUtil::computeStamp( SW_EDITOR_API_ENTRIES( SW_MODULE_ABI_ENTRY_TEXT ) SW_EDITOR_HEADLESS_ENTRIES( SW_MODULE_ABI_ENTRY_TEXT ) );

    /** @brief 에디터 모듈 경계의 할당 태그입니다 — 표 항목(`EditorModuleForwardUtil`)과 모듈 매니저의 로드 · 리로드가 이 값 하나를 쓴다. */
    inline constexpr MemoryTag kEditorModuleMemoryTag = MemoryTag::Editor;

    /** @brief `importEditorAssets` 의 심볼 이름입니다. 에디터 인스턴스 없이 모듈만 올려 부릅니다(`App --import-textures` · `--import-models`). */
    inline constexpr const utf8* kImportEditorAssetsSymbol = "importEditorAssets";

    /** @brief 헤드리스 로컬라이제이션 작업입니다. C ABI 로는 uint32 로 건넵니다. */
    enum class EditorLocalizationTask : uint32
    {
        GatherText = 0, ///< 코드 · 데이터에서 글을 모아 원문 · 번역 표를 고친다(`App --gather-text`)
        CheckText  = 1, ///< 고치지 않고 표가 최신인지만 본다(`App --check-text`)
        ImportPo   = 2, ///< PO 하나를 번역 표로 가져온다(`App --import-po=<파일>`)
        ExportPo   = 3, ///< 문화권마다 `po/<culture>.po` 를 쓴다(`App --export-po`)
    };

    /** @brief `runEditorLocalizationTask` 의 심볼 이름입니다. 에디터 인스턴스 없이 모듈만 올려 부릅니다(`App --gather-text` · `--check-text`). */
    inline constexpr const utf8* kRunEditorLocalizationTaskSymbol = "runEditorLocalizationTask";

    /** @brief `validateEditorAssetReferences` 의 심볼 이름입니다. 에디터 인스턴스 없이 모듈만 올려 부릅니다(`App --check-asset-refs`). */
    inline constexpr const utf8* kCheckEditorAssetReferencesSymbol = "validateEditorAssetReferences";
} // namespace sw

extern "C"
{
    // ------------------------------------------------------------------------------
    // 2) export — EditorModule 이 채우는 진입점
    // ------------------------------------------------------------------------------
    /** @brief 이 모듈이 빌드된 경계 스탬프(`sw::kEditorModuleABIStamp`)입니다. 호스트가 로드 전에 자기 것과 대조합니다. */
    SW_MODULE_API uint64 getEditorModuleABIStamp();

    /** @brief EditorModule API 테이블을 내보냅니다. */
    SW_MODULE_API bool exportEditorAPI( sw::EditorAPI* pOutAPI );

    /**
     * @brief 리소스 트리의 @p kind(`sw::EditorImportKind`) 원본을 그 폴더의 `import.stamp` 와 대조하고, @p checkOnly 가 0 이면 어긋난 것을 임포트합니다.
     * @details 에디터 인스턴스 · 창 · RHI 없이 부릅니다. 리소스 루트와 로거는 호스트(Engine)가 이미 세워 두었어야 합니다.
     *          @p pService 가 있으면 임포트하는 동안 에디터 서비스로 묶습니다 — 텍스처 압축이 작업 시스템(`TaskManager`)으로 나뉩니다. 없으면 한 스레드로 돕니다.
     * @return 남은 문제 수입니다(0 이면 원본과 임포트 결과가 맞습니다). 설정을 읽지 못하거나 모르는 종류면 음수입니다.
     */
    SW_MODULE_API int32 importEditorAssets( uint32 kind, uint32 checkOnly, const sw::ISubsystemLocator* pService );

    /**
     * @brief 로컬라이제이션 작업 @p task(`sw::EditorLocalizationTask`) 하나를 돌립니다. 소스 트리 · 리소스 트리가 있어야 합니다.
     * @details 에디터 인스턴스 · 창 · RHI 없이 부릅니다. 모든 타입 공급자 모듈이 올라온 뒤여야 리플렉션 프로퍼티(`Meta = "Localizable"`)를 봅니다.
     *          @p pPoPath 는 `ImportPo` 에서만 읽고, @p pProjectArgument 가 비면(nullptr 포함) 엔진 프로젝트와 활성 게임 팩의 프로젝트 전부입니다.
     * @return 0 이면 성공(확인 모드는 표가 최신), 아니면 실패입니다.
     */
    SW_MODULE_API int32 runEditorLocalizationTask( uint32 task, const utf8* pPoPath, const utf8* pProjectArgument, const sw::ISubsystemLocator* pService );

    /**
     * @brief 에셋 참조를 검사합니다 — 글 에셋 · 테스트 CMake 의 경로 칸이 모두 있는 것을 가리키는지, 리플렉션 `AssetPath` 프로퍼티가 모두 칸 목록에 있는지.
     * @details 에디터 인스턴스 · 창 · RHI 없이 부릅니다. 모든 타입 공급자 모듈이 올라온 뒤여야 리플렉션 프로퍼티를 다 봅니다. 칸 목록은 `Config/Dev/AssetReferenceFields.json` 입니다.
     * @return 문제 수입니다(0 이면 통과). 칸 목록이나 트리를 읽지 못하면 음수입니다.
     */
    SW_MODULE_API int32 validateEditorAssetReferences( const sw::ISubsystemLocator* pService );
}
