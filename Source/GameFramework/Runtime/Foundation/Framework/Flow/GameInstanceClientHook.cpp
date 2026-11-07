#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"

namespace sw
{
    namespace
    {
        struct GameInstanceClientHookInternal
        {
            /** @brief 등록된 창구입니다. 함수 안 정적 변수라 등록자의 정적 초기화 순서와 무관하다. */
            static IGameInstanceClientHook*& slot()
            {
                static IGameInstanceClientHook* s_pHook = nullptr;
                return s_pHook;
            }
        };
    } // namespace

    IGameInstanceClientHook* IGameInstanceClientHook::find()
    {
        return GameInstanceClientHookInternal::slot();
    }

    IGameInstanceClientHook::Registration::Registration( IGameInstanceClientHook& hook )
    {
        SW_ASSERT( GameInstanceClientHookInternal::slot() == nullptr ); // 이미지에 하나다 — 둘이면 등록의 잘못이다
        GameInstanceClientHookInternal::slot() = &hook;
    }
} // namespace sw
