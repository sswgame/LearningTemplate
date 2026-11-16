#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/SnapshotFaultHook.h"

#include "Core/Registry/RegistrationSlot.h"

namespace sw
{
    namespace
    {
        struct SnapshotFaultHookInternal
        {
            /** @brief 걸린 창구입니다(이미지에 하나 — 둘째는 거절). 함수 정적이라 등록자의 정적 초기화 순서와 무관하다. */
            static SW_DEFINE_REGISTRATION_SLOT( getSlot, ISnapshotFaultHook, "SnapshotFaultHook" )
        };
    } // namespace

    ISnapshotFaultHook* ISnapshotFaultHook::find()
    {
        return SnapshotFaultHookInternal::getSlot().get();
    }

    bool ISnapshotFaultHook::attach( ISnapshotFaultHook* pFaultHook )
    {
        return SnapshotFaultHookInternal::getSlot().setLogged( pFaultHook, "Snapshot fault hook", "SnapshotFaultHook" ) == RegistrationResult::Added;
    }

    void ISnapshotFaultHook::detach( ISnapshotFaultHook* pFaultHook )
    {
        (void)SnapshotFaultHookInternal::getSlot().reset( pFaultHook );
    }
} // namespace sw
