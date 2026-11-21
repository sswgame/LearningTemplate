/**
 * @file WebFileWatcher.cpp
 * @brief 웹의 OS 감시자 자리입니다 — 없다. 브라우저 가상 파일 시스템(MEMFS · IDBFS)에는 바깥 편집기가 쓸 수 없고 변경 알림도 없다.
 * @details 그래서 `createNative` 가 nullptr 로 답하고, 감시를 거는 쪽(`FileWatchDispatcher::initialize`)이 알리고 리로드를 끈다(mtime 폴링으로 대신하지 않는다).
 *          정적 개발 구성(Web-Release — 감사 P-9)이 DevTools 를 담으며 생긴 자리다.
 */
#include "pch.h"

#include "DevTools/FileWatch/FileWatcher.h"

namespace sw
{
    unique_ptr<FileWatcher> FileWatcher::createNative()
    {
        return nullptr;
    }
} // namespace sw
