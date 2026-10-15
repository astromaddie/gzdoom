/*
** i_steam.cpp
**
**---------------------------------------------------------------------------
** Copyright 2013 Braden Obrzut
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include <sys/stat.h>

#ifdef __APPLE__
#include "m_misc.h"
#endif // __APPLE__

#include "engineerrors.h"
#include "d_main.h"
#include "sc_man.h"
#include "cmdlib.h"

static void PSR_FindEndBlock(FScanner &sc)
{
	int depth = 1;
	do
	{
		if(sc.CheckToken('}'))
			--depth;
		else if(sc.CheckToken('{'))
			++depth;
		else
			sc.MustGetAnyToken();
	}
	while(depth);
}
static void PSR_SkipBlock(FScanner &sc)
{
	sc.MustGetToken('{');
	PSR_FindEndBlock(sc);
}
static bool PSR_FindAndEnterBlock(FScanner &sc, const char* keyword)
{
	// Finds a block with a given keyword and then enter it (opening brace)
	// Should be closed with PSR_FindEndBlock
	while(sc.GetToken())
	{
		if(sc.TokenType == '}')
		{
			sc.UnGet();
			return false;
		}

		sc.TokenMustBe(TK_StringConst);
		if(!sc.Compare(keyword))
		{
			if(!sc.CheckToken(TK_StringConst))
				PSR_SkipBlock(sc);
		}
		else
		{
			sc.MustGetToken('{');
			return true;
		}
	}
	return false;
}
static TArray<FString> PSR_ReadBaseInstalls(FScanner &sc)
{
	TArray<FString> result;

	// Get a list of possible install directories.
	while(sc.GetToken())
	{
		if(sc.TokenType == '}')
			break;

		sc.TokenMustBe(TK_StringConst);
		FString key(sc.String);
		if(key.Left(18).CompareNoCase("BaseInstallFolder_") == 0)
		{
			sc.MustGetToken(TK_StringConst);
			result.Push(FString(sc.String) + "/steamapps/common");
		}
		else
		{
			if(sc.CheckToken('{'))
				PSR_FindEndBlock(sc);
			else
				sc.MustGetToken(TK_StringConst);
		}
	}

	return result;
}
static TArray<FString> ParseSteamRegistry(const char* path)
{
	TArray<FString> dirs;

	// Read registry data
	FScanner sc;
	if (sc.OpenFile(path))
	{
		sc.SetCMode(true);

		// Find the SteamApps listing
		if (PSR_FindAndEnterBlock(sc, "InstallConfigStore"))
		{
			if (PSR_FindAndEnterBlock(sc, "Software"))
			{
				if (PSR_FindAndEnterBlock(sc, "Valve"))
				{
					if (PSR_FindAndEnterBlock(sc, "Steam"))
					{
						dirs = PSR_ReadBaseInstalls(sc);
					}
					PSR_FindEndBlock(sc);
				}
				PSR_FindEndBlock(sc);
			}
			PSR_FindEndBlock(sc);
		}
	}
	return dirs;
}

static struct SteamAppInfo
{
	const char* const BasePath;
	const int AppID;
} AppInfo[] =
{
	{"Doom 2/base", 2300},
	{"Final Doom/base", 2290},
	{"Heretic Shadow of the Serpent Riders/base", 2390},
	{"Hexen/base", 2360},
	{"Hexen Deathkings of the Dark Citadel/base", 2370},
	{"Ultimate Doom/base", 2280},
	{"DOOM 3 BFG Edition/base/wads", 208200},
	{"Strife", 317040},
	{"Ultimate Doom/rerelease/DOOM_Data/StreamingAssets", 2280},
#ifdef QZD_STEAM_FRAME
	// DOOM + DOOM II (2024): the KEX rerelease's IWADs, then the original ones
	{"Ultimate Doom/rerelease", 2280},
	{"Ultimate Doom/base/doom2", 2280},
	{"Ultimate Doom/base/tnt", 2280},
	{"Ultimate Doom/base/plutonia", 2280},
#endif
	{"Doom 2/rerelease/DOOM II_Data/StreamingAssets", 2300},
	{"Doom 2/finaldoombase", 2300},
    {"Master Levels of Doom/doom2", 9160}
};

TArray<FString> I_GetSteamPath()
{
	TArray<FString> result;
	TArray<FString> SteamInstallFolders;

	// Linux and OS X actually allow the user to install to any location, so
	// we need to figure out on an app-by-app basis where the game is installed.
	// To do so, we read the virtual registry.
#ifdef __APPLE__
	const FString appSupportPath = M_GetMacAppSupportPath();
	FString regPath = appSupportPath + "/Steam/config/config.vdf";
	try
	{
		SteamInstallFolders = ParseSteamRegistry(regPath.GetChars());
	}
	catch(class CRecoverableError &error)
	{
		// If we can't parse for some reason just pretend we can't find anything.
		return result;
	}

	SteamInstallFolders.Push(appSupportPath + "/Steam/steamapps/common");
#elif defined(QZD_STEAM_FRAME)
	// The Frame's Android container sees the headset's Steam library at its own
	// path, read only. Other libraries are listed in libraryfolders.vdf.
	{
		const char* steamRoot = "/home/steamos/.local/share/Steam";
		SteamInstallFolders.Push(FStringf("%s/steamapps/common", steamRoot));

		FStringf libraries("%s/steamapps/libraryfolders.vdf", steamRoot);
		if (FILE* f = fopen(libraries.GetChars(), "r"))
		{
			char line[1024];
			while (fgets(line, sizeof(line), f))
			{
				// lines like: "path"		"/run/media/steamos/card"
				char* key = strstr(line, "\"path\"");
				if (key == nullptr) continue;
				char* open = strchr(key + 6, '"');
				char* close = open ? strchr(open + 1, '"') : nullptr;
				if (open == nullptr || close == nullptr) continue;
				FString path(open + 1, close - open - 1);
				if (path.Compare(steamRoot) != 0)
					SteamInstallFolders.Push(path + "/steamapps/common");
			}
			fclose(f);
		}
	}
#else
	char* home = getenv("HOME");
	if(home != NULL && *home != '\0')
	{
		FString regPath;
		regPath.Format("%s/.steam/config/config.vdf", home);
		// [BL] The config seems to have moved from the more modern .local to
		// .steam at some point. Not sure if it's just my setup so I guess we
		// can fall back on it?
		if(!FileExists(regPath))
			regPath.Format("%s/.steam/steam/config/config.vdf", home);

		try
		{
			SteamInstallFolders = ParseSteamRegistry(regPath.GetChars());
		}
		catch(class CRecoverableError &error)
		{
			// If we can't parse for some reason just pretend we can't find anything.
			return result;
		}

		regPath.Format("%s/.steam/steam/steamapps/common", home);
		SteamInstallFolders.Push(regPath);
	}
#endif

	for(unsigned int i = 0;i < SteamInstallFolders.Size();++i)
	{
		for(unsigned int app = 0;app < countof(AppInfo);++app)
		{
			struct stat st;
			FString candidate(SteamInstallFolders[i] + "/" + AppInfo[app].BasePath);
			if(DirExists(candidate.GetChars()))
				result.Push(candidate);
		}
	}

	return result;
}

TArray<FString> I_GetGogPaths()
{
	// GOG's Doom games are Windows only at the moment
	return TArray<FString>();
}
