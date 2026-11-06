#include "pch.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Subsystem/SubsystemCollection.h"

namespace sw::engine
{
    namespace
    {
        struct EngineRegistrationListInternal
        {
            /** @brief @p pScope 부터 상위로 올라가며 @p typeHash 를 찾습니다(`locateSubsystem` 의 해시 판). */
            static void* locate( const ISubsystemLocator* pScope, uint64 typeHash )
            {
                for ( ; pScope != nullptr; pScope = pScope->getParentScope() )
                {
                    void* const pFound = pScope->findInScope( typeHash );
                    if ( pFound != nullptr )
                        return pFound;
                }
                return nullptr;
            }

            /** @brief 이름 순으로 앞인지입니다(링크 순서와 무관하게 결정적인 보고 · 동점 순서). */
            static bool isNameBefore( const utf8* pLeft, const utf8* pRight ) { return string_view{ pLeft } < string_view{ pRight }; }

            /** @brief 목록에서 @p typeHash 의 공장 등록입니다. 없으면 nullptr 입니다. */
            static const EngineSubsystemRecord* findSubsystem( const EngineSubsystemRecord* pHead, uint64 typeHash )
            {
                for ( const EngineSubsystemRecord* pRecord = pHead; pRecord != nullptr; pRecord = pRecord->_pNext )
                {
                    if ( pRecord->_typeHash == typeHash )
                        return pRecord;
                }
                return nullptr;
            }
        };
    } // namespace

    EngineServiceRecord::EngineServiceRecord( EngineRegistrationList& list, const utf8* pTypeName, uint64 typeHash, ServiceSlot& slot, bool ( *pfnBind )( SubsystemScope&, void* ),
                                              ServiceRequirement requirement, EngineServiceVisibility visibility )
        : _pTypeName{ pTypeName }
        , _typeHash{ typeHash }
        , _pSlot{ &slot }
        , _pfnBind{ pfnBind }
        , _requirement{ requirement }
        , _visibility{ visibility }
        , _pNext{ list._pServiceHead }
    {
        list._pServiceHead = this;
    }

    EngineSubsystemRecord::EngineSubsystemRecord( EngineRegistrationList& list, const utf8* pTypeName, uint64 typeHash, EngineSubsystemStage stage, span<const uint64> listAfterHash,
                                                  const utf8* pAfterNames, EngineSubsystemFactory pfnFactory )
        : _pTypeName{ pTypeName }
        , _typeHash{ typeHash }
        , _stage{ stage }
        , _listAfterHash{ listAfterHash }
        , _pAfterNames{ pAfterNames }
        , _pfnFactory{ pfnFactory }
        , _pNext{ list._pSubsystemHead }
    {
        list._pSubsystemHead = this;
    }

    EngineRegistrationList::EngineRegistrationList()
        : _pServiceHead{ nullptr }
        , _pSubsystemHead{ nullptr }
    {
    }

    EngineRegistrationList& EngineRegistrationList::getGlobal()
    {
        static EngineRegistrationList s_list;
        return s_list;
    }

    const utf8* EngineRegistrationList::bindServices( const ISubsystemLocator& subsystems )
    {
        for ( const EngineServiceRecord* pRecord = _pServiceHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            pRecord->_pSlot->_pBound = EngineRegistrationListInternal::locate( &subsystems, pRecord->_typeHash );
        }
        return findMissingRequiredService();
    }

    void EngineRegistrationList::unbindServices()
    {
        for ( const EngineServiceRecord* pRecord = _pServiceHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            pRecord->_pSlot->_pBound = nullptr;
        }
    }

    const utf8* EngineRegistrationList::findMissingRequiredService() const
    {
        const utf8* pMissing = nullptr;
        for ( const EngineServiceRecord* pRecord = _pServiceHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            const bool bMissing = pRecord->_requirement == ServiceRequirement::Required && pRecord->_pSlot->_pBound == nullptr;
            if ( bMissing && ( pMissing == nullptr || EngineRegistrationListInternal::isNameBefore( pRecord->_pTypeName, pMissing ) ) )
                pMissing = pRecord->_pTypeName;
        }
        return pMissing;
    }

    void EngineRegistrationList::bindModuleServices( SubsystemScope& outScope, bool bGameModuleOnly, bool bSkipAlreadyBound ) const
    {
        for ( const EngineServiceRecord* pRecord = _pServiceHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            void* const pService  = pRecord->_pSlot->_pBound;
            const bool  bVisible  = bGameModuleOnly == false || pRecord->_visibility == EngineServiceVisibility::GameVisible;
            const bool  bFresh    = bSkipAlreadyBound == false || outScope.findInScope( pRecord->_typeHash ) == nullptr;
            const bool  bBindable = pService != nullptr && bVisible;
            if ( bBindable && bFresh )
                SW_LOW_LEVEL_ASSERT( pRecord->_pfnBind( outScope, pService ), "Engine service %s is bound twice - each registration is a distinct type and the caller passes a fresh scope",
                                     pRecord->_pTypeName );
        }
    }

    bool EngineRegistrationList::orderSubsystems( EngineSubsystemStage stage, vector<const EngineSubsystemRecord*>& outListOrder, string& outError ) const
    {
        outListOrder.clear();
        outError.clear();
        vector<const EngineSubsystemRecord*> listRecord;
        for ( const EngineSubsystemRecord* pRecord = _pSubsystemHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            if ( pRecord->_stage == stage )
                listRecord.push_back( pRecord );
        }
        // 의존 해시를 이름으로 푼다 — 정렬 · 같은 이름 · 순환 판정은 서브시스템 목록과 같은 함수(`SubsystemCollection::computeGraph`)다.
        vector<vector<const utf8*>> listDependencyName( listRecord.size() );
        for ( size_t recordIndex = 0; recordIndex < listRecord.size(); ++recordIndex )
        {
            const EngineSubsystemRecord& record = *listRecord[recordIndex];
            for ( const uint64 afterHash : record._listAfterHash )
            {
                const EngineSubsystemRecord* const pAfter = EngineRegistrationListInternal::findSubsystem( _pSubsystemHead, afterHash );
                if ( pAfter == nullptr )
                {
                    outError = string( "Engine subsystem '" ) + record._pTypeName + "' is built after " + record._pAfterNames + ", but one of them has no registration (SW_ENGINE_SUBSYSTEM)";
                    return false;
                }
                if ( pAfter->_stage != stage )
                {
                    outError = string( "Engine subsystem '" ) + record._pTypeName + "' is built after '" + pAfter->_pTypeName + "', which is built in another stage - list only same-stage types";
                    return false;
                }
                listDependencyName[recordIndex].push_back( pAfter->_pTypeName );
            }
        }
        vector<SubsystemNode> listNode;
        listNode.reserve( listRecord.size() );
        for ( size_t recordIndex = 0; recordIndex < listRecord.size(); ++recordIndex )
        {
            listNode.push_back( { listRecord[recordIndex]->_pTypeName, listDependencyName[recordIndex] } );
        }
        SubsystemGraph graph{};
        if ( SubsystemCollection::computeGraph( listNode, graph, outError ) == false )
        {
            outError = "Engine subsystem build order: " + outError;
            return false;
        }
        outListOrder.reserve( graph._listOrder.size() );
        for ( const uint32 nodeIndex : graph._listOrder )
        {
            outListOrder.push_back( listRecord[nodeIndex] );
        }
        return true;
    }

    void EngineRegistrationList::buildSubsystems( EngineSubsystemStage stage, SubsystemScope& scope, EngineSubsystemBuild& build ) const
    {
        vector<const EngineSubsystemRecord*> listOrder;
        string                               error;
        if ( orderSubsystems( stage, listOrder, error ) == false )
            SW_FATAL( "%#", error.c_str() );
        for ( const EngineSubsystemRecord* const pRecord : listOrder )
        {
            pRecord->_pfnFactory( scope, build );
        }
    }
} // namespace sw::engine
