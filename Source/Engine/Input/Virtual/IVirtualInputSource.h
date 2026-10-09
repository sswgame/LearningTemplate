/**
 * @file IVirtualInputSource.h
 * @brief 가상 입력 원천 — 프레임 번호마다 장치 사건(`RawInputEvent`)을 내는 쪽의 계약입니다(시나리오 · 입력 리플레이 · 시험).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Input/RawInputEvent.h"

namespace sw
{
    /** @brief 가상 입력이 붙어 있는 동안 OS 입력을 어떻게 하는가입니다. */
    enum class VirtualInputMode : uint8
    {
        Exclusive = 0, ///< OS 키 · 마우스 · 패드 사건, 패드 폴링, 창 포커스 사건, 커서 가두기를 무시한다(시험 · 시나리오 기본)
        Mixed,         ///< OS 입력과 함께 적용한다(사람이 보는 데모 · 진짜 OS 창 상태를 보는 시나리오) — 결정적이지 않다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IVirtualInputSource
     * @brief `InputManager::attachVirtualInput` 이 붙이는 사건 원천입니다. `beginFrame` 이 프레임마다 한 번 `emitFrame` 을 부릅니다(게임 스레드).
     * @details 프레임 번호는 붙인 뒤 `beginFrame` 횟수(0 부터)입니다 — 벽시계와 무관하므로 같은 원천은 같은 프레임에 같은 사건을 냅니다.
     *          낸 사건은 그 프레임의 OS 사건 **뒤에** 낸 순서대로 적용됩니다(같은 프레임의 누름 + 뗌은 "눌렸었다" 로 인정 — OS 사건과 같은 규칙).
     */
    class SW_API IVirtualInputSource
    {
    public:
        virtual ~IVirtualInputSource() = default;

        /** @brief @p frameIndex 프레임에 적용할 사건을 @p outListEvent 끝에 더합니다. `_bSynthetic` 은 부르는 쪽이 켭니다. */
        virtual void emitFrame( uint32 frameIndex, vector<RawInputEvent>& outListEvent ) = 0;
        /** @brief @p frameIndex 부터 더 낼 사건이 없으면 true 입니다. */
        virtual bool isFinished( uint32 frameIndex ) const = 0;

    protected:
        IVirtualInputSource()                                        = default;
        IVirtualInputSource( const IVirtualInputSource& )            = default;
        IVirtualInputSource& operator=( const IVirtualInputSource& ) = default;
    };
} // namespace sw
