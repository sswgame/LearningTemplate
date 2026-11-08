#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettingsOverride.h"

namespace sw
{
    namespace
    {
        struct GameSettingsOverrideInternal
        {
            /** @brief 등록된 창구입니다. 함수 안 정적 변수라 등록자의 정적 초기화 순서와 무관하다. */
            static IGameSettingsOverride*& slot()
            {
                static IGameSettingsOverride* s_pOverride = nullptr;
                return s_pOverride;
            }
        };
    } // namespace

    IGameSettingsOverride* IGameSettingsOverride::find()
    {
        return GameSettingsOverrideInternal::slot();
    }

    IGameSettingsOverride::Registration::Registration( IGameSettingsOverride& settingsOverride )
    {
        SW_ASSERT( GameSettingsOverrideInternal::slot() == nullptr ); // 이미지에 하나다 — 둘이면 등록의 잘못이다
        GameSettingsOverrideInternal::slot() = &settingsOverride;
    }
} // namespace sw
