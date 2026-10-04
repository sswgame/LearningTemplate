/**
 * @file DebugOverlayState.h
 * @brief 모듈 사이에서 쓰는 디버그 오버레이입니다(게임이 키를 쓰고 에디터가 읽습니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /** @brief 오버레이에 그릴 한 줄입니다(키 · 글로 바꾼 값). */
    struct DebugOverlayRow
    {
        string _key;
        string _value;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 장르에 묶이지 않는 디버그 오버레이 상태입니다.
     * @details float32 게이지 · 짧은 문자열을 hashed_string 키로 기록합니다.
     */
    struct SW_API DebugOverlayState
    {
        unordered_map<hashed_string, float32> _mapFloat;
        unordered_map<hashed_string, string>  _mapString;
        uint8                                 _bVisible : 1;
        [[maybe_unused]] uint8                _reserved : 7;

        DebugOverlayState()
            : _mapFloat{}
            , _mapString{}
            , _bVisible{ SW_TRUE }
            , _reserved{ 0 } {}

        /** @brief float32 값을 넣습니다. */
        void setFloat( hashed_string key, float32 value );
        /** @brief float32 값을 읽습니다. */
        float32 getFloat( hashed_string key, float32 defaultValue = 0.0f ) const;
        /** @brief 문자열 값을 넣습니다. */
        void setString( hashed_string key, string_view value );
        /** @brief 문자열 값을 읽습니다. */
        string getString( hashed_string key ) const;
        /** @brief 키 하나를 지웁니다(float · 문자열 모두). */
        void remove( hashed_string key );
        /** @brief 비웁니다. */
        void clear();
        /**
         * @brief 그릴 줄을 키 사전순으로 채웁니다. float 은 소수 둘째 자리까지, 빈 문자열 값은 뺍니다.
         * @details 에디터 Game View 가 이 줄을 캔버스 왼쪽 위에 그립니다. 판정이 여기 있어 ImGui 없이 시험합니다.
         */
        void collectRows( vector<DebugOverlayRow>& outListRow ) const;
    };
} // namespace sw
