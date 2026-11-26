/**
 * @file TaskArgs.h
 * @brief 위치 기반 태스크 인자 팩과 인자 하나를 담는 타입 소거 값, 그것을 받는 델리게이트를 정의합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include <initializer_list>

namespace sw
{
    /**
     * @class TaskValue
     * @brief 여러 타입의 값을 런타임에 안전하게 보관하는 타입 소거 컨테이너입니다.
     * @details
     * - SBO(Small Buffer Optimization, 32바이트):
     *   기본형(int32, float32, 포인터 등)과 32바이트 이하의 작은 구조체는 힙 할당(new/delete) 없이 인라인 버퍼(`_arrStorage`)에
     *   바로 저장합니다.
     * - 큰 객체는 힙에 할당:
     *   32바이트를 넘는 객체는 자동으로 힙에 할당하고 포인터로 관리합니다.
     * - 소멸 · 복사 · 이동 VTable:
     *   C++ 가상 함수 대신 함수 포인터 3개로 된 정적 VTable 로 수명을 가볍게 관리합니다.
     * - 타입 안전성:
     *   값은 `getPtr<T>()` 로 읽으며, Debug 빌드에서는 저장 방식(인라인 여부)과 타입 크기를 비교해 잘못된 타입으로 읽으면 바로
     *   assert 합니다.
     */
    class TaskValue
    {
        /** @brief 인라인으로 보관할 수 있는 최대 크기(32바이트)입니다. */
        static constexpr size_t kInlineStorageSize = 32;

        /** @brief 타입 T 가 32바이트 인라인 버퍼에 들어가는지 컴파일 타임에 판별합니다. */
        template <typename T>
        static constexpr bool kIsInline = ( sizeof( T ) <= kInlineStorageSize && alignof( T ) <= alignof( std::max_align_t ) );

        /** @brief 타입별 소멸 · 복사 생성 · 이동 생성을 담당하는 함수 테이블입니다. */
        struct VTable
        {
            void ( *_pDestroy )( void* pStorage );             ///< 소멸자를 부르는 함수
            void ( *_pClone )( const void* pSrc, void* pDst ); ///< 복사 생성 함수
            void ( *_pMove )( void* pSrc, void* pDst );        ///< 이동 생성 함수
            size_t _typeSize{ 0 };
            bool   _bIsInline{ false };
        };

        template <typename T>
        static const VTable* getInlineVtable()
        {
            static constexpr VTable s_pVtable{
                []( void* pStorage )
            {
                reinterpret_cast<T*>( pStorage )->~T();
            },
                []( const void* pSrc, void* pDst )
            {
                sw_placement_new( pDst ) T( *reinterpret_cast<const T*>( pSrc ) );
            },
                []( void* pSrc, void* pDst )
            {
                sw_placement_new( pDst ) T( std::move( *reinterpret_cast<T*>( pSrc ) ) );
                reinterpret_cast<T*>( pSrc )->~T();
            },
                sizeof( T ),
                true };
            return &s_pVtable;
        }

        template <typename T>
        static const VTable* getHeapVtable()
        {
            static constexpr VTable s_pVtable{
                []( void* pStorage )
            {
                T* pPtr = *reinterpret_cast<T**>( pStorage );
                sw_delete( pPtr );
            },
                []( const void* pSrc, void* pDst )
            {
                const T* pSource               = *reinterpret_cast<const T* const*>( pSrc );
                *reinterpret_cast<T**>( pDst ) = make_unique<T>( *pSource ).release();
            },
                []( void* pSrc, void* pDst )
            {
                *reinterpret_cast<T**>( pDst ) = *reinterpret_cast<T**>( pSrc );
                *reinterpret_cast<T**>( pSrc ) = nullptr;
            },
                sizeof( T ),
                false };
            return &s_pVtable;
        }

    public:
        /** @brief 빈 값입니다(저장소 없음). */
        TaskValue() = default;

        ~TaskValue()
        {
            reset();
        }

        TaskValue( const TaskValue& other )
        {
            if ( other._pVtable != nullptr )
            {
                _pVtable = other._pVtable;
                _pVtable->_pClone( other._arrStorage, _arrStorage );
            }
        }

        TaskValue( TaskValue&& other ) noexcept
        {
            if ( other._pVtable != nullptr )
            {
                _pVtable = other._pVtable;
                _pVtable->_pMove( other._arrStorage, _arrStorage );
                other._pVtable = nullptr;
            }
        }

        TaskValue& operator=( const TaskValue& other )
        {
            if ( this != &other )
            {
                reset();
                if ( other._pVtable != nullptr )
                {
                    _pVtable = other._pVtable;
                    _pVtable->_pClone( other._arrStorage, _arrStorage );
                }
            }
            return *this;
        }

        TaskValue& operator=( TaskValue&& other ) noexcept
        {
            if ( this != &other )
            {
                reset();
                if ( other._pVtable != nullptr )
                {
                    _pVtable = other._pVtable;
                    _pVtable->_pMove( other._arrStorage, _arrStorage );
                    other._pVtable = nullptr;
                }
            }
            return *this;
        }

        template <typename T,
                  typename Decayed = std::decay_t<T>,
                  typename         = std::enable_if_t<!std::is_same_v<Decayed, TaskValue>>>
        TaskValue( T&& value )
        {
            if constexpr ( kIsInline<Decayed> )
            {
                _pVtable = getInlineVtable<Decayed>();
                sw_placement_new( _arrStorage ) Decayed( std::forward<T>( value ) );
            }
            else
            {
                _pVtable                                    = getHeapVtable<Decayed>();
                *reinterpret_cast<Decayed**>( _arrStorage ) = make_unique<Decayed>( std::forward<T>( value ) ).release(); // 소유를 저장소의 포인터 칸에 놓는다. 해제는 함수 표(`sw_delete`)
            }
        }

        void reset()
        {
            if ( _pVtable != nullptr )
            {
                _pVtable->_pDestroy( _arrStorage );
                _pVtable = nullptr;
            }
        }

        bool hasValue() const { return _pVtable != nullptr; }

        /** @brief 저장된 값의 포인터를 반환합니다. 타입이 맞지 않으면 Debug 에서 assert 합니다. */
        template <typename T>
        const T* getPtr() const
        {
            using Stored = std::decay_t<T>;
            if ( _pVtable == nullptr )
                return nullptr;
#if defined( SW_DO_CHECKS )
            SW_ASSERT( _pVtable->_pDestroy != nullptr );
            SW_ASSERT( _pVtable->_bIsInline == kIsInline<Stored> );
            SW_ASSERT( _pVtable->_typeSize == sizeof( Stored ) );
#endif
            if constexpr ( kIsInline<Stored> )
                return reinterpret_cast<const Stored*>( _arrStorage );
            else
                return *reinterpret_cast<const Stored* const*>( _arrStorage );
        }

        /**
         * @brief 타입을 묻지 않고 값의 주소를 돌려줍니다. 비었으면 nullptr 입니다.
         * @details 담긴 타입을 다른 길(리플렉션 타입 이름 · `EnumInfo::_size`)로 아는 쪽만 씁니다 — 크기 검사가 없습니다.
         */
        const void* getRawPtr() const
        {
            if ( _pVtable == nullptr )
                return nullptr;
            if ( _pVtable->_bIsInline )
                return _arrStorage;
            return *reinterpret_cast<const void* const*>( _arrStorage );
        }

        /** @brief 담긴 값의 크기(`sizeof`)입니다. 비었으면 0 입니다. */
        size_t getStoredSize() const { return ( _pVtable != nullptr ) ? _pVtable->_typeSize : 0; }

        template <typename T>
        /** @brief 값을 복사해 반환합니다. 값이 없으면 defaultValue 입니다. */
        T getValue( const T& defaultValue = T{} ) const
        {
            const T* pVal = getPtr<T>();
            if ( pVal != nullptr )
                return *pVal;
            return defaultValue;
        }

    private:
        const VTable* _pVtable{ nullptr };
        alignas( std::max_align_t ) std::byte _arrStorage[kInlineStorageSize]{};
    };

    /**
     * @class TaskArgs
     * @brief 위치 기반 태스크 인자 팩입니다.
     * @details MakeTaskArgs<T0,T1,...>(...) 로 만들고 get<T0>(0), get<T1>(1), ... 로 읽습니다.
     */
    class TaskArgs
    {
    public:
        /** @brief 빈 인자 팩입니다. */
        TaskArgs() = default;

        /** @brief 가변 인자로 값을 채웁니다. */
        template <typename... Args, typename = std::enable_if_t<( sizeof...( Args ) > 0 )>>
        explicit TaskArgs( Args&&... args )
        {
            _listValue.reserve( sizeof...( Args ) );
            ( _listValue.emplace_back( std::forward<Args>( args ) ), ... );
        }

        /**
         * @brief 값 목록으로 채웁니다(`TaskArgs{ a, b }`).
         * @note 반복자 쌍은 소괄호로 넘긴다. `_listValue{ begin, end }` 면 vector 의 initializer_list 생성자가 이겨 반복자
         *       (`const TaskValue*`) 둘이 그대로 TaskValue 두 개로 담긴다(`Style/IteratorPairBraces` 가 막는다).
         */
        TaskArgs( std::initializer_list<TaskValue> listValue )
            : _listValue( listValue.begin(), listValue.end() ) {}

        template <typename T>
        /** @brief 값을 하나 추가합니다. */
        void add( T&& val )
        {
            _listValue.emplace_back( std::forward<T>( val ) );
        }

        /** @brief 인자 개수를 반환합니다. */
        uint32 getCount() const { return static_cast<uint32>( _listValue.size() ); }

        /** @brief index 번째 인자를 반환합니다. */
        const TaskValue& get( uint32 index ) const { return _listValue[index]; }

        template <typename T>
        const T* getPtr( uint32 index ) const
        {
            if ( index < _listValue.size() )
                return _listValue[index].getPtr<T>();
            return nullptr;
        }

        template <typename T>
        /** @brief index 번째 인자를 T 로 꺼내 반환합니다. 없으면 defaultVal 입니다. */
        T get( uint32 index, const T& defaultVal = T{} ) const
        {
            const T* pVal = getPtr<T>( index );
            if ( pVal != nullptr )
                return *pVal;
            return defaultVal;
        }

    private:
        vector<TaskValue> _listValue;
    };

    /** @brief 타입 목록을 명시해 TaskArgs 를 만듭니다. */
    template <typename... Ts>
    TaskArgs MakeTaskArgs( Ts... values )
    {
        TaskArgs args;
        ( args.add( std::move( values ) ), ... );
        return args;
    }

    /** @brief 위치 기반 TaskArgs 를 받는 태스크 델리게이트입니다. */
    using TaskArgsDelegate = Delegate<void( const TaskArgs& args )>;
} // namespace sw
