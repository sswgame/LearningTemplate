/**
 * @file AppearanceSocketRig.h
 * @brief 해석된 외형의 소켓 이름 공간 — 몸과 장비 부품의 소켓 에셋을 한 표(`ResolvedSocketTable`)로 풀고, 부품을 붙일 자리를 계산합니다.
 * @details 유닛 0 이 몸(접두어 없음), 그 뒤가 소켓 에셋을 가진 부품(접두어 = 칸 · 꾸미기 매개변수 이름 — `MainHand.Muzzle`)입니다. 외형의 소켓 덮어쓰기
 *          (`_listSocketOverride`)는 몸 소켓 위에 외형 층으로 얹습니다. 같은 이름은 다시 지어도 같은 번호라 무기를 바꾸면 같은 `MainHand.Muzzle` 번호가
 *          새 무기의 총구를 가리킵니다. 씬 · 컴포넌트를 모르는 값 타입이라 시험에서 그대로 돕니다 — 외형 컴포넌트가 이것을 들고 오브젝트에 붙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/CharacterGeometry.h"
#include "Engine/Character/ResolvedSocketTable.h"
#include "Engine/Character/SocketSet.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AppearancePlacement;
    struct ResolvedAppearance;

    /** @brief 소켓 에셋을 경로로 찾는 창구입니다(시험은 메모리 표, 게임은 리소스 캐시). */
    class SW_GF_API IAppearanceSocketSource
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
     * @brief 리소스에서 소켓 에셋을 한 번 읽어 두는 기본 구현입니다. 종류는 엔진 기본 표(`engine/character/default.socketkinds.xml`)에 대조합니다.
     * @details 읽지 못한 경로도 기억해 두고 다시 읽지 않습니다(오류 로그는 한 번). 데이터를 고친 뒤 다시 읽으려면 `clear` 합니다.
     */
    class SW_GF_API AppearanceSocketSetCache final : public IAppearanceSocketSource
    {
    public:
        AppearanceSocketSetCache();
        ~AppearanceSocketSetCache() override;

        const SocketSet* findSocketSet( const hashed_string& path ) override;
        /** @brief 읽어 둔 것을 모두 버립니다. */
        void clear();

    private:
        unordered_map<hashed_string, unique_ptr<SocketSet>> _mapSocketSet; ///< 값이 null 이면 읽지 못한 경로
        SocketKindTable                                     _kinds;
        uint8                                               _bKindsLoaded : 1;
        [[maybe_unused]] uint8                              _reserved     : 7;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AppearanceSocketRig
     * @brief 해석된 외형 하나의 소켓 표 · 유닛 본 배열입니다. `rebuild` 가 표를 다시 짓고, `computePlacement` 가 부품의 자리를 냅니다.
     */
    class SW_GF_API AppearanceSocketRig
    {
    public:
        /** @brief 몸 유닛 번호입니다. */
        static constexpr uint32 kBodyUnit = 0;
        /** @brief 소켓 에셋이 없는 부품의 유닛 번호입니다. */
        static constexpr uint32 kNoUnit = 0xFFFFFFFFu;

        AppearanceSocketRig();

        /**
         * @brief 해석 결과로 표를 다시 짓습니다. 몸 소켓은 몸 부품(주인이 빈 첫 스킨드 부품)의 소켓 에셋 + 외형의 소켓 덮어쓰기이고, 부품 소켓은 소켓 에셋을 가진
         *        부품마다 한 유닛(본 하나 "root" — 강체)입니다.
         * @param bodyBindBones 몸의 바인드 본 — 소켓의 부모 본 이름을 대조합니다(없는 본은 오류).
         * @return 소켓 에셋을 읽지 못했거나 부모 본이 없으면 false 입니다(그래도 나머지 유닛은 들어갑니다). 오류 글은 @p pOutError 에 더합니다.
         */
        bool rebuild( const ResolvedAppearance& resolved, const CharacterBoneArray& bodyBindBones, IAppearanceSocketSource& source, string* pOutError );

        /** @brief 부품(해석 결과의 `_listPart` 번호)의 유닛 번호입니다. 소켓 에셋이 없으면 `kNoUnit` 입니다. */
        uint32 findPartUnit( uint32 partIndex ) const;
        /**
         * @brief 부품 배치(소켓 후보 + 오프셋 · 회전)를 붙을 유닛 공간 변환으로 계산합니다. 후보 중 이번 해석에 켜진 첫 소켓을 씁니다.
         * @param bodyBones 몸의 지금 본(모델 칸) — 몸 소켓이면 이것으로, 부품 소켓이면 그 부품의 강체 본으로 계산합니다.
         * @param outUnitIndex 소켓이 속한 유닛(붙을 쪽 — 몸이면 `kBodyUnit`)입니다.
         * @return 켜진 후보가 없으면 false 입니다.
         */
        bool computePlacement( const AppearancePlacement& placement, const CharacterBoneArray& bodyBones, float4x4& outInUnit, uint32& outUnitIndex ) const;
        /**
         * @brief 이름(접두어 포함)의 소켓 월드 변환입니다. 몸은 @p bodyBones 와 @p listUnitWorld[0], 부품은 그 유닛의 월드 행렬로 계산합니다.
         * @param listUnitWorld 유닛 번호 순서의 월드 행렬입니다(빠진 유닛의 소켓은 찾지 못합니다).
         */
        bool findSocketWorldTransform( const hashed_string& fullName, const CharacterBoneArray& bodyBones, vector_reference<const float4x4> listUnitWorld,
                                       float4x4& outWorldTransform ) const;

        /** @brief 해석된 소켓 표입니다. */
        const ResolvedSocketTable& getTable() const { return _table; }
        /** @brief 유닛 수(몸 포함)입니다. */
        uint32 getUnitCount() const { return static_cast<uint32>( _listUnitPart.size() ); }
        /** @brief 유닛의 부품 번호입니다(몸 유닛도 몸 부품 번호, 몸 부품이 없으면 `kNoUnit`). */
        uint32 getUnitPart( uint32 unitIndex ) const { return unitIndex < _listUnitPart.size() ? _listUnitPart[unitIndex] : kNoUnit; }

        /** @brief 배치의 로컬 변환(오프셋 · 도 단위 "피치 요 롤" 회전)입니다. */
        static float4x4 makePlacementTransform( const AppearancePlacement& placement );

    private:
        /** @brief 몸 소켓 에셋에 외형의 소켓 덮어쓰기를 얹습니다. 부모 후보 중 몸에 있는 본(또는 이미 있는 소켓의 부모)을 고릅니다. */
        static void applySocketOverrides( const ResolvedAppearance& resolved, const CharacterBoneArray& bodyBindBones, SocketSet& inoutBodySockets );

    private:
        ResolvedSocketTable _table;
        CharacterBoneArray  _rigidBones;   ///< 강체 부품 유닛의 본("root" 하나, 단위 변환)
        vector<uint32>      _listUnitPart; ///< 유닛 → 부품 번호
        vector<uint32>      _listPartUnit; ///< 부품 → 유닛 번호(`kNoUnit` 이면 소켓 없음)
    };
} // namespace sw
