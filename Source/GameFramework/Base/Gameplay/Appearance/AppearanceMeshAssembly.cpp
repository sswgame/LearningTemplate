#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceMeshAssembly.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Container/unordered_map.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Character/Fit/FitPartDef.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"

namespace sw
{
    namespace
    {
        struct AppearanceMeshAssemblyInternal
        {
            /** @brief 베이크한 메시 캐시의 항목 하나입니다. 원본을 그리는 부품(nullptr)은 표시로 남긴다 — 약한 참조로는 "사라짐" 과 구분되지 않는다. */
            struct BakedEntry
            {
                vector<weak_ptr<Mesh>> _listMesh{};
                vector<uint8>          _listBaked{};
            };

            /** @brief 항목의 베이크한 메시가 모두 살아 있으면 true 입니다. */
            static bool isAlive( const BakedEntry& entry )
            {
                for ( size_t index = 0; index < entry._listMesh.size(); ++index )
                {
                    if ( entry._listBaked[index] == SW_TRUE && entry._listMesh[index].expired() )
                        return false;
                }
                return true;
            }

            static void appendReport( string& inoutReport, string_view message )
            {
                if ( inoutReport.empty() == false )
                    inoutReport += "\n";
                inoutReport += message;
            }

            template <typename... Args>
            static void appendReportFormat( string& inoutReport, string_view format, Args&&... args )
            {
                utf8 arrMessage[constant::kMaxBuffer512]{};
                formatstring( arrMessage, constant::kMaxBuffer512, format, std::forward<Args>( args )... );
                appendReport( inoutReport, arrMessage );
            }

            /** @brief 몸 모프(주인이 빈 것) 위에 그 주인의 모프를 얹은 가중치입니다. 같은 이름이면 주인의 것이 이긴다. */
            static void collectMorphWeights( const ResolvedAppearance& resolved, const hashed_string& owner, vector<BodyMorphWeight>& outListWeight )
            {
                outListWeight.clear();
                for ( const ResolvedMorph& morph : resolved._listMorph )
                {
                    if ( morph._owner.empty() )
                        outListWeight.push_back( BodyMorphWeight{ morph._name, morph._weight } );
                }
                if ( owner.empty() )
                    return;
                for ( const ResolvedMorph& morph : resolved._listMorph )
                {
                    if ( morph._owner != owner )
                        continue;
                    BodyMorphWeight* pExisting = VectorUtil::findBy<&BodyMorphWeight::_morph>( outListWeight, morph._name );
                    if ( pExisting != nullptr )
                        pExisting->_weight = morph._weight;
                    else
                        outListWeight.push_back( BodyMorphWeight{ morph._name, morph._weight } );
                }
            }

            /** @brief 메시에 그 이름의 타깃이 있고 가중치가 0 이 아닌 모프가 하나라도 있으면 true 입니다(구울 것이 있다). */
            static bool hasBakedMorph( const Mesh& mesh, const vector<BodyMorphWeight>& listWeight )
            {
                for ( const BodyMorphWeight& weight : listWeight )
                {
                    if ( weight._weight != 0.0f && mesh.findMorphTargetIndex( weight._morph ) >= 0 )
                        return true;
                }
                return false;
            }

            /**
             * @brief 몸에서 해석의 숨김 영역(몸 수정 `HideRegion`)을 뺍니다. 꼭짓점이 모두 그 영역들에 든 삼각형을 숨깁니다(부품 피팅 데이터의 숨김과 같은 규칙).
             * @return 표에 없는 영역 이름이 있으면 보고에 남기고 false 입니다.
             */
            [[nodiscard]] static bool hideResolvedRegions( const ResolvedAppearance& resolved, const FitTables& tables, const AppearanceGeometry& body,
                                                           const CharacterBoneArray& bindBones, FitPartResult& inoutResult, string& inoutReport )
            {
                if ( resolved._listHiddenRegion.empty() )
                    return true;
                vector<uint16> listHiddenIndex;
                bool           bValid = true;
                for ( const hashed_string& region : resolved._listHiddenRegion )
                {
                    const int32 regionIndex = tables.findRegionIndex( region );
                    if ( regionIndex < 0 )
                    {
                        appendReportFormat( inoutReport, "hidden region '%#' is not in the body's fit tables", region.c_str() );
                        bValid = false;
                        continue;
                    }
                    listHiddenIndex.push_back( static_cast<uint16>( regionIndex ) );
                }
                vector<uint16> listVertexRegion;
                tables.assignRegions( body, bindBones, listVertexRegion );
                vector<uint8> listTriangleHidden( body.getTriangleCount(), SW_FALSE );
                for ( uint32 triangle = 0; triangle < body.getTriangleCount(); ++triangle )
                {
                    bool bAllHidden = true;
                    for ( uint32 corner = 0; corner < 3 && bAllHidden; ++corner )
                    {
                        bAllHidden = VectorUtil::contains( listHiddenIndex, listVertexRegion[body._listIndex[triangle * 3 + corner]] );
                    }
                    listTriangleHidden[triangle] = bAllHidden ? SW_TRUE : SW_FALSE;
                }
                (void)inoutResult.hideTriangles( vector_reference<const uint8>( listTriangleHidden.data(), listTriangleHidden.size() ) );
                return bValid;
            }
        };

        /**
         * @class AppearanceBakedMeshTable
         * @brief 베이크한 메시 캐시의 표입니다(키 → 부품마다의 약한 참조). `AppearanceBakedMeshCache` 창구만 부릅니다.
         * @details GameFramework 의 모듈 서브시스템이다(`SW_MODULE_SUBSYSTEM( Engine, … )`) — 엔진 범위 아래 GameFramework 의 자식 범위에 서고 `game::findSubsystem` 으로 찾힌다.
         *          예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 게임 서비스가 없는 실행(테스트의 손수 만든 컴포넌트)에는 표가 없어 매번 굽는다.
         *          언리얼 `UEngineSubsystem` 이 메시 머지 결과를 나누는 자리.
         */
        class AppearanceBakedMeshTable final : public ISubsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "AppearanceBakedMeshTable";

            // ISubsystem — 세울 것이 없다(처음 구운 차림부터 채운다).
            SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
            void                shutdownSubsystem() override {}
            const utf8*         getSubsystemName() const override { return kSubsystemName; }

            mutex                                                                       _mutex;
            unordered_map<uint64, AppearanceMeshAssemblyInternal::BakedEntry> _mapEntry SW_GUARDED_BY( _mutex );
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AppearanceMeshAssembly::assemble( const ResolvedAppearance& resolved, vector_reference<const AppearanceMeshPart> listPart, const FitTables* pTables,
                                           const CharacterBoneArray& bindBones, vector<shared_ptr<Mesh>>& outListMesh, string& outReport, FitResult* pOutFitResult )
    {
        using Internal = AppearanceMeshAssemblyInternal;
        outListMesh.clear();
        const size_t partCount = listPart.size();

        // 피팅에 들어갈 부품 — 위치로 이은 형상에 그 부품의 체형 모프를 건다(피팅은 체형을 적용한 바인드 공간에서 푼다).
        vector<vector<BodyMorphWeight>> listPartWeight( partCount );
        vector<AppearanceGeometry>      listWelded( partCount );
        vector<AppearanceGeometry>      listMorphed( partCount );
        FitInput                        input;
        vector<uint32>                  listInputPart;
        Internal::collectMorphWeights( resolved, hashed_string{}, input._listMorphWeight );
        input._pBindBones = &bindBones;
        bool bValid       = true;
        for ( size_t partIndex = 0; partIndex < partCount; ++partIndex )
        {
            const AppearanceMeshPart& part = listPart[partIndex];
            Internal::collectMorphWeights( resolved, part._owner, listPartWeight[partIndex] );
            if ( part._pMesh == nullptr )
            {
                Internal::appendReportFormat( outReport, "part '%#' has no mesh", part._name.c_str() );
                bValid = false;
                continue;
            }
            if ( part._pFitData == nullptr )
                continue;
            string nameError;
            if ( pTables == nullptr )
            {
                Internal::appendReportFormat( outReport, "part '%#' has fit data but the body has no fit tables", part._name.c_str() );
                bValid = false;
                continue;
            }
            if ( part._pFitData->validateNames( *pTables, &nameError ) == false )
            {
                Internal::appendReportFormat( outReport, "part '%#' fit data %#", part._name.c_str(), nameError.c_str() );
                bValid = false;
                continue;
            }
            CharacterMeshUtil::makeWeldedGeometry( *part._pMesh, listWelded[partIndex] );
            listMorphed[partIndex] = listWelded[partIndex];
            listMorphed[partIndex].applyMorphs( vector_reference<const BodyMorphWeight>( listPartWeight[partIndex].data(), listPartWeight[partIndex].size() ) );
            input._listPart.push_back( FitPartInput{ part._name, &listMorphed[partIndex], part._pFitData } );
            listInputPart.push_back( static_cast<uint32>( partIndex ) );
        }
        if ( bValid == false )
            return false;

        FitResult result;
        if ( input._listPart.empty() == false )
        {
            const FitSolver solver;
            if ( solver.solve( *pTables, input, result ) == false )
            {
                for ( const string& message : result._report._listMessage )
                {
                    Internal::appendReport( outReport, message );
                }
                return false;
            }
            for ( size_t inputIndex = 0; inputIndex < listInputPart.size(); ++inputIndex )
            {
                const uint32 partIndex = listInputPart[inputIndex];
                if ( listPart[partIndex]._owner.empty() == false )
                    continue;
                if ( Internal::hideResolvedRegions( resolved, *pTables, listMorphed[partIndex], bindBones, result._listPart[inputIndex], outReport ) == false )
                    return false;
            }
        }

        outListMesh.assign( partCount, nullptr );
        for ( size_t partIndex = 0; partIndex < partCount; ++partIndex )
        {
            const AppearanceMeshPart&                     part = listPart[partIndex];
            const vector_reference<const BodyMorphWeight> weights( listPartWeight[partIndex].data(), listPartWeight[partIndex].size() );
            const auto                                    found   = std::find( listInputPart.begin(), listInputPart.end(), static_cast<uint32>( partIndex ) );
            const bool                                    bFitted = found != listInputPart.end();
            if ( bFitted == false && Internal::hasBakedMorph( *part._pMesh, listPartWeight[partIndex] ) == false )
                continue; // 바꿀 것이 없다 — 원본을 그린다
            const FitPartResult* pFit = bFitted ? &result._listPart[static_cast<size_t>( found - listInputPart.begin() )] : nullptr;
            outListMesh[partIndex]    = CharacterMeshUtil::createBakedMesh( *part._pMesh, weights, bFitted ? &listWelded[partIndex] : nullptr, pFit );
            if ( outListMesh[partIndex] == nullptr )
            {
                Internal::appendReportFormat( outReport, "part '%#' could not be baked", part._name.c_str() );
                outListMesh.clear();
                return false;
            }
        }
        if ( pOutFitResult != nullptr )
            *pOutFitResult = std::move( result );
        return true;
    }
} // namespace sw

namespace sw
{
    bool AppearanceBakedMeshCache::tryGet( uint64 key, vector<shared_ptr<Mesh>>& outListMesh )
    {
        using Internal = AppearanceMeshAssemblyInternal;
        outListMesh.clear();
        AppearanceBakedMeshTable* const pTable = game::findSubsystem<AppearanceBakedMeshTable>();
        if ( pTable == nullptr )
            return false; // 게임 서비스가 없는 실행 — 표가 없어 매번 굽는다
        ScopedLock<mutex> lock{ pTable->_mutex };
        const auto        found = pTable->_mapEntry.find( key );
        if ( found == pTable->_mapEntry.end() || Internal::isAlive( found->second ) == false )
            return false;
        for ( const weak_ptr<Mesh>& mesh : found->second._listMesh )
        {
            outListMesh.push_back( mesh.lock() );
        }
        return true;
    }

    void AppearanceBakedMeshCache::store( uint64 key, const vector<shared_ptr<Mesh>>& listMesh )
    {
        using Internal                         = AppearanceMeshAssemblyInternal;
        AppearanceBakedMeshTable* const pTable = game::findSubsystem<AppearanceBakedMeshTable>();
        if ( pTable == nullptr )
            return; // 게임 서비스가 없는 실행 — 기억할 곳이 없다
        ScopedLock<mutex> lock{ pTable->_mutex };
        for ( auto it = pTable->_mapEntry.begin(); it != pTable->_mapEntry.end(); )
        {
            if ( Internal::isAlive( it->second ) )
                ++it;
            else
                it = pTable->_mapEntry.erase( it );
        }
        Internal::BakedEntry entry;
        for ( const shared_ptr<Mesh>& mesh : listMesh )
        {
            entry._listMesh.push_back( mesh );
            entry._listBaked.push_back( mesh != nullptr ? SW_TRUE : SW_FALSE );
        }
        pTable->_mapEntry[key] = std::move( entry );
    }
} // namespace sw

namespace sw
{
    SW_MODULE_SUBSYSTEM( Engine, AppearanceBakedMeshTable, AppearanceBakedMeshTable );
} // namespace sw
