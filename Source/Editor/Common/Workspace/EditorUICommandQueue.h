/**
 * @file EditorUICommandQueue.h
 * @brief 에디터 UI 가 엔진 상태(뷰 렌더 타깃 크기 · 플레이 세션)를 바꾸려는 의도를 담는 명령 큐입니다. UI 는 넣기만 하고 게임 스레드가 다음 게임 틱 시작에 적용합니다.
 * @details 에디터 UI 스레드 분리 3 단계의 출력 계약이다(`docs/DesignBacklog.md` "에디터 UI 스레드 분리 검토" 3). **UI 프레임 N 에 넣은 명령은 게임 틱 N+1 시작에 적용된다 — 예전 직접 변경과
 *          지연이 같다.** 호스트 프레임은 게임 모듈 갱신 → UI 프레임 → 씬 틱 순이고, 호스트가 UI 프레임 뒤 · 씬 틱과 틱 여부 질의 앞에서 `applyUICommands` 를 부르므로(게임 스레드)
 *          UI 프레임 N 의 Step · Pause 는 바로 뒤의 씬 틱이, Play 는 다음 게임 모듈 갱신이 받는다. UI 를 게임 틱과 나란히 돌리게 되면 그때 한 프레임 지연이 생긴다(DesignBacklog 3). 입력 큐(`InputManager::postRawEventAt`)와
 *          같은 모양이다 — 칸마다 적용할 프레임 번호를 들고, `takeDue` 가 그 번호까지를 넣은 순서대로 꺼내고 뒤 번호는 남긴다. 언리얼 Slate 가 게임 스레드에 명령을 큐로 넘기는 자리다.
 *          적용(엔진 상태를 실제로 바꾸는 쪽)은 `applyDue` · `EditorUICommand::apply` 이고 본문(.cpp)만 엔진을 본다 — 이 헤더는 값과 큐만 안다(큐는 엔진 없이 테스트한다).
 */
#pragma once
#include "Core/Concurrency/Queue/ConcurrentQueue.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorViewTargetUtil.h"

namespace sw::editor
{
    /** @brief UI 가 보내는 명령의 종류입니다. */
    enum class EditorUICommandKind : uint8
    {
        FitViewTarget = 0, ///< 뷰 렌더 타깃을 `_width` x `_height` 로 맞춘다(`EditorRenderViews::ensureViewTargetSize`)
        StartPlay,         ///< 플레이어가 조종하는 재생을 시작한다(`_bStartAtCamera` 면 에디터 카메라 위치에서, 멈춤에서 시작할 때만)
        StartSimulate,     ///< 월드만 도는 재생을 시작한다
        Pause,
        Resume, ///< 멈춘 재생을 다시 돌린다(Paused → Playing)
        Step,   ///< `_stepFrameCount` 프레임 진행한다
        Stop,   ///< 재생을 멈추고 선택을 이름으로 편집 씬의 오브젝트에 다시 잇는다
        /**
         * F8 Eject/Possess — 조종 중이면 Simulate 로 바꾸고 씬 뷰를 앞으로, Eject 한 세션이면 다시 조종하고 게임 뷰를 앞으로(`EditorPlayCommands::toggleEject` 가 보낸다).
         * 세션 칸(`_bEjected` · 최대화)과 패널 포커스를 한 명령이 함께 바꾼다 — 따로 보내면 그 사이 프레임이 반쯤 바뀐 상태를 본다.
         */
        ToggleEject,
        Count
    };

    /** @brief UI 명령 하나입니다 — 값만 든다(포인터 없음). 종류마다 쓰는 칸은 `EditorUICommandKind` 에 적었다. */
    struct EditorUICommand
    {
        EditorUICommandKind _kind{ EditorUICommandKind::Count };
        EditorViewKind      _view{ EditorViewKind::Scene };
        uint8               _bStartAtCamera{ SW_FALSE };
        uint32              _width{ 0 };
        uint32              _height{ 0 };
        uint32              _stepFrameCount{ 0 };

        static EditorUICommand makeFitViewTarget( EditorViewKind view, uint32 width, uint32 height );
        static EditorUICommand makeStart( bool bSimulate, bool bStartAtCamera );
        static EditorUICommand makeStep( uint32 frameCount );
        /** @brief 칸이 없는 종류(Pause · Resume · Stop · ToggleEject)입니다. */
        static EditorUICommand make( EditorUICommandKind kind );

        /** @brief 이 명령을 엔진 상태에 적용합니다 — 게임 스레드만 부른다(`EditorUICommandQueue::applyDue`). */
        void apply() const;
    };

    /**
     * @class EditorUICommandQueue
     * @brief UI → 게임 스레드 명령 큐입니다(`EditorContext::getSubsystem<EditorUICommandQueue>()`). 넣기는 어느 스레드에서나, 꺼내기(`takeDue`)는 게임 스레드 하나만 한다.
     */
    class SW_EDITOR_API EditorUICommandQueue final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorUICommandQueue";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 적용하지 않은 명령을 버립니다(모듈 리로드 · 종료 — 적용할 엔진 상태가 내려간다). */
        void shutdownSubsystem() override;

        EditorUICommandQueue();

        /** @brief 명령을 지금 모으는 프레임(`getCollectingFrame`)에 넣습니다 — 다음 `takeDue` 가 꺼냅니다. 큐가 차면 오류를 남기고 false 입니다. */
        bool post( const EditorUICommand& command );
        /** @brief 명령을 프레임 @p frame 에 적용하도록 넣습니다. 그 번호의 `takeDue`(이미 지났으면 다음 `takeDue`)가 넣은 순서대로 꺼냅니다. */
        bool postAt( const EditorUICommand& command, uint32 frame );
        /** @brief 지금 명령을 모으는 프레임 번호입니다(1 부터). 다음 `takeDue` 가 이 번호까지를 꺼내고 하나 올린다. */
        uint32 getCollectingFrame() const { return _collectingFrame.load( std::memory_order_acquire ); }
        /**
         * @brief 게임 틱 시작 — 지금 프레임 번호까지의 명령을 넣은 순서대로 @p outList 에 담고(비운 뒤) 번호를 하나 올립니다. 뒤 번호 칸은 남깁니다.
         * @details 번호를 먼저 올린다 — 올리기 전에 번호를 읽고 꺼낸 뒤에 넣은 명령은 지난 번호라 다음 `takeDue` 가 꺼낸다(잃지 않는다).
         */
        void takeDue( vector<EditorUICommand>& outList );
        /** @brief 게임 틱 시작에 부른다 — `takeDue` 로 이번 프레임 차례의 명령을 꺼내 넣은 순서대로 적용합니다(호스트가 UI 프레임 뒤 · 씬 틱 앞에서 부르는 `ImGuiEditor::applyUICommands`). */
        void applyDue();
        /** @brief @p command 를 지금 에디터 범위의 큐에 넣습니다(UI 쪽 창구). 에디터 컨텍스트가 없으면 아무것도 하지 않습니다(컨텍스트 없이 도는 테스트 · 종료 정리). */
        static void postToEditor( const EditorUICommand& command );
        /** @brief 큐가 차서 버린 명령의 누적 수입니다(테스트 · 감시). */
        uint32 getDroppedCount() const { return _droppedCount.load( std::memory_order_relaxed ); }

    private:
        /** @brief 큐 칸 — 명령과 적용할 프레임 번호입니다. */
        struct QueuedCommand
        {
            EditorUICommand _command{};
            uint32          _frame{ 0 };
        };

        static constexpr uint32 kCapacity = 256; ///< 한 프레임의 UI 명령은 많아야 몇 개다(뷰 둘 · 플레이 단추 하나). 차면 UI 쪽 결함이다

        ConcurrentQueue<QueuedCommand, kCapacity> _queue;
        atomic<uint32>                            _collectingFrame; ///< 지금 명령을 모으는 프레임 번호(`getCollectingFrame`). `takeDue` 가 올린다
        atomic<uint32>                            _droppedCount;
        vector<QueuedCommand>                     _listTaken; ///< 큐에서 꺼낸 칸 — 뒤 번호 칸은 그 프레임의 `takeDue` 까지 넣은 순서대로 남는다(게임 스레드만 만진다)
        vector<EditorUICommand>                   _listDue;   ///< `applyDue` 가 꺼낸 이번 프레임 명령(프레임마다 재사용, 게임 스레드만 만진다)
    };

} // namespace sw::editor
