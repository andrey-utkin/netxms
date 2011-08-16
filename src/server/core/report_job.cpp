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
** File: report_job.cpp
**
**/

#include "nxcore.h"

#ifdef _WIN32
#define write _write
#define close _close
#endif


//
// Constructor
//

ReportJob::ReportJob(Report *report, StringMap *parameters, DWORD userId) 
	: ServerJob(_T("EXECUTE_REPORT"), _T("Execute report"), g_dwMgmtNode, userId, false)
{
	m_parameters = parameters;
	m_definition = _tcsdup(report->getDefinition());

	TCHAR buffer[1024];
	_sntprintf(buffer, 1024, _T("Execute report %s"), report->Name());
	setDescription(buffer);
}


//
// Destructor
//

ReportJob::~ReportJob()
{
	safe_free(m_definition);
	delete m_parameters;
}


//
// Run report
//

bool ReportJob::run()
{
	if ((m_definition == NULL) || (m_definition[0] == 0))
	{
		setFailureMessage(_T("Report definition is missing or invalid"));
		return false;
	}

	// g_szConfigFile
	// tmp-xml-file
	// buildDataFileName

	char *definition;
#ifdef UNICODE
	definition = UTF8StringFromWideString(m_definition);
#else
	definition = (char *)m_definition;
#endif

#ifdef _WIN32
	TCHAR definitionFileName[MAX_PATH] = _T("C:\\");
	GetTempPath(MAX_PATH - 32, definitionFileName);
	if (definitionFileName[_tcslen(definitionFileName) - 1] == _T('\\'))
		_tcscat(definitionFileName, _T("nxreport.XXXXXX"));
	else
		_tcscat(definitionFileName, _T("\\nxreport.XXXXXX"));
#else
	TCHAR definitionFileName[] = _T("/tmp/nxreport.XXXXXX");
#endif
	int fd = _tmkstemp(definitionFileName);

	// dump report definition
	write(fd, definition, strlen(definition));
	write(fd, "\n### END OF REPORT DEFINITION\n", 29);

	// dump all variables
	for(int i = 0; i < (int)m_parameters->getSize(); i++)
	{
#ifdef UNICODE
		char *key = UTF8StringFromWideString(m_parameters->getKeyByIndex(i));
		char *value = UTF8StringFromWideString(m_parameters->getValueByIndex(i));
#else
		const char *key = m_parameters->getKeyByIndex(i);
		const char *value = m_parameters->getValueByIndex(i);
#endif
		write(fd, key, strlen(key));
		write(fd, "=", 1);
		write(fd, value, strlen(value));
		write(fd, "\n", 1);

#ifdef UNICODE
		safe_free(key);
		safe_free(value);
#endif
	}

	close(fd);

#ifdef UNICODE
	safe_free(definition);
#endif

	// launch report generator
	TCHAR destFileName[256];
	buildDataFileName(getId(), destFileName, 256);
	TCHAR buffer[1024];
	_sntprintf(buffer, 1024, _T("%s -cp %s") FS_PATH_SEPARATOR _T("report-generator.jar org.netxms.report.Generator %s %s %s"),
			g_szJavaPath,
			g_szDataDir,
			g_szConfigFile,
			definitionFileName,
			destFileName);

	bool ret = (_tsystem(buffer) == 0);

	_tunlink(definitionFileName);

	return ret;
}


//
// Build name of data file
//

TCHAR *ReportJob::buildDataFileName(DWORD jobId, TCHAR *buffer, size_t bufferSize)
{
	_sntprintf(buffer, bufferSize, _T("%s") DDIR_REPORTS FS_PATH_SEPARATOR _T("job_%u"), g_szDataDir, jobId);
	return buffer;
}
