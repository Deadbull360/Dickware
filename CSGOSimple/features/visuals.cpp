
#include <algorithm>

#include "../definitions.h"
#include "visuals.hpp"
#include "../valve_sdk/sdk.hpp"

//#include "../options.hpp"
#include "../Settings.h"
#include "../ConfigSystem.h"
#include "../helpers/math.hpp"
#include "../helpers/utils.hpp"
#include "../Resolver.h"
#include "../RuntimeSaver.h"
#include "../Logger.h"
#include "../ConsoleHelper.h"
#include "../Autowall.h"
#include "../Rbot.h"
#include "../helpers/C_Texture.h"
#include "../resource.h"
#include "../Lbot.h"
#include <chrono>
#include <ctime>

RECT GetBBox ( C_BaseEntity* ent )
{
    RECT rect{};
    auto collideable = ent->GetCollideable();

    if ( !collideable )
        return rect;

    auto min = collideable->OBBMins();
    auto max = collideable->OBBMaxs();

    const matrix3x4_t& trans = ent->m_rgflCoordinateFrame();

    Vector points[] =
    {
        Vector ( min.x, min.y, min.z ),
        Vector ( min.x, max.y, min.z ),
        Vector ( max.x, max.y, min.z ),
        Vector ( max.x, min.y, min.z ),
        Vector ( max.x, max.y, max.z ),
        Vector ( min.x, max.y, max.z ),
        Vector ( min.x, min.y, max.z ),
        Vector ( max.x, min.y, max.z )
    };

    Vector pointsTransformed[8];

    for ( int i = 0; i < 8; i++ )
        Math::VectorTransform ( points[i], trans, pointsTransformed[i] );

    Vector screen_points[8] = {};

    for ( int i = 0; i < 8; i++ )
    {
        if ( !Math::WorldToScreen ( pointsTransformed[i], screen_points[i] ) )
            return rect;
    }

    auto left = screen_points[0].x;
    auto top = screen_points[0].y;
    auto right = screen_points[0].x;
    auto bottom = screen_points[0].y;

    for ( int i = 1; i < 8; i++ )
    {
        if ( left > screen_points[i].x )
            left = screen_points[i].x;

        if ( top < screen_points[i].y )
            top = screen_points[i].y;

        if ( right < screen_points[i].x )
            right = screen_points[i].x;

        if ( bottom > screen_points[i].y )
            bottom = screen_points[i].y;
    }

    return RECT{ ( long ) left, ( long ) top, ( long ) right, ( long ) bottom };
}

Visuals::Visuals()
{
    InitializeCriticalSection ( &cs );
}

Visuals::~Visuals()
{
    DeleteCriticalSection ( &cs );
}

//--------------------------------------------------------------------------------
void Visuals::Render()
{
}
//--------------------------------------------------------------------------------
bool Visuals::Player::Begin ( C_BasePlayer* pl )
{
    if ( pl->IsDormant() || !pl->IsAlive() )
        return false;

    ctx.pl = pl;
    ctx.is_enemy = pl->IsEnemy();
    ctx.is_visible = g_LocalPlayer->CanSeePlayer ( pl, HITBOX_CHEST );

    //if (!ctx.is_enemy && g_Config.GetBool("esp_enemies_only")) //to fix
    //	return false;

    ctx.clr = ctx.is_enemy ? ( ctx.is_visible ? g_Config.GetColor ( "color_esp_enemy_visible" ) : g_Config.GetColor ( "color_esp_enemy_occluded" ) ) : ( ctx.is_visible ? g_Config.GetColor ( "color_esp_ally_visible" ) : g_Config.GetColor ( "color_esp_ally_occluded" ) );

    auto head = pl->GetHitboxPos ( HITBOX_HEAD );
    auto origin = pl->m_vecOrigin();

    head.z += 15;

    if ( !Math::WorldToScreen ( head, ctx.head_pos ) ||
            !Math::WorldToScreen ( origin, ctx.feet_pos ) )
        return false;

    auto h = fabs ( ctx.head_pos.y - ctx.feet_pos.y );
    auto w = h / 1.65f;

    ctx.bbox.left = static_cast<long> ( ctx.feet_pos.x - w * 0.5f );
    ctx.bbox.right = static_cast<long> ( ctx.bbox.left + w );
    ctx.bbox.bottom = static_cast<long> ( ctx.feet_pos.y );
    ctx.bbox.top = static_cast<long> ( ctx.head_pos.y );

    if ( ctx.bbox.left > ctx.bbox.right )
    {
        ctx.bbox.left = ctx.bbox.right;
        ctx.ShouldDrawBox = false;
    }

    if ( ctx.bbox.bottom < ctx.bbox.top )
    {
        ctx.bbox.bottom = ctx.bbox.top;
        ctx.ShouldDrawBox = false;
    }

    return true;
}
//--------------------------------------------------------------------------------
void Visuals::Player::RenderBox()
{
    if ( !g_LocalPlayer || !ctx.ShouldDrawBox )
        return;

    int mode = ctx.boxmode;

    if ( !g_LocalPlayer->IsAlive() )
        ctx.boxmode = 0;

    switch ( ctx.boxmode )
    {
        case 0:
            //Render::Get().RenderBoxByType(ctx.bbox.left, ctx.bbox.top, ctx.bbox.right, ctx.bbox.bottom, ctx.clr, 1.f);
            VGSHelper::Get().DrawBox ( ctx.bbox.left, ctx.bbox.top, ctx.bbox.right, ctx.bbox.bottom, ctx.BoxClr, 1.f );
            break;

        case 1:
            float edge_size = 25.f;

            if ( ctx.pl != g_LocalPlayer )
                edge_size = 4000.f / Math::VectorDistance ( g_LocalPlayer->m_vecOrigin(), ctx.pl->m_vecOrigin() );

            VGSHelper::Get().DrawBoxEdges ( ctx.bbox.left, ctx.bbox.top, ctx.bbox.right, ctx.bbox.bottom, ctx.BoxClr, edge_size, 1.f );

            break;
    }
}
//--------------------------------------------------------------------------------
void Visuals::Player::RenderName()
{
    player_info_t info = ctx.pl->GetPlayerInfo();
	std::string name = info.szName;
    auto sz = g_pDefaultFont->CalcTextSizeA ( 12.f, FLT_MAX, 0.0f, info.szName );

	//Render::Get().RenderTextNoOutline(name, ImVec2((ctx.bbox.left + ((ctx.bbox.right - ctx.bbox.left) / 2)) - (sz.x / 2), ctx.head_pos.y - sz.y - 4.f), 12.f, ctx.NameClr, true);
    VGSHelper::Get().DrawText ( info.szName, ( ctx.bbox.left + ( ( ctx.bbox.right - ctx.bbox.left ) / 2 ) ) - ( sz.x / 2 ), ctx.head_pos.y - sz.y - 4.f, ctx.NameClr, 12.f );
    //TextHeight += 14.f;
}
//--------------------------------------------------------------------------------
void Visuals::Player::RenderHealth()
{
    if ( !ctx.ShouldDrawBox )
        return;

    auto  hp = ctx.pl->m_iHealth();

    if ( hp > 100 )
        hp = 100;

    int green = int ( hp * 2.55f );
    int red = 255 - green;

    RenderLine ( ctx.healthpos, Color ( red, green, 0, 255 ), ( hp ) / 100.f );
}
//--------------------------------------------------------------------------------
void Visuals::Player::RenderArmour()
{
    if ( !ctx.ShouldDrawBox )
        return;

    auto armour = ctx.pl->m_ArmorValue();
    RenderLine ( ctx.armourpos, ctx.ArmourClr, ( armour ) / 100.f );
}
void Visuals::Player::RenderLbyUpdateBar()
{
    if ( !ctx.ShouldDrawBox )
        return;

    int i = ctx.pl->EntIndex();

    if ( ctx.pl->m_vecVelocity().Length2D() > 0.1f || ! ( ctx.pl->m_fFlags() & FL_ONGROUND ) )
        return;

    if ( !g_Resolver.GResolverData[i].CanuseLbyPrediction )
        return;

    float percent = 1.f - ( ( g_Resolver.GResolverData[i].NextPredictedLbyBreak - ctx.pl->m_flSimulationTime() ) / 1.1f );

    if ( percent < 0.f || percent > 1.f )
        return;

    RenderLine ( ctx.lbyupdatepos, ctx.LbyTimerClr, percent );
}
//--------------------------------------------------------------------------------
void Visuals::Player::RenderWeaponName()
{
	auto weapon = ctx.pl->m_hActiveWeapon().Get();

	if (!weapon)
		return;

	auto text = weapon->GetCSWeaponData()->szWeaponName + 7;
	auto sz = g_pDefaultFont->CalcTextSizeA(12, FLT_MAX, 0.0f, text);
	//VGSHelper::Get().DrawText ( text, ctx.bbox.right + 2.f + ctx.PosHelper.right, ctx.head_pos.y - sz.y + TextHeight, ctx.WeaponClr, 12 );
	//VGSHelper::Get().DrawText ( text, ctx.bbox.right + 2.f + ctx.PosHelper.right, ctx.head_pos.y - sz.y + TextHeight, ctx.WeaponClr, 12 );
	VGSHelper::Get().DrawIcon((wchar_t)g_WeaponIcons[weapon->GetItemDefinitionIndex()], ctx.bbox.right + 2.f + ctx.PosHelper.right, ctx.head_pos.y - sz.y + TextHeight, ctx.WeaponClr, 18);
	
	//Render::Get().RenderTextNoOutline("S", ImVec2(ctx.bbox.right + 2.f + ctx.PosHelper.bottom, ctx.feet_pos .y - sz.y), 18, ctx.WeaponClr, g_pIconFont);
	TextHeight += 12.f;
}

//--------------------------------------------------------------------------------
void Visuals::Player::RenderSnapline()
{

    int screen_w, screen_h;
    g_EngineClient->GetScreenSize ( screen_w, screen_h );

    //Render::Get().RenderLine(screen_w / 2.f, (float)screen_h,
    //	ctx.feet_pos.x, ctx.feet_pos.y, ctx.clr);

    VGSHelper::Get().DrawLine ( screen_w / 2.f, ( float ) screen_h, ctx.feet_pos.x, ctx.feet_pos.y, ctx.SnaplineClr );
}

void Visuals::Player::DrawPlayerDebugInfo()
{
    if ( !g_LocalPlayer || ctx.pl == g_LocalPlayer )
        return;

    if ( !ctx.pl->IsEnemy() )
        return;

    std::string t1 = "missed shots: " + std::to_string ( g_Resolver.GResolverData[ctx.pl->EntIndex()].Shots );
	std::string t2 = "mode: "; //+ std::to_string(g_Resolver.GResolverData[ctx.pl->EntIndex()].mode);
    std::string t3 = "detected: ";
    std::string t4 = g_Resolver.GResolverData[ctx.pl->EntIndex()].Fake ? "fake" : "real";
    std::string t5 = "velocity: " + std::to_string ( ctx.pl->m_vecVelocity().Length2D() );
    int i = ctx.pl->EntIndex();

    switch ( g_Resolver.GResolverData[i].mode )
    {
        case ResolverModes::NONE:
            t2 += "none";
            break;

        case ResolverModes::FREESTANDING:
            t2 += "FREESTANDING";
            break;

        case ResolverModes::EDGE:
            t2 += "EDGE";
            break;

        case ResolverModes::MOVE_STAND_DELTA:
            t2 += "MOVE_STAND_DELTA";
            break;

        case ResolverModes::FORCE_LAST_MOVING_LBY:
            t2 += "FORCE_LAST_MOVING_LBY";
            break;

        case ResolverModes::FORCE_FREESTANDING:
            t2 += "FORCE_FREESTANDING";
            break;

        case ResolverModes::BRUTFORCE_ALL_DISABLED:
            t2 += "BRUTFORCE_ALL_DISABLED";
            break;

        case ResolverModes::BRUTFORCE:
            t2 += "BRUTFORCE";
            break;

        case ResolverModes::FORCE_MOVE_STAND_DELTA:
            t2 += "FORCE_MOVE_STAND_DELTA";
            break;

        case ResolverModes::FORCE_LBY:
            t2 += "FORCE_LBY";
            break;

        case ResolverModes::MOVING:
            t2 += "MOVING";
            break;

        case ResolverModes::LBY_BREAK:
            t2 += "LBY_BREAK";
            break;

        case ResolverModes::SPINBOT:
            t2 += "SPINBOT";
            break;

        case ResolverModes::AIR_FREESTANDING:
            t2 += "AIR_FREESTANDING";
            break;

        case ResolverModes::AIR_BRUTFORCE:
            t2 += "AIR_BRUTFORCE";
            break;

        case ResolverModes::FAKEWALK_FREESTANDING:
            t2 += "FAKEWALK_FREESTANDING";
            break;

        case ResolverModes::FAKEWALK_BRUTFORCE:
            t2 += "FAKEWALK_BRUTFORCE";
            break;

        case ResolverModes::BACKWARDS:
            t2 += "BACKWARDS";
            break;

        case ResolverModes::FORCE_BACKWARDS:
            t2 += "FORCE_BACKWARDS";
            break;
    }

    switch ( g_Resolver.GResolverData[i].detection )
    {
        case ResolverDetections::FAKEWALKING:
            t3 += "Fakewalking";
            break;

        case ResolverDetections::AIR:
            t3 += "Air";
            break;

        case ResolverDetections::MOVING:
            t3 += "Moving";
            break;

        case ResolverDetections::STANDING:
            t3 += "Standing";
            break;
    }

    VGSHelper::Get().DrawText ( t1, ctx.bbox.right + 48.f, ctx.head_pos.y, Color::White );
    VGSHelper::Get().DrawText ( t2, ctx.bbox.right + 48.f, ctx.head_pos.y + 14.f, Color::White );
    VGSHelper::Get().DrawText ( t3, ctx.bbox.right + 48.f, ctx.head_pos.y + 28.f, Color::White );
    VGSHelper::Get().DrawText ( t4, ctx.bbox.right + 48.f, ctx.head_pos.y + 42.f, Color::White );
    VGSHelper::Get().DrawText ( t5, ctx.bbox.right + 48.f, ctx.head_pos.y + 56.f, Color::White );
}

void Visuals::Player::RenderLine ( DrawSideModes mode, Color color, float percent )
{
    float box_h = ( float ) fabs ( ctx.bbox.bottom - ctx.bbox.top );
    float box_w = ( float ) fabs ( ctx.bbox.right - ctx.bbox.left );
    float off = 4;
    float x = 0;
    float y = 0;
    float x2 = 0;
    float y2 = 0;

    //float x3 = 0;
    //float y3 = 0;
    switch ( mode )
    {
        case DrawSideModes::TOP:
            off = ctx.PosHelper.top;
            x = ctx.bbox.left;
            y = ctx.bbox.top + off;
            x2 = x + ( box_w * percent );
            y2 = y;
            ctx.PosHelper.top += 8;
            break;

        case DrawSideModes::RIGHT:
            off = ctx.PosHelper.right;
            x = ctx.bbox.right + off;
            y = ctx.bbox.top;
            x2 = x + 4;
            y2 = y + ( box_h * percent );
            ctx.PosHelper.right += 8;
            break;

        case DrawSideModes::BOTTOM:
            off = ctx.PosHelper.bottom;
            x = ctx.bbox.left;
            y = ctx.bbox.bottom + off;
            x2 = x + ( box_w * percent );
            y2 = y;
            ctx.PosHelper.bottom += 8;
            break;

        case DrawSideModes::LEFT:
            off = ctx.PosHelper.left;
            x = ctx.bbox.left - ( off * 2 );
            y = ctx.bbox.top;
            x2 = x + 4;
            y2 = y + ( box_h * percent );
            ctx.PosHelper.left += 8;
            break;
    }

    //Render::Get().RenderBox(x, y, x + w, y + h, Color::Black, 1.f, true);
    //Render::Get().RenderBox(x + 1, y + 1, x + w - 1, y + height - 2, Color(0, 50, 255, 255), 1.f, true);
	if (mode == DrawSideModes::LEFT || mode == DrawSideModes::RIGHT)
		//VGSHelper::Get().DrawFilledBox ( x, y, x2, y + box_h, Color ( 0, 0, 0, 100 ) );
		Render::Get().RenderBoxFilled(x, y, x2, y + box_h, Color(0, 0, 0, 100));
	else
		Render::Get().RenderBoxFilled(x, y, x + box_w, y2, Color(0, 0, 0, 100));
        //GSHelper::Get().DrawFilledBox ( x, y, x + box_w, y2, Color ( 0, 0, 0, 100 ) );

    //VGSHelper::Get().DrawFilledBox ( x + 1, y + 1, x2 - 1, y2 - 2, color );
	Render::Get().RenderBoxFilled(x + 1, y + 1, x2 - 1, y2 - 2, color);
}
void Visuals::Player::RenderResolverInfo()
{
    if ( g_Resolver.GResolverData[ctx.pl->EntIndex()].Fake )
    {
        char* t1 = "Fake";
        auto sz = g_pDefaultFont->CalcTextSizeA ( 12, FLT_MAX, 0.0f, t1 );
        //VGSHelper::Get().DrawText ( t1, ctx.bbox.right + 8.f, ctx.head_pos.y - sz.y + TextHeight, ctx.InfoClr, 12 );
        VGSHelper::Get().DrawText ( t1, ctx.bbox.right + 2.f + ctx.PosHelper.right, ctx.head_pos.y - sz.y + TextHeight, ctx.WeaponClr, 12 );
        TextHeight += 12.f;
    }

    if ( g_Resolver.GResolverData[ctx.pl->EntIndex()].BreakingLC )
    {
        char* t1 = "LC";
        auto sz = g_pDefaultFont->CalcTextSizeA ( 12, FLT_MAX, 0.0f, t1 );
        //VGSHelper::Get().DrawText ( t1, ctx.bbox.right + 8.f, ctx.head_pos.y - sz.y + TextHeight, ctx.InfoClr, 12 );
        VGSHelper::Get().DrawText ( t1, ctx.bbox.right + 2.f + ctx.PosHelper.right, ctx.head_pos.y - sz.y + TextHeight, ctx.WeaponClr, 12 );
        TextHeight += 12.f;
    }
}
//--------------------------------------------------------------------------------
void Visuals::RenderCrosshair()
{
    //int w, h;
	/*if (!g_LocalPlayer->m_hActiveWeapon()->IsSniper())
		return;

    //g_EngineClient->GetScreenSize ( w, h );

    int cx = ScreenX / 2;
    int cy = ScreenY / 2;
	Color clr = Color::Red; //g_Config.GetColor ( "color_esp_crosshair" );
    VGSHelper::Get().DrawLine ( cx - 5, cy, cx + 5, cy, clr );
    VGSHelper::Get().DrawLine ( cx, cy - 5, cx, cy + 5, clr );*/

	static bool active;
	static ConVar* weapon_debug_spread_show = g_CVar->FindVar("weapon_debug_spread_show");
	weapon_debug_spread_show->m_nFlags &= ~FCVAR_CHEAT;


	if (!active && Settings::Visual::SniperCrosshair)
		active = true;

	if (active && Settings::Visual::SniperCrosshair)
		active = false;

	if (active != weapon_debug_spread_show->GetInt())
	{
		if (active && g_LocalPlayer->m_hActiveWeapon()->IsSniper())
			g_EngineClient->ClientCmd_Unrestricted("weapon_debug_spread_show 3");
		else
			g_EngineClient->ClientCmd_Unrestricted("weapon_debug_spread_show 0");
	}

}
//--------------------------------------------------------------------------------
void Visuals::RenderWeapon ( C_BaseCombatWeapon* ent )
{
    auto clean_item_name = [] ( const char* name ) -> const char*
    {
        if ( name[0] == 'C' )
            name++;

        auto start = strstr ( name, "Weapon" );

        if ( start != nullptr )
            name = start + 6;

        return name;
    };

    // We don't want to Render weapons that are being held
    if ( ent->m_hOwnerEntity().IsValid() )
        return;

    auto bbox = GetBBox ( ent );

    if ( bbox.right == 0 || bbox.bottom == 0 )
        return;

    Color clr = Color::White; //g_Config.GetColor("color_esp_weapons");

    //Render::Get().RenderBox(bbox, clr);

    //VGSHelper::Get().DrawBox ( bbox.left, bbox.top, bbox.right, bbox.bottom, clr );

    auto name = clean_item_name ( ent->GetClientClass()->m_pNetworkName );

    auto sz = g_pDefaultFont->CalcTextSizeA ( 12.f, FLT_MAX, 0.0f, name );
    int w = bbox.right - bbox.left;

    //VGSHelper::Get().DrawText ( name, ( bbox.left + w * 0.5f ) - sz.x * 0.5f, bbox.bottom + 1, clr, 12 );
	VGSHelper::Get().DrawIcon((wchar_t)g_WeaponIcons[ent->GetItemDefinitionIndex()], (bbox.left + w * 0.5f) - sz.x * 0.5f, bbox.bottom + 1, clr);
	//Render::Get().RenderText(name, ImVec2((bbox.left + w * 0.5f) - sz.x * 0.5f, bbox.bottom + 1), 12.f, clr);
}
//--------------------------------------------------------------------------------
void Visuals::RenderDefuseKit ( C_BaseEntity* ent )
{
    if ( ent->m_hOwnerEntity().IsValid() )
        return;

    auto bbox = GetBBox ( ent );

    if ( bbox.right == 0 || bbox.bottom == 0 )
        return;

	Color clr = Color::White; //g_Config.GetColor ( "color_esp_defuse" );
    //Render::Get().RenderBox(bbox, clr);
    VGSHelper::Get().DrawBox ( bbox.left, bbox.top, bbox.right, bbox.bottom, clr );

    auto name = "Defuse Kit";
    auto sz = g_pDefaultFont->CalcTextSizeA ( 14.f, FLT_MAX, 0.0f, name );
    int w = bbox.right - bbox.left;
    //Render::Get().RenderText(name, ImVec2((bbox.left + w * 0.5f) - sz.x * 0.5f, bbox.bottom + 1), 14.f, clr);
    VGSHelper::Get().DrawText ( name, ( bbox.left + w * 0.5f ) - sz.x * 0.5f, bbox.bottom + 1, clr, 12 );
}
//--------------------------------------------------------------------------------
void Visuals::RenderPlantedC4 ( C_BaseEntity* ent )
{
    auto bbox = GetBBox ( ent );

    if ( bbox.right == 0 || bbox.bottom == 0 )
        return;

	Color clr = Settings::Visual::GlobalESP.BombColor;
	//Color timerClr = Color::Green;
 //   float bombTimer = ent->m_flC4Blow() - g_GlobalVars->curtime;

 //   if ( bombTimer < 0.f )
 //       return;

 //   //Render::Get().RenderBox(bbox, clr);
 //   //VGSHelper::Get().DrawBox ( bbox.left, bbox.top, bbox.right, bbox.bottom, clr );

 //   std::string timer = std::to_string ( bombTimer );

	//if (bombTimer <= 5.f)
	//	timerClr = Color::Red;
	//else if (bombTimer <= 10.f)
	//	timerClr = Color(255, 153, 0);
	//

    auto sz = g_pDefaultFont->CalcTextSizeA ( 12.f, FLT_MAX, 0.0f, "00" );
    int w = bbox.right - bbox.left;

	////int x, y;
	////g_EngineClient->GetScreenSize(x, y);
	//ImVec2 t = g_pDefaultFont->CalcTextSizeA(34.f, FLT_MAX, 0.0f, timer.data());

	//Render::Get().RenderTextNoOutline(timer.data(), ImVec2(ScreenX - 150, ScreenY / 2 - 340.f), 34.f, timerClr);

    //VGSHelper::Get().DrawText ( timer, ( bbox.left + w * 0.5f ) - sz.x * 0.5f, bbox.bottom + 1, clr, 12 );
	VGSHelper::Get().DrawIcon((wchar_t)'o', (bbox.left + w * 0.5f) - sz.x * 0.5f, bbox.bottom + 1, clr, 12);
}

void Visuals::RenderSoundESP()
{
	/*
	for (size_t i = 0; i < g_Saver.StepInfo.size(); i++)
	{
		if ((g_Saver.StepInfo[i].Origin != nullptr) || i > 16 || g_Saver.StepInfo[i].Time < g_GlobalVars->curtime)
			g_Saver.StepInfo.erase(g_Saver.StepInfo.begin() + i);

		C_BasePlayer* ent = static_cast<C_BasePlayer*>(g_EntityList->GetClientEntity(g_Saver.StepInfo[i].EntityIndex));

		if (ent->IsEnemy())
			VGSHelper::Get().DrawWave(g_Saver.StepInfo[i].Origin, g_Saver.StepInfo[i].Radius, Color::Red);
		if (!ent->IsEnemy())
			VGSHelper::Get().DrawWave(g_Saver.StepInfo[i].Origin, g_Saver.StepInfo[i].Radius, Color::Green);

		g_Saver.StepInfo[i].Radius -= 0.5f;
	}
	*/

	if (!g_EngineClient->IsInGame() || !g_EngineClient->IsConnected())
		return;

	for (size_t i = 0; i < g_Saver.StepInfo.size(); i++)
	{
		Vector Pos2D;
		Math::WorldToScreen(g_Saver.StepInfo[i].Origin, Pos2D);
		VGSHelper::Get().DrawText("step", Pos2D.x, Pos2D.y, Color::White);
		if(g_GlobalVars->curtime > g_Saver.StepInfo[i].Time + 1.5f)
			g_Saver.StepInfo.erase(g_Saver.StepInfo.begin() + i);
	}
}

void Visuals::RenderBombESP(C_BaseEntity* ent)
{
	float flblow = ent->m_flC4Blow();//the time when the bomb will detonate
	float ExplodeTimeRemaining = flblow - (g_LocalPlayer->m_nTickBase() * g_GlobalVars->interval_per_tick);//subtract current time to get time remaining

	float fldefuse = ent->m_flDefuseCountDown();//time bomb is expected to defuse. if defuse is cancelled and started again this will be changed to the new value
	float DefuseTimeRemaining = fldefuse - (g_LocalPlayer->m_nTickBase() * g_GlobalVars->interval_per_tick);//subtract current time to get time remaining

	char TimeToExplode[64]; sprintf_s(TimeToExplode, "%.1f", ExplodeTimeRemaining);//Text we gonna display for explosion

	char TimeToDefuse[64]; sprintf_s(TimeToDefuse, "%.1f", DefuseTimeRemaining);//Text we gonna display for defuse

	int width, height;//text width and height for rendering in correct place. your cheat may get text height as a rect with both width and height
	ImVec2 t = g_pDefaultFont->CalcTextSizeA(34.f, FLT_MAX, 0.0f, TimeToExplode);

	if (ExplodeTimeRemaining > 0 && !ent->m_bBombDefused())//there is a period when u cant defuse the bomb and it hasn't exploded. > 0 check stops text showing then
	{									                      //also need to check if the bomb has been defused, cos otherwise it will just display time remaining when bomb was defused

		float fraction = ExplodeTimeRemaining / ent->m_flTimerLength(); //the proportion of time remaining, use fltimerlength cos bomb detonation time can vary by gamemode
		int onscreenwidth = fraction * ScreenX; //the width of the bomb timer bar. proportion of time remaining multiplied by width of screen

		float red = 255 - (fraction * 255); //make our bar fade from complete green to complete red
		float green = fraction * 255;


		g_VGuiSurface->DrawSetColor(red, green, 0, 140);
		g_VGuiSurface->DrawFilledRect(0, 0, onscreenwidth, 10);//rectangle from top left to the width of the bar and down 10 pixels
		Render::Get().RenderText(std::string(TimeToExplode), onscreenwidth - 10.f, 0.f, 12.f, Color::White); //render the time remaining as text beneath the end of the bar.
	}//could remove the "explode in" but why make things more complicated


	t = g_pDefaultFont->CalcTextSizeA(34.f, FLT_MAX, 0.0f, TimeToDefuse); //now we gonna do defuse bar. why add a new variable for text width when we can use the old one...
	C_BasePlayer* Defuser = (C_BasePlayer*)C_BasePlayer::get_entity_from_handle(ent->m_hBombDefuser());//this is the player whos is defusing the bomb

	if (Defuser) //if there is a player defusing the bomb. this check is needed or it will continue showing time if a player stops defusing
	{
		float fraction = DefuseTimeRemaining / ent->m_flTimerLength();
		int onscreenwidth = fraction * ScreenX;

		g_VGuiSurface->DrawSetColor(3, 117, 193, 140);//pick any color. lama uses blue so...
		g_VGuiSurface->DrawFilledRect(0, 10, onscreenwidth, 20);
		Render::Get().RenderText(std::string(TimeToDefuse), onscreenwidth - 10.f, 10.f, 12.f, Color::White); //once again, could be simplidied to just a number
	}
}

void Visuals::DrawFOV()
{
	auto pWeapon = g_LocalPlayer->m_hActiveWeapon();
	if (!pWeapon)
		return;

	int WeaponID = Settings::Aimbot::GetWeaponType(pWeapon);


	//auto settings = g_Options.legitbot_items[pWeapon->m_Item().m_iItemDefinitionIndex()];

	if (Settings::Aimbot::Enabled) 
	{

		float fov = static_cast<float>(g_LocalPlayer->GetFOV());

		int w, h;
		g_EngineClient->GetScreenSize(w, h);

		Vector2D screenSize = Vector2D(w, h);
		Vector2D center = screenSize * 0.5f;

		float ratio = screenSize.x / screenSize.y;
		float screenFov = atanf((ratio) * (0.75f) * tan(DEG2RAD(fov * 0.5f)));

		float radiusFOV = tanf(DEG2RAD(Lbot::Get().GetFov())) / tanf(screenFov) * center.x;

		Render::Get().RenderCircleFilled(center.x, center.y, radiusFOV, 32, Color(0, 0, 0, 50));
		Render::Get().RenderCircle(center.x, center.y, radiusFOV, 32, Color(0, 0, 0, 100));

		if (Settings::Aimbot::WeaponAimSetting[WeaponID].Silent) 
		{
			float silentRadiusFOV = tanf(DEG2RAD(Settings::Aimbot::WeaponAimSetting[WeaponID].SilentFOV)) / tanf(screenFov) * center.x;

			Render::Get().RenderCircleFilled(center.x, center.y, silentRadiusFOV, 32, Color(255, 25, 10, 50));
			Render::Get().RenderCircle(center.x, center.y, silentRadiusFOV, 32, Color(255, 25, 10, 100));
		}
	}
}

bool IsOnScreen(Vector origin, Vector& screen)
{
	if (!Math::WorldToScreen(origin, screen)) return false;
	int iScreenWidth, iScreenHeight;
	g_EngineClient->GetScreenSize(iScreenWidth, iScreenHeight);
	bool xOk = iScreenWidth > screen.x > 0, yOk = iScreenHeight > screen.y > 0;
	return xOk && yOk;
}

void Visuals::RenderOffscreenESP(C_BasePlayer* ent)
{
	if (!g_LocalPlayer) return;
	if (ent->IsDormant()) return;
	if (!ent->IsAlive()) return;

	Vector screenPos;
	QAngle client_viewangles;
	Vector hitboxPos;
	ent->GetHitboxPos(0, hitboxPos);
	//int screen_width = 0, screen_height = 0;
	float radius = 300.f;

	if (IsOnScreen(hitboxPos, screenPos)) return;

	g_EngineClient->GetViewAngles(client_viewangles);
	//g_EngineClient->GetScreenSize(screen_width, screen_height);

	const auto screen_center = Vector(ScreenX / 2.f, ScreenY / 2.f, 0);
	const auto rot = DEG2RAD(client_viewangles.yaw - Math::CalcAngle(g_LocalPlayer->GetEyePos(), hitboxPos).yaw - 90);

	std::vector<Vertex_t> vertices;

	vertices.push_back(Vertex_t(Vector2D(screen_center.x + cosf(rot) * radius, screen_center.y + sinf(rot) * radius)));
	vertices.push_back(Vertex_t(Vector2D(screen_center.x + cosf(rot + DEG2RAD(2)) * (radius - 16), screen_center.y + sinf(rot + DEG2RAD(2)) * (radius - 16))));
	vertices.push_back(Vertex_t(Vector2D(screen_center.x + cosf(rot - DEG2RAD(2)) * (radius - 16), screen_center.y + sinf(rot - DEG2RAD(2)) * (radius - 16))));

	VGSHelper::Get().DrawTriangle(3, vertices.data(), Settings::Visual::OffscreenESPColor);
}


void Visuals::DrawGrenade ( C_BaseEntity* ent )
{
    ClassId id = ent->GetClientClass()->m_ClassID;
    Vector vGrenadePos2D;
    Vector vGrenadePos3D = ent->m_vecOrigin();


    if ( !Math::WorldToScreen ( vGrenadePos3D, vGrenadePos2D ) )
        return;

    switch ( id )
    {
        case ClassId::CSmokeGrenadeProjectile:
			VGSHelper::Get().DrawIcon((wchar_t)'k', vGrenadePos2D.x, vGrenadePos2D.y, Color::White);
			break;

        case ClassId::CBaseCSGrenadeProjectile:
        {
            model_t* model = ent->GetModel();

            if ( !model )
            {
                VGSHelper::Get().DrawText ( "nade", vGrenadePos2D.x, vGrenadePos2D.y, Color::White, 12 );
                return;
            }

            studiohdr_t* hdr = g_MdlInfo->GetStudiomodel ( model );

            if ( !hdr )
            {
                VGSHelper::Get().DrawText ( "nade", vGrenadePos2D.x, vGrenadePos2D.y, Color::White, 12 );
                return;
            }

            std::string name = hdr->szName;

            if ( name.find ( "incendiarygrenade" ) != std::string::npos || name.find ( "fraggrenade" ) != std::string::npos )
            {
				VGSHelper::Get().DrawIcon((wchar_t)'n', vGrenadePos2D.x, vGrenadePos2D.y, Color::Red);
				return;
            }

			VGSHelper::Get().DrawIcon((wchar_t)'i', vGrenadePos2D.x, vGrenadePos2D.y, Color::White);
            break;
        }

        case ClassId::CMolotovProjectile:
			VGSHelper::Get().DrawIcon((wchar_t)'l', vGrenadePos2D.x, vGrenadePos2D.y, Color::Red);
            break;

        case ClassId::CDecoyProjectile:
			VGSHelper::Get().DrawIcon((wchar_t)'m', vGrenadePos2D.x, vGrenadePos2D.y, Color::White);
			break;
    }

	for (int i = 0; i < g_Saver.MolotovInfo.size(); i++)
	{
		float CurrentTime = g_LocalPlayer->m_nTickBase() * g_GlobalVars->interval_per_tick;
		//VGSHelper::Get().Draw3DCircle(g_Saver.MolotovInfo[i].Position, 150.f, 33, Color::Red);

		Vector screen;
		const std::string to_render = "time: " + std::to_string(g_Saver.MolotovInfo[i].TimeToExpire - CurrentTime); //hdr->szName;
		if (Math::WorldToScreen(g_Saver.MolotovInfo[i].Position, screen))
			//Render::Get().RenderText(to_render, screen.x, screen.y, 12.f, Color::Red);
			VGSHelper::Get().DrawText(to_render, screen.x, screen.y, Color::Red, 12);
	}

	for (int i = 0; i < g_Saver.SmokeInfo.size(); i++)
	{
		float CurrentTime = g_LocalPlayer->m_nTickBase() * g_GlobalVars->interval_per_tick;

		Vector screen;
		const std::string to_render = "smoke: " + std::to_string(g_Saver.SmokeInfo[i].TimeToExpire - CurrentTime); //hdr->szName;
		if (Math::WorldToScreen(g_Saver.SmokeInfo[i].Position, screen))
			VGSHelper::Get().DrawText(to_render, screen.x, screen.y, Color::White, 12);
			//Render::Get().RenderText(to_render, screen.x, screen.y, 13.f, Color::White);
	}

}
void Visuals::DrawDangerzoneItem ( C_BaseEntity* ent, float maxRange )
{
    if ( !g_LocalPlayer || !g_LocalPlayer->IsAlive() )
        return;

    ClientClass* cl = ent->GetClientClass();

    if ( !cl )
        return;

    ClassId id = cl->m_ClassID;
    //std::string name = cl->m_pNetworkName;

    std::string name = "unknown";

    const model_t* itemModel = ent->GetModel();

    if ( !itemModel )
        return;

    studiohdr_t* hdr = g_MdlInfo->GetStudiomodel ( itemModel );

    if ( !hdr )
        return;

    name = hdr->szName;



    if ( id != ClassId::CPhysPropAmmoBox && id != ClassId::CPhysPropLootCrate && id != ClassId::CPhysPropRadarJammer && id != ClassId::CPhysPropWeaponUpgrade )
        return;

    Vector vPos2D;
    Vector vPos3D = ent->m_vecOrigin();

    //vPos3D
    if ( g_LocalPlayer->m_vecOrigin().DistTo ( vPos3D ) > maxRange )
        return;

    if ( !Math::WorldToScreen ( vPos3D, vPos2D ) )
        return;

    if ( name.find ( "case_pistol" ) != std::string::npos )
        name = "pistol case";
    else if ( name.find ( "case_light_weapon" ) != std::string::npos ) // Reinforced!
        name = "light case";
    else if ( name.find ( "case_heavy_weapon" ) != std::string::npos )
        name = "heavy case";
    else if ( name.find ( "case_explosive" ) != std::string::npos )
        name = "explosive case";
    else if ( name.find ( "case_tools" ) != std::string::npos )
        name = "tools case";
    else if ( name.find ( "random" ) != std::string::npos )
        name = "airdrop";
    else if ( name.find ( "dz_armor_helmet" ) != std::string::npos )
        name = "full armor";
    else if ( name.find ( "dz_helmet" ) != std::string::npos )
        name = "helmet";
    else if ( name.find ( "dz_armor" ) != std::string::npos )
        name = "armor";
    else if ( name.find ( "upgrade_tablet" ) != std::string::npos )
        name = "tablet upgrade";
    else if ( name.find ( "briefcase" ) != std::string::npos )
        name = "briefcase";
    else if ( name.find ( "parachutepack" ) != std::string::npos )
        name = "parachute";
    else if ( name.find ( "dufflebag" ) != std::string::npos )
        name = "cash dufflebag";
    else if ( name.find ( "ammobox" ) != std::string::npos )
        name = "ammobox";

    VGSHelper::Get().DrawText ( name, vPos2D.x, vPos2D.y, Color::White, 12 );
}
//--------------------------------------------------------------------------------
void Visuals::ThirdPerson()
{
    if ( !g_LocalPlayer )
        return;

	if (Settings::Visual::ThirdPersonEnabled && g_LocalPlayer->IsAlive())
    {
        if ( !g_Input->m_fCameraInThirdPerson )
            g_Input->m_fCameraInThirdPerson = true;
    }
    else
        g_Input->m_fCameraInThirdPerson = false;
}

void Visuals::LbyIndicator()
{
    if ( !g_LocalPlayer || !g_LocalPlayer->IsAlive() )
        return;

    //int x, y;
    //g_EngineClient->GetScreenSize ( x, y );

    bool Moving = g_LocalPlayer->m_vecVelocity().Length2D() > 0.1;
    bool InAir = ! ( g_LocalPlayer->m_fFlags() & FL_ONGROUND );

    Color clr = Color::Green;

    if ( Moving && !InAir )
        clr = Color::Red;

    if ( fabs ( g_Saver.AARealAngle.yaw - g_LocalPlayer->m_flLowerBodyYawTarget() ) < 35.f )
        clr = Color::Red;

    float percent;

	percent = ( g_Saver.NextLbyUpdate - g_GlobalVars->curtime ) / 1.1f;

    percent = 1.f - percent;

    ImVec2 t = g_pDefaultFont->CalcTextSizeA ( 34.f, FLT_MAX, 0.0f, "LBY" );
    float width = t.x * percent;

    Render::Get().RenderLine ( 9.f, ScreenY - 100.f - ( CurrentIndicatorHeight - 34.f ), 11.f + t.x, ScreenY - 100.f - ( CurrentIndicatorHeight - 34.f ), Color ( 0, 0, 0, 25 ), 4.f );

    if ( width < t.x && width > 0.f )
        Render::Get().RenderLine ( 10.f, ScreenY - 100.f - ( CurrentIndicatorHeight - 34.f ), 10.f + width, ScreenY - 100.f - ( CurrentIndicatorHeight - 34.f ), clr, 2.f );

    Render::Get().RenderTextNoOutline ( "LBY", ImVec2 ( 10, ScreenY - 100.f - CurrentIndicatorHeight ), 34.f, clr );
    CurrentIndicatorHeight += 34.f;
}

void Visuals::PingIndicator()
{
    if ( !g_LocalPlayer || !g_LocalPlayer->IsAlive() )
        return;

    INetChannelInfo* nci = g_EngineClient->GetNetChannelInfo();

    if ( !nci )
        return;

    float ping = nci ? ( nci->GetAvgLatency ( FLOW_INCOMING ) ) * 1000.f : 0.0f;
    //int x, y;
    //g_EngineClient->GetScreenSize ( x, y );

    //std::string text = "PING: " + std::to_string(ping);
    float percent = ping / 100.f;
    ImVec2 t = g_pDefaultFont->CalcTextSizeA ( 34.f, FLT_MAX, 0.0f, "PING" );
    float width = t.x * percent;

    int green = int ( percent * 2.55f );
    int red = 255 - green;

    Render::Get().RenderLine ( 9.f, ScreenY - 100.f - ( CurrentIndicatorHeight - 34.f ), 11.f + t.x, ScreenY - 100.f - ( CurrentIndicatorHeight - 34.f ), Color ( 0, 0, 0, 25 ), 4.f );
    Render::Get().RenderLine ( 10.f, ScreenY - 100.f - ( CurrentIndicatorHeight - 34.f ), 10.f + width, ScreenY - 100.f - ( CurrentIndicatorHeight - 34.f ), Color ( red, green, 0 ), 2.f );
    Render::Get().RenderTextNoOutline ( "PING", ImVec2 ( 10, ScreenY - 100.f - CurrentIndicatorHeight ), 34.f, Color ( red, green, 0 ) );
    CurrentIndicatorHeight += 34.f;
}

void Visuals::LCIndicator()
{
    if ( !g_LocalPlayer || !g_LocalPlayer->IsAlive() || g_LocalPlayer->m_fFlags() & FL_ONGROUND )
        return;

    //int x, y;
    //g_EngineClient->GetScreenSize ( x, y );

    if ( ( g_LocalPlayer->m_fFlags() & FL_ONGROUND ) )
        return;

    //ImVec2 t = g_pDefaultFont->CalcTextSizeA(34.f, FLT_MAX, 0.0f, "LBY");
    Render::Get().RenderTextNoOutline ( "LC", ImVec2 ( 10, ScreenY - 100.f - CurrentIndicatorHeight ), 34.f, g_Saver.LCbroken ? Color::Green : Color::Red );
    CurrentIndicatorHeight += 34.f;
}

void Visuals::BAimIndicator()
{
	if (!g_LocalPlayer || !g_LocalPlayer->IsAlive())
		return;

	Color clr = Color::Red;
	//int x, y;
	//g_EngineClient->GetScreenSize(x, y);

	if (*Rbot::Get().GetBAimStatus() == BaimMode::FORCE_BAIM)
		clr = Color::Green;
	ImVec2 t = g_pDefaultFont->CalcTextSizeA(34.f, FLT_MAX, 0.0f, "BAIM");

	Render::Get().RenderTextNoOutline("BAIM", ImVec2(10, ScreenY - 100.f - CurrentIndicatorHeight), 34.f, clr);
	CurrentIndicatorHeight += 34.f;

}

void Visuals::DesyncIndicator()
{
	if (!g_LocalPlayer || !g_LocalPlayer->IsAlive())
		return;

	Color clr = Color::Green;

	float percent;

	percent = g_LocalPlayer->GetMaxDesyncAngle() / 58.f;

	ImVec2 t = g_pDefaultFont->CalcTextSizeA(34.f, FLT_MAX, 0.0f, "DESYNC");
	float width = t.x * percent;

	Render::Get().RenderLine(9.f, ScreenY - 100.f - (CurrentIndicatorHeight - 34.f), 11.f + t.x, ScreenY - 100.f - (CurrentIndicatorHeight - 34.f), Color(0, 0, 0, 25), 4.f);

	if (width <= t.x && width > 0.f)
		Render::Get().RenderLine(10.f, ScreenY - 100.f - (CurrentIndicatorHeight - 34.f), 10.f + width, ScreenY - 100.f - (CurrentIndicatorHeight - 34.f), clr, 2.f);

	Render::Get().RenderTextNoOutline("DESYNC", ImVec2(10, ScreenY - 100.f - CurrentIndicatorHeight), 34.f, clr);
	CurrentIndicatorHeight += 34.f;
}

void Visuals::AutowallCrosshair()
{
    /*
    if (!g_LocalPlayer || !g_LocalPlayer->IsAlive()) return;
    float Damage = 0.f;
    Autowall::Get().trace_awall(Damage);
    if (Damage != 0.f)
    {
    	int x, y;
    	g_EngineClient->GetScreenSize(x, y);

    	float cx = x / 2.f, cy = y / 2.f;

    	VGSHelper::Get().DrawText("Damage: "+std::to_string(Damage), cx, cy, Color::Green, 12);
    }
    */
}

void Visuals::RenderDamageIndicators()
{
	float CurrentTime = g_LocalPlayer->m_nTickBase() * g_GlobalVars->interval_per_tick;

	for (int i = 0; i < g_Saver.DamageIndicators.size(); i++)
	{
		if (g_Saver.DamageIndicators[i].flEraseTime < CurrentTime)
		{
			g_Saver.DamageIndicators.erase(g_Saver.DamageIndicators.begin() + i);
			continue;
		}

		if (!g_Saver.DamageIndicators[i].bInitialized)
		{
			g_Saver.DamageIndicators[i].Position = g_Saver.DamageIndicators[i].Player->GetHitboxPos(HITBOX_HEAD);
			g_Saver.DamageIndicators[i].bInitialized = true;
		}

		if (CurrentTime - g_Saver.DamageIndicators[i].flLastUpdate > 0.0001f)
		{
			g_Saver.DamageIndicators[i].Position.z -= (0.1f * (CurrentTime - g_Saver.DamageIndicators[i].flEraseTime));
			g_Saver.DamageIndicators[i].flLastUpdate = CurrentTime;
		}

		Vector ScreenPosition;

		if (Math::WorldToScreen(g_Saver.DamageIndicators[i].Position, ScreenPosition))
			VGSHelper::Get().DrawText(std::to_string(g_Saver.DamageIndicators[i].iDamage).c_str(), ScreenPosition.x, ScreenPosition.y, Settings::Visual::DamageIndicatorColor);
	}
}

void Visuals::ManualAAIndicator()
{
    if ( !g_LocalPlayer || !g_LocalPlayer->IsAlive() )
        return;

	if (Settings::RageBot::AntiAimSettings[0].Yaw != 5 || Settings::RageBot::AntiAimSettings[1].Yaw != 5)
		return;

    float cx = ScreenX / 2.f;
    float cy = ScreenY / 2.f;


	switch (Settings::RageBot::ManualAAState)
	{
		case 1:
			VGSHelper::Get().DrawText(">", cx + 34, cy - 20, Color::Red);
			VGSHelper::Get().DrawText("<", cx - 64, cy - 20, Color::White);
			VGSHelper::Get().DrawText("v", cx - 12, cy + 30, Color::White);
			break;
		case 2:
			VGSHelper::Get().DrawText(">", cx + 34, cy - 20, Color::White);
			VGSHelper::Get().DrawText("<", cx - 64, cy - 20, Color::Red);
			VGSHelper::Get().DrawText("v", cx - 12, cy + 30, Color::White);
			break;
		case 3:
			VGSHelper::Get().DrawText(">", cx + 34, cy - 20, Color::White);
			VGSHelper::Get().DrawText("<", cx - 64, cy - 20, Color::White);
			VGSHelper::Get().DrawText("v", cx - 12, cy + 30, Color::Red);
			break;
		default:
			VGSHelper::Get().DrawText(">", cx + 34, cy - 20, Color::White);
			VGSHelper::Get().DrawText("<", cx - 64, cy - 20, Color::White);
			VGSHelper::Get().DrawText("v", cx - 12, cy + 30, Color::White);
			break;
	}
}

void Visuals::NoFlash()
{
    if ( !g_LocalPlayer )
        return;

    g_LocalPlayer->m_flFlashMaxAlpha() = 0.f;
}

void Visuals::SpreadCircle()
{
    if ( !g_LocalPlayer || !g_LocalPlayer->IsAlive() )
        return;

    C_BaseCombatWeapon* weapon = g_LocalPlayer->m_hActiveWeapon().Get();

    if ( !weapon )
        return;

    float spread = weapon->GetInaccuracy() * 1000;

    if ( spread == 0.f )
        return;

    float cx = ScreenX / 2.f;
    float cy = ScreenY / 2.f;
	VGSHelper::Get().DrawFilledCircle(cx, cy, spread, 35, Settings::Visual::SpreadCircleColor);
}

void Visuals::RenderNoScoopeOverlay()
{
    if ( !g_LocalPlayer || !g_LocalPlayer->IsAlive() )
        return;

    static int cx;
    static int cy;
    cx = ScreenX / 2;
    cy = ScreenY / 2;

    if ( g_LocalPlayer->m_bIsScoped() )
    {
        VGSHelper::Get().DrawLine ( 0, cy, ScreenX, cy, Color::Black );
        VGSHelper::Get().DrawLine ( cx, 0, cx, ScreenY, Color::Black );
    }
}

void Visuals::RenderHitmarker()
{
    if ( !g_LocalPlayer || !g_LocalPlayer->IsAlive() )
        return;

	auto curtime = g_GlobalVars->realtime;
	float lineSize = 8.f;
	if (g_Saver.HitmarkerInfo.HitTime + .05f >= curtime) {
		int screenSizeX, screenCenterX;
		int screenSizeY, screenCenterY;
		g_EngineClient->GetScreenSize(screenSizeX, screenSizeY);

		screenCenterX = ScreenX / 2;
		screenCenterY = ScreenY / 2;

		Color bg = Color(0, 0, 0, 50);
		Color white = Color(255, 255, 255, 255);

		Render::Get().RenderLine(screenCenterX - lineSize, screenCenterY - lineSize, screenCenterX - (lineSize / 4), screenCenterY - (lineSize / 4), bg, 2.5f);
		Render::Get().RenderLine(screenCenterX - lineSize, screenCenterY + lineSize, screenCenterX - (lineSize / 4), screenCenterY + (lineSize / 4), bg, 2.5f);
		Render::Get().RenderLine(screenCenterX + lineSize, screenCenterY + lineSize, screenCenterX + (lineSize / 4), screenCenterY + (lineSize / 4), bg, 2.5f);
		Render::Get().RenderLine(screenCenterX + lineSize, screenCenterY - lineSize, screenCenterX + (lineSize / 4), screenCenterY - (lineSize / 4), bg, 2.5f);

		Render::Get().RenderLine(screenCenterX - lineSize, screenCenterY - lineSize, screenCenterX - (lineSize / 4), screenCenterY - (lineSize / 4), white);
		Render::Get().RenderLine(screenCenterX - lineSize, screenCenterY + lineSize, screenCenterX - (lineSize / 4), screenCenterY + (lineSize / 4), white);
		Render::Get().RenderLine(screenCenterX + lineSize, screenCenterY + lineSize, screenCenterX + (lineSize / 4), screenCenterY + (lineSize / 4), white);
		Render::Get().RenderLine(screenCenterX + lineSize, screenCenterY - lineSize, screenCenterX + (lineSize / 4), screenCenterY - (lineSize / 4), white);
	}
}



void Visuals::AddToDrawList()
{
	g_EngineClient->GetScreenSize(ScreenX, ScreenY);

    if ( !g_EngineClient->IsConnected() || !g_LocalPlayer || !g_EngineClient->IsInGame() )
        return;

	bool GrenadeEsp = Settings::Visual::GlobalESP.GrenadeEnabled; 

	DrawSideModes health_pos = (DrawSideModes)Settings::Visual::HealthPos;
	DrawSideModes armour_pos = (DrawSideModes)Settings::Visual::ArmorPos;

	bool esp_local_enabled = Settings::Visual::LocalESP.Enabled; 
	bool esp_team_enabled = Settings::Visual::TeamESP.Enabled;
	bool esp_enemy_enabled = Settings::Visual::EnemyESP.Enabled;

    bool esp_local_boxes = false;
    bool esp_local_weapons = false;
    bool esp_local_names = false;
    bool esp_local_health = false;
    bool esp_local_armour = false;
    int esp_local_boxes_type = 0;
    Color color_esp_local_boxes = Color ( 0, 0, 0 );
    Color color_esp_local_names = Color ( 0, 0, 0 );
    Color color_esp_local_armour = Color ( 0, 0, 0 );
    Color color_esp_local_weapons = Color ( 0, 0, 0 );

	if (esp_local_enabled && Settings::Visual::ThirdPersonEnabled)
    {
		esp_local_boxes = Settings::Visual::LocalESP.BoxEnabled;
		esp_local_weapons = Settings::Visual::LocalESP.WeaponEnabled;
		esp_local_names = Settings::Visual::LocalESP.NameEnabled;
		esp_local_health = Settings::Visual::LocalESP.HealthEnabled;
		esp_local_armour = Settings::Visual::LocalESP.ArmorEnabled;
		esp_local_boxes_type = Settings::Visual::LocalESP.BoxType;
		color_esp_local_boxes = Settings::Visual::LocalESP.BoxColor;
		color_esp_local_names = Settings::Visual::LocalESP.NameColor;
		color_esp_local_armour = Settings::Visual::LocalESP.ArmorColor;
		color_esp_local_weapons = Settings::Visual::LocalESP.WeaponColor;
    }

    bool esp_team_boxes = false;
    bool esp_team_snaplines = false;
    bool esp_team_weapons = false;
    bool esp_team_names = false;
    bool esp_team_health = false;
    bool esp_team_armour = false;
    int esp_team_boxes_type = 0;
    Color color_esp_team_boxes = Color ( 0, 0, 0 );
    Color color_esp_team_names = Color ( 0, 0, 0 );
    Color color_esp_team_armour = Color ( 0, 0, 0 );
    Color color_esp_team_weapons = Color ( 0, 0, 0 );
    Color color_esp_team_snaplines = Color ( 0, 0, 0 );

    if ( esp_team_enabled )
    {
		esp_team_boxes = Settings::Visual::TeamESP.BoxEnabled;
		esp_team_snaplines = Settings::Visual::TeamESP.SnaplineEnabled;
		esp_team_weapons = Settings::Visual::TeamESP.WeaponEnabled;
		esp_team_names = Settings::Visual::TeamESP.NameEnabled;
		esp_team_health = Settings::Visual::TeamESP.HealthEnabled;
		esp_team_armour = Settings::Visual::TeamESP.ArmorEnabled;
		esp_team_boxes_type = Settings::Visual::TeamESP.BoxType;
		color_esp_team_boxes = Settings::Visual::TeamESP.BoxColor;
		color_esp_team_names = Settings::Visual::TeamESP.NameColor;
		color_esp_team_armour = Settings::Visual::TeamESP.ArmorColor;
		color_esp_team_weapons = Settings::Visual::TeamESP.WeaponColor;
		color_esp_team_snaplines = Settings::Visual::TeamESP.SnaplineColor;
    }

    bool esp_enemy_boxes = false;
    bool esp_enemy_snaplines = false;
    bool esp_enemy_weapons = false;
    bool esp_enemy_names = false;
    bool esp_enemy_health = false;
    bool esp_enemy_armour = false;
    bool esp_enemy_info = false;
    bool esp_enemy_lby_timer = false;
    int esp_enemy_boxes_type = 0;
    Color color_esp_enemy_boxes = Color ( 0, 0, 0 );
    Color color_esp_enemy_names = Color ( 0, 0, 0 );
    Color color_esp_enemy_armour = Color ( 0, 0, 0 );
    Color color_esp_enemy_weapons = Color ( 0, 0, 0 );
    Color color_esp_enemy_snaplines = Color ( 0, 0, 0 );
    Color color_esp_enemy_info = Color ( 0, 0, 0 );
    Color color_esp_enemy_lby_timer = Color::Blue;

    if ( esp_enemy_enabled )
    {
		esp_enemy_boxes = Settings::Visual::EnemyESP.BoxEnabled;
		esp_enemy_snaplines = Settings::Visual::EnemyESP.SnaplineEnabled;
		esp_enemy_weapons = Settings::Visual::EnemyESP.WeaponEnabled;
		esp_enemy_names = Settings::Visual::EnemyESP.NameEnabled;
		esp_enemy_health = Settings::Visual::EnemyESP.HealthEnabled;
		esp_enemy_armour = Settings::Visual::EnemyESP.ArmorEnabled;
		esp_enemy_boxes_type = Settings::Visual::EnemyESP.BoxType;
		esp_enemy_info = false;
		esp_enemy_lby_timer = false;
		color_esp_enemy_boxes = Settings::Visual::EnemyESP.BoxColor;
		color_esp_enemy_names = Settings::Visual::EnemyESP.NameColor;
		color_esp_enemy_armour = Settings::Visual::EnemyESP.ArmorColor;
		color_esp_enemy_weapons = Settings::Visual::EnemyESP.WeaponColor;
		color_esp_enemy_snaplines = Settings::Visual::EnemyESP.SnaplineColor;
		color_esp_enemy_info = Color::White;
		color_esp_enemy_lby_timer = Color::White; 
    }

    bool esp_misc_dangerzone_item_esp = false;
    float esp_misc_dangerzone_item_esp_dist = 0.f;
    #ifdef _DEBUG
	bool misc_debug_overlay = Settings::Visual::DebugInfoEnabled;
    #endif // _DEBUG
    bool IsDangerzone = g_LocalPlayer && g_LocalPlayer->InDangerzone();

    if ( IsDangerzone )
    {
		esp_misc_dangerzone_item_esp = Settings::Visual::GlobalESP.DZEnabled;
		esp_misc_dangerzone_item_esp_dist = Settings::Visual::GlobalESP.DZRange;
    }

	bool rbot_resolver = Settings::RageBot::Resolver; 

	bool esp_dropped_weapons = Settings::Visual::GlobalESP.DropedWeaponsEnabled;
	bool esp_planted_c4 = Settings::Visual::GlobalESP.BombEnabled;


    for ( auto i = 1; i <= g_EntityList->GetHighestEntityIndex(); ++i )
    {
        auto entity = C_BaseEntity::GetEntityByIndex ( i );

        if ( !entity )
            continue;

        if ( i < 65 && ( esp_local_enabled || esp_team_enabled || esp_enemy_enabled ) )
        {
            auto player = Player();

			C_BasePlayer* plr = static_cast<C_BasePlayer*>(entity);
			if (Settings::Visual::GlobalESP.RadarType == 1 && plr->m_bSpotted() == false)
				plr->m_bSpotted() = true;

			if (Settings::Visual::OffscreenESPEnabled && g_LocalPlayer->m_iTeamNum() != plr->m_iTeamNum())
				RenderOffscreenESP(plr);

            if ( player.Begin ( ( C_BasePlayer* ) entity ) )
            {
                bool Enemy = player.ctx.pl->IsEnemy();
                bool Local = player.ctx.pl == g_LocalPlayer;
                bool Team = Team = !Enemy && !Local;

                if ( Local )
                {
                    player.ctx.BoxClr = color_esp_local_boxes;
                    player.ctx.NameClr = color_esp_local_names;
                    player.ctx.ArmourClr = color_esp_local_armour;
                    player.ctx.WeaponClr = color_esp_local_weapons;
                }
                else if ( Team )
                {
                    player.ctx.BoxClr = color_esp_team_boxes;
                    player.ctx.NameClr = color_esp_team_names;
                    player.ctx.ArmourClr = color_esp_team_armour;
                    player.ctx.WeaponClr = color_esp_team_weapons;
                    player.ctx.SnaplineClr = color_esp_team_snaplines;
                }
                else
                {
                    player.ctx.BoxClr = color_esp_enemy_boxes;
                    player.ctx.NameClr = color_esp_enemy_names;
                    player.ctx.ArmourClr = color_esp_enemy_armour;
                    player.ctx.WeaponClr = color_esp_enemy_weapons;
                    player.ctx.SnaplineClr = color_esp_enemy_snaplines;
                    player.ctx.InfoClr = color_esp_enemy_info;
                    player.ctx.LbyTimerClr = color_esp_enemy_lby_timer;
                }

                if ( Enemy )
                    player.ctx.boxmode = esp_enemy_boxes_type;
                else if ( Local )
                    player.ctx.boxmode = esp_local_boxes_type;
                else
                    player.ctx.boxmode = esp_team_boxes_type;

                player.ctx.healthpos = health_pos;
                player.ctx.armourpos = armour_pos;

                if ( ( Local && esp_local_enabled && esp_local_boxes ) || ( Team && esp_team_enabled && esp_team_boxes ) || ( Enemy && esp_enemy_enabled && esp_enemy_boxes ) )
                    player.RenderBox();

                if ( ( Team && esp_team_enabled && esp_team_snaplines ) || ( Enemy && esp_enemy_enabled && esp_enemy_snaplines ) )
                    player.RenderSnapline();

                if ( ( Local && esp_local_enabled && esp_local_names ) || ( Team && esp_team_enabled && esp_team_names ) || ( Enemy && esp_enemy_enabled && esp_enemy_names ) )
                    player.RenderName();

                if ( ( Local && esp_local_enabled && esp_local_health ) || ( Team && esp_team_enabled && esp_team_health ) || ( Enemy && esp_enemy_enabled && esp_enemy_health ) )
                    player.RenderHealth();

                if ( ( Local && esp_local_enabled && esp_local_armour ) || ( Team && esp_team_enabled && esp_team_armour ) || ( Enemy && esp_enemy_enabled && esp_enemy_armour ) )
                    player.RenderArmour();

                if ( rbot_resolver && Enemy && esp_enemy_info )
                    player.RenderResolverInfo();

                if ( ( Local && esp_local_enabled && esp_local_weapons ) || ( Team && esp_team_enabled && esp_team_weapons ) || ( Enemy && esp_enemy_enabled && esp_enemy_weapons ) )
                    player.RenderWeaponName();

                if ( Enemy && esp_enemy_lby_timer )
                    player.RenderLbyUpdateBar();

                #ifdef _DEBUG

                if ( misc_debug_overlay )
                    player.DrawPlayerDebugInfo();

                #endif // _DEBUG
            }
        }

        else if ( entity->IsWeapon() && esp_dropped_weapons )
            RenderWeapon ( static_cast<C_BaseCombatWeapon*> ( entity ) );
        else if ( entity->IsDefuseKit() && esp_dropped_weapons )
            RenderDefuseKit ( entity );
        else if ( entity->IsPlantedC4() && esp_planted_c4 )
            RenderPlantedC4 ( entity );

		if (entity->GetClientClass()->m_ClassID == ClassId::CPlantedC4 && Settings::Visual::GlobalESP.Enabled && Settings::Visual::GlobalESP.BombEnabled)
			RenderBombESP(entity);

        if ( IsDangerzone && esp_misc_dangerzone_item_esp )
            DrawDangerzoneItem ( entity, esp_misc_dangerzone_item_esp_dist );

        if ( GrenadeEsp )
            DrawGrenade ( entity );
		
		if(Settings::Visual::GlobalESP.SoundESPEnabled)
			RenderSoundESP();
    }

	if ( Settings::RageBot::Enabled )
    {
		BAimIndicator();
		if(Settings::RageBot::DesyncType > 0)
			DesyncIndicator();
		
		/* Not showing as intended
		auto drawAngleLine = [&](const Vector& origin, const Vector& w2sOrigin, const float& angle, const char* text, Color clr) {
			Vector forward;
			Math::AngleVectors(QAngle(0.0f, angle, 0.0f), forward);
			float AngleLinesLength = 30.0f;

			Vector w2sReal;
			if (Math::WorldToScreen(origin + forward * AngleLinesLength, w2sReal)) 
			{
				Render::Get().RenderLine(w2sOrigin.x, w2sOrigin.y, w2sReal.x, w2sReal.y, Color::White, 1.0f);
				Render::Get().RenderBoxFilled(w2sReal.x - 5.0f, w2sReal.y - 5.0f, w2sReal.x + 5.0f, w2sReal.y + 5.0f, Color::White);
				Render::Get().RenderText(text, ImVec2(w2sReal.x, w2sReal.y - 5.0f), 14.f, clr, true);
			}
		};

		if (Settings::RageBot::Desync > 0)
		{
			Vector w2sOrigin;
			if (Math::WorldToScreen(g_LocalPlayer->m_vecOrigin(), w2sOrigin)) 
			{
				drawAngleLine(g_LocalPlayer->m_vecOrigin(), w2sOrigin, g_Saver.DesyncYaw, "viewangles", Color(0.937f, 0.713f, 0.094f, 1.0f));
				drawAngleLine(g_LocalPlayer->m_vecOrigin(), w2sOrigin, g_LocalPlayer->m_flLowerBodyYawTarget(), "lby", Color(0.0f, 0.0f, 1.0f, 1.0f));
				drawAngleLine(g_LocalPlayer->m_vecOrigin(), w2sOrigin, g_Saver.RealYaw, "real", Color(0.0f, 1.0f, 0.0f, 1.0f));
			}
		}*/

		if (Settings::RageBot::AntiAimSettings[0].FakelagTicks || Settings::RageBot::AntiAimSettings[1].FakelagTicks || Settings::RageBot::AntiAimSettings[2].FakelagTicks)
            LCIndicator();
    }

	if (Settings::RageBot::Enabled && Settings::RageBot::EnabledAA)
		ManualAAIndicator();

	if ( Settings::Visual::SpreadCircleEnabled )
        SpreadCircle();

	if ( Settings::Visual::NoScopeOverlay )
        RenderNoScoopeOverlay();

	if ( Settings::Visual::Hitmarker )
        RenderHitmarker();
	if(Settings::Visual::DamageIndicator)
		RenderDamageIndicators();

    CurrentIndicatorHeight = 0.f;
   
}

void VGSHelper::Init()
{
	for (int size = 1; size < 128; size++)
	{
		Fonts[size] = g_VGuiSurface->CreateFont_();
		g_VGuiSurface->SetFontGlyphSet(Fonts[size], "Tahoma", size, 700, 0, 0, FONTFLAG_DROPSHADOW);
	}

	for (size_t size = 1; size < 128; size++)
	{
		WeaponFonts[size] = g_VGuiSurface->CreateFont_();
		g_VGuiSurface->SetFontGlyphSet(WeaponFonts[size], "undefeated", size, 700, 0, 0, FONTFLAG_ANTIALIAS);
	}

	for (size_t size = 1; size < 128; size++)
	{
		LogBase[size] = g_VGuiSurface->CreateFont_();
		g_VGuiSurface->SetFontGlyphSet(LogBase[size], "Verdana", size, 700, 0, 0, FONTFLAG_DROPSHADOW);
	}

	for (size_t size = 1; size < 128; size++)
	{
		LogHeader[size] = g_VGuiSurface->CreateFont_();
		g_VGuiSurface->SetFontGlyphSet(LogHeader[size], "Verdana", size, 700, 0, 0, FONTFLAG_DROPSHADOW);
	}

	Inited = true;
}

void VGSHelper::DrawText ( std::string text, float x, float y, Color color, int size )
{
    if ( !Inited )
        Init();

    g_VGuiSurface->DrawClearApparentDepth();
    wchar_t buf[256];
    g_VGuiSurface->DrawSetTextFont (Fonts[size] );
    g_VGuiSurface->DrawSetTextColor ( color );

    if ( MultiByteToWideChar ( CP_UTF8, 0, text.c_str(), -1, buf, 256 ) )
    {
        g_VGuiSurface->DrawSetTextPos ( x, y );
        g_VGuiSurface->DrawPrintText ( buf, wcslen ( buf ) );
    }
}

void VGSHelper::DrawLogHeader(std::string text, float x, float y, Color color, int size)
{
	if (!Inited)
		Init();

	g_VGuiSurface->DrawClearApparentDepth();
	wchar_t buf[256];
	g_VGuiSurface->DrawSetTextFont(LogHeader[size]);
	g_VGuiSurface->DrawSetTextColor(color);

	if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buf, 256))
	{
		g_VGuiSurface->DrawSetTextPos(x, y);
		g_VGuiSurface->DrawPrintText(buf, wcslen(buf));
	}
}

void VGSHelper::DrawLogBase(std::string text, float x, float y, Color color, int size)
{
	if (!Inited)
		Init();

	g_VGuiSurface->DrawClearApparentDepth();
	wchar_t buf[256];
	g_VGuiSurface->DrawSetTextFont(LogBase[size]);
	g_VGuiSurface->DrawSetTextColor(color);

	if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buf, 256))
	{
		g_VGuiSurface->DrawSetTextPos(x, y);
		g_VGuiSurface->DrawPrintText(buf, wcslen(buf));
	}
}

void VGSHelper::DrawLine ( float x1, float y1, float x2, float y2, Color color, float size )
{
    g_VGuiSurface->DrawSetColor ( color );

    if ( size == 1.f )
        g_VGuiSurface->DrawLine ( x1, y1, x2, y2 );
    else
        g_VGuiSurface->DrawFilledRect ( x1 - ( size / 2.f ), y1 - ( size / 2.f ), x2 + ( size / 2.f ), y2 + ( size / 2.f ) );
}
void VGSHelper::DrawBox ( float x1, float y1, float x2, float y2, Color clr, float size )
{
    DrawLine ( x1, y1, x2, y1, clr, size );
    DrawLine ( x1, y2, x2, y2, clr, size );
    DrawLine ( x1, y1, x1, y2, clr, size );
    DrawLine ( x2, y1, x2, y2, clr, size );
}
void VGSHelper::DrawFilledBox ( float x1, float y1, float x2, float y2, Color clr )
{
    g_VGuiSurface->DrawSetColor ( clr );
    //g_VGuiSurface->DrawSetApparentDepth(size);
    g_VGuiSurface->DrawFilledRect ( static_cast<int> ( x1 ), static_cast<int> ( y1 ), static_cast<int> ( x2 ), static_cast<int> ( y2 ) );

}
void VGSHelper::DrawTriangle ( int count, Vertex_t* vertexes, Color c )
{
    static int Texture = g_VGuiSurface->CreateNewTextureID ( true ); // need to make a texture with procedural true
    unsigned char buffer[4] = { ( unsigned char ) c.r(), ( unsigned char ) c.g(), ( unsigned char ) c.b(), ( unsigned char ) c.a() }; // r,g,b,a

    g_VGuiSurface->DrawSetTextureRGBA ( Texture, buffer, 1, 1 ); //Texture, char array of texture, width, height
    g_VGuiSurface->DrawSetColor ( c ); // keep this full color and opacity use the RGBA @top to set values.
    g_VGuiSurface->DrawSetTexture ( Texture ); // bind texture

    g_VGuiSurface->DrawTexturedPolygon ( count, vertexes );
}

void VGSHelper::DrawBoxEdges ( float x1, float y1, float x2, float y2, Color clr, float edge_size, float size )
{
    if ( fabs ( x1 - x2 ) < ( edge_size * 2 ) )
    {
        edge_size = fabs ( x1 - x2 ) / 4.f;
    }

    DrawLine ( x1, y1, x1, y1 + edge_size + ( 0.5f * edge_size ), clr, size );
    DrawLine ( x2, y1, x2, y1 + edge_size + ( 0.5f * edge_size ), clr, size );
    DrawLine ( x1, y2, x1, y2 - edge_size - ( 0.5f * edge_size ), clr, size );
    DrawLine ( x2, y2, x2, y2 - edge_size - ( 0.5f * edge_size ), clr, size );
    DrawLine ( x1, y1, x1 + edge_size, y1, clr, size );
    DrawLine ( x2, y1, x2 - edge_size, y1, clr, size );
    DrawLine ( x1, y2, x1 + edge_size, y2, clr, size );
    DrawLine ( x2, y2, x2 - edge_size, y2, clr, size );
}

void VGSHelper::DrawCircle ( float x, float y, float r, int seg, Color clr )
{
    g_VGuiSurface->DrawSetColor ( clr );
    g_VGuiSurface->DrawOutlinedCircle ( x, y, r, seg );
}

void VGSHelper::DrawFilledCircle(float x, float y, float r, int seg, Color clr)
{
	static bool once = true;

	static std::vector<float> temppointsx;
	static std::vector<float> temppointsy;

	if (once)
	{
		float step = (float)M_PI * 2.0f / seg;
		for (float a = 0; a < (M_PI * 2.0f); a += step)
		{
			temppointsx.push_back(cosf(a));
			temppointsy.push_back(sinf(a));
		}
		once = false;
	}

	std::vector<int> pointsx;
	std::vector<int> pointsy;
	std::vector<Vertex_t> vertices;

	for (int i = 0; i < temppointsx.size(); i++)
	{
		float fx = r * temppointsx[i] + x;
		float fy = r * temppointsy[i] + y;
		pointsx.push_back(fx);
		pointsy.push_back(fy);

		vertices.push_back(Vertex_t(Vector2D(fx, fy)));
	}

	g_VGuiSurface->DrawSetColor(clr);
	g_VGuiSurface->DrawTexturedPolygon(seg, vertices.data());

	//DrawTexturedPoly(points, vertices.data(), color);
	//g_pSurface->DrawSetColor(outline);
	//g_pSurface->DrawPolyLine(pointsx.data(), pointsy.data(), points); // only if you want en extra outline
}

void VGSHelper::DrawWave(Vector pos, float r, Color clr)
{
	static float Step = M_PI * 3.0f / 50;
	Vector prev = { 0, 0, 0 };
	for (float lat = 0; lat <= M_PI * 4.0f; lat += Step)
	{
		float
			sin1 = sin(lat),
			cos1 = cos(lat),
			sin3 = sin(0.0),
			cos3 = cos(0.0);

		Vector point1 = { 0, 0, 0 };
		point1 = Vector(sin1 * cos3, cos1, sin1 * sin3) * r;
		Vector point3 = pos;
		Vector Out = { 0, 0, 0 };
		point3 += point1;

		if (Math::WorldToScreen(point3, Out))
		{
			if (lat > 0.000)
				VGSHelper::Get().DrawLine(prev.x, prev.y, Out.x, Out.y, clr);
		}
		prev = Out;
	}
}

void VGSHelper::Draw3DCircle(Vector position, float radius, int seg, Color clr)
{
	Vector prev_scr_pos{ -1, -1, -1 };
	Vector scr_pos;

	float step = M_PI * 2.0 / seg;

	Vector origin = position;

	for (float rotation = 0; rotation < (M_PI * 2.0); rotation += step)
	{
		Vector pos(radius * cos(rotation) + origin.x, radius * sin(rotation) + origin.y, origin.z + 2);
		Vector tracepos(origin.x, origin.y, origin.z + 2);

		Ray_t ray;
		trace_t trace;
		CTraceFilter filter;

		ray.Init(tracepos, pos);

		g_EngineTrace->TraceRay(ray, MASK_SPLITAREAPORTAL, &filter, &trace);

		if (Math::WorldToScreen(trace.endpos, scr_pos))
		{
			if (prev_scr_pos != Vector{ -1, -1, -1 })
			{
				g_VGuiSurface->DrawSetColor(clr);
				g_VGuiSurface->DrawLine(prev_scr_pos.x, prev_scr_pos.y, scr_pos.x, scr_pos.y);
			}
			prev_scr_pos = scr_pos;
		}
	}
}

void VGSHelper::DrawIcon(wchar_t code, float x, float y, Color color, int size)
{
	if (!Inited)
		Init();

	if (size < 1 || size > 128)
		return;

	g_VGuiSurface->DrawSetTextColor(color);
	g_VGuiSurface->DrawSetTextPos(x, y);
	g_VGuiSurface->DrawSetTextFont(WeaponFonts[size]);
	g_VGuiSurface->DrawPrintText(&code, 1);
}

void VGSHelper::DrawWave(Vector pos, Color clr)
{
	BeamInfo_t beamInfo;
	beamInfo.m_nType = TE_BEAMRINGPOINT;
	beamInfo.m_pszModelName = "sprites/purplelaser1.vmt";
	beamInfo.m_nModelIndex = g_MdlInfo->GetModelIndex("sprites/purplelaser1.vmt");
	beamInfo.m_pszHaloName = "sprites/purplelaser1.vmt";
	beamInfo.m_nHaloIndex = g_MdlInfo->GetModelIndex("sprites/purplelaser1.vmt");
	beamInfo.m_flHaloScale = 5;
	beamInfo.m_flLife = 2.50f;
	beamInfo.m_flWidth = 12.f;
	beamInfo.m_flFadeLength = 1.0f;
	beamInfo.m_flAmplitude = 0.f;
	beamInfo.m_flRed = clr.r();
	beamInfo.m_flGreen = clr.g();
	beamInfo.m_flBlue = clr.b();
	beamInfo.m_flBrightness = 255;
	beamInfo.m_flSpeed = 1.f;
	beamInfo.m_nStartFrame = 0;
	beamInfo.m_flFrameRate = 1;
	beamInfo.m_nSegments = 1;
	beamInfo.m_bRenderable = true;
	beamInfo.m_nFlags = 0;
	beamInfo.m_vecCenter = pos + Vector(0, 0, 5);
	beamInfo.m_flStartRadius = 1;
	beamInfo.m_flEndRadius = 350;

	auto beam = g_RenderBeams->CreateBeamRingPoint(beamInfo);

	if (beam) g_RenderBeams->DrawBeam(beam);
}

ImVec2 VGSHelper::GetSize ( std::string text, int size )
{
    if ( !Inited )
        Init();

    wchar_t buf[256];
    int x, y;

    if ( MultiByteToWideChar ( CP_UTF8, 0, text.c_str(), -1, buf, 256 ) )
    {
        g_VGuiSurface->GetTextSize (Fonts[size], buf, x, y );
        return ImVec2 ( x, y );
    }

    return ImVec2 ( 0, 0 );
}