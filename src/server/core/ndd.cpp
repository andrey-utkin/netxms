/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2011 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: ndd.cpp
**
**/

#include "nxcore.h"

#define MAX_DEVICE_DRIVERS		1024


/**
 * List of loaded drivers
 */
static int s_numDrivers = 0;
static NetworkDeviceDriver *s_drivers[MAX_DEVICE_DRIVERS];
static NetworkDeviceDriver *s_defaultDriver = new NetworkDeviceDriver();

/**
 * Load device driver
 *
 * @param file Driver's file name
 */
static void LoadDriver(const TCHAR *file)
{
   TCHAR errorText[256];

   HMODULE hModule = DLOpen(file, errorText);
   if (hModule != NULL)
   {
		int *apiVersion = (int *)DLGetSymbolAddr(hModule, _T("nddAPIVersion"), errorText);
      NetworkDeviceDriver *(* CreateInstance)() = (NetworkDeviceDriver *(*)())DLGetSymbolAddr(hModule, _T("nddCreateInstance"), errorText);

      if ((apiVersion != NULL) && (CreateInstance != NULL))
      {
         if (*apiVersion == NDDRV_API_VERSION)
         {
				NetworkDeviceDriver *driver = CreateInstance();
				if (driver != NULL)
				{
					s_drivers[s_numDrivers++] = driver;
					nxlog_write(MSG_NDD_LOADED, EVENTLOG_INFORMATION_TYPE, "s", driver->getName());
				}
				else
				{
					nxlog_write(MSG_NDD_INIT_FAILED, EVENTLOG_ERROR_TYPE, "s", file);
					DLClose(hModule);
				}
         }
         else
         {
            nxlog_write(MSG_NDD_API_VERSION_MISMATCH, EVENTLOG_ERROR_TYPE, "sdd", file, NDDRV_API_VERSION, *apiVersion);
            DLClose(hModule);
         }
      }
      else
      {
         nxlog_write(MSG_NO_NDD_ENTRY_POINT, EVENTLOG_ERROR_TYPE, "s", file);
         DLClose(hModule);
      }
   }
   else
   {
      nxlog_write(MSG_DLOPEN_FAILED, EVENTLOG_ERROR_TYPE, "ss", file, errorText);
   }
}

/**
 * Load all available device drivers
 */
void LoadNetworkDeviceDrivers()
{
	memset(s_drivers, 0, sizeof(NetworkDeviceDriver *) * MAX_DEVICE_DRIVERS);

	TCHAR path[MAX_PATH];
	_tcscpy(path, g_szLibDir);
	_tcscat(path, LDIR_NDD);

	DbgPrintf(1, _T("Loading network device drivers from %s"), path);
#ifdef _WIN32
	SetDllDirectory(path);
#endif
	_TDIR *dir = _topendir(path);
	if (dir != NULL)
	{
		_tcscat(path, FS_PATH_SEPARATOR);
		int insPos = (int)_tcslen(path);

		struct _tdirent *f;
		while((f = _treaddir(dir)) != NULL)
		{
			if (MatchString(_T("*.ndd"), f->d_name, FALSE))
			{
				_tcscpy(&path[insPos], f->d_name);
				LoadDriver(path);
				if (s_numDrivers == MAX_DEVICE_DRIVERS)
					break;	// Too many drivers already loaded
			}
		}
		_tclosedir(dir);
	}
#ifdef _WIN32
	SetDllDirectory(NULL);
#endif
	DbgPrintf(1, _T("%d network device drivers loaded"), s_numDrivers);
}

/**
 * Find appropriate device driver for given node
 *
 * @param node Node object to test
 * @returns Pointer to device driver object
 */
NetworkDeviceDriver *FindDriverForNode(Node *node)
{
	for(int i = 0; i < s_numDrivers; i++)
		if (s_drivers[i]->isDeviceSupported(node->getObjectId()))
			return s_drivers[i];
	return s_defaultDriver;
}
