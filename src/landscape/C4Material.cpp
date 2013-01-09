/*
 * OpenClonk, http://www.openclonk.org
 *
 * Copyright (c) 1998-2000, 2007  Matthes Bender
 * Copyright (c) 2001-2002, 2005-2007, 2011  Sven Eberhardt
 * Copyright (c) 2006-2007  Peter Wortmann
 * Copyright (c) 2006-2007, 2009, 2011-2012  Günther Brammer
 * Copyright (c) 2010-2011  Nicolas Hake
 * Copyright (c) 2012  Armin Burgmeier
 * Copyright (c) 2001-2009, RedWolf Design GmbH, http://www.clonk.de
 *
 * Portions might be copyrighted by other authors who have contributed
 * to OpenClonk.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * See isc_license.txt for full license and disclaimer.
 *
 * "Clonk" is a registered trademark of Matthes Bender.
 * See clonk_trademark_license.txt for full license.
 */

/* Material definitions used by the landscape */

#include <C4Include.h>
#include <C4Material.h>
#include <C4Components.h>

#include <C4Group.h>
#include <C4PXS.h>
#include <C4Random.h>
#include <C4ToolsDlg.h> // For C4TLS_MatSky...
#include <C4Texture.h>
#include <C4Aul.h>
#include <C4Landscape.h>
#include <C4SoundSystem.h>
#include <C4Effect.h>
#include <C4Game.h>
#include <C4Log.h>
#include <C4Physics.h> // For GravAccel


int32_t MVehic=MNone,MTunnel=MNone,MWater=MNone,MEarth=MNone;
BYTE MCVehic=0;

// -------------------------------------- C4MaterialReaction


struct ReactionFuncMapEntry { const char *szRFName; C4MaterialReactionFunc pFunc; };

const ReactionFuncMapEntry ReactionFuncMap[] =
{
	{ "Script",  &C4MaterialMap::mrfScript },
	{ "Convert", &C4MaterialMap::mrfConvert},
	{ "Poof",    &C4MaterialMap::mrfPoof },
	{ "Corrode", &C4MaterialMap::mrfCorrode },
	{ "Insert",  &C4MaterialMap::mrfInsert },
	{ NULL, &C4MaterialReaction::NoReaction }
};


void C4MaterialReaction::CompileFunc(StdCompiler *pComp)
{
	if (pComp->isCompiler()) pScriptFunc = NULL;
	// compile reaction func ptr
	StdStrBuf sReactionFuncName;
	int32_t i=0; while (ReactionFuncMap[i].szRFName && (ReactionFuncMap[i].pFunc != pFunc)) ++i;
	sReactionFuncName = ReactionFuncMap[i].szRFName;
	pComp->Value(mkNamingAdapt(mkParAdapt(sReactionFuncName, StdCompiler::RCT_IdtfAllowEmpty),   "Type",                     StdCopyStrBuf() ));
	i=0; while (ReactionFuncMap[i].szRFName && !SEqual(ReactionFuncMap[i].szRFName, sReactionFuncName.getData())) ++i;
	pFunc = ReactionFuncMap[i].pFunc;
	// compile the rest
	pComp->Value(mkNamingAdapt(mkParAdapt(TargetSpec, StdCompiler::RCT_All),          "TargetSpec",               StdCopyStrBuf() ));
	pComp->Value(mkNamingAdapt(mkParAdapt(ScriptFunc, StdCompiler::RCT_IdtfAllowEmpty),          "ScriptFunc",               StdCopyStrBuf() ));
	pComp->Value(mkNamingAdapt(iExecMask,           "ExecMask",                 ~0u             ));
	pComp->Value(mkNamingAdapt(fReverse,            "Reverse",                  false           ));
	pComp->Value(mkNamingAdapt(fInverseSpec,        "InverseSpec",              false           ));
	pComp->Value(mkNamingAdapt(fInsertionCheck,     "CheckSlide",               true            ));
	pComp->Value(mkNamingAdapt(iDepth,              "Depth",                    0               ));
	pComp->Value(mkNamingAdapt(mkParAdapt(sConvertMat, StdCompiler::RCT_IdtfAllowEmpty),         "ConvertMat",               StdCopyStrBuf() ));
	pComp->Value(mkNamingAdapt(iCorrosionRate,      "CorrosionRate",            100             ));
}


void C4MaterialReaction::ResolveScriptFuncs(const char *szMatName)
{
	// get script func for script-defined behaviour
	if (pFunc == &C4MaterialMap::mrfScript)
	{
		pScriptFunc = ::ScriptEngine.GetPropList()->GetFunc(this->ScriptFunc.getData());
		if (!pScriptFunc)
			DebugLogF("Error getting function \"%s\" for Material reaction of \"%s\"", this->ScriptFunc.getData(), szMatName);
	}
	else
		pScriptFunc = NULL;
}

// -------------------------------------- C4MaterialShape

C4MaterialShape::C4MaterialShape() : prepared_for_zoom(0)
{
	wdt = hgt = overlap_left = overlap_top = overlap_right = overlap_bottom = 0;
	max_poly_width=max_poly_height=0;
}

bool C4MaterialShape::Load(C4Group &group, const char *filename)
{
	// Material shapes: Currently, shapes are loaded as a list polygons derived from vectorizing a binary image
	// In the future, vectorization of the image could be put directly into the engine (if we get a free* library to do it)
	// load file contents into buffer
	StdStrBuf source;
	if (!group.LoadEntryString(filename,&source)) return false;
	// parse buffer
	StdStrBuf name = group.GetFullName() + DirSep + filename;
	if (!CompileFromBuf_LogWarn<StdCompilerINIRead>(*this, source, name.getData())) return false;
	// Compute shape centers/mins/maxs and maximum overlap
	max_poly_width=max_poly_height=0;
	overlap_left=0; overlap_top=0; overlap_right=0; overlap_bottom=0;
	for (PolyVec::iterator i = polys.begin(); i != polys.end(); ++i)
	{
		int32_t n = 0; Pt center(0,0), min(0,0), max(0,0);
		for (Poly::iterator j = i->begin(); j != i->end(); ++j)
		{
			center.x += j->x; center.y += j->y;
			if (n++)
			{
				min.x = Min(min.x, j->x); max.x = Max(max.x, j->x);
				min.y = Min(min.y, j->y); max.y = Max(max.y, j->y);
			}
			else
			{
				min = max = *j;
			}
			if (j ->x<- overlap_left     ) overlap_left   =- j ->x;
			if (j ->y<- overlap_top      ) overlap_top    =- j ->y;
			if (j ->x> wdt+overlap_right ) overlap_right  =  j ->x- wdt;
			if (j ->y> hgt+overlap_bottom) overlap_bottom =  j ->y- hgt;
		}
		center.x /= n; center.y /= n;
		i->center = center; i->min = min; i->max = max;
		max_poly_width  = Max(max_poly_width , max.x-min.x);
		max_poly_height = Max(max_poly_height, max.y-min.y);
	}
	// Overlap data not calculated yet
	prepared_for_zoom = 0;
	return true;
}

void C4MaterialShape::CompileFunc(StdCompiler *comp)
{
	if (comp->Name("Shape"))
	{
		comp->Value(mkNamingAdapt(wdt, "Width"));
		comp->Value(mkNamingAdapt(hgt, "Height"));
		comp->Value(mkNamingAdapt(mkSTLContainerAdapt(polys, StdCompiler::SEP_SEP2), "Shape"));
		comp->NameEnd();
	}
}

bool C4MaterialShape::DoPrepareForZoom(int32_t zoom)
{
	// calculate map pixel overlaps from polygons
	// only works if shape size is a multiple of the map zoom!
	if ((wdt % zoom) || (hgt % zoom)) return false;
	for (PolyVec::iterator i = polys.begin(); i != polys.end(); ++i)
		i->PrepareForZoom(zoom);
	// done; mark cache for zoom
	prepared_for_zoom = zoom;
	return true;
}

void C4MaterialShape::Poly::CompileFunc(StdCompiler *comp)
{
	comp->Value(mkSTLContainerAdapt(*this, StdCompiler::SEP_SEP));
}

void C4MaterialShape::Poly::PrepareForZoom(int32_t zoom)
{
	overlaps.clear();
	// center is always contained and always first in list (for IFT)
	Pt center_map(center.x/zoom, center.y/zoom);
	overlaps.push_back(center_map);
	// walk from min to max; check if center or some corner point is in poly and add if this is the case
	for (int32_t y=min.y/zoom; y<=max.y/zoom; ++y)
		for (int32_t x=min.x/zoom; x<=max.x/zoom; ++x)
			if (x != center_map.x || y != center_map.y)
				for (int32_t ty=0; ty<=zoom; ty += 3)
					for (int32_t tx=0; tx<=zoom; tx += 3)
						if (IsPtContained(x*zoom+tx,y*zoom+ty))
						{
							overlaps.push_back(Pt(x,y));
							tx=zoom+1; break;
						}
}

bool C4MaterialShape::Poly::IsPtContained(int32_t x, int32_t y) const
{
	// point is contained if it crosses an off number of borders
	int crossings = 0;
	for (size_t i=0; i<size(); ++i)
	{
		Pt pt1 = (*this)[i];
		Pt pt2 = (*this)[(i+1)%size()];
		if ((pt1.y<y) != (pt2.y<y)) // crossing vertically?
		{
			// does line pt1-pt2 intersecti line (x,y)-(inf,y)?
			crossings += ((pt1.x-(pt1.y-y)*(pt2.x-pt1.x)/(pt2.y-pt1.y))>x);
		}
	}
	return (crossings % 2)==1;
}

void C4MaterialShape::Pt::CompileFunc(StdCompiler *comp)
{
	comp->Value(x);
	comp->Separator();
	comp->Value(y);
}


// -------------------------------------- C4MaterialCore

C4MaterialCore::C4MaterialCore()
{
	Clear();
}

void C4MaterialCore::Clear()
{
	CustomReactionList.clear();
	sTextureOverlay.Clear();
	sPXSGfx.Clear();
	sBlastShiftTo.Clear();
	sInMatConvert.Clear();
	sInMatConvertTo.Clear();
	sBelowTempConvertTo.Clear();
	sAboveTempConvertTo.Clear();
	*Name='\0';
	MapChunkType = C4M_Flat;
	ShapeTexture.Clear();
	Density = 0;
	Friction = 0;
	DigFree = 0;
	BlastFree = 0;
	Dig2Object = C4ID::None;
	Dig2ObjectRatio = 0;
	Dig2ObjectCollect = 0;
	Blast2Object = C4ID::None;
	Blast2ObjectRatio = 0;
	Blast2PXSRatio = 0;
	Instable = 0;
	MaxAirSpeed = 0;
	MaxSlide = 0;
	WindDrift = 0;
	Inflammable = 0;
	Incindiary = 0;
	Extinguisher = 0;
	Corrosive = 0;
	Corrode = 0;
	Soil = 0;
	Placement = 0;
	OverlayType = 0;
	PXSGfxRt.Default();
	PXSGfxSize = 0;
	InMatConvertDepth = 0;
	BelowTempConvert = 0;
	BelowTempConvertDir = 0;
	AboveTempConvert = 0;
	AboveTempConvertDir = 0;
	ColorAnimation = 0;
	TempConvStrength = 0;
	MinHeightCount = 0;
	SplashRate=10;
}

void C4MaterialCore::Default()
{
	Clear();
}

bool C4MaterialCore::Load(C4Group &hGroup,
                          const char *szEntryName)
{
	StdStrBuf Source;
	if (!hGroup.LoadEntryString(szEntryName,&Source))
		return false;
	StdStrBuf Name = hGroup.GetFullName() + DirSep + szEntryName;
	if (!CompileFromBuf_LogWarn<StdCompilerINIRead>(*this, Source, Name.getData()))
		return false;
	// adjust placement, if not specified
	if (!Placement)
	{
		if (DensitySolid(Density))
		{
			Placement=30;
			if (!DigFree) Placement+=20;
			if (!BlastFree) Placement+=10;
		}
		else if (DensityLiquid(Density))
			Placement=10;
		else Placement=5;
	}
	return true;
}

void C4MaterialCore::CompileFunc(StdCompiler *pComp)
{
	if (pComp->isCompiler()) Clear();
	pComp->Name("Material");
	pComp->Value(mkNamingAdapt(toC4CStr(Name),      "Name",                ""));
	pComp->Value(mkNamingAdapt(ColorAnimation,      "ColorAnimation",      0));

	const StdEnumEntry<C4MaterialCoreShape> Shapes[] =
	{
		{ "Flat",     C4M_Flat },
		{ "TopFlat",  C4M_TopFlat },
		{ "Smooth",   C4M_Smooth },
		{ "Rough",    C4M_Rough },
		{ "Octagon",  C4M_Octagon },
		{ "Smoother", C4M_Smoother },
		{ NULL, C4M_Flat }
	};
	pComp->Value(mkNamingAdapt(mkEnumAdaptT<uint8_t>(MapChunkType, Shapes),
	                                                "Shape",               C4M_Flat));
	pComp->Value(mkNamingAdapt(mkParAdapt(ShapeTexture, StdCompiler::RCT_All),
	                                                "ShapeTexture",        ""));
	pComp->Value(mkNamingAdapt(Density,             "Density",             0));
	pComp->Value(mkNamingAdapt(Friction,            "Friction",            0));
	pComp->Value(mkNamingAdapt(DigFree,             "DigFree",             0));
	pComp->Value(mkNamingAdapt(BlastFree,           "BlastFree",           0));
	pComp->Value(mkNamingAdapt(Blast2Object,        "Blast2Object",        C4ID::None));
	pComp->Value(mkNamingAdapt(Dig2Object,          "Dig2Object",          C4ID::None));
	pComp->Value(mkNamingAdapt(Dig2ObjectRatio,     "Dig2ObjectRatio",     0));
	pComp->Value(mkNamingAdapt(Dig2ObjectCollect,   "Dig2ObjectCollect",   0));
	pComp->Value(mkNamingAdapt(Blast2ObjectRatio,   "Blast2ObjectRatio",   0));
	pComp->Value(mkNamingAdapt(Blast2PXSRatio,      "Blast2PXSRatio",      0));
	pComp->Value(mkNamingAdapt(Instable,            "Instable",            0));
	pComp->Value(mkNamingAdapt(MaxAirSpeed,         "MaxAirSpeed",         0));
	pComp->Value(mkNamingAdapt(MaxSlide,            "MaxSlide",            0));
	pComp->Value(mkNamingAdapt(WindDrift,           "WindDrift",           0));
	pComp->Value(mkNamingAdapt(Inflammable,         "Inflammable",         0));
	pComp->Value(mkNamingAdapt(Incindiary,          "Incindiary",          0));
	pComp->Value(mkNamingAdapt(Corrode,             "Corrode",             0));
	pComp->Value(mkNamingAdapt(Corrosive,           "Corrosive",           0));
	pComp->Value(mkNamingAdapt(Extinguisher,        "Extinguisher",        0));
	pComp->Value(mkNamingAdapt(Soil,                "Soil",                0));
	pComp->Value(mkNamingAdapt(Placement,           "Placement",           0));
	pComp->Value(mkNamingAdapt(mkParAdapt(sTextureOverlay, StdCompiler::RCT_IdtfAllowEmpty),
	                                                "TextureOverlay",      ""));
	pComp->Value(mkNamingAdapt(OverlayType,         "OverlayType",         0));
	pComp->Value(mkNamingAdapt(mkParAdapt(sPXSGfx, StdCompiler::RCT_IdtfAllowEmpty),
	                                                "PXSGfx",              ""));
	pComp->Value(mkNamingAdapt(PXSGfxRt,            "PXSGfxRt",            TargetRect0));
	pComp->Value(mkNamingAdapt(PXSGfxSize,          "PXSGfxSize",          PXSGfxRt.Wdt));
	pComp->Value(mkNamingAdapt(TempConvStrength,    "TempConvStrength",    0));
	pComp->Value(mkNamingAdapt(mkParAdapt(sBlastShiftTo, StdCompiler::RCT_IdtfAllowEmpty),
	                                                "BlastShiftTo",        ""));
	pComp->Value(mkNamingAdapt(mkParAdapt(sInMatConvert, StdCompiler::RCT_IdtfAllowEmpty),
	                                                "InMatConvert",        ""));
	pComp->Value(mkNamingAdapt(mkParAdapt(sInMatConvertTo, StdCompiler::RCT_IdtfAllowEmpty),
	                                                "InMatConvertTo",      ""));
	pComp->Value(mkNamingAdapt(InMatConvertDepth,   "InMatConvertDepth",   0));
	pComp->Value(mkNamingAdapt(AboveTempConvert,    "AboveTempConvert",    0));
	pComp->Value(mkNamingAdapt(AboveTempConvertDir, "AboveTempConvertDir", 0));
	pComp->Value(mkNamingAdapt(mkParAdapt(sAboveTempConvertTo, StdCompiler::RCT_IdtfAllowEmpty),
	                                                "AboveTempConvertTo",  ""));
	pComp->Value(mkNamingAdapt(BelowTempConvert,    "BelowTempConvert",    0));
	pComp->Value(mkNamingAdapt(BelowTempConvertDir, "BelowTempConvertDir", 0));
	pComp->Value(mkNamingAdapt(mkParAdapt(sBelowTempConvertTo, StdCompiler::RCT_IdtfAllowEmpty),
	                                                "BelowTempConvertTo",  ""));
	pComp->Value(mkNamingAdapt(MinHeightCount,      "MinHeightCount",      0));
	pComp->Value(mkNamingAdapt(SplashRate,          "SplashRate",          10));
	pComp->NameEnd();
	// material reactions
	pComp->Value(mkNamingAdapt(mkSTLContainerAdapt(CustomReactionList),
	                                                "Reaction",            std::vector<C4MaterialReaction>()));
}


// -------------------------------------- C4Material

C4Material::C4Material()
{
	BlastShiftTo=0;
	InMatConvertTo=MNone;
	BelowTempConvertTo=0;
	AboveTempConvertTo=0;
	CustomShape = NULL;
}

void C4Material::UpdateScriptPointers()
{
	for (uint32_t i = 0; i < CustomReactionList.size(); ++i)
		CustomReactionList[i].ResolveScriptFuncs(Name);
}


// -------------------------------------- C4MaterialMap


C4MaterialMap::C4MaterialMap() : DefReactConvert(&mrfConvert), DefReactPoof(&mrfPoof), DefReactCorrode(&mrfCorrode), DefReactIncinerate(&mrfIncinerate), DefReactInsert(&mrfInsert)
{
	Default();
}


C4MaterialMap::~C4MaterialMap()
{
	Clear();
}

void C4MaterialMap::Clear()
{
	if (Map) delete [] Map; Map=NULL; Num=0;
	delete [] ppReactionMap; ppReactionMap = NULL;
	Shapes.clear();
}

int32_t C4MaterialMap::Load(C4Group &hGroup)
{
	char entryname[256+1];

	// Determine number of materials in files
	int32_t mat_num=hGroup.EntryCount(C4CFN_MaterialFiles);

	// Allocate new map
	C4Material *pNewMap = new C4Material [mat_num + Num];
	if (!pNewMap) return 0;

	// Load material cores to map
	hGroup.ResetSearch(); int32_t cnt=0;
	while (hGroup.FindNextEntry(C4CFN_MaterialFiles,entryname))
	{
		// Load mat
		if (!pNewMap[cnt].Load(hGroup,entryname))
			{ delete [] pNewMap; return 0; }
		// A new material?
		if (Get(pNewMap[cnt].Name) == MNone)
			cnt++;
	}

	// Take over old materials.
	for (int32_t i = 0; i < Num; i++)
	{
		pNewMap[cnt+i] = Map[i];
	}
	delete [] Map;
	Map = pNewMap;

	// set material number
	Num+=cnt;

	// Load material shapes
	hGroup.ResetSearch();
	while (hGroup.FindNextEntry(C4CFN_MaterialShapeFiles,entryname))
	{
		C4MaterialShape shape;
		if (shape.Load(hGroup, entryname))
		{
			Shapes[StdCopyStrBuf(entryname)] = shape;
		}
		else
		{
			DebugLogF("Error loading material shape %s from %s.", entryname, hGroup.GetFullName().getData());
		}
	}

	return cnt;
}

bool C4MaterialMap::HasMaterials(C4Group &hGroup) const
{
	return !!hGroup.EntryCount(C4CFN_MaterialFiles);
}

int32_t C4MaterialMap::Get(const char *szMaterial)
{
	int32_t cnt;
	for (cnt=0; cnt<Num; cnt++)
		if (SEqualNoCase(szMaterial,Map[cnt].Name))
			return cnt;
	return MNone;
}


bool C4MaterialMap::CrossMapMaterials() // Called after load
{
	// Check material number
	if (::MaterialMap.Num>C4MaxMaterial)
		{ LogFatal(LoadResStr("IDS_PRC_TOOMANYMATS")); return false; }
	// build reaction function map
	delete [] ppReactionMap;
	typedef C4MaterialReaction * C4MaterialReactionPtr;
	ppReactionMap = new C4MaterialReactionPtr[(Num+1)*(Num+1)];
	for (int32_t iMatPXS=-1; iMatPXS<Num; iMatPXS++)
	{
		C4Material *pMatPXS = (iMatPXS+1) ? Map+iMatPXS : NULL;
		for (int32_t iMatLS=-1; iMatLS<Num; iMatLS++)
		{
			C4MaterialReaction *pReaction = NULL;
			C4Material *pMatLS  = ( iMatLS+1) ? Map+ iMatLS : NULL;
			// natural stuff: material conversion here?
			if (pMatPXS && pMatPXS->sInMatConvert.getLength() && SEqualNoCase(pMatPXS->sInMatConvert.getData(), pMatLS ? pMatLS->Name : C4TLS_MatSky))
				pReaction = &DefReactConvert;
			// the rest is happening for same/higher densities only
			else if ((MatDensity(iMatPXS) <= MatDensity(iMatLS)) && pMatPXS && pMatLS)
			{
				// incindiary vs extinguisher
				if ((pMatPXS->Incindiary && pMatLS->Extinguisher) || (pMatPXS->Extinguisher && pMatLS->Incindiary))
					pReaction = &DefReactPoof;
				// incindiary vs inflammable
				else if ((pMatPXS->Incindiary && pMatLS->Inflammable) || (pMatPXS->Inflammable && pMatLS->Incindiary))
					pReaction = &DefReactIncinerate;
				// corrosive vs corrode
				else if (pMatPXS->Corrosive && pMatLS->Corrode)
					pReaction = &DefReactCorrode;
				// otherwise, when hitting same or higher density: Material insertion
				else
					pReaction = &DefReactInsert;
			}
			// assign the function; or NULL for no reaction
			SetMatReaction(iMatPXS, iMatLS, pReaction);
		}
	}
	// reset max shape size
	max_shape_width=max_shape_height=0;
	// material-specific initialization
	int32_t cnt;
	for (cnt=0; cnt<Num; cnt++)
	{
		C4Material *pMat = Map+cnt;
		const char *szTextureOverlay = NULL;
		// newgfx: init pattern
		if (Map[cnt].sTextureOverlay.getLength())
			if (::TextureMap.GetTexture(Map[cnt].sTextureOverlay.getLength()))
			{
				szTextureOverlay = Map[cnt].sTextureOverlay.getData();
				// backwards compatibility: if a pattern was specified although the no-pattern flag was set, overwrite that flag
				if (Map[cnt].OverlayType & C4MatOv_None)
				{
					DebugLogF("Error in overlay of material %s: Flag C4MatOv_None ignored because a custom overlay (%s) was specified!", Map[cnt].Name, szTextureOverlay);
					Map[cnt].OverlayType &= ~C4MatOv_None;
				}
			}
		// default to first texture in texture map
		int iTexMapIx;
		if (!szTextureOverlay && (iTexMapIx = ::TextureMap.GetIndex(Map[cnt].Name, NULL, false)))
			szTextureOverlay = TextureMap.GetEntry(iTexMapIx)->GetTextureName();
		// default to smooth
		if (!szTextureOverlay)
			szTextureOverlay = "none";
		// search/create entry in texmap
		Map[cnt].DefaultMatTex = ::TextureMap.GetIndex(Map[cnt].Name, szTextureOverlay, true,
		                         FormatString("DefaultMatTex of mat %s", Map[cnt].Name).getData());
		// init PXS facet
		C4Surface * sfcTexture;
		C4Texture * Texture;
		if (Map[cnt].sPXSGfx.getLength())
			if ((Texture=::TextureMap.GetTexture(Map[cnt].sPXSGfx.getData())))
				if ((sfcTexture=Texture->Surface32))
					Map[cnt].PXSFace.Set(sfcTexture, Map[cnt].PXSGfxRt.x, Map[cnt].PXSGfxRt.y, Map[cnt].PXSGfxRt.Wdt, Map[cnt].PXSGfxRt.Hgt);
		// init shape
		if (Map[cnt].ShapeTexture.getLength())
		{
			C4MaterialShape *shape = GetShapeByName(Map[cnt].ShapeTexture.getData());
			Map[cnt].CustomShape = shape;
			if (!shape)
			{
				DebugLogF("Custom shape texture (%s) for material %s not found!", Map[cnt].ShapeTexture.getData(), Map[cnt].Name);
			}
			else
			{
				// adjust max shape overlap
				max_shape_width  = Max(max_shape_width , shape->max_poly_width);
				max_shape_height = Max(max_shape_height, shape->max_poly_height);
			}
		}
		else
			Map[cnt].CustomShape = NULL;
		// evaluate reactions for that material
		for (unsigned int iRCnt = 0; iRCnt < pMat->CustomReactionList.size(); ++iRCnt)
		{
			C4MaterialReaction *pReact = &(pMat->CustomReactionList[iRCnt]);
			if (pReact->sConvertMat.getLength()) pReact->iConvertMat = Get(pReact->sConvertMat.getData()); else pReact->iConvertMat = -1;
			// evaluate target spec
			int32_t tmat;
			if (MatValid(tmat=Get(pReact->TargetSpec.getData())))
			{
				// single material target
				if (pReact->fInverseSpec)
					for (int32_t cnt2=-1; cnt2<Num; cnt2++) if (cnt2!=tmat) SetMatReaction(cnt, cnt2, pReact);
						else
							SetMatReaction(cnt, tmat, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "All"))
			{
				// add to all materials, including sky
				if (!pReact->fInverseSpec) for (int32_t cnt2=-1; cnt2<Num; cnt2++) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "Solid"))
			{
				// add to all solid materials
				if (pReact->fInverseSpec) SetMatReaction(cnt, -1, pReact);
				for (int32_t cnt2=0; cnt2<Num; cnt2++) if (DensitySolid(Map[cnt2].Density) != pReact->fInverseSpec) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "SemiSolid"))
			{
				// add to all semisolid materials
				if (pReact->fInverseSpec) SetMatReaction(cnt, -1, pReact);
				for (int32_t cnt2=0; cnt2<Num; cnt2++) if (DensitySemiSolid(Map[cnt2].Density) != pReact->fInverseSpec) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "Background"))
			{
				// add to all BG materials, including sky
				if (!pReact->fInverseSpec) SetMatReaction(cnt, -1, pReact);
				for (int32_t cnt2=0; cnt2<Num; cnt2++) if (!Map[cnt2].Density != pReact->fInverseSpec) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "Sky"))
			{
				// add to sky
				if (!pReact->fInverseSpec)
					SetMatReaction(cnt, -1, pReact);
				else
					for (int32_t cnt2=0; cnt2<Num; cnt2++) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "Incindiary"))
			{
				// add to all incendiary materials
				if (pReact->fInverseSpec) SetMatReaction(cnt, -1, pReact);
				for (int32_t cnt2=0; cnt2<Num; cnt2++) if (!Map[cnt2].Incindiary == pReact->fInverseSpec) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "Extinguisher"))
			{
				// add to all incendiary materials
				if (pReact->fInverseSpec) SetMatReaction(cnt, -1, pReact);
				for (int32_t cnt2=0; cnt2<Num; cnt2++) if (!Map[cnt2].Extinguisher == pReact->fInverseSpec) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "Inflammable"))
			{
				// add to all incendiary materials
				if (pReact->fInverseSpec) SetMatReaction(cnt, -1, pReact);
				for (int32_t cnt2=0; cnt2<Num; cnt2++) if (!Map[cnt2].Inflammable == pReact->fInverseSpec) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "Corrosive"))
			{
				// add to all incendiary materials
				if (pReact->fInverseSpec) SetMatReaction(cnt, -1, pReact);
				for (int32_t cnt2=0; cnt2<Num; cnt2++) if (!Map[cnt2].Corrosive == pReact->fInverseSpec) SetMatReaction(cnt, cnt2, pReact);
			}
			else if (SEqualNoCase(pReact->TargetSpec.getData(), "Corrode"))
			{
				// add to all incendiary materials
				if (pReact->fInverseSpec) SetMatReaction(cnt, -1, pReact);
				for (int32_t cnt2=0; cnt2<Num; cnt2++) if (!Map[cnt2].Corrode == pReact->fInverseSpec) SetMatReaction(cnt, cnt2, pReact);
			}
		}
	}
	// second loop (DefaultMatTex is needed by GetIndexMatTex)
	for (cnt=0; cnt<Num; cnt++)
	{
		if (Map[cnt].sBlastShiftTo.getLength())
			Map[cnt].BlastShiftTo=::TextureMap.GetIndexMatTex(Map[cnt].sBlastShiftTo.getData(), NULL, true, FormatString("BlastShiftTo of mat %s", Map[cnt].Name).getData());
		if (Map[cnt].sInMatConvertTo.getLength())
			Map[cnt].InMatConvertTo=Get(Map[cnt].sInMatConvertTo.getData());
		if (Map[cnt].sBelowTempConvertTo.getLength())
			Map[cnt].BelowTempConvertTo=::TextureMap.GetIndexMatTex(Map[cnt].sBelowTempConvertTo.getData(), NULL, true, FormatString("BelowTempConvertTo of mat %s", Map[cnt].Name).getData());
		if (Map[cnt].sAboveTempConvertTo.getLength())
			Map[cnt].AboveTempConvertTo=::TextureMap.GetIndexMatTex(Map[cnt].sAboveTempConvertTo.getData(), NULL, true, FormatString("AboveTempConvertTo of mat %s", Map[cnt].Name).getData());
	}
#if 0
	int32_t i=0;
	while (ReactionFuncMap[i].szRFName) {printf("%s: %p\n", ReactionFuncMap[i].szRFName, ReactionFuncMap[i].pFunc); ++i;}
	for (int32_t cnt=-1; cnt<Num; cnt++)
		for (int32_t cnt2=-1; cnt2<Num; cnt2++)
			if (ppReactionMap[(cnt2+1)*(Num+1) + cnt+1])
				printf("%s -> %s: %p\n", Map[cnt].Name, Map[cnt2].Name, ppReactionMap[(cnt2+1)*(Num+1) + cnt+1]->pFunc);
#endif
	// Get hardcoded system material indices
	const C4TexMapEntry* earth_entry = ::TextureMap.GetEntry(::TextureMap.GetIndexMatTex(Game.C4S.Landscape.Material));
	if(!earth_entry)
		{ LogFatal(FormatString("Earth material \"%s\" not found!", Game.C4S.Landscape.Material).getData()); return false; }

	MVehic   = Get("Vehicle"); MCVehic = Mat2PixColDefault(MVehic);
	MTunnel  = Get("Tunnel");
	MWater   = Get("Water");
	MEarth   = Get(earth_entry->GetMaterialName());

	if ((MVehic==MNone) || (MTunnel==MNone))
		{ LogFatal(LoadResStr("IDS_PRC_NOSYSMATS")); return false; }
	return true;
}


void C4MaterialMap::SetMatReaction(int32_t iPXSMat, int32_t iLSMat, C4MaterialReaction *pReact)
{
	// evaluate reaction swap
	if (pReact && pReact->fReverse) Swap(iPXSMat, iLSMat);
	// set it
	ppReactionMap[(iLSMat+1)*(Num+1) + iPXSMat+1] = pReact;
}

bool C4MaterialMap::SaveEnumeration(C4Group &hGroup)
{
	char *mapbuf = new char [1000];
	mapbuf[0]=0;
	SAppend("[Enumeration]",mapbuf); SAppend(LineFeed,mapbuf);
	for (int32_t cnt=0; cnt<Num; cnt++)
	{
		SAppend(Map[cnt].Name,mapbuf);
		SAppend(LineFeed,mapbuf);
	}
	return hGroup.Add(C4CFN_MatMap,mapbuf,SLen(mapbuf),false,true);
}

bool C4MaterialMap::LoadEnumeration(C4Group &hGroup)
{
	// Load enumeration map (from savegame), succeed if not present
	StdStrBuf mapbuf;
	if (!hGroup.LoadEntryString(C4CFN_MatMap, &mapbuf)) return true;

	// Sort material array by enumeration map, fail if some missing
	const char *csearch;
	char cmatname[C4M_MaxName+1];
	int32_t cmat=0;
	if (!(csearch = SSearch(mapbuf.getData(),"[Enumeration]"))) { return false; }
	csearch=SAdvanceSpace(csearch);
	while (IsIdentifier(*csearch))
	{
		SCopyIdentifier(csearch,cmatname,C4M_MaxName);
		if (!SortEnumeration(cmat,cmatname))
		{
			// Output error message!
			return false;
		}
		cmat++;
		csearch+=SLen(cmatname);
		csearch=SAdvanceSpace(csearch);
	}

	return true;
}

bool C4MaterialMap::SortEnumeration(int32_t iMat, const char *szMatName)
{

	// Not enough materials loaded
	if (iMat>=Num) return false;

	// Find requested mat
	int32_t cmat;
	for (cmat=iMat; cmat<Num; cmat++)
		if (SEqual(szMatName,Map[cmat].Name))
			break;
	// Not found
	if (cmat>=Num) return false;

	// already the same?
	if (cmat == iMat) return true;

	// Move requested mat to indexed position
	C4Material mswap;
	mswap = Map[iMat];
	Map[iMat] = Map[cmat];
	Map[cmat] = mswap;

	return true;
}

void C4MaterialMap::Default()
{
	Num=0;
	Map=NULL;
	ppReactionMap=NULL;
	max_shape_width=max_shape_height=0;
}

C4MaterialReaction *C4MaterialMap::GetReaction(int32_t iPXSMat, int32_t iLandscapeMat)
{
	// safety
	if (!ppReactionMap) return NULL;
	if (!Inside<int32_t>(iPXSMat, -1, Num-1)) return NULL;
	if (!Inside<int32_t>(iLandscapeMat, -1, Num-1)) return NULL;
	// values OK; get func!
	return GetReactionUnsafe(iPXSMat, iLandscapeMat);
}


bool mrfInsertCheck(int32_t &iX, int32_t &iY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, bool *pfPosChanged)
{
	// always manipulating pos/speed here
	if (pfPosChanged) *pfPosChanged = true;

	// Rough contact? May splash
	if (fYDir > itofix(1))
		if (::MaterialMap.Map[iPxsMat].SplashRate && !Random(::MaterialMap.Map[iPxsMat].SplashRate))
		{
			fYDir = -fYDir/8;
			fXDir = fXDir/8 + C4REAL100(Random(200) - 100);
			if (fYDir) return false;
		}

	// Contact: Stop
	fYDir = 0;

	// Incindiary mats smoke on contact even before doing their slide
	if (::MaterialMap.Map[iPxsMat].Incindiary)
		if (!Random(25))
		{
			Smoke(iX, iY, 4 + Random(3));
		}

	// Move by mat path/slide
	int32_t iSlideX = iX, iSlideY = iY;
	if (::Landscape.FindMatSlide(iSlideX,iSlideY,Sign(GravAccel),::MaterialMap.Map[iPxsMat].Density,::MaterialMap.Map[iPxsMat].MaxSlide))
	{
		if (iPxsMat == iLsMat)
			{ iX = iSlideX; iY = iSlideY; fXDir = 0; return false; }
		// Accelerate into the direction
		fXDir = (fXDir * 10 + Sign(iSlideX - iX)) / 11 + C4REAL10(Random(5)-2);
		// Slide target in range? Move there directly.
		if (Abs(iX - iSlideX) <= Abs(fixtoi(fXDir)))
		{
			iX = iSlideX;
			iY = iSlideY;
			if (fYDir <= 0) fXDir = 0;
		}
		// Continue existance
		return false;
	}
	// insertion OK
	return true;
}

bool mrfUserCheck(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged)
{
	// check execution mask
	if ((1<<evEvent) & ~pReaction->iExecMask) return false;
	// do splash/slide check, if desired
	if (pReaction->fInsertionCheck && evEvent == meePXSMove)
		if (!mrfInsertCheck(iX, iY, fXDir, fYDir, iPxsMat, iLsMat, pfPosChanged))
			return false;
	// checks OK; reaction may be applied
	return true;
}

bool C4MaterialMap::mrfConvert(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged)
{
	if (pReaction->fUserDefined) if (!mrfUserCheck(pReaction, iX, iY, iLSPosX, iLSPosY, fXDir, fYDir, iPxsMat, iLsMat, evEvent, pfPosChanged)) return false;
	switch (evEvent)
	{
	case meePXSMove: // PXS movement
		// for hardcoded stuff: only InMatConvert is Snow in Water, which does not have any collision proc
		if (!pReaction->fUserDefined) break;
		// user-defined conversions may also convert upon hitting materials

	case meePXSPos: // PXS check before movement
	{
		// Check depth
		int32_t iDepth = pReaction->fUserDefined ? pReaction->iDepth : ::MaterialMap.Map[iPxsMat].InMatConvertDepth;
		if (!iDepth || GBackMat(iX, iY - iDepth) == iLsMat)
		{
			// Convert
			iPxsMat = pReaction->fUserDefined ? pReaction->iConvertMat : ::MaterialMap.Map[iPxsMat].InMatConvertTo;
			if (!MatValid(iPxsMat))
				// Convert failure (target mat not be loaded, or target may be C4TLS_MatSky): Kill Pix
				return true;
			// stop movement after conversion
			fXDir = fYDir = 0;
			if (pfPosChanged) *pfPosChanged = true;
		}
	}
	break;

	case meeMassMove: // MassMover-movement
		// Conversion-transfer to PXS
		::PXS.Create(iPxsMat,itofix(iX),itofix(iY));
		return true;
	}
	// not handled
	return false;
}

bool C4MaterialMap::mrfPoof(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged)
{
	if (pReaction->fUserDefined) if (!mrfUserCheck(pReaction, iX, iY, iLSPosX, iLSPosY, fXDir, fYDir, iPxsMat, iLsMat, evEvent, pfPosChanged)) return false;
	switch (evEvent)
	{
	case meeMassMove: // MassMover-movement
	case meePXSPos: // PXS check before movement: Kill both landscape and PXS mat
		::Landscape.ExtractMaterial(iLSPosX,iLSPosY);
		if (!Random(3)) Smoke(iX,iY,3);
		if (!Random(3)) StartSoundEffectAt("Pshshsh", iX, iY);
		return true;

	case meePXSMove: // PXS movement
		// incindiary/extinguisher/corrosives are always same density proc; so do insertion check first
		if (!pReaction->fUserDefined)
			if (!mrfInsertCheck(iX, iY, fXDir, fYDir, iPxsMat, iLsMat, pfPosChanged))
				// either splash or slide prevented interaction
				return false;
		// Always kill both landscape and PXS mat
		::Landscape.ExtractMaterial(iLSPosX,iLSPosY);
		if (!Random(3)) Smoke(iX,iY,3);
		if (!Random(3)) StartSoundEffectAt("Pshshsh", iX, iY);
		return true;
	}
	// not handled
	return false;
}

bool C4MaterialMap::mrfCorrode(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged)
{
	if (pReaction->fUserDefined) if (!mrfUserCheck(pReaction, iX, iY, iLSPosX, iLSPosY, fXDir, fYDir, iPxsMat, iLsMat, evEvent, pfPosChanged)) return false;
	switch (evEvent)
	{
	case meePXSPos: // PXS check before movement
		// No corrosion - it would make acid incredibly effective
		break;
	case meeMassMove: // MassMover-movement
	{
		// evaluate corrosion percentage
		bool fDoCorrode; int d100 = Random(100);
		if (pReaction->fUserDefined)
			fDoCorrode = (d100 < pReaction->iCorrosionRate);
		else
			fDoCorrode = (d100 < ::MaterialMap.Map[iPxsMat].Corrosive) && (d100 < ::MaterialMap.Map[iLsMat].Corrode);
		if (fDoCorrode)
		{
			ClearBackPix(iLSPosX,iLSPosY);
			//::Landscape.CheckInstabilityRange(iLSPosX,iLSPosY); - more correct, but makes acid too effective as well
			if (!Random(5))
			{
				Smoke(iX, iY, 3 + Random(3));
			}
			if (!Random(20)) StartSoundEffectAt("Corrode", iX, iY);
			return true;
		}
	}
	break;

	case meePXSMove: // PXS movement
	{
		// corrodes to corrosives are always same density proc; so do insertion check first
		if (!pReaction->fUserDefined)
			if (!mrfInsertCheck(iX, iY, fXDir, fYDir, iPxsMat, iLsMat, pfPosChanged))
				// either splash or slide prevented interaction
				return false;
		// evaluate corrosion percentage
		bool fDoCorrode; int d100 = Random(100);
		if (pReaction->fUserDefined)
			fDoCorrode = (d100 < pReaction->iCorrosionRate);
		else
			fDoCorrode = (d100 < ::MaterialMap.Map[iPxsMat].Corrosive) && (d100 < ::MaterialMap.Map[iLsMat].Corrode);
		if (fDoCorrode)
		{
			ClearBackPix(iLSPosX,iLSPosY);
			::Landscape.CheckInstabilityRange(iLSPosX,iLSPosY);
			if (!Random(5))
			{
				Smoke(iX,iY,3+Random(3));
			}
			if (!Random(20)) StartSoundEffectAt("Corrode", iX, iY);
			return true;
		}
		// Else: dead. Insert material here
		::Landscape.InsertMaterial(iPxsMat,iX,iY);
		return true;
	}
	}
	// not handled
	return false;
}

bool C4MaterialMap::mrfIncinerate(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged)
{
	// not available as user reaction
	assert(!pReaction->fUserDefined);
	switch (evEvent)
	{
	case meeMassMove: // MassMover-movement
	case meePXSPos: // PXS check before movement
		if (::Landscape.Incinerate(iX, iY)) return true;
		break;

	case meePXSMove: // PXS movement
		// incinerate to inflammables are always same density proc; so do insertion check first
		if (!mrfInsertCheck(iX, iY, fXDir, fYDir, iPxsMat, iLsMat, pfPosChanged))
			// either splash or slide prevented interaction
			return false;
		// evaluate inflammation (should always succeed)
		if (::Landscape.Incinerate(iX, iY)) return true;
		// Else: dead. Insert material here
		::Landscape.InsertMaterial(iPxsMat,iX,iY);
		return true;
	}
	// not handled
	return false;
}

bool C4MaterialMap::mrfInsert(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged)
{
	if (pReaction->fUserDefined) if (!mrfUserCheck(pReaction, iX, iY, iLSPosX, iLSPosY, fXDir, fYDir, iPxsMat, iLsMat, evEvent, pfPosChanged)) return false;
	switch (evEvent)
	{
	case meePXSPos: // PXS check before movement
		break;

	case meePXSMove: // PXS movement
	{
		// check for bounce/slide
		if (!pReaction->fUserDefined)
			if (!mrfInsertCheck(iX, iY, fXDir, fYDir, iPxsMat, iLsMat, pfPosChanged))
				// continue existing
				return false;
		// Else: dead. Insert material here
		::Landscape.InsertMaterial(iPxsMat,iX,iY);
		return true;
	}

	case meeMassMove: // MassMover-movement
		break;
	}
	// not handled
	return false;
}

bool C4MaterialMap::mrfScript(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged)
{
	// do generic checks for user-defined reactions
	if (!mrfUserCheck(pReaction, iX, iY, iLSPosX, iLSPosY, fXDir, fYDir, iPxsMat, iLsMat, evEvent, pfPosChanged))
		return false;

	// check script func
	if (!pReaction->pScriptFunc) return false;
	// OK - let's call it!
	//                      0           1           2                3                        4                           5                      6               7              8
	int32_t iXDir1, iYDir1, iXDir2, iYDir2;
	C4AulParSet pars(C4VInt(iX), C4VInt(iY), C4VInt(iLSPosX), C4VInt(iLSPosY), C4VInt(iXDir1=fixtoi(fXDir, 100)), C4VInt(iYDir1=fixtoi(fYDir, 100)), C4VInt(iPxsMat), C4VInt(iLsMat), C4VInt(evEvent));
	if (!!pReaction->pScriptFunc->Exec(NULL, &pars, false))
	{
		// PXS shall be killed!
		return true;
	}
	// PXS shall exist further: write back parameters
	iPxsMat = pars[6].getInt();
	int32_t iX2 = pars[0].getInt(), iY2 = pars[1].getInt();
	iXDir2 = pars[4].getInt(); iYDir2 = pars[5].getInt();
	if (iX!=iX2 || iY!=iY2 || iXDir1!=iXDir2 || iYDir1!=iYDir2)
	{
		// changes to pos/speed detected
		if (pfPosChanged) *pfPosChanged = true;
		iX=iX2; iY=iY2;
		fXDir = C4REAL100(iXDir2);
		fYDir = C4REAL100(iYDir2);
	}
	// OK; done
	return false;
}

void C4MaterialMap::UpdateScriptPointers()
{
	// update in all materials
	for (int32_t i=0; i<Num; ++i) Map[i].UpdateScriptPointers();
}

C4MaterialShape *C4MaterialMap::GetShapeByName(const char *name)
{
	C4MaterialShapeMap::iterator i = Shapes.find(StdCopyStrBuf(name));
	if (i == Shapes.end()) return NULL;
	return &(i->second);
}

C4MaterialMap MaterialMap;
