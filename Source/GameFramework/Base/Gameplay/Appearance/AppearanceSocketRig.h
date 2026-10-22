/**
 * @file AppearanceSocketRig.h
 * @brief 해석된 외형의 소켓 이름 공간입니다. 몸과 장비 부품의 소켓 에셋을 한 테이블(`ResolvedSocketTable`)로 합치고, 부품을 붙일 위치를 계산합니다.
 * @details 유닛 0 이 몸(접두어 없음)이고, 그 뒤가 소켓 에셋을 가진 부품이다. 부품의 접두어는 슬롯이나 꾸미기 매개변수 이름이다(`MainHand.Muzzle`). 외형의 소켓 덮어쓰기
 *          (`_listSocketOverride`)는 몸 소켓 위에 외형 레이어로 덮는다. 같은 이름은 다시 만들어도 같은 번호가 나오므로, 무기를 바꾸면 같은 `MainHand.Muzzle` 번호가
 *          새 무기의 총구를 가리킨다. 씬과 컴포넌트를 모르는 값 타입이라 테스트에서 그대로 실행할 수 있다. 외형 컴포넌트가 이 타입을 보관하고 오브젝트에 붙인다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Character/Socket/SocketSet.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AppearancePlacement;
    struct ResolvedAppearance;

    /** @brief 소켓 에셋을 경로로 찾는 인터페이스입니다. 테스트는 메모리 테이블을, 게임은 리소스 캐시를 씁니다. */
    class IAppearanceSocketSource
    {
    public:
        IAppearanceSocketSource()                                            = default;
        virtual ~IAppearanceSocketSource()                                   = default;
        IAppearanceSocketSource( const IAppearanceSocketSource& )            = delete;
        IAppearanceSocketSource& operator=( const IAppearanceSocketSource& ) = delete;

        /** @brief 경로의 소켓 에셋입니다. 없거나 읽지 못했으면 nullptr 입니다. */
        virtual const SocketSet* findSocketSet( const hashed_string& path ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AppearanceSocketSetCache
     * @brief 엔진의 공유 소켓 캐시(`SharedAssetCache<SocketSet>`, 종류는 엔진 기본 테이블과 대조)에서 받은 소켓 에셋을 쥐는 기본 구현입니다.
     * @details 에셋 매니저가 파일을 고치면 공유 캐시가 제자리에서 다시 읽고 다시 읽은 횟수를 올린다(`isStale`). 쥔 쪽은 그때 `clear` 하고 다시 조립한다 —
     *          그래서 외형이 읽는 소켓과 소켓 편집 창 · 소켓 컴포넌트가 읽는 소켓이 한 객체다. 읽지 못한 경로는 기억해 두고 다시 읽지 않으므로 오류 로그는 한 번이다.
     */
    class SW_GF_API AppearanceSocketSetCache final : public IAppearanceSocketSource
    {
    public:
        AppearanceSocketSetCache();
        ~AppearanceSocketSetCache() override;

        const SocketSet* findSocketSet( const hashed_string& path ) override;
        /** @brief 쥔 것을 모두 버리고 지금의 다시 읽은 횟수를 기준으로 삼습니다. */
        void clear();
        /** @brief 쥔 뒤에 공유 캐시가 소켓 에셋을 다시 읽었으면(핫 리로드) true 입니다. */
        bool isStale() const;

    private:
        unordered_map<hashed_string, shared_ptr<const SocketSet>> _mapSocketSet;    ///< 값이 null 이면 읽지 못한 경로이다
        uint32                                                    _seenReloadCount; ///< 마지막 `clear` 때 공유 캐시의 다시 읽은 횟수
    };
} // namespace sw

namespace sw
{
    /**
     * @class AppearanceSocketRig
     * @brief 해석된 외형 하나의 소켓 테이블과 유닛 본 배열입니다. `rebuild` 가 테이블을 다시 만들고, `computePlacement` 가 부품의 위치를 계산합니다.
     */
    class SW_GF_API AppearanceSocketRig
    {
    public:
        /** @brief 몸 유닛의 번호입니다. */
        static constexpr uint32 kBodyUnit = 0;
        /** @brief 소켓 에셋이 없는 부품의 유닛 번호입니다. */
        static constexpr uint32 kNoUnit = invalid_index::kUint32;

        AppearanceSocketRig();

        /**
         * @brief 해석 결과로 테이블을 다시 만듭니다. 몸 소켓은 몸 부품(주인이 비어 있는 첫 스킨드 부품)의 소켓 에셋에 외형의 소켓 덮어쓰기를 더한 것입니다.
         *        부품 소켓은 소켓 에셋을 가진 부품마다 유닛 하나이고, 이 유닛은 본 "root" 하나짜리 강체입니다.
         * @param bodyBindBones 몸의 바인드 본입니다. 소켓의 부모 본 이름을 대조하며, 없는 본은 오류입니다.
         * @return 소켓 에셋을 읽지 못했거나 부모 본이 없으면 false 입니다. 이때도 나머지 유닛은 들어갑니다. 오류 메시지는 @p pOutError 에 추가합니다.
         */
        bool rebuild( const ResolvedAppearance& resolved, const CharacterBoneArray& bodyBindBones, IAppearanceSocketSource& source, string* pOutError );

        /** @brief 부품(해석 결과의 `_listPart` 번호)의 유닛 번호입니다. 소켓 에셋이 없으면 `kNoUnit` 입니다. */
        uint32 findPartUnit( uint32 partIndex ) const;
        /**
         * @brief 부품 배치(소켓 후보, 오프셋, 회전)를 붙을 유닛 공간의 변환으로 계산합니다. 후보 중 이번 해석에서 켜진 첫 소켓을 씁니다.
         * @param bodyBones 몸의 현재 본(모델 공간)입니다. 몸 소켓이면 이 본으로, 부품 소켓이면 그 부품의 강체 본으로 계산합니다.
         * @param outUnitIndex 소켓이 속한 유닛입니다. 붙는 쪽이며, 몸이면 `kBodyUnit` 입니다.
         * @return 켜진 후보가 없으면 false 입니다.
         */
        bool computePlacement( const AppearancePlacement& placement, const CharacterBoneArray& bodyBones, float4x4& outInUnit, uint32& outUnitIndex ) const;
        /**
         * @brief 이름(접두어 포함)의 소켓 월드 변환입니다. 몸은 @p bodyBones 와 @p listUnitWorld[0] 으로, 부품은 그 유닛의 월드 행렬로 계산합니다.
         * @param listUnitWorld 유닛 번호 순서의 월드 행렬입니다. 빠진 유닛의 소켓은 찾지 못합니다.
         */
        bool findSocketWorldTransform( const hashed_string& fullName, const CharacterBoneArray& bodyBones, vector_reference<const float4x4> listUnitWorld,
                                       float4x4& outWorldTransform ) const;

        /** @brief 해석된 소켓 테이블입니다. */
        const ResolvedSocketTable& getTable() const { return _table; }
        /** @brief 몸을 포함한 유닛 수입니다. */
        uint32 getUnitCount() const { return static_cast<uint32>( _listUnitPart.size() ); }
        /** @brief 유닛의 부품 번호입니다. 몸 유닛도 몸 부품 번호이며, 몸 부품이 없으면 `kNoUnit` 입니다. */
        uint32 getUnitPart( uint32 unitIndex ) const { return unitIndex < _listUnitPart.size() ? _listUnitPart[unitIndex] : kNoUnit; }

        /** @brief 배치의 로컬 변환입니다. 오프셋과, 도 단위 "피치 요 롤" 회전으로 이루어집니다. */
        static float4x4 makePlacementTransform( const AppearancePlacement& placement );

    private:
        /** @brief 몸 소켓 에셋에 외형의 소켓 덮어쓰기를 적용합니다. 부모 후보 중 몸에 있는 본(또는 이미 있는 소켓의 부모)을 고릅니다. */
        static void applySocketOverrides( const ResolvedAppearance& resolved, const CharacterBoneArray& bodyBindBones, SocketSet& inoutBodySockets );

    private:
        ResolvedSocketTable _table;
        CharacterBoneArray  _rigidBones;   ///< 강체 부품 유닛의 본("root" 하나, 단위 변환)
        vector<uint32>      _listUnitPart; ///< 유닛에서 부품 번호로의 대응
        vector<uint32>      _listPartUnit; ///< 부품에서 유닛 번호로의 대응(`kNoUnit` 이면 소켓 없음)
    };
} // namespace sw
