#include "pch.h"

#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /** @brief 교체 이미지를 올리는 중첩 깊이입니다. 게임 스레드에서만 만지므로 원자일 필요가 없다. */
        int32 s_reloadScopeDepth = 0;
    } // namespace

    RegistrationReloadScope::RegistrationReloadScope()
    {
        ++s_reloadScopeDepth;
    }

    RegistrationReloadScope::~RegistrationReloadScope()
    {
        --s_reloadScopeDepth;
    }

    bool RegistrationReloadScope::isActive()
    {
        return s_reloadScopeDepth > 0;
    }
} // namespace sw
