/**
 * @file MailboxKitConfig.h
 * @brief 우편함 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 우편함 키트의 서버 설정입니다. */
    REFLECT()
    struct MailboxKitConfig
    {
        REFLECT_BODY();
        /** @brief 만료 정리 주기(밀리초)입니다. 이 주기의 예약 작업(`mail.sweep`)을 등록해 서버 하나만 정리합니다. */
        PROPERTY( Min = 60000 )
        int32 _sweepIntervalMs{ 60000 };
    };
} // namespace sw
