#include "pch.h"

#include "Engine/Animation/Scene/AnimationSkinSlot.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"

namespace sw
{
    namespace
    {
        /** @brief 프로세스 하나의 칸 번호 목록입니다. 놓은 번호를 먼저 다시 준다. 미리보기 씬 · 테스트가 다른 스레드에서 만들 수 있어 잠근다. */
        struct AnimationSkinSlotRegistry
        {
            SpinLock                 _spinLock;
            vector<uint32> _listFree SW_GUARDED_BY( _spinLock ){};
            uint32 _nextSlot         SW_GUARDED_BY( _spinLock ){ 0 };

            static AnimationSkinSlotRegistry& get()
            {
                static AnimationSkinSlotRegistry s_registry;
                return s_registry;
            }
        };
    } // namespace

    AnimationSkinSlot::AnimationSkinSlot()
    {
        AnimationSkinSlotRegistry& registry = AnimationSkinSlotRegistry::get();
        ScopedLock<SpinLock>       lock{ registry._spinLock };
        if ( registry._listFree.empty() )
        {
            _slot = registry._nextSlot++;
            return;
        }
        _slot = registry._listFree.back();
        registry._listFree.pop_back();
    }

    AnimationSkinSlot::~AnimationSkinSlot()
    {
        AnimationSkinSlotRegistry& registry = AnimationSkinSlotRegistry::get();
        ScopedLock<SpinLock>       lock{ registry._spinLock };
        registry._listFree.push_back( _slot );
    }
} // namespace sw
