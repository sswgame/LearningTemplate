/**
 * @file CrashContext.h
 * @brief 크래시 시점에 **할당 없이** 읽을 수 있도록 미리 채워 두는 저장소(키-값 · 최근 사건)입니다. 크래시 처리기(`CrashHandler`)가 하나씩 들고 있습니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

namespace sw
{
    /**
     * @struct CrashContextStore
     * @brief 크래시 리포트에 함께 적을 키-값을 고정 버퍼에 들고 있습니다.
     * @details **크래시 시점에는 힙을 믿을 수 없습니다.** 접근 위반이 힙 손상에서 왔다면 그 안에서 하는 할당이 다시 죽습니다.
     *          그래서 값은 미리 고정 크기 문자열에 복사해 두고, 처리기는 읽기만 합니다.
     */
    struct SW_API CrashContextStore
    {
        /** @brief 키-값의 최대 개수입니다. */
        static constexpr uint32 kMaxEntry = 24;
        /** @brief 키 하나의 최대 길이입니다(넘으면 잘립니다). */
        static constexpr uint32 kMaxKeyLength = 32;
        /** @brief 값 하나의 최대 길이입니다(넘으면 잘립니다). 경로 하나가 들어가야 한다(`kLogFileKey`). */
        static constexpr uint32 kMaxValueLength = constant::file::kMaxPathSize;
        /**
         * @brief 이 세션의 로그가 고정 이름 파일(`-log-file`)이면 그 절대 경로를 담는 키입니다. 기본 로그는 세션 접미사(`LOG_…_<세션>.txt`)로 찾으므로 적지 않는다.
         * @details 크래시 묶음(`CrashReportService`)은 다음 실행에서 이 값으로 로그를 찾는다 — 죽은 프로세스의 싱크에 물을 수 없다.
         */
        static constexpr const utf8* kLogFileKey = "LogFile";

        struct Entry
        {
            fixed_string<kMaxKeyLength>   _key;
            fixed_string<kMaxValueLength> _value;
        };

        Entry  _arrEntry[kMaxEntry];
        uint32 _entryCount{ 0 };

        /** @brief 키를 넣거나 덮어씁니다. 공간이 없으면 조용히 버립니다(크래시 경로에서 실패를 키우지 않기 위해서입니다). */
        void set( string_view key, string_view value );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CrashBreadcrumbStore
     * @brief 크래시 직전의 최근 사건(빵부스러기 — 텔레메트리 사건 · 게임 표지)을 고정 버퍼 고리에 들고 있습니다. 크래시 때 할당 없이 파일로 씁니다.
     * @details 쓰는 쪽은 위치를 원자 카운터로 받아 서로 다른 슬롯에 씁니다(한 바퀴 — 32 개 — 안에서 겹치지 않는다). 크래시 경로는 잠그지 않고 읽으므로
     *          그 순간 쓰던 한 줄은 잘려 있을 수 있습니다(진단용이라 받아들인다). Sentry 의 breadcrumbs 에 해당합니다.
     */
    struct SW_API CrashBreadcrumbStore
    {
        static constexpr uint32 kMaxBreadcrumb = 32;
        static constexpr uint32 kMaxLength     = 160;

        fixed_string<kMaxLength> _arrEntry[kMaxBreadcrumb];
        atomic<uint32>           _pushedCount{ 0 }; ///< 지금까지 넣은 수 — 다음 칸은 `% kMaxBreadcrumb`

        /** @brief 한 줄을 넣습니다(길면 잘린다). 가장 오래된 것을 덮습니다. */
        void add( string_view text );
        /** @brief 들어 있는 줄을 오래된 것부터 @p pOutText 에 줄바꿈으로 이어 씁니다. **할당하지 않습니다.** 쓴 길이입니다. */
        uint32 writeText( utf8* pOutText, uint32 capacity ) const;
    };
} // namespace sw
