/**
 * @file EditorUICapture.h
 * @brief 에디터 UI 전체(메인 창의 ImGui 패널 + 멀티 뷰포트 플로팅 창)를 오프스크린 텍스처에 다시 그려 PNG 로 저장합니다.
 * @details `-gv_screenshot` 은 게임 뷰(Present 결과)만 담고 에디터 UI 는 담지 못한다. 창을 전경으로 올려 화면을 복사하는 방식은 사용자 화면을 훔치고
 *          다른 창이 섞인다. 그래서 렌더 스레드가 그 프레임의 **최종 ImGui 그리기 목록**(모든 뷰포트를 화면 좌표로 합친 것)을 백버퍼와 같은 포맷의
 *          렌더 타깃에 한 번 더 그리고, endFrame 뒤에 읽어 파일로 쓴다. 네 백엔드가 같은 ImGui 렌더러 백엔드로 같은 목록을 그리므로 결과가 같고, 창 위치 · 가림 ·
 *          포커스와 무관하다. 언리얼의 `FScreenshotRequest`(UI 포함 스크린샷)와 Slate 위젯 리플렉터 캡처, 유니티의 `ScreenCapture` 에 해당한다.
 *
 *          흐름: 시나리오 단계(`CaptureEditorUI`)나 `-gv_screenshotEditor` 가 `request` → UI 스레드가 ImGui 를 닫은 뒤 `takeJob`(창 잘림 지표를 이때 측정한다) →
 *          스냅샷이 합본을 복사 → 렌더 스레드 `renderJob`(present 훅) → `finishJob`(postPresent, GPU 를 기다려 읽는다) → `findResult`.
 *          주의: 읽기는 GPU 를 기다린다. 캡처하는 프레임만 그렇고, 요청이 없으면 아무 일도 하지 않는다.
 *          주의: OpenGL 은 FBO 의 0 행이 아래쪽이라 읽은 뒤 뒤집는다(엔진 텍스처와 달리 ImGui GL 렌더러는 클립 원점을 보고 위아래를 바꿔 그린다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorSubsystem.h"

struct ImDrawData;

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    class IImGuiRendererBackend;
} // namespace sw::editor

namespace sw::editor
{
    /** @struct EditorUICaptureJob @brief UI 스레드가 렌더 스레드로 넘기는 캡처 한 건입니다. 잘림 지표는 UI 스레드가 ImGui 를 닫을 때 잰 값입니다. */
    struct EditorUICaptureJob
    {
        string _path{};                        ///< 쓸 파일(.png)
        string _clippedWindowNames{};          ///< 잘린 창 이름들(", " 로 잇는다)
        uint32 _ticket{ 0 };                   ///< `request` 가 돌려준 번호
        uint32 _clippedWindowCount{ 0 };       ///< 내용이 창보다 큰데 그 방향 스크롤바가 없는 창 수입니다(내용이 잘려 보이지 않는다)
        uint32 _windowCount{ 0 };              ///< 그린 창 수(자식 창 포함)
        bool   _bFromStartupArgument{ false }; ///< `-gv_screenshotEditor` 가 요청한 건이다(`-gv_screenshotQuit` 이 이 건의 결과로 앱을 끝낸다).
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @struct EditorUICaptureResult @brief 캡처 한 건의 결과입니다. */
    struct EditorUICaptureResult
    {
        EditorUICaptureJob _job{};
        float64            _magentaFraction{ 0.0 }; ///< 마젠타(R≥230, B≥230, G≤40) 픽셀 비율. 빠진 텍스처 · 디버그 색이 UI 에 섞였는지 본다
        uint32             _width{ 0 };
        uint32             _height{ 0 };
        uint8              _bWritten{ SW_FALSE }; ///< 읽고 파일로 썼으면 SW_TRUE
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorUICapture
     * @brief 에디터 UI 캡처의 요청 · 렌더 · 결과를 맡는 에디터 서브시스템입니다. 요청과 결과 조회는 UI(게임) 스레드, 렌더와 읽기는 렌더 스레드입니다.
     */
    class EditorUICapture final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorUICapture";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        // ------------------------------------------------------------------------------
        // UI 스레드
        // ------------------------------------------------------------------------------
        /** @brief 다음 UI 프레임의 에디터 UI 전체를 @p path(.png)에 찍도록 요청하고 번호를 돌려줍니다. */
        uint32 request( string_view path );
        /**
         * @brief ImGui 를 닫은 직후(`ImGui::Render` 뒤) 부릅니다. 이번 프레임에 찍을 요청이 있으면 꺼내 잘림 지표를 재고 true 입니다.
         * @details `-gv_screenshotEditor` 의 대기 프레임도 여기서 센다.
         */
        bool takeJob( EditorUICaptureJob& outJob );
        /** @brief @p ticket 의 결과가 나왔으면 true 와 결과입니다. */
        [[nodiscard]] bool findResult( uint32 ticket, EditorUICaptureResult& outResult ) const;

        // ------------------------------------------------------------------------------
        // 렌더 스레드
        // ------------------------------------------------------------------------------
        /** @brief present 훅 안에서 합본 @p drawData 를 캡처 텍스처에 그립니다. 읽기는 `finishJob` 이 합니다. */
        void renderJob( IRHIDevice& device, IImGuiRendererBackend& rendererBackend, ImDrawData& drawData, const EditorUICaptureJob& job );
        /** @brief endFrame 뒤(postPresent)에 부릅니다. 그린 캡처가 있으면 읽어 파일로 쓰고 결과를 전달합니다. */
        void finishJob( IRHIDevice& device );
        /** @brief 캡처 텍스처를 놓습니다. 렌더 스레드가 쉬는 동안(에디터 종료) 부릅니다. */
        void releaseRHI( IRHIDevice* pDevice );

    private:
        void ensureTexture( IRHIDevice& device, uint32 width, uint32 height );
        void addResult( const EditorUICaptureResult& result );

    private:
        vector<EditorUICaptureJob>    _listQueued; ///< UI 스레드 전용
        mutable mutex                 _resultMutex;
        vector<EditorUICaptureResult> _listResult;    ///< `_resultMutex` 로 지킨다
        EditorUICaptureJob            _renderedJob{}; ///< 렌더 스레드 전용 — 이번 프레임에 그린 것
        uint64                        _texture{ 0 };  ///< 렌더 스레드 전용 — RHITextureHandle
        uint32                        _textureWidth{ 0 };
        uint32                        _textureHeight{ 0 };
        uint32                        _nextTicket{ 1 };
        uint32                        _uiFrameCount{ 0 };     ///< `-gv_screenshotEditor` 대기 프레임 수
        uint8                         _bRendered{ SW_FALSE }; ///< 렌더 스레드 전용
        uint8                         _bGlobalRequested{ SW_FALSE };
    };
} // namespace sw::editor
