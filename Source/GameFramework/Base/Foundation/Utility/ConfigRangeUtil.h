/**
 * @file ConfigRangeUtil.h
 * @brief 키트와 게임 규칙 설정 구조체의 범위 검사입니다. 설정 구조체는 `REFLECT` 로 선언하고 필드마다 `PROPERTY( Min, Max )` 로 범위를 적습니다.
 * @details 범위의 원본은 `PROPERTY` 하나다. 받는 쪽(`initialize`)은 받은 설정을 이 함수로 보고, 범위 밖이면 오류를 남기고 시작을 거절한다.
 *          예전에는 받는 쪽마다 받은 값을 범위 안으로 조용히 맞췄다(`MathUtil::clamp` · `max`). 그러면 지연 20을 준 게임이 8로 돌아도 아무도 몰랐다(폴백 금지 원칙).
 *          필드 사이의 관계(내 번호 < 플레이어 수, 나가는 반경 >= 들어오는 반경)처럼 `PROPERTY` 로 적을 수 없는 규칙만 `initialize` 가 따로 본다.
 *          네트워크 키트에서 시작해(`NetKitConfigUtil`) Base 와 다른 키트도 같이 쓰도록 여기로 옮겼다.
 * @code
 *     const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
 *     if ( rangeError.empty() == false ) { SW_LOG_ERROR( "Kit: refused to start - %#", rangeError.c_str() ); return false; }
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Serialization/Base/SerializerUtil.h"

namespace sw
{
    /** @struct ConfigRangeUtil @brief 설정 구조체의 선언된 범위(`Min` · `Max`)를 봅니다. */
    struct ConfigRangeUtil
    {
        /**
         * @brief @p config 의 필드 가운데 `Min` 과 `Max` 밖인 것을 "필드 = 값 is outside [아래, 위]" 형식으로 이어 붙여 반환합니다. 모두 범위 안이면 빈 문자열입니다.
         * @details @p TConfig 는 `REFLECT` 구조체여야 한다(`StaticType` 이 없으면 컴파일이 멈춘다). 다른 모듈이 부르면 구조체에 그 모듈의 내보내기(`SW_GF_API` · `SW_KIT_API`)가 필요하다.
         */
        template <typename TConfig>
        [[nodiscard]] static string describeRangeErrors( const TConfig& config )
        {
            // 타입 레지스트리가 없는 프로세스(엔진 서비스를 세우지 않는 도구)에서는 타입을 못 찾는다. 역참조로 죽지 말고 거절 사유로 돌려 받는 쪽이 이유를 남기고 멈추게 한다.
            const TypeInfo* const pType = TConfig::StaticType();
            if ( pType == nullptr )
                return string( "the reflection type is not registered in this process (no engine type registry), so the ranges cannot be checked" );
            vector<string> listError;
            SerializerUtil::collectRangeErrors( *pType, ObjectAddress{ &config }, string(), listError );
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
