/**
 * @file HorrorSnapshot.h
 * @brief 비대칭 공포의 서버 권위 상태입니다. 발전기와 탈출구 진행, 판자, 창틀, 엔드게임(전원, 해치, 붕괴), 생존자(상태, 갈고리 단계, 시간, 치료, 출혈, 위치),
 *        살인마(위치, 들고 있는 생존자, 기절, 쿨다운)를 담습니다.
 * @details 바이트는 고정 배치형(B, `FixedLayout`)입니다. 코드 생성기가 아래 `PROPERTY` 의 범위 키로 코덱을 만들고, 손 코덱과 손 버전 상수는 없습니다.
 *          범위 밖 정수, 255 개를 넘는 목록은 잘라 쓰지 않고 쓰기가 실패합니다. `Step` 이 붙은 실수만 양자화 손실이 있습니다
 *          (위치 0.01 m 로 +-1000 m 안, 진행량 0.001, 시간 0.01 초). **필드 순서가 와이어 형식입니다.**
 *          장르 키트는 넷 키트를 include 하지 않습니다. 게임이 `HorrorMatch::writeState` 의 바이트를 채널에 싣고,
 *          핸드셰이크 버전에 이 타입의 스키마 해시를 섞습니다(`FixedNetWireVersion::combine< HorrorSnapshot >`). 받는 쪽은 `FixedLayout::read` 로 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorMatch.h"

namespace sw
{
    /** @brief 생존자 하나의 화면에 보이는 상태입니다. */
    REFLECT()
    struct HorrorSurvivorSnapshot
    {
        REFLECT_BODY();
        PROPERTY( Step = 0.01, Min = -1000, Max = 1000 )
        float3 _position{};
        PROPERTY( Step = 0.01, Min = 0, Max = 10000 )
        float32 _hookTimer{ 0.0f };
        PROPERTY( Step = 0.001, Min = 0, Max = 1 )
        float32 _healProgress{ 0.0f };
        PROPERTY( Step = 0.01, Min = 0, Max = 10000 )
        float32 _bleedout{ 0.0f }; ///< 남은 출혈 시간(초)
        PROPERTY( Step = 0.001, Min = 0, Max = 1 )
        float32 _wiggleProgress{ 0.0f };
        PROPERTY( Min = 0, Max = 15 )
        int32 _hookStage{ 0 };
        PROPERTY()
        SurvivorState _state{ SurvivorState::Healthy };
        PROPERTY()
        SurvivorActivity _activity{ SurvivorActivity::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 발전기 하나의 보이는 상태입니다. 진행과 표시를 한 원소에 두어 두 목록의 길이가 어긋나는 모양을 없앱니다. */
    REFLECT()
    struct HorrorGeneratorSnapshot
    {
        REFLECT_BODY();
        PROPERTY( Step = 0.001, Min = 0, Max = 1 )
        float32 _progress{ 0.0f };
        PROPERTY( Min = 0, Max = 7 )
        uint8 _flag{ 0 }; ///< 비트 0 은 완료, 1 은 걷어차임, 2 는 막힘이다
    };
} // namespace sw

namespace sw
{
    /** @brief 한 걸음의 판 전체 상태입니다. 같은 스냅샷은 항상 같은 바이트가 나옵니다(결정적). */
    REFLECT()
    struct HorrorSnapshot
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 255 )
        vector<HorrorSurvivorSnapshot> _listSurvivor{};
        PROPERTY( MaxCount = 255 )
        vector<HorrorGeneratorSnapshot> _listGenerator{};
        PROPERTY( MaxCount = 255, Step = 0.001, Min = 0, Max = 1 )
        vector<float32> _listGateProgress{};
        PROPERTY( MaxCount = 255 )
        vector<PalletState> _listPalletState{};
        PROPERTY( MaxCount = 255, Min = 0, Max = 1 )
        vector<uint8> _listWindowBlocked{};
        PROPERTY( Step = 0.01, Min = -1000, Max = 1000 )
        float3 _killerPosition{};
        PROPERTY( Step = 0.01, Min = 0, Max = 10000 )
        float32 _collapseRemaining{ 0.0f };
        PROPERTY( Step = 0.01, Min = 0, Max = 10000 )
        float32 _killerStun{ 0.0f };
        PROPERTY( Step = 0.01, Min = 0, Max = 10000 )
        float32 _killerCooldown{ 0.0f };
        PROPERTY()
        uint32 _tick{ 0 };
        PROPERTY( Min = -1, Max = 255 )
        int32 _killerCarrying{ -1 };
        PROPERTY()
        MatchPhase _phase{ MatchPhase::Waiting };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bGatesPowered{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bHatchOpen{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bCollapseStarted{ SW_FALSE };
    };
} // namespace sw
