/**
 * @file EditorPlaySession.h
 * @brief 에디터 플레이(PIE) 세션 및 씬 스냅샷/복구 관리자
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Uuid/Uuid.h"

namespace sw::editor
{
    enum class PlaySessionState : uint8
    {
        Stopped = 0,
        Playing,
        Paused
    };

    /**
     * @struct PlaySessionData
     * @brief 플레이 세션이 들고 있는 상태 전부. **소유는 `EditorContext`** 입니다.
     * @details 예전에는 이것들이 `EditorPlaySession.cpp` 의 파일 정적이었다. 그러면 수명이
     *          아무에게도 속하지 않아, 컨텍스트가 다시 만들어져도 앞 세션의 스냅샷과 재생
     *          상태가 그대로 남는다. 컨텍스트가 들면 컨텍스트와 함께 나고 죽는다.
     */
    struct PlaySessionData
    {
        /** @brief 롤백용 오브젝트 스냅샷 하나. */
        struct ObjectSnapshot
        {
            Uuid          _guid{};
            uint64        _objectId{ 0 };
            string        _name;
            vector<uint8> _bytes;
            string        _xml;
        };

        vector<ObjectSnapshot> _listSnapshot;
        PlaySessionState       _state{ PlaySessionState::Stopped };
        uint8                  _bStepPending : 1;
        uint8                  _bHasSnapshot : 1;
        [[maybe_unused]] uint8 _reserved     : 6;

        PlaySessionData()
            : _listSnapshot{}
            , _state{ PlaySessionState::Stopped }
            , _bStepPending{ SW_FALSE }
            , _bHasSnapshot{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @class EditorPlaySession
     * @brief 에디터 Play-In-Editor (PIE) 시뮬레이션 수명주기 및 씬 롤백을 관리합니다.
     * @details 상태는 `EditorContext` 가 들고 있고(`PlaySessionData`), 이 클래스는 그것을 조작하는
     *          정적 파사드다. 컨텍스트가 없으면 "정지" 로 답한다.
     */
    class EditorPlaySession
    {
    public:
        /** @brief 현재 플레이 세션 상태를 반환합니다. */
        static PlaySessionState getState();
        /** @brief 플레이 중인지 여부를 반환합니다. Step 대기 중이면 true입니다. */
        static bool isPlaying();
        /** @brief 일시정지 상태인지 여부를 반환합니다. */
        static bool isPaused();
        /** @brief 정지(편집 모드) 상태인지 여부를 반환합니다. */
        static bool isStopped();
        /** @brief 이번 프레임에 Step이 예약되어 있는지 반환합니다. */
        static bool hasPendingStep();

        /** @brief 플레이 세션 상태를 변경합니다 (스냅샷 캡처 및 롤백 복구 처리). */
        static void setState( PlaySessionState state );
        /** @brief 시뮬레이션을 시작합니다. */
        static void play() { setState( PlaySessionState::Playing ); }
        /** @brief 시뮬레이션을 일시정지합니다. */
        static void pause() { setState( PlaySessionState::Paused ); }
        /** @brief 시뮬레이션을 정지하고 씬을 롤백 복구합니다. */
        static void stop() { setState( PlaySessionState::Stopped ); }
        /** @brief 한 프레임만 시뮬레이션한 뒤 일시정지합니다. */
        static void stepOnce();
        /** @brief 예약된 Step을 소비하고 일시정지로 되돌립니다. */
        static void consumePendingStep();
    };
} // namespace sw::editor
