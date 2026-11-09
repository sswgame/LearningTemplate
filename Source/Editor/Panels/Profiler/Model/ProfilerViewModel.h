/**
 * @file ProfilerViewModel.h
 * @brief 프로파일러 창의 ViewModel — 창이 닿는 에디터 · 엔진 서비스(프레임 프로파일러 · 메모리 프로파일러 · RHI 장치 · 작업 관리자 · 멀티 프로세스 재생)와
 *        활성 씬의 컴포넌트 분포(`gatherScene`), "Tracy 열기" 명령을 맡습니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 구간 수집 · 타임라인 · 성능 탭의 상태는 원래대로 창 상태 모델(`ProfilerCaptureModel` · `ProfilerTimelineModel` · `ProfilerPerformanceModel`)이 들고,
 *          이 ViewModel 은 View 가 작업 공간 · 씬 · 명령 헤더를 보지 않게 그 밖의 모델 접근을 한 곳에 모은다(엔진 서비스는 Model 의 정적 함수로 찾는다 — `AudioMixerModel::getAudioService` 와 같다).
 *          아무것도 문서 데이터를 바꾸지 않으므로 되돌리기를 남기지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class FrameProfiler;
    class IRHIDevice;
    class MemoryProfiler;
} // namespace sw

namespace sw::editor
{
    /** @brief 컴포넌트 종류 하나의 활성 인스턴스 수입니다(값만). */
    struct ProfilerComponentRow
    {
        string _typeName;
        uint32 _instanceCount{ 0 };
    };

    /** @brief 활성 씬의 오브젝트 · 컴포넌트 집계입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). 행은 인스턴스 수의 내림차순이다. */
    struct ProfilerSceneViewState
    {
        vector<ProfilerComponentRow> _listRow{};
        uint32                       _objectCount{ 0 };
        uint32                       _rootCount{ 0 };
        uint32                       _componentCount{ 0 };
        uint8                        _bHasScene{ SW_FALSE }; ///< 편집 중인 씬이 있다
    };

    /**
     * @class ProfilerViewModel
     * @brief 프로파일러 창의 모델 접근 창구입니다. 파일 머리말 참고.
     */
    class ProfilerViewModel
    {
    public:
        ProfilerViewModel();

        /** @brief 에디터의 프레임 프로파일러입니다(필수 서비스 — 없으면 멈춘다). */
        [[nodiscard]] static FrameProfiler& getFrameProfiler();
        /** @brief 메모리 프로파일러입니다. 이 빌드에 없으면 nullptr 입니다. */
        [[nodiscard]] static MemoryProfiler* findMemoryProfiler();
        /** @brief 에디터가 그리는 RHI 장치입니다. 아직 없으면 nullptr 입니다(GPU Memory 탭). */
        [[nodiscard]] static IRHIDevice* findRHIDevice();
        /** @brief 작업 관리자의 작업 스레드 수입니다. */
        [[nodiscard]] static uint32 getWorkerCount();
#if !defined( SW_SHIPPING )
        /** @brief 멀티 프로세스 재생이 Stop 뒤에 합쳐 아직 열지 않은 트레이스 경로를 꺼냅니다. 없으면 빈 글입니다(한 번만 돌려준다). */
        [[nodiscard]] static string takeMergedNetPlayTrace();
#endif

        /** @brief 활성 씬의 오브젝트 · 컴포넌트 분포를 모읍니다(@p outState 를 비우고 채운다). 씬이 없으면 `_bHasScene` 이 거짓입니다. */
        void gatherScene( ProfilerSceneViewState& outState ) const;

        /** @brief Tracy 출력을 켜고 외부 뷰어를 띄웁니다("Open Tracy"). 실패하면 그 이유를 `getTracyError` 가 돌려준다. */
        void openTracyViewer();
        /** @brief 마지막 "Open Tracy" 가 실패했으면 그 이유 한 줄, 누른 적이 없거나 성공했으면 빈 글입니다. */
        const string& getTracyError() const { return _tracyError; }

    private:
        string _tracyError; ///< 마지막 "Open Tracy" 의 실패 이유(성공 · 누르기 전이면 빈 글)
    };
} // namespace sw::editor
