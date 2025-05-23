/*******************************************************************************************
MQ2TakeADump.dll: MacroQuest2's extension DLL for EverQuest
Copyright (C) 2018 Maudigan

This program is free software; you can redistribute it and/or modify it under the terms of
the GNU General Public License, version 2, as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.


▄▄▄█████▓ ▄▄▄       ██ ▄█▀▓█████     ▄▄▄         ▓█████▄  █    ██  ███▄ ▄███▓ ██▓███
▓  ██▒ ▓▒▒████▄     ██▄█▒ ▓█   ▀    ▒████▄       ▒██▀ ██▌ ██  ▓██▒▓██▒▀█▀ ██▒▓██░  ██▒
▒ ▓██░ ▒░▒██  ▀█▄  ▓███▄░ ▒███      ▒██  ▀█▄     ░██   █▌▓██  ▒██░▓██    ▓██░▓██░ ██▓▒
░ ▓██▓ ░ ░██▄▄▄▄██ ▓██ █▄ ▒▓█  ▄    ░██▄▄▄▄██    ░▓█▄   ▌▓▓█  ░██░▒██    ▒██ ▒██▄█▓▒ ▒
▒██▒ ░  ▓█   ▓██▒▒██▒ █▄░▒████▒    ▓█   ▓██▒   ░▒████▓ ▒▒█████▓ ▒██▒   ░██▒▒██▒ ░  ░
▒ ░░    ▒▒   ▓▒█░▒ ▒▒ ▓▒░░ ▒░ ░    ▒▒   ▓▒█░    ▒▒▓  ▒ ░▒▓▒ ▒ ▒ ░ ▒░   ░  ░▒▓▒░ ░  ░
░      ▒   ▒▒ ░░ ░▒ ▒░ ░ ░  ░     ▒   ▒▒ ░    ░ ▒  ▒ ░░▒░ ░ ░ ░  ░      ░░▒ ░
░        ░   ▒   ░ ░░ ░    ░        ░   ▒       ░ ░  ░  ░░░ ░ ░ ░      ░   ░░
░  ░░  ░      ░  ░         ░  ░      ░       ░            ░
░


DESCRIPTION:
-----------------------------------------------------------------------------------------
This plugin will allow you to dump EQ information out to CSV files, such as doors,
groundspawns, objects, NPCs, current zone and zonepoints. To take a dump use the
/takeadump command. You can optionally send a parameter to limit the dump to just
the specific data you are after, e.g. "/takeadump door". For the list of parameters
try "/takeadump help".

When run it will drop all the varios data dumps as CSV files in the macroquest
directory with the zone name, type, and timestamp in the filename.

The first row of the CSV is a description of the field (taken from the MQ objects).
The second row is a description of the datatype. Numeric and float data shows up as
plain numbers, boolean show up as true or false literals, and strings show up
as text surrounded by double quotes (the quotes may not display in excel but they will
in notepad).

"/takedump target" is a special case. If you use this you have to have a target. It
will dump that targets coordinates until it dies, you zone or you lose the target.
One row will dump out every time your targets heading changes.


REVISION HISTORY
Date		Author			Description
-----------------------------------------------------------------------------------------
20180922	Maudigan		Initial revision
20180929	Maudigan		Added path recording
20181006    Maudigan        cleaned up output file name
							put output files into a "Dumps" folder
							stoped target output for "/takeadump all"; must request it now
							added some missing elements namely FindBits and Level to NPC
20181008	Maudigan		Split the groundspawn and objects into seperate commands/files
20181013	Maudigan		Spawn structure updated for patch
20181104    Maudigan        Fixed some changed data types for the new client
20190209    Maudigan        added "/takeadump merchant" to dump the item IDs in MerchantWnd
20190309	Maudigan		Had a misallignment in the spawninfo struct which caused a
							a series of corrupted dumps. Going to do some minor verification
							by validating an element towards the bottom of each data struct.
							This assumes if the last element is good, then all of them are
							good. This may need some tweaks. You should probably still spot
							check your dumps for accuracy following a patch.
20190511    Maudigan        repairs after patch
20190803	Maudigan		updated for 20190731
20190810	Maudigan		Added a timer to help calculate pause when pathing
							${TAD.Seconds}, ${TAD.SecondsReset}, ${TAD.Milliseconds},
							${TAD.MillisecondsReset} and
							/takeadump tstart|tpause|treset|tcontinue
20190824	Maudigan		added markup to merchant window
20200809    Maudigan        update for 20200722
Version 1.1.2
********************************************************************************************/

#include <mq/Plugin.h>
#include <time.h>
#include "fmt/format.h"

PreSetup("MQ2TakeADump");
PLUGIN_VERSION(1.13);

//passed to the dump functions to print a comma or new line 
enum { TAD_NONE, TAD_COMMA, TAD_EOL };

//file for target output
FILE *fTargetOut = NULL;
FLOAT LastHeading = 0;
FLOAT FirstHeading = 0;
FLOAT FirstX = 0;
FLOAT FirstY = 0;
FLOAT FirstZ = 0;
BOOL MovedFromStart = false;
long TimerStart = 0;
long TimerPaused = 0;
class MQ2TADType *pTADType = 0;

//creates and opens the dump files
BOOL fOpenDump(FILE **fOut, PCHAR szType)
{
	struct tm newtime;
	__time32_t aclock;
	CHAR szTime[80] = { 0 };
	CHAR szFilename[MAX_STRING] = { 0 };
	CHAR szDir[MAX_STRING] = { 0 };
	errno_t errNum;
	PZONEINFO pMyZone = (PZONEINFO)pZoneInfo;

	//get local time stamp
	_time32(&aclock);
	_localtime32_s(&newtime, &aclock);
	strftime(szTime, sizeof(szTime), "%Y-%m-%d-%H-%M-%S", &newtime);

	//create the Dumps directory
	sprintf_s(szDir, MAX_STRING, "%s\\ZoneDumps", gPathLogs);
	CreateDirectory(szDir, NULL);

	//open timestamped file for the dump output
	sprintf_s(szFilename, MAX_STRING, "%s\\%s_%s_%s.csv", szDir, pMyZone->ShortName, szType, szTime);
	errNum = fopen_s(fOut, szFilename, "at");
	if (errNum) {
		WriteChatf("[MQ2TakeADump] Failed to open Dump file for output. Error: %d, File: %s", (int)errNum, szFilename);
		return false;
	}

	WriteChatf("[MQ2TakeADump] Writing Dump to: %s", szFilename);
	return true;
}

//closes the dump file
BOOL fCloseDump(FILE *fOut)
{
	if (!fclose(fOut)) return true;
	return false;
}

//does the actuall printing to the output file
VOID fOutDump(FILE *fOut, PCHAR output, DWORD delimiter = TAD_COMMA)
{
	fputs(output, fOut);

	if (delimiter == TAD_COMMA) //comma delimiter
		fputs(",", fOut);
	else if (delimiter == TAD_EOL) //end of line
		fputs("\n", fOut);
}

//formats a bool for printing
VOID fOutDumpBOOL(FILE *fOut, BOOL output, DWORD delimiter = TAD_COMMA)
{
	if (output)
		fOutDump(fOut, "true", delimiter);
	else
		fOutDump(fOut, "false", delimiter);
}

//formats numbers for printing
template <class TNUM>
VOID fOutDumpNUM(FILE *fOut, TNUM output, DWORD delimiter = TAD_COMMA)
{
	CHAR szOutput[MAX_STRING] = { 0 };

	fmt::format_to(szOutput, "{}", output);
	fOutDump(fOut, szOutput, delimiter);
}

//formats floats for printing
VOID fOutDumpFLOAT(FILE *fOut, FLOAT output, DWORD delimiter = TAD_COMMA)
{
	CHAR szOutput[MAX_STRING] = { 0 };

	sprintf_s(szOutput, MAX_STRING, "%f", output);
	fOutDump(fOut, szOutput, delimiter);
}

//formats chars for printing (adds quotes around it)
VOID fOutDumpCHAR(FILE *fOut, const char* output, DWORD delimiter = TAD_COMMA)
{
	CHAR szOutput[MAX_STRING] = { 0 };

	sprintf_s(szOutput, MAX_STRING, "\"%s\"", output);
	fOutDump(fOut, szOutput, delimiter);
}

//return the value of the timer in MS
int getTimer()
{
	//if its paused return the time it was paused at
	if (TimerPaused)
	{
		return TimerPaused;
	}

	//else if the timer is on return current time
	else if (TimerStart)
	{
		return (long)clock() - TimerStart;
	}

	//else return 0
	return 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                   //
//                                         DUMP FUNCTIONS                                            //
//                                                                                                   //
///////////////////////////////////////////////////////////////////////////////////////////////////////



//loops through every door in the zone and dumps them to the CSV
VOID dumpDoor()
{
	FILE *fOut = NULL;
	BOOL alignmentError = false;

	//open the door dump for output
	if (fOpenDump(&fOut, "Door"))
	{
		//headers
		fOutDumpCHAR(fOut, "ID");
		fOutDumpCHAR(fOut, "Name");
		fOutDumpCHAR(fOut, "Type");
		fOutDumpCHAR(fOut, "State // 0 = closed, 1 = open, 2 = opening, 3 = closing");
		fOutDumpCHAR(fOut, "DefaultY");
		fOutDumpCHAR(fOut, "DefaultX");
		fOutDumpCHAR(fOut, "DefaultZ");
		fOutDumpCHAR(fOut, "DefaultHeading");
		fOutDumpCHAR(fOut, "DefaultDoorAngle");
		fOutDumpCHAR(fOut, "TopSpeed1");
		fOutDumpCHAR(fOut, "TopSpeed2");
		fOutDumpCHAR(fOut, "Y");
		fOutDumpCHAR(fOut, "X");
		fOutDumpCHAR(fOut, "Z");
		fOutDumpCHAR(fOut, "Heading");
		fOutDumpCHAR(fOut, "DoorAngle");
		fOutDumpCHAR(fOut, "DefaultState");
		fOutDumpCHAR(fOut, "SelfActivated");
		fOutDumpCHAR(fOut, "Dependent");
		fOutDumpCHAR(fOut, "bTemplate");
		fOutDumpCHAR(fOut, "Difficulty  //pick/disarm...");
		fOutDumpCHAR(fOut, "AffectSlots[0]");
		fOutDumpCHAR(fOut, "AffectSlots[1]");
		fOutDumpCHAR(fOut, "AffectSlots[2]");
		fOutDumpCHAR(fOut, "AffectSlots[3]");
		fOutDumpCHAR(fOut, "AffectSlots[4]");
		fOutDumpCHAR(fOut, "CurrentCombination[0]");
		fOutDumpCHAR(fOut, "CurrentCombination[1]");
		fOutDumpCHAR(fOut, "CurrentCombination[2]");
		fOutDumpCHAR(fOut, "CurrentCombination[3]");
		fOutDumpCHAR(fOut, "CurrentCombination[4]");
		fOutDumpCHAR(fOut, "ReqCombination[0]");
		fOutDumpCHAR(fOut, "ReqCombination[1]");
		fOutDumpCHAR(fOut, "ReqCombination[2]");
		fOutDumpCHAR(fOut, "ReqCombination[3]");
		fOutDumpCHAR(fOut, "ReqCombination[4]");
		fOutDumpCHAR(fOut, "RandomCombo");
		fOutDumpCHAR(fOut, "Key");
		fOutDumpCHAR(fOut, "ScaleFactor // divide by 100 to get scale multiplier");
		fOutDumpCHAR(fOut, "SpellID");
		fOutDumpCHAR(fOut, "TargetID[0]");
		fOutDumpCHAR(fOut, "TargetID[1]");
		fOutDumpCHAR(fOut, "TargetID[2]");
		fOutDumpCHAR(fOut, "TargetID[3]");
		fOutDumpCHAR(fOut, "TargetID[4]");
		fOutDumpCHAR(fOut, "Script");
		fOutDumpCHAR(fOut, "TimeStamp   //last time UseSwitch");
		fOutDumpCHAR(fOut, "Accel");
		fOutDumpCHAR(fOut, "AlwaysActive");
		fOutDumpCHAR(fOut, "AdventureDoorID");
		fOutDumpCHAR(fOut, "ReturnY");
		fOutDumpCHAR(fOut, "ReturnX");
		fOutDumpCHAR(fOut, "ReturnZ");
		fOutDumpCHAR(fOut, "DynDoorID");
		fOutDumpCHAR(fOut, "bHasScript");
		fOutDumpCHAR(fOut, "SomeID");
		fOutDumpCHAR(fOut, "bUsable");
		fOutDumpCHAR(fOut, "bRemainOpen");
		fOutDumpCHAR(fOut, "bVisible");
		fOutDumpCHAR(fOut, "bHeadingChanged");
		fOutDumpCHAR(fOut, "bAllowCorpseDrag");
		fOutDumpCHAR(fOut, "RealEstateDoorID");
		fOutDumpCHAR(fOut, "unknownFloat1");
		fOutDumpCHAR(fOut, "unknownFloat2", TAD_EOL); //end of line

														 //data types headers
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "SHORT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT", TAD_EOL); //end of line

											//loop through the doors and dump their structure to a CSV
		for (int i = 0; i < pSwitchMgr->GetCount(); i++)
		{
			PDOOR pDoor = pSwitchMgr->GetSwitch(i);

			fOutDumpNUM(fOut, pDoor->ID);
			fOutDumpCHAR(fOut, pDoor->Name);
			fOutDumpNUM(fOut, pDoor->Type);
			fOutDumpNUM(fOut, pDoor->State);
			fOutDumpFLOAT(fOut, pDoor->DefaultY);
			fOutDumpFLOAT(fOut, pDoor->DefaultX);
			fOutDumpFLOAT(fOut, pDoor->DefaultZ);
			fOutDumpFLOAT(fOut, pDoor->DefaultHeading);
			fOutDumpFLOAT(fOut, pDoor->DefaultDoorAngle);
			fOutDumpFLOAT(fOut, pDoor->TopSpeed1);
			fOutDumpFLOAT(fOut, pDoor->TopSpeed2);
			fOutDumpFLOAT(fOut, pDoor->Y);
			fOutDumpFLOAT(fOut, pDoor->X);
			fOutDumpFLOAT(fOut, pDoor->Z);
			fOutDumpFLOAT(fOut, pDoor->Heading);
			fOutDumpFLOAT(fOut, pDoor->DoorAngle);
			fOutDumpNUM(fOut, pDoor->DefaultState);
			fOutDumpNUM(fOut, pDoor->SelfActivated);
			fOutDumpNUM(fOut, pDoor->Dependent);
			fOutDumpBOOL(fOut, pDoor->bTemplate);
			fOutDumpNUM(fOut, pDoor->Difficulty);
			fOutDumpNUM(fOut, pDoor->AffectSlots[0]);
			fOutDumpNUM(fOut, pDoor->AffectSlots[1]);
			fOutDumpNUM(fOut, pDoor->AffectSlots[2]);
			fOutDumpNUM(fOut, pDoor->AffectSlots[3]);
			fOutDumpNUM(fOut, pDoor->AffectSlots[4]);
			fOutDumpNUM(fOut, pDoor->CurrentCombination[0]);
			fOutDumpNUM(fOut, pDoor->CurrentCombination[1]);
			fOutDumpNUM(fOut, pDoor->CurrentCombination[2]);
			fOutDumpNUM(fOut, pDoor->CurrentCombination[3]);
			fOutDumpNUM(fOut, pDoor->CurrentCombination[4]);
			fOutDumpNUM(fOut, pDoor->ReqCombination[0]);
			fOutDumpNUM(fOut, pDoor->ReqCombination[1]);
			fOutDumpNUM(fOut, pDoor->ReqCombination[2]);
			fOutDumpNUM(fOut, pDoor->ReqCombination[3]);
			fOutDumpNUM(fOut, pDoor->ReqCombination[4]);
			fOutDumpNUM(fOut, pDoor->RandomCombo);
			fOutDumpNUM(fOut, pDoor->Key);
			fOutDumpNUM(fOut, pDoor->ScaleFactor);
			fOutDumpNUM(fOut, pDoor->SpellID);
			fOutDumpNUM(fOut, pDoor->TargetID[0]);
			fOutDumpNUM(fOut, pDoor->TargetID[1]);
			fOutDumpNUM(fOut, pDoor->TargetID[2]);
			fOutDumpNUM(fOut, pDoor->TargetID[3]);
			fOutDumpNUM(fOut, pDoor->TargetID[4]);
			fOutDumpCHAR(fOut, pDoor->Script);
			fOutDumpNUM(fOut, pDoor->TimeStamp);
			fOutDumpFLOAT(fOut, pDoor->Accel);
			fOutDumpNUM(fOut, pDoor->AlwaysActive);
			fOutDumpNUM(fOut, pDoor->AdventureDoorID);
			fOutDumpFLOAT(fOut, pDoor->ReturnY);
			fOutDumpFLOAT(fOut, pDoor->ReturnX);
			fOutDumpFLOAT(fOut, pDoor->ReturnZ);
			fOutDumpNUM(fOut, pDoor->DynDoorID);
			fOutDumpBOOL(fOut, pDoor->bHasScript);
			fOutDumpNUM(fOut, pDoor->SomeID);
			fOutDumpBOOL(fOut, pDoor->bUsable);
			fOutDumpBOOL(fOut, pDoor->bRemainOpen);
			fOutDumpBOOL(fOut, pDoor->bVisible);
			fOutDumpBOOL(fOut, pDoor->bHeadingChanged);
			fOutDumpBOOL(fOut, pDoor->bAllowCorpseDrag);
			fOutDumpNUM(fOut, pDoor->RealEstateDoorID);
			fOutDumpFLOAT(fOut, pDoor->unknownFloat1);
			fOutDumpFLOAT(fOut, pDoor->unknownFloat2, TAD_EOL);

			//Struct Verification
			// spot check a few data elements to see if we have any alignment issues
			// most of the end elements in the door struct are bool, that makes it
			// not very good for detecting alignment issues since it would just
			// be showing the wrong bool value. So were using an earlier and
			// more verifiable element.
			if (pDoor->Heading < -1000 || pDoor->Heading > 2000 ||
				pDoor->SpellID < -1 || pDoor->SpellID > 70000)
			{
				alignmentError = true;
			}

		}

		//close the file
		fCloseDump(fOut);

		//notify if theres any issues
		if (alignmentError)
		{
			WriteChatf("\ar[MQ2TakeADump] ALIGNMENT ISSUE: There may be an issue with PDOOR, please examine your door dumps for accuracy.\ax");
		}
	}
}

//dumps all the groundspawns to the csv
VOID dumpGroundItem()
{
	FILE *fOut = NULL;
	BOOL alignmentError = false;

	//open the grounditem dump for output
	if (fOpenDump(&fOut, "GroundItem"))
	{
		//headers
		fOutDumpCHAR(fOut, "DropID");
		fOutDumpCHAR(fOut, "ZoneID");
		fOutDumpCHAR(fOut, "DropSubID //well zonefile id, but yeah...");
		fOutDumpCHAR(fOut, "Name");
		fOutDumpCHAR(fOut, "Expires");
		fOutDumpCHAR(fOut, "Heading");
		fOutDumpCHAR(fOut, "Pitch");
		fOutDumpCHAR(fOut, "Roll");
		fOutDumpCHAR(fOut, "Scale");
		fOutDumpCHAR(fOut, "Y");
		fOutDumpCHAR(fOut, "X");
		fOutDumpCHAR(fOut, "Z");
		fOutDumpCHAR(fOut, "Weight//-1 means it can't be picked up", TAD_EOL); //end of line

		//data type headers
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "long");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int", TAD_EOL); //end of line

		//loop through the grounditems and dump their structure to a CSV
		EQGroundItem* pItem = pItemList->Top;
		while (pItem)
		{
			//ground items have a weight
			if (pItem->Weight != -1)
			{
				fOutDumpNUM(fOut, pItem->DropID);
				fOutDumpNUM(fOut, pItem->ZoneID);
				fOutDumpNUM(fOut, pItem->DropSubID);
				fOutDumpCHAR(fOut, pItem->Name);
				fOutDumpNUM(fOut, pItem->Expires);
				fOutDumpFLOAT(fOut, pItem->Heading);
				fOutDumpFLOAT(fOut, pItem->Pitch);
				fOutDumpFLOAT(fOut, pItem->Roll);
				fOutDumpFLOAT(fOut, pItem->Scale);
				fOutDumpFLOAT(fOut, pItem->Y);
				fOutDumpFLOAT(fOut, pItem->X);
				fOutDumpFLOAT(fOut, pItem->Z);
				fOutDumpNUM(fOut, pItem->Weight, TAD_EOL); //end of line
			}

			//Struct Verification
			// spot check a few data elements at the end
			// of the struct to predict any alignment issues
			if (pItem->Y > 20000 || pItem->Y < -20000 ||
				pItem->X > 20000 || pItem->X < -20000 ||
				pItem->Z > 9999 || pItem->Z < -99999)
			{
				alignmentError = true;
			}

			pItem = pItem->pNext;
		}

		//close the file
		fCloseDump(fOut);

		//notify if theres any issues
		if (alignmentError)
		{
			WriteChatf("\ar[MQ2TakeADump] ALIGNMENT ISSUE: There may be an issue with PGROUNDITEM, please examine your ground item dumps for accuracy.\ax");
		}
	}
}

//dumps all the objects to the csv
VOID dumpObjects()
{
	FILE *fOut = NULL;
	BOOL alignmentError = false;

	//open the grounditem dump for output
	if (fOpenDump(&fOut, "Objects"))
	{
		//headers
		fOutDumpCHAR(fOut, "DropID");
		fOutDumpCHAR(fOut, "ZoneID");
		fOutDumpCHAR(fOut, "DropSubID //well zonefile id, but yeah...");
		fOutDumpCHAR(fOut, "Name");
		fOutDumpCHAR(fOut, "Expires");
		fOutDumpCHAR(fOut, "Heading");
		fOutDumpCHAR(fOut, "Pitch");
		fOutDumpCHAR(fOut, "Roll");
		fOutDumpCHAR(fOut, "Scale");
		fOutDumpCHAR(fOut, "Y");
		fOutDumpCHAR(fOut, "X");
		fOutDumpCHAR(fOut, "Z");
		fOutDumpCHAR(fOut, "Weight//-1 means it can't be picked up", TAD_EOL); //end of line

		//data type headers
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "long");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int", TAD_EOL); //end of line

		//loop through the objects and dump their structure to a CSV
		EQGroundItem* pItem = pItemList->Top;
		while (pItem)
		{
			//objects have a -1 weight
			if (pItem->Weight == -1)
			{
				fOutDumpNUM(fOut, pItem->DropID);
				fOutDumpNUM(fOut, pItem->ZoneID);
				fOutDumpNUM(fOut, pItem->DropSubID);
				fOutDumpCHAR(fOut, pItem->Name);
				fOutDumpNUM(fOut, pItem->Expires);
				fOutDumpFLOAT(fOut, pItem->Heading);
				fOutDumpFLOAT(fOut, pItem->Pitch);
				fOutDumpFLOAT(fOut, pItem->Roll);
				fOutDumpFLOAT(fOut, pItem->Scale);
				fOutDumpFLOAT(fOut, pItem->Y);
				fOutDumpFLOAT(fOut, pItem->X);
				fOutDumpFLOAT(fOut, pItem->Z);
				fOutDumpNUM(fOut, pItem->Weight, TAD_EOL); //end of line
			}

			//Struct Verification
			// spot check a few data elements at the end
			// of the struct to predict any alignment issues
			if (pItem->Y > 20000 || pItem->Y < -20000 ||
				pItem->X > 20000 || pItem->X < -20000 ||
				pItem->Z > 9999 || pItem->Z < -99999)
			{
				alignmentError = true;
			}

			pItem = pItem->pNext;
		}

		//close the file
		fCloseDump(fOut);

		//notify if theres any issues
		if (alignmentError)
		{
			WriteChatf("\ar[MQ2TakeADump] ALIGNMENT ISSUE: There may be an issue with PGROUNDITEM, please examine your objects item dumps for accuracy.\ax");
		}
	}
}

//dump all the spawn objects to the CSV
VOID dumpNPCType()
{
	FILE *fOut = NULL;
	BOOL alignmentError = false;

	//open the grounditem dump for output
	if (fOpenDump(&fOut, "NPC"))
	{
		//headers
		fOutDumpCHAR(fOut, "JumpStrength");
		fOutDumpCHAR(fOut, "SwimStrength");
		fOutDumpCHAR(fOut, "SpeedMultiplier");
		fOutDumpCHAR(fOut, "AreaFriction");
		fOutDumpCHAR(fOut, "AccelerationFriction");
		fOutDumpCHAR(fOut, "FloorHeight");
		fOutDumpCHAR(fOut, "bSinksInWater");
		fOutDumpCHAR(fOut, "PlayerTimeStamp /* doesn't update when on a Vehicle (mounts/boats etc) */");
		fOutDumpCHAR(fOut, "LastTimeIdle");
		fOutDumpCHAR(fOut, "Lastname[0x20]");
		fOutDumpCHAR(fOut, "AreaHPRegenMod /*from guild hall pools etc. */");
		fOutDumpCHAR(fOut, "AreaEndRegenMod");
		fOutDumpCHAR(fOut, "AreaManaRegenMod");
		fOutDumpCHAR(fOut, "Y");
		fOutDumpCHAR(fOut, "X");
		fOutDumpCHAR(fOut, "Z");
		fOutDumpCHAR(fOut, "SpeedY");
		fOutDumpCHAR(fOut, "SpeedX");
		fOutDumpCHAR(fOut, "SpeedZ");
		fOutDumpCHAR(fOut, "SpeedRun");
		fOutDumpCHAR(fOut, "Heading");
		fOutDumpCHAR(fOut, "Angle");
		fOutDumpCHAR(fOut, "AccelAngle");
		fOutDumpCHAR(fOut, "SpeedHeading");
		fOutDumpCHAR(fOut, "CameraAngle");
		fOutDumpCHAR(fOut, "UnderWater /*LastHeadEnvironmentType */");
		fOutDumpCHAR(fOut, "LastBodyEnvironmentType");
		fOutDumpCHAR(fOut, "LastFeetEnvironmentType");
		fOutDumpCHAR(fOut, "HeadWet /*these really are environment related, like lava as well for example */");
		fOutDumpCHAR(fOut, "FeetWet");
		fOutDumpCHAR(fOut, "BodyWet");
		fOutDumpCHAR(fOut, "LastBodyWet");
		fOutDumpCHAR(fOut, "Name[0x40] /* ie priest_of_discord00 */");
		fOutDumpCHAR(fOut, "DisplayedName[0x40] /* ie Priest of Discord*/");
		fOutDumpCHAR(fOut, "PossiblyStuck /* never seen this be 1 so maybe it was used a a point but not now... */");
		fOutDumpCHAR(fOut, "Type");
		fOutDumpCHAR(fOut, "BodyType /* this really should be renamed to charprops or something its broken anyway*/");
		fOutDumpCHAR(fOut, "SpawnTyp");
		fOutDumpCHAR(fOut, "CharacterPropertyHash");
		fOutDumpCHAR(fOut, "AvatarHeight /* height of avatar from groundwhen standing*/");
		fOutDumpCHAR(fOut, "Height");
		fOutDumpCHAR(fOut, "Width");
		fOutDumpCHAR(fOut, "Length");
		fOutDumpCHAR(fOut, "SpawnID");
		fOutDumpCHAR(fOut, "PlayerState /* 0=Idle 1=Open 2=WeaponSheathed 4=Aggressive 8=ForcedAggressive 0x10=InstrumentEquipped 0x20=Stunned 0x40=PrimaryWeaponEquipped 0x80=SecondaryWeaponEquipped */");
		fOutDumpCHAR(fOut, "Vehicle /* NULL until you collide with a vehicle (boat,airship etc) */");
		fOutDumpCHAR(fOut, "Mount /* NULL if no mount present */");
		fOutDumpCHAR(fOut, "Rider /* _SPAWNINFO of mount's rider */");
		fOutDumpCHAR(fOut, "Unknown0x0164");
		fOutDumpCHAR(fOut, "Targetable /* true if mob is targetable */");
		fOutDumpCHAR(fOut, "bTargetCyclable");
		fOutDumpCHAR(fOut, "bClickThrough");
		fOutDumpCHAR(fOut, "bBeingFlung");
		fOutDumpCHAR(fOut, "FlingActiveTimer");
		fOutDumpCHAR(fOut, "FlingTimerStart");
		fOutDumpCHAR(fOut, "bFlingSomething");
		fOutDumpCHAR(fOut, "FlingY");
		fOutDumpCHAR(fOut, "FlingX");
		fOutDumpCHAR(fOut, "FlingZ");
		fOutDumpCHAR(fOut, "bFlingSnapToDest");
		fOutDumpCHAR(fOut, "SplineID");
		fOutDumpCHAR(fOut, "SplineRiderID");
		fOutDumpCHAR(fOut, "LockID");
		fOutDumpCHAR(fOut, "EncounterLockState");
		fOutDumpCHAR(fOut, "ParticleCastStartTime");
		fOutDumpCHAR(fOut, "ParticleCastDuration");
		fOutDumpCHAR(fOut, "ParticleVisualSpellNum");
		fOutDumpCHAR(fOut, "MeleeRadius // used by GetMeleeRange");
		fOutDumpCHAR(fOut, "CollisionCounter");
		fOutDumpCHAR(fOut, "CachedFloorLocationY");
		fOutDumpCHAR(fOut, "CachedFloorLocationX");
		fOutDumpCHAR(fOut, "CachedFloorLocationZ");
		fOutDumpCHAR(fOut, "CachedFloorHeight");
		fOutDumpCHAR(fOut, "CachedCeilingLocationY");
		fOutDumpCHAR(fOut, "CachedCeilingLocationX");
		fOutDumpCHAR(fOut, "CachedCeilingLocationZ");
		fOutDumpCHAR(fOut, "CachedCeilingHeight");
		fOutDumpCHAR(fOut, "Animation /* Current Animation Playing. */");
		fOutDumpCHAR(fOut, "NextAnim");
		fOutDumpCHAR(fOut, "CurrLowerBodyAnim");
		fOutDumpCHAR(fOut, "NextLowerBodyAnim");
		fOutDumpCHAR(fOut, "CurrLowerAnimVariation");
		fOutDumpCHAR(fOut, "CurrAnimVariation");
		fOutDumpCHAR(fOut, "CurrAnimRndVariation");
		fOutDumpCHAR(fOut, "Loop3d_SoundID");
		fOutDumpCHAR(fOut, "Step_SoundID");
		fOutDumpCHAR(fOut, "CurLoop_SoundID");
		fOutDumpCHAR(fOut, "Idle3d1_SoundID");
		fOutDumpCHAR(fOut, "Idle3d2_SoundID");
		fOutDumpCHAR(fOut, "Jump_SoundID");
		fOutDumpCHAR(fOut, "Hit1_SoundID");
		fOutDumpCHAR(fOut, "Hit2_SoundID");
		fOutDumpCHAR(fOut, "Hit3_SoundID");
		fOutDumpCHAR(fOut, "Hit4_SoundID");
		fOutDumpCHAR(fOut, "Gasp1_SoundID");
		fOutDumpCHAR(fOut, "Gasp2_SoundID");
		fOutDumpCHAR(fOut, "Drown_SoundID");
		fOutDumpCHAR(fOut, "Death_SoundID");
		fOutDumpCHAR(fOut, "Attk1_SoundID");
		fOutDumpCHAR(fOut, "Attk2_SoundID");
		fOutDumpCHAR(fOut, "Attk3_SoundID");
		fOutDumpCHAR(fOut, "Walk_SoundID");
		fOutDumpCHAR(fOut, "Run_SoundID");
		fOutDumpCHAR(fOut, "Crouch_SoundID");
		fOutDumpCHAR(fOut, "Swim_SoundID");
		fOutDumpCHAR(fOut, "TreadWater_SoundID");
		fOutDumpCHAR(fOut, "Climb_SoundID");
		fOutDumpCHAR(fOut, "Sit_SoundID");
		fOutDumpCHAR(fOut, "Kick_SoundID");
		fOutDumpCHAR(fOut, "Bash_SoundID");
		fOutDumpCHAR(fOut, "FireBow_SoundID");
		fOutDumpCHAR(fOut, "MonkAttack1_SoundID");
		fOutDumpCHAR(fOut, "MonkAttack2_SoundID");
		fOutDumpCHAR(fOut, "MonkSpecial_SoundID");
		fOutDumpCHAR(fOut, "PrimaryBlunt_SoundID");
		fOutDumpCHAR(fOut, "PrimarySlash_SoundID");
		fOutDumpCHAR(fOut, "PrimaryStab_SoundID");
		fOutDumpCHAR(fOut, "Punch_SoundID");
		fOutDumpCHAR(fOut, "Roundhouse_SoundID");
		fOutDumpCHAR(fOut, "SecondaryBlunt_SoundID");
		fOutDumpCHAR(fOut, "SecondarySlash_SoundID");
		fOutDumpCHAR(fOut, "SecondaryStab_SoundID");
		fOutDumpCHAR(fOut, "SwimAttack_SoundID");
		fOutDumpCHAR(fOut, "TwoHandedBlunt_SoundID");
		fOutDumpCHAR(fOut, "TwoHandedSlash_SoundID");
		fOutDumpCHAR(fOut, "TwoHandedStab_SoundID");
		fOutDumpCHAR(fOut, "SecondaryPunch_SoundID");
		fOutDumpCHAR(fOut, "JumpAcross_SoundID");
		fOutDumpCHAR(fOut, "WalkBackwards_SoundID");
		fOutDumpCHAR(fOut, "CrouchWalk_SoundID");
		fOutDumpCHAR(fOut, "LastHurtSound");
		fOutDumpCHAR(fOut, "LastWalkTime//used for animations");
		fOutDumpCHAR(fOut, "ShipRelated//ID? look into.");
		fOutDumpCHAR(fOut, "RightHolding//Nothing=0 Other/Weapon=1 shield=2");
		fOutDumpCHAR(fOut, "LeftHolding//old Holding");
		fOutDumpCHAR(fOut, "DeathAnimationFinishTime");
		fOutDumpCHAR(fOut, "bRemoveCorpseAfterDeathAnim//0x1274 for sure used by /hidecorpse");
		fOutDumpCHAR(fOut, "LastBubblesTime");
		fOutDumpCHAR(fOut, "LastBubblesTime1");
		fOutDumpCHAR(fOut, "LastColdBreathTime");
		fOutDumpCHAR(fOut, "LastParticleUpdateTime");
		fOutDumpCHAR(fOut, "MercID //IT IS 0x1288 //if the spawn is player and has a merc up this is it's spawn ID -eqmule 16 jul 2014");
		fOutDumpCHAR(fOut, "ContractorID //if the spawn is a merc this is its contractor's spawn ID -eqmule 16 jul 2014");
		fOutDumpCHAR(fOut, "CeilingHeightAtCurrLocation");
		fOutDumpCHAR(fOut, "bInstantHPGaugeChange");
		fOutDumpCHAR(fOut, "LastUpdateReceivedTime");
		fOutDumpCHAR(fOut, "MaxSpeakDistance");
		fOutDumpCHAR(fOut, "WalkSpeed //how much we will slow down while sneaking");
		fOutDumpCHAR(fOut, "bHideCorpse //IT IS 0x12a8");
		fOutDumpCHAR(fOut, "AssistName[0x40]");
		fOutDumpCHAR(fOut, "InvitedToGroup //IT IS 12E9!");
		fOutDumpCHAR(fOut, "GroupMemberTargeted //12ec for sure! // 0xFFFFFFFF if no target, else 1 through 5");
		fOutDumpCHAR(fOut, "bRemovalPending");
		fOutDumpCHAR(fOut, "EmitterScalingRadius //0x12f8 FOR SURE");
		fOutDumpCHAR(fOut, "DefaultEmitterID");
		fOutDumpCHAR(fOut, "bDisplayNameSprite");
		fOutDumpCHAR(fOut, "bIdleAnimationOff");
		fOutDumpCHAR(fOut, "bIsInteractiveObject");
		fOutDumpCHAR(fOut, "InteractiveObjectModelName[0x80]");
		fOutDumpCHAR(fOut, "InteractiveObjectOtherName[0x80]");
		fOutDumpCHAR(fOut, "InteractiveObjectName[0x40]");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.Y");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.X");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.Z");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.SpeedY");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.SpeedX");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.SpeedZ");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.SpeedRun");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.Heading");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.Angle");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.AccelAngle");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.SpeedHeading");
		fOutDumpCHAR(fOut, "PhysicsBeforeLastPort.CameraAngle");
		fOutDumpCHAR(fOut, "notsure");
		fOutDumpCHAR(fOut, "Fellowship.Version");
		fOutDumpCHAR(fOut, "Fellowship.Version2//just place holders for now, ill fix these later");
		fOutDumpCHAR(fOut, "Fellowship.Version3");
		fOutDumpCHAR(fOut, "Fellowship.Version4");
		fOutDumpCHAR(fOut, "Fellowship.FellowshipID");
		fOutDumpCHAR(fOut, "Fellowship.FellowshipGUID.UniqueEntityID");
		fOutDumpCHAR(fOut, "Fellowship.FellowshipGUID.WorldUniqueID");
		fOutDumpCHAR(fOut, "Fellowship.FellowshipGUID.Reserved");
		fOutDumpCHAR(fOut, "Fellowship.Leader[0x40]");
		fOutDumpCHAR(fOut, "Fellowship.MotD[0x400]");
		fOutDumpCHAR(fOut, "Fellowship.Members");
		for (int i = 0; i < MAX_FELLOWSHIP_MEMBERS; i++)
		{
			fOutDumpCHAR(fOut, fmt::format("Fellowship.FellowshipMember[{}].UniqueEntityID.UniqueEntityID", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.FellowshipMember[{}].UniqueEntityID.WorldUniqueID", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.FellowshipMember[{}].UniqueEntityID.Reserved", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.FellowshipMember[{}].Name[0x40]", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.FellowshipMember[{}].ZoneID", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.FellowshipMember[{}].Level", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.FellowshipMember[{}].Class", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.FellowshipMember[{}].LastOn// FastTime() timestamp", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.bExpSharingEnabled[{}]", i).c_str());
			fOutDumpCHAR(fOut, fmt::format("Fellowship.bSharedExpCapped[{}]", i).c_str());
		}
		fOutDumpCHAR(fOut, "Fellowship.Sync");
		fOutDumpCHAR(fOut, "CampfireY//see 585D7A in jul 9 2018 test");
		fOutDumpCHAR(fOut, "CampfireX");
		fOutDumpCHAR(fOut, "CampfireZ");
		fOutDumpCHAR(fOut, "CampfireZoneID // zone ID where campfire is");
		fOutDumpCHAR(fOut, "CampfireTimestamp // CampfireTimestamp-FastTime()=time left on campfire");
		fOutDumpCHAR(fOut, "CampfireTimestamp2");
		fOutDumpCHAR(fOut, "FellowShipID");
		fOutDumpCHAR(fOut, "FellowShipID2");
		fOutDumpCHAR(fOut, "CampType");
		fOutDumpCHAR(fOut, "Campfire");
		fOutDumpCHAR(fOut, "SeeInvis[0]");
		fOutDumpCHAR(fOut, "SeeInvis[1]");
		fOutDumpCHAR(fOut, "SeeInvis[2]");
		fOutDumpCHAR(fOut, "Equipment.Head.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Head.Var");
		fOutDumpCHAR(fOut, "Equipment.Head.Material");
		fOutDumpCHAR(fOut, "Equipment.Head.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Head.NewArmorType");
		fOutDumpCHAR(fOut, "Equipment.Chest.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Chest.Var");
		fOutDumpCHAR(fOut, "Equipment.Chest.Material");
		fOutDumpCHAR(fOut, "Equipment.Chest.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Chest.NewArmorType");
		fOutDumpCHAR(fOut, "Equipment.Arms.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Arms.Var");
		fOutDumpCHAR(fOut, "Equipment.Arms.Material");
		fOutDumpCHAR(fOut, "Equipment.Arms.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Arms.NewArmorType");
		fOutDumpCHAR(fOut, "Equipment.Wrists.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Wrists.Var");
		fOutDumpCHAR(fOut, "Equipment.Wrists.Material");
		fOutDumpCHAR(fOut, "Equipment.Wrists.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Wrists.NewArmorType");
		fOutDumpCHAR(fOut, "Equipment.Hands.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Hands.Var");
		fOutDumpCHAR(fOut, "Equipment.Hands.Material");
		fOutDumpCHAR(fOut, "Equipment.Hands.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Hands.NewArmorType");
		fOutDumpCHAR(fOut, "Equipment.Legs.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Legs.Var");
		fOutDumpCHAR(fOut, "Equipment.Legs.Material");
		fOutDumpCHAR(fOut, "Equipment.Legs.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Legs.NewArmorType");
		fOutDumpCHAR(fOut, "Equipment.Feet.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Feet.Var");
		fOutDumpCHAR(fOut, "Equipment.Feet.Material");
		fOutDumpCHAR(fOut, "Equipment.Feet.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Feet.NewArmorType");
		fOutDumpCHAR(fOut, "Equipment.Primary.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Primary.Var");
		fOutDumpCHAR(fOut, "Equipment.Primary.Material");
		fOutDumpCHAR(fOut, "Equipment.Primary.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Primary.NewArmorType");
		fOutDumpCHAR(fOut, "Equipment.Offhand.ID //idfile on Lucy");
		fOutDumpCHAR(fOut, "Equipment.Offhand.Var");
		fOutDumpCHAR(fOut, "Equipment.Offhand.Material");
		fOutDumpCHAR(fOut, "Equipment.Offhand.NewArmorID");
		fOutDumpCHAR(fOut, "Equipment.Offhand.NewArmorType");
		fOutDumpCHAR(fOut, "bIsPlacingItem");
		fOutDumpCHAR(fOut, "bGMCreatedNPC");
		fOutDumpCHAR(fOut, "ObjectAnimationID");
		fOutDumpCHAR(fOut, "bInteractiveObjectCollidable");
		fOutDumpCHAR(fOut, "InteractiveObjectType");
		fOutDumpCHAR(fOut, "SoundIDs[0]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[1]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[2]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[3]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[4]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[5]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[6]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[7]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[8]//0x28 bytes");
		fOutDumpCHAR(fOut, "SoundIDs[9]//0x28 bytes");
		fOutDumpCHAR(fOut, "BardTwistSpells");
		fOutDumpCHAR(fOut, "CurrentBardTwistIndex");
		fOutDumpCHAR(fOut, "CurrentBardTwistIndex2");
		fOutDumpCHAR(fOut, "SpawnStatus[0]//todo: look closer at these i think they can show like status of mobs slowed, mezzed etc, but not sure");
		fOutDumpCHAR(fOut, "SpawnStatus[1]//todo: look closer at these i think they can show like status of mobs slowed, mezzed etc, but not sure");
		fOutDumpCHAR(fOut, "SpawnStatus[2]//todo: look closer at these i think they can show like status of mobs slowed, mezzed etc, but not sure");
		fOutDumpCHAR(fOut, "SpawnStatus[3]//todo: look closer at these i think they can show like status of mobs slowed, mezzed etc, but not sure");
		fOutDumpCHAR(fOut, "SpawnStatus[4]//todo: look closer at these i think they can show like status of mobs slowed, mezzed etc, but not sure");
		fOutDumpCHAR(fOut, "SpawnStatus[5]//todo: look closer at these i think they can show like status of mobs slowed, mezzed etc, but not sure");
		fOutDumpCHAR(fOut, "BannerIndex0//guild banners");
		fOutDumpCHAR(fOut, "BannerIndex1");
		fOutDumpCHAR(fOut, "BannerTint0");
		fOutDumpCHAR(fOut, "BannerTint1");
		fOutDumpCHAR(fOut, "MountAnimationRelated");
		fOutDumpCHAR(fOut, "bGuildShowAnim//or sprite? need to check");
		fOutDumpCHAR(fOut, "bWaitingForPort//check this");
		fOutDumpCHAR(fOut, "mActorClient.TextureType");
		fOutDumpCHAR(fOut, "mActorClient.Material");
		fOutDumpCHAR(fOut, "mActorClient.Variation");
		fOutDumpCHAR(fOut, "mActorClient.HeadType");
		fOutDumpCHAR(fOut, "mActorClient.FaceStyle");
		fOutDumpCHAR(fOut, "mActorClient.HairColor");
		fOutDumpCHAR(fOut, "mActorClient.FacialHairColor");
		fOutDumpCHAR(fOut, "mActorClient.EyeColor1");
		fOutDumpCHAR(fOut, "mActorClient.EyeColor2");
		fOutDumpCHAR(fOut, "mActorClient.HairStyle");
		fOutDumpCHAR(fOut, "mActorClient.FacialHair");
		fOutDumpCHAR(fOut, "mActorClient.Race");
		fOutDumpCHAR(fOut, "mActorClient.RaceOverride");
		fOutDumpCHAR(fOut, "mActorClient.Class");
		fOutDumpCHAR(fOut, "mActorClient.Gender");
		fOutDumpCHAR(fOut, "mActorClient.ActorDef[0x40]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[0]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[1]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[2]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[3]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[4]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[5]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[6]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[7]");
		fOutDumpCHAR(fOut, "mActorClient.ArmorColor[8]");
		fOutDumpCHAR(fOut, "mActorClient.bShowHelm");
		fOutDumpCHAR(fOut, "mActorClient.Heritage");
		fOutDumpCHAR(fOut, "mActorClient.Tattoo");
		fOutDumpCHAR(fOut, "mActorClient.Details");
		fOutDumpCHAR(fOut, "mActorClient.LeftEyeMaterialIndex");
		fOutDumpCHAR(fOut, "mActorClient.RightEyeMaterialIndex");
		fOutDumpCHAR(fOut, "mActorClient.bActiveTransition");
		fOutDumpCHAR(fOut, "mActorClient.CurrentStage");
		fOutDumpCHAR(fOut, "mActorClient.ZOffset");
		fOutDumpCHAR(fOut, "mActorClient.AdjustedLoc.Y");
		fOutDumpCHAR(fOut, "mActorClient.AdjustedLoc.X");
		fOutDumpCHAR(fOut, "mActorClient.AdjustedLoc.Z");
		fOutDumpCHAR(fOut, "mActorClient.bReplacedStaticObject");
		fOutDumpCHAR(fOut, "mActorClient.PartialFaceNumber");
		fOutDumpCHAR(fOut, "mActorClient.bNewArmorDisabled");
		fOutDumpCHAR(fOut, "GetClassString()");
		fOutDumpCHAR(fOut, "GetId()");
		fOutDumpCHAR(fOut, "RunSpeed");
		fOutDumpCHAR(fOut, "HPMax");
		fOutDumpCHAR(fOut, "CharClass");
		fOutDumpCHAR(fOut, "WarCry");
		fOutDumpCHAR(fOut, "Deity");
		fOutDumpCHAR(fOut, "MyWalkSpeed");
		fOutDumpCHAR(fOut, "GetMeleeRangeVar1");
		fOutDumpCHAR(fOut, "FindBits");
		fOutDumpCHAR(fOut, "Title[0x80]");
		fOutDumpCHAR(fOut, "Level");
		fOutDumpCHAR(fOut, "Light", TAD_EOL); //end of line

		//data type headers
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "uint64_t");
		fOutDumpCHAR(fOut, "uint32_t");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "WORD");
		fOutDumpCHAR(fOut, "WORD");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "DWORD");
		for (int i = 0; i < MAX_FELLOWSHIP_MEMBERS; i++)
		{
			fOutDumpCHAR(fOut, "UINT");
			fOutDumpCHAR(fOut, "WORD");
			fOutDumpCHAR(fOut, "WORD");
			fOutDumpCHAR(fOut, "CHAR");
			fOutDumpCHAR(fOut, "DWORD");
			fOutDumpCHAR(fOut, "DWORD");
			fOutDumpCHAR(fOut, "DWORD");
			fOutDumpCHAR(fOut, "DWORD");
			fOutDumpCHAR(fOut, "bool");
			fOutDumpCHAR(fOut, "bool");
		}
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "ARGBCOLOR");
		fOutDumpCHAR(fOut, "ARGBCOLOR");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "signed int");
		fOutDumpCHAR(fOut, "signed int");
		fOutDumpCHAR(fOut, "signed int");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "__int64");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE", TAD_EOL); //end of line

		//loop through the npcs and dump their structure to a CSV
		PlayerClient* pSpawn = pSpawnList;
		while (pSpawn)
		{
			fOutDumpFLOAT(fOut, pSpawn->JumpStrength);
			fOutDumpFLOAT(fOut, pSpawn->SwimStrength);
			fOutDumpFLOAT(fOut, pSpawn->SpeedMultiplier);
			fOutDumpFLOAT(fOut, pSpawn->AreaFriction);
			fOutDumpFLOAT(fOut, pSpawn->AccelerationFriction);
			fOutDumpFLOAT(fOut, pSpawn->FloorHeight);
			fOutDumpBOOL(fOut, pSpawn->bSinksInWater);
			fOutDumpNUM(fOut, pSpawn->PlayerTimeStamp);
			fOutDumpNUM(fOut, pSpawn->LastTimeIdle);
			fOutDumpCHAR(fOut, pSpawn->Lastname);
			fOutDumpFLOAT(fOut, pSpawn->AreaHPRegenMod);
			fOutDumpFLOAT(fOut, pSpawn->AreaEndRegenMod);
			fOutDumpFLOAT(fOut, pSpawn->AreaManaRegenMod);
			fOutDumpFLOAT(fOut, pSpawn->Y);
			fOutDumpFLOAT(fOut, pSpawn->X);
			fOutDumpFLOAT(fOut, pSpawn->Z);
			fOutDumpFLOAT(fOut, pSpawn->SpeedY);
			fOutDumpFLOAT(fOut, pSpawn->SpeedX);
			fOutDumpFLOAT(fOut, pSpawn->SpeedZ);
			fOutDumpFLOAT(fOut, pSpawn->SpeedRun);
			fOutDumpFLOAT(fOut, pSpawn->Heading);
			fOutDumpFLOAT(fOut, pSpawn->Angle);
			fOutDumpFLOAT(fOut, pSpawn->AccelAngle);
			fOutDumpFLOAT(fOut, pSpawn->SpeedHeading);
			fOutDumpFLOAT(fOut, pSpawn->CameraAngle);
			fOutDumpNUM(fOut, pSpawn->UnderWater);
			fOutDumpNUM(fOut, pSpawn->LastBodyEnvironmentType);
			fOutDumpNUM(fOut, pSpawn->LastFeetEnvironmentType);
			fOutDumpNUM(fOut, pSpawn->HeadWet);
			fOutDumpNUM(fOut, pSpawn->FeetWet);
			fOutDumpNUM(fOut, pSpawn->BodyWet);
			fOutDumpNUM(fOut, pSpawn->LastBodyWet);
			fOutDumpCHAR(fOut, pSpawn->Name);
			fOutDumpCHAR(fOut, pSpawn->DisplayedName);
			fOutDumpNUM(fOut, pSpawn->PossiblyStuck);
			fOutDumpNUM(fOut, pSpawn->Type);
			fOutDumpNUM(fOut, GetBodyType(pSpawn)); // specialize body type because it's known useful
			fOutDumpNUM(fOut, static_cast<uint32_t>(GetSpawnType(pSpawn)));
			std::vector<int> properties;
			properties.reserve(pSpawn->Properties.size());
			for (auto prop_it = pSpawn->Properties.begin(); prop_it != pSpawn->Properties.end(); ++prop_it)
				properties.push_back(prop_it->first);
			fOutDumpCHAR(fOut, fmt::format("{}", fmt::join(properties, "|")).c_str());
			fOutDumpFLOAT(fOut, pSpawn->AvatarHeight);
			fOutDumpFLOAT(fOut, pSpawn->Height);
			fOutDumpFLOAT(fOut, pSpawn->Width);
			fOutDumpFLOAT(fOut, pSpawn->Length);
			fOutDumpNUM(fOut, pSpawn->SpawnID);
			fOutDumpNUM(fOut, pSpawn->PlayerState);
			fOutDumpNUM(fOut, pSpawn->Vehicle != nullptr ? pSpawn->Vehicle->SpawnID : 0U);
			fOutDumpNUM(fOut, pSpawn->Mount != nullptr ? pSpawn->Mount->SpawnID : 0U);
			fOutDumpNUM(fOut, pSpawn->Rider != nullptr ? pSpawn->Rider->SpawnID : 0U);
			fOutDumpNUM(fOut, pSpawn->Unknown0x0164);
			fOutDumpBOOL(fOut, pSpawn->Targetable);
			fOutDumpBOOL(fOut, pSpawn->bTargetCyclable);
			fOutDumpBOOL(fOut, pSpawn->bClickThrough);
			fOutDumpBOOL(fOut, pSpawn->bBeingFlung);
			fOutDumpNUM(fOut, pSpawn->FlingActiveTimer);
			fOutDumpNUM(fOut, pSpawn->FlingTimerStart);
			fOutDumpBOOL(fOut, pSpawn->bFlingSomething);
			fOutDumpFLOAT(fOut, pSpawn->FlingY);
			fOutDumpFLOAT(fOut, pSpawn->FlingX);
			fOutDumpFLOAT(fOut, pSpawn->FlingZ);
			fOutDumpBOOL(fOut, pSpawn->bFlingSnapToDest);
			fOutDumpNUM(fOut, pSpawn->SplineID);
			fOutDumpNUM(fOut, pSpawn->SplineRiderID);
			fOutDumpNUM(fOut, pSpawn->LockID);
			fOutDumpNUM(fOut, pSpawn->EncounterLockState);
			fOutDumpNUM(fOut, pSpawn->ParticleCastStartTime);
			fOutDumpNUM(fOut, pSpawn->ParticleCastDuration);
			fOutDumpNUM(fOut, pSpawn->ParticleVisualSpellNum);
			fOutDumpFLOAT(fOut, pSpawn->MeleeRadius);
			fOutDumpNUM(fOut, pSpawn->CollisionCounter);
			fOutDumpFLOAT(fOut, pSpawn->CachedFloorLocationY);
			fOutDumpFLOAT(fOut, pSpawn->CachedFloorLocationX);
			fOutDumpFLOAT(fOut, pSpawn->CachedFloorLocationZ);
			fOutDumpFLOAT(fOut, pSpawn->CachedFloorHeight);
			fOutDumpFLOAT(fOut, pSpawn->CachedCeilingLocationY);
			fOutDumpFLOAT(fOut, pSpawn->CachedCeilingLocationX);
			fOutDumpFLOAT(fOut, pSpawn->CachedCeilingLocationZ);
			fOutDumpFLOAT(fOut, pSpawn->CachedCeilingHeight);
			fOutDumpNUM(fOut, pSpawn->Animation);
			fOutDumpNUM(fOut, pSpawn->NextAnim);
			fOutDumpNUM(fOut, pSpawn->CurrLowerBodyAnim);
			fOutDumpNUM(fOut, pSpawn->NextLowerBodyAnim);
			fOutDumpNUM(fOut, pSpawn->CurrLowerAnimVariation);
			fOutDumpNUM(fOut, pSpawn->CurrAnimVariation);
			fOutDumpNUM(fOut, pSpawn->CurrAnimRndVariation);
			fOutDumpNUM(fOut, pSpawn->Loop3d_SoundID);
			fOutDumpNUM(fOut, pSpawn->Step_SoundID);
			fOutDumpNUM(fOut, pSpawn->CurLoop_SoundID);
			fOutDumpNUM(fOut, pSpawn->Idle3d1_SoundID);
			fOutDumpNUM(fOut, pSpawn->Idle3d2_SoundID);
			fOutDumpNUM(fOut, pSpawn->Jump_SoundID);
			fOutDumpNUM(fOut, pSpawn->Hit1_SoundID);
			fOutDumpNUM(fOut, pSpawn->Hit2_SoundID);
			fOutDumpNUM(fOut, pSpawn->Hit3_SoundID);
			fOutDumpNUM(fOut, pSpawn->Hit4_SoundID);
			fOutDumpNUM(fOut, pSpawn->Gasp1_SoundID);
			fOutDumpNUM(fOut, pSpawn->Gasp2_SoundID);
			fOutDumpNUM(fOut, pSpawn->Drown_SoundID);
			fOutDumpNUM(fOut, pSpawn->Death_SoundID);
			fOutDumpNUM(fOut, pSpawn->Attk1_SoundID);
			fOutDumpNUM(fOut, pSpawn->Attk2_SoundID);
			fOutDumpNUM(fOut, pSpawn->Attk3_SoundID);
			fOutDumpNUM(fOut, pSpawn->Walk_SoundID);
			fOutDumpNUM(fOut, pSpawn->Run_SoundID);
			fOutDumpNUM(fOut, pSpawn->Crouch_SoundID);
			fOutDumpNUM(fOut, pSpawn->Swim_SoundID);
			fOutDumpNUM(fOut, pSpawn->TreadWater_SoundID);
			fOutDumpNUM(fOut, pSpawn->Climb_SoundID);
			fOutDumpNUM(fOut, pSpawn->Sit_SoundID);
			fOutDumpNUM(fOut, pSpawn->Kick_SoundID);
			fOutDumpNUM(fOut, pSpawn->Bash_SoundID);
			fOutDumpNUM(fOut, pSpawn->FireBow_SoundID);
			fOutDumpNUM(fOut, pSpawn->MonkAttack1_SoundID);
			fOutDumpNUM(fOut, pSpawn->MonkAttack2_SoundID);
			fOutDumpNUM(fOut, pSpawn->MonkSpecial_SoundID);
			fOutDumpNUM(fOut, pSpawn->PrimaryBlunt_SoundID);
			fOutDumpNUM(fOut, pSpawn->PrimarySlash_SoundID);
			fOutDumpNUM(fOut, pSpawn->PrimaryStab_SoundID);
			fOutDumpNUM(fOut, pSpawn->Punch_SoundID);
			fOutDumpNUM(fOut, pSpawn->Roundhouse_SoundID);
			fOutDumpNUM(fOut, pSpawn->SecondaryBlunt_SoundID);
			fOutDumpNUM(fOut, pSpawn->SecondarySlash_SoundID);
			fOutDumpNUM(fOut, pSpawn->SecondaryStab_SoundID);
			fOutDumpNUM(fOut, pSpawn->SwimAttack_SoundID);
			fOutDumpNUM(fOut, pSpawn->TwoHandedBlunt_SoundID);
			fOutDumpNUM(fOut, pSpawn->TwoHandedSlash_SoundID);
			fOutDumpNUM(fOut, pSpawn->TwoHandedStab_SoundID);
			fOutDumpNUM(fOut, pSpawn->SecondaryPunch_SoundID);
			fOutDumpNUM(fOut, pSpawn->JumpAcross_SoundID);
			fOutDumpNUM(fOut, pSpawn->WalkBackwards_SoundID);
			fOutDumpNUM(fOut, pSpawn->CrouchWalk_SoundID);
			fOutDumpNUM(fOut, pSpawn->LastHurtSound);
			fOutDumpNUM(fOut, pSpawn->LastWalkTime);
			fOutDumpNUM(fOut, pSpawn->ShipRelated);
			fOutDumpNUM(fOut, pSpawn->RightHolding);
			fOutDumpNUM(fOut, pSpawn->LeftHolding);
			fOutDumpNUM(fOut, pSpawn->DeathAnimationFinishTime);
			fOutDumpBOOL(fOut, pSpawn->bRemoveCorpseAfterDeathAnim);
			fOutDumpNUM(fOut, pSpawn->LastBubblesTime);
			fOutDumpNUM(fOut, pSpawn->LastBubblesTime1);
			fOutDumpNUM(fOut, pSpawn->LastColdBreathTime);
			fOutDumpNUM(fOut, pSpawn->LastParticleUpdateTime);
			fOutDumpNUM(fOut, pSpawn->MercID);
			fOutDumpNUM(fOut, pSpawn->ContractorID);
			fOutDumpFLOAT(fOut, pSpawn->CeilingHeightAtCurrLocation);
			fOutDumpBOOL(fOut, pSpawn->bInstantHPGaugeChange);
			fOutDumpNUM(fOut, pSpawn->LastUpdateReceivedTime);
			fOutDumpFLOAT(fOut, pSpawn->MaxSpeakDistance);
			fOutDumpFLOAT(fOut, pSpawn->WalkSpeed);
			fOutDumpBOOL(fOut, pSpawn->bHideCorpse);
			fOutDumpCHAR(fOut, pSpawn->AssistName);
			fOutDumpBOOL(fOut, pSpawn->InvitedToGroup);
			fOutDumpNUM(fOut, pSpawn->GroupMemberTargeted);
			fOutDumpBOOL(fOut, pSpawn->bRemovalPending);
			fOutDumpFLOAT(fOut, pSpawn->EmitterScalingRadius);
			fOutDumpNUM(fOut, pSpawn->DefaultEmitterID);
			fOutDumpBOOL(fOut, pSpawn->bDisplayNameSprite);
			fOutDumpBOOL(fOut, pSpawn->bIdleAnimationOff);
			fOutDumpBOOL(fOut, pSpawn->bIsInteractiveObject);
			fOutDumpCHAR(fOut, (PCHAR)pSpawn->InteractiveObjectModelName);
			fOutDumpCHAR(fOut, (PCHAR)pSpawn->InteractiveObjectOtherName);
			fOutDumpCHAR(fOut, (PCHAR)pSpawn->InteractiveObjectName);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.Y);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.X);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.Z);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.SpeedY);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.SpeedX);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.SpeedZ);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.SpeedRun);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.Heading);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.Angle);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.AccelAngle);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.SpeedHeading);
			fOutDumpFLOAT(fOut, pSpawn->PhysicsBeforeLastPort.CameraAngle);
			fOutDumpNUM(fOut, pSpawn->notsure);
			fOutDumpNUM(fOut, pSpawn->Fellowship.Version);
			fOutDumpNUM(fOut, pSpawn->Fellowship.Version2);
			fOutDumpNUM(fOut, pSpawn->Fellowship.Version3);
			fOutDumpNUM(fOut, pSpawn->Fellowship.Version4);
			fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipID);
			fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipGUID.UniqueEntityID);
			fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipGUID.WorldUniqueID);
			fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipGUID.Reserved);
			fOutDumpCHAR(fOut, pSpawn->Fellowship.Leader);
			fOutDumpCHAR(fOut, pSpawn->Fellowship.MotD);
			fOutDumpNUM(fOut, pSpawn->Fellowship.Members);
			for (int i = 0; i < MAX_FELLOWSHIP_MEMBERS; i++)
			{
				fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipMember[i].UniqueEntityID.UniqueEntityID);
				fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipMember[i].UniqueEntityID.WorldUniqueID);
				fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipMember[i].UniqueEntityID.Reserved);
				fOutDumpCHAR(fOut, pSpawn->Fellowship.FellowshipMember[i].Name);
				fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipMember[i].ZoneID);
				fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipMember[i].Level);
				fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipMember[i].Class);
				fOutDumpNUM(fOut, pSpawn->Fellowship.FellowshipMember[i].LastOn);
				fOutDumpBOOL(fOut, pSpawn->Fellowship.bExpSharingEnabled[i]);
				fOutDumpBOOL(fOut, pSpawn->Fellowship.bSharedExpCapped[i]);
			}
			fOutDumpNUM(fOut, pSpawn->Fellowship.Sync);
			fOutDumpFLOAT(fOut, pSpawn->CampfireY);
			fOutDumpFLOAT(fOut, pSpawn->CampfireX);
			fOutDumpFLOAT(fOut, pSpawn->CampfireZ);
			fOutDumpNUM(fOut, pSpawn->CampfireZoneID);
			fOutDumpNUM(fOut, pSpawn->CampfireTimestamp);
			fOutDumpNUM(fOut, pSpawn->CampfireTimestamp2);
			fOutDumpNUM(fOut, pSpawn->FellowShipID);
			fOutDumpNUM(fOut, pSpawn->FellowShipID2);
			fOutDumpNUM(fOut, pSpawn->CampType);
			fOutDumpBOOL(fOut, pSpawn->Campfire);
			fOutDumpNUM(fOut, (int)pSpawn->SeeInvis[0]);
			fOutDumpNUM(fOut, (int)pSpawn->SeeInvis[1]);
			fOutDumpNUM(fOut, (int)pSpawn->SeeInvis[2]);
			fOutDumpNUM(fOut, pSpawn->Equipment.Head.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Head.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Head.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Head.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Head.NewArmorType);
			fOutDumpNUM(fOut, pSpawn->Equipment.Chest.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Chest.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Chest.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Chest.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Chest.NewArmorType);
			fOutDumpNUM(fOut, pSpawn->Equipment.Arms.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Arms.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Arms.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Arms.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Arms.NewArmorType);
			fOutDumpNUM(fOut, pSpawn->Equipment.Wrists.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Wrists.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Wrists.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Wrists.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Wrists.NewArmorType);
			fOutDumpNUM(fOut, pSpawn->Equipment.Hands.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Hands.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Hands.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Hands.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Hands.NewArmorType);
			fOutDumpNUM(fOut, pSpawn->Equipment.Legs.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Legs.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Legs.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Legs.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Legs.NewArmorType);
			fOutDumpNUM(fOut, pSpawn->Equipment.Feet.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Feet.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Feet.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Feet.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Feet.NewArmorType);
			fOutDumpNUM(fOut, pSpawn->Equipment.Primary.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Primary.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Primary.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Primary.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Primary.NewArmorType);
			fOutDumpNUM(fOut, pSpawn->Equipment.Offhand.ID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Offhand.Var);
			fOutDumpNUM(fOut, pSpawn->Equipment.Offhand.Material);
			fOutDumpNUM(fOut, pSpawn->Equipment.Offhand.NewArmorID);
			fOutDumpNUM(fOut, pSpawn->Equipment.Offhand.NewArmorType);
			fOutDumpBOOL(fOut, pSpawn->bIsPlacingItem);
			fOutDumpBOOL(fOut, pSpawn->bGMCreatedNPC);
			fOutDumpNUM(fOut, pSpawn->ObjectAnimationID);
			fOutDumpBOOL(fOut, pSpawn->bInteractiveObjectCollidable);
			fOutDumpNUM(fOut, pSpawn->InteractiveObjectType);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[0]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[1]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[2]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[3]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[4]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[5]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[6]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[7]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[8]);
			fOutDumpNUM(fOut, pSpawn->SoundIDs[9]);
			fOutDumpCHAR(fOut, ""); // the old bard members. Never really needed, but don't want to break the schema
			fOutDumpNUM(fOut, 0);
			fOutDumpNUM(fOut, 0);
			fOutDumpNUM(fOut, pSpawn->SpawnStatus[0]);
			fOutDumpNUM(fOut, pSpawn->SpawnStatus[1]);
			fOutDumpNUM(fOut, pSpawn->SpawnStatus[2]);
			fOutDumpNUM(fOut, pSpawn->SpawnStatus[3]);
			fOutDumpNUM(fOut, pSpawn->SpawnStatus[4]);
			fOutDumpNUM(fOut, pSpawn->SpawnStatus[5]);
			fOutDumpNUM(fOut, pSpawn->BannerIndex0);
			fOutDumpNUM(fOut, pSpawn->BannerIndex1);
			fOutDumpNUM(fOut, pSpawn->BannerTint0.ARGB);
			fOutDumpNUM(fOut, pSpawn->BannerTint1.ARGB);
			fOutDumpNUM(fOut, pSpawn->MountAnimationRelated);
			fOutDumpBOOL(fOut, pSpawn->bGuildShowAnim);
			fOutDumpBOOL(fOut, pSpawn->bWaitingForPort);
			fOutDumpNUM(fOut, (BYTE)pSpawn->mActorClient.TextureType);
			fOutDumpNUM(fOut, (BYTE)pSpawn->mActorClient.Material);
			fOutDumpNUM(fOut, (BYTE)pSpawn->mActorClient.Variation);
			fOutDumpNUM(fOut, (BYTE)pSpawn->mActorClient.HeadType);
			fOutDumpNUM(fOut, pSpawn->mActorClient.FaceStyle);
			fOutDumpNUM(fOut, pSpawn->mActorClient.HairColor);
			fOutDumpNUM(fOut, pSpawn->mActorClient.FacialHairColor);
			fOutDumpNUM(fOut, pSpawn->mActorClient.EyeColor1);
			fOutDumpNUM(fOut, pSpawn->mActorClient.EyeColor2);
			fOutDumpNUM(fOut, pSpawn->mActorClient.HairStyle);
			fOutDumpNUM(fOut, pSpawn->mActorClient.FacialHair);
			fOutDumpNUM(fOut, pSpawn->mActorClient.Race);
			fOutDumpNUM(fOut, pSpawn->mActorClient.RaceOverride);
			fOutDumpNUM(fOut, pSpawn->mActorClient.Class);
			fOutDumpNUM(fOut, pSpawn->mActorClient.Gender);
			fOutDumpCHAR(fOut, pSpawn->mActorClient.ActorDef);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[0]);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[1]);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[2]);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[3]);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[4]);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[5]);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[6]);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[7]);
			fOutDumpNUM(fOut, pSpawn->mActorClient.ArmorColor[8]);
			fOutDumpBOOL(fOut, pSpawn->mActorClient.bShowHelm);
			fOutDumpNUM(fOut, pSpawn->mActorClient.Heritage);
			fOutDumpNUM(fOut, pSpawn->mActorClient.Tattoo);
			fOutDumpNUM(fOut, pSpawn->mActorClient.Details);
			fOutDumpNUM(fOut, pSpawn->mActorClient.LeftEyeMaterialIndex);
			fOutDumpNUM(fOut, pSpawn->mActorClient.RightEyeMaterialIndex);
			fOutDumpBOOL(fOut, pSpawn->mActorClient.bActiveTransition);
			fOutDumpNUM(fOut, pSpawn->mActorClient.CurrentStage);
			fOutDumpFLOAT(fOut, pSpawn->mActorClient.ZOffset);
			fOutDumpFLOAT(fOut, pSpawn->mActorClient.AdjustedLoc.Y);
			fOutDumpFLOAT(fOut, pSpawn->mActorClient.AdjustedLoc.X);
			fOutDumpFLOAT(fOut, pSpawn->mActorClient.AdjustedLoc.Z);
			fOutDumpBOOL(fOut, pSpawn->mActorClient.bReplacedStaticObject);
			fOutDumpNUM(fOut, pSpawn->mActorClient.PartialFaceNumber);
			fOutDumpBOOL(fOut, pSpawn->mActorClient.bNewArmorDisabled);
			fOutDumpCHAR(fOut, pSpawn->GetClassString());
			fOutDumpNUM(fOut, pSpawn->GetId());
			fOutDumpFLOAT(fOut, pSpawn->RunSpeed);
			fOutDumpNUM(fOut, pSpawn->HPMax);
			fOutDumpNUM(fOut, pSpawn->CharClass);
			fOutDumpNUM(fOut, pSpawn->WarCry);
			fOutDumpNUM(fOut, pSpawn->Deity);
			fOutDumpFLOAT(fOut, pSpawn->MyWalkSpeed);
			fOutDumpFLOAT(fOut, pSpawn->GetMeleeRangeVar1);
			fOutDumpNUM(fOut, pSpawn->FindBits);
			fOutDumpCHAR(fOut, pSpawn->Title);
			fOutDumpNUM(fOut, pSpawn->Level);
			fOutDumpNUM(fOut, pSpawn->Light, TAD_EOL);

			//Struct Verification
			// spot check a few data elements at the end
			// of the struct to predict any alignment issues
			if (pSpawn->WalkSpeed < 0 || pSpawn->WalkSpeed > 10 ||
				pSpawn->Level < 1 || pSpawn->Level > 300 ||
				pSpawn->mActorClient.Race < 0 || pSpawn->mActorClient.Race > 3000)
			{
				alignmentError = true;
			}

			pSpawn = pSpawn->pNext;
		}

		//close the file
		fCloseDump(fOut);

		//notify if theres any issues
		if (alignmentError)
		{
			WriteChatf("\ar[MQ2TakeADump] ALIGNMENT ISSUE: There may be an issue with PSPAWNINFO, please examine your NPC dumps for accuracy.\ax");
		}
	}
}

//dump the current zones data out to the CSV
VOID dumpZone()
{
	FILE *fOut = NULL;
	BOOL alignmentError = false;

	//open the door dump for output
	if (fOpenDump(&fOut, "Zone"))
	{
		//headers
		fOutDumpCHAR(fOut, "ShortName");
		fOutDumpCHAR(fOut, "LongName");
		fOutDumpCHAR(fOut, "WeatherType");
		fOutDumpCHAR(fOut, "WeatherTypeOverride");
		fOutDumpCHAR(fOut, "SkyType");
		fOutDumpCHAR(fOut, "SkyTypeOverride");
		fOutDumpCHAR(fOut, "ZoneType");
		fOutDumpCHAR(fOut, "ZoneID");
		fOutDumpCHAR(fOut, "ZoneExpModifier");
		fOutDumpCHAR(fOut, "GroupLvlExpRelated");
		fOutDumpCHAR(fOut, "FilterID");
		fOutDumpCHAR(fOut, "Unknown1");
		fOutDumpCHAR(fOut, "FogDensity");
		fOutDumpCHAR(fOut, "FogStart[0]");
		fOutDumpCHAR(fOut, "FogStart[1]");
		fOutDumpCHAR(fOut, "FogStart[2]");
		fOutDumpCHAR(fOut, "FogStart[3]");
		fOutDumpCHAR(fOut, "FogEnd[0]");
		fOutDumpCHAR(fOut, "FogEnd[1]");
		fOutDumpCHAR(fOut, "FogEnd[2]");
		fOutDumpCHAR(fOut, "FogEnd[3]");
		fOutDumpCHAR(fOut, "FogRed[0]");
		fOutDumpCHAR(fOut, "FogRed[1]");
		fOutDumpCHAR(fOut, "FogRed[2]");
		fOutDumpCHAR(fOut, "FogRed[3]");
		fOutDumpCHAR(fOut, "FogGreen[0]");
		fOutDumpCHAR(fOut, "FogGreen[1]");
		fOutDumpCHAR(fOut, "FogGreen[2]");
		fOutDumpCHAR(fOut, "FogGreen[3]");
		fOutDumpCHAR(fOut, "FogBlue[0]");
		fOutDumpCHAR(fOut, "FogBlue[1]");
		fOutDumpCHAR(fOut, "FogBlue[2]");
		fOutDumpCHAR(fOut, "FogBlue[3]");
		fOutDumpCHAR(fOut, "RainChance[0]");
		fOutDumpCHAR(fOut, "RainChance[1]");
		fOutDumpCHAR(fOut, "RainChance[2]");
		fOutDumpCHAR(fOut, "RainChance[3]");
		fOutDumpCHAR(fOut, "RainDuration[0]");
		fOutDumpCHAR(fOut, "RainDuration[1]");
		fOutDumpCHAR(fOut, "RainDuration[2]");
		fOutDumpCHAR(fOut, "RainDuration[3]");
		fOutDumpCHAR(fOut, "SnowChance[0]");
		fOutDumpCHAR(fOut, "SnowChance[1]");
		fOutDumpCHAR(fOut, "SnowChance[2]");
		fOutDumpCHAR(fOut, "SnowChance[3]");
		fOutDumpCHAR(fOut, "SnowDuration[0]");
		fOutDumpCHAR(fOut, "SnowDuration[1]");
		fOutDumpCHAR(fOut, "SnowDuration[2]");
		fOutDumpCHAR(fOut, "SnowDuration[3]");
		fOutDumpCHAR(fOut, "PrecipitationType");
		fOutDumpCHAR(fOut, "BloomIntensity");
		fOutDumpCHAR(fOut, "ZoneGravity");
		fOutDumpCHAR(fOut, "LavaDamage");
		fOutDumpCHAR(fOut, "MinLavaDamage");
		fOutDumpCHAR(fOut, "TimeStringID");
		fOutDumpCHAR(fOut, "Unknown3");
		fOutDumpCHAR(fOut, "SkyLock");
		fOutDumpCHAR(fOut, "SkyLockOverride");
		fOutDumpCHAR(fOut, "SafeYLoc");
		fOutDumpCHAR(fOut, "SafeXLoc");
		fOutDumpCHAR(fOut, "SafeZLoc");
		fOutDumpCHAR(fOut, "SafeHeading");
		fOutDumpCHAR(fOut, "Ceiling");
		fOutDumpCHAR(fOut, "Floor");
		fOutDumpCHAR(fOut, "MinClip");
		fOutDumpCHAR(fOut, "MaxClip");
		fOutDumpCHAR(fOut, "FallThroughWorldTeleportID");
		fOutDumpCHAR(fOut, "Unknown4");
		fOutDumpCHAR(fOut, "ScriptIDHour");
		fOutDumpCHAR(fOut, "ScriptIDMinute");
		fOutDumpCHAR(fOut, "ScriptIDTick");
		fOutDumpCHAR(fOut, "ScriptIDOnPlayerDeath");
		fOutDumpCHAR(fOut, "ScriptIDOnNPCDeath");
		fOutDumpCHAR(fOut, "ScriptIDPlayerEnteringZone");
		fOutDumpCHAR(fOut, "ScriptIDOnZonePop");
		fOutDumpCHAR(fOut, "ScriptIDNPCLoot");
		fOutDumpCHAR(fOut, "Unknown4b");
		fOutDumpCHAR(fOut, "ScriptIDOnFishing");
		fOutDumpCHAR(fOut, "ScriptIDOnForage");
		fOutDumpCHAR(fOut, "Unknown4c");
		fOutDumpCHAR(fOut, "NPCAgroMaxDist");
		fOutDumpCHAR(fOut, "ForageLow");
		fOutDumpCHAR(fOut, "ForageMedium");
		fOutDumpCHAR(fOut, "ForageHigh");
		fOutDumpCHAR(fOut, "ForageSpecial");
		fOutDumpCHAR(fOut, "FishingLow");
		fOutDumpCHAR(fOut, "FishingMedium");
		fOutDumpCHAR(fOut, "FishingHigh");
		fOutDumpCHAR(fOut, "FishingRelated");
		fOutDumpCHAR(fOut, "CanPlaceCampsite");
		fOutDumpCHAR(fOut, "CanPlaceGuildBanner");
		fOutDumpCHAR(fOut, "Unknown4d");
		fOutDumpCHAR(fOut, "FastRegenHP");
		fOutDumpCHAR(fOut, "FastRegenMana");
		fOutDumpCHAR(fOut, "FastRegenEndurance");
		fOutDumpCHAR(fOut, "NewEngineZone");
		fOutDumpCHAR(fOut, "SkyEnabled");
		fOutDumpCHAR(fOut, "FogOnOff");
		fOutDumpCHAR(fOut, "ClimateType");
		fOutDumpCHAR(fOut, "bNoPlayerLight");
		fOutDumpCHAR(fOut, "bUnknown5");
		fOutDumpCHAR(fOut, "bNoAttack");
		fOutDumpCHAR(fOut, "bAllowPVP");
		fOutDumpCHAR(fOut, "bNoEncumber");
		fOutDumpCHAR(fOut, "bUnknowns6[0]");
		fOutDumpCHAR(fOut, "bUnknowns6[1]");
		fOutDumpCHAR(fOut, "bUnknowns6[2]");
		fOutDumpCHAR(fOut, "bNoLevitate");
		fOutDumpCHAR(fOut, "bNoBuffExpiration");
		fOutDumpCHAR(fOut, "bDisallowManaStone");
		fOutDumpCHAR(fOut, "bNoBind");
		fOutDumpCHAR(fOut, "bNoCallOfTheHero");
		fOutDumpCHAR(fOut, "bUnknown8");
		fOutDumpCHAR(fOut, "bNoFear");
		fOutDumpCHAR(fOut, "bUnknown9, TAD_EOL"); // end of line

                                                  //data types
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "BYTE");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "UINT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool");
		fOutDumpCHAR(fOut, "bool", TAD_EOL); // end of line

		fOutDumpCHAR(fOut, pZoneInfo->ShortName);
		fOutDumpCHAR(fOut, pZoneInfo->LongName);
		fOutDumpCHAR(fOut, pZoneInfo->WeatherType);
		fOutDumpCHAR(fOut, pZoneInfo->WeatherTypeOverride);
		fOutDumpCHAR(fOut, pZoneInfo->SkyType);
		fOutDumpCHAR(fOut, pZoneInfo->SkyTypeOverride);
		fOutDumpNUM(fOut, static_cast<uint32_t>(pZoneInfo->ZoneType));
		fOutDumpNUM(fOut, pZoneInfo->ZoneID);
		fOutDumpFLOAT(fOut, pZoneInfo->ZoneExpModifier);
		fOutDumpNUM(fOut, pZoneInfo->GroupLvlExpRelated);
		fOutDumpNUM(fOut, pZoneInfo->FilterID);
		fOutDumpNUM(fOut, pZoneInfo->Unknown1);
		fOutDumpFLOAT(fOut, pZoneInfo->FogDensity);
		fOutDumpFLOAT(fOut, pZoneInfo->FogStart[0]);
		fOutDumpFLOAT(fOut, pZoneInfo->FogStart[1]);
		fOutDumpFLOAT(fOut, pZoneInfo->FogStart[2]);
		fOutDumpFLOAT(fOut, pZoneInfo->FogStart[3]);
		fOutDumpFLOAT(fOut, pZoneInfo->FogEnd[0]);
		fOutDumpFLOAT(fOut, pZoneInfo->FogEnd[1]);
		fOutDumpFLOAT(fOut, pZoneInfo->FogEnd[2]);
		fOutDumpFLOAT(fOut, pZoneInfo->FogEnd[3]);
		fOutDumpNUM(fOut, pZoneInfo->FogRed[0]);
		fOutDumpNUM(fOut, pZoneInfo->FogRed[1]);
		fOutDumpNUM(fOut, pZoneInfo->FogRed[2]);
		fOutDumpNUM(fOut, pZoneInfo->FogRed[3]);
		fOutDumpNUM(fOut, pZoneInfo->FogGreen[0]);
		fOutDumpNUM(fOut, pZoneInfo->FogGreen[1]);
		fOutDumpNUM(fOut, pZoneInfo->FogGreen[2]);
		fOutDumpNUM(fOut, pZoneInfo->FogGreen[3]);
		fOutDumpNUM(fOut, pZoneInfo->FogBlue[0]);
		fOutDumpNUM(fOut, pZoneInfo->FogBlue[1]);
		fOutDumpNUM(fOut, pZoneInfo->FogBlue[2]);
		fOutDumpNUM(fOut, pZoneInfo->FogBlue[3]);
		fOutDumpNUM(fOut, pZoneInfo->RainChance[0]);
		fOutDumpNUM(fOut, pZoneInfo->RainChance[1]);
		fOutDumpNUM(fOut, pZoneInfo->RainChance[2]);
		fOutDumpNUM(fOut, pZoneInfo->RainChance[3]);
		fOutDumpNUM(fOut, pZoneInfo->RainDuration[0]);
		fOutDumpNUM(fOut, pZoneInfo->RainDuration[1]);
		fOutDumpNUM(fOut, pZoneInfo->RainDuration[2]);
		fOutDumpNUM(fOut, pZoneInfo->RainDuration[3]);
		fOutDumpNUM(fOut, pZoneInfo->SnowChance[0]);
		fOutDumpNUM(fOut, pZoneInfo->SnowChance[1]);
		fOutDumpNUM(fOut, pZoneInfo->SnowChance[2]);
		fOutDumpNUM(fOut, pZoneInfo->SnowChance[3]);
		fOutDumpNUM(fOut, pZoneInfo->SnowDuration[0]);
		fOutDumpNUM(fOut, pZoneInfo->SnowDuration[1]);
		fOutDumpNUM(fOut, pZoneInfo->SnowDuration[2]);
		fOutDumpNUM(fOut, pZoneInfo->SnowDuration[3]);
		fOutDumpNUM(fOut, pZoneInfo->PrecipitationType);
		fOutDumpFLOAT(fOut, pZoneInfo->BloomIntensity);
		fOutDumpFLOAT(fOut, pZoneInfo->ZoneGravity);
		fOutDumpNUM(fOut, pZoneInfo->LavaDamage);
		fOutDumpNUM(fOut, pZoneInfo->MinLavaDamage);
		fOutDumpNUM(fOut, pZoneInfo->TimeStringID);
		fOutDumpNUM(fOut, pZoneInfo->Unknown3);
		fOutDumpNUM(fOut, pZoneInfo->SkyLock);
		fOutDumpNUM(fOut, pZoneInfo->SkyLockOverride);
		fOutDumpFLOAT(fOut, pZoneInfo->SafeYLoc);
		fOutDumpFLOAT(fOut, pZoneInfo->SafeXLoc);
		fOutDumpFLOAT(fOut, pZoneInfo->SafeZLoc);
		fOutDumpFLOAT(fOut, pZoneInfo->SafeHeading);
		fOutDumpFLOAT(fOut, pZoneInfo->Ceiling);
		fOutDumpFLOAT(fOut, pZoneInfo->Floor);
		fOutDumpFLOAT(fOut, pZoneInfo->MinClip);
		fOutDumpFLOAT(fOut, pZoneInfo->MaxClip);
		fOutDumpNUM(fOut, pZoneInfo->FallThroughWorldTeleportID);
		fOutDumpNUM(fOut, pZoneInfo->Unknown4);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDHour);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDMinute);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDTick);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDOnPlayerDeath);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDOnNPCDeath);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDPlayerEnteringZone);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDOnZonePop);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDNPCLoot);
		fOutDumpNUM(fOut, pZoneInfo->Unknown4b);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDOnFishing);
		fOutDumpNUM(fOut, pZoneInfo->ScriptIDOnForage);
		fOutDumpNUM(fOut, pZoneInfo->Unknown4c);
		fOutDumpNUM(fOut, pZoneInfo->NPCAgroMaxDist);
		fOutDumpNUM(fOut, pZoneInfo->ForageLow);
		fOutDumpNUM(fOut, pZoneInfo->ForageMedium);
		fOutDumpNUM(fOut, pZoneInfo->ForageHigh);
		fOutDumpNUM(fOut, pZoneInfo->ForageSpecial);
		fOutDumpNUM(fOut, pZoneInfo->FishingLow);
		fOutDumpNUM(fOut, pZoneInfo->FishingMedium);
		fOutDumpNUM(fOut, pZoneInfo->FishingHigh);
		fOutDumpNUM(fOut, pZoneInfo->FishingRelated);
		fOutDumpNUM(fOut, static_cast<uint8_t>(pZoneInfo->CanPlaceCampsite));
		fOutDumpNUM(fOut, static_cast<uint8_t>(pZoneInfo->CanPlaceGuildBanner));
		fOutDumpNUM(fOut, pZoneInfo->Unknown4d);
		fOutDumpNUM(fOut, pZoneInfo->FastRegenHP);
		fOutDumpNUM(fOut, pZoneInfo->FastRegenMana);
		fOutDumpNUM(fOut, pZoneInfo->FastRegenEndurance);
		fOutDumpBOOL(fOut, pZoneInfo->NewEngineZone);
		fOutDumpBOOL(fOut, pZoneInfo->SkyEnabled);
		fOutDumpBOOL(fOut, pZoneInfo->FogOnOff);
		fOutDumpBOOL(fOut, pZoneInfo->ClimateType);
		fOutDumpBOOL(fOut, pZoneInfo->bNoPlayerLight);
		fOutDumpBOOL(fOut, pZoneInfo->bUnknown5);
		fOutDumpBOOL(fOut, pZoneInfo->bNoAttack);
		fOutDumpBOOL(fOut, pZoneInfo->bAllowPVP);
		fOutDumpBOOL(fOut, pZoneInfo->bNoEncumber);
		fOutDumpBOOL(fOut, pZoneInfo->bUnknowns6[0]);
		fOutDumpBOOL(fOut, pZoneInfo->bUnknowns6[1]);
		fOutDumpBOOL(fOut, pZoneInfo->bUnknowns6[2]);
		fOutDumpBOOL(fOut, pZoneInfo->bNoLevitate);
		fOutDumpBOOL(fOut, pZoneInfo->bNoBuffExpiration);
		fOutDumpBOOL(fOut, pZoneInfo->bDisallowManaStone);
		fOutDumpBOOL(fOut, pZoneInfo->bNoBind);
		fOutDumpBOOL(fOut, pZoneInfo->bNoCallOfTheHero);
		fOutDumpBOOL(fOut, pZoneInfo->bUnknown8);
		fOutDumpBOOL(fOut, pZoneInfo->bNoFear);
		fOutDumpBOOL(fOut, pZoneInfo->bUnknown9, TAD_EOL); // end of line

		//Struct Verification
		// spot check a few data elements at the end
		// of the struct to predict any alignment issues
		if (pZoneInfo->FogDensity < 0 || pZoneInfo->FogDensity > 1)
		{
			alignmentError = true;
		}

		//close the file
		fCloseDump(fOut);

		//notify if theres any issues
		if (alignmentError)
		{
			WriteChatf("\ar[MQ2TakeADump] ALIGNMENT ISSUE: There may be an issue with PZONEINFO, please examine your zone dumps for accuracy.\ax");
		}
	}
}

//dump all the zone points to the CSV
VOID dumpZonePoint()
{
	FILE *fOut = NULL;
	BOOL alignmentError = false;

	//open the door dump for output
	if (fOpenDump(&fOut, "ZonePoint"))
	{
		//headers
		fOutDumpCHAR(fOut, "this ZoneID");
		fOutDumpCHAR(fOut, "Index");
		fOutDumpCHAR(fOut, "Y");
		fOutDumpCHAR(fOut, "X");
		fOutDumpCHAR(fOut, "Z");
		fOutDumpCHAR(fOut, "Heading");
		fOutDumpCHAR(fOut, "Target ZoneId");
		fOutDumpCHAR(fOut, "FilterID");
		fOutDumpCHAR(fOut, "VehicleID", TAD_EOL); //end of line

		//data types
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "FLOAT");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "int");
		fOutDumpCHAR(fOut, "UINT", TAD_EOL); //end of line

		DWORD ZoneID = (DWORD)((PZONEINFO)pZoneInfo)->ZoneID;
		DWORD tp_tablesize = *(DWORD*)Teleport_Table_Size;
		tp_coords* tp = (tp_coords*)Teleport_Table;
		for (unsigned int i = 0; i < tp_tablesize; ++i)
		{
			fOutDumpNUM(fOut, ZoneID); //not part of the EQ object but its needed in the zone points table
			fOutDumpNUM(fOut, tp[i].Index);
			fOutDumpFLOAT(fOut, tp[i].Y);
			fOutDumpFLOAT(fOut, tp[i].X);
			fOutDumpFLOAT(fOut, tp[i].Z);
			fOutDumpFLOAT(fOut, tp[i].Heading);
			fOutDumpNUM(fOut, tp[i].ZoneId);
			fOutDumpNUM(fOut, tp[i].FilterID);
			fOutDumpNUM(fOut, tp[i].VehicleID, TAD_EOL);

			//Struct Verification
			// spot check a few data elements at the end
			// of the struct to predict any alignment issues
			if (tp[i].ZoneId < -1 || tp[i].ZoneId > 10000)
			{
				alignmentError = true;
			}
		}

		//close the file
		fCloseDump(fOut);

		//notify if theres any issues
		if (alignmentError)
		{
			WriteChatf("\ar[MQ2TakeADump] ALIGNMENT ISSUE: There may be an issue with Ptp_coords, please examine your zonepoint dumps for accuracy.\ax");
		}
	}
}

//finish the dump for target
VOID dumpTargetEnd()
{
	//bail if its not open
	if (!fTargetOut) return;

	//close it
	fCloseDump(fTargetOut);

	WriteChatColor("\ao[MQ2TakeADump]\ax Target dump completed.", 10);
	fTargetOut = NULL;
	LastHeading = 0;
	FirstHeading = 0;
	FirstX = 0;
	FirstY = 0;
	FirstZ = 0;
	MovedFromStart = false;
}

//dump a target row the the csv
VOID dumpTargetRow(PCHAR szNote)
{

	struct tm newtime;
	__time32_t aclock;
	CHAR szTime[80] = { 0 };


	//if theres no file skip it
	if (!fTargetOut) return;

	//if theres no target end the file
	if (!pTarget)
	{
		//if there is already a path being taken, close it out
		dumpTargetEnd();
		return;
	}

	//get local time stamp
	_time32(&aclock);
	_localtime32_s(&newtime, &aclock);
	strftime(szTime, sizeof(szTime), "%Y%m%d %H:%M:%S", &newtime);

	//cast the target
	PSPAWNINFO pDumpTarget = (PSPAWNINFO)pTarget;

	if (strlen(pDumpTarget->AssistName) == 0)
		fOutDumpCHAR(fTargetOut, szNote); //use the note if no aggro
	else
		fOutDumpCHAR(fTargetOut, "aggro"); //use "aggro" if aggroed

	fOutDumpFLOAT(fTargetOut, pDumpTarget->Y);
	fOutDumpFLOAT(fTargetOut, pDumpTarget->X);
	fOutDumpFLOAT(fTargetOut, pDumpTarget->Z);
	fOutDumpFLOAT(fTargetOut, pDumpTarget->Heading);
	fOutDumpCHAR(fTargetOut, pDumpTarget->Name);
	fOutDumpNUM(fTargetOut, pDumpTarget->LastUpdateReceivedTime);
	fOutDumpCHAR(fTargetOut, szTime, TAD_EOL);

	LastHeading = pDumpTarget->Heading;
}

//dump all the pathing locations for your target
VOID dumpTargetBegin()
{
	CHAR szName[MAX_STRING] = { 0 };

	if (!pTarget)
	{
		WriteChatColor("\ao[MQ2TakeADump]\ax No target to record pathing.", 10);
		return;
	}

	//cast the target
	PSPAWNINFO pDumpTarget = (PSPAWNINFO)pTarget;

	//if there is already a path being taken, close it out
	dumpTargetEnd();

	//filename
	sprintf_s(szName, MAX_STRING, "Path_%s", pDumpTarget->Name);

	//open the door dump for output
	if (fOpenDump(&fTargetOut, szName))
	{
		//headers
		fOutDumpCHAR(fTargetOut, "Note");
		fOutDumpCHAR(fTargetOut, "Y");
		fOutDumpCHAR(fTargetOut, "X");
		fOutDumpCHAR(fTargetOut, "Z");
		fOutDumpCHAR(fTargetOut, "Heading");
		fOutDumpCHAR(fTargetOut, "Name");
		fOutDumpCHAR(fTargetOut, "LastUpdateReceivedTime (miliseconds since server update)");
		fOutDumpCHAR(fTargetOut, "Time", TAD_EOL); //end of line

		//data types
		fOutDumpCHAR(fTargetOut, "CHAR");
		fOutDumpCHAR(fTargetOut, "FLOAT");
		fOutDumpCHAR(fTargetOut, "FLOAT");
		fOutDumpCHAR(fTargetOut, "FLOAT");
		fOutDumpCHAR(fTargetOut, "FLOAT");
		fOutDumpCHAR(fTargetOut, "CHAR");
		fOutDumpCHAR(fTargetOut, "UINT");
		fOutDumpCHAR(fTargetOut, "CHAR", TAD_EOL); //end of line

		//cache the first coord for detecting when they
		//return to the start
		PSPAWNINFO pDumpTarget = (PSPAWNINFO)pTarget;
		FirstY = pDumpTarget->Y;
		FirstX = pDumpTarget->X;
		FirstZ = pDumpTarget->Z;
		FirstHeading = pDumpTarget->Heading;

		//dump the initial row
		dumpTargetRow("initial");
	}
}

//loops through every item in the OPEN merchant window and dumps them to the CSV
VOID dumpMerchantWin()
{
	CHAR szName[MAX_STRING] = { 0 };
	FILE *fOut = NULL;
	BOOL alignmentError = false;
	UINT curID = 0;

	if (!pMerchantWnd || !pMerchantWnd->IsVisible())
	{
		WriteChatColor("\ao[MQ2TakeADump]\ax No merchant window open.", 10);
		return;
	}

	FLOAT mwMarkup = pMerchantWnd->MerchantGreed;

	//get merchant name from the window itself
	fmt::format_to(szName, "MerchantWnd_");
	if (auto *nameLabel = pMerchantWnd->GetChildItem("MW_MerchantName"))
		fmt::format_to(szName, "{}", nameLabel->GetWindowText());

	//open the dump for output
	if (fOpenDump(&fOut, szName))
	{
		//headers
		fOutDumpCHAR(fOut, "ItemID");
		fOutDumpCHAR(fOut, "ItemName");
		fOutDumpCHAR(fOut, "Markup", TAD_EOL);

		//data type headers
		fOutDumpCHAR(fOut, "DWORD");
		fOutDumpCHAR(fOut, "CHAR");
		fOutDumpCHAR(fOut, "FLOAT", TAD_EOL); //end of line

		//loop through the items in the merchant window
		auto& page = pMerchantWnd->PageHandlers[RegularMerchantPage];
		for (int i = 0; i < page->GetItemCount(); ++i)
		{
			ItemPtr ptr = page->GetItem(i);
			if (ptr)
			{
				curID = ptr->ID;
				fOutDumpNUM(fOut, curID);
				fOutDumpCHAR(fOut, ptr->GetName());
				fOutDumpFLOAT(fOut, mwMarkup, TAD_EOL);

				//Struct Verification
				// spot check a few data elements at the end
				// of the struct to predict any alignment issues
				if (curID < 1001 || curID > 200000)
					alignmentError = true;
			}
		}

		//close the file
		fCloseDump(fOut);

		//notify if theres any issues
		if (alignmentError)
		{
			WriteChatf("\ar[MQ2TakeADump] ALIGNMENT ISSUE: There may be an issue with the merchant window, please examine your merchant dumps for accuracy.\ax");
		}
	}
}



///////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                   //
//                                              COMMANDS                                             //
//                                                                                                   //
///////////////////////////////////////////////////////////////////////////////////////////////////////



// the /takeadump command to actuate the dump, by default it dumps every object type
// or you can specify door, ground, npc, myzone, zonepoint
VOID cmdDump(PSPAWNINFO pChar, PCHAR szLine)
{
	CHAR szTemp[MAX_STRING] = { 0 };

	if (strlen(szLine) == 0 || !_strnicmp(szLine, "all", 3))
	{
		dumpDoor();
		dumpGroundItem();
		dumpObjects();
		dumpNPCType();
		dumpZone();
		dumpZonePoint();
		//dumpTargetBegin(); //target output for "all" is annoying
	}
	else if (!_strnicmp(szLine, "door", 4))
	{
		dumpDoor();
	}
	else if (!_strnicmp(szLine, "ground", 5))
	{
		dumpGroundItem();
	}
	else if (!_strnicmp(szLine, "object", 6))
	{
		dumpObjects();
	}
	else if (!_strnicmp(szLine, "npc", 3))
	{
		dumpNPCType();
	}
	else if (!_strnicmp(szLine, "myzone", 6))
	{
		dumpZone();
	}
	else if (!_strnicmp(szLine, "zonep", 5))
	{
		dumpZonePoint();
	}
	else if (!_strnicmp(szLine, "target", 6))
	{
		dumpTargetBegin();
	}
	else if (!_strnicmp(szLine, "path", 4)) //alias for "target"
	{
		dumpTargetBegin();
	}
	else if (!_strnicmp(szLine, "merch", 5))
	{
		dumpMerchantWin();
	}
	else if (!_strnicmp(szLine, "ts", 2)) //tstart (start the timer)
	{
		TimerPaused = 0;
		TimerStart = (long)clock();
		WriteChatColor("\ao[MQ2TakeADump]\ax Starting Timer.", 10);
		return;
	}
	else if (!_strnicmp(szLine, "tp", 2)) //tpause (pause the timer)
	{
		if (TimerStart == 0) //cant pause if its not running
		{
			WriteChatColor("\ao[MQ2TakeADump]\ax The timer is not running.", 10);
			return;
		}
		TimerPaused = (long)clock() - TimerStart;
		sprintf_s(szTemp, MAX_STRING, "\ao[MQ2TakeADump]\ax Timer Paused %dms", TimerPaused);
		WriteChatColor(szTemp, 10);
		return;
	}
	else if (!_strnicmp(szLine, "tr", 2)) //treset (reset timer)
	{
		TimerPaused = 0;
		TimerStart = 0;
		WriteChatColor("\ao[MQ2TakeADump]\ax Resetting Timer.", 10);
		return;
	}
	else if (!_strnicmp(szLine, "tc", 2)) //tcontinue (unpause the timer)
	{
		if (TimerStart == 0)
		{
			WriteChatColor("\ao[MQ2TakeADump]\ax The timer is not running.", 10);
			return;
		}
		if (TimerPaused == 0)
		{
			WriteChatColor("\ao[MQ2TakeADump]\ax The timer is not paused.", 10);
			return;
		}
		TimerPaused = 0;
		WriteChatColor("\ao[MQ2TakeADump]\ax The timer is now running.", 10);
		return;
	}
	else
	{
		WriteChatColor("\ao[MQ2TakeADump]\ax Proper usage for a dump is \"/takeadump [all|ground|object|door|npc|myzone|zonepoint|target|merchant]\". No parameter dumps functions as an \"all\".", 10);
		WriteChatColor("\ao[MQ2TakeADump]\ax Proper usage for a timer is \"/takeadump tstart|tpause|treset|tcontinue\".", 10);
	}
}



///////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                   //
//                                                TLO                                                //
//                                                                                                   //
///////////////////////////////////////////////////////////////////////////////////////////////////////


class MQ2TADType : public MQ2Type
{
private:
public:
	enum TADMembers
	{
		Seconds = 1,
		SecondsReset = 2,
		Milliseconds = 3,
		MillisecondsReset = 4
	};

	MQ2TADType() :MQ2Type("TAD")
	{
		TypeMember(Seconds);
		TypeMember(SecondsReset);
		TypeMember(Milliseconds);
		TypeMember(MillisecondsReset);
	}

	bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest)
	{
		using namespace datatypes;
		MQTypeMember* pMember = MQ2TADType::FindMember(Member);
		if (pMember) switch ((TADMembers)pMember->ID)
		{
		case Seconds:
			Dest.Int = getTimer() / 1000;
			Dest.Type = pIntType;
			return true;
		case SecondsReset:
			Dest.Int = getTimer() / 1000;
			Dest.Type = pIntType;
			TimerStart = 0;
			TimerPaused = 0;
			return true;
		case Milliseconds:
			Dest.Int = getTimer();
			Dest.Type = pIntType;
			return true;
		case MillisecondsReset:
			Dest.Int = getTimer();
			Dest.Type = pIntType;
			TimerStart = 0;
			TimerPaused = 0;
			return true;
		}
		return FALSE;
	}

	bool ToString(MQVarPtr VarPtr, char* Destination)
	{
		strcpy_s(Destination, MAX_STRING, "TRUE");
		return true;
	}

	~MQ2TADType() { }
};

BOOL dataTAD(const char* zName, MQTypeVar& Dest)
{
	Dest.DWord = 1;
	Dest.Type = pTADType;
	return true;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                   //
//                                              EVENTS                                               //
//                                                                                                   //
///////////////////////////////////////////////////////////////////////////////////////////////////////



//record a path row every pulse, but only if the heading changed
PLUGIN_API VOID OnPulse(VOID)
{
	//if we are recording a target
	if (fTargetOut)
	{
		//end if there is no target
		if (!pTarget)
		{
			dumpTargetEnd();
		}
		else
		{
			//if there is a target and its heading changed, record it
			if (LastHeading != ((PSPAWNINFO)pTarget)->Heading)
			{
				//uncomment to get spam when a row is recorded... kind of annoying
				//WriteChatColor("\ao[MQ2TakeADump]\ax Target heading changed, dumping row.", 10);

				dumpTargetRow("heading change");
			}

			//detecting when they return to the start
			PSPAWNINFO pDumpTarget = (PSPAWNINFO)pTarget;
			if (FirstY == pDumpTarget->Y && FirstX == pDumpTarget->X && FirstZ == pDumpTarget->Z && FirstHeading == pDumpTarget->Heading)
			{
				//only let them know they hit the start if they left it in the first place
				if (MovedFromStart)
				{
					WriteChatColor("\ao[MQ2TakeADump]\ax INITIAL COORDINATES REACHED.", 10);
					dumpTargetRow("looped");
				}
			}
			else
			{
				//if we arent at the start coords mark that we moved
				MovedFromStart = true;
			}
		}
	}
}

PLUGIN_API VOID InitializePlugin(VOID)
{
	//the command to start the dump process
	AddCommand("/takeadump", cmdDump);

	//TLO setup
	pTADType = new MQ2TADType;
	AddMQ2Data("TAD", dataTAD);
}

PLUGIN_API VOID ShutdownPlugin(VOID)
{
	//remove the /takeadump command
	RemoveCommand("/takeadump");

	//remove the TAD TLO
	delete pTADType;
	RemoveMQ2Data("TAD");
}

