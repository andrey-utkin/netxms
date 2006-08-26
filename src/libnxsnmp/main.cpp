/* 
** NetXMS - Network Management System
** SNMP support library
** Copyright (C) 2003, 2004 Victor Kirhenshtein
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
** $module: main.cpp
**
**/

#include "libnxsnmp.h"


//
// Convert OID to text
//

void LIBNXSNMP_EXPORTABLE SNMPConvertOIDToText(DWORD dwLength, DWORD *pdwValue, TCHAR *pszBuffer, DWORD dwBufferSize)
{
   DWORD i, dwBufPos, dwNumChars;

   pszBuffer[0] = 0;
   for(i = 0, dwBufPos = 0; (i < dwLength) && (dwBufPos < dwBufferSize); i++)
   {
      dwNumChars = _sntprintf(&pszBuffer[dwBufPos], dwBufferSize - dwBufPos, _T(".%d"), pdwValue[i]);
      dwBufPos += dwNumChars;
   }
}


//
// Parse OID in text into binary format
// Will return 0 if OID is invalid or empty, and OID length (in DWORDs) on success
// Buffer size should be given in number of DWORDs
//

DWORD LIBNXSNMP_EXPORTABLE SNMPParseOID(const TCHAR *pszText, DWORD *pdwBuffer, DWORD dwBufferSize)
{
   TCHAR *pCurr = (TCHAR *)pszText, *pEnd, szNumber[32];
   DWORD dwLength = 0;
   int iNumLen;

   if (*pCurr == 0)
      return 0;

   // Skip initial dot if persent
   if (*pCurr == _T('.'))
      pCurr++;

   for(pEnd = pCurr; (*pEnd != 0) && (dwLength < dwBufferSize); pCurr = pEnd + 1)
   {
      for(iNumLen = 0, pEnd = pCurr; (*pEnd >= _T('0')) && (*pEnd <= _T('9')); pEnd++, iNumLen++);
      if ((iNumLen > 15) || ((*pEnd != _T('.')) && (*pEnd != 0)))
         return 0;   // Number is definitely too large or not a number
      memcpy(szNumber, pCurr, sizeof(TCHAR) * iNumLen);
      szNumber[iNumLen] = 0;
      pdwBuffer[dwLength++] = _tcstoul(szNumber, NULL, 10);
   }
   return dwLength;
}


//
// Check if given OID is syntaxically correct
//

BOOL LIBNXSNMP_EXPORTABLE SNMPIsCorrectOID(const TCHAR *pszText)
{
   DWORD dwLength, *pdwBuffer;

   pdwBuffer = (DWORD *)malloc(sizeof(DWORD) * MAX_OID_LEN);
   dwLength = SNMPParseOID(pszText, pdwBuffer, MAX_OID_LEN);
   free(pdwBuffer);
   return (dwLength > 0);
}


//
// Get text for libnxsnmp error code
//

const TCHAR LIBNXSNMP_EXPORTABLE *SNMPGetErrorText(DWORD dwError)
{
   static TCHAR *pszErrorText[] =
   {
      _T("Operation completed successfully"),
      _T("Request timed out"),
      _T("Invalid parameters passed to function"),
      _T("Unable to create socket"),
      _T("Communication error"),
      _T("Rrror parsing PDU"),
      _T("No such object"),
      _T("Invalid hostname or IP address"),
      _T("OID is incorrect"),
      _T("Agent error"),
      _T("Unknown variable data type"),
      _T("File I/O error"),
      _T("Invalid file header"),
      _T("Invalid or corrupted file data")
   };

   return ((dwError >= SNMP_ERR_SUCCESS) && (dwError <= SNMP_ERR_BAD_FILE_DATA)) ?
      pszErrorText[dwError] : _T("Unknown error");
}


//
// Resolve text representation of data type to integer value
//

DWORD LIBNXSNMP_EXPORTABLE SNMPResolveDataType(TCHAR *pszType)
{
   static struct
   {
      TCHAR *pszName;
      DWORD dwValue;
   } typeList[] =
   {
      { _T("INT"), ASN_INTEGER },
      { _T("INTEGER"), ASN_INTEGER },
      { _T("STRING"), ASN_OCTET_STRING },
      { _T("OID"), ASN_OBJECT_ID },
      { _T("IPADDR"), ASN_IP_ADDR },
      { _T("COUNTER32"), ASN_COUNTER32 },
      { _T("GAUGE32"), ASN_GAUGE32 },
      { _T("TIMETICKS"), ASN_TIMETICKS },
      { _T("COUNTER64"), ASN_COUNTER64 },
      { _T("UINT32"), ASN_UINTEGER32 },
      { _T("UINTEGER32"), ASN_UINTEGER32 },
      { NULL, 0 }
   };
   int i;

   for(i = 0; typeList[i].pszName != NULL; i++)
      if (!_tcsicmp(typeList[i].pszName, pszType))
         return typeList[i].dwValue;
   return ASN_NULL;
}


//
// DLL entry point
//

#ifdef _WIN32

#ifndef UNDER_CE // FIXME
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
   if (dwReason == DLL_PROCESS_ATTACH)
      DisableThreadLibraryCalls(hInstance);
   return TRUE;
}
#endif // UNDER_CE

#endif   /* _WIN32 */
