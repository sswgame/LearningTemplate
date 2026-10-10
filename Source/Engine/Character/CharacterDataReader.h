/**
 * @file CharacterDataReader.h
 * @brief 캐릭터 데이터 XML(소켓 · 레퍼런스 포즈 · 체형 · 피팅 표 · 표면 채널)을 읽는 공통 도우미입니다. 모르는 이름은 모두 **오류**로 모읍니다.
 * @details 데이터가 사람 손으로 고쳐지므로 틀린 이름이 조용히 기본값이 되지 않게 합니다 — 모르는 속성 · 원소 · 숫자가 아닌 숫자 칸이 오류이고,
 *          읽기가 끝나면 로더가 오류를 한꺼번에 로그로 내고 실패를 돌려줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    /**
     * @brief 파일 하나를 읽는 동안의 오류 모음과 칸 읽기 도우미입니다.
     * @details 숫자 · 벡터 칸은 비어 있으면 @p fallback, 글이 숫자가 아니면 오류를 남기고 @p fallback 입니다. 벡터는 공백이나 쉼표로 나눈 세 수,
     *          회전은 도 단위 오일러(피치 · 요 · 롤 — `quaternion::makeFromYawPitchRoll( float3 )` 과 같은 배치)입니다.
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

        /** @brief 오류 하나를 남깁니다. 원소 이름을 앞에 붙입니다. */
        void addError( const XMLNode& node, string_view message );
        /** @brief 원소와 상관없는 오류 하나를 남깁니다. */
        void addError( string_view message );
        /** @brief 표에 없는 속성마다 오류를 남깁니다. */
        template <size_t Count>
        void reportUnknownAttributes( const XMLNode& node, const utf8* const ( &arrKnown )[Count] )
        {
            reportUnknownAttributes( node, arrKnown, Count );
        }
        /** @brief 속성이 하나라도 있으면 오류를 남깁니다(속성이 없는 원소 — 루트 등). */
        void reportUnexpectedAttributes( const XMLNode& node ) { reportUnknownAttributes( node, nullptr, 0 ); }
        /** @brief 모르는 자식 원소 하나를 오류로 남깁니다. */
        void reportUnknownElement( const XMLNode& node );

        /** @brief 실수 칸입니다. */
        float32 readFloat( const XMLNode& node, const utf8* pName, float32 fallback );
        /** @brief 정수 칸입니다. */
        int32 readInt( const XMLNode& node, const utf8* pName, int32 fallback );
        /** @brief 참거짓 칸입니다. */
        [[nodiscard]] bool readBool( const XMLNode& node, const utf8* pName, bool fallback );
        /** @brief 세 수 칸입니다("x y z"). */
        float3 readFloat3( const XMLNode& node, const utf8* pName, const float3& fallback );
        /** @brief 회전 칸입니다(도 단위 "피치 요 롤"). */
        quaternion readRotation( const XMLNode& node, const utf8* pName, const quaternion& fallback );
        /** @brief 이름 칸입니다. @p bRequired 인데 비었으면 오류입니다. */
        hashed_string readName( const XMLNode& node, const utf8* pName, bool bRequired );
        /** @brief 공백 · 쉼표로 나눈 이름 목록 칸입니다(비우고 채움). */
        void readNameList( const XMLNode& node, const utf8* pName, vector<hashed_string>& outListName );
        /** @brief 칸이 있으면 true 입니다. */
        static bool hasAttribute( const XMLNode& node, const utf8* pName ) { return node.findAttribute( pName ) != nullptr; }

        /** @brief 오류가 하나라도 있으면 true 입니다. */
        bool hasError() const { return _listError.empty() == false; }
        /** @brief 오류를 줄바꿈으로 이은 글입니다. */
        string getErrorText() const;
        /** @brief 오류가 있으면 한 줄씩 로그(Error)로 내고 false, 없으면 true 입니다. 로더가 끝에서 부릅니다. */
        [[nodiscard]] bool finish() const;
        /** @brief 읽는 파일 이름입니다. */
        const string& getSourceName() const { return _sourceName; }

        /** @brief 공백 · 쉼표로 나눈 토큰 목록입니다(비우고 채움). */
        static void splitTokens( string_view text, vector<string_view>& outListToken );
        /** @brief 실수 하나를 가장 짧은 표현으로 씁니다. */
        static string formatFloat( float32 value );
        /** @brief 세 수를 "x y z" 로 씁니다. */
        static string formatFloat3( const float3& value );
        /** @brief 회전을 도 단위 "피치 요 롤" 로 씁니다. */
        static string formatRotation( const quaternion& rotation );
        /** @brief 이름 목록을 공백으로 이어 씁니다. */
        static string formatNameList( const vector<hashed_string>& listName );

    private:
        void reportUnknownAttributes( const XMLNode& node, const utf8* const* ppKnown, size_t knownCount );

    private:
        string         _sourceName;
        vector<string> _listError;
    };
} // namespace sw
