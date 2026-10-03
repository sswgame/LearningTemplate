/**
 * @file EditorRegistryDump.h
 * @brief 정적 등록된 에디터 확장(패널 · 팝업 · 인스펙터 · 시각화)과 커맨드 메뉴 배치를 로그로 덤프합니다.
 *
 * @details 확장은 각자 자기 .cpp 에서 정적 등록자로 등록하므로, 등록이 링크에서 빠지거나 순서 키가 바뀌어도 컴파일은 그대로
 *          통과합니다. `-gv_editorRegistryDump=1` 을 주면 기동 때 한 항목에 한 줄을 남기고, `AppSmokeTest` 가 실제 App 을 띄워
 *          그 줄을 기대 목록과 대조합니다. 줄 모양은 `EditorRegistry|<종류>|<id>|...` 로 고정입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /**
     * @struct EditorRegistryDump
     * @brief 에디터 등록부를 로그로 남기는 진단 도구입니다.
     */
    struct EditorRegistryDump
    {
        /** @brief `gv_editorRegistryDump` 가 켜져 있으면 덤프합니다. 패널 · 커맨드를 등록한 뒤에 부릅니다. */
        static void dumpIfRequested();
    };
} // namespace sw::editor
