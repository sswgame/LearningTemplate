/**
 * @file AnimationSkinSlot.h
 * @brief 스킨 칸 — 스킨 결과를 내는 것(유닛 · 군중 묶음)마다 하나인 오래 가는 번호입니다. GPU 스키닝 결과 · 팔레트 · 인스턴스가 이 번호로 짝을 짓습니다.
 * @details 결과의 정체성을 메시 객체로 두면 포즈가 다른 캐릭터마다 메시 사본이 필요하고, 사본마다 정점 풀 구간 · 배치가 갈린다. 칸이면 메시는 한 벌이고 결과만 칸마다다
 *          (언리얼이 스켈레탈 메시 렌더 데이터 한 벌을 나누고 컴포넌트마다 `FSkeletalMeshObjectGPUSkin` 을 두는 것과 같다). 번호는 프로세스 하나에서 겹치지 않는다 —
 *          씬(매니저)마다 따로 주면 두 씬의 칸이 한 스냅숏에서 겹친다. 놓은 번호는 다음에 다시 준다(표가 칸 수만큼이라 번호를 촘촘히 쓴다).
 *          한 프레임의 스냅숏은 인스턴스 · 팔레트가 같은 프레임의 칸을 실으므로 놓은 번호를 곧바로 다시 주어도 RT 가 섞어 읽지 않는다.
 */
#pragma once
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /**
     * @class AnimationSkinSlotRegistry
     * @brief 칸 번호 목록입니다(엔진 서비스 범위가 소유한다 — 컴포넌트 기본값 객체보다 오래 산다). 놓은 번호를 먼저 다시 준다.
     */
    class SW_API AnimationSkinSlotRegistry final : public ISubsystem
    {
    public:
        SpinLock                 _spinLock;
        vector<uint32> _listFree SW_GUARDED_BY( _spinLock ){};
        uint32 _nextSlot         SW_GUARDED_BY( _spinLock ){ 0 };

        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::Animation; ///< 할당 용도(`ISubsystem::getSubsystemMemoryTag`)
        static constexpr const utf8* kSubsystemName      = "AnimationSkinSlotRegistry";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        SW_SERVICE_SLOT( AnimationSkinSlotRegistry, Required );
    };

    /**
     * @class AnimationSkinSlot
     * @brief 만들 때 칸 번호를 받고 사라질 때 돌려주는 값입니다(복사 · 이동하지 않는다 — 번호는 주인 하나의 것이다).
     */
    class SW_API AnimationSkinSlot
    {
    public:
        /** @brief 칸이 없음을 나타냅니다(`GPUInstance::_skinSlot` 의 무효 값과 같다). */
        static constexpr uint32 kNone = constant::invalid_index::kUint32;

        AnimationSkinSlot();
        ~AnimationSkinSlot();
        AnimationSkinSlot( const AnimationSkinSlot& )            = delete;
        AnimationSkinSlot& operator=( const AnimationSkinSlot& ) = delete;

        /** @brief 칸 번호입니다. */
        uint32 get() const { return _slot; }

    private:
        uint32 _slot;
    };
} // namespace sw
