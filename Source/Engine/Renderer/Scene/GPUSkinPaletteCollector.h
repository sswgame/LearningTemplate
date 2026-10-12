/**
 * @file GPUSkinPaletteCollector.h
 * @brief 애니메이션 유닛과 군중 묶음의 스킨 팔레트(본 행렬)와 모프 가중치를 스냅샷으로 옮깁니다.
 * @details `GPUSceneBuilder` 가 값 멤버로 두고 프레임마다 직접 부른다(가상 호출 없음). 애니메이션 쪽 의존(`AnimationSystem`, 군중,
 *          `SkeletalMeshComponent`)은 이 파일에만 있다.
 */
#pragma once
#include "Core/Common/Defines.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct GPUSceneSnapshot;

    class AnimationSystem;

    /**
     * @class GPUSkinPaletteCollector
     * @brief 스킨 팔레트를 스냅샷으로 옮기는 클래스입니다. 게임 스레드 전용입니다.
     */
    class SW_API GPUSkinPaletteCollector
    {
    public:
        /**
         * @brief 애니메이션 유닛의 스킨 팔레트를 스냅샷으로 옮깁니다. 프리미티브 더티와 무관하게 매 프레임 실행합니다.
         * @details 팔레트는 포즈가 바뀔 때마다 바뀌므로 수집 건너뛰기(아무것도 바뀌지 않음) 판단보다 앞에서 따로 돈다. 배열은 프레임마다 새로 만들어
         *          공유한다. RT 가 지난 프레임 배열을 읽는 동안 덮어쓰지 않기 위해서다. 유닛도 지난 팔레트도 없으면 아무것도 하지 않는다.
         */
        void collect( const AnimationSystem& animation, GPUSceneSnapshot& inoutSnapshot );

    private:
        /** @brief 지난 프레임 팔레트 행 수입니다. 이번 프레임 배열을 그만큼 미리 확보합니다(캐릭터 천 명이면 행이 십만 개이므로, 자라면서 옮겨지지 않게 합니다). */
        size_t _paletteRowCountHint{ 0 };
    };
} // namespace sw
