#include "pch.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

namespace sw
{
    ModuleSubsystemRegistrar*& ModuleSubsystemRegistrar::getHead()
    {
        // 상수 초기화 머리라 정적 초기화 순서를 타지 않는다 — 다른 이미지의 등록자가 먼저 와도 닿는다(모듈 객체 등록자와 같다).
        static ModuleSubsystemRegistrar* s_pHead = nullptr;
        return s_pHead;
    }

    ModuleSubsystemRegistrar::ModuleSubsystemRegistrar( ModuleSubsystemScope targetScope )
        : _pNext{ getHead() }
        , _targetScope{ targetScope }
    {
        getHead() = this;
    }
} // namespace sw
