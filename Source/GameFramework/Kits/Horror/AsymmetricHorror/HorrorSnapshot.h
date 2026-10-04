/**
 * @file HorrorSnapshot.h
 * @brief 비대칭 공포의 서버 권위 상태를 바이트로 — 발전기 · 탈출구 진행, 판자 · 창틀, 엔드게임(전원 · 해치 · 붕괴), 생존자(상태 · 갈고리 단계 · 시간 · 치료 · 출혈 · 위치),
 *        살인마(위치 · 들고 있는 생존자 · 기절 · 쿨다운)입니다.
 * @details 장르 키트는 넷 키트를 include 하지 않습니다 — 게임이 이 바이트를 `GF_NetClientServer` 의 스냅샷에 싣습니다(Core 의 `BitWriter`,
 *          위치 0.01 m · 진행량 1/1023 · 시간 0.01 초 양자화). 읽기는 깨진 바이트를 받으면 false 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Horror/AsymmetricHorror/HorrorMatch.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 생존자 하나의 보이는 상태입니다. */
    struct HorrorSurvivorSnapshot
    {
        float3           _position{};
        float32          _hookTimer{ 0.0f };
        float32          _healProgress{ 0.0f };
        float32          _bleedout{ 0.0f }; ///< 남은 출혈 초
        float32          _wiggleProgress{ 0.0f };
        int32            _hookStage{ 0 };
        SurvivorState    _state{ SurvivorState::Healthy };
        SurvivorActivity _activity{ SurvivorActivity::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 걸음의 판 전체입니다. */
    struct HorrorSnapshot
    {
        vector<HorrorSurvivorSnapshot> _listSurvivor{};
        vector<float32>                _listGeneratorProgress{};
        vector<uint8>                  _listGeneratorFlag{}; ///< 비트 0 완료 · 1 걷어차임 · 2 막힘
        vector<float32>                _listGateProgress{};
        vector<PalletState>            _listPalletState{};
        vector<uint8>                  _listWindowBlocked{};
        float3                         _killerPosition{};
        float32                        _collapseRemaining{ 0.0f };
        float32                        _killerStun{ 0.0f };
        float32                        _killerCooldown{ 0.0f };
        uint32                         _tick{ 0 };
        int32                          _killerCarrying{ -1 };
        MatchPhase                     _phase{ MatchPhase::Waiting };
        uint8                          _bGatesPowered{ SW_FALSE };
        uint8                          _bHatchOpen{ SW_FALSE };
        uint8                          _bCollapseStarted{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct HorrorSnapshotCodec
     * @brief 스냅샷 ↔ 비트입니다. 같은 스냅샷은 늘 같은 바이트입니다(결정적). 위치는 ±1000 m 안이어야 합니다.
     */
    struct SW_GF_API HorrorSnapshotCodec
    {
        static void write( const HorrorSnapshot& snapshot, BitWriter& outWriter );
        /** @brief 읽습니다. 바이트가 모자라거나 머리가 다르면 false 이고 @p outSnapshot 은 믿을 수 없습니다. */
        [[nodiscard]] static bool read( BitReader& reader, HorrorSnapshot& outSnapshot );
    };
} // namespace sw
