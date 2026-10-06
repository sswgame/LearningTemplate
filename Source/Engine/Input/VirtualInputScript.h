/**
 * @file VirtualInputScript.h
 * @brief 프레임 번호에 묶은 사건 목록 — "60 프레임에 E 누름, 61 에 뗌" 을 적어 두는 가상 입력 원천입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/IVirtualInputSource.h"

namespace sw
{
    /** @struct VirtualInputScriptEntry @brief 프레임 하나에 낼 사건 하나입니다. */
    struct VirtualInputScriptEntry
    {
        uint32        _frameIndex{ 0 };
        RawInputEvent _event{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class VirtualInputScript
     * @brief 사건을 프레임 번호 순서로 들고 있다가 그 프레임에 냅니다. 같은 프레임의 사건은 더한 순서를 지킵니다.
     */
    class SW_API VirtualInputScript final : public IVirtualInputSource
    {
    public:
        VirtualInputScript();

        /** @brief @p frameIndex 에 사건 하나를 더합니다. */
        void addEvent( uint32 frameIndex, const RawInputEvent& rawEvent );
        /**
         * @brief @p frameIndex 에 누르고 @p holdFrameCount 프레임 뒤에 뗍니다(0 이면 같은 프레임에 뗀다 — 그래도 한 번 눌린 것으로 인정).
         * @return 슬롯의 장치 종류 · 번호를 사건으로 만들 수 없으면 false 입니다(아무것도 더하지 않는다).
         */
        [[nodiscard]] bool addTap( uint32 frameIndex, const InputSlot& slot, uint32 holdFrameCount = 1 );
        /** @brief 슬롯 하나를 누르거나 뗍니다(키 · 마우스 버튼 · 패드 버튼). 사건으로 만들 수 없는 슬롯이면 false 입니다. */
        [[nodiscard]] bool addSlot( uint32 frameIndex, const InputSlot& slot, bool bDown );
        /** @brief 마우스 이동량(픽셀)을 냅니다 — `MouseRawDelta`(위치는 건드리지 않는다). */
        void addMouseDelta( uint32 frameIndex, float32 deltaX, float32 deltaY );
        /** @brief 패드 축(스틱 · 트리거) 값을 냅니다. */
        void addGamepadAxis( uint32 frameIndex, uint16 axisIndex, float32 value, uint8 padIndex = 0 );
        /** @brief 사건을 모두 지웁니다. */
        void clear();

        void emitFrame( uint32 frameIndex, vector<RawInputEvent>& outListEvent ) override;
        bool isFinished( uint32 frameIndex ) const override;

        uint32 getEntryCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        vector<VirtualInputScriptEntry> _listEntry; ///< 프레임 번호 순(같은 프레임은 더한 순) — `addEvent` 가 자리를 찾아 넣는다
        uint32                          _cursor;    ///< 다음에 낼 항목(프레임 번호가 되돌아가면 처음부터 찾는다)
    };
} // namespace sw
