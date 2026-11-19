/**
 * @file EnumInfo.h
 * @brief 열거형 하나의 리플렉션 정보입니다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/String/EnumNameTable.h"

#include "Engine/Reflection/ReflectionMetadataBox.h"

namespace sw
{
    struct EnumEditorMetadata;
    struct EnumeratorEditorMetadata;

    /**
     * @struct EnumInfoDesc
     * @brief 생성기가 열거형마다 내는 등록 설명자입니다 — 항목 표(이름 · 값 배열) 하나와 표식 이름만 담고, 이름 표 · 값 표 · 인덱스 목록은 `EnumInfo::create` 가 짓는다.
     * @details 열거형마다 같은 맵 초기화 코드를 펼치지 않는다(magic_enum 이 이름 · 값 배열 하나에서 모든 조회를 푸는 것과 같은 모양). 값은 생성 코드에서
     *          `static_cast<int64>( E::X )` 라 컴파일러가 계산한다 — libclang 의 값을 숫자로 박지 않는다.
     */
    struct EnumInfoDesc
    {
        const utf8*                   _pName;
        const utf8*                   _pFullyQualifiedName;
        const utf8*                   _pModuleName;
        span<const NamedValue<int64>> _listEntry;    ///< 선언 순서의 열거자 전부(표식 열거자 포함)
        span<const NamedValue<int64>> _listRedirect; ///< 옛 열거자 이름 → 지금 값(`ValueAlias` · `Removed`)
        const utf8*                   _pInvalidName; ///< `Invalid` 표식 열거자 이름. 없으면 nullptr
        const utf8*                   _pCountName;   ///< `Count` 표식 열거자 이름(끝 열거자 `Count` 는 생성기가 자동으로 넣는다). 없으면 nullptr
        bool                          _bIsBitFlag;   // NOLINT(sw-bool-flag): 생성 코드가 집합체 초기화로 한 번 채우는 일회용 설명자다(기본값 없는 비트필드는 순서 초기화를 깨뜨린다)
    };

    /** @brief 열거자 하나(이름 · 값)입니다. `EnumInfo::_listEnumerator` 의 원소입니다. */
    struct EnumeratorInfo
    {
        hashed_string _name;
        int64         _value{ 0 };
    };

    /// @brief 등록된 enum 의 정보입니다. 이름과 값의 대응, Flags 여부, Invalid 와 Count 센티널을 담습니다.
    struct SW_API EnumInfo
    {
        unordered_map<hashed_string, int64> _mapNameToValue; ///< 지금 열거자 이름만 담습니다. 옛 이름은 `_mapRedirectNameToValue` 에 있습니다
        unordered_map<int64, hashed_string> _mapValueToName;
        /**
         * @brief 옛 열거자 이름에서 지금 값으로 가는 리다이렉트 표입니다. 이름 변경(`ENUM( ValueAlias = "Old:New" )`)과 지운 열거자의 대체
         *        (`ENUM( Removed = "Old:Replacement" )`)를 생성기가 채웁니다.
         * @details 읽기(바이너리 이름 해시, 텍스트 이름)는 지금 이름에서 찾지 못했을 때만 이 표를 봅니다. 쓰기는 이 이름을 쓰지 않습니다.
         *          선언이 없는 옛 이름은 여기 없으므로 읽기가 실패합니다(조용히 기본값으로 읽지 않습니다).
         */
        unordered_map<hashed_string, int64> _mapRedirectNameToValue;
        /** @brief 에디터 메타데이터(`ReflectionEditorMetadata.h` — 커스텀 메타)입니다. 열거형을 등록할 때 `TypeRegistry` 가 붙이고, 없으면(배포 · 적은 것 없음) nullptr 입니다. */
        const EnumEditorMetadata* _pEditor;
        hashed_string             _name;
        hashed_string             _fullyQualifiedName;
        hashed_string             _moduleName;
        /**
         * @brief 값이 곧 인덱스인 이름 목록입니다. 열거자 값이 `0..N-1` 로 빈틈없는 열거형에만 생성기가 채우고, 그 밖에는 비어 있습니다.
         * @details `valueToCString` 이 해시 맵 대신 이 목록을 봅니다. 원소는 `_mapValueToName` 과 같은 intern 된 이름이라 돌려주는 포인터도 같습니다.
         *          `EnumInfo` 가 값으로 가진다. 모듈의 정적 배열을 가리키면 핫 리로드 해제와 겹친 렌더 · 오디오 · 로거 스레드의 `getEnumName` 이
         *          해제된 저장소를 읽는다. intern 된 이름은 모듈보다 오래 살므로 묘비에도 그대로 둔다(`_mapValueToName` 과 같은 규칙).
         */
        vector<hashed_string> _listDenseName;
        /**
         * @brief 선언 순서의 열거자 목록입니다. `Count` 표식 열거자는 빠진다(에디터 선택지 · 반복은 이 목록을 돈다). 생성기가 채운 등록(`create`)에만 있습니다.
         * @details 이름 표(`_mapNameToValue`)에는 표식 열거자도 남는다 — 필드에 든 표식 값을 이름으로 쓰고 읽는 왕복(`tryParseText`)이 그 이름을 본다.
         */
        vector<EnumeratorInfo> _listEnumerator;
        int64                  _invalidValue{ 0 };
        int64                  _countValue{ 0 };
        uint8                  _size{ sizeof( int32 ) };
        uint8                  _bIsBitFlag    : 1;
        uint8                  _bHasInvalid   : 1;
        uint8                  _bHasCount     : 1;
        uint8                  _bIsSigned     : 1; ///< 기반 타입이 부호 있는 정수인지 나타낸다. 좁은 값을 메모리에서 읽을 때의 확장 방식이 이것으로 정해진다
        [[maybe_unused]] uint8 _reservedFlags : 4;

        /** @brief 비어 있는 이름-값 맵으로 만듭니다. */
        EnumInfo() noexcept;

        /** @brief 생성 코드의 항목 표(@p desc)에서 열거형 정보를 짓습니다. 크기와 부호는 열거형 @p E 에서 읽습니다. */
        template <typename E>
        [[nodiscard]] static EnumInfo create( const EnumInfoDesc& desc )
        {
            static_assert( std::is_enum_v<E>, "EnumInfo::create needs an enum type" );
            return createFromDesc( desc, static_cast<uint8>( sizeof( E ) ), std::is_signed_v<std::underlying_type_t<E>> );
        }

        /**
         * @brief 항목 표에서 이름 표 · 값 표 · 리다이렉트 표 · 열거자 목록 · 인덱스 목록을 짓습니다.
         * @details 같은 값의 이름이 여럿이면 값 → 이름은 먼저 적은 열거자다. 표식(`Invalid` · `Count`)을 뺀 열거자 값이 `0..N-1` 로 빈틈없고 겹치지 않으면
         *          값이 곧 인덱스인 이름 목록(`_listDenseName`)을 채운다(비트 플래그 제외). 표식 이름이 표에 없으면 멈춘다 — 생성기가 이미 확인한다.
         */
        [[nodiscard]] static EnumInfo createFromDesc( const EnumInfoDesc& desc, uint8 size, bool bSigned );

        /** @brief 커스텀 메타데이터를 조회합니다. 에디터 메타데이터가 붙지 않았으면(배포) nullptr 입니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;
        /** @brief 열거자 @p name 의 표시 메타(`ENUMERATOR( … )`)입니다. 적지 않았거나 에디터 메타데이터가 없으면(배포) 기본값입니다. */
        const EnumeratorEditorMetadata& findEnumeratorEditorMetadata( const hashed_string& name ) const noexcept;

        /**
         * @brief 메모리 포인터에서 실제 enum 크기만큼 안전하게 읽어 int64 로 반환합니다.
         * @details 기반 타입의 부호에 따라 확장한다. 이름 맵(`_mapValueToName`)의 값도 같은 규칙으로 만든다(생성 코드의 `static_cast<int64>( 열거자 )`).
         *          둘이 어긋나면 `uint8` 의 0x80 이나 `int8` 의 -1 같은 값이 이름을 잃는다.
         */
        int64 readValueFromMemory( const void* pPtr ) const noexcept
        {
            if ( pPtr == nullptr )
                return 0;
            const bool bSigned = _bIsSigned != SW_FALSE;
            switch ( _size )
            {
                case 1:
                    return bSigned ? static_cast<int64>( *static_cast<const int8*>( pPtr ) ) : static_cast<int64>( *static_cast<const uint8*>( pPtr ) );
                case 2:
                    return bSigned ? static_cast<int64>( *static_cast<const int16*>( pPtr ) ) : static_cast<int64>( *static_cast<const uint16*>( pPtr ) );
                case 8:
                    return *static_cast<const int64*>( pPtr );
                case 4:
                default:
                    return bSigned ? static_cast<int64>( *static_cast<const int32*>( pPtr ) ) : static_cast<int64>( *static_cast<const uint32*>( pPtr ) );
            }
        }

        /** @brief int64 값을 실제 enum 크기만큼만 메모리에 안전하게 씁니다. */
        void writeValueToMemory( void* pPtr, int64 val ) const noexcept
        {
            if ( pPtr == nullptr )
                return;
            switch ( _size )
            {
                case 1:
                {
                    *static_cast<uint8*>( pPtr ) = static_cast<uint8>( val );
                    break;
                }
                case 2:
                {
                    *static_cast<uint16*>( pPtr ) = static_cast<uint16>( val );
                    break;
                }
                case 4:
                {
                    *static_cast<int32*>( pPtr ) = static_cast<int32>( val );
                    break;
                }
                case 8:
                {
                    *static_cast<int64*>( pPtr ) = val;
                    break;
                }
                default:
                {
                    *static_cast<int32*>( pPtr ) = static_cast<int32>( val );
                    break;
                }
            }
        }

        /** @brief ENUM(Invalid, Count) 센티널을 반영해 유효한 값인지 판단합니다. 메타가 없으면 true 입니다. */
        bool isValidValue( int64 value ) const noexcept
        {
            if ( _bHasInvalid != SW_FALSE && value == _invalidValue )
                return false;
            if ( _bHasCount != SW_FALSE && value >= _countValue )
                return false;
            return true;
        }

        /**
         * @brief @p value 가 선언된 열거자인지(비트 플래그면 선언된 비트들의 조합인지) 판단합니다. 표식(`Invalid` · `Count`)은 거절합니다.
         * @details 남이 보낸 바이트를 읽는 쪽(`StateIO::reflected`)이 쓴다 — `isValidValue` 는 표식만 보므로 열거자 밖 값(`Boss` 다음의 9)이 그대로 들어왔다.
         */
        [[nodiscard]] bool isDeclaredValue( int64 value ) const noexcept
        {
            if ( isValidValue( value ) == false )
                return false;
            if ( _bIsBitFlag == SW_FALSE )
                return _mapValueToName.find( value ) != _mapValueToName.end();
            int64 declaredBits = 0;
            for ( const auto& [bitValue, name] : _mapValueToName )
            {
                declaredBits |= bitValue;
            }
            return ( value & ~declaredBits ) == 0;
        }

        /** @brief 값의 이름입니다(해시 · 맵 키용, 언리얼 `UEnum::GetNameByValue`). 열거자 `None` 은 빈 이름이다 — 글로 쓸 때는 `toString` 을 쓴다. */
        hashed_string getNameByValue( int64 val ) const
        {
            auto iter = _mapValueToName.find( val );
            return iter != _mapValueToName.end() ? iter->second : hashed_string();
        }

        /** @brief 비트플래그 값의 이름(`A | B`)입니다. 비트플래그가 아니면 `getNameByValue` 와 같다. 글로 쓸 때는 `toStringFlags` 를 쓴다. */
        hashed_string getFlagsNameByValue( int64 val ) const
        {
            if ( _bIsBitFlag == SW_FALSE )
                return getNameByValue( val );

            if ( val == 0 )
            {
                auto iter = _mapValueToName.find( 0 );
                return iter != _mapValueToName.end() ? iter->second : hashed_string( constant::reflection::kNone );
            }

            vector<hashed_string> listName;
            (void)collectFlagNames( val, listName ); // 이름이 덮지 못한 비트는 텍스트에 적지 않는다
            string result;
            result.reserve( 64 );
            for ( const hashed_string& name : listName )
            {
                if ( result.empty() == false )
                    result += constant::reflection::kFlagSeparator;
                result += name.toString();
            }

            return hashed_string( result.c_str() );
        }

        /** @brief 값의 표시 글입니다(언리얼 `UEnum::GetNameStringByValue`) — 열거자 `None` 도 "None" 이다. 모르는 값은 빈 글이다. 글은 이름 표의 영구 글이다. */
        string_view toString( int64 val ) const
        {
            const auto iter = _mapValueToName.find( val );
            return iter != _mapValueToName.end() ? iter->second.toString() : string_view{}; // 모르는 값을 "None"(0 으로 읽힌다)으로 적지 않는다
        }

        /** @brief 비트플래그 값의 표시 글(`A | B`, 0 은 `None`)입니다. 텍스트 직렬화가 적는 글과 같다. */
        string_view toStringFlags( int64 val ) const
        {
            if ( _bIsBitFlag == SW_FALSE )
                return toString( val );
            const hashed_string name = getFlagsNameByValue( val );
            return ( name.empty() && val != 0 ) ? string_view{} : name.toString(); // 이름이 덮지 못한 값은 빈 글이다(0 만 None)
        }

        /**
         * @brief 비트플래그 값에서 켜진 열거자의 이름을 모으고, 이름이 덮지 못한 비트를 반환합니다.
         * @details **단일 비트 열거자의 이름만** 쓴다 — 조합 열거자(`All = A | B`)는 `A | B` 로 풀어 적는다(magic_enum 플래그 글과 같은 규칙). 조합 이름을 겹쳐 적으면
         *          같은 값이 `A | B | All` 처럼 길어지고, 조합을 바꾸면 옛 글의 뜻이 바뀐다. 단일 비트 이름이 없는 비트만 그 비트를 덮는 조합 이름으로 적는다(값을 잃지 않는다).
         *          `_mapValueToName` 만 쓰고 ValueAlias 는 겹쳐 적지 않는다. 텍스트(`toStringFlags`)와 바이너리(`SerializerUtil` 의 열거자 식별 인코딩)가
         *          **같은 이름**을 적도록 이 규칙을 한 곳에 둔다. 읽기(`tryParseFlags`)는 `A | B` 의 토큰마다 이름을 받는다 — 조합 이름(`All`)도 받는다(손으로 쓴 데이터).
         */
        int64 collectFlagNames( int64 value, vector<hashed_string>& outListName ) const
        {
            outListName.clear();
            int64 coveredBits{ 0 };
            for ( const auto& [bitValue, name] : _mapValueToName )
            {
                if ( isSingleBit( bitValue ) && ( value & bitValue ) == bitValue )
                {
                    outListName.push_back( name );
                    coveredBits |= bitValue;
                }
            }
            for ( const auto& [bitValue, name] : _mapValueToName )
            {
                const int64 uncoveredBits = value & ~coveredBits;
                if ( uncoveredBits == 0 )
                    break;
                if ( bitValue != 0 && isSingleBit( bitValue ) == false && ( value & bitValue ) == bitValue && ( bitValue & uncoveredBits ) != 0 )
                {
                    outListName.push_back( name );
                    coveredBits |= bitValue;
                }
            }
            return value & ~coveredBits;
        }

        /**
         * @brief 이름 해시로 열거자 값을 찾습니다. 지금 이름에 없으면 리다이렉트 표(`_mapRedirectNameToValue`)를 한 번 봅니다.
         * @details 바이너리는 열거자를 이 해시로 저장한다. 값이 아니라 이름이 열거자의 정체성이기 때문이다.
         *          표식 값(`Invalid`, `Count`)도 받으며, 이 규칙은 `tryParseText` 와 같다. 필드에 들어 있는 값을 적힌 그대로 읽는다.
         *          이름 해시는 대소문자를 구분하지 않는다(`hashed_string` 이 그렇다). 바이너리는 엔진이 쓴 해시라 철자가 갈릴 일이 없고, 텍스트 읽기는 철자 그대로 견준다.
         *          옛 이름(`ValueAlias` · `Removed`)은 실제 게임 데이터가 생긴 뒤 이름을 바꾸거나 지울 때 쓰는 수단이고, 그 전에는 데이터를 새 이름으로 다시 쓴다.
         */
        [[nodiscard]] bool findValueByNameHash( uint32 nameHash, int64& outValue ) const
        {
            if ( findInMapByNameHash( _mapNameToValue, nameHash, outValue ) )
                return true;
            return findInMapByNameHash( _mapRedirectNameToValue, nameHash, outValue );
        }

        /** @brief 값에 해당하는 열거자 이름 글입니다(열거자 `None` 은 "None"). Invalid 나 Count 센티널이거나 이름이 없으면 nullptr 입니다. */
        const utf8* valueToCString( int64 value ) const
        {
            if ( isValidValue( value ) == false )
                return nullptr;
            if ( _listDenseName.empty() == false )
            {
                const bool bInRange = 0 <= value && value < static_cast<int64>( _listDenseName.size() );
                return bInRange ? _listDenseName[static_cast<size_t>( value )].toString().data() : nullptr;
            }
            // 열거자 `None` 의 이름은 빈 이름이라 표시 글(`toString`)로 꺼낸다. 그 `data()` 는 널로 끝나는 영구 글이다.
            if ( _bIsBitFlag != SW_FALSE )
            {
                const hashed_string name = getFlagsNameByValue( value );
                return ( name.empty() && value != 0 ) ? nullptr : name.toString().data();
            }
            const auto iter = _mapValueToName.find( value );
            return iter != _mapValueToName.end() ? iter->second.toString().data() : nullptr;
        }

        /** @brief 이름을 값으로 바꿉니다. 철자는 열거자 이름 그대로여야 하고(대소문자가 다르면 모르는 이름), 비트플래그는 `A|B` 도 허용합니다. */
        [[nodiscard]] bool tryParse( string_view name, int64& outValue ) const
        {
            if ( name.empty() )
                return false;

            {
                // 찾기만 한다. 열거자 이름은 등록할 때 intern 됐으므로 결과가 같고, 에셋의 모르는 문자열이 전역 이름 테이블에 들어가지 않는다.
                // `hashed_string` 은 대소문자를 구분하지 않으므로 찾은 뒤 철자를 한 번 더 견준다 — 대소문자 무시는 이름마다 별칭을 만든다.
                const hashed_string key = hashed_string::findInterned( name );
                const auto          it  = _mapNameToValue.find( key );
                if ( it != _mapNameToValue.end() && it->first.toString() == name )
                {
                    if ( isValidValue( it->second ) == false )
                        return false;
                    outValue = it->second;
                    return true;
                }
            }

            int64 namedValue{ 0 };
            if ( findValueByText( name, namedValue ) )
            {
                if ( isValidValue( namedValue ) == false )
                    return false;
                outValue = namedValue;
                return true;
            }

            int64      flagsValue{ 0 };
            const bool bInvalidFlagsText = _bIsBitFlag == SW_FALSE || tryParseFlags( name, flagsValue ) == false || isValidValue( flagsValue ) == false;
            if ( bInvalidFlagsText )
                return false;
            outValue = flagsValue;
            return true;
        }

        /**
         * @brief 에셋이나 설정 텍스트를 값으로 읽습니다. 이름(철자 그대로), 비트플래그의 `A | B`, 알려진 값의 숫자를 받습니다.
         * @details 모르는 이름이나 모르는 플래그 토큰이 하나라도 있으면 false 다. 아는 토큰만 남기거나 0 으로 읽지 않는다.
         *          에셋과 설정 텍스트를 값으로 읽는 경로는 이 함수 하나다(직렬화기, 머티리얼).
         *
         *          `tryParse` 와 달리 표식 값(`Invalid`, `Count`)도 받는다. 직렬화는 필드에 들어 있는 값을 이름으로 적으므로(`Key::Unknown` 은 "바인딩 없음"이다)
         *          읽기도 그 이름을 받아야 왕복이 맞는다. 이름을 해시로 바꾸지 않는다. 에셋 텍스트를 전역 이름 테이블에 넣지 않기 위해서다.
         */
        [[nodiscard]] bool tryParseText( string_view text, int64& outValue ) const
        {
            const string_view trimmed = StringUtil::trim( text );
            // 비트플래그의 빈 텍스트는 "아무 비트도 없음"이다(손으로 쓴 에셋). 쓰는 쪽은 0 을 `None` 으로 적는다(`toStringFlags`).
            if ( trimmed.empty() && _bIsBitFlag != SW_FALSE )
            {
                outValue = 0;
                return true;
            }
            if ( findValueByText( trimmed, outValue ) )
                return true;
            if ( _bIsBitFlag != SW_FALSE && tryParseFlags( trimmed, outValue ) )
                return true;
            int64 number{ 0 };
            if ( StringUtil::parseInt64( trimmed, number ) == false )
                return false;
            if ( _bIsBitFlag != SW_FALSE )
            {
                int64 knownBits{ 0 };
                for ( const auto& [bitValue, name] : _mapValueToName )
                {
                    knownBits |= bitValue;
                }
                if ( ( number & ~knownBits ) != 0 )
                    return false;
            }
            else if ( _mapValueToName.find( number ) == _mapValueToName.end() )
            {
                return false;
            }
            outValue = number;
            return true;
        }

    private:
        /** @brief 비트가 정확히 하나 켜진 값인지 봅니다. */
        static constexpr bool isSingleBit( int64 value ) noexcept
        {
            const uint64 bits = static_cast<uint64>( value );
            return bits != 0 && ( bits & ( bits - 1 ) ) == 0;
        }

        /** @brief 이름 → 값 표 하나에서 이름 해시로 찾습니다. */
        [[nodiscard]] static bool findInMapByNameHash( const unordered_map<hashed_string, int64>& mapNameToValue, uint32 nameHash, int64& outValue )
        {
            for ( const auto& [nameKey, enumValue] : mapNameToValue )
            {
                if ( nameKey.getHash() == nameHash )
                {
                    outValue = enumValue;
                    return true;
                }
            }
            return false;
        }

        /** @brief 이름 → 값 표 하나에서 글(철자 그대로)로 찾습니다. 글을 intern 하지 않습니다. */
        [[nodiscard]] static bool findInMapByText( const unordered_map<hashed_string, int64>& mapNameToValue, string_view text, int64& outValue )
        {
            for ( const auto& [nameKey, enumValue] : mapNameToValue )
            {
                if ( text == nameKey.toString() )
                {
                    outValue = enumValue;
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief 열거자 이름 글(철자 그대로)로 값을 찾습니다. 지금 이름에 없을 때만 리다이렉트 표를 봅니다.
         * @details 텍스트 읽기(`tryParse` · `tryParseText` · `tryParseFlags`)가 모두 이 함수를 써서 바이너리(`findValueByNameHash`)와 같은 옛 이름을 받습니다.
         */
        [[nodiscard]] bool findValueByText( string_view text, int64& outValue ) const
        {
            if ( findInMapByText( _mapNameToValue, text, outValue ) )
                return true;
            return findInMapByText( _mapRedirectNameToValue, text, outValue );
        }

        /** @brief `A | B` 를 읽습니다. 토큰마다 알려진 이름(철자 그대로)이어야 하며, 하나라도 모르면 false 입니다. 표식 값 검사는 부르는 쪽이 합니다. */
        [[nodiscard]] bool tryParseFlags( string_view text, int64& outValue ) const
        {
            int64 result{ 0 };
            bool  bAnyToken = false;
            for ( const string_view token : StringUtil::split( text, "|", { ._bTrim = true, ._bDropEmpty = true } ) )
            {
                int64      tokenValue{ 0 };
                const bool bKnown = findValueByText( token, tokenValue );
                if ( bKnown )
                    result |= tokenValue;
                // `None` 은 0 열거자가 없는 플래그에 `toStringFlags` 가 0 을 적는 이름이다 . 읽을 때도 0 이어야 왕복이 맞는다.
                if ( bKnown == false && token != string_view{ constant::reflection::kNone } )
                    return false;
                bAnyToken = true;
            }
            if ( bAnyToken == false )
                return false;
            outValue = result;
            return true;
        }

    public:
    };
} // namespace sw
