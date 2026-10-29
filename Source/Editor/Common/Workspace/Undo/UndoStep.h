/**
 * @file Editor/Common/Workspace/Undo/UndoStep.h
 * @brief 되돌리기 · 다시 하기 단계 하나입니다 — **데이터**(부를 함수의 이름 해시 + 인자 바이트)라 편집기 모듈을 리로드해도 기록이 넘어간다.
 * @details 언리얼 `FTransaction` 이 오브젝트 상태를 직렬화해 기록하듯, 단계는 코드가 아니라 값이다. 실행은 이름 해시로 처리기 표(`UndoStepRegistry`)에서
 *          함수를 찾아 인자를 읽어 부른다. 함수는 처음 쓰일 때 저절로 표에 오르고(`UndoStepFunction<&F>`), 그 함수가 든 DLL 이 내려가면 빠진다 —
 *          새 DLL 이 올라오면 같은 이름으로 다시 오른다. 이름은 함수의 정규 이름(시그니처 문자열)이라 함수 이름 · 인자 형을 바꾸면 옛 기록의 그 단계는
 *          찾지 못해 건너뛴다(로그).
 *
 *          패널 인스턴스에 묶인 대상(미리보기 창의 위젯 · 열린 문서 버퍼)은 리로드 뒤에 없으므로 코드 단계(`_transient`)를 허용한다 — 기록을 맡길 때
 *          그런 단계를 가진 명령은 버린다(`CommandStack::save`).
 *
 *          인자 형: 산술 · 열거형 · 단순 복사 구조체(바이트 그대로), `string`, 그 둘의 `vector`. 그 밖의 형은 같은 이름의 `writeUndoArg` · `readUndoArg`
 *          겹지정을 그 형 옆에 둔다(`ObjectSnapshot`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Serialization/Format/Archive.h"

#include <tuple>
#include <type_traits>

namespace sw::editor
{
    /** @brief 단순 복사 형(산술 · 열거형 · 단순 구조체)을 바이트 그대로 씁니다. */
    template <typename T, std::enable_if_t<std::is_trivially_copyable_v<T>, int> = 0>
    void writeUndoArg( Archive& archive, const T& value )
    {
        archive.writeBytes( &value, sizeof( T ) );
    }
    /** @brief 단순 복사 형을 읽습니다. */
    template <typename T, std::enable_if_t<std::is_trivially_copyable_v<T>, int> = 0>
    [[nodiscard]] bool readUndoArg( Archive& archive, T& outValue )
    {
        return archive.readBytes( &outValue, sizeof( T ) );
    }
    /** @brief 글을 씁니다. */
    inline void writeUndoArg( Archive& archive, const string& value )
    {
        archive.writeString( value );
    }
    /** @brief 글을 읽습니다. */
    [[nodiscard]] inline bool readUndoArg( Archive& archive, string& outValue )
    {
        return archive.readString( outValue );
    }
    /** @brief 목록을 씁니다(개수 + 원소). */
    template <typename T>
    void writeUndoArg( Archive& archive, const vector<T>& listValue )
    {
        writeUndoArg( archive, static_cast<uint64>( listValue.size() ) );
        if constexpr ( std::is_trivially_copyable_v<T> )
        {
            if ( listValue.empty() == false )
                archive.writeBytes( listValue.data(), listValue.size() * sizeof( T ) );
        }
        else
        {
            for ( const T& value : listValue )
            {
                writeUndoArg( archive, value );
            }
        }
    }
    /** @brief 목록을 읽습니다. */
    template <typename T>
    [[nodiscard]] bool readUndoArg( Archive& archive, vector<T>& outListValue )
    {
        uint64 count{ 0 };
        if ( readUndoArg( archive, count ) == false )
            return false;
        outListValue.resize( static_cast<size_t>( count ) );
        if constexpr ( std::is_trivially_copyable_v<T> )
        {
            return count == 0 || archive.readBytes( outListValue.data(), count * sizeof( T ) );
        }
        else
        {
            for ( T& value : outListValue )
            {
                if ( readUndoArg( archive, value ) == false )
                    return false;
            }
            return true;
        }
    }
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 인자 바이트를 읽어 함수 하나를 부르는 처리기입니다. 인자를 다 읽지 못하면 false 입니다. */
    using UndoStepInvoker = bool ( * )( Archive& arguments );

    /**
     * @class UndoStepRegistry
     * @brief 이름 해시 → 처리기 표입니다. `UndoStepFunction` 의 등록자만 고친다(정적 초기화 · DLL 언로드).
     */
    class SW_EDITOR_API UndoStepRegistry
    {
    public:
        /** @brief 처리기를 올립니다. 같은 해시가 이미 있으면(다른 DLL 의 같은 함수 — 편집기 확장이 같은 헤더 함수를 쓸 때) 덮지 않고 수만 셉니다. */
        static void add( uint64 functionHash, UndoStepInvoker invoker );
        /** @brief 처리기를 내립니다. 같은 해시를 올린 곳이 남아 있으면 그대로 둡니다. */
        static void remove( uint64 functionHash );
        /** @brief 처리기입니다. 없으면 nullptr 입니다. */
        static UndoStepInvoker find( uint64 functionHash );
    };

    /** @brief `UndoStepFunction` 이 두는 정적 등록자입니다. 만들 때 올리고 사라질 때(그 DLL 이 내려갈 때) 내립니다. */
    struct UndoStepRegistrar
    {
        UndoStepRegistrar( uint64 functionHash, UndoStepInvoker invoker )
            : _functionHash{ functionHash }
        {
            UndoStepRegistry::add( functionHash, invoker );
        }
        ~UndoStepRegistrar() { UndoStepRegistry::remove( _functionHash ); }
        UndoStepRegistrar( const UndoStepRegistrar& )            = delete;
        UndoStepRegistrar& operator=( const UndoStepRegistrar& ) = delete;

        uint64 _functionHash;
    };

    /**
     * @struct UndoStep
     * @brief 단계 하나 — 데이터 단계(함수 해시 + 인자 바이트) 또는 인스턴스에 묶인 코드 단계(`_transient`)입니다.
     */
    struct UndoStep
    {
        uint64           _functionHash{ 0 }; ///< 처리기 이름 해시(0 이면 데이터 단계가 아니다)
        vector<uint8>    _arguments{};       ///< 인자 바이트(`writeUndoArg` 순서)
        Delegate<void()> _transient{};       ///< 인스턴스에 묶인 코드 단계(맡기지 않는다)

        /** @brief 실행할 것이 있으면 true 입니다. */
        bool isBound() const { return _functionHash != 0 || _transient.isBound(); }
        /** @brief 데이터 단계면 true 입니다(리로드 · 파일로 넘길 수 있다). */
        bool isPersistable() const { return _functionHash != 0; }
        /** @brief 코드 단계로 만듭니다. 대상이 패널 인스턴스와 함께 사라질 때만 쓴다(파일 머리말). */
        static UndoStep makeTransient( Delegate<void()> step )
        {
            UndoStep result;
            result._transient = std::move( step );
            return result;
        }
        /** @brief 실행합니다. 데이터 단계의 처리기가 없거나 인자를 읽지 못하면 경고하고 아무것도 하지 않습니다. */
        SW_EDITOR_API void execute() const;
    };

    template <auto TFunction, typename TSignature = decltype( TFunction )>
    struct UndoStepFunction;

    /**
     * @struct UndoStepFunction
     * @brief 정적 함수 `TFunction` 을 데이터 단계로 부르는 틀입니다. `make` 가 처음 쓰일 때 처리기가 표에 오른다.
     */
    template <auto TFunction, typename... TArgs>
    struct UndoStepFunction<TFunction, void ( * )( TArgs... )>
    {
        /** @brief 이 함수의 이름 해시입니다(이 틀의 시그니처 — 함수의 정규 이름이 들어 있다). */
        static uint64 getHash()
        {
            static const uint64 s_hash = HashUtil::computeStringHash64( SW_FUNCTION_SIGNATURE, sizeof( SW_FUNCTION_SIGNATURE ) - 1, false );
            return s_hash;
        }

        /** @brief 인자를 읽어 함수를 부릅니다. */
        static bool invoke( Archive& arguments )
        {
            std::tuple<std::remove_cv_t<std::remove_reference_t<TArgs>>...> tupleArgument{};
            const bool                                                      bRead = std::apply( [&arguments]( auto&... value )
            { return ( readUndoArg( arguments, value ) && ... ); }, tupleArgument );
            if ( bRead == false )
                return false;
            std::apply( TFunction, tupleArgument );
            return true;
        }

        /** @brief 인자를 바이트로 적은 단계를 만듭니다. */
        template <typename... TValues>
        static UndoStep make( const TValues&... values )
        {
            static_assert( sizeof...( TValues ) == sizeof...( TArgs ), "UndoStepFunction::make: argument count differs from the function" );
            (void)s_registrar; // 처음 쓰일 때 표에 오른다(정적 멤버 정의가 이 줄로 인스턴스화된다)
            Archive archive;
            ( writeUndoArg( archive, static_cast<const std::remove_cv_t<std::remove_reference_t<TArgs>>&>( values ) ), ... );
            UndoStep result;
            result._functionHash = getHash();
            result._arguments.assign( archive.getData(), archive.getData() + archive.getSize() );
            return result;
        }

    private:
        static inline const UndoStepRegistrar s_registrar{ getHash(), &invoke };
    };
} // namespace sw::editor

/** @brief 정적 함수 @p Function 을 인자 `...` 로 부르는 데이터 단계입니다(`UndoStep`). 예: `SW_UNDO_STEP( applyWeight, objectID, nodeIndex, before )`. */
#define SW_UNDO_STEP( Function, ... ) ::sw::editor::UndoStepFunction<&Function>::make( __VA_ARGS__ )
