/**
 * @file DestructionDamage.h
 * @brief 파괴 피해 사건(맞은 자리 · 폭발 · 부딪힘)과 그 기록입니다. 사건이 잎마다 변형을 주고(`DestructionState::applyDamage`), 같은 사건열이면 어느
 *        기계에서도 같은 상태가 되므로 네트워크는 변환이 아니라 씨앗과 사건을 보냅니다(`DestructionEventLog`).
 * @details 자리 · 방향은 **메시 공간**(오브젝트 로컬)입니다 — 오브젝트가 어디 놓였든 같은 사건은 같은 조각을 깹니다. 부딪힘 사건은 물리가 만들므로
 *          기계마다 다를 수 있어, 권한을 가진 쪽(서버)이 만든 사건만 기록에 실어 보냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct DestructionProfile;
    struct FractureGraph;

    /** @brief 피해의 종류입니다. 변형 퍼짐은 같고, 런타임이 떨어진 조각에 주는 충격이 다릅니다. */
    enum class DestructionDamageKind : uint8
    {
        Point = 0, ///< 맞은 자리(총알 · 근접) — 반경 0 이면 가장 가까운 잎 하나(또는 `_leafHint`)
        Radial,    ///< 폭발 — 반경 안 거리 감쇠, 떨어진 조각을 바깥으로 민다
        Impact,    ///< 부딪힘 — 물리 접촉 충격량에서(`DestructionDamageUtil::makeImpactEvent`)
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 피해 사건 하나입니다. */
    struct DestructionDamageEvent
    {
        float3                _position{};      ///< 중심(메시 공간)
        float3                _direction{};     ///< 맞은 방향(메시 공간, 단위). 폭발은 쓰지 않는다(바깥쪽)
        float32               _strain{ 0.0f };  ///< 중심의 변형
        float32               _radius{ 0.0f };  ///< 감쇠 반경(미터). 0 이면 잎 하나
        float32               _impulse{ 0.0f }; ///< 떨어진 조각에 줄 충격량(뉴턴초) — 런타임만 쓴다
        int32                 _leafHint{ -1 };  ///< 맞은 잎(광선이 고른 것). 있으면 그 잎은 감쇠 없이 `_strain`
        uint32                _groupId{ 0 };    ///< 이 그룹의 잎만(떨어져 다른 곳에 있는 덩어리를 맞힌 것). 0 이면 모든 잎
        DestructionDamageKind _kind{ DestructionDamageKind::Point };
    };
} // namespace sw

namespace sw
{
    /** @brief 잎 하나가 받은 변형입니다. */
    struct DestructionLeafStrain
    {
        uint32  _leaf{ 0 };
        float32 _strain{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 피해 계산 도우미입니다(전부 static). */
    struct SW_API DestructionDamageUtil
    {
        /**
         * @brief 사건이 잎마다 주는 변형을 @p outListStrain 에 채웁니다(잎 번호 순, 0 은 빼고). 반경 안은 선형 감쇠 `1 - 거리 / 반경`, 반경 0 이면
         *        `_leafHint` 또는 무게 중심이 가장 가까운 잎 하나입니다.
         * @param listLeafGroup 잎마다 그룹 번호. 사건에 `_groupId` 가 있으면 그 그룹의 잎만 봅니다(비면 거르지 않는다).
         */
        static void computeLeafStrain( const FractureGraph& graph, const DestructionDamageEvent& event, vector_reference<const uint32> listLeafGroup,
                                       vector<DestructionLeafStrain>& outListStrain );
        /**
         * @brief 부딪힘 충격량(뉴턴초)으로 사건을 만듭니다. `minImpulse` 이하이면 변형 0 입니다.
         * @param position · normal 메시 공간
         */
        static DestructionDamageEvent makeImpactEvent( const DestructionProfile& profile, const float3& position, const float3& normal, float32 impulse );
    };
} // namespace sw

namespace sw
{
    /** @brief 네트워크 · 리플레이로 보내는 파괴 기록 — 씨앗 + 사건열입니다. 받는 쪽은 같은 순서로 `applyDamage` 합니다. */
    struct SW_API DestructionEventLog
    {
        static constexpr uint32 kVersion = 1;

        vector<DestructionDamageEvent> _listEvent;
        uint64                         _seed{ 0 };

        /** @brief 바이트로 만듭니다(리틀 엔디언). */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 매직 · 버전 · 길이 · 모르는 종류면 false 이고 비웁니다. */
        [[nodiscard]] bool readFromBytes( const uint8* pData, size_t size );
    };
} // namespace sw
