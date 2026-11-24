#include "pch.h"

#include "Core/Concurrency/DiskIOScope.h"

namespace sw
{
    namespace
    {
        /** @brief 이 스레드가 디스크 IO 를 금지한 구간 안인가입니다. */
        thread_local bool t_bDiskIOForbidden{ false };
    } // namespace

    DiskIOForbiddenScope::DiskIOForbiddenScope()
        : _bOuter{ t_bDiskIOForbidden }
    {
        t_bDiskIOForbidden = true;
    }

    DiskIOForbiddenScope::~DiskIOForbiddenScope()
    {
        t_bDiskIOForbidden = _bOuter;
    }

    bool DiskIOForbiddenScope::isActive()
    {
        return t_bDiskIOForbidden;
    }

    DiskIOAllowedScope::DiskIOAllowedScope()
        : _bOuter{ t_bDiskIOForbidden }
    {
        t_bDiskIOForbidden = false;
    }

    DiskIOAllowedScope::~DiskIOAllowedScope()
    {
        t_bDiskIOForbidden = _bOuter;
    }
} // namespace sw
