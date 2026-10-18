/**
 * @file NetKitConfig.h
 * @brief 네트워크 키트 설정 구조체의 범위 검사입니다. 설정 구조체는 `REFLECT` 로 선언하고 칸마다 `PROPERTY( Min · Max )` 로 범위를 적습니다.
 * @details 범위의 원본은 `PROPERTY` 하나다. 키트의 `initialize` 는 받은 설정을 이 함수로 보고, 범위 밖이면 오류를 남기고 시작을 거절한다.
 *          예전에는 키트마다 받은 값을 범위 안으로 조용히 맞췄다(`MathUtil::clamp` · `max`). 그러면 지연 20을 준 게임이 8로 돌아도 아무도 몰랐다(폴백 금지 원칙).
 *          칸 사이의 관계(내 번호 < 플레이어 수)처럼 `PROPERTY` 로 적을 수 없는 규칙만 `initialize` 가 따로 본다.
 * @code
 *     const string rangeError = NetKitConfigUtil::describeRangeErrors( settings );
 *     if ( rangeError.empty() == false ) { SW_LOG_ERROR( "Kit: refused to start - %#", rangeError.c_str() ); return false; }
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Serialization/Base/SerializerUtil.h"

namespace sw
{
    /** @struct NetKitConfigUtil @brief 키트 설정의 선언된 범위(`Min` · `Max`)를 봅니다. */
    struct NetKitConfigUtil
    {
        /**
         * @brief @p config 의 칸 가운데 `Min` · `Max` 밖인 것을 "칸 = 값 is outside [아래, 위]" 로 이어 붙여 반환합니다. 모두 범위 안이면 빈 글입니다.
         * @details @p TConfig 는 `REFLECT` 구조체여야 한다(`StaticType` 이 없으면 컴파일이 멈춘다). 다른 모듈이 부르면 구조체에 키트 내보내기(`SW_KIT_API`)가 필요하다.
         */
        template <typename TConfig>
        [[nodiscard]] static string describeRangeErrors( const TConfig& config )
        {
            vector<string> listError;
            SerializerUtil::collectRangeErrors( *TConfig::StaticType(), &config, string(), listError );
            string joined;
            for ( const string& rangeError : listError )
            {
                if ( joined.empty() == false )
                    joined += "; ";
                joined += rangeError;
            }
            return joined;
        }
    };
} // namespace sw
