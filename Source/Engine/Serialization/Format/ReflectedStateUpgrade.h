/**
 * @file ReflectedStateUpgrade.h
 * @brief 손 본문 상태 바이트를 리플렉션 본문(`StateIO::reflected`)으로 올리는 상태 판 올림 단계의 공통 조각입니다.
 * @details 손 `serializeState` 를 `reflected` 로 옮긴 형식은 칸 앞에 머리(타입 판 + 칸 배치 해시)가 붙는다. 판 올림 사슬(`SW_STATE_FORMAT_UPGRADE`)이 있는 형식은
 *          옛 판 바이트에 그 머리를 붙여 세이브를 잃지 않는다(유니티 `FormerlySerializedAs` · 언리얼 `FCustomVersion` 의 판 분기를 등록된 단계로 둔 자리).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    /**
     * @struct ReflectedStateUpgrade
     * @brief `reflected` 의 판 1 머리를 쓰는 단계 함수들입니다.
     */
    struct ReflectedStateUpgrade
    {
        /**
         * @brief @p T 의 `reflected` 판 1 머리(판 1 + 지금 칸 배치 해시)를 씁니다. 등록되지 않은 타입이면 false 입니다(올림이 멈추고 읽기가 실패한다).
         * @details 판 1 이므로 뒤에 타입 판(`REFLECT( Version )`)이 오르면 그 패치 사슬(`SW_TYPE_VERSION_PATCH`)이 이어 올린다.
         */
        template <typename T>
        [[nodiscard]] static bool writeHead( Archive& outNewBody )
        {
            const TypeInfo* pType = engine::getSubsystem<TypeRegistry>().findType( typeFqn<T>() );
            if ( pType == nullptr )
                return false;
            constexpr uint32 kFirstVersion = 1; // 손 본문은 리플렉션 판이 생기기 전이다
            outNewBody << kFirstVersion;
            outNewBody << StateIO::computeReflectedLayoutHash( *pType );
            return true;
        }

        /**
         * @brief 손 본문이 칸을 선언 순서 · 같은 폭(수 · 이름 · uint32 개수의 목록)으로 싣던 형식의 단계입니다 — 옛 바이트 앞에 머리만 붙인다
         *        (`SW_STATE_FORMAT_UPGRADE( …, &ReflectedStateUpgrade::prependHead<T> )`). 열거 폭 · 순서가 다르면 그 형식이 칸을 다시 쓰는 단계를 따로 둔다.
         */
        template <typename T>
        [[nodiscard]] static bool prependHead( Archive& inOldBody, Archive& outNewBody )
        {
            (void)inOldBody; // 읽지 않은 옛 바이트는 `FormatUpgradeStep::runStateStep` 이 그대로 뒤에 붙인다
            return writeHead<T>( outNewBody );
        }
    };
} // namespace sw
