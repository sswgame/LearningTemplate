/**
 * @file SaveReloadGuard.h
 * @brief 우리가 파일을 쓴 직후의 핫 리로드를 "외부 변경" 으로 착각하지 않게 하는 작은 판정기입니다.
 * @details 에셋 캐시(`SharedAssetCache<SocketSet>::getReloadCount`)는 파일을 쓰면 잠시 뒤 같은 파일을 다시 읽고 횟수를 올린다. 그 횟수가 오르면 "누군가 파일을 고쳤다" 로 보여 주려는 창이
 *          자기 저장 때문에 경고를 띄우지 않도록, 저장 직후 일정 시간 안에 오른 횟수 하나를 무시한다. 횟수는 모든 소켓 에셋이 나눠 쓰므로 다른 파일의 다시 읽기와 구별하지는 못한다 —
 *          그 경우는 놓치는 쪽(경고 안 함)으로 기운다. 시간은 호출하는 쪽이 정한다(창의 단조 시계) — 이 클래스는 시계를 모른다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @class SaveReloadGuard
     * @brief 저장 직후 다시 읽기 한 번을 무시하고, 그 밖의 증가는 외부 변경으로 알립니다.
     */
    class SaveReloadGuard
    {
    public:
        /** @brief 저장 직후 이 시간 안의 다시 읽기만 우리 것으로 봅니다(초). 파일 감시는 보통 한 프레임 안팎에 알린다. */
        static constexpr float64 kOwnWriteWindowSeconds = 3.0;

        /** @brief 지금의 다시 읽은 횟수를 기준으로 삼습니다(창을 열 때). 무시 중인 저장은 버립니다. */
        void reset( uint32 reloadCount )
        {
            _lastCount         = reloadCount;
            _ignoreUntilSecond = -1.0;
        }
        /** @brief 파일을 쓴 순간에 부릅니다. @p nowSeconds 는 단조 시계의 지금입니다. */
        void onSaved( uint32 reloadCount, float64 nowSeconds )
        {
            _lastCount         = reloadCount;
            _ignoreUntilSecond = nowSeconds + kOwnWriteWindowSeconds;
        }
        /** @brief 매 프레임 부릅니다. 횟수가 올랐고 우리 저장 몫이 아니면 true 입니다(true 는 한 번만). */
        [[nodiscard]] bool pollExternalChange( uint32 reloadCount, float64 nowSeconds )
        {
            if ( reloadCount == _lastCount )
            {
                if ( _ignoreUntilSecond >= 0.0 && nowSeconds > _ignoreUntilSecond )
                    _ignoreUntilSecond = -1.0; // 다시 읽기가 오지 않았다(캐시에 없던 파일) — 무시 몫을 거둔다
                return false;
            }
            const bool bOwn = _ignoreUntilSecond >= 0.0 && nowSeconds <= _ignoreUntilSecond;
            _lastCount      = reloadCount;
            if ( bOwn )
            {
                _ignoreUntilSecond = -1.0;
                return false;
            }
            return true;
        }

    private:
        uint32  _lastCount{ 0 };
        float64 _ignoreUntilSecond{ -1.0 };
    };
} // namespace sw::editor
