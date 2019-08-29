/*
  xmp-coverart
  by Bernhard Schelling

  This is free and unencumbered software released into the public domain.

  Anyone is free to copy, modify, publish, use, compile, sell, or
  distribute this software, either in source code form or as a compiled
  binary, for any purpose, commercial or non-commercial, and by any
  means.

  In jurisdictions that recognize copyright laws, the author or authors
  of this software dedicate any and all copyright interest in the
  software to the public domain. We make this dedication for the benefit
  of the public at large and to the detriment of our heirs and
  successors. We intend this dedication to be an overt act of
  relinquishment in perpetuity of all present and future rights to this
  software under copyright law.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  For more information, please refer to <http://unlicense.org/>
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <commdlg.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include "vis_1.h"
#include "xmpfunc.h"

//Google disabled (unable to get anything but thumbnail with user agent used by XMPlay_File->Open)
//#define ACD_USE_GOOGLE

//Amazon disabled (keys expired and cannot be published, api and access rules changed)
//#define ACD_USE_AMAZON

//Leave "Transfer log (debug)" option in the menu
//#define ACD_DISABLE_LOG

enum ACD_Services
{
	ACD_SERVER_SERVICE_LFMALB,
	ACD_SERVER_SERVICE_LFMART,
	ACD_SERVER_SERVICE_FLICKR,
	#ifdef ACD_USE_GOOGLE
	ACD_SERVER_SERVICE_GOOGLE,
	#endif
	ACD_SERVER_SERVICE_TOTAL,
};

static HWND g_XMPlayHWND = NULL;
static XMPFUNC_MISC   *XMPlay_Misc   = NULL;
static XMPFUNC_STATUS *XMPlay_Status = NULL;
static XMPFUNC_FILE   *XMPlay_File   = NULL;
static XMPFUNC_TEXT   *XMPlay_Text   = NULL;
static QueryInterface *XMPlay_Query  = NULL;
static char pcXMPlayDir[MAX_PATH] = "";
static char pcMyDLLDir[MAX_PATH] = "\0";

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_PNM
#define STBI_NO_PIC
#define STBI_MALLOC XMPlay_Misc->Alloc
#define STBI_REALLOC XMPlay_Misc->ReAlloc
#define STBI_FREE XMPlay_Misc->Free
#include "stb_image.h"

struct sFoundCover
{
	int iCoverRating;
	std::string strCoverTitle;
	unsigned char *pcData;
	unsigned int iDataLength;
	bool bSavedToDisk;
	sFoundCover() { pcData = NULL; iDataLength = 0; iCoverRating = 0; bSavedToDisk = false; }
	~sFoundCover() { if (pcData) XMPlay_Misc->Free(pcData); }
};

static bool NU = false;
static unsigned char cBackground[3];

static BOOL bFadeAlbumart = TRUE;
static int iAlbumArtAlign = 0;

static long colBackground = 0;
static long colInfoLine1 = RGB(145,165,170);
static long colInfoLine2 = RGB(113,133,143);
static long colInfoLine3 = RGB(100,100,130);

static HMENU hMenu = NULL, hACDMenu = NULL, hCoverMenu = NULL;

static clock_t clCycleCoverTime = 0;
static BOOL bCycleCover = FALSE;
static int iCycleCoverSeconds = 10;
static clock_t clFadeStart = clock();
static int iFadeLevel = 0;

static int iLastScaledWidth = 0, iLastScaledHeight = 0;
static clock_t clTimeLastScaledCover = 0;

static unsigned char* pScaledCoverVideo = NULL;

static int iCoverImageWidth = 0;
static int iCoverImageHeight = 0;
static unsigned char *pcCoverImage = NULL;
static sFoundCover *pActiveFoundCover = NULL;
static std::vector<sFoundCover*> vecFoundCovers;

static std::vector<sFoundCover*> ACD_FoundCovers;
static unsigned long ACD_ServiceSelected = 0;
static HANDLE ACD_hThread = NULL;
static bool ACD_ThreadSongChangedSince = false;
static WCHAR ACD_SearchPrevious[300] = L"";
static BOOL ACD_CurrentCoversSaveable = FALSE;
static char ACD_ForceReDownload = 0;
static int ACD_CoverCountByServices[ACD_SERVER_SERVICE_TOTAL];
static unsigned char ACD_ArtistAlbumTitle = 3;
static BOOL ACD_Enabled = FALSE;
static char ACD_SavePath[MAX_PATH] = "\0";
static BOOL ACD_AutoSave = FALSE;
static BOOL ACD_AutoSaveStreamArchive = FALSE;
static BOOL ACD_SaveActive = FALSE;
static char ACD_ImageSize = 0;

#ifdef ACD_USE_AMAZON
#include "hmacsha256.h"
static unsigned long ACD_ServersSelected = 8;
static char *ACD_Servers[6] = { "amazon.ca", "amazon.co.jp", "amazon.co.uk", "amazon.com", "amazon.de", "amazon.fr" };
static char pcAWSKey[] = "....................";
static char pcAWSSecret[40] = { '.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.','.' };
#endif

static char pc8CurrentFile[MAX_PATH*4] = "-";
static char pcLastTrackName[100] = "";
static clock_t clTimeLastCheckCover = 0;

extern unsigned char cNoAlbumCover[2541];
extern unsigned char cAlbumCoverDownloading[526];

#ifndef ACD_DISABLE_LOG
static BOOL ACD_LogActive = FALSE;
static std::string* ACD_pstrLog = NULL;
static clock_t ACD_clStarted = 0;
static void CoverDownload_Log(const char * format, ...)
{
	std::string &ACD_Log = *ACD_pstrLog;
	char text[2048];
	clock_t clTimeSinceStart = clock()-ACD_clStarted;
	sprintf(text, "<Log TimeSinceStart=\"%02d.%03d\">", (int)(clTimeSinceStart/CLOCKS_PER_SEC), clTimeSinceStart%CLOCKS_PER_SEC);
	ACD_Log += text;
	va_list arg;
	va_start( arg, format );
	vsprintf( text, format, arg );
	va_end(arg);
	ACD_Log += text;
	ACD_Log += "</Log>\x0A\x0D";
}
#define COVERDOWNLOAD_LOG0(f) if (ACD_LogActive) CoverDownload_Log(f);
#define COVERDOWNLOAD_LOG1(f,v1) if (ACD_LogActive) CoverDownload_Log(f,v1);
#define COVERDOWNLOAD_LOG2(f,v1,v2) if (ACD_LogActive) CoverDownload_Log(f,v1,v2);
#define COVERDOWNLOAD_DO_LOG 1
#else
#define COVERDOWNLOAD_LOG0(f)
#define COVERDOWNLOAD_LOG1(f,v1)
#define COVERDOWNLOAD_LOG2(f,v1,v2)
#endif

#pragma pack(push,1)
struct id3v2header
{
	char id[3];
	char ver[2];
	char flags;
	long size;
};

struct id3v2frame
{
	char type[4];
	long size;
	char flags[2];
};
#pragma pack(pop)

struct mp4trak 
{
	bool image;
	std::string name;
	long timescale;
	long sizeall, sizetotal;
	long numsizes, numoffsets, numdurations;
	long sizes[256];
	long offsets[256];
	long duration[256];
};

#define SWAPENDIAN(field) swapendian(&field, sizeof(field))

static void swapendian(void *data, int type_size)
{
	if (type_size == 1) return;
	unsigned char *d = (unsigned char *) data;
	for (int i=0; i<type_size/2; i++)
	{
		unsigned char a = d[i];
		d[i] = d[type_size-1-i];
		d[type_size-1-i] = a;
	}
}

static bool GetGenericSongAlbumName300(char* pcSearch, bool ACD, unsigned char aat, char *pcSplitter = " - ")
{
	char *art = XMPlay_Misc->GetTag(1), *alb = XMPlay_Misc->GetTag(2), *cue = XMPlay_Misc->GetTag(-3), *tit = XMPlay_Misc->GetTag(0), *dsp = XMPlay_Misc->GetTag(-1);
	if ((!ACD || (aat & 1)) && art) strncat(pcSearch, art, 300);
	if ((!ACD || (aat & 2)) && alb && (!pcSearch[0] || strncat(pcSearch,pcSplitter,300))) strncat(pcSearch, alb, 300);
	if (ACD &&   (aat & 4)  && tit && (!pcSearch[0] || strncat(pcSearch,pcSplitter,300))) strncat(pcSearch, tit, 300);
	if (!pcSearch[0] && cue) strncat(pcSearch, cue, 300);
	if (!ACD && !alb && tit && (!pcSearch[0] || strncat(pcSearch,pcSplitter,300))) strncat(pcSearch, tit, 300);
	if (!pcSearch[0] && dsp) strncat(pcSearch, dsp, 300);
	if (art) XMPlay_Misc->Free(art); if (alb) XMPlay_Misc->Free(alb); if (cue) XMPlay_Misc->Free(cue); if (tit) XMPlay_Misc->Free(tit); if (dsp) XMPlay_Misc->Free(dsp);
	return (pcSearch[0] != 0);
}

static bool GetGenericSongAlbumName300(WCHAR* wcSearch, bool ACD)
{
	//read tags (artist - album/song or formatted title) and covert from UTF8 to Unicode

	/*
	if (!XMPlay_Misc) return false;
	char pcSearch[300] = "";
	char *t = XMPlay_Misc->GetTag(1);
	if (t && strlen(t)) { strncat(pcSearch, t, 300); strncat(pcSearch, " - ", 300); }
	if (t) XMPlay_Misc->Free(t);
	t = XMPlay_Misc->GetTag(2);
	if (t)
	{
		strncat(pcSearch, t, 300);
		XMPlay_Misc->Free(t);
	}
	else
	{
		if ((!strlen(pcSearch)) && (t = XMPlay_Misc->GetTag(-3)))
		{
			strncat(pcSearch, t, 300); 
			XMPlay_Misc->Free(t);
		}
		else if (t = XMPlay_Misc->GetTag(0)) 
		{
			strncat(pcSearch, t, 300); 
			XMPlay_Misc->Free(t); 
		}
		else if (!strlen(pcSearch))
		{
			if (t = XMPlay_Misc->GetTag(-1))
			{
				strncat(pcSearch, t, 300);
				XMPlay_Misc->Free(t); 
			}
			else return false;
		}
	}
	MultiByteToWideChar(CP_UTF8, 0, pcSearch, -1, wcSearch, 300);
	return true;
	*/

	if (!XMPlay_Misc) return false;
	char pcSearch[300] = "";
	GetGenericSongAlbumName300(pcSearch, ACD, ACD_ArtistAlbumTitle);
	if (pcSearch[0]) MultiByteToWideChar(CP_UTF8, 0, pcSearch, -1, wcSearch, 300);
	return (pcSearch[0] != 0);
}

static void FilterFileNameCharacters(WCHAR* wcSearch)
{
	int len = wcslen(wcSearch);
	for (int i = 0; i < len; i++)
	{
		WCHAR c = wcSearch[i];
		if (c == L'\\' || c == L'/' || c == L'?' || c == L':' || c == L'*' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
		{
			wcSearch[i] = L'-';
		}
	}
}

static bool FoundCoverSetType(sFoundCover *pFoundCover)
{
	if (!pFoundCover || !pFoundCover->pcData) return false;
	stbi__context s;
	stbi__start_mem(&s, pFoundCover->pcData, pFoundCover->iDataLength);
	return (
		   stbi__jpeg_test(&s)
		|| stbi__png_test(&s)
		|| stbi__bmp_test(&s)
		|| stbi__gif_test(&s)
		|| stbi__psd_test(&s)
		|| stbi__tga_test(&s));
}

static void SetCoverAPICTypeTitle(sFoundCover* pFoundCover, long lPictureType)
{
	switch (lPictureType)
	{
		case 0x00: pFoundCover->strCoverTitle = "Other"; pFoundCover->iCoverRating = 100; break;
		case 0x01: pFoundCover->strCoverTitle = "32x32 pixels file icon"; break;
		case 0x02: pFoundCover->strCoverTitle = "Other file icon"; break;
		case 0x03: pFoundCover->strCoverTitle = "Cover (front)"; pFoundCover->iCoverRating = 111; break;
		case 0x04: pFoundCover->strCoverTitle = "Cover (back)"; pFoundCover->iCoverRating = 6; break;
		case 0x05: pFoundCover->strCoverTitle = "Leaflet page"; pFoundCover->iCoverRating = 2; break;
		case 0x06: pFoundCover->strCoverTitle = "Media (e.g. label side of CD)"; pFoundCover->iCoverRating = 3; break;
		case 0x07: pFoundCover->strCoverTitle = "Lead artist/lead performer/soloist"; break;
		case 0x08: pFoundCover->strCoverTitle = "Artist/performer"; break;
		case 0x09: pFoundCover->strCoverTitle = "Conductor"; break;
		case 0x0A: pFoundCover->strCoverTitle = "Band/Orchestra"; break;
		case 0x0B: pFoundCover->strCoverTitle = "Composer"; break;
		case 0x0C: pFoundCover->strCoverTitle = "Lyricist/text writer"; break;
		case 0x0D: pFoundCover->strCoverTitle = "Recording Location"; break;
		case 0x0E: pFoundCover->strCoverTitle = "During recording"; break;
		case 0x0F: pFoundCover->strCoverTitle = "During performance"; break;
		case 0x10: pFoundCover->strCoverTitle = "Movie/video screen capture"; break;
		case 0x11: pFoundCover->strCoverTitle = "A bright coloured fish"; pFoundCover->iCoverRating = 130; break;
		case 0x12: pFoundCover->strCoverTitle = "Illustration"; pFoundCover->iCoverRating = 103; break;
		case 0x13: pFoundCover->strCoverTitle = "Band/artist logotype"; pFoundCover->iCoverRating = 3; break;
		case 0x14: pFoundCover->strCoverTitle = "Publisher/Studio logotype"; break;
		default:   pFoundCover->strCoverTitle = "Other"; pFoundCover->iCoverRating = 100; break;
	}
}

static void GetCoverImageFromID3Tag(XMPFILE f)
{
	id3v2header header;
	if (!f) return;
	XMPlay_File->Seek(f, 0);
	XMPlay_File->Read(f, &header, sizeof(id3v2header));
	if (header.id[0] != 'I' || header.id[1] != 'D' || header.id[2] != '3') return;
	if ((header.ver[0] < 3) || (header.ver[0] > 4)) return;

	long tag_length = 0;
	for (int i = 0; i < 4; i++) { tag_length <<= 7; tag_length |= ((((char*)&header.size)[i]) & 0x7f); }
	if (tag_length == 0) return;

	id3v2frame frame;
	long readframes = 0;
	while (readframes < tag_length)
	{
		unsigned char *pcData;
		XMPlay_File->Read(f, &frame, sizeof(id3v2frame));
		SWAPENDIAN(frame.size);
		readframes += sizeof(id3v2frame);
		if (readframes + frame.size > tag_length) frame.size = tag_length-readframes;
		if (frame.size <= 0) break;
		if (*(long*)frame.type == 1128878145) //APIC
		{
			//printf("Got Header: %c%c%c%c (%d), size = %d\n", frame.type[0], frame.type[1], frame.type[2], frame.type[3], *(long*)frame.type, frame.size);
			int iDataLength = 0;
			int iRealFrameSize = frame.size;
			if (frame.flags[1] & 0x01) 
			{
				//means it has an actual data size long following the frame header
				XMPlay_File->Read(f, &iDataLength, sizeof(long));
				SWAPENDIAN(iDataLength);
				iRealFrameSize -= sizeof(long);
			}
			pcData = (unsigned char*)XMPlay_Misc->Alloc(iRealFrameSize);
			XMPlay_File->Read(f, pcData, iRealFrameSize);
			if (frame.flags[1] & 0x02) 
			{
				//means data is unsynchronisated
				unsigned char *ps = pcData+1;
				unsigned char *p = pcData+1;
				for (; p < pcData+iRealFrameSize-1; p++,ps++)
				{
					if (*(p-1) == 0xFF && *(p) == 0x0 && ((*(p+1) & 0xE0) || (*(p+1) == 0))) p++;
					*ps = *p;
				}			
				if (iDataLength == 0) iRealFrameSize -= p-ps;
			}

			char cEncoding = pcData[0];
			char pcMime[100];
			unsigned char *p = pcData+1;

			pcMime[99] = 0;
			strncpy(pcMime, (char*)p, 99);
			p+=strlen(pcMime)+1;
			char cPictureType = p[0];
			p++;
			int iDescLength = 0;
			char *pcDesc = (char*)p, *pcDescUTF8 = 0;;
			if (cEncoding == 0x00 || cEncoding == 0x03 || cEncoding == 0xFF || cEncoding == 0xFC)
			{
				//ISO-8859-1 or UTF-8
				p = (unsigned char*)strchr((char*)p, 0)+1;	
				iDescLength = (char*)p - pcDesc - 1;
			}
			else
			{
				//UTF-16 or UTF-16BE
 				while ((p = (unsigned char*)strchr((char*)p, 0)) && *++p!=0);
				if (pcDesc[0] == '\xFF' && pcDesc[1] == '\xFE') pcDesc += 2; //little endian
				if (pcDesc[0] == '\xFE' && pcDesc[1] == '\xFF') iDescLength = 0; //big endian
				else iDescLength = (char*)p - pcDesc - 1; p++; if (!*p) p++;

				if (iDescLength > 0) 
				{
					pcDescUTF8 = (char*)XMPlay_Misc->Alloc(300);
					if (WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)pcDesc, iDescLength, (char*)pcDescUTF8, 300, NULL, NULL))
						iDescLength = strlen(pcDescUTF8);
				}
			}
			
			/*if (iDataLength == 0) */iDataLength = iRealFrameSize - (p - pcData);

			if (iDataLength > 0)
			{
				sFoundCover *pFoundCover = new sFoundCover();
				pFoundCover->pcData = (unsigned char*)XMPlay_Misc->Alloc(iDataLength);
				memcpy(pFoundCover->pcData, p, iDataLength);
				pFoundCover->iDataLength = iDataLength;
				if (!FoundCoverSetType(pFoundCover)) { delete pFoundCover; }
				else
				{
					SetCoverAPICTypeTitle(pFoundCover, cPictureType);
					if (iDescLength > 0 && iDescLength < 100) { pFoundCover->strCoverTitle += " "; pFoundCover->strCoverTitle += (pcDescUTF8 ? pcDescUTF8 : pcDesc); }
					pFoundCover->strCoverTitle += " (ID3)";
					vecFoundCovers.push_back(pFoundCover);
				}
				
				/*
				int iType = -1;
				if ((stricmp(pcMime, "image/jpeg") == 0) || (stricmp(pcMime, "image/jpg") == 0)) iType = 0;
				else if (stricmp(pcMime, "image/png") == 0) iType = 1;
				if (iType != -1)
				{
					vecFoundCovers.push_back(new sFoundCover());
					vecFoundCovers.back()->cImageType = iType;
					vecFoundCovers.back()->pcData = (unsigned char*)XMPlay_Misc->Alloc(iDataLength);
					memcpy(vecFoundCovers.back()->pcData, p, iDataLength);
					vecFoundCovers.back()->iDataLength = iDataLength;
					SetCoverAPICTypeTitle(vecFoundCovers.back(), cPictureType);
					vecFoundCovers.back()->strCoverTitle += " (ID3)";
				}
				*/
			}
			if (pcDescUTF8) XMPlay_Misc->Free(pcDescUTF8);
			XMPlay_Misc->Free(pcData);
		}
		/*
		else XMPlay_File->Seek(f, XMPlay_File->Tell(f)+frame.size);
		*/
		else if (!(frame.flags[1] & 0x02) && !(header.flags & 0x80))  XMPlay_File->Seek(f, XMPlay_File->Tell(f)+frame.size);
		else
		{
			unsigned char s[3] = { 0, 0, 0 }; long left = frame.size;
			while (left-- > 0 || (s[0] == 0xFF && s[1] == 0x0))
			{
				XMPlay_File->Read(f, s+2, 1);
				if (s[0] == 0xFF && s[1] == 0x0 && ((s[2] & 0xE0) || (s[2] == 0))) { left++; frame.size++; }
				if (left < 0) XMPlay_File->Seek(f, XMPlay_File->Tell(f)-1);
				s[0] = s[1]; s[1] = s[2];
			}
		}
		readframes += frame.size;
	}
	if (readframes < tag_length) XMPlay_File->Seek(f, XMPlay_File->Tell(f)-readframes+tag_length);
}

static void GetCoverImageFromFLACTag(XMPFILE f)
{
	unsigned long l;
	if (!f) return;
	if (!XMPlay_File->Read(f, &l, 4)) return;
	//1716281667 = fLaC (stream beginning header)
	if (l != 1130450022) { XMPlay_File->Seek(f, 0); if (!XMPlay_File->Read(f, &l, 4)) return; }
	if (l != 1130450022) return;
	while (1)
	{
		if (!XMPlay_File->Read(f, &l, 4)) return;
		SWAPENDIAN(l);
		unsigned char cBlockType = l >> 24;
		long lBlockLength = l << 8 >> 8;
		if (cBlockType > 126) return;
		if (cBlockType == 6)
		{
			long lPictureType, lDescLength, iDataLength;;
			char pcMime[100], pcDescription[100];

			if (!XMPlay_File->Read(f, &lPictureType, 4)) return; SWAPENDIAN(lPictureType);
			if (!XMPlay_File->Read(f, &l, 4))            return; SWAPENDIAN(l);
			if (l >= 100 || !XMPlay_File->Read(f, pcMime, l)) return;
			pcMime[l] = 0;
			if (!XMPlay_File->Read(f, &lDescLength, 4))  return; SWAPENDIAN(lDescLength);
			if (lDescLength && lDescLength <  100 && !XMPlay_File->Read(f, pcDescription, lDescLength)) return;
			if (lDescLength && lDescLength >= 100 && !XMPlay_File->Seek(f, XMPlay_File->Tell(f)+lDescLength)) return;
			if (lDescLength <  100) pcDescription[lDescLength] = 0;
			if (!XMPlay_File->Read(f, &l, 4)) return; //Image Width
			if (!XMPlay_File->Read(f, &l, 4)) return; //Image Height
			if (!XMPlay_File->Read(f, &l, 4)) return; //Image Color Depth
			if (!XMPlay_File->Read(f, &l, 4)) return; //Image Number of Palette colors used
			if (!XMPlay_File->Read(f, &iDataLength, 4)) return;
			SWAPENDIAN(iDataLength);

			sFoundCover *pFoundCover = new sFoundCover();
			pFoundCover->pcData = (unsigned char*)XMPlay_Misc->Alloc(iDataLength);
			if (!XMPlay_File->Read(f, pFoundCover->pcData, iDataLength)) { delete pFoundCover; return; }
			else
			{
				pFoundCover->iDataLength = iDataLength;
				if (!FoundCoverSetType(pFoundCover)) { delete pFoundCover; }
				else
				{
					SetCoverAPICTypeTitle(pFoundCover, lPictureType);
					if (lDescLength && lDescLength < 100) { pFoundCover->strCoverTitle += " "; pFoundCover->strCoverTitle += pcDescription; }
					pFoundCover->strCoverTitle += " (FLAC)";
					vecFoundCovers.push_back(pFoundCover);
				}
			}

			/*
			int iType = -1;
			if ((stricmp(pcMime, "image/jpeg") == 0) || (stricmp(pcMime, "image/jpg") == 0)) iType = 0;
			else if (stricmp(pcMime, "image/png") == 0) iType = 1;
			if (iType != -1)
			{
				vecFoundCovers.push_back(new sFoundCover());
				vecFoundCovers.back()->cImageType = iType;
				vecFoundCovers.back()->pcData = (unsigned char*)XMPlay_Misc->Alloc(iDataLength);
				XMPlay_File->Read(f, vecFoundCovers.back()->pcData, iDataLength);
				vecFoundCovers.back()->iDataLength = iDataLength;
				SetCoverAPICTypeTitle(vecFoundCovers.back(), lPictureType);
				if (lDescLength && lDescLength < 100) { vecFoundCovers.back()->strCoverTitle += " "; vecFoundCovers.back()->strCoverTitle += pcDescription; }
				vecFoundCovers.back()->strCoverTitle += " (FLAC)";
			}
			else if (!XMPlay_File->Seek(f, XMPlay_File->Tell(f)+iDataLength)) return;
			*/
		}
		else if (!XMPlay_File->Seek(f, XMPlay_File->Tell(f)+lBlockLength)) return;
	}
}

static int mp4_checkmdhd(mp4trak &trak, XMPFILE f, unsigned long size) //timing sample
{
	char ver, flags[3], creationdate[16];
	if (size < 20) return size;
	size -= XMPlay_File->Read(f, &ver, 1);
	size -= XMPlay_File->Read(f, &flags, 3);
	if (ver == 0) size -= XMPlay_File->Read(f, &creationdate, 8);
	else          size -= XMPlay_File->Read(f, &creationdate, 16);
	size -= XMPlay_File->Read(f, &trak.timescale, 4); SWAPENDIAN(trak.timescale);
	//__int64 duration;
	//if (ver == 0) { size -= XMPlay_File->Read(f, &duration, 4); swapendian(&duration, 4); }
	//else          { size -= XMPlay_File->Read(f, &duration, 8); swapendian(&duration, 8); }
	//printf("--- mdhd: ver: %d - timescale: %d\n", ver, trak.timescale);
	return size;
}

static int mp4_checkstsd(mp4trak &trak, XMPFILE f, unsigned long size) //sample description
{
	long verflags, numdescs;
	if (size < 8) return size;
	size -= XMPlay_File->Read(f, &verflags, 4); SWAPENDIAN(verflags);
	size -= XMPlay_File->Read(f, &numdescs, 4); SWAPENDIAN(numdescs);
	//printf("--- STSD: verflags: %d - numdescs: %d\n", verflags, numdescs);
	while (size)
	{
		unsigned long desclen; char format[5]; format[4]=0;
		if (size < 8) break;
		size -= XMPlay_File->Read(f, &desclen, 4); SWAPENDIAN(desclen);
		size -= XMPlay_File->Read(f, &format, 4);
		//printf(" Desc %3d = %s\n", numdescs, format);
		trak.image = (!strncmp(format, "mp4v", 4) || !strncmp(format, "jpeg", 4) || !strncmp(format, "png", 3));
		if (size < desclen-8) break;
		size -= desclen-8;
		if (desclen > 51 && trak.image)
		{
			XMPlay_File->Seek(f, XMPlay_File->Tell(f)+42);
			unsigned char tlen; char title[256];
			XMPlay_File->Read(f, &tlen, 1);
			if (tlen > 254) tlen = 254;
			XMPlay_File->Read(f, title, tlen); title[tlen] = 0;
			trak.name = title;
			desclen -= 43+tlen;
			//printf(" TITLE = %s\n", title);
		}
		XMPlay_File->Seek(f, XMPlay_File->Tell(f)+desclen-8);
	}
	return size;
}

static int mp4_checkstts(mp4trak &trak, XMPFILE f, unsigned long size) //timing sample
{
	long verflags;
	if (size < 8 || !trak.image) return size;
	size -= XMPlay_File->Read(f, &verflags, 4); SWAPENDIAN(verflags);
	size -= XMPlay_File->Read(f, &trak.numdurations, 4); SWAPENDIAN(trak.numdurations);
	//printf("--- STTS: verflags: %d - numtimes: %d\n", verflags, trak.numdurations);
	for (int i = 0; size && i < trak.numdurations && i < 256; i++)
	{
		long framecount;
		size -= XMPlay_File->Read(f, &framecount, 4); SWAPENDIAN(framecount);
		size -= XMPlay_File->Read(f, &trak.duration[i], 4); SWAPENDIAN(trak.duration[i]);
		for (int x = 1; x < framecount; x++) { i++; trak.numdurations++; trak.duration[i] = trak.duration[i-x]; }
		//printf(" timing %3d = frame count: %12d - duration: %12d\n", i, framecount, trak.duration[i]);
	}
	return size;
}

static int mp4_checkstsz(mp4trak &trak, XMPFILE f, unsigned long size) //(chunk) sizes
{
	long verflags;
	if (size < 12 || !trak.image) return size;
	size -= XMPlay_File->Read(f, &verflags, 4); SWAPENDIAN(verflags);
	size -= XMPlay_File->Read(f, &trak.sizeall, 4); SWAPENDIAN(trak.sizeall);
	size -= XMPlay_File->Read(f, &trak.numsizes, 4); SWAPENDIAN(trak.numsizes);
	//printf("--- STSZ: verflags: %d - sizeall: %d - num: %d\n", verflags, trak.sizeall, trak.numsizes);
	trak.sizetotal = 0;
	for (int i = 0; size && i < trak.numsizes && i < 256; i++)
	{
		size -= XMPlay_File->Read(f, &trak.sizes[i], 4); SWAPENDIAN(trak.sizes[i]);
		//printf(" Size %3d = %12d\n", i, trak.sizes[i]);
		trak.sizetotal += trak.sizes[i];
	}
	return size;
}

static int mp4_checkstco(mp4trak &trak, XMPFILE f, unsigned long size) //chunk offset
{
	long verflags;
	if (size < 8 || !trak.image) return size;
	size -= XMPlay_File->Read(f, &verflags, 4); SWAPENDIAN(verflags);
	if (verflags) { trak.numoffsets = verflags; }
	else { size -= XMPlay_File->Read(f, &trak.numoffsets, 4); SWAPENDIAN(trak.numoffsets);}
	//printf("--- STCO: verflags: %d - num: %d\n", verflags, trak.numoffsets);
	for (int i = 0; size && i < trak.numoffsets && i < 100; i++)
	{
		size -= XMPlay_File->Read(f, &trak.offsets[i], 4); SWAPENDIAN(trak.offsets[i]);
		//printf(" Offset %3d = %12d\n", i, trak.offsets[i]);
	}
	return size;
}

static void mp4_checktrak(mp4trak &trak, XMPFILE f)
{
	if (!trak.image || !trak.numoffsets || (!trak.numsizes && !trak.sizeall)) return;
	if (trak.sizeall && !trak.numsizes) trak.sizetotal = trak.sizeall * trak.numoffsets;
	int iPosBefore = XMPlay_File->Tell(f);
	int readtotal = 0, durationtotal = 0;
	for (int i = 0; i < trak.numoffsets; i++)
	{
		int iRead = (trak.numsizes ? trak.sizes[i] : trak.sizeall);
		unsigned char *pcData = (unsigned char*)XMPlay_Misc->Alloc(trak.sizetotal);
		if (!XMPlay_File->Seek(f, trak.offsets[i]) || 
			   readtotal+iRead > trak.sizetotal ||
				 !(iRead = XMPlay_File->Read(f, pcData, iRead))) { XMPlay_Misc->Free(pcData); XMPlay_File->Seek(f, iPosBefore); return; }
		readtotal += iRead;

		sFoundCover *pFoundCover = new sFoundCover();
		pFoundCover->pcData = pcData;
		pFoundCover->iDataLength = readtotal;
		if (!FoundCoverSetType(pFoundCover)) { delete pFoundCover; }
		else
		{
			pFoundCover->strCoverTitle = trak.name;
			if (i < trak.numdurations && trak.timescale) 
			{
				char time[20];
				sprintf(time, " - Time: %02d:%02d", durationtotal/trak.timescale/60, durationtotal/trak.timescale%60);
				pFoundCover->strCoverTitle += " "; pFoundCover->strCoverTitle += time;
				durationtotal += trak.duration[i];
				//sprintf(time, " - %02d:%02d", durationtotal/trak.timescale/60, durationtotal/trak.timescale%60);
				//pFoundCover->strCoverTitle += time;
			}
			pFoundCover->strCoverTitle += " (MP4)";
			pFoundCover->iCoverRating = 100; //embedded cover images are always preferred
			vecFoundCovers.push_back(pFoundCover);
		}
	}
	XMPlay_File->Seek(f, iPosBefore);
}

static int mp4_checkmeta(mp4trak &trak, XMPFILE f, unsigned long size) //chunk offset
{
	unsigned long offset; char id[4];
	size -= XMPlay_File->Read(f, &offset, 4);
	while (size)
	{
		size -= XMPlay_File->Read(f, &offset, 4);
		SWAPENDIAN(offset);
		if (offset < 8 || offset > size+4) break;
		size -= XMPlay_File->Read(f, id, 4);
		if (*(long*)id == 1953721449/*ilst*/) continue;
		if (*(long*)id != 1920364387/*covr*/) { XMPlay_File->Seek(f, XMPlay_File->Tell(f)+offset-8); size -= offset-8; continue; }

		size -= XMPlay_File->Read(f, &offset, 4);
		SWAPENDIAN(offset);
		if (offset < 8 || offset > size+4) break;
		size -= XMPlay_File->Read(f, id, 4);
		if (*(long*)id != 1635017060/*data*/) { XMPlay_File->Seek(f, XMPlay_File->Tell(f)+offset-8); size -= offset-8; continue; }
		XMPlay_File->Seek(f, XMPlay_File->Tell(f)+8); size -= 8;
		offset -= 16;

		sFoundCover *pFoundCover = new sFoundCover();
		pFoundCover->iDataLength = offset;
		pFoundCover->pcData = (unsigned char*)XMPlay_Misc->Alloc(offset);
		size -= XMPlay_File->Read(f, pFoundCover->pcData, offset);
		if (!FoundCoverSetType(pFoundCover)) { delete pFoundCover; continue; }
		pFoundCover->strCoverTitle = "Cover (front)";
		pFoundCover->strCoverTitle += " (MP4)";
		pFoundCover->iCoverRating = 110; //embedded cover images are always preferred
		vecFoundCovers.push_back(pFoundCover);
	}
	return size;
}

static int mp4_readblock(mp4trak &trak, XMPFILE f, unsigned long size, bool firstheader, int indent = 0)
{
	if (size < 8) { XMPlay_File->Seek(f, XMPlay_File->Tell(f)+size); return 0; }

	unsigned long offset;
	char id[4];
	size -= XMPlay_File->Read(f, &offset, 4);
	size -= XMPlay_File->Read(f, id, 4);
	SWAPENDIAN(offset);

	if (firstheader && (strncmp(id, "ftyp", 4) || offset > 100)) return 0;

	if (offset < 0 || offset-8 > size || id[0] < '0' || id[0] > 'z' || id[1] < '0' || id[1] > 'z') { if (size) XMPlay_File->Seek(f, XMPlay_File->Tell(f)+size); return 0; }

	//if (indent >= 38) indent = 38; char ind[40]; memset(ind, ' ', 40); ind[39] = ind[indent] = 0;
	//printf("%s%c%c%c%c%s - Pos: %12d - Size: %12d\n", ind, id[0], id[1], id[2], id[3], &ind[indent+1], ftell(f), offset-8);

	if (!strncmp(id, "trak", 4)) { mp4_checktrak(trak, f); memset(&trak, 0, sizeof(mp4trak)); }
	if (!strncmp(id, "mdat", 4)) { mp4_checktrak(trak, f); return 0; }

	if (offset > 8)
	{
		offset -= 8;
		size -= offset;
		if (!strncmp(id, "mdhd", 4)) { offset = mp4_checkmdhd(trak, f, offset); if (offset) XMPlay_File->Seek(f, XMPlay_File->Tell(f)+offset); offset = 0; }
		if (!strncmp(id, "stts", 4)) { offset = mp4_checkstts(trak, f, offset); if (offset) XMPlay_File->Seek(f, XMPlay_File->Tell(f)+offset); offset = 0; }
		if (!strncmp(id, "stsd", 4)) { offset = mp4_checkstsd(trak, f, offset); if (offset) XMPlay_File->Seek(f, XMPlay_File->Tell(f)+offset); offset = 0; }
		if (!strncmp(id, "stsz", 4)) { offset = mp4_checkstsz(trak, f, offset); if (offset) XMPlay_File->Seek(f, XMPlay_File->Tell(f)+offset); offset = 0; }
		if (!strncmp(id, "stco", 4)) { offset = mp4_checkstco(trak, f, offset); if (offset) XMPlay_File->Seek(f, XMPlay_File->Tell(f)+offset); offset = 0; }
		if (!strncmp(id, "meta", 4)) { offset = mp4_checkmeta(trak, f, offset); if (offset) XMPlay_File->Seek(f, XMPlay_File->Tell(f)+offset); offset = 0; }
		while (offset) offset = mp4_readblock(trak, f, offset, false, indent + 2);
	}

	return size;
}

static void GetCoverImageFromMP4Stream(XMPFILE f)
{
	if (!f) return;
	XMPlay_File->Seek(f, 0);
	DWORD fsize = XMPlay_File->GetSize(f);

	mp4trak trak; memset(&trak, 0, sizeof(mp4trak));
	bool firstheader = true;
	while (fsize) { fsize = mp4_readblock(trak, f, fsize, firstheader); firstheader = false; }
}

static void FreeCoverImage()
{
	if (pcCoverImage && XMPlay_Misc) { XMPlay_Misc->Free(pcCoverImage); pcCoverImage = NULL; }
}

static void UnloadCoverImage()
{
	pc8CurrentFile[0] = '-';
	pc8CurrentFile[1] = 0;
	pcLastTrackName[0] = 0;
	clTimeLastCheckCover = 0;
	FreeCoverImage();
	pcCoverImage = NULL;
	HANDLE hThreadBck = ACD_hThread;
	ACD_hThread = NULL;
	if (hThreadBck)
	{
		ACD_ThreadSongChangedSince = true;
		if (WaitForSingleObject(hThreadBck, 10000) != WAIT_OBJECT_0) 
			TerminateThread(hThreadBck, 0);
		CloseHandle(hThreadBck);
	}
	for (std::vector<sFoundCover*>::iterator it = ACD_FoundCovers.begin(); it != ACD_FoundCovers.end(); ++it) delete (*it);
	ACD_FoundCovers.clear();
	ACD_ThreadSongChangedSince = false;
	while (ACD_SaveActive) Sleep(50);
	pActiveFoundCover = NULL;
	ACD_CurrentCoversSaveable = FALSE;
	ACD_ForceReDownload = 0;
	for (std::vector<sFoundCover*>::iterator itFoundCover = vecFoundCovers.begin(); itFoundCover != vecFoundCovers.end(); ++itFoundCover) delete (*itFoundCover);
	vecFoundCovers.clear();
}

static sFoundCover *FoundCoverGetAndCloseFile(XMPFILE f, char *pcName = NULL, int iRating = 0)
{
	if (!f) return NULL;
	int len = XMPlay_File->GetSize(f);
	if (len < 10) return NULL;
	sFoundCover *pFoundCover = new sFoundCover();
	pFoundCover->pcData = (unsigned char*)XMPlay_Misc->Alloc(len);
	pFoundCover->iDataLength = len;
	XMPlay_File->Read(f, pFoundCover->pcData, len);
	if (!FoundCoverSetType(pFoundCover)) { delete pFoundCover; return NULL; }
	XMPlay_File->Close(f);

	if (pcName) pFoundCover->strCoverTitle = pcName;
	if (iRating < 0)
	{
		strupr(pcName);
		if      (strcmp(pcName, "COVER") == 0)  pFoundCover->iCoverRating = 10+iRating;
		else if (strcmp(pcName, "FOLDER") == 0) pFoundCover->iCoverRating = 8+iRating;
		else if (strcmp(pcName, "FRONT") == 0)  pFoundCover->iCoverRating = 6+iRating;
		else if (strcmp(pcName, "ALBUM") == 0)  pFoundCover->iCoverRating = 4+iRating;
		else if (strstr(pcName, "COVER"))       pFoundCover->iCoverRating = 9+iRating;
		else if (strstr(pcName, "FOLDER"))      pFoundCover->iCoverRating = 7+iRating;
		else if (strstr(pcName, "FRONT"))       pFoundCover->iCoverRating = 5+iRating;
		else if (strstr(pcName, "ALBUM"))       pFoundCover->iCoverRating = 3+iRating;
		else                                    pFoundCover->iCoverRating = 2+iRating;
		memcpy(pcName, pFoundCover->strCoverTitle.c_str(), pFoundCover->strCoverTitle.size());
	}
	else pFoundCover->iCoverRating = iRating;
	return pFoundCover;
}

static void FindCoverImages()
{
	if (!pc8CurrentFile[0] || !XMPlay_Misc || !XMPlay_File || !XMPlay_Text) return;
	int iMaxType = 7;

	WCHAR wcFileNamePath[MAX_PATH] = L"";
	char *pc8ArchSplit = NULL;

	/*
	char* pc8Message = XMPlay_Misc->GetInfoText(XMPINFO_TEXT_MESSAGE);
	while (!pc8Message)
	{
		Sleep(50);
		pc8Message = XMPlay_Misc->GetInfoText(XMPINFO_TEXT_MESSAGE);
	}
	bool bActiveHasID3v2 = (!pc8Message ? false : (strstr(pc8Message, "ID3v2:") > 0));
	XMPlay_Misc->Free(pc8Message);
	*/

	if (/*!bActiveHasID3v2 && */(!strncmp(pc8CurrentFile+4, "://", 3) || !strncmp(pc8CurrentFile+3, "://", 3) || !strncmp(pc8CurrentFile+5, "://", 3))) iMaxType = 0;
	else
	{
		XMPFILE f = XMPlay_File->Open(pc8CurrentFile);
		DWORD t = (f?XMPlay_File->GetType(f):0);
		if (!f || t == XMPFILE_TYPE_NETFILE || t == XMPFILE_TYPE_NETSTREAM || !XMPlay_File->GetSize(f))
		{
			if (f) XMPlay_File->Close(f);
			iMaxType = 0;
		}
		else
		{
			pc8ArchSplit = strchr(pc8CurrentFile, '|');
			GetCoverImageFromID3Tag(f);
			GetCoverImageFromFLACTag(f);
			GetCoverImageFromMP4Stream(f);
			XMPlay_File->Close(f);

			int len = MultiByteToWideChar(CP_UTF8, 0, pc8CurrentFile, -1, wcFileNamePath, MAX_PATH);
			WCHAR *p = wcschr(wcFileNamePath, L'|');
			if (!p) p = wcFileNamePath+len-1;
			if (pc8ArchSplit)  while (p >= wcFileNamePath && *p != L'|') p--;
			while (p >= wcFileNamePath && *p != L'\\' && *p != L'/') p--;
			if (p >= wcFileNamePath) *(p+1) = 0;
		}
	}

	if (pc8ArchSplit)
	{
		int iArchDirLen = strlen(pc8ArchSplit);
		while(iArchDirLen > 0 && *(pc8ArchSplit+iArchDirLen) != '\\' && *(pc8ArchSplit+iArchDirLen) != '/') --iArchDirLen;
		*pc8ArchSplit = 0;
		XMPFILE f = XMPlay_File->Open(pc8CurrentFile);
		*pc8ArchSplit = '|';
		char *pcList = (f?XMPlay_File->ArchiveList(f):NULL);
		char *pNext = pcList;
		int len;
		while (pNext && *pNext && (len = strlen(pNext)))
		{
			char *p = pNext; pNext+=len+1;
			if (!iArchDirLen && (strchr(p, '/') || strchr(p, '\\'))) continue;
			if (iArchDirLen && strncmp(p, pc8ArchSplit+1, iArchDirLen)) continue;
			if (
				   strnicmp(p+len-3, "jpg", 3)
				&& strnicmp(p+len-4, "jpeg", 4)
				&& strnicmp(p+len-3, "png", 3)
				&& strnicmp(p+len-3, "bmp", 3)
				&& strnicmp(p+len-3, "tga", 3)
				&& strnicmp(p+len-3, "psd", 3)
				&& strnicmp(p+len-3, "gif", 3)
				) continue;
			char pc8ArchiveFile[sizeof(pc8CurrentFile)];
			memcpy(pc8ArchiveFile, pc8CurrentFile, pc8ArchSplit-pc8CurrentFile+1);
			strcpy(pc8ArchiveFile+(pc8ArchSplit-pc8CurrentFile+1), p);
			XMPFILE fa = XMPlay_File->Open(pc8ArchiveFile);
			if (!fa) continue;
					
			p = pc8ArchiveFile+strlen(pc8ArchiveFile);
			while (p >= pc8ArchiveFile && *p != L'.' && *p != L'\\' && *p != L'/') { p--; }
			if (*p == '.') *p = 0;
			
			sFoundCover* pFoundCover = FoundCoverGetAndCloseFile(fa, pc8ArchiveFile+(pc8ArchSplit-pc8CurrentFile)+1+iArchDirLen, -1);
			if (pFoundCover) vecFoundCovers.push_back(pFoundCover);
		}
		if (pcList) XMPlay_Misc->Free(pcList);
	}
	
	WCHAR wcFileNameTry[MAX_PATH];
	for (int iType = 0; iType <= iMaxType; iType++)
	{
		if (iType == 0)
		{
			if (!ACD_SavePath[0]) continue;
			WCHAR wcSearch[300];
			if (!GetGenericSongAlbumName300(wcSearch, false)) continue;
			FilterFileNameCharacters(wcSearch);
			MultiByteToWideChar(CP_OEMCP, 0, ACD_SavePath, -1, wcFileNameTry, MAX_PATH);
			WCHAR wcLastChar = wcFileNameTry[wcslen(wcFileNameTry)-1];
			if ((wcLastChar != L'\\') && (wcLastChar != L'/')) wcsncat(wcFileNameTry, L"\\", MAX_PATH);
			wcsncat(wcFileNameTry, wcSearch, MAX_PATH);
			wcsncat(wcFileNameTry, L".jpg", MAX_PATH);
		}
		else
		{
			wcscpy(wcFileNameTry, wcFileNamePath);
			if (iType == 1) wcscat(wcFileNameTry, L"*.jpg");
			else if (iType == 2) wcscat(wcFileNameTry, L"*.png");
			else if (iType == 3) wcscat(wcFileNameTry, L"*.jpeg");
			else if (iType == 4) wcscat(wcFileNameTry, L"*.bmp");
			else if (iType == 5) wcscat(wcFileNameTry, L"*.tga");
			else if (iType == 6) wcscat(wcFileNameTry, L"*.psd");
			else if (iType == 7) wcscat(wcFileNameTry, L"*.gif");
		}
		HANDLE hFind = INVALID_HANDLE_VALUE;

		typedef HANDLE (CALLBACK *FindFirstFileProc)(void*, void*);
		typedef BOOL (CALLBACK *FindNextFileProc)(HANDLE, void*);
		FindFirstFileProc pFindFirstFileMY = (NU?(FindFirstFileProc)&FindFirstFileA:(FindFirstFileProc)&FindFirstFileW);
		FindNextFileProc pFindNextFileMY = (NU?(FindNextFileProc)&FindNextFileA:(FindNextFileProc)&FindNextFileW);
		
		WIN32_FIND_DATAW FindFileDataW; WIN32_FIND_DATAA FindFileDataA;
		char cFileNameTry[MAX_PATH];
		if (NU) WideCharToMultiByte(CP_OEMCP, 0, wcFileNameTry, MAX_PATH, cFileNameTry, MAX_PATH, "_", NULL);
		void* pFileNameTry = (NU?(void*)cFileNameTry:(void*)wcFileNameTry);
		void *pFindFileData = (NU?(void*)&FindFileDataA:(void*)&FindFileDataW);
		if ((hFind = (*pFindFirstFileMY)(pFileNameTry, pFindFileData)) != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (NU) MultiByteToWideChar(CP_OEMCP, 0, FindFileDataA.cFileName, -1, FindFileDataW.cFileName, MAX_PATH);
				if (iType > 0) { wcscpy(wcFileNameTry, wcFileNamePath); wcscat(wcFileNameTry, FindFileDataW.cFileName); }
				char* pc8CoverFileName = XMPlay_Text->Unicode(wcFileNameTry, -1);
				if (XMPFILE f = XMPlay_File->Open(pc8CoverFileName))
				{
					FindFileDataW.cFileName[wcslen(FindFileDataW.cFileName)-(iType==3?5:4)] = 0;
					char cBaseData[50]; WideCharToMultiByte(CP_UTF8, 0, FindFileDataW.cFileName, MAX_PATH, cBaseData, 50, 0, 0);
					sFoundCover* pFoundCover = FoundCoverGetAndCloseFile(f, cBaseData, (iType == 0 ? 10 : -2));
					if (pFoundCover) vecFoundCovers.push_back(pFoundCover);
				}
			} while (pFindNextFileMY(hFind, pFindFileData));
			FindClose(hFind);
		}
	}
}

static void SaveActiveCoverImageToDisk()
{
	if (ACD_hThread) return;
	if (ACD_CurrentCoversSaveable && pActiveFoundCover->bSavedToDisk == false && pActiveFoundCover->iDataLength && pc8CurrentFile[0])
	{
		ACD_SaveActive = TRUE;
		//set up store folder and file name
		WCHAR ACD_StorePath[MAX_PATH];
		if (!ACD_SavePath[0])
		{
			MultiByteToWideChar(CP_UTF8, 0, pc8CurrentFile, -1, ACD_StorePath, MAX_PATH);
			WCHAR *p = ACD_StorePath+wcslen(ACD_StorePath);
			while (p >= ACD_StorePath && *p != L'\\' && *p != L'/') p--;
			if (p >= ACD_StorePath) *(p+1) = 0;
			wcscat(ACD_StorePath, L"Cover.jpg");
		}
		else
		{
			FilterFileNameCharacters(ACD_SearchPrevious);
			MultiByteToWideChar(CP_OEMCP, 0, ACD_SavePath, -1, ACD_StorePath, MAX_PATH);
			WCHAR wcLastChar = ACD_StorePath[wcslen(ACD_StorePath)-1];
			if ((wcLastChar != L'\\') && (wcLastChar != L'/')) wcsncat(ACD_StorePath, L"\\", MAX_PATH);
			wcsncat(ACD_StorePath, ACD_SearchPrevious, MAX_PATH);
			wcsncat(ACD_StorePath, L".jpg", MAX_PATH);
		}
		FILE *pF = NULL;
		if (ACD_StorePath[0])
		{
			char cACD_StorePath[MAX_PATH];
			if (NU) WideCharToMultiByte(CP_OEMCP, 0, ACD_StorePath, MAX_PATH, cACD_StorePath, MAX_PATH, "_", NULL);
			FILE *pF = (NU?fopen(cACD_StorePath, "rb") : _wfopen(ACD_StorePath, L"rb"));
			if (pF)
			{
				fseek(pF, 0, SEEK_END); int iCurLen = ftell(pF);
				fclose(pF);
				WCHAR wcMessage[300];
				_snwprintf(wcMessage, 300, L"'%s' (%d bytes) already exists.\x0A\x0DDo you want to replace it (new image %d bytes)?", ACD_StorePath, iCurLen, pActiveFoundCover->iDataLength);
				if (MessageBoxW(g_XMPlayHWND, wcMessage, L"Overwrite cover", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDNO) { ACD_SaveActive = FALSE; return; }
			}
			pF = (NU?fopen(cACD_StorePath, "wb") : _wfopen(ACD_StorePath, L"wb"));
			if (pF) 
			{
				fwrite(pActiveFoundCover->pcData, 1, pActiveFoundCover->iDataLength, pF);
				fclose(pF);

				for (std::vector<sFoundCover*>::iterator itFoundCover = vecFoundCovers.begin(); itFoundCover != vecFoundCovers.end(); ++itFoundCover) 
					(*itFoundCover)->bSavedToDisk = false;

				pActiveFoundCover->bSavedToDisk = true;
			}
		}
		ACD_SaveActive = FALSE;
	}
}

static void UpdateCoverImage(sFoundCover *pFoundCover)
{
	if (!XMPlay_Misc) return;
	while (ACD_SaveActive) Sleep(50);
	FreeCoverImage();
	if (!pFoundCover->pcData) return;
	pcCoverImage = stbi_load_from_memory(pFoundCover->pcData, pFoundCover->iDataLength, &iCoverImageWidth, &iCoverImageHeight, NULL, 3);
}

static BOOL SongChanged()
{
	int iFileChanged = (pc8CurrentFile[0] != 0);
	//if ((ACD_SavePath[0] || ACD_Enabled) && XMPlay_Status && !XMPlay_Status->IsPlaying()) return FALSE;
	if (char *pc8NewFile = XMPlay_Query->QueryString("currentsongfilename"))
	{
		iFileChanged = strcmp(pc8NewFile, pc8CurrentFile);
		strcpy(pc8CurrentFile, pc8NewFile);
		XMPlay_Query->FreeString(pc8NewFile);
	}
	else pc8CurrentFile[0] = 0;

	char pcCurTrackName[100] = "";
	if (XMPlay_Misc)
	{
		char *t0 = XMPlay_Misc->GetTag(0);
		char *t1 = XMPlay_Misc->GetTag(1);
		char *t2 = XMPlay_Misc->GetTag(2);
		char *tc = XMPlay_Misc->GetTag(-3);
		_snprintf(pcCurTrackName, 99, "%s%s%s%s", (t0?t0:""), (t1?t1:""), (t2?t2:""), (tc?tc:"")); pcCurTrackName[99] = 0;
		if (t0) XMPlay_Misc->Free(t0);
		if (t1) XMPlay_Misc->Free(t1);
		if (t2) XMPlay_Misc->Free(t2);
		if (tc) XMPlay_Misc->Free(tc);
	}

	if (iFileChanged || strcmp(pcCurTrackName, pcLastTrackName))
	{
		strcpy(pcLastTrackName, pcCurTrackName);
		return TRUE;
	}
	return FALSE;
}

static unsigned long CheckSumFoundCoverData(std::vector<sFoundCover*>& vecCovers)
{
	unsigned long s = 0;
	for (std::vector<sFoundCover*>::iterator itFoundCover = vecCovers.begin(); itFoundCover != vecCovers.end(); ++itFoundCover)
	{
		s += (*itFoundCover)->iDataLength;
		for (size_t i = 0; i < (*itFoundCover)->strCoverTitle.length(); i++)
			s += (*itFoundCover)->strCoverTitle[i];
	}
	return s;
}

static int GetURLData(std::string &strData, const char* url)
{
	strData.erase();
	for (int iRetry = 0; iRetry < 3 && !ACD_ThreadSongChangedSince; iRetry++)
	{
		XMPFILE f = XMPlay_File->Open(url);
		if (!f) continue;
		unsigned long m_lBytesRead;
		char buff[1024*16];
		while(XMPlay_File->NetIsActive(f) && (m_lBytesRead = XMPlay_File->Read(f, buff, 1024*16)) && m_lBytesRead && m_lBytesRead <= 1024*16 && !ACD_ThreadSongChangedSince)
			strData.append(buff, m_lBytesRead);
		if (strData.length()) return 1;
	}
	return 0;
}

static sFoundCover* CoverDownload_Add(std::string strCoverUrl, std::string strTitle, int num = 0)
{
	COVERDOWNLOAD_LOG1("Downloading Image - URL: <![CDATA[ %s ]]>", strCoverUrl.c_str());

	std::string strCoverImageData;
	GetURLData(strCoverImageData, strCoverUrl.c_str());
	if (ACD_ThreadSongChangedSince) return NULL;

	//content length 0 is error
	if (strCoverImageData.length() == 0) return NULL;

	sFoundCover *pDownloadedCover = new sFoundCover();
	pDownloadedCover->pcData = (unsigned char*)XMPlay_Misc->Alloc(strCoverImageData.length());
	memcpy(pDownloadedCover->pcData, strCoverImageData.data(), strCoverImageData.length());
	pDownloadedCover->iDataLength = strCoverImageData.length();
	if (!FoundCoverSetType(pDownloadedCover)) { delete pDownloadedCover; return NULL; }
	if (num) { pDownloadedCover->strCoverTitle = "Download "; char cNum[5]; sprintf(cNum, "%d", num); pDownloadedCover->strCoverTitle += cNum; }
	if (strTitle.length()) pDownloadedCover->strCoverTitle += ' ' + strTitle;
	ACD_FoundCovers.push_back(pDownloadedCover);
	COVERDOWNLOAD_LOG2("Got cover %d image data - Length: %d", ACD_FoundCovers.size(), strCoverImageData.length());
	return pDownloadedCover;
}

static void AppendStringUrlEnc(std::string& strUrl, const char* cSearch, int iUTF8Len = -1)
{
	if (iUTF8Len == -1) iUTF8Len = strlen(cSearch);
	for (int i = 0; i < iUTF8Len; i++) 
	{
		unsigned char c = (unsigned char)cSearch[i];
		if ((c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c=='-' || c=='.' || c=='_' || c=='~') { strUrl += c; }
		else { char cHex[5]; sprintf(cHex, "%%%X", c); strUrl += cHex; }
	}
}

static DWORD WINAPI CoverDownload_ThreadProc(LPVOID lpParam)
{
	#ifndef ACD_DISABLE_LOG
	ACD_clStarted = clock();
	std::string strLog; ACD_pstrLog = &strLog;
	#endif

	COVERDOWNLOAD_LOG0("Started auto cover downloading");

	char cSearch[300]; int iUTF8Len = WideCharToMultiByte(CP_UTF8, 0, ACD_SearchPrevious, -1, cSearch, 300, NULL, NULL) - 1;
	COVERDOWNLOAD_LOG1("Search query: <![CDATA[ %s ]]>", cSearch);

	int iServicePicLimit = (ACD_ServiceSelected == 7 || ACD_ServiceSelected == 11 || ACD_ServiceSelected >= 13 ? 2 : (ACD_ServiceSelected == 8 || ACD_ServiceSelected == 4 || ACD_ServiceSelected <= 2 ? 6 : 3));
	
	if ((ACD_ServiceSelected & 1<<ACD_SERVER_SERVICE_LFMALB) || (ACD_ServiceSelected & 1<<ACD_SERVER_SERVICE_LFMART))
	{
		char pcLastFmSearch[300] = "", *pcLastFmSearchSplit;
		char *art = XMPlay_Misc->GetTag(1), *alb = XMPlay_Misc->GetTag(2);
		if (!art) { GetGenericSongAlbumName300(pcLastFmSearch, false, 0, "/"); pcLastFmSearchSplit = strstr(pcLastFmSearch, "/"); }
		for (int iLastFmMode = 0; iLastFmMode < 2; iLastFmMode++)
		{
			std::string strUrl = "http://www.last.fm/music/";
			if (iLastFmMode == 0)
			{
				if (!(ACD_ServiceSelected & 1<<ACD_SERVER_SERVICE_LFMALB)) continue;
				if (!alb && !pcLastFmSearch[0]) continue;
				if (art && alb) { AppendStringUrlEnc(strUrl, art); strUrl.append("/"); AppendStringUrlEnc(strUrl, alb); }
				else AppendStringUrlEnc(strUrl, pcLastFmSearch);
			}
			else
			{
				if (!(ACD_ServiceSelected & 1<<ACD_SERVER_SERVICE_LFMART)) continue;
				if (art) AppendStringUrlEnc(strUrl, art);
				else { if (pcLastFmSearchSplit) *pcLastFmSearchSplit = 0; AppendStringUrlEnc(strUrl, pcLastFmSearch); }
			}
			strUrl += "/+images";

			std::string strHTML;
			COVERDOWNLOAD_LOG1("Getting Last.fm results: <![CDATA[ %s ]]>", strUrl.c_str());
			GetURLData(strHTML, strUrl.c_str());
			if (ACD_ThreadSongChangedSince) return 3;
			COVERDOWNLOAD_LOG1("Got Last.fm result page - Length: %d", strHTML.size());

			int iLastFMImgs = 0, iLastFMSkip = ACD_CoverCountByServices[(iLastFmMode == 0 ? ACD_SERVER_SERVICE_LFMALB : ACD_SERVER_SERVICE_LFMART)];
			for (std::string::size_type loc = strHTML.find("src=\"https://lastfm-img2.akamaized.net/i/u/avatar170s/", 0); loc != std::string::npos; loc = strHTML.find("src=\"https://lastfm-img2.akamaized.net/i/u/avatar170s/", loc+1))
			{
				if (iLastFMSkip && iLastFMSkip--) continue; ACD_CoverCountByServices[(iLastFmMode == 0 ? ACD_SERVER_SERVICE_LFMALB : ACD_SERVER_SERVICE_LFMART)]++;
				std::string::size_type locEnd = strHTML.find("\"", loc+6);
				std::string picurl = strHTML.substr(loc+5, locEnd-loc-5);
				if (ACD_ImageSize == 0) picurl.replace(38, 10, "770x0");
				if (ACD_ImageSize == 1) picurl.replace(38, 10, "300x300");

				std::string title;
				std::string::size_type locDesc = strHTML.find("alt=\"", locEnd);
				if (locDesc - locEnd < 70) title += strHTML.substr(locDesc+5, strHTML.find("\"", locDesc+5)-locDesc-1);
				else title += picurl.substr(picurl.rfind("/")+1);

				COVERDOWNLOAD_LOG2("Getting image %d - Title: \"%s\"", iLastFMImgs+1, title.c_str());
				if (CoverDownload_Add(picurl, (iLastFmMode == 0 ? "Last.fm Album: " : "Last.fm Artist: ") + title) && ++iLastFMImgs >= iServicePicLimit) break;
			}
			if (iLastFMImgs >= iServicePicLimit) break;
		}
		if (art) XMPlay_Misc->Free(art); if (alb) XMPlay_Misc->Free(alb);
	}

	#ifdef ACD_USE_GOOGLE
	if (ACD_ServiceSelected & 1<<ACD_SERVER_SERVICE_GOOGLE)
	{
		std::string strUrl = "https://www.google.com/search?source=lnms&tbm=isch&q=";
		AppendStringUrlEnc(strUrl, cSearch, iUTF8Len);
		if (ACD_ImageSize == 0) strUrl += "&tbs=isz:lt,islt:svga"; //larger than 800x600"
		if (ACD_ImageSize == 1) strUrl += "&tbs=isz:m"; //medium
		if (ACD_ImageSize == 2) strUrl += "&tbs=isz:i"; //icon
		strUrl += "&gbv=2&ie=UTF-8";
		COVERDOWNLOAD_LOG1("Getting Google Image results: <![CDATA[ %s ]]>", strUrl.c_str());
		std::string strHTML;
		GetURLData(strHTML, strUrl.c_str());
		if (ACD_ThreadSongChangedSince) return 3;
		COVERDOWNLOAD_LOG1("Got Google Image result page - Length: %d", strHTML.size());
		int iGoogleImgs = 0, iGoogleSkip = ACD_CoverCountByServices[ACD_SERVER_SERVICE_GOOGLE];
		for (std::string::size_type loc = strHTML.find("\"ou\":\"http", 0); loc != std::string::npos; loc = strHTML.find("\"ou\":\"http", loc+1))
		{
			if (iGoogleSkip && iGoogleSkip--) continue; ACD_CoverCountByServices[ACD_SERVER_SERVICE_GOOGLE]++;
			std::string::size_type locEnd = strHTML.find("\"", loc);
			std::string picurl = strHTML.substr(loc+6, locEnd-loc-6);
			if (picurl.length() > 1024) continue;
			std::string title;
			std::string::size_type locDesc = strHTML.find("\"pt\":\"", locEnd);
			if (locDesc - locEnd < 70) title += strHTML.substr(locDesc+6, strHTML.find("\"", locDesc+6)-locDesc-1);
			else title += picurl.substr(picurl.rfind("/")+1);
			COVERDOWNLOAD_LOG2("Getting image %d - Title: \"%s\"", iGoogleImgs+1, title.c_str());
			if (CoverDownload_Add(picurl, "Google: " + title) && ++iGoogleImgs >= iServicePicLimit) break;
		}
		for (std::string::size_type loc = strHTML.find("imgurl=http", 0); loc != std::string::npos; loc = strHTML.find("imgurl=http", loc+1))
		{
			if (iGoogleSkip && iGoogleSkip--) continue; ACD_CoverCountByServices[ACD_SERVER_SERVICE_GOOGLE]++;
			std::string::size_type locEnd = strHTML.find("&imgrefurl=", loc);
			std::string picurl = strHTML.substr(loc+7, locEnd-loc-7);
			if (picurl.length() > 1024) continue;
			std::string title = picurl.substr(picurl.rfind("/")+1, picurl.rfind(".")-picurl.rfind("/")-1);
			if (title.length() > 128) continue;
			COVERDOWNLOAD_LOG2("Getting image %d - Title: \"%s\"", iGoogleImgs+1, title.c_str());
			if (CoverDownload_Add(picurl, "Google: " + title) && ++iGoogleImgs >= iServicePicLimit) break;
		}
	}
	#endif

	if (ACD_ServiceSelected & 1<<ACD_SERVER_SERVICE_FLICKR)
	{
		std::string strUrl = "http://www.flickr.com/search/?ct=6&mt=photos&z=t&q=";
		AppendStringUrlEnc(strUrl, cSearch, iUTF8Len);
		COVERDOWNLOAD_LOG1("Getting Flickr results: <![CDATA[ %s ]]>", strUrl.c_str());
		std::string strHTML;
		GetURLData(strHTML, strUrl.c_str());
		if (ACD_ThreadSongChangedSince) return 3;
		COVERDOWNLOAD_LOG1("Got Flickr result page - Length: %d", strHTML.size());

		int iFlickrImgs = 0, iFlickrSkip = ACD_CoverCountByServices[ACD_SERVER_SERVICE_FLICKR];
		for (std::string::size_type loc = strHTML.find("url(//live", 0); loc != std::string::npos; loc = strHTML.find("url(//live", loc+1))
		{
			if (iFlickrSkip && iFlickrSkip--) continue; ACD_CoverCountByServices[ACD_SERVER_SERVICE_FLICKR]++;
			std::string::size_type locEnd = strHTML.find(")", loc+20);
			if (locEnd == std::string::npos || locEnd-loc > 256) continue;
			std::string picurl = std::string("https:") + strHTML.substr(loc+4, locEnd-loc-4-(strHTML[locEnd - 6] == '_' ? 6 : 4));
			if (ACD_ImageSize == 0) strUrl += ".jpg"; //full
			if (ACD_ImageSize == 1) strUrl += "_n.jpg"; //medium
			if (ACD_ImageSize == 2) strUrl += "_m.jpg"; //small
			std::string title = picurl.substr(picurl.rfind("/")+1, picurl.rfind(".")-picurl.rfind("/")-1);
			COVERDOWNLOAD_LOG2("Getting image %d - Title: \"%s\"", iFlickrImgs+1, title.c_str());
			char sizes[] = "b\0mst";
			for (int s = (!ACD_ImageSize ? 0 : ACD_ImageSize+1); s < sizeof(sizes); s++)
				if (CoverDownload_Add(picurl + (sizes[s] ? std::string("_") + sizes[s] : std::string("")) + std::string(".jpg"), "Flickr: " + title)) { iFlickrImgs++; break; }
			if (iFlickrImgs >= iServicePicLimit) break;
		}
	}

#ifdef ACD_USE_AMAZON
	std::string strXMLLog;
	if (/*!ACD_FoundCovers.size() && */ ACD_ServersSelected && ACD_ForceReDownload != 3)
	{
		bool bFoundFirstSizedImage = false; int iAmazonImages = 0;
		for (int iServer = 0; iServer < sizeof(ACD_Servers); iServer++)
		{
			if (!(ACD_ServersSelected & (1<<iServer))) continue;

			time_t rawtime;
			time(&rawtime);
			tm *ptm = gmtime ( &rawtime );
			char pcTime[100];
			sprintf(pcTime, "%04d-%02d-%02dT%02d%%3A%02d%%3A%02d.000Z", ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

			std::string strQuery = "AWSAccessKeyId=";
			strQuery += pcAWSKey;
			strQuery += "&Keywords=";
			AppendStringUrlEnc(strQuery, cSearch, iUTF8Len);
			strQuery += "&Operation=ItemSearch&ResponseGroup=Small%2CImages&SearchIndex=Music&Service=AWSECommerceService&Timestamp=";
			strQuery += pcTime;
			strQuery += "&Version=2009-03-31";

			std::string strToSign = "GET\nwebservices.";
			strToSign += ACD_Servers[iServer];
			strToSign += "\n/onca/xml\n" + strQuery;

			unsigned char hmac[SHA256_DIGEST_SIZE];
			hmac_sha256((unsigned char*)pcAWSSecret, sizeof(pcAWSSecret), (unsigned char*)strToSign.c_str(), strToSign.length(), hmac, SHA256_DIGEST_SIZE);

			char hmacb64[SHA256_DIGEST_SIZE*4/3+(SHA256_DIGEST_SIZE%3)];
			for (int im=0; im < SHA256_DIGEST_SIZE; im+=3)
			{
				static const char cb64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				hmacb64[im*4/3+0] = cb64[ hmac[im+0] >> 2 ];
				hmacb64[im*4/3+1] = cb64[ ((hmac[im+0] & 0x03) << 4) | ((hmac[im+1] & 0xf0) >> 4) ];
				hmacb64[im*4/3+2] = ((SHA256_DIGEST_SIZE-im) > 1 ? cb64[ ((hmac[im+1] & 0x0f) << 2) | ((SHA256_DIGEST_SIZE-im) > 2 ? ((hmac[im+2] & 0xc0) >> 6) : 0) ] : '=');
				hmacb64[im*4/3+3] = ((SHA256_DIGEST_SIZE-im) > 2 ? cb64[ hmac[im+2] & 0x3f ] : '=');
			}

			strQuery += "&Signature=";
			AppendStringUrlEnc(strQuery, hmacb64, sizeof(hmacb64));
			
			std::string strUrl = "http://webservices.";
			strUrl += ACD_Servers[iServer];
			strUrl += "/onca/xml?" + strQuery;

			COVERDOWNLOAD_LOG1("Checking server %s", ACD_Servers[iServer]);
			//COVERDOWNLOAD_LOG1("URL: <![CDATA[ %s ]]>", strUrl.c_str());

			std::string strXML;
			GetURLData(strXML, strUrl.c_str());
			if (ACD_ThreadSongChangedSince) return 3;
			COVERDOWNLOAD_LOG1("Got XML data - Length: %d", strXML.size());

			int iNumProducts = 0;
			std::string::size_type locCur, locNext = strXML.find( "<Item><ASIN>", 0);
			while ((locCur = locNext) != std::string::npos)
			{
				locNext = strXML.find( "<Item><ASIN>", locCur+12);
				if (strncmp(strXML.c_str()+locCur+6+6+10, "</ASIN>", 7)) continue;
				std::string strAsin = strXML.substr(locCur+12, 10);
				iNumProducts++;
				COVERDOWNLOAD_LOG2("Found product %d - ASIN: \"%s\"", iNumProducts, strAsin.c_str());

				std::string::size_type locImage[3];
				locImage[0] = strXML.find("<LargeImage><URL>", locCur+12);
				locImage[1] = strXML.find("<MediumImage><URL>", locCur+12);
				locImage[2] = strXML.find("<SmallImage><URL>", locCur+12);
				for (int i=0;i<3;i++) { if (locImage[i] != std::string::npos && locNext != std::string::npos && locImage[i] > locNext) locImage[i] = std::string::npos; }
				if (locImage[0] == std::string::npos && locImage[1] == std::string::npos && locImage[2] == std::string::npos) continue;

				for (int iCover = ACD_ImageSize; iCover <= 2; iCover++)
				{		
					std::string strCoverUrl, strTitle;
					if (locImage[iCover] == std::string::npos) strCoverUrl = "http://ecx.images-amazon.com/images/P/" + strAsin + ".01." + std::string(iCover == 2 ? "S" : (iCover == 1 ? "M" : "L")) + "ZZZZZZZ.jpg";
					else
					{
						std::string::size_type locSURL, locURL = locImage[iCover] + (iCover == 2 ? 17 : (iCover == 1 ? 18 : 17));
						if ((locSURL = strXML.find("</URL>", locURL)) == std::string::npos) continue;
						strCoverUrl = strXML.substr(locURL, locSURL-locURL);
					}
					//COVERDOWNLOAD_LOG2("Downloading Image - Dimensiontype: %d - URL: %s", iCover, strCoverUrl.c_str());

					std::string::size_type locTitle = strXML.find("<Title>", locImage[iCover]+20);
					std::string::size_type locTitleE = (locTitle != std::string::npos ? strXML.find("</Title>", locTitle+7) : std::string::npos);
					if (locTitle != std::string::npos && locTitleE != std::string::npos && ((locTitle < locNext && locTitleE < locNext) || locNext == std::string::npos)) 
						strTitle = strXML.substr(locTitle+7, locTitleE-locTitle-7);

					if (sFoundCover *ACD_DownloadedCover = CoverDownload_Add(strCoverUrl, strTitle, ++iAmazonImages))
					{
						if (!bFoundFirstSizedImage && iCover == ACD_ImageSize)
						{
							ACD_DownloadedCover->iCoverRating = 10;
							bFoundFirstSizedImage = true;
							COVERDOWNLOAD_LOG0("Accepted cover as first showing cover");
						}
						break;
					}
					if (ACD_ThreadSongChangedSince) return 3;
				}
			}

			std::string::size_type l = 0;
			while ((l = strXML.find(pcAWSKey, l)) != std::string::npos) strXML = strXML.replace(l, sizeof(pcAWSKey)-1, "");
			strXMLLog += strXML.substr( ((l = strXML.find("<ItemSearchResponse")) != std::string::npos) ? l : 0 ) +"\x0A\x0D";

			if (iAmazonImages) break;
		}
	}
#endif

#ifdef COVERDOWNLOAD_DO_LOG
	if (ACD_LogActive && pcXMPlayDir)
	{
		char pcLogFile[MAX_PATH];
		//char pcTempPath[MAX_PATH]: GetTempPath(MAX_PATH, pcTempPath); GetTempFileName(pcTempPath, "ACD", 0, pcLogFile); DeleteFile(pcLogFile); strcpy(pcLogFile+strlen(pcLogFile)-3, "xml");
		strcpy(pcLogFile, pcXMPlayDir);
		strcpy(pcLogFile+strlen(pcLogFile), "\\templog_cover_download.xml");
		FILE *pFileTemp = fopen(pcLogFile, "wb");
		fwrite(/*\xEF\xBB\xBF*/"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\x0A\x0D", 1, sizeof("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\x0A\x0D")-1, pFileTemp);
		fwrite("<CoverDownloadLog>\x0A\x0D", 1, sizeof("<CoverDownloadLog>\x0A\x0D")-1, pFileTemp);
		fwrite("<LogEntries>\x0A\x0D", 1, sizeof("<LogEntries>\x0A\x0D")-1, pFileTemp);
		fwrite(strLog.c_str(), 1, strLog.length(), pFileTemp);
		fwrite("</LogEntries>\x0A\x0D", 1, sizeof("</LogEntries>\x0A\x0D")-1, pFileTemp);
		#ifdef ACD_USE_AMAZON
		fwrite(strXMLLog.c_str(), 1, strXMLLog.length(), pFileTemp);
		#endif
		fwrite("</CoverDownloadLog>\x0A\x0D", 1, sizeof("</CoverDownloadLog>\x0A\x0D")-1, pFileTemp);
		fclose(pFileTemp);
		//ShellExecute(NULL, "open", pcLogFile, NULL, NULL, 0);
		//Sleep(1000); DeleteFile(pcTempFile);
	}
#endif

	return (ACD_FoundCovers.size() ? 1 : 0);
}

static void SetFixAlbumCover(const unsigned char *pcInData, int size, const char *pcFixBaseFile)
{
	if (!XMPlay_Misc) return;
	if (pcMyDLLDir[0] && XMPlay_File)
	{
		sFoundCover *pTempFoundCover = NULL;
		char pcCover[MAX_PATH];
		if (!pTempFoundCover)
		{
			strcpy(pcCover, pcMyDLLDir); strcat(pcCover, "\\");strcat(pcCover, pcFixBaseFile);strcat(pcCover, ".jpg");
			pTempFoundCover = FoundCoverGetAndCloseFile(XMPlay_File->Open(pcCover));
		}
		if (!pTempFoundCover)
		{
			strcpy(pcCover, pcMyDLLDir); strcat(pcCover, "\\");strcat(pcCover, pcFixBaseFile);strcat(pcCover, ".png");
			pTempFoundCover = FoundCoverGetAndCloseFile(XMPlay_File->Open(pcCover));
		}

		if (pTempFoundCover)
		{
			UpdateCoverImage(pTempFoundCover);
			delete pTempFoundCover;
			return;
		}
	}
	FreeCoverImage();
	pcCoverImage = stbi_load_from_memory(pcInData, size, &iCoverImageWidth, &iCoverImageHeight, NULL, 3);
}

static bool StartACDThread()
{
	HANDLE hThreadBck = ACD_hThread;
	ACD_hThread = NULL;
	if (hThreadBck)
	{
		ACD_ThreadSongChangedSince = true;
		if (WaitForSingleObject(hThreadBck, 5000) != WAIT_OBJECT_0) 
			TerminateThread(hThreadBck, 0);
		CloseHandle(hThreadBck);
		ACD_ThreadSongChangedSince = false;
	}
	DWORD lpThreadID;
	ACD_hThread = CreateThread(NULL, 0, CoverDownload_ThreadProc, NULL, 0, &lpThreadID);
	if (ACD_hThread) SetThreadPriority(ACD_hThread, THREAD_PRIORITY_LOWEST);
	SetFixAlbumCover(cAlbumCoverDownloading, sizeof(cAlbumCoverDownloading), "download");
	//pActiveFoundCover = NULL;
	return (ACD_hThread != NULL);
}

static BOOL CheckSongAndUpdateCoverImage()
{
	BOOL bUpdated = FALSE;
	if (ACD_hThread)
	{
		DWORD lpExitCode = 0;
		if (GetExitCodeThread(ACD_hThread, &lpExitCode) && (lpExitCode != STILL_ACTIVE))
		{
			CloseHandle(ACD_hThread);
			ACD_hThread = 0;

			for (std::vector<sFoundCover*>::iterator itFoundCover = ACD_FoundCovers.begin(); itFoundCover != ACD_FoundCovers.end(); ++itFoundCover)
			{
				if ((*itFoundCover)->iCoverRating) pActiveFoundCover = (*itFoundCover);
				vecFoundCovers.push_back(*itFoundCover);
			}
			ACD_FoundCovers.clear();

			if (lpExitCode == 3) { ACD_ForceReDownload = 0; goto CheckChangedSong; } //SongChanged
				
			if (vecFoundCovers.size()) //Success
			{
				if (!pActiveFoundCover) pActiveFoundCover = vecFoundCovers[0];
				UpdateCoverImage(pActiveFoundCover);
				bool bSaveable = (!strchr(pc8CurrentFile,'|') && strncmp(pc8CurrentFile+4, "://", 3) && strncmp(pc8CurrentFile+3, "://", 3) && strncmp(pc8CurrentFile+5, "://", 3));
				ACD_CurrentCoversSaveable = (ACD_SearchPrevious[0] && pc8CurrentFile[0] && pc8CurrentFile[1] && (ACD_SavePath[0] || bSaveable));
				if (!ACD_ForceReDownload && ((ACD_AutoSave && bSaveable) || (!bSaveable && ACD_AutoSaveStreamArchive))) SaveActiveCoverImageToDisk();
			}
			else SetFixAlbumCover(cNoAlbumCover, sizeof(cNoAlbumCover), "cover");

			ACD_ForceReDownload = 0;
			bUpdated = TRUE;
		}
		else if (SongChanged()) ACD_ThreadSongChangedSince = true;
	}
	else if (XMPlay_Query && ((!clTimeLastCheckCover) || ((clock() - clTimeLastCheckCover) > CLOCKS_PER_SEC/3)))
	{
		if (SongChanged() || (ACD_ForceReDownload > 0 && ACD_ForceReDownload < 3))
		{
			CheckChangedSong:
			while (ACD_SaveActive) Sleep(50);
			int iThreadPrioBefore = GetThreadPriority(GetCurrentThread());
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
			FreeCoverImage();
			ACD_CurrentCoversSaveable = FALSE;
			memset(ACD_CoverCountByServices, 0, sizeof(ACD_CoverCountByServices));

			if (!pc8CurrentFile[0])
			{
				for (std::vector<sFoundCover*>::iterator itFoundCover = vecFoundCovers.begin(); itFoundCover != vecFoundCovers.end(); ++itFoundCover) 
					delete (*itFoundCover);
				vecFoundCovers.clear();
			}
			else
			{
				std::vector<sFoundCover*> vecFoundCoversPrev = vecFoundCovers;
				vecFoundCovers.clear();
				if (ACD_ForceReDownload <= 0) FindCoverImages();
				//////for (std::vector<sFoundCover*>::iterator itFoundCover = vecFoundCovers.begin(); itFoundCover != vecFoundCovers.end(); ++itFoundCover) delete (*itFoundCover);
				//////vecFoundCovers.clear(); ACD_SaveToDisk = FALSE;  // DEBUG + /\ 

				if (vecFoundCovers.size())
				{
					bool bSameAsBefore = (vecFoundCoversPrev.size() == vecFoundCovers.size() && CheckSumFoundCoverData(vecFoundCoversPrev) == CheckSumFoundCoverData(vecFoundCovers));
					int iActivePrev = -1;
					for (int i=vecFoundCoversPrev.size()-1;i>=0;i--)
					{
						if (vecFoundCoversPrev[i] == pActiveFoundCover) iActivePrev=i; 
						delete (vecFoundCoversPrev[i]); 
					}
					if (iActivePrev >= 0 && bSameAsBefore)
					{
						UpdateCoverImage(pActiveFoundCover = vecFoundCovers[iActivePrev]);
						bUpdated = FALSE;
					}
					else
					{
						int iHighestRating = 0;
						pActiveFoundCover = vecFoundCovers[0];
						for (std::vector<sFoundCover*>::iterator itFoundCover = vecFoundCovers.begin(); itFoundCover != vecFoundCovers.end(); ++itFoundCover)
						{
							if ((*itFoundCover)->iCoverRating > iHighestRating)
							{
								pActiveFoundCover = (*itFoundCover);
								iHighestRating = (*itFoundCover)->iCoverRating;
							}
						}
						UpdateCoverImage(pActiveFoundCover);
						bUpdated = TRUE;
					}
					ACD_SearchPrevious[0] = 0;
				}
				else if (XMPlay_Misc && XMPlay_File && ACD_Enabled && (
				#ifdef ACD_USE_AMAZON
				ACD_ServersSelected || 
				#endif
				ACD_ServiceSelected))
				{
					ACD_ThreadSongChangedSince = false;

					WCHAR wcSearch[300];
					BOOL bGotSearch = GetGenericSongAlbumName300(wcSearch, true);
					if (ACD_ForceReDownload <= 1 && vecFoundCoversPrev.size() && !wcscmp(wcSearch, ACD_SearchPrevious))
					{
						ACD_ForceReDownload = 0;
						vecFoundCovers = vecFoundCoversPrev;
						UpdateCoverImage(pActiveFoundCover);
					}
					else
					{
						for (std::vector<sFoundCover*>::iterator it = vecFoundCoversPrev.begin(); it != vecFoundCoversPrev.end(); ++it) 
							delete (*it);
						if (bGotSearch)
						{
							wcscpy(ACD_SearchPrevious, wcSearch);
							bUpdated = StartACDThread();
							pActiveFoundCover = NULL;
						}
					}
				}
				else if (ACD_ForceReDownload) ACD_ForceReDownload = 0;
			}

			if (!pcCoverImage)
			{
				bUpdated = (pActiveFoundCover != NULL||!iCoverImageWidth);
				SetFixAlbumCover(cNoAlbumCover, sizeof(cNoAlbumCover), "cover");
				pActiveFoundCover = NULL;
			}

			SetThreadPriority(GetCurrentThread(), iThreadPrioBefore);
		}
		else if (ACD_ForceReDownload == 3 && ACD_SearchPrevious[0])
			bUpdated = StartACDThread();
		clTimeLastCheckCover = clock();
	}
	return bUpdated;
}

static BOOL receivequeryinterface(QueryInterface* queryinterface)
{
	XMPlay_Query = queryinterface;
	InterfaceProc xmpface = NULL;
	if (queryinterface->QueryInt("xmplay:interface", (int*)&xmpface) && xmpface)
	{
		XMPlay_Misc = (XMPFUNC_MISC*)xmpface(XMPFUNC_MISC_FACE);
		XMPlay_Status = (XMPFUNC_STATUS*)xmpface(XMPFUNC_STATUS_FACE);
		XMPlay_File = (XMPFUNC_FILE*)xmpface(XMPFUNC_FILE_FACE);
		XMPlay_Text = (XMPFUNC_TEXT*)xmpface(XMPFUNC_TEXT_FACE);
		unsigned char x[4]; const char *c;
		c = XMPlay_Misc->GetSkinConfig("color_infoback");
		if (c) { sscanf(c, "%x%x%x", x, x+1, x+2); colBackground = RGB(x[0], x[1], x[2]); cBackground[0] = x[2]; cBackground[1] = x[1]; cBackground[2] = x[0]; }
		c = XMPlay_Misc->GetSkinConfig("color_listcurrent");
		if (c) { sscanf(c, "%x%x%x", x, x+1, x+2); colInfoLine1 = RGB(x[0], x[1], x[2]); }
		c = XMPlay_Misc->GetSkinConfig("color_title");
		if (c) { sscanf(c, "%x%x%x", x, x+1, x+2); colInfoLine2 = RGB(x[0], x[1], x[2]); }
		c = XMPlay_Misc->GetSkinConfig("color_text");
		if (c) { sscanf(c, "%x%x%x", x, x+1, x+2); colInfoLine3 = RGB(x[0], x[1], x[2]); }
		g_XMPlayHWND = XMPlay_Misc->GetWindow();
	}
	return TRUE;
}

static void initCoverMenu()
{
	if (!hCoverMenu)
	{
		hCoverMenu = CreatePopupMenu();
		char pcCycleCover[100];
		_snprintf(pcCycleCover, 99, "Cycle covers every %d seconds", iCycleCoverSeconds);
		AppendMenu(hCoverMenu, MF_STRING|(bCycleCover ? MF_CHECKED : MF_UNCHECKED), 150, pcCycleCover);
	}
	else
	{
		DeleteMenu(hCoverMenu, 151, 0);
		DeleteMenu(hCoverMenu, 152, 0);
		DeleteMenu(hCoverMenu, 153, 0);
		for (int i=300;i<=399 && DeleteMenu(hCoverMenu, i, 0)!=0;i++);
	}
	if (int s = vecFoundCovers.size())
	{
		if (ACD_CurrentCoversSaveable) AppendMenu(hCoverMenu, MF_STRING|(pActiveFoundCover->bSavedToDisk ? MFS_DISABLED : MFS_ENABLED), 151, "Save selected downloaded cover");
		for (int i = 0; i < ACD_SERVER_SERVICE_TOTAL; i++) if (ACD_CoverCountByServices[i])
			{ AppendMenu(hCoverMenu, MF_STRING|(ACD_hThread?MFS_DISABLED:MFS_ENABLED), 152, "Download more images..."); break; }
		AppendMenu(hCoverMenu, MFT_SEPARATOR, 153, NULL);
	}
	int iID = 300;
	for (std::vector<sFoundCover*>::iterator itFoundCover = vecFoundCovers.begin(); itFoundCover != vecFoundCovers.end(); ++itFoundCover)
	{
		std::string* pstr = &(*itFoundCover)->strCoverTitle;
		if (NU) { AppendMenuA(hCoverMenu, MF_STRING|((*itFoundCover)==pActiveFoundCover ? MF_CHECKED : MF_UNCHECKED), iID++, pstr->c_str()); continue; }
		WCHAR* pwcEntry = (WCHAR*)XMPlay_Misc->Alloc(pstr->length()*2+2);
		Utf2Uni(pstr->c_str(), -1, pwcEntry, pstr->length()+1);
		AppendMenuW(hCoverMenu, MF_STRING|((*itFoundCover)==pActiveFoundCover ? MF_CHECKED : MF_UNCHECKED), iID++, pwcEntry);
		XMPlay_Misc->Free(pwcEntry);
	}
}

static void initACDMenu()
{
	UINT d = (ACD_Enabled?MFS_ENABLED:MFS_DISABLED);
	if (!hACDMenu) 
	{
		hACDMenu = CreatePopupMenu();
		AppendMenu(hACDMenu, MF_STRING|(ACD_Enabled ? MF_CHECKED : MF_UNCHECKED), 101, (/*ACD_Enabled?"Disable":*/"Enable"));
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_SavePath[0] ? MF_CHECKED : MF_UNCHECKED), 102, "Set default cover directory");
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_AutoSave ? MF_CHECKED : MF_UNCHECKED), 103, "Auto save downloaded covers");
		AppendMenu(hACDMenu, MF_STRING|(ACD_SavePath[0]&&ACD_Enabled ? MFS_ENABLED : MFS_DISABLED)|(ACD_AutoSaveStreamArchive ? MF_CHECKED : MF_UNCHECKED), 104, "Auto save stream/archive");
		AppendMenu(hACDMenu, MF_STRING|d, 105, "Download now");
		AppendMenu(hACDMenu, MFT_SEPARATOR|d, 106, NULL);
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ImageSize == 0 ? MF_CHECKED : MF_UNCHECKED), 107, "Size: Large");
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ImageSize == 1 ? MF_CHECKED : MF_UNCHECKED), 108, "Size: Medium");
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ImageSize == 2 ? MF_CHECKED : MF_UNCHECKED), 109, "Size: Small");
		AppendMenu(hACDMenu, MFT_SEPARATOR|d, 110, NULL);
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ArtistAlbumTitle&1 ? MF_CHECKED : MF_UNCHECKED), 111, "Search with: Artist");
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ArtistAlbumTitle&2 ? MF_CHECKED : MF_UNCHECKED), 112, "Search with: Album");
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ArtistAlbumTitle&4 ? MF_CHECKED : MF_UNCHECKED), 113, "Search with: Title");
		AppendMenu(hACDMenu, MFT_SEPARATOR|d, 114, NULL);
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ServiceSelected&1<<ACD_SERVER_SERVICE_LFMALB ? MF_CHECKED : MF_UNCHECKED), 290, "Use Last.fm - Album");
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ServiceSelected&1<<ACD_SERVER_SERVICE_LFMART ? MF_CHECKED : MF_UNCHECKED), 291, "Use Last.fm - Artist");
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ServiceSelected&1<<ACD_SERVER_SERVICE_FLICKR ? MF_CHECKED : MF_UNCHECKED), 292, "Use Flickr");
		#ifdef ACD_USE_GOOGLE
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_ServiceSelected&1<<ACD_SERVER_SERVICE_GOOGLE ? MF_CHECKED : MF_UNCHECKED), 293, "Use Google Images");
		#endif
		#ifdef ACD_USE_AMAZON
		AppendMenu(hACDMenu, MFT_SEPARATOR|d, 115, NULL);
		for (int i=0;i<(sizeof(ACD_Servers)/sizeof(char*));i++)
		{
			char MyMenuButton[50];
			sprintf(MyMenuButton, "Use %s", ACD_Servers[i]);
			AppendMenu(hACDMenu, MF_STRING|d|(ACD_ServersSelected&(1<<i) ? MF_CHECKED : MF_UNCHECKED), 200+i, MyMenuButton);
		}
		#endif
		#ifndef ACD_DISABLE_LOG
		AppendMenu(hACDMenu, MFT_SEPARATOR|d, 116, NULL);
		AppendMenu(hACDMenu, MF_STRING|d|(ACD_LogActive ? MF_CHECKED : MF_UNCHECKED), 117, "Transfer log (debug)");
		#endif
	}
	else
	{
		for (int x=102;x<=117;x++) { EnableMenuItem(hACDMenu, x, (x!=104?d:(ACD_SavePath[0]&&ACD_Enabled ? MFS_ENABLED : MFS_DISABLED))); }
		#ifdef ACD_USE_AMAZON
		for (int i=0;i<(sizeof(ACD_Servers)/sizeof(char*));i++) { EnableMenuItem(hACDMenu, 200+i, d); }
		#endif
		for (int s=0;s<ACD_SERVER_SERVICE_TOTAL           ;s++) { EnableMenuItem(hACDMenu, 290+s, d); }
	}
}

static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (uMsg == BFFM_INITIALIZED)
		SendMessage(hwnd, BFFM_SETSELECTION, TRUE, (LPARAM)(ACD_SavePath[0]?ACD_SavePath:"C:\\"));
	return 0;
}

static void ACDSaveDirectory()
{
	BROWSEINFOA bi;
	ZeroMemory(&bi,sizeof(bi));
	bi.lpszTitle = "Pick a Directory";
	bi.ulFlags = 64 | BIF_RETURNONLYFSDIRS | BIF_STATUSTEXT;
	bi.lpfn = BrowseCallbackProc;
	LPITEMIDLIST pidl = SHBrowseForFolderA ( &bi );
	if ( pidl != 0 )
	{
		if (!SHGetPathFromIDList(pidl, ACD_SavePath)) ACD_SavePath[0] = 0;
		// free memory used
		IMalloc * imalloc = 0;
		if (SUCCEEDED(SHGetMalloc(&imalloc))) { imalloc->Free(pidl); imalloc->Release(); }
	}
}

static void initialize(void)
{
	if (!g_XMPlayHWND) g_XMPlayHWND = FindWindow("XMPLAY-MAIN", NULL);

	int iLen = GetModuleFileName(NULL,pcXMPlayDir,MAX_PATH);
	char *p;
	for (p = pcXMPlayDir + iLen; p >= pcXMPlayDir && *p != '\\' && *p != '/'; p--); *p = 0;

	OSVERSIONINFO vi; ZeroMemory(&vi, sizeof(OSVERSIONINFO)); vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionExA(&vi);
	NU = (vi.dwMajorVersion < 5);

	if (!XMPlay_Misc) 
	{
		MessageBoxA(g_XMPlayHWND, "The cover feature requires a newer XMPlay version.\nPlease download the newest release from un4seen.com and/or use download the program from http://www.un4seen.com/stuff/xmplay.exe", "XMPlay version too old", MB_ICONEXCLAMATION); 
	}
	else
	{
		hMenu = CreatePopupMenu();
		
		AppendMenu(hMenu, MF_STRING|MFS_ENABLED|(bFadeAlbumart ? MF_CHECKED : MF_UNCHECKED), 6, "&Fade Album Art");
		AppendMenu(hMenu, MF_STRING|MFS_ENABLED, 7, "Album Art align&ment/stretch");
		AppendMenu(hMenu, MF_STRING|MFS_ENABLED, 8, "Select &Cover");
		AppendMenu(hMenu, MF_STRING|MFS_ENABLED, 9, "Auto cover &download");

		initACDMenu();
		MENUITEMINFO ItemInfo;
		ItemInfo.cbSize = sizeof(MENUITEMINFO);
		ItemInfo.fMask = MIIM_SUBMENU;
		ItemInfo.hSubMenu = hACDMenu;
		SetMenuItemInfo(hMenu, 9, FALSE, &ItemInfo);
	}
}

static BOOL clicked(int x, int y, int buttons)
{
	//MENUITEMINFO ItemInfo;
	if (!hMenu) return TRUE;
	POINT cur;
	GetCursorPos(&cur);
	long u = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RETURNCMD, cur.x, cur.y, g_XMPlayHWND, NULL);
	switch (u)
	{
		case 6: //fade album art
			bFadeAlbumart = !bFadeAlbumart;
			CheckMenuItem( hMenu, 6, (bFadeAlbumart ? MF_CHECKED : MF_UNCHECKED));
			break;
		case 7: //album art align
			iAlbumArtAlign = (iAlbumArtAlign + 1)%5;
			iLastScaledWidth = iLastScaledHeight = 0;
			break;
		case 101: //Enable/Disable auto cover downloader	
			ACD_Enabled = !ACD_Enabled;
			CheckMenuItem(hACDMenu, 101, (ACD_Enabled ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem(hACDMenu, 103, (ACD_AutoSave ? MF_CHECKED : MF_UNCHECKED));
			EnableMenuItem(hACDMenu, 104, (ACD_SavePath[0] ? MFS_ENABLED : MFS_DISABLED));
			CheckMenuItem(hACDMenu, 104, (ACD_AutoSaveStreamArchive ? MF_CHECKED : MF_UNCHECKED));
			initACDMenu();
			if (!ACD_Enabled && ACD_SavePath[0] && MessageBox(g_XMPlayHWND, "Do you want to keep the chosen default directory as a source for covers?", "Default directory", MB_YESNO | MB_ICONQUESTION) == IDNO) ACD_SavePath[0] = 0;
			if (ACD_Enabled) ACD_ForceReDownload = 1;
			break;
		case 102: //Set cover directory
			if (MessageBox(g_XMPlayHWND, "Do you want to use a default directory for cover images?\x0A\x0D(Chosing no loads and saves covers only to songs directory and disables the ability to save covers from streamed music or songs stored in an archive)", "Default directory", MB_YESNO | MB_ICONQUESTION) == IDNO) ACD_SavePath[0] = 0;
			else ACDSaveDirectory();
			CheckMenuItem(hACDMenu, 102, (ACD_SavePath[0] ? MF_CHECKED : MF_UNCHECKED));
			EnableMenuItem(hACDMenu, 104, (ACD_SavePath[0] ? MFS_ENABLED : MFS_DISABLED));
			break;
		case 103: //Auto save downloaded covers
			ACD_AutoSave = !ACD_AutoSave;
			CheckMenuItem(hACDMenu, 103, (ACD_AutoSave ? MF_CHECKED : MF_UNCHECKED));
			break;
		case 104: //Auto save streamed/archived songs covers
			ACD_AutoSaveStreamArchive = !ACD_AutoSaveStreamArchive;
			CheckMenuItem(hACDMenu, 104, (ACD_AutoSaveStreamArchive ? MF_CHECKED : MF_UNCHECKED));
			break;
		case 105: //Redownload
			ACD_ForceReDownload = 2;
			break;
		case 107: //ACD Size Large
		case 108: //ACD Size Medium
		case 109: //ACD Size Small
			CheckMenuItem(hACDMenu, 107+ACD_ImageSize, MF_UNCHECKED);
			CheckMenuItem(hACDMenu, u, MF_CHECKED);
			ACD_ImageSize = (char)(u-107);
			break;
		case 111: //ACD Artist
		case 112: //ACD Album
		case 113: //ACD Title
			if (ACD_ArtistAlbumTitle & (1<<(u-111)) && ACD_ArtistAlbumTitle > (unsigned long)(1<<(u-111))) ACD_ArtistAlbumTitle -= (1<<(u-111));
			else { ACD_ArtistAlbumTitle |= (1<<(u-111)); }
			CheckMenuItem(hACDMenu, u, ((ACD_ArtistAlbumTitle & (1<<(u-111))) ? MF_CHECKED : MF_UNCHECKED));
			break;
		#ifndef ACD_DISABLE_LOG
		case 117: //ACD LogActive
			ACD_LogActive = !ACD_LogActive;
			CheckMenuItem(hACDMenu, 117, (ACD_LogActive ? MF_CHECKED : MF_UNCHECKED));
			break;
		#endif
		case 150: //Cycle Covers
			bCycleCover = !bCycleCover;
			CheckMenuItem(hCoverMenu, 150, (bCycleCover ? MF_CHECKED : MF_UNCHECKED));
			//clCycleCoverTime = 0;
			break;
		case 151: //save downloaded cover to disk
			SaveActiveCoverImageToDisk();
			EnableMenuItem(hCoverMenu, 151, MFS_DISABLED);
			break;
		case 152: //download more images
			ACD_ForceReDownload = 3;
			EnableMenuItem(hCoverMenu, 152, MFS_DISABLED);
			break;
		default:
			break;
	}
	#ifdef ACD_USE_AMAZON
	if (u >= 200 && u <= 220) //amazon server selection
	{
		if (ACD_ServersSelected & (1<<(u-200))/* && ACD_ServersSelected > (unsigned long)(1<<(u-200))*/) ACD_ServersSelected -= (1<<(u-200));
		else { ACD_ServersSelected |= (1<<(u-200)); }
		CheckMenuItem(hACDMenu, u, ((ACD_ServersSelected & (1<<(u-200))) ? MF_CHECKED : MF_UNCHECKED));
	}
	#endif
	if (u >= 290 && u <= 299) //service selection
	{
		if (ACD_ServiceSelected & (1<<(u-290))) ACD_ServiceSelected -= (1<<(u-290));
		else { ACD_ServiceSelected |= (1<<(u-290)); }
		CheckMenuItem(hACDMenu, u, ((ACD_ServiceSelected & (1<<(u-290))) ? MF_CHECKED : MF_UNCHECKED));
	}
	if (u >= 300 && u <= 399 && (vecFoundCovers.size() >= (size_t)(u-300)) && (vecFoundCovers[u-300] != pActiveFoundCover)) //cover selection
	{
		for(int i=300;i<=399 && CheckMenuItem(hCoverMenu, i, (i==u?MF_CHECKED:MF_UNCHECKED))!=-1; i++);
		UpdateCoverImage(pActiveFoundCover = vecFoundCovers[u-300]);
		EnableMenuItem(hCoverMenu, 151, (vecFoundCovers[u-300]->bSavedToDisk ? MFS_DISABLED : MFS_ENABLED));
		iLastScaledWidth = iLastScaledHeight = 0;
	}
	return TRUE;
}

static BOOL deinit()
{
	DWORD lpExitCode = 1;

	if (hMenu) { DestroyMenu(hMenu); hMenu = NULL; }
	if (hACDMenu) { DestroyMenu(hACDMenu); hACDMenu = NULL; }
	if (hCoverMenu) { DestroyMenu(hCoverMenu); hCoverMenu = NULL; }
	if (pScaledCoverVideo) XMPlay_Misc->Free(pScaledCoverVideo);

	UnloadCoverImage();

	return TRUE;
}

static BOOL render(unsigned long* Video, int width, int height, int pitch, VisData* pVD)
{
	static int xoff,yoff,scx,scy;
	BOOL bUpdated;
	if ((bUpdated = CheckSongAndUpdateCoverImage()) || ((iLastScaledWidth != width || iLastScaledHeight != height) && ((clock() - clTimeLastScaledCover) > CLOCKS_PER_SEC/3)))
	{
		BOOL bDoFadeAndCycle = bUpdated;
		if (iLastScaledWidth ==  0) bDoFadeAndCycle = TRUE;
		if (iLastScaledWidth == -1) bDoFadeAndCycle = FALSE;

		if (bDoFadeAndCycle) clCycleCoverTime = clock() + (CLOCKS_PER_SEC*iCycleCoverSeconds);
		clFadeStart = (bDoFadeAndCycle ? clock() : -1000000);
		iFadeLevel = 0;

		iLastScaledWidth = width;
		iLastScaledHeight = height;
		clTimeLastScaledCover = clock();

		if (bUpdated)
		{
			initCoverMenu();
			MENUITEMINFO ItemInfo;
			char MyMenuButton[50];
			ItemInfo.cbSize = sizeof(MENUITEMINFO);
			ItemInfo.fMask = MIIM_SUBMENU|MIIM_STATE|MIIM_DATA|MIIM_TYPE;
			ItemInfo.fState = (vecFoundCovers.size()?MFS_ENABLED:MFS_DISABLED);
			ItemInfo.hSubMenu = hCoverMenu;
			ItemInfo.fType = MFT_STRING;
			wsprintf(MyMenuButton, (vecFoundCovers.size()?"Select &Cover (%d)":"Select &Cover"), vecFoundCovers.size());
			ItemInfo.dwTypeData = MyMenuButton;
			ItemInfo.cch = strlen(MyMenuButton);
			SetMenuItemInfo(hMenu, 8, FALSE, &ItemInfo);
		}

		if (pScaledCoverVideo) { XMPlay_Misc->Free(pScaledCoverVideo); pScaledCoverVideo = NULL; }
		if (pcCoverImage && iCoverImageWidth > 0 && iCoverImageHeight > 0)
		{
			scx = iCoverImageWidth * height / iCoverImageHeight;
			scy = iCoverImageHeight * width / iCoverImageWidth;
			if (scx > width) scx = iCoverImageWidth * width / iCoverImageWidth;
			else             scy = iCoverImageHeight * height / iCoverImageHeight;
			xoff = (iAlbumArtAlign == 1 ? 0 : (iAlbumArtAlign == 2 ? width - scx : (width - scx)/2 ) );
			yoff = (iAlbumArtAlign == 1 ? 0 : (iAlbumArtAlign == 2 ? height - scy : (height - scy)/2 ) );
			if (iAlbumArtAlign == 3) { scx = width; scy = height; xoff = yoff = 0; }
			if (iAlbumArtAlign == 4) { scx = iCoverImageWidth; scy = iCoverImageHeight; xoff = (width > scx ? (width - scx)/2 : 0); yoff = (height > scy ? (height - scy)/2 : 0); }

			pScaledCoverVideo = (unsigned char*)XMPlay_Misc->Alloc(3 *  scx * scy);

			for (int y = 0; y < scy; y++)
			{
				float sy = y * (iCoverImageHeight-1) / ((float)scy-1.0f);
				unsigned char *rowA = pcCoverImage+(iCoverImageWidth * (0+(int)sy) * 3);
				unsigned char *rowB = rowA+(iCoverImageWidth*3);
				for (int x = 0; x < scx; x++)
				{
					float sx = x * (iCoverImageWidth-1) / ((float)scx-1.0f);
					int ix = (int)sx;
					for (char c = 0; c < 3; c++)
					{
						int ixc = ix * 3 + c;
						float v = rowA[ixc] * (((float)ix) - sx + 1.0f);
						if (sx > ix) v += rowA[ixc+3] * (sx - ((float)ix));
						if (sy > (int)sy)
						{
							float vB = rowB[ixc] * (((float)ix) - sx + 1.0f);
							if (sx > ix) vB += rowB[ixc+3] * (sx - ((float)ix));
							v = (v * (((float)(int)sy) - sy + 1.0f)) + (vB * (sy - ((float)(int)sy)));
						}
						if      (c == 0) pScaledCoverVideo[(scx * y + x)*3 + 2] = (unsigned char)v;
						else if (c == 1) pScaledCoverVideo[(scx * y + x)*3 + 1] = (unsigned char)v;
						else if (c == 2) pScaledCoverVideo[(scx * y + x)*3 + 0] = (unsigned char)v;
					}
				}
			}

		}
		else { xoff = yoff = scx = scy = 0; }
	}
	else if (bCycleCover && (clock() > clCycleCoverTime) && (vecFoundCovers.size() > 1))
	{
		std::vector<sFoundCover*>::iterator itFoundCover;
		int iMenuIndex = 0;
		for (itFoundCover = vecFoundCovers.begin(); itFoundCover != vecFoundCovers.end(); ++itFoundCover) 
			if ((*itFoundCover) == pActiveFoundCover) break; else iMenuIndex++;
		if (itFoundCover != vecFoundCovers.end()) { itFoundCover++; iMenuIndex++; }
		if (itFoundCover == vecFoundCovers.end()) { itFoundCover = vecFoundCovers.begin(); iMenuIndex = 0; }

		//for(int i=300;i<=399 && CheckMenuItem(hCoverMenu, i, (i==iMenuIndex?MF_CHECKED:MF_UNCHECKED))!=-1; i++);
		for(int i=vecFoundCovers.size()-1;i>=0;i--)
		{
			//if (!ModifyMenu(hCoverMenu, i+300, MF_STRING|(i==iMenuIndex?MF_CHECKED:MF_UNCHECKED), i+300, vecFoundCovers[i]->strCoverTitle.c_str())) break;
			std::string* pstr = &vecFoundCovers[i]->strCoverTitle;
			if (NU) { ModifyMenuA(hCoverMenu, i+300, MF_STRING|(i==iMenuIndex?MF_CHECKED:MF_UNCHECKED), i+300, pstr->c_str()); continue; }
			WCHAR* pwcEntry = (WCHAR*)XMPlay_Misc->Alloc(pstr->length()*2+2);
			Utf2Uni(pstr->c_str(), -1, pwcEntry, pstr->length()+1);
			ModifyMenuW(hCoverMenu, i+300, MF_STRING|(i==iMenuIndex?MF_CHECKED:MF_UNCHECKED), i+300, pwcEntry);
			XMPlay_Misc->Free(pwcEntry);

		}
		EnableMenuItem(hCoverMenu, 151, ((*itFoundCover)->bSavedToDisk ? MFS_DISABLED : MFS_ENABLED));
		UpdateCoverImage(pActiveFoundCover = *itFoundCover);
		iLastScaledWidth = iLastScaledHeight = 0;
	}

	if (iFadeLevel < 10)
	{
		iFadeLevel = (bFadeAlbumart && clFadeStart >= 0 ? (clock() - clFadeStart) * CLOCKS_PER_SEC / 50000 : 10);
		if (iFadeLevel >= 10)
		{
			int z;
			for (z=0;z<width;z++) Video[z] = colBackground;
			for (z=1;z<height;z++) memcpy(&Video[z*pitch], Video, width*sizeof(long));
		}
		else
		{
			bool bFillX = (scx < width);
			unsigned char v[3] = { *((unsigned char*)(&colBackground)+0), *((unsigned char*)(&colBackground)+1), *((unsigned char*)(&colBackground)+2) };
			for (int z = 0; z < (bFillX ? width - scx : height - scy); z++)
			{
				for (int zz = 0; zz < (bFillX ? height : width); zz++)
				{
					unsigned char *p = (unsigned char*)&Video[pitch * (bFillX ? zz : (z >= yoff ? z+scy : z)) + (!bFillX ? zz : (z >= xoff ? z+scx : z))];
					for (char c = 0; c < 3; c++)
					{
						float vv = *(p + c);
						*(p + c) = ((unsigned char)((vv + ((v[c]-vv)/(11.0f-iFadeLevel)))));
					}
				}
			}
			if (bFillX && scy < height)
			{
				for (int z = 0; z < height - scy; z++)
				{
					for (int zz = 0; zz < width; zz++)
					{
						unsigned char *p = (unsigned char*)&Video[pitch * (z >= yoff ? z+scy : z) + zz];
						for (char c = 0; c < 3; c++)
						{
							float vv = *(p + c);
							*(p + c) = ((unsigned char)((vv + ((v[c]-vv)/(11.0f-iFadeLevel)))));
						}
					}
				}
			}
		}
		for (int y = 0; y < scy && y+yoff < height; y++)
		{
			for (int x = 0; x < scx && x+xoff < width; x++)
			{
				unsigned char *p = (unsigned char*)&Video[pitch * (y+yoff) + x + xoff];
				for (char c = 0; c < 3; c++)
				{
					float v = pScaledCoverVideo[(scx * y + x)*3 + c];
					if (iFadeLevel >= 10) *(p + c) = (unsigned char)v;
					else
					{
						float vv = *(p + c);
						*(p + c) = ((unsigned char)((vv + ((v-vv)/(11-iFadeLevel)))));
					}
				}
			}
		}
	}
	return TRUE;
}

static BOOL opensettings(char *FileName)
{
	char cNumber[10];
	char pcINIFileName[MAX_PATH];
	if (FileName) strcpy(pcINIFileName, FileName); 
	else if (pcXMPlayDir[0]) { strcpy(pcINIFileName, pcXMPlayDir); strcat(pcINIFileName,"\\vis.ini"); }
	else return TRUE;

	if (GetPrivateProfileString("coverart", "albumart_fade", "", cNumber, sizeof(cNumber), pcINIFileName))
		bFadeAlbumart = (atoi(cNumber) > 0);

	if (GetPrivateProfileString("coverart", "albumart_alignment", "", cNumber, sizeof(cNumber), pcINIFileName))
		iAlbumArtAlign = (atoi(cNumber) % 5);

	if (GetPrivateProfileString("coverart", "albumart_cycle", "", cNumber, sizeof(cNumber), pcINIFileName))
		bCycleCover = (atoi(cNumber) > 0);

	if (GetPrivateProfileString("coverart", "albumart_cycleseconds", "", cNumber, sizeof(cNumber), pcINIFileName))
		if (atoi(cNumber)) iCycleCoverSeconds = atoi(cNumber); 
	
	if (bCycleCover) clCycleCoverTime = clock() + CLOCKS_PER_SEC*iCycleCoverSeconds;

	if (GetPrivateProfileString("coverart", "acd_exe", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_Enabled = (strlen(cNumber) > 0);
	if (GetPrivateProfileString("coverart", "acd_enabled", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_Enabled = (atoi(cNumber) > 0);

	if (GetPrivateProfileString("coverart", "acd_savetodisk", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_AutoSave = (atoi(cNumber) > 0);
	if (GetPrivateProfileString("coverart", "acd_autosave", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_AutoSave = (atoi(cNumber) > 0);

	if (GetPrivateProfileString("coverart", "acd_autosavestreamarchive", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_AutoSaveStreamArchive = (atoi(cNumber) > 0);

	#ifdef ACD_USE_AMAZON
	if (GetPrivateProfileString("coverart", "acd_servers", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_ServersSelected = (atoi(cNumber) % (2<<6-1));
	#endif

	if (GetPrivateProfileString("coverart", "acd_imagesize", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_ImageSize = (atoi(cNumber) & 3);

	GetPrivateProfileString("coverart", "acd_savepath", "", ACD_SavePath, sizeof(ACD_SavePath), pcINIFileName);

	#ifndef ACD_DISABLE_LOG
	if (GetPrivateProfileString("coverart", "acd_logactive", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_LogActive = (atoi(cNumber) > 0);
	#endif

	if (GetPrivateProfileString("coverart", "acd_services", "", cNumber, sizeof(cNumber), pcINIFileName))
		ACD_ServiceSelected = (atoi(cNumber) & ((1<<ACD_SERVER_SERVICE_TOTAL)-1));

	if (GetPrivateProfileString("coverart", "acd_searchwith", "", cNumber, sizeof(cNumber), pcINIFileName))
		if (atoi(cNumber) & 7) ACD_ArtistAlbumTitle = (atoi(cNumber) & 7);

	return TRUE;
}

static BOOL savesettings(char *FileName)
{
	char pcINIFileName[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, pcINIFileName);
	if (FileName) strcpy(pcINIFileName, FileName); 
	else if (pcXMPlayDir[0]) { strcpy(pcINIFileName, pcXMPlayDir); strcat(pcINIFileName,"\\vis.ini"); }
	else return TRUE;

	char cNumber[10];
	#define WritePrivateProfileInt(key,value) itoa(value, cNumber, 10); WritePrivateProfileString("coverart", key, cNumber, pcINIFileName);
	#define WritePrivateProfileClear(key) WritePrivateProfileString("coverart", key, NULL, pcINIFileName);

	WritePrivateProfileString("coverart", "albumart_fade", (bFadeAlbumart ? NULL : "0"), pcINIFileName);

	if (iAlbumArtAlign > 0) { WritePrivateProfileInt("albumart_alignment", iAlbumArtAlign); }
	else                    { WritePrivateProfileClear("albumart_alignment"); }

	WritePrivateProfileString("coverart", "albumart_cycle", (bCycleCover ? "1" : NULL), pcINIFileName);

	if (iCycleCoverSeconds > 0 && (iCycleCoverSeconds != 10 || bCycleCover)) { WritePrivateProfileInt("albumart_cycleseconds", iCycleCoverSeconds); }
	else                                                                     { WritePrivateProfileClear("albumart_cycleseconds"); }

	WritePrivateProfileString("coverart", "acd_exe", NULL, pcINIFileName);
	WritePrivateProfileString("coverart", "acd_enabled", (ACD_Enabled ? "1" : NULL), pcINIFileName);

	WritePrivateProfileString("coverart", "acd_savetodisk", NULL, pcINIFileName);

	WritePrivateProfileString("coverart", "acd_autosave", (ACD_AutoSave ? "1" : NULL), pcINIFileName);

	WritePrivateProfileString("coverart", "acd_autosavestreamarchive", (ACD_AutoSaveStreamArchive ? "1" : NULL), pcINIFileName);

	#ifdef ACD_USE_AMAZON
	if (ACD_ServersSelected != 8) { WritePrivateProfileInt("acd_servers", ACD_ServersSelected); }
	else                          { WritePrivateProfileClear("acd_servers"); }
	#endif

	if (ACD_ImageSize > 0) { WritePrivateProfileInt("acd_imagesize", ACD_ImageSize); }
	else                   { WritePrivateProfileClear("acd_imagesize"); }

	WritePrivateProfileString("coverart", "acd_savepath", (ACD_SavePath[0] ? ACD_SavePath : NULL), pcINIFileName);

	#ifndef ACD_DISABLE_LOG
	WritePrivateProfileString("coverart", "acd_logactive", (ACD_LogActive ? "1" : NULL), pcINIFileName);
	#endif

	if (ACD_ServiceSelected > 0) { WritePrivateProfileInt("acd_services", ACD_ServiceSelected); }
	else                         { WritePrivateProfileClear("acd_services"); }
	
	if (ACD_ArtistAlbumTitle != 3) { WritePrivateProfileInt("acd_searchwith", ACD_ArtistAlbumTitle); }
	else                           { WritePrivateProfileClear("acd_searchwith"); }

	return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	int iLen = GetModuleFileName(hinstDLL, pcMyDLLDir, MAX_PATH);
	char *p = pcMyDLLDir+iLen;
	while (p >= pcMyDLLDir && *p != '\\' && *p != '/') p--;
	if (p >= pcMyDLLDir) *p = 0;
	//remove UNC special path heading "\\?\" from the returned path string
	if (*pcMyDLLDir && memcmp(pcMyDLLDir, "\\\\?\\", 4) == 0) for (p = pcMyDLLDir+4; p[-4]; p++) p[-4] = *p;
	return TRUE;
}

static VisInfo plugin = 
{
	1,                     // Version
	"Cover Art",           // Plug-In Name (char*, the name you want displayed when your plug-in is playing)
	0,                     // Options
	&initialize,           // Name of the Initialize function (leave as is)
	&render,               // Name of the Render function (leave as is)
	&savesettings,         // Name of the SaveSettings function (leave as is)
	&opensettings,         // Name of the OpenSettings function (leave as is)
	&deinit,               // deinit
	&clicked,              // clicked
	&receivequeryinterface // queryinterface
};

DLLEXPORT VisInfo* QueryModule(void) { return &plugin; }

unsigned char cNoAlbumCover[2541] = {
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 
	0x00, 0x00, 0x00, 0x8C, 0x00, 0x00, 0x00, 0x8C, 0x04, 0x03, 0x00, 0x00, 0x00, 0x5C, 0xEE, 0x5C, 
	0x0D, 0x00, 0x00, 0x00, 0x2B, 0x74, 0x45, 0x58, 0x74, 0x43, 0x72, 0x65, 0x61, 0x74, 0x69, 0x6F, 
	0x6E, 0x20, 0x54, 0x69, 0x6D, 0x65, 0x00, 0x53, 0x6F, 0x20, 0x32, 0x34, 0x20, 0x46, 0x65, 0x62, 
	0x20, 0x32, 0x30, 0x30, 0x38, 0x20, 0x30, 0x36, 0x3A, 0x33, 0x31, 0x3A, 0x32, 0x35, 0x20, 0x2B, 
	0x30, 0x31, 0x30, 0x30, 0xA4, 0xF4, 0x00, 0x58, 0x00, 0x00, 0x00, 0x07, 0x74, 0x49, 0x4D, 0x45, 
	0x07, 0xD8, 0x02, 0x18, 0x05, 0x30, 0x06, 0xED, 0x3A, 0x49, 0xDF, 0x00, 0x00, 0x00, 0x09, 0x70, 
	0x48, 0x59, 0x73, 0x00, 0x00, 0x0B, 0x12, 0x00, 0x00, 0x0B, 0x12, 0x01, 0xD2, 0xDD, 0x7E, 0xFC, 
	0x00, 0x00, 0x00, 0x04, 0x67, 0x41, 0x4D, 0x41, 0x00, 0x00, 0xB1, 0x8F, 0x0B, 0xFC, 0x61, 0x05, 
	0x00, 0x00, 0x00, 0x30, 0x50, 0x4C, 0x54, 0x45, 0x33, 0x33, 0x33, 0x43, 0x43, 0x43, 0x4D, 0x4D, 
	0x4D, 0x6C, 0x6C, 0x6C, 0x73, 0x73, 0x73, 0x7A, 0x7A, 0x7A, 0x63, 0x63, 0x63, 0x5C, 0x5C, 0x5C, 
	0x54, 0x54, 0x54, 0x3C, 0x3C, 0x3C, 0x2B, 0x2B, 0x2B, 0x23, 0x23, 0x23, 0x1A, 0x1A, 0x1A, 0x11, 
	0x11, 0x11, 0x00, 0x00, 0x00, 0x84, 0x84, 0x84, 0x12, 0x68, 0x02, 0x4D, 0x00, 0x00, 0x09, 0x09, 
	0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0xED, 0xDA, 0x7F, 0x6C, 0x13, 0xD7, 0x1D, 0x00, 0xF0, 0x83, 
	0x4D, 0xA2, 0xD8, 0x46, 0xAA, 0xBD, 0x2E, 0x09, 0x0B, 0x93, 0x6A, 0x47, 0x8A, 0xB3, 0x6A, 0x48, 
	0x25, 0x8E, 0x1D, 0xBA, 0x82, 0x22, 0x6A, 0x3B, 0x49, 0x0B, 0xC8, 0x93, 0x82, 0xC3, 0x16, 0x9A, 
	0x9E, 0xF2, 0xCB, 0x89, 0xDA, 0x21, 0x37, 0x89, 0x63, 0xB2, 0xA9, 0xA8, 0xB2, 0x93, 0x25, 0xD9, 
	0x34, 0xD4, 0xC0, 0xA4, 0x16, 0x87, 0x20, 0x19, 0x95, 0x55, 0x8D, 0x7D, 0x96, 0x4C, 0x81, 0xAD, 
	0xDC, 0x9D, 0xD1, 0xC1, 0xD6, 0x84, 0xDC, 0x0F, 0x03, 0x83, 0x04, 0xEA, 0xBB, 0xE7, 0xFC, 0xB5, 
	0x7F, 0x8A, 0xCF, 0x1B, 0x32, 0x44, 0xA5, 0x25, 0xF2, 0xDE, 0xC5, 0x21, 0x81, 0x41, 0x1C, 0x73, 
	0xCE, 0x5F, 0x53, 0xBE, 0x92, 0x75, 0x71, 0x7E, 0x7C, 0xF2, 0xFC, 0xDE, 0xF7, 0x7D, 0xDF, 0xBB, 
	0xD3, 0x43, 0x90, 0xD5, 0x88, 0x75, 0xC8, 0xFA, 0xA1, 0xBC, 0xC3, 0xAB, 0xD3, 0x22, 0x2F, 0x58, 
	0xED, 0xB2, 0xC2, 0xF2, 0x58, 0x98, 0x21, 0x83, 0xDE, 0x97, 0xE7, 0x2C, 0xC6, 0x3E, 0xFB, 0x3E, 
	0x89, 0x69, 0xCF, 0xF7, 0x43, 0xB9, 0x25, 0xC6, 0xDE, 0xA5, 0xF6, 0xE5, 0x15, 0x6A, 0xFD, 0x3E, 
	0x0F, 0x64, 0x3C, 0x0A, 0x22, 0xAF, 0xF0, 0x17, 0x59, 0xE6, 0x19, 0x0C, 0xE4, 0x15, 0x74, 0x86, 
	0xF9, 0x43, 0x24, 0x9E, 0xCC, 0x23, 0x44, 0xFE, 0xF8, 0x02, 0x93, 0x8F, 0x92, 0x4C, 0xAE, 0x31, 
	0x6B, 0xCC, 0x1A, 0x93, 0x13, 0x23, 0xB2, 0x6C, 0x14, 0x5E, 0x12, 0x2C, 0x9B, 0x17, 0x13, 0x53, 
	0xAB, 0x4F, 0xC0, 0xCB, 0x85, 0xCD, 0x9B, 0x2F, 0xE6, 0xC1, 0x88, 0xEB, 0x94, 0xC4, 0xC6, 0x8B, 
	0xC9, 0x3B, 0x08, 0x11, 0x56, 0xE6, 0xC1, 0x24, 0x5E, 0x3C, 0x3D, 0x8B, 0x9F, 0x48, 0x4E, 0x2A, 
	0xAB, 0x5E, 0xDF, 0x18, 0xCD, 0x83, 0x29, 0xAE, 0xB0, 0xEC, 0x40, 0xA2, 0x93, 0x7F, 0x35, 0x57, 
	0x5F, 0xC8, 0x87, 0x51, 0xA5, 0x86, 0x6A, 0xA8, 0x10, 0xF5, 0x55, 0xB9, 0x91, 0xCA, 0x87, 0xC1, 
	0x52, 0xBA, 0x1E, 0x95, 0x92, 0xBE, 0xDA, 0xE4, 0x8C, 0xC5, 0x93, 0x02, 0x0B, 0xD8, 0xB8, 0x1C, 
	0x46, 0xC4, 0x52, 0xDA, 0xD2, 0x3D, 0xE1, 0xC0, 0x4C, 0x93, 0x93, 0x8F, 0x0B, 0x61, 0x45, 0x00, 
	0x21, 0xE3, 0x32, 0x19, 0x8D, 0x29, 0x36, 0x2C, 0x31, 0x9C, 0x9F, 0x60, 0x05, 0x26, 0x1C, 0x92, 
	0xC3, 0x84, 0x52, 0x3F, 0x53, 0xF7, 0x8F, 0x22, 0x12, 0x83, 0x2B, 0x2F, 0x19, 0xEF, 0x6F, 0x63, 
	0x14, 0x71, 0x19, 0x4C, 0x24, 0xA5, 0x55, 0x17, 0x1E, 0x60, 0x21, 0x13, 0x1B, 0xA6, 0x2D, 0x26, 
	0x47, 0xC5, 0x5B, 0x54, 0x48, 0x1E, 0xA3, 0xD3, 0x39, 0xC0, 0x54, 0x93, 0xF3, 0xDA, 0xE8, 0xE9, 
	0x0E, 0x5D, 0x57, 0x59, 0x2B, 0x86, 0xC9, 0x62, 0x34, 0x7F, 0x6C, 0xEE, 0xFB, 0xE9, 0x8D, 0x4E, 
	0x27, 0x4F, 0x56, 0xAB, 0x4B, 0xEA, 0xB4, 0x05, 0x7B, 0x02, 0x71, 0x19, 0xCC, 0x6C, 0xD1, 0x07, 
	0x05, 0x65, 0xEF, 0x4D, 0x78, 0x9C, 0xC2, 0xF5, 0x5A, 0xCD, 0xBB, 0xDF, 0x34, 0x17, 0xED, 0x0A, 
	0x47, 0xE5, 0x30, 0x85, 0x07, 0x6F, 0x35, 0x0F, 0x7E, 0xA9, 0x6E, 0x03, 0xD3, 0xE5, 0x25, 0x27, 
	0xC1, 0x9F, 0x75, 0x06, 0x5A, 0x16, 0x53, 0xB0, 0x57, 0xA8, 0x2C, 0x52, 0x1F, 0x6D, 0x03, 0x37, 
	0x9B, 0x7A, 0x04, 0x6C, 0xAA, 0xB3, 0x8D, 0x97, 0xF5, 0xA1, 0x0A, 0x1A, 0xB9, 0x1F, 0x7A, 0x36, 
	0x43, 0x66, 0xAA, 0xE9, 0xC3, 0x3B, 0xCA, 0x7F, 0x4B, 0x79, 0x28, 0x8B, 0x99, 0x23, 0x6E, 0x97, 
	0xFB, 0xF2, 0x6F, 0xCD, 0x9C, 0x3F, 0x62, 0x92, 0x5A, 0x33, 0xBD, 0x6D, 0x85, 0xBE, 0xD1, 0x66, 
	0x67, 0x70, 0xEC, 0x9C, 0x07, 0x32, 0x8F, 0x46, 0x8A, 0x92, 0xC9, 0xB0, 0xC4, 0xF5, 0xF2, 0xA3, 
	0x4E, 0x81, 0xCB, 0xE4, 0xCD, 0x5E, 0x3C, 0x2E, 0x83, 0xA9, 0x2B, 0x69, 0x8C, 0xC4, 0x22, 0x0E, 
	0xAD, 0x33, 0xA6, 0x82, 0x59, 0xEC, 0xCD, 0x96, 0xC5, 0xDA, 0x2C, 0xF5, 0xA6, 0xD5, 0xF0, 0x9B, 
	0x50, 0x82, 0xFC, 0x87, 0xC9, 0x70, 0x4D, 0xC1, 0xCF, 0xCF, 0xA9, 0x58, 0xE4, 0xF9, 0x19, 0x31, 
	0x78, 0x36, 0x45, 0x84, 0x92, 0x0C, 0xEB, 0x6C, 0xA0, 0x70, 0xEC, 0x3A, 0x9C, 0xE1, 0x42, 0x20, 
	0xFE, 0xFC, 0x4C, 0x92, 0x26, 0x59, 0x22, 0x9A, 0x14, 0x08, 0x8E, 0x0D, 0x02, 0x9C, 0x05, 0x40, 
	0x60, 0x96, 0x59, 0x16, 0xB3, 0x33, 0x89, 0x40, 0x00, 0xF6, 0x85, 0x48, 0x07, 0xF1, 0x48, 0x52, 
	0xC0, 0x49, 0x16, 0xCF, 0x52, 0xFD, 0xB2, 0x30, 0x49, 0x01, 0x48, 0x7F, 0x27, 0x02, 0xE9, 0x22, 
	0xED, 0x7F, 0xB3, 0xD4, 0x62, 0xED, 0xEA, 0xAC, 0xE1, 0x6B, 0xCC, 0x1A, 0x93, 0x09, 0x98, 0x75, 
	0xF3, 0x2F, 0xB9, 0x8C, 0xC8, 0x0A, 0x52, 0xBA, 0xD2, 0xC1, 0x00, 0x07, 0x5F, 0x51, 0xB9, 0x0C, 
	0xAD, 0xF0, 0xC3, 0xA9, 0x94, 0x20, 0x38, 0x06, 0x87, 0x2F, 0x52, 0x26, 0x93, 0x50, 0xB0, 0x2C, 
	0x1E, 0x4D, 0xC6, 0x88, 0xAA, 0xD7, 0x69, 0xB6, 0x6A, 0x07, 0x9E, 0xC3, 0xFD, 0xE3, 0xB3, 0x18, 
	0x0A, 0xFB, 0x68, 0x96, 0x09, 0x25, 0x63, 0xE3, 0x26, 0x93, 0x70, 0xDB, 0x54, 0xC9, 0xC8, 0x63, 
	0x44, 0xFF, 0x84, 0xB9, 0x62, 0x27, 0x11, 0xE7, 0xA7, 0x9A, 0x5A, 0xE1, 0xBA, 0xD2, 0x26, 0xC8, 
	0x63, 0x12, 0xCA, 0xF3, 0xCD, 0xFA, 0x5F, 0xD3, 0x71, 0x61, 0xA6, 0x09, 0xAE, 0x72, 0x9D, 0x1F, 
	0x02, 0x99, 0x8C, 0xAA, 0xDF, 0xA3, 0x31, 0xF1, 0x51, 0x61, 0xA6, 0xB3, 0x67, 0x9E, 0x91, 0xCA, 
	0x0C, 0x1C, 0x78, 0x20, 0x55, 0x9C, 0xB8, 0x08, 0x04, 0x4E, 0xBA, 0x70, 0x4F, 0x6C, 0x04, 0x9F, 
	0xC1, 0xF0, 0x91, 0x3A, 0xAD, 0xBA, 0x87, 0x8D, 0x2C, 0x30, 0x3D, 0x80, 0x65, 0xC8, 0x38, 0x1F, 
	0x64, 0x60, 0x1D, 0x25, 0x71, 0x92, 0x21, 0xC2, 0x0A, 0x02, 0xC7, 0x68, 0x05, 0x8E, 0x60, 0x59, 
	0x19, 0x2A, 0xBC, 0x5F, 0xA7, 0xED, 0xDF, 0x12, 0x5A, 0x60, 0x06, 0x60, 0x1B, 0x18, 0x02, 0xE7, 
	0x00, 0x1D, 0xC0, 0x80, 0xE0, 0xC7, 0x38, 0xB8, 0x23, 0x15, 0x18, 0x12, 0x6E, 0x04, 0xFD, 0xD1, 
	0xC7, 0x18, 0xF3, 0xFF, 0x32, 0xF4, 0x78, 0xAD, 0x47, 0x53, 0xB2, 0x93, 0x5C, 0x60, 0x8A, 0x3E, 
	0x47, 0x7F, 0xC5, 0x85, 0x55, 0xE7, 0xEE, 0x91, 0x3E, 0xC6, 0xB6, 0x5B, 0xCA, 0x80, 0xDB, 0xE8, 
	0xFE, 0x88, 0xF0, 0xFB, 0xF4, 0xAB, 0xD4, 0x68, 0x16, 0x46, 0x0C, 0x7E, 0xD5, 0xF1, 0xEE, 0x06, 
	0xFD, 0xDB, 0x34, 0x9B, 0x61, 0x8E, 0x0E, 0x38, 0x5A, 0xDE, 0xE4, 0x23, 0x2D, 0x2D, 0x07, 0x03, 
	0xD3, 0x06, 0x13, 0x98, 0x36, 0x9B, 0x2D, 0x06, 0xEB, 0x83, 0xF3, 0x76, 0x2B, 0xFA, 0xF3, 0xA5, 
	0xFD, 0x64, 0x86, 0xB1, 0x3E, 0xC6, 0x24, 0x54, 0x3F, 0x76, 0x1F, 0x9C, 0xEE, 0x7D, 0x83, 0x26, 
	0x33, 0x8C, 0x4F, 0xE3, 0xD5, 0xF4, 0x6F, 0x02, 0xF5, 0x70, 0x19, 0x9E, 0x69, 0x86, 0x43, 0x57, 
	0x3E, 0xD8, 0xDE, 0x31, 0x68, 0x34, 0x1B, 0x0E, 0xB5, 0x7E, 0x37, 0x16, 0x5D, 0xBE, 0x35, 0x89, 
	0x90, 0xA1, 0x6F, 0xEB, 0xED, 0xF2, 0x56, 0x3E, 0xB8, 0xD0, 0x9A, 0x92, 0x6A, 0x6D, 0xE1, 0x4E, 
	0x50, 0x23, 0x31, 0x52, 0x06, 0x94, 0x6B, 0x5C, 0xF5, 0xDE, 0xC1, 0x16, 0xBB, 0xA9, 0x7B, 0xFF, 
	0x64, 0x74, 0xF9, 0xD6, 0x50, 0x44, 0x7D, 0x4F, 0xE8, 0xEB, 0xDA, 0x1E, 0x72, 0x24, 0xC3, 0x68, 
	0xDE, 0xFB, 0xA6, 0x59, 0xB3, 0x0D, 0x54, 0x2E, 0x32, 0xFA, 0xC3, 0xB7, 0x3B, 0xF4, 0xED, 0xA0, 
	0xDB, 0x61, 0xA7, 0xB2, 0x31, 0x97, 0x6B, 0x5B, 0x81, 0x50, 0x53, 0xB2, 0x75, 0xA1, 0x35, 0x25, 
	0x9B, 0xC0, 0xC7, 0xDA, 0x0E, 0xA1, 0x7D, 0x91, 0x71, 0x45, 0xF8, 0xFA, 0x81, 0x03, 0xE0, 0xBC, 
	0x09, 0xA5, 0x97, 0xEF, 0x1B, 0x31, 0x30, 0x55, 0xDE, 0x05, 0xC0, 0x50, 0x41, 0xE3, 0x42, 0x17, 
	0xF7, 0x08, 0x91, 0x29, 0xCF, 0x21, 0xF6, 0xD0, 0x22, 0xE3, 0x04, 0x20, 0xD5, 0x07, 0xDF, 0x54, 
	0xA3, 0xB1, 0xE5, 0x99, 0x84, 0xF2, 0x2F, 0xCD, 0xFA, 0xB6, 0x7A, 0x4F, 0x51, 0xC3, 0xC2, 0x80, 
	0xBB, 0x12, 0xD8, 0xBF, 0x3A, 0x0F, 0x31, 0x4B, 0x4C, 0x95, 0xC0, 0xA5, 0xFA, 0xE7, 0x88, 0x99, 
	0xFA, 0x6C, 0xCC, 0x1D, 0x95, 0xCB, 0xA3, 0x19, 0xE8, 0x50, 0x1F, 0xDB, 0x3E, 0x9F, 0xC1, 0x4B, 
	0xAD, 0x29, 0x6C, 0x04, 0x0B, 0x0C, 0x1F, 0x81, 0x4C, 0x20, 0x3B, 0x73, 0x2D, 0x54, 0xA7, 0x73, 
	0x3B, 0x1C, 0x5D, 0x6A, 0x17, 0xB8, 0xB9, 0xD4, 0x37, 0xED, 0x42, 0x7B, 0xD1, 0x6E, 0x70, 0xCE, 
	0x9B, 0x33, 0x33, 0x79, 0x61, 0xBF, 0xDE, 0x96, 0x4E, 0xD7, 0x6B, 0xFB, 0x36, 0xDD, 0x78, 0x7C, 
	0xA4, 0xDC, 0xDA, 0x21, 0x30, 0xA4, 0x33, 0x80, 0x99, 0x1C, 0x99, 0xB3, 0xB5, 0xDD, 0x70, 0x16, 
	0x09, 0xCD, 0xA5, 0x47, 0xC6, 0x97, 0xF2, 0x66, 0x2F, 0xF8, 0x91, 0xA7, 0xC8, 0xE2, 0x2D, 0xDA, 
	0xB5, 0xC8, 0x20, 0x90, 0xC9, 0x32, 0x52, 0xD4, 0xD5, 0xED, 0xB5, 0x02, 0xC9, 0x72, 0xA6, 0xD2, 
	0x83, 0xD3, 0x9D, 0xD2, 0xAE, 0xBA, 0xCC, 0xEB, 0xD5, 0xB8, 0x42, 0xE4, 0xCD, 0x0E, 0xED, 0x90, 
	0x6E, 0x60, 0x2F, 0x3B, 0xB3, 0xBD, 0x2A, 0x16, 0xBA, 0xE7, 0x7A, 0xE8, 0xBB, 0x5A, 0x87, 0x66, 
	0xC9, 0x9B, 0xC9, 0xF3, 0x96, 0x03, 0x61, 0x4E, 0xC0, 0x5E, 0x32, 0xEC, 0xBC, 0x65, 0xAA, 0x14, 
	0x6E, 0x99, 0x4C, 0x66, 0x47, 0xCB, 0x2F, 0x79, 0x05, 0x0B, 0xB7, 0x7E, 0x2D, 0x16, 0x36, 0x70, 
	0xC3, 0x6C, 0x9E, 0x1C, 0x35, 0x5A, 0x77, 0xA8, 0xFF, 0x6E, 0x4F, 0x65, 0xC9, 0xE2, 0xC9, 0x00, 
	0xBA, 0x45, 0x11, 0x17, 0x03, 0xB7, 0xEE, 0x87, 0x84, 0xAA, 0x3D, 0x34, 0xDB, 0x7A, 0xF0, 0x0A, 
	0x9C, 0xE1, 0x38, 0x11, 0xF9, 0xBA, 0x2D, 0x5D, 0x7F, 0x92, 0x41, 0xE0, 0x2C, 0x3F, 0x53, 0x7C, 
	0xEE, 0x1E, 0xA1, 0x23, 0x6C, 0xBB, 0xC7, 0x96, 0x67, 0x62, 0x23, 0xAC, 0x74, 0x43, 0x49, 0x93, 
	0xB0, 0xA6, 0x70, 0x70, 0x97, 0xC6, 0xC1, 0x62, 0x07, 0xBF, 0x14, 0x79, 0xB8, 0xF5, 0x03, 0x2C, 
	0xC9, 0x07, 0x01, 0xE3, 0x47, 0x48, 0x36, 0x8C, 0x28, 0x58, 0x7A, 0x24, 0x4B, 0x16, 0x8F, 0x21, 
	0xC3, 0x51, 0x69, 0xD7, 0x87, 0x07, 0x19, 0x02, 0xAE, 0x52, 0x44, 0x58, 0x29, 0x55, 0xBF, 0xA4, 
	0x48, 0x90, 0xB0, 0x7A, 0xC5, 0x45, 0x9C, 0xC0, 0x23, 0xBC, 0x22, 0xA0, 0x00, 0x63, 0x88, 0x6F, 
	0xE9, 0x86, 0xF1, 0xE9, 0x39, 0x25, 0xB0, 0x9C, 0x74, 0xC9, 0x3C, 0xEB, 0x4C, 0x2E, 0x94, 0xDE, 
	0x47, 0xDF, 0x48, 0x2E, 0xD6, 0x63, 0xF8, 0x7B, 0x8F, 0x15, 0xE3, 0xA7, 0x19, 0x59, 0xB1, 0xC6, 
	0xAC, 0x31, 0xAB, 0xC9, 0x88, 0xD2, 0xDD, 0x53, 0xEE, 0x8F, 0xE9, 0x97, 0x61, 0x44, 0x06, 0x09, 
	0x12, 0x7E, 0x2C, 0x67, 0x67, 0x19, 0x86, 0x56, 0x90, 0x70, 0x03, 0xC3, 0x60, 0xF9, 0x31, 0x09, 
	0x84, 0xDB, 0x60, 0x4B, 0xD7, 0x46, 0xE0, 0x46, 0x32, 0x1F, 0xE6, 0x82, 0xEA, 0x07, 0xB0, 0xE6, 
	0x59, 0x67, 0xF9, 0x50, 0x3E, 0x4C, 0x02, 0xB9, 0x5C, 0x59, 0xE6, 0xEE, 0xD5, 0xDB, 0xFE, 0xA4, 
	0xCA, 0x87, 0x89, 0xA9, 0x0A, 0x75, 0xAD, 0xE8, 0x3E, 0x83, 0xED, 0x03, 0x55, 0xE6, 0x06, 0x73, 
	0xBE, 0xD2, 0x08, 0xB0, 0xC8, 0x64, 0xDE, 0x8A, 0xAC, 0x14, 0xF1, 0x95, 0x18, 0x2A, 0xDC, 0x59, 
	0x5A, 0x17, 0x12, 0x3E, 0x4E, 0xD1, 0x3F, 0xC1, 0x31, 0x16, 0x8F, 0x00, 0x9C, 0x08, 0x44, 0x13, 
	0xFE, 0x80, 0x82, 0xC5, 0x83, 0x44, 0x80, 0x8C, 0xC7, 0x86, 0x37, 0x6E, 0xF6, 0xF9, 0x42, 0x2B, 
	0x30, 0xA2, 0x7F, 0x5C, 0xD7, 0x76, 0x58, 0xFA, 0x87, 0xA7, 0x5E, 0x26, 0xA5, 0x4A, 0x1C, 0x08, 
	0x72, 0x34, 0x46, 0x63, 0xB0, 0x00, 0x4B, 0xE3, 0x87, 0x13, 0x1B, 0x31, 0x96, 0x20, 0xC3, 0x23, 
	0xF1, 0xEC, 0x4C, 0xE2, 0xC4, 0x86, 0x52, 0xBB, 0x10, 0xA4, 0x11, 0xBF, 0x66, 0xF4, 0xD2, 0xFC, 
	0xA3, 0x00, 0x1A, 0x3D, 0xE2, 0x57, 0x5C, 0xDF, 0x47, 0x92, 0x70, 0x79, 0xD8, 0x26, 0x9C, 0x7A, 
	0xE5, 0x5C, 0x8A, 0x71, 0x36, 0xAC, 0x8B, 0xAE, 0xC4, 0x0C, 0xF5, 0xDF, 0xA5, 0x22, 0x22, 0xCB, 
	0x0C, 0xD3, 0x06, 0x38, 0x60, 0x0D, 0xC2, 0xB4, 0xF1, 0x3F, 0x97, 0x8A, 0x3F, 0xB5, 0xBE, 0xC3, 
	0x57, 0x18, 0x2A, 0xD0, 0x86, 0xC9, 0xD1, 0x36, 0xD4, 0x8E, 0xDE, 0x3F, 0xB3, 0x22, 0xD3, 0xDC, 
	0xF3, 0xBD, 0xF4, 0x44, 0x2D, 0x81, 0x7D, 0xE6, 0x2D, 0xD3, 0x0D, 0xD8, 0x42, 0xE3, 0x25, 0xB3, 
	0xD7, 0x46, 0xCB, 0xBA, 0xDF, 0x39, 0xDB, 0xEB, 0xEE, 0x6D, 0x41, 0x31, 0xAE, 0xBA, 0xC5, 0x6C, 
	0x5B, 0x99, 0x51, 0x95, 0x3B, 0xE7, 0x08, 0xF8, 0xC9, 0x63, 0x58, 0xA7, 0xC6, 0x58, 0xAE, 0x76, 
	0xED, 0xB9, 0xAC, 0x47, 0x05, 0xAE, 0x6B, 0xF0, 0x48, 0xAF, 0xDE, 0xD1, 0x3E, 0x64, 0x6B, 0x04, 
	0x35, 0x83, 0xC6, 0xF4, 0xCA, 0x1F, 0x4A, 0x62, 0xA4, 0xBB, 0x28, 0xEA, 0x8C, 0xA7, 0xE4, 0xAD, 
	0x6B, 0xDA, 0x81, 0xD7, 0xA8, 0x21, 0xEB, 0xA6, 0x58, 0xD7, 0xE0, 0x46, 0xAF, 0xFB, 0x77, 0xF4, 
	0x50, 0xDB, 0x5D, 0x50, 0xD3, 0x8D, 0xBE, 0x4F, 0xBD, 0xB8, 0x52, 0x17, 0x3F, 0x6A, 0x0D, 0x75, 
	0x45, 0x67, 0x3C, 0xC9, 0x1D, 0x2B, 0x6D, 0x17, 0xBC, 0x15, 0x87, 0xAF, 0x0F, 0x75, 0x7C, 0xA1, 
	0x73, 0x63, 0xEC, 0x97, 0xAE, 0x3A, 0x50, 0x63, 0x7C, 0xC0, 0xAF, 0x38, 0xE0, 0x8F, 0xFA, 0x46, 
	0xC0, 0xC7, 0x0B, 0x51, 0x01, 0x9B, 0x28, 0xA8, 0x00, 0xC7, 0xBB, 0xF7, 0x5E, 0x19, 0x78, 0x6D, 
	0x42, 0xAB, 0x77, 0x38, 0xBC, 0xFD, 0x56, 0x50, 0x63, 0x9B, 0x0B, 0x63, 0x4F, 0xA7, 0x9F, 0xE5, 
	0x49, 0xA6, 0x78, 0x7E, 0xA4, 0x04, 0xBF, 0x72, 0xBA, 0x30, 0x7D, 0x47, 0x39, 0x5D, 0x60, 0x07, 
	0x13, 0xFA, 0x57, 0x3F, 0xEF, 0xDE, 0x33, 0xAE, 0xD5, 0xB8, 0x4D, 0xBD, 0x2E, 0x0B, 0xA8, 0x41, 
	0xE7, 0x94, 0x4F, 0x14, 0x91, 0x67, 0x32, 0x23, 0x9F, 0x94, 0xDA, 0x79, 0x85, 0x5F, 0xC9, 0x5D, 
	0xC9, 0xB4, 0xC6, 0x2E, 0xFC, 0x53, 0xD7, 0x75, 0xDC, 0xB8, 0x75, 0x5A, 0xAB, 0x6F, 0x31, 0x38, 
	0x5C, 0xBB, 0x20, 0xF3, 0xF0, 0xC9, 0xD9, 0xF6, 0x2C, 0x26, 0x93, 0xC5, 0x0C, 0x41, 0x9B, 0x4E, 
	0x69, 0x8C, 0x27, 0xC1, 0xB1, 0x92, 0x54, 0x8C, 0xE8, 0xD4, 0x15, 0xA2, 0xDC, 0x2D, 0x2F, 0xEC, 
	0x1B, 0xF6, 0x6F, 0x6F, 0xE6, 0xC6, 0xC0, 0x11, 0x92, 0xE6, 0x14, 0xDF, 0x57, 0xF9, 0x5B, 0x75, 
	0x1F, 0x1C, 0xA9, 0xFE, 0xBB, 0x97, 0x46, 0xBD, 0xEA, 0x92, 0x59, 0x7E, 0xB2, 0x6B, 0xE0, 0x7D, 
	0xF6, 0x8C, 0xCB, 0x0E, 0xAA, 0xD1, 0x87, 0xA3, 0x2B, 0x33, 0x31, 0xD5, 0xA0, 0xAE, 0x35, 0x5D, 
	0xD7, 0xD5, 0xF7, 0xB6, 0xB7, 0xC8, 0xB8, 0x5D, 0xE3, 0x7C, 0x78, 0xAA, 0xF8, 0x05, 0x75, 0xCF, 
	0x77, 0x93, 0xAA, 0x0E, 0xBD, 0xC3, 0xD0, 0x9E, 0x33, 0x93, 0x18, 0x9E, 0xA8, 0x1C, 0x74, 0x74, 
	0x0D, 0x56, 0x1D, 0xF9, 0xCC, 0x3B, 0xE8, 0x1D, 0x40, 0x83, 0x8A, 0xC0, 0x84, 0xAE, 0xAA, 0x71, 
	0xEC, 0xC4, 0xD9, 0x76, 0xB7, 0xC9, 0x6D, 0x7D, 0x00, 0x99, 0xEF, 0xB7, 0xAC, 0xCC, 0xC0, 0xEA, 
	0xF7, 0x92, 0xD9, 0x60, 0xB0, 0x7E, 0x2B, 0xD0, 0x06, 0x93, 0xC1, 0xD6, 0x40, 0x85, 0xA8, 0x49, 
	0x03, 0x7A, 0x42, 0xE1, 0x27, 0xDA, 0x2C, 0x16, 0xFB, 0x7D, 0x15, 0xD9, 0x86, 0xFE, 0x22, 0x17, 
	0xE6, 0xCE, 0x30, 0xF9, 0x89, 0x2D, 0xFD, 0x06, 0x39, 0xA6, 0xBC, 0x64, 0x4C, 0x4B, 0x8F, 0xF3, 
	0x63, 0xD8, 0x47, 0xDF, 0x52, 0xA1, 0x98, 0x92, 0x72, 0xA5, 0xEB, 0xB7, 0x5C, 0x78, 0xF9, 0xF4, 
	0xBD, 0xB1, 0x5C, 0x98, 0x24, 0x3D, 0x1C, 0x64, 0x59, 0xC6, 0x1F, 0xF4, 0x4B, 0x97, 0x30, 0x06, 
	0xCB, 0x2A, 0xC9, 0x20, 0x51, 0x31, 0xAC, 0x20, 0xE1, 0x77, 0x87, 0x8F, 0x8E, 0xE0, 0xBE, 0x8B, 
	0xB9, 0x30, 0x22, 0xED, 0x1B, 0x46, 0x7C, 0xCA, 0xB8, 0x10, 0x1E, 0x0E, 0x20, 0x52, 0xBE, 0x52, 
	0x3E, 0x1F, 0x1C, 0xE2, 0xC4, 0x98, 0x0F, 0xF1, 0x8D, 0x70, 0x94, 0x5A, 0x5D, 0x1C, 0xCF, 0x85, 
	0xC9, 0x94, 0xDB, 0xA8, 0xB4, 0x13, 0xCC, 0x14, 0x5D, 0x31, 0x73, 0x91, 0xDE, 0x72, 0x70, 0x31, 
	0x24, 0xA2, 0xC9, 0x9C, 0x98, 0xE7, 0x8C, 0x35, 0x66, 0x8D, 0xF9, 0x3F, 0x62, 0xF2, 0x3D, 0x1C, 
	0x32, 0xCF, 0xE4, 0x7F, 0x54, 0x25, 0xC3, 0xE4, 0x7D, 0x70, 0x06, 0x32, 0xEB, 0xAD, 0xAB, 0x70, 
	0x8C, 0x47, 0x62, 0x56, 0xE5, 0x50, 0xD1, 0x7A, 0xEB, 0x13, 0x87, 0x9E, 0xE4, 0x84, 0x74, 0xC4, 
	0x69, 0x7D, 0xC5, 0x72, 0x3F, 0x85, 0xEB, 0x43, 0x6E, 0x21, 0x1D, 0xB8, 0x5A, 0xA7, 0x5B, 0x85, 
	0x50, 0xAF, 0xCA, 0x59, 0x34, 0x64, 0xDD, 0x7F, 0x01, 0x25, 0x95, 0x50, 0xB2, 0xB3, 0x42, 0x49, 
	0xD3, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

unsigned char cAlbumCoverDownloading[526] = {
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 
	0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x5F, 0x04, 0x03, 0x00, 0x00, 0x00, 0x7C, 0x62, 0x06, 
	0xC6, 0x00, 0x00, 0x00, 0x2B, 0x74, 0x45, 0x58, 0x74, 0x43, 0x72, 0x65, 0x61, 0x74, 0x69, 0x6F, 
	0x6E, 0x20, 0x54, 0x69, 0x6D, 0x65, 0x00, 0x4D, 0x69, 0x20, 0x32, 0x30, 0x20, 0x46, 0x65, 0x62, 
	0x20, 0x32, 0x30, 0x30, 0x38, 0x20, 0x30, 0x38, 0x3A, 0x31, 0x38, 0x3A, 0x35, 0x35, 0x20, 0x2B, 
	0x30, 0x31, 0x30, 0x30, 0xB7, 0xDA, 0x4B, 0x71, 0x00, 0x00, 0x00, 0x07, 0x74, 0x49, 0x4D, 0x45, 
	0x07, 0xD8, 0x03, 0x06, 0x14, 0x1C, 0x01, 0xDA, 0x2D, 0x77, 0x89, 0x00, 0x00, 0x00, 0x09, 0x70, 
	0x48, 0x59, 0x73, 0x00, 0x00, 0x0A, 0xF0, 0x00, 0x00, 0x0A, 0xF0, 0x01, 0x42, 0xAC, 0x34, 0x98, 
	0x00, 0x00, 0x00, 0x04, 0x67, 0x41, 0x4D, 0x41, 0x00, 0x00, 0xB1, 0x8F, 0x0B, 0xFC, 0x61, 0x05, 
	0x00, 0x00, 0x00, 0x30, 0x50, 0x4C, 0x54, 0x45, 0x00, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x81, 0x81, 
	0x81, 0x8B, 0x8B, 0x8B, 0x7B, 0x7B, 0x7B, 0x44, 0x44, 0x44, 0x4B, 0x4B, 0x4B, 0x6B, 0x6B, 0x6B, 
	0x53, 0x53, 0x53, 0x0C, 0x0C, 0x0C, 0x3A, 0x3A, 0x3A, 0x72, 0x72, 0x72, 0x28, 0x28, 0x28, 0x34, 
	0x34, 0x34, 0x13, 0x13, 0x13, 0x63, 0x63, 0x63, 0x93, 0x1D, 0xF6, 0x54, 0x00, 0x00, 0x01, 0x2A, 
	0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x60, 0x18, 0x05, 0xA3, 0x60, 0x14, 0x8C, 0x82, 0x51, 
	0x30, 0x0A, 0x46, 0xC1, 0x48, 0x06, 0xFC, 0xC6, 0x26, 0x9D, 0x28, 0x02, 0xEC, 0x05, 0xC8, 0xEC, 
	0xDD, 0x18, 0x1A, 0x98, 0x8D, 0x8D, 0x8D, 0x26, 0xA0, 0x08, 0x18, 0x20, 0xB3, 0xB1, 0x68, 0x30, 
	0xE0, 0x29, 0x6E, 0xC0, 0xAD, 0x81, 0x01, 0x8B, 0x06, 0x86, 0xC7, 0x0A, 0x5C, 0xCE, 0x5E, 0xDC, 
	0x1B, 0xF8, 0x3F, 0xF0, 0x6F, 0x70, 0xD9, 0xAC, 0xC3, 0x6C, 0x00, 0xE4, 0x31, 0x70, 0x39, 0xBB, 
	0x38, 0x70, 0x39, 0x6F, 0x36, 0x28, 0x67, 0xF1, 0x36, 0x3A, 0xC0, 0xE5, 0xBC, 0xC9, 0x01, 0x59, 
	0x03, 0xB3, 0xC1, 0xE2, 0x86, 0xE6, 0x25, 0x76, 0x8F, 0xED, 0x1E, 0x3B, 0x18, 0x9F, 0x31, 0x02, 
	0xF3, 0x16, 0x5C, 0x6E, 0x68, 0x06, 0xD1, 0x06, 0xC6, 0xCC, 0x36, 0x87, 0x15, 0x40, 0x1C, 0x54, 
	0x0D, 0xCE, 0x0F, 0x1E, 0xBB, 0x99, 0x5E, 0xB6, 0x5A, 0x9C, 0x60, 0xCC, 0x60, 0x0C, 0xE6, 0x39, 
	0x38, 0x4F, 0x98, 0x0C, 0xA4, 0x27, 0x03, 0x35, 0x18, 0x00, 0x05, 0x80, 0x1C, 0x54, 0x27, 0x19, 
	0x33, 0x30, 0xDB, 0x99, 0xB7, 0x3B, 0x7E, 0x7E, 0x00, 0xD6, 0x00, 0xE4, 0x21, 0x30, 0x48, 0x83, 
	0x31, 0xB2, 0x67, 0x80, 0x9E, 0xDE, 0xDC, 0x00, 0x32, 0x73, 0xB3, 0x5B, 0x93, 0x35, 0x03, 0xC2, 
	0x86, 0x07, 0x8F, 0x91, 0x6C, 0x00, 0x72, 0x50, 0x83, 0x15, 0xE4, 0xEA, 0xC5, 0x36, 0x87, 0xAD, 
	0x20, 0x1A, 0xC0, 0x3C, 0xA0, 0xB3, 0x2F, 0x83, 0xFD, 0x00, 0xD4, 0x10, 0x0C, 0x64, 0x30, 0x6F, 
	0x00, 0x41, 0x50, 0xDC, 0x18, 0x1B, 0x75, 0x32, 0x80, 0xC2, 0x65, 0xCA, 0x06, 0x6E, 0x4F, 0x86, 
	0x72, 0x86, 0x72, 0xF6, 0x02, 0x70, 0x28, 0x29, 0x6F, 0x01, 0x86, 0xD2, 0x16, 0x07, 0x20, 0x97, 
	0xBD, 0x80, 0x55, 0xB9, 0xD8, 0x80, 0x7B, 0x01, 0x08, 0x12, 0x9D, 0x1E, 0x98, 0x15, 0x48, 0x4C, 
	0x40, 0xC9, 0x09, 0x24, 0x29, 0x17, 0x32, 0x76, 0x25, 0xD1, 0x82, 0x51, 0x30, 0x0A, 0x46, 0xC1, 
	0x28, 0x18, 0x05, 0xA3, 0x60, 0x14, 0x0C, 0x00, 0x00, 0x00, 0x04, 0xA2, 0x63, 0x2D, 0xC4, 0x3B, 
	0x53, 0xC2, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};
