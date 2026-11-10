/**
 * @file ReflectionMetadataBox.h
 * @brief 리플렉션 메타데이터를 정의 없이 값처럼 드는 상자와 생성 코드의 함수 형식입니다. 메타데이터 정의는 `ReflectionMetadata.h` 에 있습니다.
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/TextAddress.h"
#include "Engine/Reflection/TypeCastHeader.h"

namespace sw
{
    struct FixedCodec;
    struct ReflectEventOps;
    struct ReflectTypeOps;

    class ValidationContext;

    /** @brief `PROPERTY( Validate = fn )` 과 `REFLECT( Validate = fn )` 의 검증 함수 타입입니다. 생성 코드가 이 함수를 만듭니다. */
    using ReflectValidateFunction = void ( * )( const void* pInstance, ValidationContext& context );
    /** @brief 필드 이름 꼴 범위 경계(`PROPERTY( Min = "-_halfSize" )`)를 인스턴스에서 읽는 함수 타입입니다. 생성 코드가 이 함수를 만듭니다(`PropertyMetadata::_pReadMinRange`). */
    using PropertyRangeReadFunction = float64 ( * )( const void* pInstance );
    /** @brief `PROPERTY( Default = … )` 을 인스턴스의 필드에 넣는 함수 타입입니다. 생성 코드가 칸 타입의 리터럴로 만들어 철자가 틀리면 컴파일 오류입니다(`PropertyMetadata::_pApplyDefault`). */
    using PropertyDefaultApplyFunction = void ( * )( void* pInstance );
    /** @brief `PROPERTY( EditCondition = "…" )` 을 인스턴스 값으로 판정하는 함수 타입입니다. 생성 코드가 컴파일된 식으로 만듭니다(`PropertyMetadata::_pEditCondition`). */
    using PropertyEditConditionFunction = bool ( * )( const void* pInstance );

    /// @brief 함수의 네트워크 역할입니다. 목록은 PredefinedFunctionNetRole.xxx 에 있습니다.
    enum class FunctionNetRole : uint8
    {
#define REGISTER_FUNCTION_NET_ROLE( Name ) Name,
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"

#undef REGISTER_FUNCTION_NET_ROLE
    };

    struct FunctionMetadata;
    struct PropertyMetadata;
    struct TypeMetadata;

    /**
     * @brief 리플렉션 메타데이터(`ReflectionMetadata.h`)를 값처럼 들되, 그 정의를 모르는 TU 에서도 복사 · 이동 · 소멸할 수 있게 하는 상자입니다.
     * @details 속성 키를 더할 때마다 바뀌는 메타데이터 구조를 이 헤더(`GameObject.h` · `Component.h` 를 거쳐 TU 약 1,200 개가 읽는다)에서 빼려는 것이다.
     *          복사와 소멸은 만들 때 적은 함수 표로 하므로 메타데이터 정의가 필요한 것은 기본 생성뿐이다. 기본 생성은 `TypeInfo.cpp` · `PropertyInfo.cpp` · `FunctionInfo.cpp` 의
     *          `TypeInfo` · `PropertyInfo` · `FunctionInfo` · `EventInfo` 생성자만 한다(함수 표 · 기본값이 엔진 모듈에 있어 게임 모듈을 내려도 남는다).
     *          **쓰기 때 복사**다 — 처음에는 공유 기본값(`T` 의 정적 `const` 객체)을 가리키고, `edit()` 가 처음 불릴 때만 제 것을 힙에 짓는다.
     *          적은 것이 없는 프로퍼티(대부분)는 등록에서 힙을 쓰지 않는다(NEXT 5-4 W-R3 a — 등록 할당 타입당 3 이하). 읽기는 `->`(`prop._metadata->_bTransient`),
     *          쓰기는 `edit()`(`prop._metadata.edit()._bTransient = SW_TRUE`)라 쓰는 자리가 형식으로 드러난다. 값 의미다 — 복사는 깊은 복사(공유 기본값이면 공유)다.
     *          이동한 뒤의 원본은 소멸과 대입만 할 수 있다. 칸을 읽는 TU 는 `ReflectionMetadata.h` 를 include 한다.
     */
    template <typename T>
    class ReflectMetadataBox
    {
    public:
        /** @brief 공유 기본값을 가리킵니다. `T` 의 정의가 보여야 합니다. */
        ReflectMetadataBox()
            : _pValue{ &getDefault() }
            , _pDefault{ &getDefault() }
            , _pOps{ &getOps() }
        {
        }
        ~ReflectMetadataBox() { release(); }
        ReflectMetadataBox( const ReflectMetadataBox& other )
            : _pValue{ other.isOwned() ? other._pOps->_pClone( *other._pValue ) : other._pValue }
            , _pDefault{ other._pDefault }
            , _pOps{ other._pOps }
        {
        }
        ReflectMetadataBox( ReflectMetadataBox&& other ) noexcept
            : _pValue{ other._pValue }
            , _pDefault{ other._pDefault }
            , _pOps{ other._pOps }
        {
            other._pValue = nullptr;
        }
        ReflectMetadataBox& operator=( const ReflectMetadataBox& other )
        {
            if ( this != &other )
            {
                const T* pCopy = other.isOwned() ? other._pOps->_pClone( *other._pValue ) : other._pValue;
                release();
                _pValue   = pCopy;
                _pDefault = other._pDefault;
                _pOps     = other._pOps;
            }
            return *this;
        }
        ReflectMetadataBox& operator=( ReflectMetadataBox&& other ) noexcept
        {
            std::swap( _pValue, other._pValue );
            std::swap( _pDefault, other._pDefault );
            std::swap( _pOps, other._pOps );
            return *this;
        }

        const T* operator->() const noexcept { return _pValue; }
        const T& operator*() const noexcept { return *_pValue; }

        /** @brief 고칠 수 있는 메타데이터입니다. 공유 기본값을 가리키고 있으면 제 사본을 지은 뒤 돌려줍니다. */
        T& edit()
        {
            if ( isOwned() == false )
                _pValue = _pOps->_pClone( *_pValue );
            return *const_cast<T*>( _pValue ); // 제 사본만 여기 닿는다(위에서 지었다)
        }

    private:
        /** @brief 정의를 아는 곳(기본 생성)에서 한 번 만드는 복사 · 소멸 함수 표입니다. */
        struct Ops
        {
            T* ( *_pClone )( const T& value );
            void ( *_pDestroy )( const T* pValue );
        };

        static T* cloneValue( const T& value )
        {
            return make_unique<T>( value ).release();
        }
        static void destroyValue( const T* pValue )
        {
            sw_delete( const_cast<T*>( pValue ) );
        }
        static const Ops& getOps()
        {
            static constexpr Ops s_ops{ &cloneValue, &destroyValue };
            return s_ops;
        }
        static const T& getDefault()
        {
            static const T s_default{};
            return s_default;
        }

        bool isOwned() const noexcept { return _pValue != nullptr && _pValue != _pDefault; }
        void release() noexcept
        {
            if ( isOwned() )
                _pOps->_pDestroy( _pValue );
            _pValue = nullptr;
        }

    private:
        const T*   _pValue;   ///< 공유 기본값이거나 제 사본이다. 이동한 뒤에만 nullptr 이다
        const T*   _pDefault; ///< 기본 생성한 TU 의 공유 기본값(엔진 모듈) — 제 사본인지 가린다
        const Ops* _pOps;
    };
} // namespace sw
