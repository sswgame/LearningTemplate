/**
 * @file UiSystem.h
 * @brief 런타임 UI 의 엔진 서비스입니다 — 화면 스택 · 입력 → 사건 · 갱신 순서를 돕니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/UI/Core/UiFocusManager.h"
#include "Engine/UI/Core/UiPointerState.h"
#include "Engine/UI/Core/WidgetTypes.h"
#include "Engine/UI/Screen/UiScreen.h"

namespace sw
{
    class FontSystem;
    class InputManager;

    /** @brief UI 를 지금 무엇으로 다루는가입니다(CommonUI 의 입력 방식). 탐색이면 포커스 테두리를 보이고, 포인터면 숨긴다. */
    enum class UiInputMode : uint8
    {
        Pointer,   ///< 마우스 · 터치
        Navigation ///< 키보드 · 게임패드
    };
} // namespace sw

namespace sw
{
    /**
     * @class UiSystem
     * @brief 런타임 UI 의 엔진 서비스입니다(`engine::getUiSystem()` · 게임은 `game::getService<UiSystem>()`).
     * @details 언리얼 `FSlateApplication` + CommonUI `UCommonUIActionRouter` 의 자리입니다. 틱은 둘로 나뉩니다 —
     *          `processInput`(입력 갱신 직후, 게임 틱 **앞** — UI 가 먹은 입력을 폰이 못 보게)과 `update`(게임 틱 **뒤**, 렌더 패킷 앞 —
     *          이번 프레임의 게임 상태로 애니메이션 → 바인딩 → 스타일 → 레이아웃 → 그리기). 기동 단계 `Ui`(Client 대상)가 `initialize` 합니다.
     *
     *          **화면 스택**: 화면은 층(`UiLayer`)마다 쌓입니다 — 그리기 순서 = 층 순서 → 층 안 쌓인 순서, 입력은 그 역순입니다. 맨 위의 포커스 받는 화면이
     *          **활성 화면**이고 포커스 · UI 행동을 받습니다. 모달 · 로딩 화면은 아래 화면과 게임의 입력을 막습니다(`isGameInputBlocked` — 플레이어 조종자가 본다).
     *          덮인 화면은 그때의 포커스를 기억했다가 다시 활성이 되면 돌려줍니다. 닫기는 지연입니다(이번 입력 처리가 끝난 뒤).
     *
     *          핫 리로드: 내려가는 모듈 이미지에 vtable 이 있는 화면 · 위젯(게임 · 키트가 만든 타입)이 든 화면은 그 자리에서 닫습니다. 게임 스레드만.
     */
    class SW_API UiSystem final : public IModuleUnloadListener
    {
    public:
        UiSystem();
        ~UiSystem() override;
        UiSystem( const UiSystem& )            = delete;
        UiSystem& operator=( const UiSystem& ) = delete;

        /**
         * @brief 입력을 묶고 일을 시작합니다.
         * @param inputManager 행동 · 포인터를 읽는 입력(시험은 자기 것을 넘긴다).
         * @param pFontSystem 글 측정 · 그리기가 쓰는 글꼴(레이아웃부터). 없으면 글 위젯이 크기 0 입니다.
         */
        [[nodiscard]] bool initialize( InputManager& inputManager, FontSystem* pFontSystem );
        /** @brief 화면을 모두 닫고(지연 없이) 입력을 놓습니다. 건 게임 정지 요청도 풉니다. */
        void shutdown();
        bool isInitialized() const { return _pInput != nullptr; }

        /** @brief 입력 → UI 사건(게임 틱 앞). 포인터 → 닫기 요청 적용 순서입니다. */
        void processInput( float32 deltaSeconds );
        /** @brief 애니메이션 → 바인딩 → 스타일 → 레이아웃 → 그리기(게임 틱 뒤). @p viewport 는 이번 프레임에 UI 를 그릴 화면입니다. */
        void update( float32 deltaSeconds, const UiViewport& viewport );

        // --- 화면 스택 ---------------------------------------------------------------
        /** @brief 화면을 그 층의 맨 위에 올립니다. 활성 화면이 바뀌면 포커스를 옮깁니다(탐색 방식이면 기본 포커스). */
        UiScreenHandle pushScreen( unique_ptr<UiScreen> screen );
        /** @brief 화면을 닫습니다(지연 — 사건 처리 중에 닫아도 이번 경로가 끝난 뒤 · 다음 `processInput` 끝 · `update` 앞에서 지운다). */
        void closeScreen( UiScreenHandle handle );
        /** @brief 화면을 찾습니다. 없거나 이미 지웠으면 nullptr 입니다(닫는 중인 화면은 찾는다). */
        UiScreen* findScreen( UiScreenHandle handle ) const;
        /** @brief 지금 활성 화면(맨 위의 포커스 받는 화면 — 막는 화면 아래로는 내려가지 않는다)입니다. 없으면 nullptr 입니다. */
        UiScreen* getActiveScreen() const;
        uint32    getScreenCount() const { return static_cast<uint32>( _listScreen.size() ); }

        // --- 게임 쪽이 묻는 것 -----------------------------------------------------------
        /** @brief 모달 · 로딩 화면이 떠 있어 게임 입력을 막아야 하면 true 입니다(플레이어 조종자가 의도를 0 으로 둔다). */
        bool isGameInputBlocked() const;
        /** @brief 활성 화면이 OS 커서를 바라면 true 입니다(플레이어 조종자가 마우스 잠금을 쉰다). */
        bool wantsCursor() const;

        // --- 포커스 · 입력 방식 ---------------------------------------------------------------
        UiFocusManager&       getFocusManager() { return _focus; }
        const UiFocusManager& getFocusManager() const { return _focus; }
        const UiPointerState& getPointerState() const { return _pointer; }
        UiInputMode           getInputMode() const { return _inputMode; }
        /** @brief 입력 방식을 바꿉니다. 탐색으로 바뀌는 순간 활성 화면에 포커스가 없으면 기본 포커스로 옮깁니다. */
        void setInputMode( UiInputMode mode );

        InputManager*     getInputManager() const { return _pInput; }
        FontSystem*       getFontSystem() const { return _pFontSystem; }
        const UiViewport& getViewport() const { return _viewport; }

        const utf8* getModuleUnloadListenerName() const override { return "ui screens"; }
        /** @brief vtable 이 [@p pBegin, @p pEnd) 안인 화면 · 위젯이 든 화면을 그 자리에서 닫습니다. 닫은 화면 수를 반환합니다. */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

    private:
        /** @brief 마우스 상태를 포인터 사건으로 바꿔 화면들로 보냅니다. */
        void processPointer();
        /** @brief 포인터 사건 하나를 받을 화면으로 보냅니다. 처리했으면 true 입니다. */
        bool dispatchPointerEvent( const UiPointerEvent& event );
        /** @brief 점 아래의 맨 위 화면입니다(막는 화면 아래로는 내려가지 않는다). 없으면 nullptr 입니다. */
        UiScreen* findPointerScreen( const float2& point ) const;
        /** @brief 닫기를 요청한 화면을 지웁니다. */
        void applyPendingCloses();
        /** @brief 화면 @p index 를 바로 지웁니다(포인터 · 포커스가 그 트리를 놓게). */
        void destroyScreenAt( uint32 index );
        /** @brief 활성 화면 · 게임 정지를 다시 정합니다(화면을 올리고 지운 뒤). */
        void refreshActiveScreen();
        /** @brief @p screen 이 활성이 될 때의 포커스 — 기억한 위젯, 없고 탐색 방식이면 기본 포커스. */
        void restoreFocus( UiScreen& screen );

    private:
        vector<unique_ptr<UiScreen>> _listScreen; ///< 그리기 순서(층 → 쌓인 순서). 입력은 역순.
        UiFocusManager               _focus;
        UiPointerState               _pointer;
        InputManager*                _pInput;
        FontSystem*                  _pFontSystem;
        UiViewport                   _viewport;
        float2                       _lastPointerPosition;
        UiScreenHandle               _activeScreen;
        UiScreenHandle               _nextScreenHandle;
        uint32                       _nextPushOrder;
        UiInputMode                  _inputMode;
        uint8                        _bPauseRequested : 1; ///< 게임 정지 요청을 걸어 두었다(정지 화면이 하나라도 있다)
        uint8                        _bPendingClose   : 1; ///< 닫기를 요청한 화면이 있다
        uint8                        _bPointerKnown   : 1; ///< 마우스 위치를 한 번 읽었다(첫 프레임의 Move 기준)
        [[maybe_unused]] uint8       _reserved        : 5;
    };
} // namespace sw
