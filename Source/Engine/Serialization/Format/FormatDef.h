/**
 * @file FormatDef.h
 * @brief 저장 바이트 형식 하나의 선언 — 네 글자 표 · 지금 판 · 올리기 단계 — 과 그 머리(표 · 판)를 쓰고 읽고 거절하는 한 길입니다.
 * @details 형식은 `static constexpr FormatDef kFormat{ "SWHF" };` 한 줄로 선언한다. 세이브 · 상태처럼 옛 판을 읽는 형식은 올리기 단계 배열을 주고 **단계 수가 곧 판**이라
 *          판만 올리고 단계를 빠뜨리거나 가운데가 빈 사슬을 쓸 수 없다. 옛 판을 읽지 않는 형식(쿠킹본 · 캐시 · 도구 상태)은 판을 숫자로 올린다(`{ "SWPK", 1 }`).
 *          언리얼 `FCustomVersion` 은 판을 아카이브에 등록하고 `Serialize` 가 판마다 갈라 읽고, 유니티 · Godot 은 형식 번호를 로더마다 견준다 — 여기는 갈라 읽기 대신
 *          순수 함수 단계를 두고 대조 · 거절 로그(`Format.HeadRefused` · `FormatUpgrade.*`)를 한 곳에 둔다. XML 에셋의 판은 종류 정의(`AssetTypeDef::_formatVersion`),
 *          리플렉션 본문의 판은 타입(`REFLECT( Version )`)이고 거절 로그는 같다(`FormatUpgrade.h`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class Archive;
    class BitReader;
    class BitWriter;
    class ByteReader;

    /** @brief 판 n → n+1 순수 함수입니다(배열의 n 번째). 옛 본문에서 자기 칸만 읽어 새 본문 칸으로 쓴다 — 읽지 않은 꼬리(바깥 형식의 칸)는 그대로 붙는다. 못 고치면 false 입니다. */
    using FormatStep = bool ( * )( Archive& inOldBody, Archive& outNewBody );

    /** @brief 저장 바이트 형식 하나입니다. 머리는 `[표 uint32][판 uint32]`(비트 스트림은 판이 가변 정수)이다. */
    struct SW_API FormatDef
    {
        /** @brief 표 · 판의 바이트 수입니다. */
        static constexpr size_t kHeadBytes = 2 * sizeof( uint32 );

        uint32            _tag;
        uint32            _version;
        const FormatStep* _pSteps; ///< `_version` 개. 없으면 옛 판을 읽지 않는 형식이다

        /** @brief 옛 판을 읽지 않는 형식입니다. 첫 판은 0 이다. */
        constexpr explicit FormatDef( const utf8 ( &tag )[5], uint32 version = 0 ) noexcept
            : _tag{ makeTag( tag ) }
            , _version{ version }
            , _pSteps{ nullptr }
        {
        }
        /** @brief 옛 판을 단계로 올려 읽는 형식입니다. 판은 단계 수다. */
        template <size_t N>
        constexpr FormatDef( const utf8 ( &tag )[5], const FormatStep ( &arrStep )[N] ) noexcept
            : _tag{ makeTag( tag ) }
            , _version{ static_cast<uint32>( N ) }
            , _pSteps{ arrStep }
        {
        }

        void write( vector<uint8>& outBytes ) const;
        void write( Archive& outArchive ) const;
        void write( BitWriter& outWriter ) const;
        /** @brief 머리를 읽어 표와 판이 이 형식의 지금 판인지 봅니다. 아니면(다른 표 · 새 판 · 옛 판 · 잘린 바이트) 까닭입니다. 로그는 남기지 않는다. */
        [[nodiscard]] expected<void> check( ByteReader& reader, size_t headBytes = kHeadBytes ) const;
        [[nodiscard]] expected<void> check( Archive& archive ) const;
        [[nodiscard]] expected<void> check( BitReader& reader ) const;
        /** @brief 머리에서 읽은 값으로 봅니다(머리를 다른 칸과 함께 읽는 형식). */
        [[nodiscard]] expected<void> check( uint32 tag, uint32 version ) const;
        /** @brief `check` 하고, 아니면 @p sourceName 과 까닭을 적은 오류(`Format.HeadRefused`) 하나를 남기고 false 입니다. */
        template <typename TReader>
        [[nodiscard]] bool read( TReader& reader, string_view sourceName ) const
        {
            return report( check( reader ), sourceName );
        }
        /** @brief 표 · 판 뒤의 고정 칸까지 머리 @p headBytes 바이트가 다 있는지도 봅니다. */
        [[nodiscard]] bool read( ByteReader& reader, string_view sourceName, size_t headBytes ) const { return report( check( reader, headBytes ), sourceName ); }
        /** @brief 종류를 그릇이 아는 형식(봉투 · JSON)의 판 @p version 을 봅니다. 아니면 `read` 와 같은 오류를 남기고 false 입니다. */
        [[nodiscard]] bool accept( uint32 version, string_view sourceName ) const { return report( check( _tag, version ), sourceName ); }

        /** @brief 표가 종류를 말하는 저장 레코드의 머리(판만, 가변 정수)입니다. 읽기는 로그를 남기지 않는다 — 키를 아는 쪽이 레코드 이름과 함께 남긴다. */
        void               writeVersion( BitWriter& outWriter ) const;
        [[nodiscard]] bool readVersion( BitReader& reader ) const;

        /**
         * @brief 판 @p fromVersion 의 본문 @p inoutBody(머리 뒤)를 지금 판까지 올립니다. 같은 판이면 그대로 true 입니다.
         * @return 새 판(내리기) · 단계 없음 · 단계 실패면 오류를 남기고 false 입니다(@p inoutBody 는 중간 판일 수 있다 — 쓰지 않는다).
         */
        [[nodiscard]] bool raise( uint32 fromVersion, vector<uint8>& inoutBody ) const;

        /** @brief 판 @p fromVersion 을 @p toVersion 으로 올릴 수 없다는 오류를 남깁니다(새 판이면 `NewerThanSupported`, 아니면 `StepMissing`). 사슬마다 이것을 쓴다. */
        static void reportCannotRaise( string_view kind, uint32 fromVersion, uint32 toVersion );
        /** @brief 단계 하나로 @p inoutBody 의 앞 칸을 다시 쓰고 읽지 않은 나머지를 그대로 붙입니다. */
        [[nodiscard]] static bool runStep( FormatStep pStep, vector<uint8>& inoutBody );

        /** @brief @p checked 가 오류면 `Format.HeadRefused` 를 남깁니다. 오류가 아니면 true 입니다. 로그 앞에 할 일(잠금 놓기)이 있는 읽기가 `check` 뒤에 부른다. */
        static bool report( const expected<void>& checked, string_view sourceName );

    private:
        /** @brief `FourCCUtil::make` 와 같은 값입니다(파일 앞의 글자 순서) — 형식을 선언하는 헤더마다 그 헤더를 끌지 않게 여기 둔다. */
        static constexpr uint32 makeTag( const utf8 ( &tag )[5] ) noexcept
        {
            return static_cast<uint32>( static_cast<uint8>( tag[0] ) ) | ( static_cast<uint32>( static_cast<uint8>( tag[1] ) ) << 8 ) | ( static_cast<uint32>( static_cast<uint8>( tag[2] ) ) << 16 ) |
                   ( static_cast<uint32>( static_cast<uint8>( tag[3] ) ) << 24 );
        }
        /** @brief 표의 네 글자입니다(파일 앞의 글자 순서). */
        fixed_string<constant::buffer::kMax16> makeTagText() const;
    };
} // namespace sw
