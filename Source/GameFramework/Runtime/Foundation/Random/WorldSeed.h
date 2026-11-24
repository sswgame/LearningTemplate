/**
 * @file WorldSeed.h
 * @brief 규칙 난수의 씨앗입니다 — 클라이언트가 미리 돌려 보면 안 되는 난수(탄 퍼짐 · 다음 안전 구역 · 전리품)의 씨앗을 어디서 얻는지 형식이 정합니다.
 * @details 정해진 값을 서버와 클라이언트가 함께 알면 클라이언트가 서버의 수열을 그대로 돌려 퍼짐을 되받아 조준하거나 다음 구역을 안다(Valve 가 서버 · 클라이언트의 퍼짐 씨앗을 가른 까닭).
 *          - `makeForWorld`: 서버 권위 게임. 넷 링크가 서버인 월드는 운영체제 난수, 혼자 하는 판과 클라이언트(예측한 모습)는 정해진 값이라 되풀이할 수 있다.
 *          - `makeFixed`: 같은 입력이면 같은 결과여야 하는 시뮬레이션(락스텝 · 롤백 · 테스트). 모든 참가자가 같은 씨앗을 나눈다 — 이런 게임은 씨앗을 숨길 수 없다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Component;

    /** @class WorldSeed @brief 씨앗 하나입니다. 파일 머리말의 두 길로만 만듭니다. */
    class SW_GF_API WorldSeed
    {
    public:
        /** @brief @p component 가 사는 월드의 씨앗입니다(서버면 운영체제 난수, 아니면 @p fixedSeed). */
        [[nodiscard]] static WorldSeed makeForWorld( const Component& component, uint32 fixedSeed );
        /** @brief 정해진 씨앗입니다(결정적 시뮬레이션). */
        [[nodiscard]] static WorldSeed makeFixed( uint32 seed ) { return WorldSeed{ seed }; }

        uint32 getValue() const { return _value; }

    private:
        explicit WorldSeed( uint32 value )
            : _value{ value }
        {
        }

    private:
        uint32 _value;
    };
} // namespace sw
