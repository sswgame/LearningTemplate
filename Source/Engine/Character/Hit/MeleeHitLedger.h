/**
 * @file MeleeHitLedger.h
 * @brief 서버가 자기 근접 스윕을 프레임별로 적어 두고, 클라이언트가 예측으로 본 맞음의 주장을 그 기록으로 확인하는 장부입니다(`MeleeHitUnify` M3).
 * @details 클라이언트의 근접 판정은 같은 스윕으로 맞음을 예측해 타격감만 낸다(`MeleeHitAuthority::Predicted`). 서버는 자기 시점의 스윕으로 정하는데,
 *          클라이언트가 지연된 월드를 보고 휘둘렀다면 서버의 칼날이 닿는 순간 표적은 이미 비켜서 있을 수 있다. 그래서 클라이언트가 "내가 휘두른 입력 틱에 이 표적이
 *          칼날에 있었다" 를 주장(`MeleeHitClaim`)하면, 서버는 그 주장을 **자기 스윕 기록**과 맞춰 본다.
 *          - 기록: 권위 있는 `MeleeHitWindowUtil::sweep` 이 칼날 표본점 줄(지난 칼날 → 지금 칼날)과 두께 · 피해를 이 장부에 프레임 번호로 적는다. 장부는 서버가
 *            `ensureFor` 로 켠 씬에만 있다 — 혼자 하는 게임과 클라이언트는 켜지 않으므로 기록하지 않는다.
 *          - 확인: 서버의 폰 루프가 주장의 입력 틱을 그 입력을 처리한 서버 프레임으로 옮겨(`MeleeHitClaim::_frame`), 지연 보상으로 되감은 표적 자리를 함께 넘긴다.
 *            표적 구(중심 · 반지름)가 그 프레임 안팎(`kClaimSlackFrames`)의 칼날 토막에서 칼날 두께 안에 들면 인정한다. 서버가 그 휘두름을 한 적이 없으면(기록 없음)
 *            거절한다 — 클라이언트가 휘두르지 않은 맞음을 지어낼 수 없다.
 *          - 피해: 인정된 주장만 `claimHit` 이 기록한 피해 · 충격량으로 전달한다(창에서 이미 맞힌 오브젝트는 건너뛴다). 그 밖의 판정 · 전달은 스윕 한 길 그대로다.
 *          주장의 전송 형식(`write` · `read`)만 정하고, 어디에 실을지는 게임이 정한다(입력 바이트 안에 싣는다 — 키트의 와이어 형식은 바꾸지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Character/Hit/MeleeHitWindow.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

namespace sw
{
    class BitReader;
    class BitWriter;
    class GameObject;
    class GameObjectManager;

    /** @brief 클라이언트가 예측으로 본 맞음 하나의 주장입니다. 때린 쪽은 연결의 폰이라 싣지 않고, 표적은 게임이 정한 번호로 보냅니다. */
    struct SW_API MeleeHitClaim
    {
        uint32 _frame{ 0 };    ///< 이 휘두름 입력을 서버가 처리한 서버 프레임(`MeleeHitLedger::getFrame` 와 같은 번호)
        uint64 _targetID{ 0 }; ///< 게임이 정한 표적 번호(네트워크 엔티티 번호 등). 서버가 오브젝트로 옮긴다

        void write( BitWriter& writer ) const;
        /** @brief 깨졌거나 모자라면 false 이고 @p outClaim 은 그대로입니다. */
        [[nodiscard]] static bool read( BitReader& reader, MeleeHitClaim& outClaim );
    };
} // namespace sw

namespace sw
{
    /** @brief 주장을 서버 기록과 맞춘 결과입니다. */
    enum class MeleeClaimVerdict : uint8
    {
        Confirmed = 0, ///< 서버 칼날 기록이 표적을 덮는다
        NoSweep,       ///< 그 프레임 안팎에 이 때린 쪽의 스윕 기록이 없다(휘두르지 않았거나 너무 오래됐다)
        OutOfBlade,    ///< 스윕은 있었지만 표적이 칼날 두께 밖이다
    };

    /**
     * @class MeleeHitLedger
     * @brief 씬 하나의 서버 근접 스윕 기록입니다. 서버가 `ensureFor` 로 켜고 `release` 로 끕니다. 파일 머리말 참고.
     */
    class SW_API MeleeHitLedger final : public ISceneFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "MeleeHitLedger";
        /** @brief 기록을 보관하는 프레임 수입니다. 지연 보상이 되감는 범위(`ReplicationServerConfig::_maxViewLagTicks` 의 한도 256)보다 짧게 잡지 않는다. */
        static constexpr uint32 kKeepFrameCount = 256;
        /** @brief 주장의 프레임과 기록의 프레임이 이만큼까지 어긋나도 같은 휘두름으로 본다(입력 틱 → 서버 프레임 옮김의 반올림). */
        static constexpr uint32 kClaimSlackFrames = 2;
        /** @brief 칼날 두께에 더해 주는 여유(미터)입니다. 표적 자리가 지연 보상 표본의 보간이라 생기는 오차다. */
        static constexpr float32 kClaimToleranceMeters = 0.1f;

        MeleeHitLedger();

        /** @brief 매니저에 붙은 장부입니다. 없으면 nullptr 입니다. */
        static MeleeHitLedger* find( const GameObjectManager& manager );
        /** @brief 장부를 켭니다(이미 있으면 그것). 서버가 씬을 시작할 때 부릅니다. */
        static MeleeHitLedger& ensureFor( GameObjectManager& manager );
        /** @brief 장부를 끕니다. 없으면 아무 일도 없습니다. */
        static void release( GameObjectManager& manager );

        void runBeforeTick( GameObjectManager& manager, float32 deltaTime ) override;

        /** @brief 지금 프레임 번호입니다. 갱신마다 하나 오릅니다. 주장의 `_frame` 이 이 번호를 쓴다. */
        uint32 getFrame() const { return _frame; }
        /** @brief 지금 보관한 스윕 기록 수입니다(테스트 · 진단). */
        size_t getRecordCount() const { return _listRecord.size(); }

        /** @brief 권위 있는 스윕 하나를 이번 프레임으로 적습니다. `MeleeHitWindowUtil::sweep` 이 부릅니다. */
        void record( uint64 attackerID, const MeleeHitParams& params, vector_reference<const float3> listPrevious, vector_reference<const float3> listCurrent );

        /**
         * @brief 주장 @p claim 을 서버 기록과 맞춥니다.
         * @param attackerID 때린 쪽 오브젝트 id(연결의 폰)
         * @param targetCenter 표적 중심 — 지연 보상으로 클라이언트가 보던 틱에 되감은 자리
         * @param targetRadius 표적의 반지름(구로 본다)
         * @param pOutParams 인정할 때 그 기록의 값(피해 · 충격량 · 두께)을 받습니다. 필요 없으면 nullptr
         */
        MeleeClaimVerdict verify( uint64 attackerID, const MeleeHitClaim& claim, const float3& targetCenter, float32 targetRadius, MeleeHitParams* pOutParams ) const;

        /**
         * @brief 주장을 확인하고 인정되면 기록된 피해로 @p target 에 맞음을 전달합니다. @p inoutListHitTarget 에 이미 있는 오브젝트는 전달하지 않고(창 하나에 한 번) 인정만 돌려줍니다.
         * @return 인정 결과입니다. `Confirmed` 일 때만 전달이 일어날 수 있습니다.
         */
        MeleeClaimVerdict claimHit( GameObject& attacker, GameObject& target, const MeleeHitClaim& claim, const float3& targetCenter, float32 targetRadius,
                                    vector<GameObjectHandle>& inoutListHitTarget ) const;

    private:
        /** @brief 스윕 하나의 기록입니다. 두 줄이 같은 길이일 때만 지난 칼날 토막을 쓴다(창의 첫 프레임은 지난 칼날이 없다). */
        struct SweepRecord
        {
            vector<float3> _listPrevious{};
            vector<float3> _listCurrent{};
            MeleeHitParams _params{};
            uint64         _attackerID{ 0 };
            uint32         _frame{ 0 };
        };

        /** @brief 점 @p point 에서 선분 @p from → @p to 까지의 거리 제곱입니다. */
        static float32 computeDistanceSquaredToSegment( const float3& point, const float3& from, const float3& to );
        /** @brief 기록 하나의 칼날 토막들이 구 @p center / @p radius 를 덮는지 봅니다(표본점 토막, 칼날 자체). */
        static bool coversSphere( const SweepRecord& record, const float3& center, float32 radius );

        vector<SweepRecord> _listRecord;
        uint32              _frame;
    };
} // namespace sw
