/**
 * @file FrameViewHistory.h
 * @brief 뷰 하나가 지난 프레임에서 이어받는 텍스처 기록(TAA · 볼륨 구름 · 자동 노출)의 한 형식입니다 — 텍스처, 이번 프레임에 쓰는지, 버리는지를 함께 듭니다.
 * @details 손으로 적으면 기록마다 "지난 프레임이 썼나" · "이번에 버리나" · "새 텍스처인가" 를 따로 들어 하나를 빠뜨리기 쉽다(컷 · 다시 켠 첫 프레임 · 새 텍스처의 쓰레기 값).
 *          언리얼 `FSceneViewState` 의 `FTemporalAAHistory` · 눈 적응 기록, 유니티 HDRP `HDCamera` 의 history RT 자리다.
 *          흐름: 프레임 시작(`beginFrame`) → 이 프레임에 쓰는 패스가 정해지면 `markWritten` → 패스가 `getPrevious` 를 읽고 `getCurrent` 에 쓴다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITextureSlot.h"

namespace sw
{
    class IRHIDevice;
    struct RHITextureDesc;

    /** @brief 기록을 쓰는 패스가 쉰 프레임 뒤의 첫 프레임이 기록을 어떻게 하는지입니다. */
    enum class FrameViewHistoryIdleRule : uint8
    {
        Discard, ///< 버린다 — 화면 그림의 기록은 쉬는 동안 움직인 것의 옛 그림이 남는다(TAA · 볼륨 구름)
        Keep,    ///< 잇는다 — 화면 자리와 무관한 값이다(자동 노출의 EV)
    };

    /** @brief 기록이 든 텍스처 수입니다. */
    enum class FrameViewHistoryLayout : uint8
    {
        Copy,     ///< 하나 — 패스가 출력을 끝에 복사한다. 읽는 것과 쓰는 것이 같은 텍스처다
        PingPong, ///< 둘 — 프레임마다 번갈아 한쪽을 읽고 다른 쪽에 쓴다
    };

    /** @class FrameViewHistory @brief 파일 머리말 참고. 렌더 스레드(셋업)에서 바꾸고 패스 기록 중에는 읽기만 합니다. */
    class SW_API FrameViewHistory
    {
    public:
        FrameViewHistory( FrameViewHistoryLayout layout, FrameViewHistoryIdleRule idleRule )
            : _arrTexture{}
            , _layout{ layout }
            , _idleRule{ idleRule }
            , _writeIndex{ 0 }
            , _bWritten{ SW_FALSE }
            , _bReset{ SW_TRUE }
            , _bHasContent{ SW_FALSE }
        {
        }

        /** @brief 이번 프레임을 시작합니다 — 버릴지(`isReset`)를 정하고 쓰기 표시를 내립니다. @p bViewReset 은 뷰 전체의 버림(처음 · 컷 · 건너뛴 프레임)입니다. */
        void beginFrame( bool bViewReset )
        {
            const bool bIdleReset = _idleRule == FrameViewHistoryIdleRule::Discard && _bWritten == SW_FALSE;
            _bReset               = ( bViewReset || _bHasContent == SW_FALSE || bIdleReset ) ? SW_TRUE : SW_FALSE;
            _bWritten             = SW_FALSE;
        }
        /** @brief 이번 프레임의 패스가 기록을 씁니다(번갈아 쓰는 기록은 쓰는 쪽을 바꿉니다). 프레임에 한 번 부릅니다. */
        void markWritten()
        {
            if ( _layout == FrameViewHistoryLayout::PingPong )
                _writeIndex = _writeIndex == 0 ? 1 : 0;
            _bWritten    = SW_TRUE;
            _bHasContent = SW_TRUE;
        }

        /** @brief 이번 프레임의 패스가 기록을 쓰면 true 입니다. */
        bool isWritten() const { return _bWritten == SW_TRUE; }
        /** @brief 이번 프레임은 지난 기록을 섞지 않습니다(처음 · 컷 · 쉬다 다시 돈 첫 프레임). */
        bool isReset() const { return _bReset == SW_TRUE; }
        /** @brief 지금 텍스처에 한 번이라도 썼으면 true 입니다 — 새 텍스처의 값은 읽지 않는다(NaN 일 수 있다). */
        bool hasContent() const { return _bHasContent == SW_TRUE; }
        /** @brief 텍스처가 모두 있으면 true 입니다. */
        bool isValid() const { return _arrTexture[0].isValid() && ( _layout == FrameViewHistoryLayout::Copy || _arrTexture[1].isValid() ); }
        /** @brief 텍스처를 RW 로도 쓸 수 있으면(만들 때 청했으면) true 입니다. */
        bool isUnorderedAccess() const
        {
            return _arrTexture[0]._uav != kInvalidDescriptorIndex && ( _layout == FrameViewHistoryLayout::Copy || _arrTexture[1]._uav != kInvalidDescriptorIndex );
        }
        /** @brief 이번 프레임에 쓰는(쓰지 않는 프레임은 마지막으로 쓴) 텍스처입니다. */
        const RHITextureSlot& getCurrent() const { return _arrTexture[_writeIndex]; }
        /** @brief 이번 프레임에 읽는 지난 기록입니다. */
        const RHITextureSlot& getPrevious() const { return _arrTexture[_layout == FrameViewHistoryLayout::PingPong ? 1u - _writeIndex : 0u]; }

        /** @brief 텍스처를 @p desc 로 갖춥니다. 하나라도 못 만들면 모두 놓고 false 입니다. 모양이 바뀌어 다시 만들면 내용도 없어집니다. */
        [[nodiscard]] bool ensure( IRHIDevice& device, const RHITextureDesc& desc, const utf8* pDebugName );
        /** @brief 텍스처를 놓고 내용이 없다고 적습니다. 디바이스가 없으면 값만 잊습니다. */
        void release( IRHIDevice* pDevice );

    private:
        RHITextureSlot           _arrTexture[2];
        FrameViewHistoryLayout   _layout;
        FrameViewHistoryIdleRule _idleRule;
        uint8                    _writeIndex; ///< 쓰는 텍스처 — 번갈아 쓰지 않으면 늘 0
        uint8                    _bWritten    : 1;
        uint8                    _bReset      : 1;
        uint8                    _bHasContent : 1;
    };
} // namespace sw
