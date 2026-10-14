/**
 * @file UIScopedVariable.h
 * @brief UI 테스트가 전역 bool 변수(`gv_uiReduceMotion` 등)를 잠깐 바꾸는 도우미입니다(스위트 아님).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"

#include "TestFramework/TestFramework.h"

/** @brief 전역 bool 변수 하나를 이름으로 바꾸고 스코프 끝에 되돌립니다(테스트 DLL 은 Engine 의 gv_* 를 extern 으로 못 읽는다). */
class ScopedBoolVariable
{
public:
    ScopedBoolVariable( const utf8* pName, bool bValue )
        : _pInfo{ sw::engine::getSubsystem<sw::GlobalVariableManager>().findVariable( pName ) }
        , _bPrevious{ false }
    {
        SW_EXPECT_TRUE( _pInfo != nullptr );
        if ( _pInfo == nullptr )
            return;
        _bPrevious = _pInfo->getValueAsBool();
        (void)_pInfo->setValueAsBool( bValue );
    }

    ~ScopedBoolVariable()
    {
        if ( _pInfo != nullptr )
            (void)_pInfo->setValueAsBool( _bPrevious );
    }

    ScopedBoolVariable( const ScopedBoolVariable& )            = delete;
    ScopedBoolVariable& operator=( const ScopedBoolVariable& ) = delete;

private:
    sw::GlobalVariableInfo* _pInfo;
    bool                    _bPrevious;
};
