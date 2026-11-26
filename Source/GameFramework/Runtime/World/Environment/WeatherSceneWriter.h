/**
 * @file WeatherSceneWriter.h
 * @brief 날씨 값을 씬의 빛 · 하늘 · 안개 · 바람과 소리에 씁니다 — 흐리면 해가 어둡고, 폭풍이면 풀 · 천 · 돛이 바람을 더 받고, 비가 오면 빗소리가 납니다.
 * @details 머티리얼 쓰개(`WeatherMaterialWriter`)와 같은 자리 · 같은 까닭이다: 날씨는 판 상태(`GameStateComponent`)가 소유하고 Engine 은 날씨를 모르므로(층 방향) 판 상태가 이 쓰개를 들고 틱마다 민다.
 *          무엇을 어떻게 움직이는지는 날씨 카탈로그의 데이터다(`<SceneBinding>` · `<SoundBinding>`) — 바인딩이 없는 카탈로그는 아무것도 쓰지 않는다.
 *          씬 값은 배율로만 움직인다: 씬에 적은 값 · 시간대(`DaylightComponent`)가 정한 세기 위에 곱하고, 닫을 때 1 로 되돌린다(저장되지 않는다).
 *          소리는 소리 창구(`ISoundPlayer`)로 낸다 — 전용 서버는 창구가 없어 아무것도 내지 않는다. 듣는 자리의 하늘 열림은 주 시점에서 잰다(`WeatherShelter`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Presentation/ISoundPlayer.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class WeatherCatalog;
    class WeatherSystem;
    enum class WeatherSceneTarget : uint8;

    /** @class WeatherSceneWriter @brief 판 상태의 날씨를 카탈로그의 바인딩대로 씬 값과 소리에 씁니다(파일 머리말 참고). 게임 스레드, 틱 밖에서만 씁니다. */
    class SW_GF_API WeatherSceneWriter
    {
    public:
        WeatherSceneWriter();

        WeatherSceneWriter( const WeatherSceneWriter& )            = delete;
        WeatherSceneWriter& operator=( const WeatherSceneWriter& ) = delete;

        /** @brief 카탈로그 @p pCatalog 의 바인딩을 씁니다. 카탈로그가 없거나 바인딩이 없으면 열지 않습니다. 쓰던 씬이 있으면 먼저 `close` 를 부릅니다. */
        void open( const WeatherCatalog* pCatalog );
        /** @brief 날씨 @p weather 의 값으로 씬 배율을 쓰고 소리를 내고 멈춥니다. 열리지 않았으면 아무것도 하지 않습니다. */
        void apply( GameObjectManager& manager, const WeatherSystem& weather );
        /** @brief 씬 배율을 1 로 되돌리고 소리를 멈춥니다. 멱등입니다. @p pManager 가 nullptr 이면 씬 값은 두고 소리만 멈춥니다. */
        void close( GameObjectManager* pManager );
        /** @brief 쓸 바인딩이 있는지입니다. */
        bool isOpen() const { return _pCatalog != nullptr; }

        /** @brief 날씨 @p weather 에서 대상 @p target 의 배율입니다 — 그 대상의 바인딩을 모두 곱한 것(없으면 1)입니다. */
        static float32 computeScale( const WeatherCatalog& catalog, const WeatherSystem& weather, WeatherSceneTarget target );
        /** @brief 이벤트 @p event 를 지금 내고 있는지입니다(소리 창구가 없어도 낼 차례면 true). */
        bool isSoundPlaying( const hashed_string& event ) const;
        /** @brief 마지막으로 잰 듣는 자리의 하늘 열림(0 실내 .. 1 트임)입니다. 소리를 내는 동안만 잰다. */
        float32 getSkyOpenness() const { return _skyOpenness; }

    private:
        /** @brief 소리 바인딩 하나의 상태입니다. */
        struct SoundState
        {
            SoundPlayingID _playingID{ 0 };  ///< 낸 소리의 번호(창구가 없으면 0)
            float32        _written{ 0.0f }; ///< 지난번에 건 날씨 값 — 바뀔 때만 건다
            uint8          _bPlaying{ 0 };   ///< 날씨 값이 0 보다 커 내는 중이다
        };

        /** @brief 대상 @p target 의 배율 @p scale 을 씬의 컴포넌트에 씁니다. */
        static void writeScale( GameObjectManager& manager, WeatherSceneTarget target, float32 scale );
        /** @brief 소리 바인딩마다 내기 · 멈추기 · 파라미터를 맞춥니다. */
        void updateAudio( GameObjectManager& manager, const WeatherSystem& weather );

    private:
        const WeatherCatalog* _pCatalog;       ///< 연 카탈로그(판 상태가 쥔 판 — 판 상태가 이 쓰개보다 오래 산다)
        vector<SoundState>    _listSoundState; ///< 소리 바인딩과 길이가 같다
        float32               _skyOpenness;
        uint8                 _boundTargetMask; ///< 바인딩이 있는 대상의 비트 — 없는 대상은 쓰지 않는다(다른 코드가 쓴 배율을 덮지 않게)
    };
} // namespace sw
