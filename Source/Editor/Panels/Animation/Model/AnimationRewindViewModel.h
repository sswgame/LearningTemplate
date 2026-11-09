/**
 * @file AnimationRewindViewModel.h
 * @brief 애니메이션 되감기 창의 ViewModel — 활성 씬의 되감기 기록기에서 기록 상태 · 고른 캐릭터의 그 시각 프레임을 값으로 모으고(`gather`),
 *        기록 켜기 · 창 길이 · 훑기 · 다시 잇기 · 뼈대 그리기 명령을 받습니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 언리얼 Rewind Debugger 에 해당한다. 훑으면 PIE 를 멈추고(`EditorPlaySession::pause`) 씬의 애니메이션을 기록된 시각으로 건다.
 *          기록 · 훑기는 진단이라 되돌리기 · 씬 dirty 에 남지 않는다. 감시 값(`Editor.AnimationRewind*`)도 여기(.cpp)서 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Animation/Scene/AnimationDebugState.h"

namespace sw::editor
{
    /** @brief 창이 그릴 내용의 종류입니다. */
    enum class AnimationRewindContent : uint8
    {
        NoActiveScene = 0, ///< 편집 중인 씬이 없다
        NothingRecorded,   ///< 기록된 트랙이 없다
        NoSelection,       ///< 고른 캐릭터(스켈레탈)의 트랙이 없다
        NoFrame,           ///< 고른 트랙에 보는 시각의 프레임이 없다
        Frame              ///< 고른 트랙의 프레임 값이 있다
    };

    /** @brief 애니메이션 되감기 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct AnimationRewindViewState
    {
        AnimationRewindContent _content{ AnimationRewindContent::NoActiveScene };
        float32                _requestedWindowSeconds{ 0.0f }; ///< 기록 창 길이(초)
        uint32                 _trackCount{ 0 };
        uint64                 _byteCount{ 0 };
        float64                _historySeconds{ 0.0 }; ///< 가장 이른 기록부터 가장 늦은 기록까지(초)
        uint8                  _bRecordingRequested{ SW_FALSE };
        uint8                  _bScrubbing{ SW_FALSE }; ///< 지난 시각을 보는 중
        // 고른 트랙의 보는 시각 프레임(`_content == Frame` 일 때만)
        string              _trackLabel{};
        uint64              _frameIndex{ 0 };
        float64             _frameTime{ 0.0 };
        uint32              _boneCount{ 0 };
        uint32              _poseByteCount{ 0 };
        AnimationDebugState _frameState{};
    };

    /** @brief 애니메이션 되감기 창의 ViewModel 입니다(상태 없음 — 기록기와 플레이 세션이 정본이다). */
    class AnimationRewindViewModel
    {
    public:
        AnimationRewindViewModel();

        /** @brief 창이 그릴 값을 @p outState 에 채웁니다. */
        void gather( AnimationRewindViewState& outState );
        /** @brief 기록을 켜거나 끕니다. */
        void setRecordingRequested( bool bRecord );
        /** @brief 기록 창 길이(초)를 정합니다. */
        void setRequestedWindowSeconds( float32 seconds );
        /** @brief 가장 늦은 기록에서 @p secondsAgo 초 전으로 훑습니다. 플레이 중이면 PIE 를 멈춘다(다음 틱이 새 기록을 쌓아 막대가 밀리지 않게). */
        void scrubTo( float32 secondsAgo );
        /** @brief 훑기를 풀고 멈춘 플레이를 잇습니다. */
        void resume();
        /**
         * @brief 보는 시각의 뼈대(부모 → 자식 선)와 고른 것의 상태 이름을 이번 프레임 디버그 드로우로 냅니다(`debugdraw.category AnimationRewind off` 로 끈다).
         * @param bAllTracks false 면 고른 캐릭터만 그린다.
         */
        void submitSkeletonDraw( bool bAllTracks, const float4& selectedColor, const float4& otherColor );

    private:
        vector<float3> _listScratchPosition; ///< 뼈대 월드 위치 버퍼(프레임마다 다시 쓴다)
    };
} // namespace sw::editor
