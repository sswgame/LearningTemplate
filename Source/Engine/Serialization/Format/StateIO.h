/**
 * @file StateIO.h
 * @brief 런타임 상태(세이브 · 핫 리로드 · 스냅숏)를 함수 하나로 쓰고 읽는 양방향 직렬화 입구를 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Serialization/Format/Archive.h"

#include <type_traits>
#include <utility>

namespace sw
{
    struct Countdown;
    struct FixedStepTimer;
    struct int2;
    struct RateAccumulator;

    class StateIO;

    namespace StateIOInternal
    {
        /** @brief @p T 에 `serializeState( StateIO& )` 가 있는지 봅니다(C++17 이라 `requires` 를 쓰지 않는다). */
        template <typename T, typename = void>
        struct HasSerializeState : std::false_type
        {
        };
        /** @brief @p T 가 머리표 없이 쓰던 판(`kStateHeaderlessVersion`)을 둔 형식인지 봅니다 — 그 형식은 안쪽 머리가 판을 올린다(`StateIO::header`). */
        template <typename T, typename = void>
        struct HasHeaderlessVersion : std::false_type
        {
        };
        template <typename T>
        struct HasHeaderlessVersion<T, std::void_t<decltype( T::kStateHeaderlessVersion )>> : std::true_type
        {
        };
        template <typename T>
        struct HasSerializeState<T, std::void_t<decltype( std::declval<T&>().serializeState( std::declval<StateIO&>() ) )>> : std::true_type
        {
        };
    } // namespace StateIOInternal

    /**
     * @class StateIO
     * @brief 형식마다 `void serializeState( StateIO& io )` 하나로 쓰기와 읽기를 함께 적게 하는 아카이브 앞문입니다. 쓰기 · 읽기 칸 순서가 어긋날 수 없습니다.
     * @details 언리얼 `Serialize( FArchive& )` 의 양방향 `Ar << x` 자리다(기억에 의존). 유니티 `[SerializeField]` · Godot 저장 깃발처럼 리플렉션으로 가지 않는 까닭은
     *          이 상태가 포인터 ↔ id 변환과 범위 검사를 많이 하고 상당수가 리플렉션 형식이 아니기 때문이다.
     *
     *          규칙은 다음과 같다.
     *          - 실패는 아카이브의 끈적한 오류 하나다(`check` 가 거기에 남긴다). `serializeState` 는 값을 돌려주지 않으므로 실패한 읽기를 성공으로 돌려줄 길이 없다.
     *            뒤따르는 코드가 깨진 값으로 범위를 넘을 수 있으면 `if ( io.check( … ) == false ) return;` 으로 바로 나간다.
     *          - `check` 는 읽을 때만 거절한다. 쓸 때도 식은 평가되므로, 쓸 때 없는 포인터(읽을 때만 필수인 카탈로그)를 보는 식은 `if ( io.isReading() )` 안에 둔다.
     *          - 값으로 구한 칸은 지역 변수에 지금 값을 넣고 `io.value( local )` 뒤 `if ( io.isReading() )` 에서 되돌린다. 쓰기 쪽은 그 값을 그대로 쓴다.
     *          - 반쯤 읽은 상태를 남기지 않는 일은 입구가 맡는다. `SW_DECLARE_STATE_IO` 의 `readState` 가 사본에 읽고 끝까지 맞으면 옮긴다(`readAtomic`).
     *            사본을 만들 수 없는 형식과 프레임마다 도는 경로만 `SW_DECLARE_STATE_IO_IN_PLACE` 를 쓴다 — 그때는 형식이 지역 변수에 읽어 원자성을 스스로 지킨다.
     *          - 형식 안의 형식은 `io.value( _child )` 로 부른다. 바깥 사본 하나로 원자성이 끝나 안쪽은 사본을 또 만들지 않는다.
     *
     *          바이트 약속: 칸은 `Archive` 의 `<<` · `>>` 그대로 놓이고 개수는 uint32 다. 이름은 길이가 붙은 글자이고 빈 이름은 빈 글이다.
     *          손으로 `writeState` · `readState` 를 짝지어 쓰는 모양은 린트(`Scripts/lint/state_shape.py`)가 막는다.
     */
    class SW_API StateIO final
    {
    public:
        /** @brief 방향은 @p archive 의 모드가 정합니다(`Archive::isReadMode`). */
        explicit StateIO( Archive& archive )
            : _pArchive( &archive )
            , _pSourceArchive( &archive )
            , _pUpgradedArchive{}
            , _bReading{ static_cast<uint8>( archive.isReadMode() ? SW_TRUE : SW_FALSE ) }
        {
        }
        /** @brief 판을 올려 읽었으면(`header`) 올린 바이트의 오류와 남은 바이트를 원래 아카이브에 돌려줍니다 — 부르는 쪽의 `getRemainingBytes` · `isOk` 검사가 그대로 맞는다. */
        ~StateIO();
        StateIO( const StateIO& )            = delete;
        StateIO& operator=( const StateIO& ) = delete;

        bool isReading() const { return _bReading == SW_TRUE; }
        bool isWriting() const { return _bReading == SW_FALSE; }
        bool isOk() const { return _pArchive->isOk(); }
        /** @brief 코덱이 따로 있는 칸(넷 코덱 바이트, 리플렉션 직렬화)이 아카이브를 직접 볼 때 씁니다. `header` 가 판을 올리면 올린 바이트로 바뀌므로 `header` 뒤에 받습니다. */
        Archive& getArchive() { return *_pArchive; }

        /** @brief 칸 하나를 씁니다 · 읽습니다. `serializeState` 가 있는 형식은 그것을 부르고, 나머지는 `Archive` 연산자가 받는 형식이어야 합니다. 열거는 `enumValue` 나 `valueAs` 로 저장 폭을 적습니다. */
        template <typename T>
        void value( T& inoutValue )
        {
            static_assert( std::is_enum_v<T> == false, "열거는 enumValue 또는 valueAs 로 저장 폭을 적는다" );
            if constexpr ( StateIOInternal::HasSerializeState<T>::value )
            {
                inoutValue.serializeState( *this );
            }
            else if constexpr ( std::is_base_of_v<DeterministicRandom32, T> )
            {
                valueRandom( inoutValue ); // 파생 형식(`GameRandom`)은 템플릿이 겹지기 전에 기반으로 넘긴다
            }
            else if ( isReading() )
            {
                *_pArchive >> inoutValue;
            }
            else
            {
                *_pArchive << static_cast<const T&>( inoutValue );
            }
        }
        /** @brief 이름입니다. 길이가 붙은 글자로 놓입니다. */
        void value( hashed_string& inoutName );
        /** @brief 글자입니다. 길이가 붙은 글자로 놓입니다. */
        void value( string& inoutText );
        void value( int2& inoutValue );
        /** @brief 남은 시간을 그대로 놓습니다. 음수 늦음까지 있어야 복원한 뒤 같은 발생 시각이 나옵니다. NaN 은 거절합니다. */
        void value( Countdown& inoutCountdown );
        /** @brief 고정 걸음 타이머의 남은 시간만 놓습니다. 걸음과 상한은 설정이 정하므로 @p inoutTimer 는 설정된 타이머여야 합니다. 음수와 한 프레임 상한을 넘는 값을 거절합니다. */
        void value( FixedStepTimer& inoutTimer );
        /** @brief 아직 정수가 되지 않은 누적 양입니다. 0..1 밖을 거절합니다. */
        void value( RateAccumulator& inoutAccumulator );

        /** @brief 형식 안의 형식이지만 읽기가 그 형식만 사본에 받아 끝까지 맞으면 옮깁니다. 바깥이 사본 없는 판(`SW_DECLARE_STATE_IO_IN_PLACE`)일 때 씁니다. */
        template <typename T>
        void valueAtomic( T& inoutValue )
        {
            if ( isReading() )
            {
                (void)readAtomic( inoutValue, *_pArchive ); // 실패는 아카이브 오류에 남는다
                return;
            }
            inoutValue.serializeState( *this );
        }

        /** @brief 저장 폭이 @p TStored 인 칸입니다(`int32` 칸을 `uint8` 로 놓거나 범위를 보지 않는 열거). 읽은 값의 범위는 부르는 쪽이 봅니다. */
        template <typename TStored, typename T>
        void valueAs( T& inoutValue )
        {
            TStored stored = static_cast<TStored>( inoutValue );
            value( stored );
            if ( isReading() )
            {
                inoutValue = static_cast<T>( stored );
            }
        }

        /** @brief 저장 폭이 @p TStored 인 열거입니다. @p last 를 넘는 값을 거절합니다. */
        template <typename TStored = uint8, typename TEnum>
        void enumValue( TEnum& inoutValue, TEnum last )
        {
            TStored stored = static_cast<TStored>( inoutValue );
            value( stored );
            if ( isReading() && check( stored <= static_cast<TStored>( last ) ) )
            {
                inoutValue = static_cast<TEnum>( stored );
            }
        }

        /** @brief 0 · 1 깃발 하나입니다. 다른 값을 거절합니다. 비트필드는 `SW_STATE_BITFLAG` 를 씁니다. */
        template <typename T>
        void flag( T& inoutFlag )
        {
            value( inoutFlag );
            check( static_cast<uint8>( inoutFlag ) <= SW_TRUE );
        }

        /** @brief 고정된 값(형식 판, 설정이 정한 개수)입니다. 읽을 때 같지 않으면 거절합니다. */
        template <typename T>
        void expect( const T& expected )
        {
            T stored = expected;
            value( stored );
            check( stored == expected );
        }

        /**
         * @brief 형식 표(4 글자 태그)와 판입니다. 읽을 때 표가 다르면 거절합니다.
         * @details 읽은 판이 @p version 보다 옛것이면 판 올림 사슬(`SW_STATE_FORMAT_UPGRADE` — 표마다 n → n+1 단계)로 머리 뒤 바이트를 올린 뒤 이어 읽습니다.
         *          단계가 빠졌거나 실패하거나 새 판(내리기)이면 오류를 남기고 거절합니다(옛 판을 그대로 읽는 폴백은 없다). 올린 바이트는 이 입구가 들고,
         *          그 뒤의 칸(바깥 형식의 남은 칸 포함 — 단계가 그대로 옮긴다)은 거기서 읽힌다. 언리얼 `FCustomVersion` 의 판 분기를 등록된 순수 함수 사슬로 둔 자리다.
         *          @p headerlessVersion 은 머리표 없이 쓰던 마지막 판이다(0 이면 처음부터 머리가 있었다). 주면 표가 다른 바이트를 그 판의 머리 없는 칸으로 읽어 올린다 —
         *          형식에 `kStateHeaderlessVersion` 을 두고 넘긴다(구간은 이런 형식을 구간 판으로 올리지 않는다 — 안쪽 머리가 올린다). 주지 않았는데 표가 다르면
         *          판을 알 수 없다는 경고(`StateIO.HeaderTagMismatch`)를 남기고 거절한다.
         */
        void header( uint32 tag, uint32 version, uint32 headerlessVersion = 0 );

        /** @brief 읽는 중이고 @p bCondition 이 거짓이면 거절합니다. 지금까지 맞으면 true 입니다. 쓰는 중에는 늘 true 입니다. */
        bool check( bool bCondition );
        /** @brief 읽을 때 남은 바이트가 없어야 합니다. 상태 하나가 아카이브 하나를 차지하는 최상위 형식이 끝에 부릅니다. */
        void end() { check( _pArchive->getRemainingBytes() == 0 ); }
        /** @brief 지금 읽기 · 쓰기를 실패로 남깁니다. */
        void fail() { _pArchive->setError(); }

        /** @brief 개수 하나입니다. 읽을 때 원소마다 적어도 @p minBytesPerElement 바이트라 남은 바이트로 담을 수 없는 개수를 거절합니다. @p inoutCount 는 쓸 값을 넣어 부릅니다. */
        void count( uint32 minBytesPerElement, uint32& inoutCount );

        /** @brief 개수 + 원소들입니다. 읽을 때 목록을 비우고 개수만큼 기본값으로 만든 뒤 원소마다 @p element 를 부릅니다. 거절되면 그 자리에서 멈춥니다. */
        template <typename T, typename TElement>
        void list( vector<T>& inoutList, uint32 minBytesPerElement, TElement&& element )
        {
            uint32 elementCount = static_cast<uint32>( inoutList.size() );
            count( minBytesPerElement, elementCount );
            if ( isOk() == false )
            {
                return;
            }
            if ( isReading() )
            {
                inoutList.clear();
                inoutList.resize( elementCount );
            }
            for ( T& entry : inoutList )
            {
                element( entry );
                if ( isOk() == false )
                {
                    return;
                }
            }
        }
        /** @brief 정수 · 이름 같은 값의 목록입니다. */
        template <typename T>
        void list( vector<T>& inoutList )
        {
            constexpr bool bLengthPrefixed = std::is_same_v<T, hashed_string> || std::is_same_v<T, string>;
            list( inoutList, bLengthPrefixed ? uint32( sizeof( uint32 ) ) : uint32( sizeof( T ) ), [this]( T& entry )
            { value( entry ); } );
        }

        /** @brief 이름 목록입니다. 읽을 때 하나씩 @p isValid( id ) 로 보고 뒤에 붙이므로, @p isValid 는 지금까지 붙은 목록(겹침, 앞 목록과의 관계)을 볼 수 있습니다. */
        template <typename TValid>
        void nameList( vector<hashed_string>& inoutList, TValid&& isValid )
        {
            uint32 nameCount = static_cast<uint32>( inoutList.size() );
            count( sizeof( uint32 ), nameCount );
            if ( isOk() == false )
            {
                return;
            }
            if ( isReading() )
            {
                inoutList.clear();
                inoutList.reserve( nameCount );
            }
            for ( uint32 index = 0; index < nameCount; ++index )
            {
                hashed_string id = isWriting() ? inoutList[index] : hashed_string{};
                value( id );
                if ( isReading() )
                {
                    if ( isOk() == false || check( isValid( id ) ) == false )
                    {
                        return;
                    }
                    inoutList.push_back( id );
                }
            }
        }

        /** @brief 정의 포인터 ↔ id 입니다. 없으면 빈 이름으로 놓입니다. 읽을 때 @p find( id ) 가 찾지 못하면 거절합니다. */
        template <typename TDef, typename TFind>
        void reference( const TDef*& inoutDef, TFind&& find )
        {
            hashed_string id = inoutDef != nullptr ? inoutDef->_id : hashed_string{};
            value( id );
            if ( isReading() )
            {
                inoutDef = id.empty() ? nullptr : find( id );
                check( id.empty() || inoutDef != nullptr );
            }
        }

        /**
         * @brief 표와 판, 길이가 붙은 본문 구간 하나입니다. 자리가 정해진 구간에 씁니다. 본문이 아니라 표나 판이 다르면 거절합니다.
         * @details 쓸 때는 @p body 를 새 아카이브에 받아 길이를 붙이고, 읽을 때는 구간을 잘라 @p body 에 넘긴다. 본문 안의 거절은 바깥 아카이브에 옮겨진다.
         */
        template <typename TBody>
        void section( uint32 tag, uint32 version, TBody&& body )
        {
            if ( isReading() )
            {
                Archive sectionBody;
                if ( readExpectedSection( *_pArchive, tag, version, sectionBody ) == false )
                {
                    fail();
                    return;
                }
                bool bBodyRead = false;
                {
                    StateIO bodyIO( sectionBody );
                    body( bodyIO );
                    bBodyRead = bodyIO.isOk();
                } // 판을 올려 읽었으면 남은 바이트가 이때 본문에 돌아온다
                check( bBodyRead && sectionBody.isOk() && sectionBody.getRemainingBytes() == 0 );
                return;
            }
            Archive sectionBody;
            StateIO bodyIO( sectionBody );
            body( bodyIO );
            writeSection( *_pArchive, tag, version, sectionBody );
        }

        /** @brief @p object 의 `serializeState` 로 씁니다. `serializeState` 는 쓸 때 값을 바꾸지 않으므로 const 를 벗겨도 안전합니다(언리얼 `Serialize` 도 const 가 아닌 함수 하나다). */
        template <typename T>
        static void writeObject( const T& object, Archive& outArchive )
        {
            StateIO io( outArchive );
            const_cast<T&>( object ).serializeState( io );
        }
        /** @brief 사본에 읽고 끝까지 맞으면 옮깁니다. 실패하면 @p object 는 그대로입니다. */
        template <typename T>
        [[nodiscard]] static bool readAtomic( T& object, Archive& archive )
        {
            T       restored = object;
            StateIO io( archive );
            restored.serializeState( io );
            if ( io.isOk() == false )
            {
                return false;
            }
            object = std::move( restored );
            return true;
        }
        /** @brief @p object 에 바로 읽습니다. 원자성은 그 형식의 `serializeState` 가 스스로 지킵니다. */
        template <typename T>
        [[nodiscard]] static bool readInPlace( T& object, Archive& archive )
        {
            StateIO io( archive );
            object.serializeState( io );
            return io.isOk();
        }

        // 아래는 바이트 약속의 낮은 층이다. `serializeState` 는 위의 칸 함수를 쓰고, 이것은 입구 자신과 일부러 꾸민 바이트를 만드는 테스트, 섹션을 표로 갈라 읽는 쪽이 쓴다.
        static void writeHeader( Archive& outArchive, uint32 tag, uint32 version );
        /** @brief 머리를 읽고 @p tag 와 @p version 이 같으면 true 입니다. */
        [[nodiscard]] static bool readHeader( Archive& archive, uint32 tag, uint32 version );
        /**
         * @brief 구간 하나를 씁니다. 표, 버전, 본문 길이(uint32), 본문 순입니다. 읽는 쪽은 모르는 표를 길이로 건너뜁니다.
         * @details 상태 하나에 키트와 기반 상태 여럿을 실을 때 쓴다(`SessionStateComponent`, 키트 여럿을 든 디렉터).
         *          한 구간의 형식이 바뀌어도 다른 구간은 그대로 읽히고, 빠진 구간은 읽는 쪽이 새 판으로 둔다.
         *          @p body 는 쓰기 모드로 다 쓴 아카이브이다.
         */
        static void writeSection( Archive& outArchive, uint32 tag, uint32 version, const Archive& body );
        /**
         * @brief `writeSection` 이 쓴 구간 하나의 머리를 읽고 본문을 @p outBody 로 자릅니다. @p outBody 는 읽기 전용 보기이며 @p archive 의 바이트를 가리킵니다.
         * @return 머리가 잘렸거나 길이가 남은 바이트를 넘으면 false 이며, @p archive 에 오류를 남긴다.
         */
        [[nodiscard]] static bool readSection( Archive& archive, uint32& outTag, uint32& outVersion, Archive& outBody );
        /**
         * @brief 표가 @p tag 이고 버전이 @p version 인 구간 하나를 읽어 본문을 @p outBody 로 자릅니다. 다른 표이거나 올릴 수 없는 판이면 false 입니다.
         * @details 자리가 정해진 구간(디렉터가 든 키트 상태)은 이것으로 읽는다 — 표와 버전을 부르는 쪽이 따로 견주면 하나를 빼먹는다(버전을 안 봐 옛 판을 새 판으로 읽었다).
         *          옛 판 구간은 `upgradeSectionBody` 로 올린 본문(@p outBody 가 소유)을 돌려준다. 표로 갈라 읽는 쪽(`SessionStateComponent`)만 `readSection` 을 쓴다.
         */
        [[nodiscard]] static bool readExpectedSection( Archive& archive, uint32 tag, uint32 version, Archive& outBody );
        /**
         * @brief 표 @p tag 의 판 @p fromVersion 구간 본문 @p inoutBody 를 판 @p toVersion 까지 올립니다(`SW_STATE_FORMAT_UPGRADE` 사슬 — `header` 와 같은 표). 같은 판이면 그대로 true 입니다.
         * @details 올리면 @p inoutBody 는 올린 바이트를 소유한 읽기 아카이브가 된다. 단계가 빠졌거나 실패하면 오류를 남기고 false 이며 @p inoutBody 는 그대로다.
         *          구간에 실린 상태의 단계는 구간 머리 뒤 바이트(그 상태가 쓴 바이트 전부 — 안쪽 머리가 있으면 그것까지)를 받는다.
         */
        [[nodiscard]] static bool upgradeSectionBody( uint32 tag, uint32 fromVersion, uint32 toVersion, Archive& inoutBody );

        static void               writeName( Archive& outArchive, const hashed_string& name );
        [[nodiscard]] static bool readName( Archive& archive, hashed_string& outName );

        /** @brief id 목록 하나(개수 + id 들)를 씁니다. `readIDList` 가 읽습니다. 64비트 id(묶은 id)는 `uint64` 목록 버전을 씁니다. */
        static void writeIDList( Archive& outArchive, const vector<uint32>& listID );
        /** @brief `writeIDList` 가 쓴 id 목록 하나를 읽습니다. 개수가 남은 바이트를 넘으면 false 입니다. */
        [[nodiscard]] static bool readIDList( Archive& archive, vector<uint32>& outListID );
        /** @brief `writeIDList` 의 `uint64` 버전입니다. */
        static void writeIDList( Archive& outArchive, const vector<uint64>& listID );
        /** @brief `readIDList` 의 `uint64` 버전입니다. */
        [[nodiscard]] static bool readIDList( Archive& archive, vector<uint64>& outListID );

        static void writeInt2( Archive& outArchive, const int2& value );
        static void readInt2( Archive& archive, int2& outValue );

        static void               writeRandom( Archive& outArchive, const DeterministicRandom32& random );
        [[nodiscard]] static bool readRandom( Archive& archive, DeterministicRandom32& outRandom );

        static void               writeStepTimer( Archive& outArchive, const FixedStepTimer& timer );
        [[nodiscard]] static bool readStepTimer( Archive& archive, FixedStepTimer& inoutTimer );

        static void               writeCountdown( Archive& outArchive, const Countdown& countdown );
        [[nodiscard]] static bool readCountdown( Archive& archive, Countdown& outCountdown );
        static void               writeRateAccumulator( Archive& outArchive, const RateAccumulator& accumulator );
        [[nodiscard]] static bool readRateAccumulator( Archive& archive, RateAccumulator& outAccumulator );

    private:
        /** @brief 난수 상태입니다. 0 은 있을 수 없는 상태라 거절합니다. */
        void valueRandom( DeterministicRandom32& inoutRandom );

        /** @brief 바이트 @p inoutBytes 를 표 @p tag 의 판 @p fromVersion 에서 @p toVersion 까지 올립니다(판 올림 사슬). 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool upgradeBytes( uint32 tag, uint32 fromVersion, uint32 toVersion, vector<uint8>& inoutBytes );
        /** @brief 아카이브에서 나머지 바이트를 모두 읽어 @p outBytes 에 담습니다(남은 것이 없으면 빈다). */
        static void readRemainingBytes( Archive& archive, vector<uint8>& outBytes );

    private:
        Archive*            _pArchive;         ///< 지금 읽고 쓰는 아카이브 — 처음엔 받은 것, 판을 올리면 `_pUpgradedArchive`
        Archive*            _pSourceArchive;   ///< 받은 아카이브(빌림)
        unique_ptr<Archive> _pUpgradedArchive; ///< `header` 가 올린 바이트(올리지 않았으면 nullptr)
        uint8               _bReading : 1;
    };
} // namespace sw

/**
 * @def SW_DECLARE_STATE_IO
 * @brief 클래스 안에 `serializeState` 선언과 호출부가 부르는 `writeState` · `readState` 를 둡니다. `readState` 는 사본에 읽고 끝까지 맞으면 옮깁니다(복사 · 이동 대입이 되어야 합니다).
 * @details 호출부(`ComponentStateStore`, 테스트, 다른 형식)는 이름을 그대로 부르고, 본문은 `serializeState` 하나뿐이다.
 */
#define SW_DECLARE_STATE_IO()                               \
    void writeState( ::sw::Archive& outArchive ) const      \
    {                                                       \
        ::sw::StateIO::writeObject( *this, outArchive );    \
    }                                                       \
    [[nodiscard]] bool readState( ::sw::Archive& archive )  \
    {                                                       \
        return ::sw::StateIO::readAtomic( *this, archive ); \
    }                                                       \
    void serializeState( ::sw::StateIO& io )

/**
 * @def SW_DECLARE_STATE_IO_IN_PLACE
 * @brief `SW_DECLARE_STATE_IO` 의 사본 없는 판입니다. 사본을 만들 수 없는 형식(참조 칸, 컴포넌트)과 사본 비용이 문제인 경로만 씁니다. `serializeState` 가 지역 변수에 읽어 끝까지 맞으면 대입합니다.
 */
#define SW_DECLARE_STATE_IO_IN_PLACE()                       \
    void writeState( ::sw::Archive& outArchive ) const       \
    {                                                        \
        ::sw::StateIO::writeObject( *this, outArchive );     \
    }                                                        \
    [[nodiscard]] bool readState( ::sw::Archive& archive )   \
    {                                                        \
        return ::sw::StateIO::readInPlace( *this, archive ); \
    }                                                        \
    void serializeState( ::sw::StateIO& io )

/** @brief 비트필드 깃발 하나입니다(`uint8 _bX : 1` 은 참조로 받을 수 없다). 0 · 1 밖을 거절합니다. */
#define SW_STATE_BITFLAG( io, field )                      \
    do                                                     \
    {                                                      \
        ::uint8 stateFlag = static_cast<::uint8>( field ); \
        ( io ).flag( stateFlag );                          \
        if ( ( io ).isReading() )                          \
        {                                                  \
            field = stateFlag;                             \
        }                                                  \
    } while ( false )
