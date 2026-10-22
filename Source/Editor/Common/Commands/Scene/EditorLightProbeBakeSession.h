/**
 * @file EditorLightProbeBakeSession.h
 * @brief 라이트 프로브 굽기를 에디터 프레임에 나눠 진행하는 세션입니다(IndirectLighting.md P6). 커맨드 `lighting.bakeProbes` 가 시작합니다.
 * @details 세션은 프레임마다 정해진 시간(`kFrameBudgetMilliseconds`) 안에서 프로브를 굽고(`LightProbeBakeJob::step`), 상태줄 항목(`EditorLightProbeBakeStatus.cpp`)이
 *          진행과 Cancel 단추를 보인다. 에디터는 굽는 동안 멈추지 않는다.
 *          레이는 볼륨마다 씬의 정적 콜라이더로 새로 세운 가림 씬(`_pOccluders`, `LightProbeBaker::collectInput`)에 쏜다 — 편집 중에는 플레이 물리 씬이 없다.
 *          굽는 동안 씬 편집을 막는다(`EditorPlaySession::holdSceneEdits` — 인스펙터 · 기즈모 · Undo 가 잠긴다). 굽는 볼륨과 결과를 받을 오브젝트가 그대로여야 하고,
 *          세운 뒤의 편집은 가림 씬에 들지 않기 때문이다. 가림 씬은 굽기의 사본이라 게임 스레드가 갱신하지 않는다(작업 스레드로 옮기는 일은 남음 — IndirectLighting P6).
 *          씬이 바뀌거나(세대) 플레이가 시작되면 굽던 볼륨을 버리고 멈춘다. 볼륨마다 끝나면 파일을 쓰고 볼륨에 넘긴다(앞서 끝난 볼륨은 취소해도 남는다).
 *          결과 파일은 씬 파일 옆 `<씬 이름>.<오브젝트 이름(소문자)>.probes` 이고 볼륨의 `_probeDataPath` 에 적는다 — 씬을 저장해야 경로가 남으므로 씬을 고친 것으로 표시한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorSubsystem.h"

#include "Engine/Renderer/Light/LightProbeBaker.h"

namespace sw
{
    class LightProbeVolumeComponent;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorLightProbeBakeSession
     * @brief 진행 중인 굽기 하나입니다(파일 머리말). 에디터 서브시스템이라 컨텍스트와 함께 생기고 사라진다(`SW_EDITOR_SUBSYSTEM`).
     */
    class EditorLightProbeBakeSession final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorLightProbeBakeSession";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /// @brief 한 프레임에 굽기에 쓰는 시간(ms)입니다. 프레임 하나(16 ms)의 절반이라 에디터가 30 fps 밑으로 떨어지지 않는다.
        static constexpr int64 kFrameBudgetMilliseconds = 8;

        EditorLightProbeBakeSession();
        /** @brief 굽는 중에 사라지면 편집 잠금을 풉니다. */
        ~EditorLightProbeBakeSession() override;

        EditorLightProbeBakeSession( const EditorLightProbeBakeSession& )            = delete;
        EditorLightProbeBakeSession& operator=( const EditorLightProbeBakeSession& ) = delete;

        /** @brief 활성 씬의 볼륨(선택한 오브젝트의 볼륨, 없으면 모든 볼륨)을 굽기 시작합니다. 볼륨이 없으면 바로 끝납니다. 굽는 중이면 아무것도 하지 않습니다. */
        void start();
        /** @brief 굽기를 멈춥니다. 다 구운 볼륨은 남고 굽던 볼륨은 버립니다. 알림에 @p pReason 을 적습니다. */
        void cancel( const utf8* pReason );
        /** @brief 이번 프레임의 몫을 굽습니다(프레임 단계가 부른다). */
        void update();

        bool isRunning() const { return _bRunning; }
        /** @brief 이번 굽기의 볼륨 수입니다. */
        uint32 getVolumeCount() const { return static_cast<uint32>( _listObjectID.size() ); }
        /** @brief 지금 굽는 볼륨의 번호(0 부터)입니다. */
        uint32 getVolumeIndex() const { return _nextVolumeIndex > 0 ? _nextVolumeIndex - 1 : 0; }
        /** @brief 지금 굽는 볼륨의 진행(0 ~ 1)입니다. */
        float32 getVolumeProgress() const;
        /**
         * @brief 씬 세대 @p sceneGeneration 에서 끝난(취소 포함) 굽기 횟수입니다(탐침 `Editor.LightProbeBakeCount`). 다른 세대의 굽기는 세지 않는다.
         * @details 세션은 에디터와 함께 살아 시나리오 체인(`-scenario=a,b`)을 넘어 남는다 — 앞 시나리오의 굽기가 뒤 시나리오의 수에 섞이지 않게 씬마다 센다.
         */
        uint32 getFinishedRunCount( uint64 sceneGeneration ) const { return sceneGeneration == _finishedRunSceneGeneration ? _finishedRunCount : 0u; }

    private:
        /** @brief 다음 볼륨을 찾아 작업을 시작합니다. 볼륨이 사라졌거나 입력이 모자라면 오류를 남기고 false 입니다. 남은 볼륨이 없으면 끝냅니다. */
        bool beginNextVolume( const Scene& scene );
        /** @brief 다 구운 볼륨을 파일로 쓰고 볼륨에 넘깁니다. */
        void completeVolume( const Scene& scene );
        void reportFailure( const string& error );
        /** @brief 편집 잠금을 풀고 결과를 알립니다. */
        void finish( bool bCanceled );

        vector<uint64>              _listObjectID; ///< 굽을 볼륨의 오브젝트(프레임을 넘어 포인터를 들지 않는다)
        unique_ptr<IPhysicsScene3D> _pOccluders;   ///< 굽는 볼륨의 가림 씬 — `_job` 이 빌리므로 `_job` 보다 앞에 둔다(뒤에 사라진다)
        LightProbeBakeJob           _job;
        string                      _lastError;
        uint64                      _sceneGeneration; ///< 시작할 때의 씬 세대 — 바뀌면 멈춘다
        uint32                      _heldEditCount;   ///< 시작할 때의 `EditorPlaySession::getHeldSceneEditCount` — 바뀌면(잠금을 지나친 편집) 멈춘다
        uint32                      _nextVolumeIndex;
        uint32                      _bakedCount;
        uint64                      _finishedRunSceneGeneration; ///< `_finishedRunCount` 를 센 씬 세대
        uint32                      _finishedRunCount;
        bool                        _bRunning;
        bool                        _bJobActive;
    };
} // namespace sw::editor
