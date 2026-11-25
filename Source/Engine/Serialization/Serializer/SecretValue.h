/**
 * @file SecretValue.h
 * @brief 글로 나가지 않는 비밀 값 형식입니다(`Secret32` — 해시 · 토큰 · 키 32 바이트, `SecretText` — 비밀번호).
 * @details 비밀을 `uint8[32]` · `string` 칸으로 두면 JSON · XML 덤프, 인스펙터, 로그 서식이 그 값을 그대로 적는다 — 쓰는 곳마다 가리는 것을 기억해야 한다.
 *          이 형식은 로그 서식 · 글 변환이 없어 `%#` 에 넘기면 컴파일되지 않고, 글 직렬화는 값 대신 `kRedactedText` 를 쓰며 그 글을 다시 읽는 것은 실패다
 *          (가린 글이 값으로 돌아오지 않는다). 값은 `reveal` 로만 꺼낸다 — 검색 한 번으로 비밀을 만지는 곳이 모두 나온다. 사라질 때 바이트를 지운다.
 *          Rust `secrecy::SecretBox`(Debug 는 가리고 꺼내기는 `expose_secret`) · Kubernetes `Secret` 값 형식과 같은 규칙이다. 언리얼 · 유니티 · Godot 엔진엔 이 자리가 없다.
 *
 *          바이너리(`BinarySerializer` · `FixedLayout`)는 값을 싣는다 — 저장 레코드와 암호화된 연결의 메시지가 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SerializeContext;

    /** @brief 비밀 값 형식들의 글 처리입니다. */
    struct SW_API SecretValueUtil
    {
        static constexpr const utf8* kRedactedText = "[redacted]";

        /** @brief 비밀 형식의 글 처리기(쓰기는 `kRedactedText`, 읽기는 실패)를 직렬화 문맥에 등록합니다. 기본 문맥이 만들어질 때 한 번 부른다. */
        static void registerSerializeHandler( SerializeContext& context );
    };

    /** @brief 32 바이트 비밀입니다(비밀번호 해시 · 토큰 비밀과 그 다이제스트 · 장치 비밀 · 세션 키). */
    REFLECT()
    class SW_API Secret32
    {
    public:
        REFLECT_BODY();
        static constexpr int32 kSize = 32;

        Secret32() = default;
        ~Secret32();

        Secret32( const Secret32& )            = default;
        Secret32& operator=( const Secret32& ) = default;

        /** @brief 값을 꺼냅니다(암호 연산 · 전송에 넘길 때만). */
        uint8 ( &reveal() )[kSize] { return _arrByte; }
        const uint8 ( &reveal() const )[kSize] { return _arrByte; }
        /** @brief 걸리는 시간이 값에 매이지 않는 비교입니다. */
        [[nodiscard]] bool isEqual( const Secret32& other ) const;
        void               wipe();

    private:
        PROPERTY( Min = 0, Max = 255 )
        uint8 _arrByte[kSize]{};
    };

    /** @brief 글 비밀(비밀번호)입니다. 넣기는 자유이고 꺼내기는 `reveal` 뿐입니다. 길이 상한은 받는 쪽 규칙이 정하고, 고정 배치형은 `kMaxWireSize` 까지만 싣습니다. */
    REFLECT()
    class SW_API SecretText
    {
    public:
        REFLECT_BODY();
        static constexpr int32 kMaxWireSize = 128;

        SecretText() = default;
        SecretText( string_view text );
        SecretText( const string& text );
        SecretText( const utf8* pText );
        ~SecretText();

        // 옮기기는 두지 않는다 — 짧은 글은 옮겨도 원본의 작은 글 버퍼에 남는다. 복사하고 원본이 사라질 때 지운다
        SecretText( const SecretText& ) = default;
        SecretText& operator=( const SecretText& other );

        /** @brief 값을 꺼냅니다(해시 · 전송에 넘길 때만). */
        string_view reveal() const { return _text; }
        bool        empty() const { return _text.empty(); }
        size_t      size() const { return _text.size(); }
        void        wipe();

    private:
        PROPERTY( MaxCount = SecretText::kMaxWireSize )
        string _text{};
    };
} // namespace sw
