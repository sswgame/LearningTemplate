/**
 * @file EditorPlayToolbarModel.h
 * @brief 에디터 상단 툴바(`EditorPlayToolbar`)의 ViewModel — 플레이 세션 · 멀티 프로세스 재생 · 시간 배율의 값을 모으고, 단추의 의도를 명령으로 받습니다. ImGui 를 모릅니다.
 * @details 툴바 View 는 이 값(`EditorPlayToolbarViewState`)만 그리고 명령을 부른다(`docs/agents/EditorUI.md` MVVM). 플레이 세션 · 작업 공간 · 카메라는 이 파일의 `.cpp` 만 본다.
 *          툴바가 프레임 사이에 기억하는 보기 값(Step N 칸 · 확인 모달 · 넘침 폭)은 `EditorPlayToolbarUIState` 로 이 모델이 든다(에디터 범위 상태 저장소의 한 칸).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySettings.h"

namespace sw::editor
{
    /** @brief 플레이를 시작하는 단추의 종류입니다. 미저장 확인 모달이 어느 쪽을 이어 갈지 기억합니다. */
    enum class EditorPlaySessionKind : uint8
    {
        Play = 0,
        Simulate
    };

    /** @brief 재생 단추 뒤의 묶음입니다. 줄에 들지 않는 묶음부터 끝까지 넘침 펼침으로 옮긴다. */
    enum class EditorPlayToolbarSection : uint8
    {
        SessionOptions = 0, ///< 카메라에서 시작 · 시간 배율 · 자동 플레이
        RenderDoc,
        Count
    };

    /** @brief 툴바가 프레임 사이에 기억하는 보기 값입니다(기록하지 않는다). */
    struct EditorPlayToolbarUIState
    {
        int32                 _stepFrameCount{ 10 }; ///< "Step N" 이 진행할 프레임 수
        EditorPlaySessionKind _pendingSession{ EditorPlaySessionKind::Play };
        uint8                 _bConfirmUnsavedPlay{ SW_FALSE };                                           ///< 다음 그리기에서 미저장 확인 모달을 연다
        uint8                 _bStartAtCamera{ SW_FALSE };                                                ///< Play 를 에디터 카메라 위치에서 시작한다
        uint8                 _bAutoplayOffered{ SW_FALSE };                                              ///< 마지막 프레임에 자동 플레이 단추를 내놓았다(줄에 그렸거나 ">>" 펼침 안에 있다)
        float32               _arrSectionWidth[static_cast<size_t>( EditorPlayToolbarSection::Count )]{}; ///< 묶음마다 마지막으로 그린 폭(넘침 판정)
    };

    /** @brief 툴바가 한 프레임 그릴 값입니다(`EditorPlayToolbarModel::gather`). */
    struct EditorPlayToolbarViewState
    {
        EditorNetPlaySettings _netSettings{}; ///< 다음 멀티 프로세스 재생의 구성(사본 — 바꾸면 `setNetSettings`)
        int32                 _netConnectedClients{ 0 };
        int32                 _netProcessCount{ 0 };
        float32               _timeScale{ 1.0f };
        uint8                 _bNetRunning{ SW_FALSE };
        uint8                 _bNetStopping{ SW_FALSE }; ///< 앞 세션을 거두는 중이다(`start` 가 거절한다)
        uint8                 _bStopped{ SW_FALSE };     ///< 에디터 안 재생이 멈춰 있다
        uint8                 _bPlayQueued{ SW_FALSE };  ///< 씬 로드가 끝나면 시작한다
        uint8                 _bPlaying{ SW_FALSE };     ///< 플레이어가 조종하는 재생이 돈다
        uint8                 _bSimulating{ SW_FALSE };  ///< 월드만 도는 재생(Simulate)이 돈다
        uint8                 _bPaused{ SW_FALSE };
    };

    /**
     * @class EditorPlayToolbarModel
     * @brief 툴바의 상태와 명령입니다. 에디터 범위마다 하나(`get`).
     */
    class SW_EDITOR_API EditorPlayToolbarModel
    {
    public:
        /** @brief 지금 에디터 범위의 툴바 모델입니다(상태 저장소가 든다). 에디터가 서 있을 때만 부른다. */
        [[nodiscard]] static EditorPlayToolbarModel& get();

        /** @brief 멀티 프로세스 재생의 로그 · 종료 알림을 모읍니다(Multiplayer 창을 닫아도 툴바가 프레임마다 부른다). */
        void updateNetPlay();
        /** @brief 이번 프레임 값을 @p outState 에 채웁니다. */
        void gather( EditorPlayToolbarViewState& outState ) const;

        /** @brief 미저장 씬이고 멈춰 있으면 확인 모달을 열게 하고, 아니면 바로 시작합니다. */
        void requestSession( EditorPlaySessionKind session );
        // 재생 명령(시작 · 멈춤 · Step · 정지)은 플레이 세션을 바로 바꾸지 않고 UI 명령으로 보낸다 — 이 UI 프레임 뒤 게임 틱 시작에 적용되고 결과는 다음 `gather` 가 본다(`EditorUICommandQueue`).
        /** @brief 세션 시작을 보냅니다. 카메라에서 시작이 켜져 있으면 에디터 카메라 위치를 시작 위치로 넘깁니다(멈춤에서 시작할 때만). */
        void startSession( EditorPlaySessionKind session );
        void pause();
        void stepOnce();
        /** @brief Step N 칸의 프레임 수만큼 진행을 보냅니다. */
        void stepFrames();
        /** @brief 정지를 보냅니다. 적용 쪽이 재생을 멈추고 선택을 이름으로 편집 씬의 오브젝트에 다시 잇습니다. */
        void stop();
        /** @brief Step N 칸 값을 1 ~ 세션 상한으로 자릅니다. */
        void clampStepFrameCount();
        void setTimeScale( float32 timeScale );

        /** @brief 다음 멀티 프로세스 재생의 구성을 바꿉니다(Net Mode 고르기). */
        void setNetSettings( const EditorNetPlaySettings& settings );
        /** @brief 지금 구성으로 멀티 프로세스 재생을 띄웁니다(실패 이유는 세션이 로그에 남긴다). */
        void startNetPlay();
        void stopNetPlay();

        EditorPlayToolbarUIState&       editUIState() { return _uiState; }
        const EditorPlayToolbarUIState& getUIState() const { return _uiState; }

    private:
        EditorPlayToolbarUIState _uiState{};
    };
} // namespace sw::editor
