#include "pch.h"

#include "DevTools/DevToolsClientPart.h"

namespace sw
{
    namespace
    {
        struct DevToolsClientPartInternal
        {
            /** @brief 등록된 창구입니다. 함수 안 정적 변수라 등록자의 정적 초기화 순서와 무관하다. */
            static IDevToolsClientPart*& slot()
            {
                static IDevToolsClientPart* s_pPart = nullptr;
                return s_pPart;
            }
        };
    } // namespace

    IDevToolsClientPart* IDevToolsClientPart::find()
    {
        return DevToolsClientPartInternal::slot();
    }

    IDevToolsClientPart::Registration::Registration( IDevToolsClientPart& part )
    {
        SW_ASSERT( DevToolsClientPartInternal::slot() == nullptr ); // 이미지에 하나다 — 둘이면 등록의 잘못이다
        DevToolsClientPartInternal::slot() = &part;
    }
} // namespace sw
