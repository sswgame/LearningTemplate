/**
 * @file LagCompensation.h
 * @brief 랙 보정을 합니다. 서버가 틱마다 맞을 수 있는 몸(위치와 반지름)을 기억해 두고, 쏜 클라이언트가 보던 시각(소수 틱)으로 되감아 명중을 판정합니다.
 * @details "내 화면에서는 맞았는데" 를 서버가 인정하는 방식으로, 밸브의 소스 엔진이 쓴다(언리얼 Lyra 의 서버 되감기와 같은 자리, 기억에 의존).
 *          되감기 한도는 기억하는 틱 수이다. 너무 늦은 사격은 기억하는 가장 오래된 틱으로 판정한다.
 *          몸은 히트박스 바디의 자세 목록이다(`LagBodyPose` — 뼈마다 되감는다). 바디가 없는 몸은 위치 · 반지름 · 높이의 세로 캡슐이다(높이 0 이면 구).
 *          기록은 물리가 끝난 뒤의 실제 자리로 한 시점에 적고, 판정은 기억의 끝이 아니라 **지금 바디의 자리**로 광선을 옮긴다. 기억은 틱 수(생성 인자)로 묶인다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Ray.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 히트박스 바디 하나의 자세입니다(절대 자리). */
    struct LagBodyPose
    {
        float3     _position{};
        quaternion _rotation{};

        /**
         * @brief 과거 자세(이것)의 바디를 쏜 광선 @p ray 를 지금 자세 @p now 의 바디를 쏘는 광선으로 옮깁니다 — 바디에 대한 상대 자세가 같다.
         * @details 뼈마다 옮기므로 자리뿐 아니라 포즈도 되감는다(Valve 랙 보정이 애니메이션 층까지 되감는 자리). 거리는 그대로다.
         */
        SW_KIT_API Ray moveRayTo( const LagBodyPose& now, const Ray& ray ) const;
    };

    /** @brief 한 틱의 몸 하나입니다. */
    struct LagRecord
    {
        float3              _position{}; ///< 구의 중심(`_height` 가 0), 캡슐의 발(`_height` 가 있으면)
        float32             _radius{ 0.5f };
        float32             _height{ 0.0f }; ///< 발에서 캡슐 꼭대기까지. 반지름의 두 배보다 작으면 발 위의 구 하나다. 0 이면 `_position` 이 중심인 구다
        uint32              _entityID{ 0 };
        uint32              _teleportCount{ 0 }; ///< 순간 이동 · 다시 태어남마다 바뀌는 번호. 다른 두 틱 사이는 잇지 않는다
        vector<LagBodyPose> _listBody{};         ///< 히트박스 바디의 자세(에셋 바디 순서). 비었으면 캡슐이 곧 맞음 상자다

        /** @brief 몸 전체(캡슐 또는 바디들)를 덮는 구에 광선이 닿을 수 있는가입니다(먼저 거르기). */
        [[nodiscard]] SW_KIT_API bool mayIntersect( const Ray& ray, float32 maxDistance ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class LagCompensationHistory
     * @brief 서버 틱마다 맞을 수 있는 몸을 기억하고 과거 시각의 표본을 냅니다. 표본 규칙은 클라이언트 화면과 같은 `NetSampleRatio` 하나다.
     */
    class SW_KIT_API LagCompensationHistory
    {
    public:
        explicit LagCompensationHistory( int32 capacity = 32 );

        void record( uint32 tick, const vector<LagRecord>& listRecord );
        /** @brief @p tick(소수)에서의 그 몸을 찾습니다. 앞뒤 틱 사이를 보간하며, 기억 범위 밖이면 가장 가까운 끝 틱을 씁니다. 그 시각에 없던 몸(아직 생기지 않음 · 이미 쓰러짐)이면 false 를 반환합니다. */
        [[nodiscard]] bool sampleAt( float64 tick, uint32 entityID, LagRecord& outRecord ) const;
        /** @brief 지금도 있고 그 시각에도 있던 몸의 표본을 모두 @p outListRecord 에 모읍니다(@p ignoreEntityID 는 뺀다). 쓰러진 몸은 지금 없으므로 들지 않습니다. */
        void collectAt( float64 tick, uint32 ignoreEntityID, vector<LagRecord>& outListRecord ) const;
        /** @brief 광선과 캡슐 · 구 몸의 첫 교차 거리입니다. 닿지 않으면 false 입니다. */
        [[nodiscard]] static bool intersectBody( const LagRecord& body, const float3& origin, const float3& direction, float32& outDistance );
        /** @brief 그 시각의 광선 명중을 판정해 가장 가까운 몸을 반환합니다(@p direction 은 단위 벡터). 맞은 몸이 없으면 0을 반환하고 @p outDistance 는 @p maxDistance 입니다. 광선의 출발점이 몸 안이면 그 몸은 맞지 않습니다. */
        uint32 raycastAt( float64 tick, const float3& origin, const float3& direction, float32 maxDistance, uint32 ignoreEntityID, float32& outDistance ) const;
        /**
         * @brief 그 시각에 두께 @p radius 의 선분(@p start ~ @p end — 근접 칼날)이 닿은 몸의 id 를 모두 @p outListEntityID 뒤에 붙입니다(가까운 차례가 아니다). 선분이 몸 안에서 시작해도 닿은 것입니다.
         * @details 몸은 `raycastAt` 과 같은 되감은 캡슐이다. 선분 · 캡슐 축의 최단 거리로 본다.
         */
        void   collectTouchedAt( float64 tick, const float3& start, const float3& end, float32 radius, uint32 ignoreEntityID, vector<uint32>& outListEntityID ) const;
        uint32 getNewestTick() const { return _listFrame.getNewestTick(); }

    private:
        static const LagRecord* findRecord( const vector<LagRecord>& listRecord, uint32 entityID );

        TickRingBuffer<vector<LagRecord>> _listFrame; ///< 틱마다 맞을 수 있는 몸 목록이다. 가장 새 틱이 기록한 틱 중 가장 큰 것이다
    };
} // namespace sw
