/**
 * @file VirtualInputScript.h
 * @brief 프레임 번호에 묶은 이벤트 목록을 가진 가상 입력 원천입니다. "60 프레임에 E 누름, 61 에 뗌" 같은 입력을 적어 둡니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/Virtual/IVirtualInputSource.h"

namespace sw
{
    /** @struct VirtualInputScriptEntry @brief 프레임 하나에 낼 이벤트 하나입니다. */
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
     * @brief 이벤트를 프레임 번호 순서로 보관했다가 그 프레임에 냅니다. 같은 프레임의 이벤트는 더한 순서를 지킵니다.
     */
    class SW_API VirtualInputScript final : public IVirtualInputSource
    {
    public:
        VirtualInputScript();

        /** @brief @p frameIndex 에 이벤트 하나를 더합니다. */
        void addEvent( uint32 frameIndex, const RawInputEvent& rawEvent );
        /**
         * @brief @p frameIndex 에 누르고 @p holdFrameCount 프레임 뒤에 뗍니다(0 이면 같은 프레임에 떼며, 그래도 한 번 눌린 것으로 인정합니다).
         * @return 슬롯의 장치 종류나 번호를 이벤트로 만들 수 없으면 false 이며, 아무것도 더하지 않습니다.
         */
        [[nodiscard]] bool addTap( uint32 frameIndex, const InputSlot& slot, uint32 holdFrameCount = 1 );
        /**
         * @brief 슬롯 하나를 누르거나 뗍니다(키, 마우스 버튼, 패드 버튼). 이벤트로 만들 수 없는 슬롯이면 false 입니다.
         * @details 마우스 버튼은 그 프레임까지(같은 프레임이면 먼저 더한 것까지) 마지막 `addMousePosition` 위치에서 누른다. 없으면 (0, 0) 이다.
         *          OS 의 버튼 이벤트가 커서 위치를 싣는 것과 같다(버튼 이벤트가 커서를 옮긴다).
         */
        [[nodiscard]] bool addSlot( uint32 frameIndex, const InputSlot& slot, bool bDown );
        /** @brief 마우스 이동량(픽셀)을 냅니다. `MouseRawDelta` 이벤트이며 위치는 건드리지 않습니다. */
        void addMouseDelta( uint32 frameIndex, float32 deltaX, float32 deltaY );
        /** @brief 커서를 창 클라이언트 픽셀 (@p x, @p y) 로 옮깁니다. `MouseMove` 이벤트이며 OS 커서 이동과 같고, 이동량도 생깁니다. */
        void addMousePosition( uint32 frameIndex, int32 x, int32 y );
        /**
         * @brief 패드를 가상 연결합니다. 가상 입력 세션의 패드는 끊긴 채 시작하고, 끊긴 패드의 스틱은 입력 맵 · 가상 커서가 읽지 않는다.
         * @details 패드를 쓰는 입력 단계(단추 · 축)는 그 패드를 먼저 연결해야 한다. 같은 연결 이벤트를 여러 번 내도 상태만 다시 정할 뿐이다.
         */
        void addGamepadConnection( uint32 frameIndex, uint8 padIndex = 0 );
        /** @brief 패드 축(스틱, 트리거) 값을 냅니다. 패드는 `addGamepadConnection` 으로 따로 연결한다. */
        void addGamepadAxis( uint32 frameIndex, uint16 axisIndex, float32 value, uint8 padIndex = 0 );
        /**
         * @brief @p firstFrameIndex 이후(포함)의 이벤트를 @p frameCount 프레임 뒤로 밉니다. 이미 낸 이벤트는 그대로입니다.
         * @details 시나리오가 조건을 기다리며 시계를 멈춰 두는 동안(`WaitUntil`) 입력 단계가 시나리오 프레임과 같은 프레임에 나도록 쓴다.
         */
        void delayEventsFrom( uint32 firstFrameIndex, uint32 frameCount );
        /** @brief 이벤트를 모두 지웁니다. */
        void clear();

        void emitFrame( uint32 frameIndex, vector<RawInputEvent>& outListEvent ) override;
        bool isFinished( uint32 frameIndex ) const override;

        uint32 getEntryCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        /** @brief @p frameIndex 까지 더한 마지막 커서 위치(`addMousePosition`)입니다. 없으면 (0, 0) 입니다. */
        int2 findPointerPosition( uint32 frameIndex ) const;

        vector<VirtualInputScriptEntry> _listEntry; ///< 프레임 번호 순(같은 프레임은 더한 순)으로 정렬한 목록입니다. `addEvent` 가 위치를 찾아 넣습니다.
        uint32                          _cursor;    ///< 다음에 낼 항목입니다(프레임 번호가 되돌아가면 처음부터 찾습니다).
    };
} // namespace sw
