/**
 * @file ResolvedSocketTable.h
 * @brief 해석된 외형의 소켓 표 — 몸 소켓과 장착 부품 소켓을 한 이름 공간(부품 것은 슬롯 이름이 앞에: `MainHand.Muzzle`)으로 내보냅니다.
 * @details 외형을 해석할 때마다(`beginResolve` → 유닛마다 `addUnit` → `endResolve`) 다시 짓지만 **이름 → 번호(`SocketId`)는 유지**합니다. 무기를
 *          바꿔도 `MainHand.Muzzle` 은 같은 번호이고 새 무기의 총구를 가리키므로, 붙어 있던 이펙트 · 사운드 · 게임 코드가 끊기지 않습니다. 이번 해석에
 *          없는 이름은 번호를 지우지 않고 꺼 둡니다(`isSocketActive`). 후보 목록(`SocketDef::_listFallback`)은 `endResolve` 가 풉니다.
 *
 *          변환은 유닛 공간입니다 — 유닛(애니메이션 단위 하나 = GameObject 하나)의 본 배열로 계산하고, 월드가 필요하면 유닛의 월드 행렬을 곱합니다
 *          (`getSocketTransform` 이 `SocketPoseView` 로 한 번에). 본 기준 소켓은 본 비율 보정이 옮긴 본을 그대로 따르고, 표면 기준 소켓
 *          (`SocketAnchor::Surface`)은 `applyShapedGeometry` 가 계산한 체형 보정을 더합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/Fit/SurfaceTransfer.h"
#include "Engine/Character/Socket/SocketSet.h"

namespace sw
{
    struct AppearanceGeometry;
    struct CharacterBoneArray;

    /** @brief 해석된 소켓 표의 번호입니다. 같은 표에서 같은 이름은 해석을 다시 해도 같은 번호입니다. */
    using SocketId = uint32;
    /** @brief 없는 소켓 번호입니다. */
    inline constexpr SocketId kInvalidSocketId = 0xFFFFFFFFu;

    /**
     * @brief 유닛마다의 본 배열 · 월드 행렬 묶음입니다(유닛 번호로 찾음). 부르는 동안만 쓰는 뷰입니다.
     * @details 2D 유닛도 같습니다 — 본이 Z 축으로만 돌 뿐입니다.
     */
    struct SocketPoseView
    {
        vector_reference<const CharacterBoneArray* const> _listUnitBones{};
        vector_reference<const float4x4>                  _listUnitWorld{};
    };
} // namespace sw

namespace sw
{
    /** @brief 해석된 외형 하나의 소켓 표입니다. */
    class SW_API ResolvedSocketTable
    {
    public:
        ResolvedSocketTable();

        /** @brief 해석을 시작합니다. 모든 소켓을 끄고 유닛 목록을 비웁니다(번호는 남김). */
        void beginResolve();
        /**
         * @brief 유닛 하나의 (층을 합친) 소켓 에셋을 더합니다.
         * @param prefix 슬롯 이름(`MainHand`)입니다. 비면 몸(접두어 없음)이고, 있으면 `prefix.이름` 으로 나옵니다.
         * @param unitIndex `SocketPoseView` 에서 이 유닛을 찾을 번호입니다.
         * @param bindBones 바인드(레퍼런스) 포즈의 본 배열입니다. 부모 본 이름을 여기에 대조합니다(없는 본은 오류, false).
         * @param pBindGeometry 있으면 표면 기준 소켓을 이 형상의 가장 가까운 삼각형에 묶습니다. 없으면 표면 기준도 본을 따릅니다.
         */
        [[nodiscard]] bool addUnit( const hashed_string& prefix, uint32 unitIndex, const SocketSet& sockets, const CharacterBoneArray& bindBones,
                                    const AppearanceGeometry* pBindGeometry, string* pOutError );
        /** @brief 후보 목록을 풉니다. 이것을 부른 뒤부터 이번 해석의 표가 보입니다. */
        void endResolve();

        /**
         * @brief 유닛의 체형을 건 형상으로 표면 기준 소켓의 보정을 다시 계산합니다(바인드 형상과 같은 위상이어야 합니다).
         * @details 체형 모프로 배가 나오면 허리 소켓이 앞으로 갑니다. 본 기준 소켓은 본 비율 보정이 옮긴 본을 따르므로 여기서 할 일이 없습니다.
         */
        void applyShapedGeometry( uint32 unitIndex, const AppearanceGeometry& shapedGeometry );

        /** @brief 이름(접두어 포함)의 번호입니다. 한 번도 나온 적 없는 이름이면 `kInvalidSocketId` 입니다(꺼진 소켓은 번호가 있습니다). */
        SocketId findSocket( const hashed_string& fullName ) const;
        /** @brief 후보 중 지금 켜진 첫 소켓입니다(외형 데이터의 "벨트 걸이 → 허리" 같은 목록). 없으면 `kInvalidSocketId` 입니다. */
        SocketId findFirstActiveSocket( vector_reference<const hashed_string> listCandidate ) const;
        /** @brief 이번 해석에 있는 소켓이면 true 입니다. */
        bool isSocketActive( SocketId socketId ) const;
        /** @brief 후보 목록을 따라간 실제 소켓입니다(후보가 없거나 모두 꺼졌으면 자기). 꺼진 소켓이면 `kInvalidSocketId` 입니다. */
        SocketId resolveTarget( SocketId socketId ) const;
        /** @brief 소켓이 속한 유닛 번호입니다. */
        uint32 getSocketUnit( SocketId socketId ) const;
        /** @brief 소켓의 이름(접두어 포함)입니다. */
        const hashed_string& getSocketName( SocketId socketId ) const;
        /** @brief 소켓 정의(층을 합친 것)입니다. 없으면 nullptr 입니다. */
        const SocketDef* findSocketDef( SocketId socketId ) const;
        /** @brief 지금까지 나온 이름 수입니다(꺼진 것 포함). */
        uint32 getSocketCount() const { return static_cast<uint32>( _listSlot.size() ); }

        /**
         * @brief 소켓(후보를 따라간 실제 소켓)의 **유닛 공간** 변환을 그 유닛의 본 배열로 계산합니다.
         * @param unitBones `getSocketUnit( resolveTarget( socketId ) )` 유닛의 지금 본 배열입니다.
         */
        bool computeUnitTransform( SocketId socketId, const CharacterBoneArray& unitBones, float4x4& outUnitTransform ) const;
        /** @brief 이름의 소켓 월드 변환입니다(후보를 따라가고, 그 유닛의 본 · 월드 행렬을 @p pose 에서 찾음). */
        bool getSocketTransform( const hashed_string& fullName, const SocketPoseView& pose, float4x4& outWorldTransform ) const;
        /** @brief 번호의 소켓 월드 변환입니다. */
        bool getSocketTransform( SocketId socketId, const SocketPoseView& pose, float4x4& outWorldTransform ) const;

    private:
        struct Slot
        {
            SocketDef      _def{};
            hashed_string  _fullName{};
            float4x4       _bindParentModel{};
            SurfaceBinding _surfaceBinding{};
            float3         _surfacePointBind{};
            float3         _shapeOffsetLocal{};
            uint32         _unit{ 0 };
            uint32         _unitSlot{ 0 };
            SocketId       _redirect{ kInvalidSocketId };
            uint8          _bActive{ SW_FALSE };
        };

        struct Unit
        {
            SocketSet     _sockets{};
            hashed_string _prefix{};
            uint32        _unitIndex{ 0 };
        };

        hashed_string makeFullName( const hashed_string& prefix, const hashed_string& name ) const;
        SocketId      findOrAddSlot( const hashed_string& fullName );
        const Unit*   findUnit( uint32 unitIndex ) const;

    private:
        vector<Slot>                           _listSlot;
        vector<Unit>                           _listUnit;
        unordered_map<hashed_string, SocketId> _mapNameToSocket;
    };
} // namespace sw
