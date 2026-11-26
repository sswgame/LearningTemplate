/**
 * @file ScopedScratchInstance.h
 * @brief 임시 스크래치 인스턴스를 만들고 범위를 벗어나면 파괴합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/SchemaMigrate.h"

namespace sw
{
    /**
     * @struct ScopedScratchInstance
     * @brief 임시 스크래치 인스턴스를 만들고, 범위를 벗어나면 반드시 파괴하는 RAII 래퍼입니다.
     */
    struct ScopedScratchInstance
    {
        explicit ScopedScratchInstance( const TypeInfo* pTypeInfo )
            : _pTypeInfo{ pTypeInfo }
            , _listStorage{}
            , _pInstance{ ( pTypeInfo != nullptr && pTypeInfo->_size > 0 ) ? createScratchInstance( *pTypeInfo, _listStorage ) : nullptr }
        {
        }

        explicit ScopedScratchInstance( const TypeInfo& typeInfo )
            : ScopedScratchInstance( &typeInfo )
        {
        }

        ~ScopedScratchInstance()
        {
            if ( _pInstance != nullptr && _pTypeInfo != nullptr )
                destroyScratchInstance( _pInstance, *_pTypeInfo );
        }

        ScopedScratchInstance( const ScopedScratchInstance& )                = delete;
        ScopedScratchInstance& operator=( const ScopedScratchInstance& )     = delete;
        ScopedScratchInstance( ScopedScratchInstance&& ) noexcept            = delete;
        ScopedScratchInstance& operator=( ScopedScratchInstance&& ) noexcept = delete;

        void* get() const { return _pInstance; }
        bool  isValid() const { return _pInstance != nullptr; }

    private:
        const TypeInfo* _pTypeInfo{ nullptr };
        vector<uint8>   _listStorage;
        void*           _pInstance{ nullptr };
    };
} // namespace sw
