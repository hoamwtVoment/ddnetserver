#include "dice3d.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>

#include <algorithm>
#include <cmath>

namespace
{
	using TQuaternion = std::array<float, 4>;

	constexpr float DICE_HALF_SIZE = 40.0f;
	constexpr float DICE_BODY_SIZE = DICE_HALF_SIZE * 2.0f;
	constexpr float DICE_PI = 3.14159265358979323846f;

	TQuaternion QuaternionNormalize(const TQuaternion &Q)
	{
		const float Length = std::sqrt(Q[0] * Q[0] + Q[1] * Q[1] + Q[2] * Q[2] + Q[3] * Q[3]);
		if(Length < 0.000001f)
			return {1.0f, 0.0f, 0.0f, 0.0f};
		return {Q[0] / Length, Q[1] / Length, Q[2] / Length, Q[3] / Length};
	}

	TQuaternion QuaternionMultiply(const TQuaternion &A, const TQuaternion &B)
	{
		return {
			A[0] * B[0] - A[1] * B[1] - A[2] * B[2] - A[3] * B[3],
			A[0] * B[1] + A[1] * B[0] + A[2] * B[3] - A[3] * B[2],
			A[0] * B[2] - A[1] * B[3] + A[2] * B[0] + A[3] * B[1],
			A[0] * B[3] + A[1] * B[2] - A[2] * B[1] + A[3] * B[0]};
	}

	TQuaternion QuaternionFromAxisAngle(vec3 Axis, float Angle)
	{
		if(length(Axis) < 0.000001f || std::abs(Angle) < 0.000001f)
			return {1.0f, 0.0f, 0.0f, 0.0f};
		Axis = normalize(Axis);
		const float HalfAngle = Angle * 0.5f;
		const float Sine = std::sin(HalfAngle);
		return {std::cos(HalfAngle), Axis.x * Sine, Axis.y * Sine, Axis.z * Sine};
	}

	vec3 QuaternionRotate(const TQuaternion &Q, vec3 V)
	{
		const vec3 VectorPart(Q[1], Q[2], Q[3]);
		const vec3 TwiceCross = cross(VectorPart, V) * 2.0f;
		return V + TwiceCross * Q[0] + cross(VectorPart, TwiceCross);
	}

	float QuaternionDot(const TQuaternion &A, const TQuaternion &B)
	{
		return A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + A[3] * B[3];
	}

	TQuaternion QuaternionConjugate(const TQuaternion &Q)
	{
		return {Q[0], -Q[1], -Q[2], -Q[3]};
	}

	float RotationError(const TQuaternion &Orientation, const TQuaternion &Target, vec3 *pAxis)
	{
		TQuaternion Error = QuaternionNormalize(QuaternionMultiply(Target, QuaternionConjugate(Orientation)));
		if(Error[0] < 0.0f)
			for(float &Value : Error)
				Value = -Value;
		const float Angle = 2.0f * std::acos(std::clamp(Error[0], -1.0f, 1.0f));
		*pAxis = vec3(Error[1], Error[2], Error[3]);
		if(length(*pAxis) > 0.000001f)
			*pAxis = normalize(*pAxis);
		else
			*pAxis = vec3();
		return Angle;
	}

	TQuaternion CanonicalFaceOrientation(int FaceValue)
	{
		switch(FaceValue)
		{
		case 1: return {1.0f, 0.0f, 0.0f, 0.0f};
		case 2: return QuaternionFromAxisAngle(vec3(0.0f, 1.0f, 0.0f), -DICE_PI * 0.5f);
		case 3: return QuaternionFromAxisAngle(vec3(1.0f, 0.0f, 0.0f), DICE_PI * 0.5f);
		case 4: return QuaternionFromAxisAngle(vec3(1.0f, 0.0f, 0.0f), -DICE_PI * 0.5f);
		case 5: return QuaternionFromAxisAngle(vec3(0.0f, 1.0f, 0.0f), DICE_PI * 0.5f);
		default: return QuaternionFromAxisAngle(vec3(0.0f, 1.0f, 0.0f), DICE_PI);
		}
	}

	TQuaternion NearestStableOrientation(const TQuaternion &Orientation)
	{
		TQuaternion Best = {1.0f, 0.0f, 0.0f, 0.0f};
		float BestAlignment = -1.0f;
		for(int FaceValue = 1; FaceValue <= 6; FaceValue++)
		{
			for(int QuarterTurn = 0; QuarterTurn < 4; QuarterTurn++)
			{
				const TQuaternion ScreenRotation = QuaternionFromAxisAngle(vec3(0.0f, 0.0f, 1.0f), QuarterTurn * DICE_PI * 0.5f);
				TQuaternion Candidate = QuaternionNormalize(QuaternionMultiply(ScreenRotation, CanonicalFaceOrientation(FaceValue)));
				const float SignedAlignment = QuaternionDot(Orientation, Candidate);
				const float Alignment = std::abs(SignedAlignment);
				if(Alignment > BestAlignment)
				{
					BestAlignment = Alignment;
					if(SignedAlignment < 0.0f)
						for(float &Value : Candidate)
							Value = -Value;
					Best = Candidate;
				}
			}
		}
		return Best;
	}

	struct SFace
	{
		vec3 m_Normal;
		vec3 m_Right;
		vec3 m_Down;
		int m_Value;
	};

	constexpr std::array<vec3, 8> gs_aVertices = {
		vec3(-DICE_HALF_SIZE, -DICE_HALF_SIZE, -DICE_HALF_SIZE),
		vec3(DICE_HALF_SIZE, -DICE_HALF_SIZE, -DICE_HALF_SIZE),
		vec3(DICE_HALF_SIZE, DICE_HALF_SIZE, -DICE_HALF_SIZE),
		vec3(-DICE_HALF_SIZE, DICE_HALF_SIZE, -DICE_HALF_SIZE),
		vec3(-DICE_HALF_SIZE, -DICE_HALF_SIZE, DICE_HALF_SIZE),
		vec3(DICE_HALF_SIZE, -DICE_HALF_SIZE, DICE_HALF_SIZE),
		vec3(DICE_HALF_SIZE, DICE_HALF_SIZE, DICE_HALF_SIZE),
		vec3(-DICE_HALF_SIZE, DICE_HALF_SIZE, DICE_HALF_SIZE)};

	// Face order: front, right, bottom, top, left, back.
	constexpr std::array<SFace, 6> gs_aFaces = {{
		{vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 1, 0), 1},
		{vec3(1, 0, 0), vec3(0, 0, -1), vec3(0, 1, 0), 2},
		{vec3(0, 1, 0), vec3(1, 0, 0), vec3(0, 0, -1), 3},
		{vec3(0, -1, 0), vec3(1, 0, 0), vec3(0, 0, 1), 4},
		{vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), 5},
		{vec3(0, 0, -1), vec3(-1, 0, 0), vec3(0, 1, 0), 6},
	}};

	struct SEdge
	{
		int m_VertexA;
		int m_VertexB;
		int m_FaceA;
		int m_FaceB;
	};

	constexpr std::array<SEdge, 12> gs_aEdges = {{
		{0, 1, 3, 5},
		{1, 2, 1, 5},
		{2, 3, 2, 5},
		{3, 0, 4, 5},
		{4, 5, 0, 3},
		{5, 6, 0, 1},
		{6, 7, 0, 2},
		{7, 4, 0, 4},
		{0, 4, 3, 4},
		{1, 5, 3, 1},
		{2, 6, 1, 2},
		{3, 7, 2, 4},
	}};

	void ConHoDice3D(IConsole::IResult *pResult, void *pUserData)
	{
		auto *pGameServer = static_cast<CGameContext *>(pUserData);
		const int ClientId = pResult->GetInteger(0);
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pGameServer->m_apPlayers[ClientId])
		{
			pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_dice3d", "invalid client ID");
			return;
		}
		CCharacter *pCharacter = pGameServer->GetPlayerChar(ClientId);
		if(!pCharacter)
		{
			pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_dice3d", "the player has no active character");
			return;
		}
		new CHoDice3D(&pGameServer->m_World, pCharacter->GetPos() + vec2(0.0f, -96.0f));
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_dice3d", "spawned a simulated 3D dice");
	}

	void ConHoDice3DClear(IConsole::IResult *pResult, void *pUserData)
	{
		auto *pGameServer = static_cast<CGameContext *>(pUserData);
		int Removed = 0;
		for(CEntity *pEntity = pGameServer->m_World.FindFirst(CGameWorld::ENTTYPE_HO_DICE3D); pEntity; pEntity = pEntity->TypeNext())
		{
			pEntity->Reset();
			Removed++;
		}
		char aBuf[96];
		str_format(aBuf, sizeof(aBuf), "removed %d simulated 3D dice", Removed);
		pGameServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ho_dice3d", aBuf);
	}
} // namespace

CHoDice3D::CHoDice3D(CGameWorld *pGameWorld, vec2 Pos) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_HO_DICE3D, false, Pos, (int)DICE_HALF_SIZE)
{
	for(std::optional<int> &Id : m_aSnapIds)
		Id = Server()->SnapNewId();
	GameWorld()->InsertEntity(this);
}

CHoDice3D::~CHoDice3D()
{
	for(const std::optional<int> &Id : m_aSnapIds)
		if(Id.has_value())
			Server()->SnapFreeId(Id.value());
}

void CHoDice3D::Reset()
{
	m_MarkedForDestroy = true;
}

bool CHoDice3D::HammerHit(vec2 Impulse, vec2 HammererPos)
{
	if(!m_Resting)
		return false;

	const vec2 Lever = m_Pos - HammererPos;
	const float TorqueZ = (Lever.x * Impulse.y - Lever.y * Impulse.x) / (DICE_HALF_SIZE * DICE_HALF_SIZE);
	m_Velocity += Impulse;
	m_AngularVelocity += vec3(-Impulse.y * 0.012f, Impulse.x * 0.012f, TorqueZ * 0.08f);
	const float AngularSpeed = length(m_AngularVelocity);
	if(AngularSpeed > 0.45f)
		m_AngularVelocity = normalize(m_AngularVelocity) * 0.45f;
	m_StillTicks = 0;
	m_Resting = false;
	m_HasSettleOrientation = false;
	m_HasBeenThrown = true;
	m_ResultReported = false;
	return true;
}

void CHoDice3D::IntegrateOrientation()
{
	const float AngularSpeed = length(m_AngularVelocity);
	if(AngularSpeed < 0.000001f)
		return;
	m_Orientation = QuaternionNormalize(QuaternionMultiply(QuaternionFromAxisAngle(m_AngularVelocity, AngularSpeed), m_Orientation));
}

int CHoDice3D::DominantFaceValue() const
{
	int Result = 1;
	float BestDepth = -2.0f;
	for(const SFace &Face : gs_aFaces)
	{
		const float Depth = QuaternionRotate(m_Orientation, Face.m_Normal).z;
		if(Depth > BestDepth)
		{
			BestDepth = Depth;
			Result = Face.m_Value;
		}
	}
	return Result;
}

void CHoDice3D::Settle()
{
	m_Velocity = vec2();
	m_AngularVelocity = vec3();
	if(!m_HasSettleOrientation)
		m_SettleOrientation = NearestStableOrientation(m_Orientation);
	m_Orientation = m_SettleOrientation;
	m_Result = DominantFaceValue();
	m_Resting = true;
	m_StillTicks = 0;
	if(m_HasBeenThrown && !m_ResultReported)
	{
		char aBuf[96];
		str_format(aBuf, sizeof(aBuf), "3D dice physically settled on %d", m_Result);
		GameServer()->SendChat(-1, TEAM_ALL, aBuf);
		m_ResultReported = true;
	}
}

void CHoDice3D::Tick()
{
	if(m_Resting)
		return;
	if(GameLayerClipped(m_Pos))
	{
		Reset();
		return;
	}

	m_Velocity.y = std::min(m_Velocity.y + 0.35f, 18.0f);
	const vec2 IncomingVelocity = m_Velocity;
	bool Grounded = false;
	Collision()->MoveBox(&m_Pos, &m_Velocity, vec2(DICE_BODY_SIZE, DICE_BODY_SIZE), vec2(0.42f, 0.42f), &Grounded);
	const bool Supported = Grounded || Collision()->TestBox(m_Pos + vec2(0.0f, 2.0f), vec2(DICE_BODY_SIZE, DICE_BODY_SIZE));

	// Every angular impulse is derived from the hammer or from a measured
	// collision response. There is no random number and no preselected face.
	if(Grounded && IncomingVelocity.y > 1.0f)
	{
		const float Impact = std::clamp(IncomingVelocity.y, 0.0f, 18.0f);
		m_AngularVelocity.x += Impact * 0.008f;
		m_AngularVelocity.y += IncomingVelocity.x * 0.006f;
		m_AngularVelocity.z += IncomingVelocity.x * 0.003f;
	}
	if(IncomingVelocity.x * m_Velocity.x < 0.0f)
	{
		const float Impact = IncomingVelocity.x - m_Velocity.x;
		m_AngularVelocity.y += Impact * 0.008f;
		m_AngularVelocity.z -= Impact * 0.004f;
	}
	const float AngularSpeed = length(m_AngularVelocity);
	if(AngularSpeed > 0.45f)
		m_AngularVelocity = normalize(m_AngularVelocity) * 0.45f;

	// Once the cube has lost most of its translational energy, gravity and the
	// flat floor make it rock toward the nearest stable face. Apply that contact
	// torque over time instead of visibly snapping the orientation at the end.
	if(!Supported || length(m_Velocity) > 2.0f || length(m_AngularVelocity) > 0.20f)
		m_HasSettleOrientation = false;
	else if(!m_HasSettleOrientation)
	{
		m_SettleOrientation = NearestStableOrientation(m_Orientation);
		m_HasSettleOrientation = true;
	}
	if(m_HasSettleOrientation)
	{
		vec3 ErrorAxis;
		const float ErrorAngle = RotationError(m_Orientation, m_SettleOrientation, &ErrorAxis);
		m_AngularVelocity += ErrorAxis * std::min(ErrorAngle * 0.020f, 0.025f);
		m_AngularVelocity *= 0.86f;
	}

	IntegrateOrientation();
	m_Velocity.x *= Supported ? 0.88f : 0.995f;
	m_Velocity.y *= 0.995f;
	m_AngularVelocity *= Supported ? 0.88f : 0.995f;

	vec3 RemainingErrorAxis;
	const float RemainingError = m_HasSettleOrientation ? RotationError(m_Orientation, m_SettleOrientation, &RemainingErrorAxis) : DICE_PI;
	if(Supported && length(m_Velocity) < 0.20f && length(m_AngularVelocity) < 0.003f && RemainingError < 0.006f)
		m_StillTicks++;
	else
		m_StillTicks = 0;
	if(m_StillTicks >= 10)
		Settle();
}

void CHoDice3D::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;
	const CSnapContext Context(GameServer()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient);

	std::array<vec2, 8> aProjected{};
	for(int Vertex = 0; Vertex < (int)gs_aVertices.size(); Vertex++)
	{
		const vec3 Rotated = QuaternionRotate(m_Orientation, gs_aVertices[Vertex]);
		aProjected[Vertex] = m_Pos + vec2(Rotated.x, Rotated.y);
	}

	std::array<bool, 6> aVisible{};
	for(int Face = 0; Face < (int)gs_aFaces.size(); Face++)
		aVisible[Face] = QuaternionRotate(m_Orientation, gs_aFaces[Face].m_Normal).z > 0.035f;

	std::array<bool, 8> aVisibleVertices{};
	for(int EdgeIndex = 0; EdgeIndex < (int)gs_aEdges.size(); EdgeIndex++)
	{
		const SEdge &Edge = gs_aEdges[EdgeIndex];
		if(!aVisible[Edge.m_FaceA] && !aVisible[Edge.m_FaceB])
			continue;
		aVisibleVertices[Edge.m_VertexA] = true;
		aVisibleVertices[Edge.m_VertexB] = true;
		if(m_aSnapIds[EdgeIndex].has_value())
			GameServer()->SnapLaserObject(Context, m_aSnapIds[EdgeIndex].value(), aProjected[Edge.m_VertexB], aProjected[Edge.m_VertexA], -1, -1, LASERTYPE_DOOR, -1, -1, LASERFLAG_NO_PREDICT);
	}

	for(int Vertex = 0; Vertex < (int)aProjected.size(); Vertex++)
	{
		if(!aVisibleVertices[Vertex] || !m_aSnapIds[30 + Vertex].has_value())
			continue;
		const vec2 Pos = aProjected[Vertex];
		GameServer()->SnapLaserObject(Context, m_aSnapIds[30 + Vertex].value(), Pos, Pos + vec2(1.0f, 0.0f), -1, -1, LASERTYPE_DOOR, -1, -1, LASERFLAG_NO_PREDICT);
	}

	constexpr float PipX = 17.0f;
	constexpr float PipY = 19.0f;
	const std::array<vec2, 4> Corners = {vec2(-PipX, -PipY), vec2(PipX, -PipY), vec2(-PipX, PipY), vec2(PipX, PipY)};
	const std::array<vec2, 6> Six = {vec2(-PipX, -PipY), vec2(PipX, -PipY), vec2(-PipX, 0), vec2(PipX, 0), vec2(-PipX, PipY), vec2(PipX, PipY)};
	int PipSnapIndex = 12;
	for(int FaceIndex = 0; FaceIndex < (int)gs_aFaces.size(); FaceIndex++)
	{
		if(!aVisible[FaceIndex])
			continue;
		std::array<vec2, 6> aPips{};
		int NumPips = 0;
		auto AddPip = [&](vec2 Offset) { aPips[NumPips++] = Offset; };
		switch(gs_aFaces[FaceIndex].m_Value)
		{
		case 1: AddPip(vec2()); break;
		case 2:
			AddPip(Corners[0]);
			AddPip(Corners[3]);
			break;
		case 3:
			AddPip(Corners[0]);
			AddPip(vec2());
			AddPip(Corners[3]);
			break;
		case 4:
			for(const vec2 Offset : Corners)
				AddPip(Offset);
			break;
		case 5:
			for(const vec2 Offset : Corners)
				AddPip(Offset);
			AddPip(vec2());
			break;
		case 6:
			for(const vec2 Offset : Six)
				AddPip(Offset);
			break;
		}
		const SFace &Face = gs_aFaces[FaceIndex];
		for(int Pip = 0; Pip < NumPips && PipSnapIndex < 30; Pip++)
		{
			const int SnapIndex = PipSnapIndex++;
			if(!m_aSnapIds[SnapIndex].has_value())
				continue;
			const vec3 Local = Face.m_Normal * (DICE_HALF_SIZE + 1.0f) + Face.m_Right * aPips[Pip].x + Face.m_Down * aPips[Pip].y;
			const vec3 Rotated = QuaternionRotate(m_Orientation, Local);
			const vec2 Pos = m_Pos + vec2(Rotated.x, Rotated.y);
			GameServer()->SnapLaserObject(Context, m_aSnapIds[SnapIndex].value(), Pos, Pos + vec2(1.0f, 0.0f), -1, -1, LASERTYPE_RIFLE);
		}
	}
}

void HoRegisterDice3DCommands(CGameContext *pGameServer)
{
	pGameServer->Console()->Register("ho_dice3d", "i[client-id]", CFGFLAG_SERVER, ConHoDice3D, pGameServer, "Spawn a simulated 3D dice above a player");
	pGameServer->Console()->Register("ho_dice3d_clear", "", CFGFLAG_SERVER, ConHoDice3DClear, pGameServer, "Remove all simulated 3D dice");
}

int HoDice3DHammerHit(CGameContext *pGameServer, vec2 HammerPos, vec2 HammererPos, CClientMask Mask)
{
	CEntity *apEntities[16];
	const int Num = pGameServer->m_World.FindEntities(HammerPos, 18.0f, apEntities, 16, CGameWorld::ENTTYPE_HO_DICE3D);
	int Hits = 0;
	for(int i = 0; i < Num; i++)
	{
		auto *pDice = static_cast<CHoDice3D *>(apEntities[i]);
		vec2 Direction = pDice->GetPos() - HammererPos;
		Direction = length(Direction) > 0.001f ? normalize(Direction) : vec2(0.0f, -1.0f);
		if(!pDice->HammerHit(Direction * 7.0f + vec2(0.0f, -8.0f), HammererPos))
			continue;
		pGameServer->CreateHammerHit(pDice->GetPos() - Direction * 18.0f, Mask);
		Hits++;
	}
	return Hits;
}
