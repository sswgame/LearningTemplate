/**
 * @file MeleeHitWindow.h
 * @brief 근접 타격 판정의 한 길입니다. 칼날(표본점 줄)을 지난 위치에서 지금 위치까지, 그리고 지금 칼날 자체를 쓸어 걸린 오브젝트를 창 하나에 한 번씩 맞힙니다.
 * @details 근접 판정은 이 스윕 하나다. 애니메이션 알림의 `HitWindow`(소켓 두 개가 칼날)와 킷의 `MeleeHitboxComponent`(오브젝트 기준 두 점이 칼날)가
 *          칼날만 다르게 만들어 같은 `MeleeHitWindowUtil::sweep` 을 부른다. 겹침 이벤트로 따로 맞히던 길은 지웠다 — 두 길이 "이미 맞힘" · 공격자 제외 ·
 *          피해 전달을 따로 들고 있으면 한쪽만 고쳐진다(언리얼의 근접 trace 창과 같은 자리다).
 *          **서버 권위.** 맞음을 전달(`onHitReceived`)하는 것은 권위가 있는 쪽(혼자 하는 게임 · 서버)뿐이다. 클라이언트는 같은 스윕으로 맞음을 예측해
 *          돌려주기만 하고(타격감 · 기록) 피해를 주지 않는다. 권위는 호출하는 쪽이 `MeleeHitAuthority` 로 반드시 넘긴다(기본값이 없다) — 프로세스의
 *          역할에서 읽는 것은 `getProcessAuthority` 한 곳이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
    class GameObject;

    /** @brief 근접 판정의 권위입니다. 맞음을 전달할지 정합니다. */
    enum class MeleeHitAuthority : uint8
    {
        Authority = 0, ///< 혼자 하는 게임이나 서버다. 맞음을 전달한다
        Predicted,     ///< 클라이언트다. 맞음을 찾아 돌려주기만 하고 전달하지 않는다(서버가 정한다)
    };
} // namespace sw

namespace sw
{
    /** @brief 스윕 한 번의 인자입니다. 칼날 모양(반지름)과 맞음의 값(피해, 충격량, 이름)을 담습니다. */
    struct MeleeHitParams
    {
        hashed_string _kind{};                            ///< 맞음의 이름이다(`HitInfo::_kind`, 알림 이름 등)
        float32       _radius{ 0.0f };                    ///< 칼날 두께다. 0 이면 광선이다
        float32       _damage{ 0.0f };                    ///< 기본 피해다
        float32       _impulse{ 0.0f };                   ///< 맞은 바디에 줄 충격량이다
        uint32        _layerMask{ MathUtil::kMaxUInt32 }; ///< 볼 레이어 비트다
        bool          _bIs2D{ false };                    ///< 2D 물리 씬을 쓸면 true 다
    };
} // namespace sw

namespace sw
{
    /** @brief 스윕이 찾은 맞음 하나입니다. */
    struct MeleeHit
    {
        GameObjectHandle _target{};
        HitInfo          _hit{};
    };
} // namespace sw

namespace sw
{
    /** @brief 근접 판정 도우미입니다(전부 static, 게임 스레드에서 틱 밖에서 부릅니다 — 물리 질의와 `onHitReceived` 가 틱 밖의 일입니다). */
    struct SW_API MeleeHitWindowUtil
    {
        /**
         * @brief 이 프로세스의 근접 판정 권위입니다. 네트워크 세션이 클라이언트면 `Predicted`, 아니면(세션 없음 · 서버) `Authority` 입니다.
         * @details 서버와 클라이언트는 다른 프로세스다(에디터의 멀티플레이 실행도 자식 프로세스를 띄운다). 세션 서비스가 묶이지 않은 프로세스(단위 테스트,
         *          도구)는 세션이 없는 것이라 혼자 하는 게임과 같다.
         */
        static MeleeHitAuthority getProcessAuthority();
        /** @brief 두 점 사이 칼날의 표본점(양 끝 포함, @p sampleCount 는 2 이상)을 @p outListPoint 에 채웁니다(지운 뒤). */
        static void makeBlade( const float3& pointA, const float3& pointB, uint32 sampleCount, vector<float3>& outListPoint );
        /**
         * @brief 칼날을 쓸어 새로 맞은 오브젝트를 @p outListHit 끝에 붙이고, @p authority 가 `Authority` 면 맞음을 전달합니다.
         * @details 표본점마다 @p listPrevious → @p listCurrent 토막(개수가 같을 때만 — 창의 첫 프레임은 지난 칼날이 없다)과 지금 칼날(첫 점 → 끝 점)을
         *          `CharacterHitUtil::sweepAll` 로 쓴다. 공격자와 그 아래 오브젝트, @p inoutListHitTarget 에 이미 있는 오브젝트는 건너뛰고, 새로 맞은 것은
         *          거기에 더한다(창 하나에 오브젝트 하나는 한 번). 맞음의 때린 쪽은 @p attacker 이다. 전달은 모든 토막을 쓴 뒤에 한다 — 받는 쪽이 오브젝트를
         *          만들고 지워도 스윕이 보던 목록이 흔들리지 않는다.
         * @return 새로 맞은 오브젝트 수입니다.
         */
        static uint32 sweep( GameObject& attacker, vector_reference<const float3> listPrevious, vector_reference<const float3> listCurrent, const MeleeHitParams& params,
                             MeleeHitAuthority authority, vector<GameObjectHandle>& inoutListHitTarget, vector<MeleeHit>& outListHit );
    };
} // namespace sw
