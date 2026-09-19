/**
 * @file IFileWatcher.cpp
 * @brief 세 플랫폼 워처가 공유하는 큐 — 넣기(pushChange)와 꺼내기(pollEvents).
 * @details 플랫폼 파일에는 그 OS 만 아는 것(디렉터리 핸들 · inotify · FSEvents 런루프)만 남기고,
 *          "무엇을 큐에 넣고 어떻게 넘침을 알리는가" 는 여기 하나로 모은다. 이 저장소는 Windows 와
 *          Linux 만 빌드하므로(macOS 는 어디서도 컴파일되지 않는다) 플랫폼 코드가 적을수록 썩을 자리가
 *          줄어든다 — 실제로 macOS 쪽만 연속 중복 접기가 빠져 있었다.
 */
#include "pch.h"

#include "Core/File/IFileWatcher.h"

namespace sw
{
    void IFileWatcher::pushChange( FileWatcherAction action, string_view directory, string_view filename )
    {
        std::scoped_lock<mutex> lock{ _eventMutex };

        if ( _listEventQueue.size() >= _s_kMaxQueuedEvent )
        {
            // 버린다. 다음 폴링이 합성 리스캔 하나로 바꿔 알린다 — 개별 변경은 이미 의미가 없다.
            _bEventQueueOverflowed = true;
            return;
        }

        // 한 번 저장하면 OS 가 같은 파일에 같은 동작을 잇달아 준다(리눅스의 IN_MODIFY 와 IN_CLOSE_WRITE,
        // 윈도우의 연속 FILE_NOTIFY). 둘 다 같은 이벤트로 접히므로 직전 것과 같으면 버린다.
        // 중복 리로드와 큐 압력이 그만큼 준다.
        if ( _listEventQueue.empty() == false )
        {
            const FileChangeEvent& last = _listEventQueue.back();
            // **디렉터리까지 같아야 같은 변경이다.** Windows · Linux 는 감시 루트 하나를
            // `directory` 로 주고 하위 경로를 `filename` 에 담으므로 이름만 봐도 갈렸지만,
            // macOS 는 이벤트마다 그 파일이 있는 디렉터리를 준다 — 서로 다른 폴더의 같은
            // 이름(`config.json` 둘)이 잇달아 오면 뒤엣것이 조용히 사라진다. 여기서 한 번
            // 더 보면 어느 플랫폼이 무엇을 넘기든 답이 맞는다(앞의 둘은 값이 늘 같으므로
            // 동작이 달라지지 않는다).
            if ( last._action == action && last._filename == filename && last._directory == directory )
                return;
        }

        FileChangeEvent changeEvent{};
        changeEvent._action    = action;
        changeEvent._directory = string{ directory };
        changeEvent._filename  = string{ filename };
        _listEventQueue.push_back( std::move( changeEvent ) );
    }

    uint32 IFileWatcher::pollEvents( vector<FileChangeEvent>& outListEvent )
    {
        std::scoped_lock<mutex> lock{ _eventMutex };

        const uint32 count = static_cast<uint32>( _listEventQueue.size() );
        if ( count > 0 )
        {
            outListEvent.insert( outListEvent.end(), _listEventQueue.begin(), _listEventQueue.end() );
            _listEventQueue.clear();
        }

        if ( _bEventQueueOverflowed )
        {
            // 버린 것이 있었다 — 개별 변경은 이미 잃었으므로 "전부 다시 훑어라" 하나로 알린다.
            // 파일 이름이 빈 Modified 가 그 약속이다.
            FileChangeEvent rescanEvent{};
            rescanEvent._action    = FileWatcherAction::Modified;
            rescanEvent._directory = _directoryPath;
            outListEvent.push_back( std::move( rescanEvent ) );
            _bEventQueueOverflowed = false;
            return count + 1;
        }

        return count;
    }
} // namespace sw
