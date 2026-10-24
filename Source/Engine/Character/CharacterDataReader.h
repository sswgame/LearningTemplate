/**
 * @file CharacterDataReader.h
 * @brief 캐릭터 데이터 XML(소켓, 레퍼런스 포즈, 체형, 피팅 테이블, 표면 채널)을 읽는 공통 도우미입니다. 모르는 이름은 모두 **오류**로 모읍니다.
 * @details 데이터를 사람이 직접 고치므로, 틀린 이름이 조용히 기본값이 되지 않게 한다. 모르는 속성이나 원소, 숫자가 아닌 값이 오류이고,
 *          읽기가 끝나면 로더가 오류를 한꺼번에 로그로 내고 실패를 반환한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    enum class XMLRootStatus : uint8;

    struct float3;
    struct quaternion;

    class XMLDocument;
    class XMLNode;

    /**
     * @brief 파일 하나를 읽는 동안의 오류 모음과 필드 읽기 도우미입니다.
     * @details 필드 읽기는 둘로 나뉜다. `readXxx` 는 필수 필드라 없으면 오류이고, `readOptionalXxx` 는 선택 필드라 없으면 넣어 둔 값(정의의 초기값) 그대로다.
     *          어느 쪽이든 숫자가 아닌 글은 오류다 — 오류가 하나라도 있으면 `finish` 가 실패해 로드가 멈추므로, 오류 뒤에 돌려주는 값은 쓰이지 않는다.
     *          벡터는 공백이나 쉼표로 나눈 세 수이고, 회전은 도 단위 오일러(피치, 요, 롤)다. `quaternion::makeFromYawPitchRoll( float3 )` 과 같은 배치다.
     */
    class SW_API CharacterDataReader
    {
    public:
        explicit CharacterDataReader( string_view sourceName );

        /**
         * @brief 문서를 텍스트에서 파싱하고 루트 원소를 찾습니다. 실패하면 오류를 남기고 false 입니다.
         * @param pRootName 루트 원소 이름입니다(대소문자 무시).
         */
        [[nodiscard]] bool parseRoot( XMLDocument& outDocument, string_view xmlText, const utf8* pRootName, XMLNode& outRoot );
        /** @brief 리소스 경로(또는 절대 경로)의 문서를 읽고 루트 원소를 찾습니다. */
        [[nodiscard]] bool loadRoot( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot );

        /** @brief 오류 하나를 남깁니다. 원소 이름을 앞에 붙입니다. id 는 `Character.DataInvalid` 입니다. */
        void addError( const XMLNode& node, string_view message );
        /** @brief 원소와 상관없는 오류 하나를 남깁니다. id 는 `Character.DataInvalid` 입니다. */
        void addError( string_view message );
        /**
         * @brief id 가 붙은 오류 하나를 남깁니다. 원소 이름을 앞에 붙입니다.
         * @param pID `"영역.사건"` 꼴의 **글자 상수**입니다(`SW_LOG_ERROR_ID` 와 같다). 테스트가 문구가 아니라 이것으로 찾는다.
         */
        void addError( const utf8* pID, const XMLNode& node, string_view message );
        /** @brief id 가 붙은, 원소와 상관없는 오류 하나를 남깁니다(@p pID 는 위와 같다). */
        void addError( const utf8* pID, string_view message );
        /** @brief 테이블에 없는 속성마다 오류를 남깁니다. */
        template <size_t Count>
        void reportUnknownAttributes( const XMLNode& node, const utf8* const ( &arrKnown )[Count] )
        {
            reportUnknownAttributes( node, arrKnown, Count );
        }
        /** @brief 속성이 하나라도 있으면 오류를 남깁니다(속성이 없어야 하는 원소, 예를 들어 루트). */
        void reportUnexpectedAttributes( const XMLNode& node ) { reportUnknownAttributes( node, nullptr, 0 ); }
        /** @brief 모르는 자식 원소 하나를 오류로 남깁니다. */
        void reportUnknownElement( const XMLNode& node );

        /** @brief 필수 필드를 읽습니다. 없거나 읽지 못하면 오류입니다. */
        float32            readFloat( const XMLNode& node, const utf8* pName );
        int32              readInt( const XMLNode& node, const utf8* pName );
        [[nodiscard]] bool readBool( const XMLNode& node, const utf8* pName );
        /** @brief 세 수 필드를 읽습니다("x y z"). 필수입니다. */
        float3 readFloat3( const XMLNode& node, const utf8* pName );
        /** @brief 회전 필드를 읽습니다(도 단위 "피치 요 롤"). 필수입니다. */
        quaternion readRotation( const XMLNode& node, const utf8* pName );

        /**
         * @brief 선택 필드를 읽어 @p inoutValue 에 넣습니다. 없으면 그대로 두고(정의의 초기값), 있는데 읽지 못하면 오류입니다(값은 그대로).
         * @details 없을 때의 값을 인자로 받지 않는다 — 기본값의 원본은 정의 구조체의 초기값 하나다(데이터 주도 계획 7.4).
         */
        void readOptionalFloat( const XMLNode& node, const utf8* pName, float32& inoutValue );
        void readOptionalInt( const XMLNode& node, const utf8* pName, int32& inoutValue );
        void readOptionalBool( const XMLNode& node, const utf8* pName, bool& inoutValue );
        void readOptionalFloat3( const XMLNode& node, const utf8* pName, float3& inoutValue );
        /** @brief 이름 필드를 읽습니다. @p bRequired 인데 비어 있으면 오류입니다. */
        hashed_string readName( const XMLNode& node, const utf8* pName, bool bRequired );
        /** @brief 공백이나 쉼표로 나눈 이름 목록 필드를 읽습니다(비우고 채웁니다). */
        void readNameList( const XMLNode& node, const utf8* pName, vector<hashed_string>& outListName );
        /** @brief 필드가 있으면 true 를 반환합니다. */
        static bool hasAttribute( const XMLNode& node, const utf8* pName );

        /** @brief 오류가 하나라도 있으면 true 입니다. */
        bool hasError() const { return _listError.empty() == false; }
        /** @brief 오류가 있으면 한 줄씩 로그(Error)로 내고 false, 없으면 true 입니다. 로더가 끝에서 부릅니다. */
        [[nodiscard]] bool finish() const;
        /** @brief 읽는 파일 이름입니다. */
        const string& getSourceName() const { return _sourceName; }

        /** @brief 공백이나 쉼표로 나눈 토큰 목록을 구합니다(비우고 채웁니다). */
        static void splitTokens( string_view text, vector<string_view>& outListToken );
        /** @brief 실수 하나를 가장 짧은 표현으로 씁니다. */
        static string formatFloat( float32 value );
        /** @brief 세 수를 "x y z" 로 씁니다. */
        static string formatFloat3( const float3& value );
        /** @brief 회전을 도 단위 "피치 요 롤" 로 씁니다. */
        static string formatRotation( const quaternion& rotation );
        /** @brief 이름 목록을 공백으로 이어서 씁니다. */
        static string formatNameList( const vector<hashed_string>& listName );

    private:
        /** @brief 필수 필드의 글입니다. 없으면 오류를 남기고 nullptr 입니다. */
        const utf8* findRequiredText( const XMLNode& node, const utf8* pName );
        /** @brief 글을 종류대로 풉니다. 풀지 못하면 오류를 남기고 형식의 0 값입니다(로드는 `finish` 에서 실패한다). */
        float32            parseFloatText( const XMLNode& node, const utf8* pName, const utf8* pText );
        int32              parseIntText( const XMLNode& node, const utf8* pName, const utf8* pText );
        [[nodiscard]] bool parseBoolText( const XMLNode& node, const utf8* pName, const utf8* pText );
        float3             parseFloat3Text( const XMLNode& node, const utf8* pName, const utf8* pText );
        quaternion         parseRotationText( const XMLNode& node, const utf8* pName, const utf8* pText );

        void reportUnknownAttributes( const XMLNode& node, const utf8* const* ppKnown, size_t knownCount );
        /** @brief 루트 찾기 결과가 실패면 이유를 오류로 남깁니다. 찾았으면 true 입니다. */
        bool reportRootStatus( const XMLDocument& document, XMLRootStatus status, const utf8* pRootName );

    private:
        /** @brief 모은 오류 하나입니다. `_pID` 는 글자 상수입니다. */
        struct Error
        {
            string      _text;
            const utf8* _pID{ nullptr };
        };

        string        _sourceName;
        vector<Error> _listError;
    };
} // namespace sw
