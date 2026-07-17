#include "rigidbody.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float PI = 3.14159265358979323846f;
constexpr float PLAYER_MASS = 5.0f;
constexpr float MAP_RESTITUTION = 0.12f;
constexpr float BODY_RESTITUTION = 0.08f;
int s_NextRigidBodyId = 1;
bool s_ShowRigidBodyIds = true;

constexpr unsigned char s_aDigitSegments[10] = {
	0x3f, // 0
	0x06, // 1
	0x5b, // 2
	0x4f, // 3
	0x66, // 4
	0x6d, // 5
	0x7d, // 6
	0x07, // 7
	0x7f, // 8
	0x6f, // 9
};

float Cross(vec2 A, vec2 B)
{
	return A.x * B.y - A.y * B.x;
}

vec2 Perpendicular(vec2 V)
{
	return vec2(-V.y, V.x);
}

vec2 Rotate(vec2 V, float Angle)
{
	const float C = std::cos(Angle);
	const float S = std::sin(Angle);
	return vec2(V.x * C - V.y * S, V.x * S + V.y * C);
}

float ClampMagnitude(float Value, float MaxMagnitude)
{
	return std::clamp(Value, -MaxMagnitude, MaxMagnitude);
}

bool SegmentIntersection(vec2 A, vec2 B, vec2 C, vec2 D, float *pT, vec2 *pHit)
{
	const vec2 R = B - A;
	const vec2 S = D - C;
	const float Denom = Cross(R, S);
	if(absolute(Denom) < 0.00001f)
		return false;
	const float T = Cross(C - A, S) / Denom;
	const float U = Cross(C - A, R) / Denom;
	if(T < 0.0f || T > 1.0f || U < 0.0f || U > 1.0f)
		return false;
	if(pT)
		*pT = T;
	if(pHit)
		*pHit = A + R * T;
	return true;
}

void Project(const vec2 *pVertices, int Count, vec2 Axis, float *pMin, float *pMax)
{
	*pMin = *pMax = dot(pVertices[0], Axis);
	for(int i = 1; i < Count; ++i)
	{
		const float Value = dot(pVertices[i], Axis);
		*pMin = std::min(*pMin, Value);
		*pMax = std::max(*pMax, Value);
	}
}

bool PolygonTileMtv(const vec2 *pVertices, int Count, vec2 BodyCenter, vec2 TileCenter, vec2 *pMtv, vec2 *pNormal)
{
	const vec2 aTile[4] = {
		TileCenter + vec2(-16.0f, -16.0f),
		TileCenter + vec2(16.0f, -16.0f),
		TileCenter + vec2(16.0f, 16.0f),
		TileCenter + vec2(-16.0f, 16.0f),
	};

	float BestOverlap = std::numeric_limits<float>::max();
	vec2 BestAxis(0.0f, -1.0f);
	for(int AxisIndex = 0; AxisIndex < Count + 2; ++AxisIndex)
	{
		vec2 Axis;
		if(AxisIndex == 0)
			Axis = vec2(1.0f, 0.0f);
		else if(AxisIndex == 1)
			Axis = vec2(0.0f, 1.0f);
		else
		{
			const vec2 Edge = pVertices[(AxisIndex - 1) % Count] - pVertices[AxisIndex - 2];
			if(length(Edge) < 0.0001f)
				continue;
			Axis = normalize(Perpendicular(Edge));
		}

		float BodyMin, BodyMax, TileMin, TileMax;
		Project(pVertices, Count, Axis, &BodyMin, &BodyMax);
		Project(aTile, 4, Axis, &TileMin, &TileMax);
		const float Overlap = std::min(BodyMax, TileMax) - std::max(BodyMin, TileMin);
		if(Overlap <= 0.0f)
			return false;
		if(Overlap < BestOverlap)
		{
			BestOverlap = Overlap;
			BestAxis = Axis;
		}
	}

	if(dot(BodyCenter - TileCenter, BestAxis) < 0.0f)
		BestAxis *= -1.0f;
	*pNormal = BestAxis;
	*pMtv = BestAxis * (BestOverlap + 0.01f);
	return true;
}

vec2 SpawnPosition(CGameContext *pGameServer, IConsole::IResult *pResult, int CoordArg)
{
	if(pResult->NumArguments() >= CoordArg + 2)
		return vec2(pResult->GetFloat(CoordArg) * 32.0f, pResult->GetFloat(CoordArg + 1) * 32.0f);

	const int ClientId = pResult->m_ClientId;
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && pGameServer->m_apPlayers[ClientId])
	{
		CPlayer *pPlayer = pGameServer->m_apPlayers[ClientId];
		CCharacter *pChr = pGameServer->GetPlayerChar(ClientId);
		if(pChr && pChr->IsAlive() && !pPlayer->IsPaused())
		{
			const vec2 Target(pChr->Core()->m_Input.m_TargetX, pChr->Core()->m_Input.m_TargetY);
			return pPlayer->m_CameraInfo.ConvertTargetToWorld(pChr->GetPos(), Target);
		}
		return pPlayer->m_ViewPos;
	}
	return vec2(0.0f, 0.0f);
}

CHoRigidBody *FindRigidBody(CGameContext *pGameServer, int BodyId)
{
	for(CEntity *pEnt = pGameServer->m_World.FindFirst(CGameWorld::ENTTYPE_HO_RIGIDBODY); pEnt; pEnt = pEnt->TypeNext())
	{
		auto *pBody = static_cast<CHoRigidBody *>(pEnt);
		if(pBody->BodyId() == BodyId)
			return pBody;
	}
	return nullptr;
}

void ConHoBlock(IConsole::IResult *pResult, void *pUserData)
{
	auto *pGameServer = static_cast<CGameContext *>(pUserData);
	const float Size = pResult->GetFloat(0);
	const float Mass = pResult->GetFloat(1);
	if(Size < 32.0f || Size > 512.0f || Mass < 0.1f || Mass > 1000.0f)
	{
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_block", "size must be 32..512 pixels and mass 0.1..1000");
		return;
	}
	if(pResult->m_ClientId == IConsole::CLIENT_ID_UNSPECIFIED && pResult->NumArguments() < 4)
	{
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_block", "server console usage: ho_block [size] [mass] [tile x] [tile y]");
		return;
	}
	const vec2 Pos = SpawnPosition(pGameServer, pResult, 2);
	auto *pBody = new CHoRigidBody(&pGameServer->m_World, CHoRigidBody::EKind::BLOCK, Pos, 4, Size, Mass);
	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "spawned block #%d: size %.1f, mass %.2f at %.2f %.2f tiles", pBody->BodyId(), Size, Mass, Pos.x / 32.0f, Pos.y / 32.0f);
	pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_block", aBuf);
}

void ConHoDice(IConsole::IResult *pResult, void *pUserData)
{
	auto *pGameServer = static_cast<CGameContext *>(pUserData);
	const int Faces = pResult->GetInteger(0);
	const float Size = pResult->GetFloat(1);
	const float Mass = pResult->GetFloat(2);
	if(Faces < 3 || Faces > CHoRigidBody::MAX_VERTICES || Size < 32.0f || Size > 512.0f || Mass < 0.1f || Mass > 1000.0f)
	{
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_dice", "faces must be 3..32, size 32..512 pixels and mass 0.1..1000");
		return;
	}
	if(pResult->m_ClientId == IConsole::CLIENT_ID_UNSPECIFIED && pResult->NumArguments() < 5)
	{
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_dice", "server console usage: ho_dice [faces] [size] [mass] [tile x] [tile y]");
		return;
	}
	const vec2 Pos = SpawnPosition(pGameServer, pResult, 3);
	auto *pBody = new CHoRigidBody(&pGameServer->m_World, CHoRigidBody::EKind::DICE, Pos, Faces, Size, Mass);
	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "spawned d%d #%d: size %.1f, mass %.2f at %.2f %.2f tiles", Faces, pBody->BodyId(), Size, Mass, Pos.x / 32.0f, Pos.y / 32.0f);
	pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_dice", aBuf);
}

void ConHoRigidClear(IConsole::IResult *pResult, void *pUserData)
{
	auto *pGameServer = static_cast<CGameContext *>(pUserData);
	int Count = 0;
	for(CEntity *pEnt = pGameServer->m_World.FindFirst(CGameWorld::ENTTYPE_HO_RIGIDBODY); pEnt; pEnt = pEnt->TypeNext())
	{
		pEnt->Reset();
		++Count;
	}
	char aBuf[96];
	str_format(aBuf, sizeof(aBuf), "removed %d rigid bodies", Count);
	pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_rigid_clear", aBuf);
}

void ConHoRigidIds(IConsole::IResult *pResult, void *pUserData)
{
	auto *pGameServer = static_cast<CGameContext *>(pUserData);
	if(pResult->NumArguments() > 0)
		s_ShowRigidBodyIds = pResult->GetInteger(0) != 0;
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "rigid body ID display is %s", s_ShowRigidBodyIds ? "on" : "off");
	pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_rigid_ids", aBuf);
}

void ConHoRigidDelete(IConsole::IResult *pResult, void *pUserData)
{
	auto *pGameServer = static_cast<CGameContext *>(pUserData);
	const int BodyId = pResult->GetInteger(0);
	CHoRigidBody *pBody = FindRigidBody(pGameServer, BodyId);
	if(!pBody)
	{
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_rigid_delete", "rigid body ID not found");
		return;
	}
	pBody->Reset();
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "removed rigid body #%d", BodyId);
	pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_rigid_delete", aBuf);
}

void ConHoRigidList(IConsole::IResult *pResult, void *pUserData)
{
	auto *pGameServer = static_cast<CGameContext *>(pUserData);
	int Count = 0;
	for(CEntity *pEnt = pGameServer->m_World.FindFirst(CGameWorld::ENTTYPE_HO_RIGIDBODY); pEnt; pEnt = pEnt->TypeNext())
	{
		auto *pBody = static_cast<CHoRigidBody *>(pEnt);
		char aBuf[144];
		str_format(aBuf, sizeof(aBuf), "#%d %s sides=%d size=%.1f mass=%.2f pos=%.2f,%.2f tiles",
			pBody->BodyId(), pBody->Kind() == CHoRigidBody::EKind::BLOCK ? "block" : "dice", pBody->Sides(),
			pBody->Size(), pBody->Mass(), pBody->GetPos().x / 32.0f, pBody->GetPos().y / 32.0f);
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_rigid_list", aBuf);
		++Count;
	}
	if(Count == 0)
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_rigid_list", "no rigid bodies");
}
} // namespace

CHoRigidBody::CHoRigidBody(CGameWorld *pGameWorld, EKind Kind, vec2 Pos, int Sides, float Size, float Mass) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_HO_RIGIDBODY, false, Pos, (int)(Kind == EKind::BLOCK ? Size * std::sqrt(0.5f) : Size * 0.5f)),
	m_Kind(Kind),
	m_BodyId(s_NextRigidBodyId++),
	m_Sides(std::clamp(Sides, 3, MAX_VERTICES)),
	m_Size(Size),
	m_Radius(Kind == EKind::BLOCK ? Size * std::sqrt(0.5f) : Size * 0.5f),
	m_Mass(Mass),
	m_Angle(Kind == EKind::BLOCK ? PI * 0.25f : 0.0f),
	m_AngularVelocity(0.0f),
	m_Velocity(0.0f, 0.0f),
	m_StillTicks(0),
	m_DiceResultReported(false)
{
	m_aHooked.fill(false);
	if(m_Kind == EKind::DICE)
	{
		m_AngularVelocity = (GameWorld()->m_Core.RandomOr0(2001) - 1000) * 0.00012f;
		if(absolute(m_AngularVelocity) < 0.025f)
			m_AngularVelocity = 0.08f;
	}
	for(int i = 0; i < m_Sides; ++i)
		m_aSnapIds[i] = Server()->SnapNewId();
	for(auto &aDigitIds : m_aaIdSnapIds)
		for(std::optional<int> &Id : aDigitIds)
			Id = Server()->SnapNewId();
	GameWorld()->InsertEntity(this);
}

CHoRigidBody::~CHoRigidBody()
{
	for(const std::optional<int> &Id : m_aSnapIds)
		if(Id.has_value())
			Server()->SnapFreeId(Id.value());
	for(const auto &aDigitIds : m_aaIdSnapIds)
		for(const std::optional<int> &Id : aDigitIds)
			if(Id.has_value())
				Server()->SnapFreeId(Id.value());
}

void CHoRigidBody::Reset()
{
	m_MarkedForDestroy = true;
}

void CHoRigidBody::BuildVertices(std::array<vec2, MAX_VERTICES> &aVertices) const
{
	for(int i = 0; i < m_Sides; ++i)
	{
		const float VertexAngle = m_Angle - PI * 0.5f + 2.0f * PI * i / m_Sides;
		aVertices[i] = m_Pos + vec2(std::cos(VertexAngle), std::sin(VertexAngle)) * m_Radius;
	}
}

float CHoRigidBody::InverseInertia() const
{
	// A solid disk is a stable approximation for all regular polygons used here.
	return 1.0f / std::max(0.001f, 0.5f * m_Mass * m_Radius * m_Radius);
}

void CHoRigidBody::ApplyImpulse(vec2 Impulse, vec2 WorldPoint, bool Wake)
{
	m_Velocity += Impulse / m_Mass;
	m_AngularVelocity += Cross(WorldPoint - m_Pos, Impulse) * InverseInertia();
	m_Velocity.x = ClampMagnitude(m_Velocity.x, 80.0f);
	m_Velocity.y = ClampMagnitude(m_Velocity.y, 80.0f);
	m_AngularVelocity = ClampMagnitude(m_AngularVelocity, 0.45f);
	if(Wake)
	{
		m_StillTicks = 0;
		m_DiceResultReported = false;
	}
}

void CHoRigidBody::ApplyCentralImpulse(vec2 Impulse)
{
	ApplyImpulse(Impulse, m_Pos);
}

bool CHoRigidBody::IntersectSegment(vec2 From, vec2 To, vec2 *pHit, vec2 *pNormal) const
{
	std::array<vec2, MAX_VERTICES> aVertices;
	BuildVertices(aVertices);
	float BestT = std::numeric_limits<float>::max();
	int BestEdge = -1;
	vec2 BestHit;
	for(int i = 0; i < m_Sides; ++i)
	{
		float T;
		vec2 Hit;
		if(SegmentIntersection(From, To, aVertices[i], aVertices[(i + 1) % m_Sides], &T, &Hit) && T < BestT)
		{
			BestT = T;
			BestEdge = i;
			BestHit = Hit;
		}
	}
	if(BestEdge < 0)
		return false;
	if(pHit)
		*pHit = BestHit;
	if(pNormal)
	{
		vec2 Normal = normalize(Perpendicular(aVertices[(BestEdge + 1) % m_Sides] - aVertices[BestEdge]));
		if(dot(Normal, BestHit - m_Pos) < 0.0f)
			Normal *= -1.0f;
		*pNormal = Normal;
	}
	return true;
}

bool CHoRigidBody::ResolveMapCollision()
{
	bool ResolvedAny = false;
	for(int Iteration = 0; Iteration < 8; ++Iteration)
	{
		std::array<vec2, MAX_VERTICES> aVertices;
		BuildVertices(aVertices);
		vec2 Min = aVertices[0];
		vec2 Max = aVertices[0];
		for(int i = 1; i < m_Sides; ++i)
		{
			Min.x = std::min(Min.x, aVertices[i].x);
			Min.y = std::min(Min.y, aVertices[i].y);
			Max.x = std::max(Max.x, aVertices[i].x);
			Max.y = std::max(Max.y, aVertices[i].y);
		}

		float BestDepth = 0.0f;
		vec2 BestMtv;
		vec2 BestNormal;
		for(int TileY = (int)std::floor(Min.y / 32.0f); TileY <= (int)std::floor(Max.y / 32.0f); ++TileY)
		{
			for(int TileX = (int)std::floor(Min.x / 32.0f); TileX <= (int)std::floor(Max.x / 32.0f); ++TileX)
			{
				const vec2 TileCenter(TileX * 32.0f + 16.0f, TileY * 32.0f + 16.0f);
				if(!Collision()->CheckPoint(TileCenter))
					continue;
				vec2 Mtv, Normal;
				if(!PolygonTileMtv(aVertices.data(), m_Sides, m_Pos, TileCenter, &Mtv, &Normal))
					continue;
				const float Depth = length(Mtv);
				if(Depth > BestDepth)
				{
					BestDepth = Depth;
					BestMtv = Mtv;
					BestNormal = Normal;
				}
			}
		}

		if(BestDepth <= 0.0f)
			break;
		ResolvedAny = true;
		m_Pos += BestMtv;

		std::array<vec2, MAX_VERTICES> aCorrectedVertices;
		BuildVertices(aCorrectedVertices);
		vec2 Contact = aCorrectedVertices[0];
		float BestSupport = dot(Contact, BestNormal);
		for(int i = 1; i < m_Sides; ++i)
		{
			const float Support = dot(aCorrectedVertices[i], BestNormal);
			if(Support < BestSupport)
			{
				BestSupport = Support;
				Contact = aCorrectedVertices[i];
			}
		}
		const vec2 R = Contact - m_Pos;
		const vec2 ContactVelocity = m_Velocity + Perpendicular(R) * m_AngularVelocity;
		const float NormalVelocity = dot(ContactVelocity, BestNormal);
		if(NormalVelocity < 0.0f)
		{
			const float Lever = Cross(R, BestNormal);
			const float J = -(1.0f + MAP_RESTITUTION) * NormalVelocity /
				(1.0f / m_Mass + Lever * Lever * InverseInertia());
			ApplyImpulse(BestNormal * J, Contact, false);

			const vec2 Tangent = Perpendicular(BestNormal);
			const float TangentVelocity = dot(m_Velocity + Perpendicular(R) * m_AngularVelocity, Tangent);
			const float TangentLever = Cross(R, Tangent);
			float FrictionJ = -TangentVelocity /
				(1.0f / m_Mass + TangentLever * TangentLever * InverseInertia());
			FrictionJ = std::clamp(FrictionJ, -J * 0.55f, J * 0.55f);
			ApplyImpulse(Tangent * FrictionJ, Contact, false);
		}
	}
	return ResolvedAny;
}

void CHoRigidBody::ResolveCharacterCollision(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive() || pChr->IsPaused())
		return;
	std::array<vec2, MAX_VERTICES> aVertices;
	BuildVertices(aVertices);

	const vec2 CircleCenter = pChr->Core()->m_Pos;
	float BestDistance = std::numeric_limits<float>::max();
	vec2 Closest;
	bool Inside = true;
	float CrossSign = 0.0f;
	for(int i = 0; i < m_Sides; ++i)
	{
		const vec2 A = aVertices[i];
		const vec2 B = aVertices[(i + 1) % m_Sides];
		vec2 EdgeClosest;
		closest_point_on_line(A, B, CircleCenter, EdgeClosest);
		const float Dist = distance(EdgeClosest, CircleCenter);
		if(Dist < BestDistance)
		{
			BestDistance = Dist;
			Closest = EdgeClosest;
		}
		const float Side = Cross(B - A, CircleCenter - A);
		if(absolute(Side) > 0.001f)
		{
			if(CrossSign == 0.0f)
				CrossSign = Side;
			else if((Side > 0.0f) != (CrossSign > 0.0f))
				Inside = false;
		}
	}

	const float PlayerRadius = CCharacterCore::PhysicalSize() * 0.5f;
	if(!Inside && BestDistance >= PlayerRadius)
		return;

	vec2 Normal;
	float Penetration;
	if(Inside)
	{
		Normal = Closest - m_Pos;
		if(length(Normal) < 0.001f)
			Normal = vec2(0.0f, -1.0f);
		else
			Normal = normalize(Normal);
		Penetration = PlayerRadius + BestDistance;
	}
	else
	{
		Normal = CircleCenter - Closest;
		if(length(Normal) < 0.001f)
			Normal = normalize(Closest - m_Pos);
		else
			Normal = normalize(Normal);
		Penetration = PlayerRadius - BestDistance;
	}

	const float InvPlayerMass = 1.0f / PLAYER_MASS;
	const float InvBodyMass = 1.0f / m_Mass;
	const float TotalInvMass = InvPlayerMass + InvBodyMass;
	const vec2 Correction = Normal * (Penetration + 0.05f);
	CCharacterCore Core = pChr->GetCore();
	Core.m_Pos += Correction * (InvPlayerMass / TotalInvMass);
	m_Pos -= Correction * (InvBodyMass / TotalInvMass);

	const vec2 R = Closest - m_Pos;
	const vec2 BodyPointVelocity = m_Velocity + Perpendicular(R) * m_AngularVelocity;
	const vec2 RelativeVelocity = Core.m_Vel - BodyPointVelocity;
	const float NormalVelocity = dot(RelativeVelocity, Normal);
	if(NormalVelocity < 0.0f)
	{
		const float Lever = Cross(R, Normal);
		const float J = -(1.0f + BODY_RESTITUTION) * NormalVelocity /
			(InvPlayerMass + InvBodyMass + Lever * Lever * InverseInertia());
		Core.m_Vel += Normal * (J * InvPlayerMass);
		ApplyImpulse(Normal * -J, Closest, absolute(J) > 1.0f);
	}
	pChr->SetCore(Core);
	pChr->m_Pos = Core.m_Pos;
}

void CHoRigidBody::UpdateHooks()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
		if(!pChr || !pChr->IsAlive())
		{
			m_aHooked[ClientId] = false;
			continue;
		}
		const CCharacterCore *pCore = pChr->Core();
		if(m_aHooked[ClientId])
		{
			if(pCore->m_HookState != HOOK_GRABBED || pCore->HookedPlayer() != -1 || !pCore->m_Input.m_Hook)
			{
				m_aHooked[ClientId] = false;
				continue;
			}
			const vec2 Anchor = m_Pos + Rotate(m_aHookLocalPos[ClientId], m_Angle);
			pChr->SetHookGrabWorld(Anchor);
			const vec2 ToPlayer = pChr->GetPos() - Anchor;
			const float Distance = length(ToPlayer);
			if(Distance > 46.0f)
			{
				const float Accel = pCore->m_Tuning.m_HookDragAccel;
				ApplyImpulse(normalize(ToPlayer) * (Accel * PLAYER_MASS * 0.85f), Anchor);
			}
			continue;
		}

		if(pCore->m_HookState != HOOK_FLYING)
			continue;
		const vec2 HookEnd = pCore->m_HookPos;
		const vec2 HookStart = HookEnd - pCore->m_HookDir * pCore->m_Tuning.m_HookFireSpeed;
		vec2 Hit;
		if(!IntersectSegment(HookStart, HookEnd, &Hit))
			continue;
		m_aHooked[ClientId] = true;
		m_aHookLocalPos[ClientId] = Rotate(Hit - m_Pos, -m_Angle);
		pChr->SetHookGrabWorld(Hit);
	}
}

void CHoRigidBody::Tick()
{
	UpdateHooks();
	m_Velocity.y += 0.5f;
	m_Velocity *= 0.999f;
	m_AngularVelocity *= 0.998f;

	const float MaxTravel = std::max(length(m_Velocity), absolute(m_AngularVelocity) * m_Radius);
	const int Steps = std::clamp((int)std::ceil(MaxTravel / 7.0f), 1, 16);
	for(int Step = 0; Step < Steps; ++Step)
	{
		m_Pos += m_Velocity / (float)Steps;
		m_Angle += m_AngularVelocity / (float)Steps;
		ResolveMapCollision();
	}
	while(m_Angle > PI)
		m_Angle -= 2.0f * PI;
	while(m_Angle < -PI)
		m_Angle += 2.0f * PI;

	if(GameLayerClipped(m_Pos))
		Reset();
}

void CHoRigidBody::TickDeferred()
{
	CEntity *apCharacters[MAX_CLIENTS];
	const int Num = GameWorld()->FindEntities(m_Pos, m_Radius + CCharacterCore::PhysicalSize(), apCharacters, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; ++i)
		ResolveCharacterCollision(static_cast<CCharacter *>(apCharacters[i]));

	if(length(m_Velocity) < 0.08f && absolute(m_AngularVelocity) < 0.0015f)
		++m_StillTicks;
	else
		m_StillTicks = 0;
	if(m_Kind == EKind::DICE && m_StillTicks >= Server()->TickSpeed())
		ReportDiceResult();
}

void CHoRigidBody::ReportDiceResult()
{
	if(m_DiceResultReported)
		return;
	const float Step = 2.0f * PI / m_Sides;
	int Face = ((int)std::lround(-m_Angle / Step) % m_Sides + m_Sides) % m_Sides + 1;
	char aBuf[96];
	str_format(aBuf, sizeof(aBuf), "d%d rolled %d", m_Sides, Face);
	GameServer()->SendChat(-1, TEAM_ALL, aBuf);
	m_DiceResultReported = true;
}

void CHoRigidBody::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;
	std::array<vec2, MAX_VERTICES> aVertices;
	BuildVertices(aVertices);
	const CSnapContext Context(GameServer()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient);
	for(int i = 0; i < m_Sides; ++i)
	{
		if(!m_aSnapIds[i].has_value())
			continue;
		GameServer()->SnapLaserObject(Context, m_aSnapIds[i].value(), aVertices[(i + 1) % m_Sides], aVertices[i],
			Server()->Tick(), -1, LASERTYPE_DOOR, 0, -1);
	}

	if(!s_ShowRigidBodyIds)
		return;
	char aId[MAX_ID_DIGITS + 1];
	str_format(aId, sizeof(aId), "%d", m_BodyId);
	const int Digits = str_length(aId);
	const float DigitWidth = 10.0f;
	const float DigitHeight = 16.0f;
	const float Spacing = 3.0f;
	const float TotalWidth = Digits * DigitWidth + std::max(0, Digits - 1) * Spacing;
	const vec2 LabelOrigin = m_Pos + vec2(-TotalWidth * 0.5f, -m_Radius - 24.0f);
	for(int DigitIndex = 0; DigitIndex < Digits && DigitIndex < MAX_ID_DIGITS; ++DigitIndex)
	{
		const int Digit = aId[DigitIndex] - '0';
		if(Digit < 0 || Digit > 9)
			continue;
		const vec2 O = LabelOrigin + vec2(DigitIndex * (DigitWidth + Spacing), 0.0f);
		const vec2 aSegmentFrom[7] = {
			O,
			O + vec2(DigitWidth, 0.0f),
			O + vec2(DigitWidth, DigitHeight * 0.5f),
			O + vec2(0.0f, DigitHeight),
			O + vec2(0.0f, DigitHeight * 0.5f),
			O,
			O + vec2(0.0f, DigitHeight * 0.5f),
		};
		const vec2 aSegmentTo[7] = {
			O + vec2(DigitWidth, 0.0f),
			O + vec2(DigitWidth, DigitHeight * 0.5f),
			O + vec2(DigitWidth, DigitHeight),
			O + vec2(DigitWidth, DigitHeight),
			O + vec2(0.0f, DigitHeight),
			O + vec2(0.0f, DigitHeight * 0.5f),
			O + vec2(DigitWidth, DigitHeight * 0.5f),
		};
		for(int Segment = 0; Segment < 7; ++Segment)
		{
			if(!(s_aDigitSegments[Digit] & (1 << Segment)) || !m_aaIdSnapIds[DigitIndex][Segment].has_value())
				continue;
			GameServer()->SnapLaserObject(Context, m_aaIdSnapIds[DigitIndex][Segment].value(), aSegmentTo[Segment], aSegmentFrom[Segment],
				Server()->Tick(), -1, LASERTYPE_DOOR, 0, -1);
		}
	}
}

void HoRegisterRigidBodyCommands(CGameContext *pGameServer)
{
	pGameServer->Console()->Register("ho_block", "f[size] f[mass] ?f[tile x] ?f[tile y]", CFGFLAG_SERVER, ConHoBlock, pGameServer, "Spawn a hookable, weapon-reactive square rigid body at cursor or tile coordinates");
	pGameServer->Console()->Register("ho_dice", "i[faces] f[size] f[mass] ?f[tile x] ?f[tile y]", CFGFLAG_SERVER, ConHoDice, pGameServer, "Spawn a hookable, weapon-reactive regular die at cursor or tile coordinates");
	pGameServer->Console()->Register("ho_rigid_clear", "", CFGFLAG_SERVER, ConHoRigidClear, pGameServer, "Remove all ho_block and ho_dice rigid bodies");
	pGameServer->Console()->Register("ho_rigid_ids", "?i[0|1]", CFGFLAG_SERVER, ConHoRigidIds, pGameServer, "Show, hide or query rigid body IDs; enabled by default");
	pGameServer->Console()->Register("ho_rigid_delete", "i[id]", CFGFLAG_SERVER, ConHoRigidDelete, pGameServer, "Remove one ho_block or ho_dice by ID");
	pGameServer->Console()->Register("ho_rigid_list", "", CFGFLAG_SERVER, ConHoRigidList, pGameServer, "List all ho_block and ho_dice IDs and properties");
}

bool HoRigidBodyWeaponHit(CGameContext *pGameServer, vec2 From, vec2 To, vec2 Impulse, int, vec2 *pHit)
{
	CHoRigidBody *pBestBody = nullptr;
	vec2 BestHit;
	float BestDistance = std::numeric_limits<float>::max();
	for(CEntity *pEnt = pGameServer->m_World.FindFirst(CGameWorld::ENTTYPE_HO_RIGIDBODY); pEnt; pEnt = pEnt->TypeNext())
	{
		auto *pBody = static_cast<CHoRigidBody *>(pEnt);
		vec2 Hit;
		if(!pBody->IntersectSegment(From, To, &Hit))
			continue;
		const float Distance = distance(From, Hit);
		if(Distance < BestDistance)
		{
			BestDistance = Distance;
			BestHit = Hit;
			pBestBody = pBody;
		}
	}
	if(!pBestBody)
		return false;
	pBestBody->ApplyImpulse(Impulse, BestHit);
	if(pHit)
		*pHit = BestHit;
	return true;
}

void HoRigidBodyExplosion(CGameContext *pGameServer, vec2 Pos, float Radius, float Strength)
{
	for(CEntity *pEnt = pGameServer->m_World.FindFirst(CGameWorld::ENTTYPE_HO_RIGIDBODY); pEnt; pEnt = pEnt->TypeNext())
	{
		auto *pBody = static_cast<CHoRigidBody *>(pEnt);
		vec2 Diff = pBody->GetPos() - Pos;
		const float Dist = length(Diff);
		if(Dist > Radius + pBody->Radius())
			continue;
		if(Dist < 0.001f)
			Diff = vec2(0.0f, -1.0f);
		const float Factor = 1.0f - std::clamp(Dist / (Radius + pBody->Radius()), 0.0f, 1.0f);
		const vec2 HitPoint = pBody->GetPos() - normalize(Diff) * pBody->Radius() * 0.65f;
		pBody->ApplyImpulse(normalize(Diff) * (Strength * Factor * 12.0f), HitPoint);
	}
}

bool HoRigidBodyHammerHit(CGameContext *pGameServer, vec2 Pos, vec2 Direction, float Strength)
{
	CHoRigidBody *pBestBody = nullptr;
	float BestDistance = std::numeric_limits<float>::max();
	vec2 BestHit;
	const vec2 End = Pos + Direction * 72.0f;
	for(CEntity *pEnt = pGameServer->m_World.FindFirst(CGameWorld::ENTTYPE_HO_RIGIDBODY); pEnt; pEnt = pEnt->TypeNext())
	{
		auto *pBody = static_cast<CHoRigidBody *>(pEnt);
		vec2 Hit;
		if(!pBody->IntersectSegment(Pos, End, &Hit))
			continue;
		const float Dist = distance(Pos, Hit);
		if(Dist < BestDistance)
		{
			BestDistance = Dist;
			BestHit = Hit;
			pBestBody = pBody;
		}
	}
	if(!pBestBody)
		return false;
	pBestBody->ApplyImpulse(normalize(Direction + vec2(0.0f, -0.35f)) * (Strength * PLAYER_MASS * 2.5f), BestHit);
	pGameServer->CreateHammerHit(BestHit);
	return true;
}
