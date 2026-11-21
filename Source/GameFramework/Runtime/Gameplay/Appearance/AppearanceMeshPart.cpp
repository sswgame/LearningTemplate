#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceMeshPart.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/DevCounter.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/list.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Log/Logger.h"
#include "Core/String/formatString.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Automation/Scenario/AutomationStageReset.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/CharacterLODSettings.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Character/Fit/FitPartDef.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

namespace sw
{
    namespace
    {
        struct AppearanceMeshAssemblyInternal
        {
            /**
             * @brief 베이크한 메시 캐시의 항목 하나입니다. 결과와 원본을 강한 참조로 쥔다 — 수명은 그리는 유닛(씬)이 아니라 캐시의 예산이다.
             * @details 예전에는 약한 참조라 씬을 바꾸면 결과가 사라지고, 원본 메시도 내려가 다시 읽히면 내용 토큰(키)이 달라져 같은 차림을 다시 지었다(Debug 에서 차림 하나에 수 초).
             */
            struct BakedEntry
            {
                vector<shared_ptr<Mesh>> _listMesh{};          ///< 구운 메시. 원본을 그리는 부품은 nullptr(`_listBaked` 가 가른다)
                vector<uint8>            _listBaked{};         ///< 그 자리를 구웠는가
                vector<shared_ptr<Mesh>> _listSource{};        ///< 키에 섞은 원본 메시 — 쥐어 두어 다음 씬도 같은 원본 객체(같은 내용 토큰 → 같은 키)를 받는다
                vector<IdentityToken>    _listSourceContent{}; ///< 구울 때의 원본 내용 토큰 — 원본이 다시 읽혀(핫 리로드) 토큰이 바뀌면 낡은 항목이다
                uint64                   _byteCount{ 0 };      ///< 구운 메시와 쥔 원본의 정점 · 인덱스 바이트(예산 계산 — 원본을 여러 항목이 나누면 겹쳐 센다)
                list<uint64>::iterator   _itKeyByUse{};        ///< 표의 `_listKeyByUse` 에서 이 항목의 자리(앞이 가장 오래 안 쓴 것)
            };

            /** @brief 원본마다 지금 내용 토큰이 구울 때와 같으면 true 입니다(다시 읽힌 원본의 낡은 결과를 내주지 않는다). */
            static bool isCurrent( const BakedEntry& entry )
            {
                for ( size_t index = 0; index < entry._listSource.size(); ++index )
                {
                    const shared_ptr<Mesh>& source = entry._listSource[index];
                    const bool              bSame  = source != nullptr ? source->getContentID() == entry._listSourceContent[index] : entry._listSourceContent[index].isValid() == false;
                    if ( bSame == false )
                        return false;
                }
                return true;
            }

            /** @brief 캐시 말고 다른 쪽(그리는 유닛)이 구운 메시를 쥐고 있으면 true 입니다 — 빼도 메모리가 줄지 않는다. */
            static bool isHeldOutside( const BakedEntry& entry )
            {
                for ( const shared_ptr<Mesh>& mesh : entry._listMesh )
                {
                    if ( mesh != nullptr && mesh.use_count() > 1 )
                        return true;
                }
                return false;
            }

            /** @brief 메시의 정점 · 인덱스 바이트입니다(LOD 포함). nullptr 이면 0 입니다. */
            static uint64 computeMeshBytes( const Mesh* pMesh )
            {
                if ( pMesh == nullptr )
                    return 0;
                return static_cast<uint64>( pMesh->getDrawVertexCount() ) * sizeof( RHIVertex ) + static_cast<uint64>( pMesh->getDrawIndexCount() ) * sizeof( uint32 );
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
                utf8 arrMessage[constant::buffer::kMax512]{};
                formatstring( arrMessage, constant::buffer::kMax512, format, std::forward<Args>( args )... );
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
                    if ( weight._weight != 0.0f && mesh.findMorphTargetIndex( weight._morph ).has_value() )
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
                    const optional<uint32> regionIndex = tables.findRegionIndex( region );
                    if ( regionIndex.has_value() == false )
                    {
                        appendReportFormat( inoutReport, "hidden region '%#' is not in the body's fit tables", region.c_str() );
                        bValid = false;
                        continue;
                    }
                    listHiddenIndex.push_back( static_cast<uint16>( *regionIndex ) );
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
         * @brief 베이크한 메시 캐시의 표입니다(키 → 구운 메시 · 원본). 예산(`CharacterMeshCacheConfig::_budgetMegabytes`)을 넘으면 그리는 쪽이 없는 항목을 가장 오래 안 쓴 것부터 뺀다.
         *        `AppearanceBakedMeshCache` 창구만 부릅니다.
         * @details GameFramework 의 모듈 서브시스템이다(`SW_MODULE_SUBSYSTEM( Engine, … )`) — 엔진 범위 아래 GameFramework 의 자식 범위에 서고 `game::findSubsystem` 으로 찾힌다.
         *          예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 게임 서비스가 없는 실행(테스트의 손수 만든 컴포넌트)에는 표가 없어 매번 굽는다.
         *          언리얼 `UEngineSubsystem` 이 메시 머지 결과를 나누는 자리.
         */
        class AppearanceBakedMeshTable final : public Subsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "AppearanceBakedMeshTable";

            mutex                                                                       _mutex;
            unordered_map<uint64, AppearanceMeshAssemblyInternal::BakedEntry> _mapEntry SW_GUARDED_BY( _mutex );
            uint64 _cachedBytes                                                         SW_GUARDED_BY( _mutex ){ 0 }; ///< 항목 바이트의 합
            list<uint64> _listKeyByUse                                                  SW_GUARDED_BY( _mutex );      ///< 쓴 순서(앞이 가장 오래 안 씀) — 넣기 · 꺼내기가 끝으로 옮긴다(`AssetPinSet` · `AudioClipCache` 와 같은 모양)
            DevCounter                                                                  _hitCount;                    ///< `tryGet` 이 내준 횟수(감시 값만 읽는 개발 전용 칸 — 원자라 잠금 밖에서도 된다)

            // Subsystem — 세울 것이 없다(처음 구운 차림부터 채운다). 예산은 남길 때 읽는다 — 모듈 서브시스템 초기화 때는 게임 서비스가 아직 걸리지 않았을 수 있다.
            SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
            void                shutdownSubsystem() override
            {
                ScopedLock<mutex> lock{ _mutex };
                _mapEntry.clear(); // 메시를 엔진 서비스보다 먼저 놓는다
                _listKeyByUse.clear();
                _cachedBytes = 0;
            }
            const utf8* getSubsystemName() const override { return kSubsystemName; }

            /** @brief 구운 메시를 모두 놓습니다 — 자동화 단계 사이(`SW_AUTOMATION_STAGE_RESET`)에 새 프로세스처럼 비운다. */
            void clear()
            {
                ScopedLock<mutex> lock{ _mutex };
                _mapEntry.clear();
                _listKeyByUse.clear();
                _cachedBytes = 0;
                _hitCount.reset();
            }

            /** @brief @p entry 를 가장 최근에 쓴 것으로 옮깁니다(목록 끝). */
            void markUsedLocked( const AppearanceMeshAssemblyInternal::BakedEntry& entry ) SW_REQUIRES_LOCK( _mutex )
            {
                _listKeyByUse.splice( _listKeyByUse.end(), _listKeyByUse, entry._itKeyByUse );
            }

            /** @brief 항목을 표와 쓴 순서 목록에서 빼고 바이트 합을 줄입니다. */
            void eraseLocked( unordered_map<uint64, AppearanceMeshAssemblyInternal::BakedEntry>::iterator it ) SW_REQUIRES_LOCK( _mutex )
            {
                _cachedBytes -= it->second._byteCount;
                _listKeyByUse.erase( it->second._itKeyByUse );
                _mapEntry.erase( it );
            }

            /** @brief @p budgetBytes 를 넘는 동안 그리는 쪽이 없는 항목을 가장 오래 안 쓴 것부터 뺍니다. @p keepKey 는 방금 넣은 항목이다. */
            void evictOverBudgetLocked( uint64 keepKey, uint64 budgetBytes ) SW_REQUIRES_LOCK( _mutex )
            {
                // 쓴 순서 목록을 앞(가장 오래 안 쓴 것)부터 한 번 걷는다 — 그리는 쪽이 있는 것만 건너뛴다.
                auto itKey = _listKeyByUse.begin();
                while ( _cachedBytes > budgetBytes && itKey != _listKeyByUse.end() )
                {
                    const auto it = _mapEntry.find( *itKey );
                    SW_ASSERT( it != _mapEntry.end() );
                    ++itKey; // 빼기 전에 다음으로 — `eraseLocked` 가 지금 자리를 지운다
                    const bool bEvictable = it->first != keepKey && AppearanceMeshAssemblyInternal::isHeldOutside( it->second ) == false;
                    if ( bEvictable )
                        eraseLocked( it );
                }
                // 다 걸었는데도 넘으면 남은 것은 모두 그리는 중이다 — 예산을 넘긴 채 둔다
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AppearanceMeshPart::assemble( const ResolvedAppearance& resolved, vector_reference<const AppearanceMeshPart> listPart, const FitTables* pTables,
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
            if ( pTables == nullptr )
            {
                Internal::appendReportFormat( outReport, "part '%#' has fit data but the body has no fit tables", part._name.c_str() );
                bValid = false;
                continue;
            }
            CharacterIssueList nameIssues;
            if ( part._pFitData->validateNames( *pTables, nameIssues ) == false )
            {
                Internal::appendReportFormat( outReport, "part '%#' fit data %#", part._name.c_str(), nameIssues.joined().c_str() );
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
    hashed_string AppearanceMeshPart::makeSectionName( const ResolvedPart& part )
    {
        if ( part._owner.empty() )
            return part._partName;
        StringBuilder<constant::buffer::kMax256> name;
        name.appendFormat( "%#.%#", part._owner.c_str(), part._partName.c_str() );
        return hashed_string( name.view() );
    }

    MeshMergePart AppearanceMeshPart::makeMergePart( const ResolvedPart& part, const Mesh& mesh, bool bBody )
    {
        MeshMergePart merge{ makeSectionName( part ), part._material, &mesh };
        for ( const AppearanceSectionDef& section : part._listSection ) // 변형 · 규칙을 반영한 구간 머티리얼 — 병합해도 구간마다 머티리얼이 남는다
        {
            StringBuilder<constant::buffer::kMax256> mergedName;
            if ( bBody == false )
                mergedName.appendFormat( "%#/%#", merge._name.c_str(), section._name.c_str() );
            const hashed_string name = bBody ? section._name : hashed_string( mergedName.view() );
            merge._listSection.push_back( MeshMergeSectionPick{ section._name, name, section._material.key() } );
        }
        return merge;
    }

    void AppearanceMeshPart::appendLODRules( const ResolvedPart& part, const MeshMergePart& merge, vector<CharacterLODSectionRule>& inoutListRule )
    {
        if ( merge._listSection.empty() )
        {
            inoutListRule.push_back( CharacterLODSectionRule{ merge._name, part._lodDetail, part._lodDropLevel } );
            return;
        }
        for ( size_t index = 0; index < merge._listSection.size() && index < part._listSection.size(); ++index )
        {
            const AppearanceSectionDef& section = part._listSection[index]; // `makeMergePart` 가 같은 순서로 조각을 만들었다
            const float32               detail  = section._lodDetail != 1.0f ? section._lodDetail : part._lodDetail;
            const uint32                drop    = section._lodDropLevel != 0 ? section._lodDropLevel : part._lodDropLevel;
            inoutListRule.push_back( CharacterLODSectionRule{ merge._listSection[index]._name, detail, drop } );
        }
    }

    expected<shared_ptr<Mesh>> AppearanceMeshPart::mergeParts( vector_reference<const MeshMergePart> listMergePart, const CharacterLODSettings& lodSettings,
                                                               const vector<CharacterLODSectionRule>& listLODRule )
    {
        expected<MeshMergeResult> result = CharacterMeshUtil::mergeMeshes( listMergePart, nullptr );
        if ( result.has_value() == false )
            return unexpected( result.error() );
        // 합친 메시의 LOD 는 여기서 한 번 만든다(조립한 메시라 임포트 LOD 가 없다). 만들지 못하면 buildLODs 가 오류를 남기고 LOD 0 만 그린다.
        (void)CharacterLODSettings::buildLODs( *result->_mesh, lodSettings, listLODRule );
        return std::move( result->_mesh );
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
        if ( found == pTable->_mapEntry.end() )
            return false;
        if ( Internal::isCurrent( found->second ) == false )
        {
            pTable->eraseLocked( found ); // 원본이 다시 읽혔다 — 낡은 결과를 내주지 않고 치운다
            return false;
        }
        pTable->_hitCount.add();
        pTable->markUsedLocked( found->second );
        outListMesh = found->second._listMesh;
        return true;
    }

    void AppearanceBakedMeshCache::store( uint64 key, const vector<shared_ptr<Mesh>>& listMesh, const vector<shared_ptr<Mesh>>& listSource, const vector<IdentityToken>& listSourceContent )
    {
        using Internal                         = AppearanceMeshAssemblyInternal;
        AppearanceBakedMeshTable* const pTable = game::findSubsystem<AppearanceBakedMeshTable>();
        if ( pTable == nullptr )
            return; // 게임 서비스가 없는 실행 — 기억할 곳이 없다
        // 예산이 없으면 캐시가 끝없이 자란다 — 기본값으로 메우지 않고 남기지 않는다(오류가 설정 빠짐을 드러낸다).
        const CharacterMeshCacheConfig& config = game::getSubsystem<const CharacterMeshCacheConfig>();
        if ( config._budgetMegabytes == 0 )
        {
            SW_LOG_ERROR_ID( "AppearanceBakedMeshTable.MissingBudget", "EngineConfig _characterMeshCache._budgetMegabytes must be > 0 - add it to Config/Engine/EngineConfig.json" );
            return;
        }
        const uint64 budgetBytes = static_cast<uint64>( config._budgetMegabytes ) * constant::memory::kBytesPerMegabyte;
        SW_ASSERT( listSource.size() == listSourceContent.size() );
        Internal::BakedEntry entry;
        for ( const shared_ptr<Mesh>& mesh : listMesh )
        {
            entry._listMesh.push_back( mesh );
            entry._listBaked.push_back( mesh != nullptr ? SW_TRUE : SW_FALSE );
            entry._byteCount += Internal::computeMeshBytes( mesh.get() );
        }
        for ( const shared_ptr<Mesh>& source : listSource )
        {
            entry._byteCount += Internal::computeMeshBytes( source.get() );
        }
        entry._listSource        = listSource;
        entry._listSourceContent = listSourceContent;
        ScopedLock<mutex> lock{ pTable->_mutex };
        if ( const auto found = pTable->_mapEntry.find( key ); found != pTable->_mapEntry.end() )
        {
            pTable->_cachedBytes -= found->second._byteCount; // 같은 차림을 둘이 지었다 — 뒤의 것으로 바꾼다
            pTable->markUsedLocked( found->second );
            entry._itKeyByUse = found->second._itKeyByUse;
        }
        else
        {
            entry._itKeyByUse = pTable->_listKeyByUse.insert( pTable->_listKeyByUse.end(), key );
        }
        pTable->_cachedBytes += entry._byteCount;
        pTable->_mapEntry[key] = std::move( entry );
        pTable->evictOverBudgetLocked( key, budgetBytes );
    }

    AppearanceBakedMeshCounters AppearanceBakedMeshCache::readCounters()
    {
        AppearanceBakedMeshCounters     counters;
        AppearanceBakedMeshTable* const pTable = game::findSubsystem<AppearanceBakedMeshTable>();
        if ( pTable == nullptr )
            return counters;
        ScopedLock<mutex> lock{ pTable->_mutex };
        counters._hitCount    = pTable->_hitCount.get();
        counters._cachedBytes = pTable->_cachedBytes;
        counters._entryCount  = static_cast<uint32>( pTable->_mapEntry.size() );
        return counters;
    }
} // namespace sw

namespace sw
{
    SW_MODULE_SUBSYSTEM( Engine, AppearanceBakedMeshTable, AppearanceBakedMeshTable );

    namespace
    {
        /** @brief 단계 되돌리기 — 앞 단계가 구운 차림을 다음 단계가 그대로 쓰면 에셋 일이 빨리 끝나 사진 시점이 혼자 돈 실행과 달라진다(shooter3d killcam 16 대 22 프레임). */
        void resetAppearanceBakedMeshes()
        {
            AppearanceBakedMeshTable* const pTable = game::findSubsystem<AppearanceBakedMeshTable>();
            if ( pTable != nullptr )
                pTable->clear();
        }
    } // namespace

    SW_AUTOMATION_STAGE_RESET( appearanceBakedMeshes, "AppearanceBakedMeshTable", &resetAppearanceBakedMeshes );
} // namespace sw
