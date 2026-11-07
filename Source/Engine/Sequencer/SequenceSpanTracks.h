/**
 * @file SequenceSpanTracks.h
 * @brief 구간 처리기가 있는 항목 종류(애니메이션 · 소리 · 자막)의 처리기입니다. 종류 표(`SequenceAsset.cpp`)만 가리킨다 — 엔진 안의 이름이라 내보내지 않는다.
 * @details 언리얼 시퀀서의 Skeletal Animation · Audio · (자막은 Audio 섹션의 Subtitle) 트랙 자리다. 처리기는 바깥 시스템을 부를 뿐이고 상태는 `SequenceSpanState` 에 둔다.
 */
#pragma once
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw
{
    /** @brief 애니메이션: 머무는 동안 대상 애니메이터의 시퀀서 슬롯(`SkeletalAnimatorComponent::setSequencerOverride`)에 클립을 구간 시작부터의 시간으로 섞고, 나가면 끈다. */
    extern const SequenceSpanHandler kSequenceAnimationSpanHandler;
    /** @brief 소리: 들어갈 때 대상 에미터(바인딩이 없으면 화면 소리)에서 이벤트를 내고, 나갈 때 그 소리만 멈춘다. */
    extern const SequenceSpanHandler kSequenceAudioSpanHandler;
    /** @brief 자막: 들어갈 때 자막 창구(`ISubtitleSink`)에 줄을 구간 길이만큼 올린다. 창구가 없는 구성(전용 서버 · 테스트)이면 올리지 않는다. */
    extern const SequenceSpanHandler kSequenceSubtitleSpanHandler;
} // namespace sw
