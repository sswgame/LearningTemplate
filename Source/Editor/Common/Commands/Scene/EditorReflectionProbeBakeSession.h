/**
 * @file EditorReflectionProbeBakeSession.h
 * @brief 반사 프로브 베이크를 에디터 프레임에 프로브 단위로 나눠 진행하는 세션입니다(결정 기록 5-5 P4). 커맨드 `lighting.bakeReflectionProbes` 가 시작합니다.
 * @details 라이트 프로브 세션(`EditorLightProbeBakeSession`)과 같은 틀이다 — 돌고 있는가 · 편집 잠금 · 멈출 이유(씬이 바뀜 / 플레이 시작 / 잠금을 지나친 편집) · 프레임 예산 ·
 *          끝난 횟수는 `EditorBakeRunState` 를 함께 쓴다. 프레임마다 예산(`EditorBakeRunState::kFrameBudgetMilliseconds`) 안에서 프로브를 하나씩 굽고, 프로브 하나가 끝나면
 *          그 자리에서 파일을 쓰고 프로브에 넘긴다(앞서 끝난 프로브는 취소해도 남는다). 상태줄 항목(`EditorReflectionProbeBakeStatus.cpp`)이 진행과 Cancel 단추를 보인다.
 *          나누는 단위는 프로브 하나다 — 프로브 하나(면 여섯을 그려 읽고 접고 거르기)는 한 번에 끝낸다(`ReflectionProbeBaker::bakeProbes` 에 프로브 하나를 준다).
 *          프로브 하나가 예산보다 오래 걸리면 그 프레임은 그만큼 길어진다. GPU 읽기는 렌더 스레드를 비우고 이 스레드가 컨텍스트를 잡는 길이라 다른 스레드로 옮기지 않는다.
 *          프로브 안을 더 잘게 나누려면 엔진의 `ReflectionProbeBaker` 가 면 단위로 부를 수 있어야 한다(지금은 프로브마다 baker 를 세우고 내린다).
 *          결과 파일은 씬 파일 옆 `<씬 이름>.<오브젝트 이름(소문자)>.reflprobe` 이고 프로브의 `_probeDataPath` 에 적는다 — 씬을 저장해야 경로가 남으므로 씬을 고친 것으로 표시한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/Scene/EditorBakeRunState.h"
#include "Editor/Common/EditorSubsystem.h"

namespace sw
{
    class Scene;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorReflectionProbeBakeSession
     * @brief 진행 중인 반사 프로브 베이크 하나입니다(파일 머리말). 에디터 서브시스템이라 컨텍스트와 함께 생기고 사라진다(`SW_EDITOR_SUBSYSTEM`).
     */
    class EditorReflectionProbeBakeSession final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorReflectionProbeBakeSession";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        EditorReflectionProbeBakeSession();

        EditorReflectionProbeBakeSession( const EditorReflectionProbeBakeSession& )            = delete;
        EditorReflectionProbeBakeSession& operator=( const EditorReflectionProbeBakeSession& ) = delete;

        /** @brief 활성 씬의 반사 프로브(선택한 오브젝트의 프로브, 없으면 모든 프로브)를 베이크 시작합니다. 프로브가 없으면 바로 끝납니다. 베이크하는 중이면 아무것도 하지 않습니다. */
        void start();
        /** @brief 베이크를 멈춥니다. 다 구운 프로브는 남고 나머지는 버립니다. 알림에 @p pReason 을 적습니다. */
        void cancel( const utf8* pReason );
        /** @brief 이번 프레임의 몫을 베이크합니다(프레임 단계가 부른다). */
        void update();

        bool isRunning() const { return _run.isRunning(); }
        /** @brief 이번 베이크의 프로브 수입니다. */
        uint32 getProbeCount() const { return static_cast<uint32>( _listObjectID.size() ); }
        /** @brief 지금까지 처리한(구웠거나 실패한) 프로브 수입니다. */
        uint32 getDoneProbeCount() const { return _nextProbeIndex; }
        /** @brief 씬 세대 @p sceneGeneration 에서 끝난(취소 포함) 베이크 횟수입니다(탐침 `Editor.ReflectionProbeBakeCount`). 다른 세대의 베이크는 세지 않는다(`EditorBakeRunState`). */
        uint32 getFinishedRunCount( uint64 sceneGeneration ) const { return _run.getFinishedRunCount( sceneGeneration ); }

    private:
        /** @brief 다음 프로브 하나를 굽고 파일로 쓰고 프로브에 넘깁니다. 실패하면 이유를 남깁니다(베이크는 다음 프로브로 계속한다). */
        void bakeNextProbe( Scene& scene );
        void reportFailure( const string& error );
        /** @brief 편집 잠금을 풀고 결과를 알립니다. */
        void finish( bool bCanceled );

        vector<uint64>     _listObjectID; ///< 굽을 프로브의 오브젝트(프레임을 넘어 포인터를 들지 않는다)
        string             _lastError;
        EditorBakeRunState _run; ///< 돌고 있는가 · 편집 잠금 · 멈출 이유 · 끝난 횟수 (라이트 프로브 세션과 같은 틀)
        uint32             _nextProbeIndex;
        uint32             _bakedCount;
        bool               _bDeferFirstSlice; ///< 시작한 프레임의 `update` 는 굽지 않고 넘긴다(상태줄이 먼저 그려지게)
    };
} // namespace sw::editor
