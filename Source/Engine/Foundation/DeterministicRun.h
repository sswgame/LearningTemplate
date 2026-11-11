/**
 * @file Engine/Foundation/DeterministicRun.h
 * @brief 결정적 실행 정책 — 시간 · 디스크 · 스레드 도착 순서에 달린 런타임 동작을 프레임에 묶을지 정하는 한 묶음입니다. 런타임은 이 정책만 읽습니다.
 * @details 채우는 쪽은 개발 장치다: 자동화 시나리오 세션(`Automation/Runner` — 배포 구성에 없다)이 에셋 도착 · 클립 대기를 켜고, 개발 스위치
 *          (`DevTools/Diagnostics/EnvironmentFreezeSwitch.cpp` 의 `-gv_environmentAnimate=0`)가 환경 시간을 멈춘다. 배포본에는 채우는 쪽이 없어 늘 기본값(모두 꺼짐)이다.
 *          예전에는 런타임 파일마다 테스트용 전역 변수(`gv_finishAssetWorkEachFrame` · `gv_animationWaitForClips` · `gv_environmentAnimate`)를 직접 읽었고,
 *          시나리오 세션이 그 이름을 문자열로 넣었다. 언리얼 자동화의 `FlushAsyncLoading` · `FApp::UseFixedTimeStep`(엔진 전역 한 자리를 테스트 틀이 켠다),
 *          유니티 테스트 프레임워크가 `Time.captureDeltaTime` 으로 시간을 묶는 자리와 같다 — 칸을 늘릴 때는 이 구조체에 한 칸을 더한다(변수마다 setter 를 두지 않는다).
 *          정책은 엔진 서비스 범위의 서브시스템이 든다(예전 함수 정적은 엔진을 다시 세워도 앞 실행의 정책이 남았다). 프레임 고정 시간(`_fixedFrameDelta`)도 여기 있다 —
 *          자동화 실행기 · 시나리오 재생기가 직접 넣고(예전에는 `gv_fixedFrameDelta` 를 이름으로 찾아 넣었다), App 은 읽기만 한다. 사람이 주는 `-gv_fixedFrameDelta` 는
 *          개발 스위치(`DevTools/Diagnostics/FixedFrameDeltaSwitch.cpp`)가 기동 때 옮긴다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /**
     * @struct DeterministicRunPolicy
     * @brief 결정적 실행의 칸들입니다. 기본(생성자)은 모두 꺼짐 — 배포본과 평소 실행의 동작입니다.
     */
    struct SW_API DeterministicRunPolicy
    {
        uint8   _bFinishAssetJobsEachFrame : 1; ///< 프레임 경계마다 걸린 에셋 로드 · IO · 업로드 · PSO · 컴포넌트 등록을 끝낸다(도착이 벽시계 · 워커 순서에 달리지 않게)
        uint8   _bWaitForAnimationClips    : 1; ///< 애니메이터가 시작 때 비동기로 읽는 클립을 `onBeginPlay` 에서 기다린다(첫 포즈가 디스크 지연에 달리지 않게)
        uint8   _bFreezeEnvironmentTime    : 1; ///< 바람 · 파도 시간을 0 에 멈춘다(백엔드끼리 사진 비교 · 결정적 측정)
        float32 _fixedFrameDelta;               ///< 0 보다 크면 프레임마다 벽시계 대신 이 시간(초)을 흘린다(시나리오 · 픽셀 비교 · 재현). 0 이면 벽시계 — 배포본은 늘 0

        DeterministicRunPolicy();
    };

    /**
     * @class DeterministicRun
     * @brief 결정적 실행 정책을 드는 엔진 서비스 서브시스템입니다(`engine::getSubsystem<DeterministicRun>()`). 정책은 게임 스레드에서 프레임 사이에 바꾸고, 어느 스레드에서나 읽습니다(칸 하나를 읽을 뿐이다).
     */
    class SW_API DeterministicRun final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "DeterministicRun";

        DeterministicRun();

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 지금 정책입니다. 아무도 채우지 않았으면(배포본) 기본값입니다. */
        [[nodiscard]] const DeterministicRunPolicy& getPolicy() const { return _policy; }
        /** @brief 정책을 통째로 바꿉니다. 한 칸만 바꾸는 쪽은 `getPolicy` 의 사본을 고쳐 넘긴다. 게임 스레드에서만 부릅니다. */
        void setPolicy( const DeterministicRunPolicy& policy ) { _policy = policy; }

    private:
        DeterministicRunPolicy _policy;
    };

    SW_SERVICE_SLOT( DeterministicRun, Required );
} // namespace sw
