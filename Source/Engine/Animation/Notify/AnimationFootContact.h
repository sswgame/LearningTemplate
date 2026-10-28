/**
 * @file AnimationFootContact.h
 * @brief 발이 땅에 닿는 순간을 발 본의 높이와 세로 속도로 감지합니다. 알림 테이블의 `<FootContact>` 줄(`AnimationFootContactDef`)과 발마다의 상태(`AnimationFootContactTracker`)입니다.
 * @details 클립에 발소리 알림을 손으로 찍지 않아도 소리가 걸음 주기에 맞는다. 블렌드 스페이스처럼 여러 클립이 섞이거나 재생 속도가 바뀌어도 실제 포즈를 보므로
 *          어긋나지 않는다(언리얼은 발 동기 마커와 알림, 유니티는 Animation Event 를 손으로 찍는다 — 자동 감지는 모션 매칭 계열의 발 닿음 태그와 같은 자리다).
 *          높이는 캐릭터 모델 공간(오브젝트 루트 기준, 루트의 y 가 바닥)의 발 본 y 다. 세로 속도만 보는 이유: 제자리 이동 클립에서 디딘 발은 모델 공간에서
 *          이동 속도만큼 뒤로 미끄러지므로 수평 속도는 닿음을 가리지 못한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @struct AnimationFootContactDef
     * @brief 발 하나의 닿음 감지 데이터입니다. 알림 테이블 `<FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12" max_vertical_speed="0.6"/>` 한 줄입니다.
     * @details 모든 필드가 필수다. `release_height` 는 `contact_height` 보다 커야 한다(그 사이가 떨림을 막는 간격이다).
     *          `notify` 는 같은 테이블의 `<Notify>` 이름이어야 하고, 그 이름의 알림은 감지만 낸다 — 클립에 같은 이름의 알림이 있어도 처리하지 않는다(출처는 하나).
     */
    struct AnimationFootContactDef
    {
        hashed_string _notify{};
        hashed_string _bone{};
        float32       _contactHeight{ 0.0f };    ///< 이 높이(m) 이하로 내려오면 닿을 수 있다
        float32       _releaseHeight{ 0.0f };    ///< 이 높이(m) 위로 올라가야 다음 닿음을 받는다
        float32       _maxVerticalSpeed{ 0.0f }; ///< 세로 속도(m/s)가 이 값 이하일 때 닿았다고 본다
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationFootContactTracker
     * @brief 발 하나의 닿음 상태입니다. 들린 발이 `contact_height` 아래로 내려와 세로 속도가 `max_vertical_speed` 이하가 되는 프레임에 한 번 닿음을 냅니다.
     * @details 닿은 뒤에는 `release_height` 위로 올라가야 다시 낼 수 있다. 첫 프레임은 기록만 하고 내지 않는다(땅에 선 채 시작하면 그 발은 이미 디딘 것이다).
     */
    class SW_API AnimationFootContactTracker
    {
    public:
        AnimationFootContactTracker();

        /** @brief 이번 프레임의 발 높이(m)를 넣습니다. 이번에 닿았으면 true 입니다. @p deltaSeconds 가 0 이하면 아무것도 하지 않습니다. */
        bool update( const AnimationFootContactDef& def, float32 footHeight, float32 deltaSeconds );
        /** @brief 기록을 버립니다(다음 프레임이 첫 프레임이 됩니다). */
        void reset();
        /** @brief 발이 디딘 상태인지 반환합니다. */
        bool isPlanted() const { return _bPlanted == SW_TRUE; }
        /** @brief 발 본을 못 찾았다는 오류를 이미 냈는지 반환합니다(프레임마다 같은 오류를 내지 않게 합니다). */
        bool isMissingBoneReported() const { return _bMissingBoneReported == SW_TRUE; }
        void setMissingBoneReported() { _bMissingBoneReported = SW_TRUE; }

    private:
        float32 _previousHeight;
        uint8   _bHasPrevious;
        uint8   _bPlanted;
        uint8   _bMissingBoneReported;
    };
} // namespace sw
