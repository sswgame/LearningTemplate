/**
 * @file AppearanceLoadReport.h
 * @brief 외형 데이터를 읽고 검사하며 나온 오류를 모은 보고서입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/formatString.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/CustomizationValueSet.h"

namespace sw
{
    /**
     * @class AppearanceLoadReport
     * @brief 외형 데이터를 읽고 검사하면서 나온 오류를 모은 것입니다. 오류마다 경고 로그도 남기며, `ResourceDataSchemaTest` 가 그 로그를 확인합니다.
     * @details 편집 창은 이 목록을 그대로 보여 준다. 규칙 충돌과 알 수 없는 이름이 여기에 들어 있다.
     */
    class SW_GF_API AppearanceLoadReport
    {
    public:
        /**
         * @brief 사건 id 가 붙은 오류를 더합니다. id 없는 형태는 없다. 경고 로그에도 같은 id 가 붙는다(`SW_LOG_WARNING_ID` 와 같다).
         * @param pID `"영역.사건"` 꼴의 글자 상수입니다. 같은 사건은 같은 id 를 쓴다.
         */
        template <typename... Args>
        void addErrorWithID( const utf8* pID, string_view format, Args&&... args )
        {
            utf8 arrBuffer[constant::buffer::kMax1024];
            FormatString::formatstring( arrBuffer, constant::buffer::kMax1024, format, std::forward<Args>( args )... );
            addMessage( pID, string_view( arrBuffer ) );
        }
        void clear()
        {
            _listError.clear();
            _listErrorID.clear();
        }

        bool                  hasErrors() const { return _listError.empty() == false; }
        const vector<string>& getErrors() const { return _listError; }
        /** @brief 사건 id 가 @p pID 인 오류의 수입니다. 테스트에 씁니다(문구가 바뀌어도 지지 않는다). */
        uint32 countWithID( const utf8* pID ) const;
        /** @brief 오류를 줄바꿈으로 연결한 문자열입니다. 테스트 메시지에 씁니다. */
        string joined() const;

    private:
        void addMessage( const utf8* pID, string_view message );

    private:
        vector<string>      _listError{};
        vector<const utf8*> _listErrorID{};
    };
} // namespace sw
