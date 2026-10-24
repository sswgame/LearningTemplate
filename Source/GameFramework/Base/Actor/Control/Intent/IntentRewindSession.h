/**
 * @file IntentRewindSession.h
 * @brief 의도 재생의 되감기 실행입니다 — 키 상태(`IntentRewindBuffer`)로 되돌린 뒤 목표 틱까지 같은 고정 프레임으로 다시 돕니다.
 * @details 세션은 세계의 모양(`IIntentRewindWorld`)만 안다: 키를 찍고, 키로 되돌리고, 고정 틱 하나를 돈다. 장면 하나를 실제로 다루는 짝은
 *          `ControlSceneRewindWorld` 이고, 이 파일은 순서(찾기 → 미래 버리기 → 되돌리기 → 다시 돌기)와 한 프레임에 돌 틱 수의 나눔만 가진다.
 *          설계는 `docs/plans/IntentNetworkWiring.md` 3절이다.
 *
 *          - **키 찍기**: 호스트가 틱 하나를 돌리기 전에 `captureIfDue` 를 부른다. 간격의 배수 틱이고 그 틱의 키가 아직 없을 때만 찍는다(되감은 자리를 다시 돌 때 같은 키를 두 번 넣지 않는다).
 *          - **되감기**: `beginSeek( 목표 )` 가 목표 이하의 가장 가까운 키로 세계를 되돌리고 그 뒤의 키를 버린다(되돌린 자리에서 다시 돌면 미래가 갈라진다).
 *            목표가 지금 틱보다 앞이 아니면 되돌리지 않고 그대로 앞으로 돈다. 키가 가장 오래된 키보다 앞이면 실패한다(상한으로 버려진 범위).
 *          - **나눠 돌기**: `stepSeek( 최대 틱 수 )` 가 목표까지 한 번에 돌 수 있는 만큼만 돌고 남은 틱 수를 돌려준다. 긴 구간은 여러 프레임에 나눠 돌고, 그동안 화면은
 *            멈춘 그림을 둔다. 돌고 있는 동안(`isSeeking`) 호스트는 자기 틱을 돌리지 않는다 — 세계를 이 세션이 돌리기 때문이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Actor/Control/Intent/IntentRewindBuffer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 되감기 세션이 다루는 세계입니다. 장면 · 시뮬레이션 하나가 구현합니다. */
    class IIntentRewindWorld
    {
    public:
        virtual ~IIntentRewindWorld() = default;

        /** @brief 지금까지 돈 틱 수(다음에 돌 틱의 번호)입니다. */
        virtual uint32 getTick() const = 0;
        /** @brief 지금 상태를 키 스냅샷 바이트로 냅니다. 실패하면 false 입니다. */
        [[nodiscard]] virtual bool captureKey( vector<uint8>& outBytes ) = 0;
        /** @brief 틱 @p keyTick 에 찍은 키로 상태를 되돌리고 틱 번호를 @p keyTick 으로 맞춥니다. 같은 고정 프레임으로 다시 돌 때 기록된 의도가 그 틱부터 나와야 합니다. */
        [[nodiscard]] virtual bool restoreKey( uint32 keyTick, const vector<uint8>& bytes ) = 0;
        /** @brief 고정 프레임 틱 하나를 돕니다. 틱 번호가 하나 오릅니다. */
        virtual void stepTick() = 0;

    protected:
        IIntentRewindWorld()                                       = default;
        IIntentRewindWorld( const IIntentRewindWorld& )            = delete;
        IIntentRewindWorld& operator=( const IIntentRewindWorld& ) = delete;
    };
} // namespace sw

namespace sw
{
    /**
     * @class IntentRewindSession
     * @brief 키 상태 버퍼와 되감기 진행 상태를 가진 세션입니다. 게임 스레드에서만 씁니다.
     */
    class SW_GF_API IntentRewindSession
    {
    public:
        IntentRewindSession();

        /** @brief 설정과 세계를 받아 비운 채 시작합니다. 설정이 범위 밖이면 오류를 남기고 false 이며 이전 상태 그대로입니다. 세계는 세션보다 오래 살아야 합니다. */
        [[nodiscard]] bool initialize( const IntentRewindConfig& config, IIntentRewindWorld* pWorld );

        /** @brief 지금 틱이 키를 찍을 차례이고 그 틱의 키가 아직 없으면 찍습니다. 틱 하나를 돌리기 전에 부릅니다. 찍지 못하면 false(오류는 남긴다)입니다. */
        [[nodiscard]] bool captureIfDue();

        /**
         * @brief 틱 @p targetTick 으로 가는 되감기를 시작합니다.
         * @details 목표가 지금 틱보다 앞이면 키로 되돌리고 그 뒤의 키를 버립니다. 키가 없거나(목표가 가장 오래된 키보다 앞) 되돌리지 못하면 오류를 남기고 false 이며
         *          세계는 그대로입니다. 이미 돌고 있었으면 새 목표로 바꿉니다.
         */
        [[nodiscard]] bool beginSeek( uint32 targetTick );
        /**
         * @brief 목표까지 최대 @p maxTickCount 틱을 돕니다(돌기 전마다 `captureIfDue`). 도는 동안 키를 찍지 못해도 시뮬레이션은 멈추지 않습니다.
         * @return 남은 틱 수입니다. 0 이면 목표에 닿았고 `isSeeking` 이 거짓이 됩니다.
         */
        uint32 stepSeek( uint32 maxTickCount );
        /** @brief 되감기를 멈추고 그 자리에 둡니다. */
        void cancelSeek();

        /** @brief 세계의 지금 틱입니다. 세계가 없으면 0 입니다. */
        uint32                    getWorldTick() const { return _pWorld != nullptr ? _pWorld->getTick() : 0; }
        bool                      isSeeking() const { return _bSeeking != SW_FALSE; }
        uint32                    getSeekTarget() const { return _targetTick; }
        uint32                    getRemainingTickCount() const;
        const IntentRewindBuffer& getBuffer() const { return _buffer; }
        /** @brief 되감을 수 있는 가장 이른 틱(가장 오래된 키)입니다. 키가 없으면 false 입니다. */
        [[nodiscard]] bool findEarliestSeekableTick( uint32& outTick ) const;

    private:
        IntentRewindBuffer  _buffer;
        IIntentRewindWorld* _pWorld;
        uint32              _targetTick;
        uint8               _bSeeking;
    };
} // namespace sw
