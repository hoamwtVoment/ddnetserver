#include "portal.h"

#include <base/math.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <cmath>

namespace
{
	constexpr float PORTAL_HALF_LENGTH = 48.0f;
	constexpr float PORTAL_ENTRY_HALF_LENGTH = 34.0f;
	constexpr int PPRACE_TILE_PORTALABLE = 0x90;
	constexpr int PPRACE_TILE_PORTAL_ABSORB = 0x93;
	// CCharacterCore::PhysicalSizeVec2() has a 28 px half-size. Keep the center
	// one extra pixel away from the surface so MoveBox cannot resolve it as stuck.
	constexpr float PORTAL_TEE_DISTANCE = 29.0f;

	vec2 PortalNormal(int Direction)
	{
		switch(Direction)
		{
		case HO_PORTAL_LEFT: return vec2(-1.0f, 0.0f);
		case HO_PORTAL_UP: return vec2(0.0f, -1.0f);
		case HO_PORTAL_RIGHT: return vec2(1.0f, 0.0f);
		case HO_PORTAL_DOWN: return vec2(0.0f, 1.0f);
		default: return vec2(0.0f, 0.0f);
		}
	}
}

CHoPortal::CHoPortal(CGameWorld *pGameWorld, int Owner, int PortalIndex) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, true),
	m_Owner(Owner),
	m_PortalIndex(PortalIndex),
	m_Direction(HO_PORTAL_UP),
	m_Active(false),
	m_From(0.0f, 0.0f),
	m_To(0.0f, 0.0f)
{
	GameWorld()->InsertEntity(this);
}

void CHoPortal::Reset()
{
	m_Active = false;
}

vec2 CHoPortal::Normal() const
{
	return PortalNormal(m_Direction);
}

vec2 CHoPortal::Tangent() const
{
	const vec2 N = Normal();
	return vec2(-N.y, N.x);
}

void CHoPortal::Place(vec2 SurfaceCenter, int Direction)
{
	m_Direction = Direction;
	m_Pos = SurfaceCenter;
	const vec2 T = Tangent();
	m_From = m_Pos - T * PORTAL_HALF_LENGTH;
	m_To = m_Pos + T * PORTAL_HALF_LENGTH;
	m_Active = true;
}

bool CHoPortal::IsIn(vec2 Pos, float *pTangentOffset) const
{
	if(!m_Active)
		return false;

	const vec2 N = Normal();
	const vec2 T = Tangent();
	const float NormalDistance = dot(Pos - m_Pos, N);
	if(NormalDistance < 0.0f || NormalDistance >= PORTAL_HALF_LENGTH)
		return false;

	const float TangentOffset = dot(Pos - m_Pos, T);
	if(absolute(TangentOffset) >= PORTAL_ENTRY_HALF_LENGTH)
		return false;

	if(pTangentOffset)
		*pTangentOffset = TangentOffset;
	return true;
}

bool CHoPortal::IntersectEntry(vec2 Pos, vec2 Move, float *pTangentOffset, float *pEntryTime) const
{
	if(IsIn(Pos, pTangentOffset))
	{
		*pEntryTime = 0.0f;
		return true;
	}
	if(!m_Active || length(Move) == 0.0f)
		return false;

	const vec2 N = Normal();
	const vec2 T = Tangent();
	const float StartNormal = dot(Pos - m_Pos, N);
	const float StartTangent = dot(Pos - m_Pos, T);
	const float DeltaNormal = dot(Move, N);
	const float DeltaTangent = dot(Move, T);
	float EnterTime = 0.0f;
	float ExitTime = 1.0f;

	const auto IntersectSlab = [&](float Start, float Delta, float Minimum, float Maximum) {
		if(absolute(Delta) < 0.000001f)
			return Start >= Minimum && Start <= Maximum;

		float First = (Minimum - Start) / Delta;
		float Last = (Maximum - Start) / Delta;
		if(First > Last)
		{
			const float Temp = First;
			First = Last;
			Last = Temp;
		}
		EnterTime = std::max(EnterTime, First);
		ExitTime = std::min(ExitTime, Last);
		return EnterTime <= ExitTime;
	};

	if(!IntersectSlab(StartNormal, DeltaNormal, 0.0f, PORTAL_HALF_LENGTH) ||
		!IntersectSlab(StartTangent, DeltaTangent, -PORTAL_ENTRY_HALF_LENGTH, PORTAL_ENTRY_HALF_LENGTH) ||
		EnterTime < 0.0f || EnterTime > 1.0f)
		return false;

	const vec2 EntryPos = Pos + Move * std::min(EnterTime + 0.0001f, 1.0f);
	*pTangentOffset = std::clamp(dot(EntryPos - m_Pos, T), -PORTAL_ENTRY_HALF_LENGTH + 0.001f, PORTAL_ENTRY_HALF_LENGTH - 0.001f);
	*pEntryTime = EnterTime;
	return true;
}

void CHoPortal::Snap(int SnappingClient)
{
	if(!m_Active || !GetId().has_value() || NetworkClippedLine(SnappingClient, m_From, m_To))
		return;

	if(SnappingClient != SERVER_DEMO_CLIENT)
	{
		CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
		CCharacter *pViewer = GameServer()->GetPlayerChar(SnappingClient);
		if(pOwner && pViewer && pOwner->Team() != pViewer->Team() && !GameServer()->m_apPlayers[SnappingClient]->m_ShowOthers)
			return;
	}

	const int ClientVersion = GameServer()->GetClientVersion(SnappingClient);
	GameServer()->SnapLaserObject(CSnapContext(ClientVersion, Server()->IsSixup(SnappingClient), SnappingClient), GetId().value(),
		m_To, m_From, Server()->Tick(), m_Owner, LASERTYPE_RIFLE, m_PortalIndex);
}

void CHoPortal::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : (m_Owner == Client2 ? Client1 : m_Owner);
}

bool CGameContext::TryCreateHoPortal(int Owner, vec2 CollisionPos, vec2 LaserDirection)
{
	// Only portal modes 1/2 place portals (not classic 0 or laser cannon 3).
	if(!CheckClientId(Owner) || !m_apPlayers[Owner] ||
		m_apPlayers[Owner]->m_HoLaserMode < 1 || m_apPlayers[Owner]->m_HoLaserMode > 2)
		return false;

	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	const int HitIndex = Collision()->GetPureMapIndex(CollisionPos);
	const int HitX = HitIndex % Width;
	const int HitY = HitIndex / Width;
	if(!m_HoSuperPortal)
	{
		const int FrontTile = Collision()->GetFrontTileIndex(HitIndex);
		if(FrontTile == PPRACE_TILE_PORTAL_ABSORB)
			return true;
		if(FrontTile != PPRACE_TILE_PORTALABLE)
			return false;
	}
	const vec2 HitTileCenter = Collision()->GetPos(HitIndex);
	const vec2 Delta = HitTileCenter - CollisionPos;

	int Direction;
	if(absolute(Delta.x) < absolute(Delta.y))
		Direction = Delta.y > 0.0f ? HO_PORTAL_UP : HO_PORTAL_DOWN;
	else if(absolute(Delta.x) > 0.0f)
		Direction = Delta.x > 0.0f ? HO_PORTAL_LEFT : HO_PORTAL_RIGHT;
	else if(absolute(LaserDirection.x) > absolute(LaserDirection.y))
		Direction = LaserDirection.x > 0.0f ? HO_PORTAL_LEFT : HO_PORTAL_RIGHT;
	else
		Direction = LaserDirection.y > 0.0f ? HO_PORTAL_UP : HO_PORTAL_DOWN;

	const vec2 N = PortalNormal(Direction);
	const vec2 T(-N.y, N.x);

	const auto IsValidCenter = [&](int CenterX, int CenterY) {
		for(int Offset = -1; Offset <= 1; ++Offset)
		{
			const int X = CenterX + round_to_int(T.x) * Offset;
			const int Y = CenterY + round_to_int(T.y) * Offset;
			const int OutsideX = X + round_to_int(N.x);
			const int OutsideY = Y + round_to_int(N.y);
			if(X < 0 || X >= Width || Y < 0 || Y >= Height || OutsideX < 0 || OutsideX >= Width || OutsideY < 0 || OutsideY >= Height)
				return false;
			const int Index = Y * Width + X;
			if(!m_HoSuperPortal && Collision()->GetFrontTileIndex(Index) != PPRACE_TILE_PORTALABLE)
				return false;
			if((m_HoSuperPortal && !Collision()->CheckPoint(vec2(X * 32 + 16, Y * 32 + 16))) || Collision()->CheckPoint(vec2(OutsideX * 32 + 16, OutsideY * 32 + 16)))
				return false;
		}
		return true;
	};

	int CenterX = HitX;
	int CenterY = HitY;
	bool Found = IsValidCenter(CenterX, CenterY);
	for(int Shift : {-1, 1})
	{
		if(Found)
			break;
		const int CandidateX = HitX + round_to_int(T.x) * Shift;
		const int CandidateY = HitY + round_to_int(T.y) * Shift;
		if(IsValidCenter(CandidateX, CandidateY))
		{
			CenterX = CandidateX;
			CenterY = CandidateY;
			Found = true;
		}
	}
	if(!Found)
		return true;

	const int PortalIndex = m_apPlayers[Owner]->m_HoLaserMode - 1;
	CHoPortal *pOther = m_aaHoPortals[Owner][1 - PortalIndex];
	const vec2 SurfaceCenter = vec2(CenterX * 32 + 16, CenterY * 32 + 16) + N * 16.0f;
	if(pOther && pOther->Active() && distance(pOther->Center(), SurfaceCenter) < 96.0f)
		return true;

	if(!m_aaHoPortals[Owner][PortalIndex])
		m_aaHoPortals[Owner][PortalIndex] = new CHoPortal(&m_World, Owner, PortalIndex);
	m_aaHoPortals[Owner][PortalIndex]->Place(SurfaceCenter, Direction);
	return true;
}

void CGameContext::DeactivateHoPortals(int ClientId)
{
	if(!CheckClientId(ClientId))
		return;
	for(auto *pPortal : m_aaHoPortals[ClientId])
	{
		if(pPortal)
			pPortal->Deactivate();
	}
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_aHoLastPortalOwner[i] == ClientId)
		{
			m_aHoLastPortalOwner[i] = -1;
			m_aHoLastPortalIndex[i] = -1;
		}
	}
}

bool CGameContext::HandleHoPortals(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive())
		return false;
	const int ClientId = pChr->GetPlayer()->GetCid();
	if(m_aHoLastPortalOwner[ClientId] >= 0)
	{
		CHoPortal *pLast = m_aaHoPortals[m_aHoLastPortalOwner[ClientId]][m_aHoLastPortalIndex[ClientId]];
		if(!pLast || !pLast->IsIn(pChr->Core()->m_Pos))
		{
			m_aHoLastPortalOwner[ClientId] = -1;
			m_aHoLastPortalIndex[ClientId] = -1;
		}
	}

	vec2 Pos = pChr->Core()->m_Pos;
	vec2 Vel = pChr->Core()->m_Vel;
	float RemainingTime = 1.0f;
	bool Teleported = false;

	constexpr int MAX_PORTAL_TRANSITIONS_PER_TICK = 128;
	for(int Transition = 0; Transition < MAX_PORTAL_TRANSITIONS_PER_TICK; ++Transition)
	{
		vec2 MoveVelocity = Vel;
		if(m_World.m_Core.m_HoSpeedLimit)
		{
			const CTuningParams *pTuning = pChr->GetTuning(pChr->m_TuneZone);
			MoveVelocity.x *= VelocityRamp(length(Vel) * 50.0f, pTuning->m_VelrampStart, pTuning->m_VelrampRange, pTuning->m_VelrampCurvature);
		}
		const vec2 Move = MoveVelocity * RemainingTime;

		CHoPortal *pBestEntrance = nullptr;
		CHoPortal *pBestExit = nullptr;
		int BestOwner = -1;
		int BestEntranceIndex = -1;
		float BestTangentOffset = 0.0f;
		float BestEntryTime = 2.0f;

		for(int Owner = 0; Owner < MAX_CLIENTS; ++Owner)
		{
			CHoPortal *pFirst = m_aaHoPortals[Owner][0];
			CHoPortal *pSecond = m_aaHoPortals[Owner][1];
			if(!pFirst || !pSecond || !pFirst->Active() || !pSecond->Active())
				continue;
			CCharacter *pOwnerChr = GetPlayerChar(Owner);
			if(!pOwnerChr || pOwnerChr->Team() != pChr->Team())
				continue;

			for(int EntranceIndex = 0; EntranceIndex < 2; ++EntranceIndex)
			{
				if(m_aHoLastPortalOwner[ClientId] == Owner && m_aHoLastPortalIndex[ClientId] == EntranceIndex)
					continue;
				float TangentOffset;
				float EntryTime;
				if(!m_aaHoPortals[Owner][EntranceIndex]->IntersectEntry(Pos, Move, &TangentOffset, &EntryTime) || EntryTime >= BestEntryTime)
					continue;
				pBestEntrance = m_aaHoPortals[Owner][EntranceIndex];
				pBestExit = m_aaHoPortals[Owner][1 - EntranceIndex];
				BestOwner = Owner;
				BestEntranceIndex = EntranceIndex;
				BestTangentOffset = TangentOffset;
				BestEntryTime = EntryTime;
			}
		}

		if(!pBestEntrance)
		{
			if(!Teleported)
				return false;

			vec2 MoveAfterPortals = Move;
			const vec2 MoveBeforeCollision = MoveAfterPortals;
			bool Grounded = false;
			const CTuningParams *pTuning = pChr->GetTuning(pChr->m_TuneZone);
			Collision()->MoveBox(&Pos, &MoveAfterPortals, CCharacterCore::PhysicalSizeVec2(),
				vec2(pTuning->m_GroundElasticityX, pTuning->m_GroundElasticityY), &Grounded);
			CCharacterCore Core = pChr->GetCore();
			if(Grounded)
			{
				Core.m_Jumped &= ~2;
				Core.m_JumpedTotal = 0;
			}
			Core.m_Colliding = 0;
			if(MoveAfterPortals.x < 0.001f && MoveAfterPortals.x > -0.001f)
			{
				if(MoveBeforeCollision.x > 0.0f)
					Core.m_Colliding = 1;
				else if(MoveBeforeCollision.x < 0.0f)
					Core.m_Colliding = 2;
			}
			else
				Core.m_LeftWall = true;
			pChr->SetCore(Core);
			if(absolute(MoveBeforeCollision.x) > 0.000001f)
				Vel.x *= MoveAfterPortals.x / MoveBeforeCollision.x;
			if(absolute(MoveBeforeCollision.y) > 0.000001f)
				Vel.y *= MoveAfterPortals.y / MoveBeforeCollision.y;
			pChr->SetPosition(Pos);
			pChr->SetRawVelocity(Vel);
			return true;
		}

		Pos += Move * BestEntryTime;
		RemainingTime *= 1.0f - BestEntryTime;
		const vec2 ExitNormal = pBestExit->Normal();
		const vec2 ExitTangent = pBestExit->Tangent();
		vec2 ExitPos;
		bool FoundExit = false;
		for(float Distance = PORTAL_TEE_DISTANCE; Distance <= 64.0f && !FoundExit; Distance += 1.0f)
		{
			for(float OffsetAdjustment : {0.0f, -4.0f, 4.0f, -8.0f, 8.0f, -16.0f, 16.0f})
			{
				const float ExitOffset = std::clamp(BestTangentOffset + OffsetAdjustment, -PORTAL_ENTRY_HALF_LENGTH + 1.0f, PORTAL_ENTRY_HALF_LENGTH - 1.0f);
				const vec2 Candidate = pBestExit->Center() + ExitNormal * Distance + ExitTangent * ExitOffset;
				if(!Collision()->TestBox(Candidate, CCharacterCore::PhysicalSizeVec2()))
				{
					ExitPos = Candidate;
					FoundExit = true;
					break;
				}
			}
		}
		if(!FoundExit)
			break;

		const vec2 EntranceNormal = pBestEntrance->Normal();
		const vec2 EntranceTangent = pBestEntrance->Tangent();
		const float TangentVelocity = dot(Vel, EntranceTangent);
		const float NormalVelocity = dot(Vel, EntranceNormal);
		Vel = ExitTangent * TangentVelocity - ExitNormal * NormalVelocity;
		Pos = ExitPos;
		pChr->m_PrevPos = ExitPos;
		m_aHoLastPortalOwner[ClientId] = BestOwner;
		m_aHoLastPortalIndex[ClientId] = 1 - BestEntranceIndex;
		Teleported = true;

		if(RemainingTime <= 0.000001f)
			break;
	}

	if(Teleported)
	{
		pChr->SetPosition(Pos);
		pChr->SetRawVelocity(Vel);
	}
	return Teleported;
}
