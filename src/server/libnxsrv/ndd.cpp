/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2011 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: ndd.cpp
**/

#include "libnxsrv.h"
#include <nddrv.h>


//
// Constructor
//

NetworkDeviceDriver::NetworkDeviceDriver()
{
}


//
// Destructor
//

NetworkDeviceDriver::~NetworkDeviceDriver()
{
}

/**
 * Get driver name
 */
const TCHAR *NetworkDeviceDriver::getName()
{
	return _T("GENERIC");
}

/**
 * Get driver version
 */
const TCHAR *NetworkDeviceDriver::getVersion()
{
	return NETXMS_VERSION_STRING;
}

/**
 * Check if given device is supported by driver
 *
 * @param oid Device OID
 */
bool NetworkDeviceDriver::isDeviceSupported(const TCHAR *oid)
{
	return true;
}


/**
 * Do additional checks on the device required by driver.
 * Driver can set device's custom attributes from within
 * this function.
 *
 * @param snmp SNMP transport
 * @param attributes Node's custom attributes
 */
void NetworkDeviceDriver::analyzeDevice(SNMP_Transport *snmp, const TCHAR *oid, StringMap *attributes)
{
}

//
// Handler for enumerating indexes
//

static DWORD HandlerIndex(DWORD dwVersion, SNMP_Variable *pVar, SNMP_Transport *pTransport, void *pArg)
{
	INTERFACE_INFO info;
	memset(&info, 0, sizeof(INTERFACE_INFO));
	info.dwIndex = pVar->GetValueAsUInt();
	((InterfaceList *)pArg)->add(&info);
   return SNMP_ERR_SUCCESS;
}


//
// Handler for enumerating IP addresses
//

static DWORD HandlerIpAddr(DWORD dwVersion, SNMP_Variable *pVar,
                           SNMP_Transport *pTransport, void *pArg)
{
   DWORD dwIndex, dwNetMask, dwNameLen, dwResult;
   DWORD oidName[MAX_OID_LEN];

   dwNameLen = pVar->GetName()->Length();
   memcpy(oidName, pVar->GetName()->GetValue(), dwNameLen * sizeof(DWORD));
   oidName[dwNameLen - 5] = 3;  // Retrieve network mask for this IP
   dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen, &dwNetMask, sizeof(DWORD), 0);
   if (dwResult != SNMP_ERR_SUCCESS)
      return dwResult;

   oidName[dwNameLen - 5] = 2;  // Retrieve interface index for this IP
   dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen, &dwIndex, sizeof(DWORD), 0);
   if (dwResult == SNMP_ERR_SUCCESS)
   {
		InterfaceList *ifList = (InterfaceList *)pArg;

		for(int i = 0; i < ifList->getSize(); i++)
         if (ifList->get(i)->dwIndex == dwIndex)
         {
            if (ifList->get(i)->dwIpAddr != 0)
            {
               // This interface entry already filled, so we have additional IP addresses
               // on a single interface
					INTERFACE_INFO iface;
					memcpy(&iface, ifList->get(i), sizeof(INTERFACE_INFO));
					iface.dwIpAddr = ntohl(pVar->GetValueAsUInt());
					iface.dwIpNetMask = dwNetMask;
					ifList->add(&iface);
            }
				else
				{
					ifList->get(i)->dwIpAddr = ntohl(pVar->GetValueAsUInt());
					ifList->get(i)->dwIpNetMask = dwNetMask;
				}
            break;
         }
   }
   return dwResult;
}

/**
 * Get list of interfaces for given node
 *
 * @param snmp SNMP transport
 * @param attributes Node's custom attributes
 */
InterfaceList *NetworkDeviceDriver::getInterfaces(SNMP_Transport *snmp, StringMap *attributes, int useAliases, bool useIfXTable)
{
   LONG i, iNumIf;
   TCHAR szOid[128], szBuffer[256];
   InterfaceList *pIfList = NULL;
   BOOL bSuccess = FALSE;

   // Get number of interfaces
	if (SnmpGet(snmp->getSnmpVersion(), snmp, _T(".1.3.6.1.2.1.2.1.0"), NULL, 0,
                &iNumIf, sizeof(LONG), 0) != SNMP_ERR_SUCCESS)
      return NULL;
      
   // Create empty list
   pIfList = new InterfaceList(iNumIf);

   // Gather interface indexes
   if (SnmpEnumerate(snmp->getSnmpVersion(), snmp, _T(".1.3.6.1.2.1.2.2.1.1"), HandlerIndex, pIfList, FALSE) == SNMP_ERR_SUCCESS)
   {
      // Enumerate interfaces
		for(i = 0; i < pIfList->getSize(); i++)
      {
			INTERFACE_INFO *iface = pIfList->get(i);

         // Get interface alias if needed
         if (useAliases > 0)
         {
		      _sntprintf(szOid, 128, _T(".1.3.6.1.2.1.31.1.1.1.18.%d"), iface->dwIndex);
				if (SnmpGet(snmp->getSnmpVersion(), snmp, szOid, NULL, 0,
		                  iface->szName, MAX_DB_STRING, 0) != SNMP_ERR_SUCCESS)
				{
					iface->szName[0] = 0;		// It's not an error if we cannot get interface alias
				}
				else
				{
					StrStrip(iface->szName);
				}
         }

			// Try to get interface name from ifXTable, if unsuccessful or disabled, use ifTable
         _sntprintf(szOid, 128, _T(".1.3.6.1.2.1.31.1.1.1.1.%d"), iface->dwIndex);
         if (!useIfXTable ||
				 (SnmpGet(snmp->getSnmpVersion(), snmp, szOid, NULL, 0, szBuffer, 256, 0) != SNMP_ERR_SUCCESS))
         {
		      _sntprintf(szOid, 128, _T(".1.3.6.1.2.1.2.2.1.2.%d"), iface->dwIndex);
		      if (SnmpGet(snmp->getSnmpVersion(), snmp, szOid, NULL, 0, szBuffer, 256, 0) != SNMP_ERR_SUCCESS)
		         break;
		   }

			// Build full interface object name
         switch(useAliases)
         {
         	case 1:	// Use only alias if available, otherwise name
         		if (iface->szName[0] == 0)
	         		nx_strncpy(iface->szName, szBuffer, MAX_DB_STRING);	// Alias is empty or not available
         		break;
         	case 2:	// Concatenate alias with name
         	case 3:	// Concatenate name with alias
         		if (iface->szName[0] != 0)
         		{
						if (useAliases == 2)
						{
         				if  (_tcslen(iface->szName) < (MAX_DB_STRING - 3))
         				{
		      				_sntprintf(&iface->szName[_tcslen(iface->szName)], MAX_DB_STRING - _tcslen(iface->szName), _T(" (%s)"), szBuffer);
		      				iface->szName[MAX_DB_STRING - 1] = 0;
		      			}
						}
						else
						{
							TCHAR temp[MAX_DB_STRING];

							_tcscpy(temp, iface->szName);
		         		nx_strncpy(iface->szName, szBuffer, MAX_DB_STRING);
         				if  (_tcslen(iface->szName) < (MAX_DB_STRING - 3))
         				{
		      				_sntprintf(&iface->szName[_tcslen(iface->szName)], MAX_DB_STRING - _tcslen(iface->szName), _T(" (%s)"), temp);
		      				iface->szName[MAX_DB_STRING - 1] = 0;
		      			}
						}
         		}
         		else
         		{
	         		nx_strncpy(iface->szName, szBuffer, MAX_DB_STRING);	// Alias is empty or not available
					}
         		break;
         	default:	// Use only name
         		nx_strncpy(iface->szName, szBuffer, MAX_DB_STRING);
         		break;
         }

         // Interface type
         _sntprintf(szOid, 128, _T(".1.3.6.1.2.1.2.2.1.3.%d"), iface->dwIndex);
         if (SnmpGet(snmp->getSnmpVersion(), snmp, szOid, NULL, 0,
                      &iface->dwType, sizeof(DWORD), 0) != SNMP_ERR_SUCCESS)
			{
				iface->dwType = IFTYPE_OTHER;
			}

         // MAC address
         _sntprintf(szOid, 128, _T(".1.3.6.1.2.1.2.2.1.6.%d"), iface->dwIndex);
         memset(szBuffer, 0, MAC_ADDR_LENGTH);
         if (SnmpGet(snmp->getSnmpVersion(), snmp, szOid, NULL, 0, szBuffer, 256, SG_RAW_RESULT) == SNMP_ERR_SUCCESS)
			{
	         memcpy(iface->bMacAddr, szBuffer, MAC_ADDR_LENGTH);
			}
			else
			{
				// Unable to get MAC address
	         memset(iface->bMacAddr, 0, MAC_ADDR_LENGTH);
			}
      }

      // Interface IP address'es and netmasks
      if (SnmpEnumerate(snmp->getSnmpVersion(), snmp, _T(".1.3.6.1.2.1.4.20.1.1"),
                        HandlerIpAddr, pIfList, FALSE) == SNMP_ERR_SUCCESS)
      {
         bSuccess = TRUE;
      }
   }

   if (!bSuccess)
   {
      delete_and_null(pIfList);
   }

   return pIfList;
}
