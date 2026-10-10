/**
 * @file GameSound.h
 * @brief 게임 코드가 소리를 내는 한 줄 창구입니다. 오디오 서비스가 없는 구성(헤드리스 · 시험)에서는 조용히 false · 0 입니다.
 * @details 소리는 이벤트 이름으로 냅니다(`postEvent` · `postEventAt`) — 무슨 클립을 어떻게 낼지는 게임의 이벤트 라이브러리(`*.audioevents.xml`)가 정합니다.
 *          오브젝트에 붙은 소리(따라 움직이는 · 루프)는 `AudioEmitterComponent` 를 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct GameSound
     * @brief `game::getService<IAudioSystem>()` 를 찾고 널을 거르는 일을 게임마다 다시 쓰지 않게 모은 것입니다.
     */
    struct SW_GF_API GameSound
    {
        /** @brief 효과음 클립 하나를 비동기로 냅니다(리소스 경로 — `game/<게임>/sounds/x.ogg`, 버스가 비면 sfx). */
        static bool play( string_view path, const hashed_string& bus = hashed_string{} );
        /** @brief 배경 음악을 바꿉니다. */
        static bool playMusic( string_view path );

        /**
         * @brief 이벤트 라이브러리를 올립니다(이름 = 경로, 같은 경로면 바꿈). 게임 초기화에서 부릅니다.
         * @return 파일을 못 읽거나, 검사 · 그래프 대조를 통과하지 못하면 false 입니다(오디오 서비스가 없으면 true — 할 일이 없다).
         */
        [[nodiscard]] static bool loadEvents( string_view path );
        /** @brief 이벤트 라이브러리를 내립니다(게임 종료 · 모듈 내림). */
        static void unloadEvents( string_view path );
        /** @brief 이벤트를 2D(공간화 없음 — UI · 플레이어 자신의 소리)로 냅니다. */
        static AudioPlayingID postEvent( const hashed_string& eventName );
        /**
         * @brief 이벤트를 월드 자리 하나에서 냅니다(따라 움직이지 않는 원샷 — 폭발 · 착탄 · 죽음).
         * @details 한 번 쓰는 에미터를 만들어 그 자리에 두고, 소리가 끝나면 엔진이 지웁니다.
         */
        static AudioPlayingID postEventAt( const hashed_string& eventName, const float3& position );
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameSoundQueue
     * @brief 틱 안(워커)에서 낼 소리를 모았다가 틱 뒤 게임 스레드에서 쌓은 순서대로 냅니다 — 오디오는 게임 스레드에서만 부릅니다.
     * @details 이름은 포인터만 듭니다 — 리터럴 · 정적 문자열을 넘깁니다. 언리얼은 게임 스레드에서 바로 내지만, 여기 컴포넌트 틱은 워커에서 돈다.
     */
    class SW_GF_API GameSoundQueue
    {
    public:
        GameSoundQueue();

        /** @brief 효과음 클립(리소스 경로 — `GameSound::play`)을 쌓습니다. */
        void queueClip( const utf8* pPath );
        /** @brief 이벤트(2D — `GameSound::postEvent`)를 쌓습니다. */
        void queueEvent( const utf8* pEventName );
        /** @brief 이벤트를 월드 자리 하나에서 내도록(`GameSound::postEventAt`) 쌓습니다. */
        void queueEventAt( const utf8* pEventName, const float3& position );

        /** @brief 쌓인 소리를 쌓은 순서대로 내고 비웁니다. 게임 스레드(틱 밖 · 틱 뒤 큐)에서만 부릅니다. */
        void playAll();
        /** @brief 내지 않고 버립니다(복원 · 걷기). */
        void clear() { _listEntry.clear(); }
        bool isEmpty() const { return _listEntry.empty(); }

    private:
        /** @brief 소리 하나를 내는 길입니다. */
        enum class Kind : uint8
        {
            Clip,
            Event,
            EventAt
        };

        /** @brief 쌓인 소리 하나입니다. */
        struct Entry
        {
            float3      _position{};
            const utf8* _pName{ nullptr };
            Kind        _kind{ Kind::Clip };
        };

        vector<Entry> _listEntry;
    };
} // namespace sw
