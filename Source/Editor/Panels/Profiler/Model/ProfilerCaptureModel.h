/**
 * @file ProfilerCaptureModel.h
 * @brief 프로파일러 패널 구간 탭(CPU · GPU · Counters)의 수집 상태와 명령입니다 — 수집 켜기, 정지 · 재개, 스파이크에서 멈춤, 캡처 저장 · 불러오기, 프레임 고르기(ImGui 없음).
 * @details 그리기는 `ProfilerPanel` 이 하고, 단추는 이 클래스의 명령만 부른다. 유니티 Profiler 의 Record 끄기 · 프레임 선택, 언리얼 stat hitches · Save/Load 에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"

#include "Editor/Panels/Profiler/Model/ProfilerScopeHistory.h"

namespace sw
{
    class FrameProfiler;
} // namespace sw

namespace sw::editor
{
    /**
     * @class ProfilerCaptureModel
     * @brief 최근 N 프레임의 구간 기록(`ProfilerScopeHistory`)과 그것을 다루는 명령을 가집니다.
     */
    class ProfilerCaptureModel
    {
    public:
        /** @brief 수집을 켠 채(멈추지 않고) 시작합니다. */
        ProfilerCaptureModel();

        /** @brief 패널이 열려 있는 프레임마다 부릅니다. 수집이 켜져 있으면 엔진 프로파일러를 켜고, 멈추지 않았으면 한 칸 담은 뒤 스파이크를 봅니다. */
        void tick( FrameProfiler& profiler );

        /** @brief 엔진 프로파일러 수집을 켜거나 끕니다. */
        void setCollect( FrameProfiler& profiler, bool bCollect );
        /** @brief 수집이 켜져 있으면 true 입니다. */
        bool isCollecting() const { return _bCollect == SW_TRUE; }
        /** @brief 기록을 비우고 가장 최근 프레임을 고릅니다. */
        void clear();
        /** @brief 수집을 멈춥니다. 보던 프레임은 그대로입니다. */
        void pause() { _bCapturePaused = SW_TRUE; }
        /** @brief 수집을 다시 시작하고 가장 최근 프레임을 고릅니다. */
        void resume();
        /** @brief 수집을 멈췄으면 true 입니다. 스파이크를 잡았거나 캡처 파일을 열었을 때 멈춥니다. */
        bool isPaused() const { return _bCapturePaused == SW_TRUE; }

        /** @brief GT.Frame 이 임계값을 넘으면 멈출지 정합니다. */
        void setPauseOnSpike( bool bPauseOnSpike ) { _bPauseOnSpike = bPauseOnSpike ? SW_TRUE : SW_FALSE; }
        bool isPauseOnSpike() const { return _bPauseOnSpike == SW_TRUE; }
        /** @brief 스파이크 임계값(GT.Frame, ms)입니다. 입력란이 직접 고칩니다. */
        float32& editSpikeThresholdMs() { return _spikeThresholdMs; }

        /** @brief 캡처를 `getCaptureFilePath` 에 씁니다. 결과를 로그에 남기고 성공하면 true 입니다. */
        bool saveCapture() const;
        /** @brief 캡처 파일을 읽습니다. 성공하면 새 프레임이 덮어쓰지 않게 수집을 멈추고 true 입니다. */
        bool loadCapture();
        /** @brief 캡처 파일 경로(`Saved/Profiler/ProfilerCapture.txt`)입니다. */
        static string getCaptureFilePath();

        /** @brief 테이블의 "Last" 열이 보여 줄 프레임입니다. 0 이 가장 최근입니다. */
        uint32 getSelectedFrameOffset() const { return _rowQuery._frameOffset; }
        /** @brief 그래프에서 고른 프레임을 정합니다. */
        void selectFrameOffset( uint32 frameOffset ) { _rowQuery._frameOffset = frameOffset; }
        /** @brief 테이블 정렬을 정합니다(헤더를 누른 결과). */
        void setSort( ProfilerSortColumn column, bool bDescending );
        /** @brief 종류 @p kind 와 검색어 @p filterText 로 테이블 행을 만듭니다. */
        void makeRows( ProfilerScopeKind kind, string_view filterText, vector<ProfilerScopeRow>& outListRow );

        /** @brief 구간 기록입니다(그래프 값 · 창 크기 · 프레임 수를 읽는다). */
        const ProfilerScopeHistory& getHistory() const { return _scopeHistory; }

    private:
        /** @brief 새로 수집한 프레임의 GT.Frame 이 임계값을 넘었으면 수집을 멈추고 플레이를 일시정지합니다(Pause on spike). */
        void pauseOnSpike();

    private:
        ProfilerScopeHistory _scopeHistory;              ///< 최근 N 프레임을 담는 링 버퍼
        ProfilerRowQuery     _rowQuery;                  ///< 검색어 · 정렬 · 고른 프레임(종류는 탭이 정한다)
        float32              _spikeThresholdMs{ 33.3f }; ///< Pause on spike 임계값(GT.Frame, ms)
        uint8                _bCollect       : 1;        ///< 엔진 프로파일러를 켜 두고 수집한다
        uint8                _bPauseOnSpike  : 1;        ///< GT.Frame 이 임계값을 넘으면 멈춘다
        uint8                _bCapturePaused : 1;        ///< 수집을 멈췄다
    };
} // namespace sw::editor
