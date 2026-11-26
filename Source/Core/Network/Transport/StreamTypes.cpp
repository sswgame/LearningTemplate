#include "pch.h"

#include "Core/Network/Transport/StreamTypes.h"

namespace sw
{
    const utf8* toString( StreamCloseReason reason )
    {
        // 열거자 이름 그대로다(표 `StreamCloseReasonList.xxx` 한 벌). 범위 밖 값은 "Unknown" 이다.
        static constexpr const utf8* kArrName[] = {
#define SW_STREAM_CLOSE_REASON( Name ) #Name,
#include "Core/Network/Transport/StreamCloseReasonList.xxx"
#undef SW_STREAM_CLOSE_REASON
        };
        const size_t index = static_cast<size_t>( reason );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "Unknown";
    }
} // namespace sw
